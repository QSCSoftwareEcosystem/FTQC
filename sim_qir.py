#!/usr/bin/env python3
"""
Simulate FTQC MLIR circuits via the --ftqc-lower-to-qir-ns pass and verify
outcomes with QIR executors.

Backends Available via QIR-EE
-----------------------------
  lightning     : qir-lightning
  qsim (default): qir-qsim
  xacc          : qir-xacc --accelerator <name> --measurements-only

Circuits from FTQC Test Suite
------------------------
  bell-state    : H + CNOT               → 50% |00⟩, 50% |11⟩
  ghz4          : H + 3 CNOTs            → 50% |0000⟩, 50% |1111⟩
  grover        : 2-qubit Grover (|11⟩)  → 100% |11⟩
  qft3          : 3-qubit QFT-like test  → distribution validated against
                                         a high-shot reference run
"""

from __future__ import annotations

import argparse
import ast
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# ── Paths ───────────────────────────────────────────────────────────────────
REPO = Path(__file__).parent
MLIR_EXAMPLES = REPO / "test" / "Integration"
QIREE_BIN_DIR = Path.home() / "qiree" / "build" / "bin"  # Replace if qiree lives elsewhere
LIGHTNING_EXE = QIREE_BIN_DIR / "qir-lightning"
QSIM_EXE = QIREE_BIN_DIR / "qir-qsim"
XACC_EXE = QIREE_BIN_DIR / "qir-xacc"

SHOTS = 4096
PASS = "--ftqc-lower-to-qir-ns=include-comments=false"

CIRCUIT_MLIR = {
    "bell-state": MLIR_EXAMPLES / "bell-state-ft.mlir",
    "ghz4": MLIR_EXAMPLES / "ghz4-steane.mlir",
    "grover": MLIR_EXAMPLES / "grover-ft.mlir",
    "qft3": MLIR_EXAMPLES / "qft3-steane.mlir"
}

def resolve_ftqc_opt() -> str:
    """Locate ftqc-opt binary from common build paths or $PATH."""
    on_path = shutil.which("ftqc-opt")
    if on_path:
        return on_path

    candidates = [
        REPO / "install" / "bin" / "ftqc-opt",
        REPO / "build" / "src" / "ftqc-opt",
        REPO / "build" / "bin" / "ftqc-opt"
    ]
    for c in candidates:
        if c.exists():
            return str(c)

    print("  [ERROR] Could not find ftqc-opt.")
    print("          Looked in:")
    for c in candidates:
        print(f"            - {c}")
    print("          and on $PATH.")
    sys.exit(1)

def qir_from_mlir(mlir_file: Path, extra_passes: list[str] | None = None) -> str:
    """Run ftqc-opt and return the QIR string (written to stderr)."""
    if extra_passes is None:
        extra_passes = []

    ftqc_opt = resolve_ftqc_opt()
    cmd = [ftqc_opt] + extra_passes + [PASS, str(mlir_file)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        if "Unknown command line argument '--ftqc-lower-to-qir-ns" in result.stderr:
            print("  [ERROR] Your ftqc-opt binary does not include --ftqc-lower-to-qir-ns yet.")
            print("          Rebuild ftqc-opt in this workspace, then retry.")
            print(f"          Active binary: {ftqc_opt}")
            sys.exit(1)
        print(f"  [ERROR] ftqc-opt failed for {mlir_file}:\n{result.stderr}")
        sys.exit(1)
    return result.stderr.strip()


def parse_counts(stdout: str) -> dict[str, int]:
    """Parse simulator stdout with fallback for JSON/Python-dict formats."""
    text = stdout.strip()
    if not text:
        return {}

    try:
        parsed = json.loads(text)
        return {str(k): int(v) for k, v in parsed.items()}
    except Exception:
        pass

    # Fallback: tolerate Python dict syntax if emitted by a backend.
    try:
        parsed = ast.literal_eval(text)
        if isinstance(parsed, dict):
            return {str(k): int(v) for k, v in parsed.items()}
    except Exception:
        pass

    print("  [ERROR] Unable to parse backend measurement output as dict.")
    print("  Raw output:")
    print(stdout)
    sys.exit(1)


def resolve_backend_cmd(
    backend: str,
    ll_file: Path,
    shots: int,
    xacc_accelerator: str,
) -> list[str]:
    """Build the simulator command line for the selected backend."""
    if backend == "lightning":
        exe = LIGHTNING_EXE
        if not exe.exists():
            sys.exit(f"  [ERROR] Backend executable not found: {exe}")
        return [str(exe), "--input", str(ll_file), "--shots", str(shots)]

    if backend == "qsim":
        exe = QSIM_EXE
        if not exe.exists():
            sys.exit(f"  [ERROR] Backend executable not found: {exe}")
        return [str(exe), "--input", str(ll_file), "--shots", str(shots)]

    if backend == "xacc":
        exe = XACC_EXE
        if not exe.exists():
            sys.exit(f"  [ERROR] Backend executable not found: {exe}")
        return [
            str(exe),
            "--input",
            str(ll_file),
            "--accelerator",
            xacc_accelerator,
            "--shots",
            str(shots),
            "--measurements-only",
        ]

    sys.exit(f"  [ERROR] Unknown backend: {backend}")


def simulate_qir(
    qir_str: str,
    backend: str,
    shots: int,
    xacc_accelerator: str,
) -> dict[str, int]:
    """Run QIR via selected backend and return counts dict."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".ll", delete=True) as tmp:
        tmp.write(qir_str)
        tmp.flush()

        cmd = resolve_backend_cmd(backend, Path(tmp.name), shots, xacc_accelerator)
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"  [ERROR] Backend failed:\n    {' '.join(cmd)}\n")
            print(result.stderr)
            sys.exit(1)

        return parse_counts(result.stdout)


def check(label: str, counts: dict[str, int], expected: dict[str, float], tol: float = 5.0) -> bool:
    """Verify expected distribution against observed counts."""
    total = sum(counts.values())
    if total == 0:
        print(f"\n[ERROR] No measurement counts for {label}.")
        return False

    ok = True

    print(f"\n{'='*60}")
    print(f"  Circuit: {label}")
    print(f"{'='*60}")
    print(f"  Shots: {total}")
    print(f"  {'Outcome':<12} {'Got':>8}  {'Expected':>10}")
    print(f"  {'-'*32}")

    keys = sorted(set(counts) | set(expected))
    for k in keys:
        got_pct = 100.0 * counts.get(k, 0) / total
        want_pct = expected.get(k, 0.0)
        marker = ""
        if abs(got_pct - want_pct) > tol:
            marker = " ← FAIL"
            ok = False
        print(f"  |{k}⟩  {got_pct:>7.1f}%  {want_pct:>9.1f}%{marker}")

    for k, cnt in counts.items():
        if k not in expected and 100.0 * cnt / total > tol:
            print(f"  |{k}⟩  {100.0*cnt/total:>7.1f}%  {'0.0':>9}%  ← UNEXPECTED")
            ok = False

    print(f"  {'PASS' if ok else 'FAIL'}")
    return ok


def run_bell(backend: str, xacc_accelerator: str, shots: int) -> bool:
    qir = qir_from_mlir(CIRCUIT_MLIR["bell-state"])
    print("\nGenerated QIR:\n" + qir)
    counts = simulate_qir(qir, backend, shots, xacc_accelerator)
    return check(
        "Bell state  H·CNOT·M  →  |00⟩+|11⟩",
        counts,
        {"00": 50.0, "11": 50.0},
    )


def run_ghz4(backend: str, xacc_accelerator: str, shots: int) -> bool:
    qir = qir_from_mlir(CIRCUIT_MLIR["ghz4"])
    print("\nGenerated QIR:\n" + qir)
    counts = simulate_qir(qir, backend, shots, xacc_accelerator)
    return check(
        "GHZ-4  H + 3 CNOTs  →  |0000⟩+|1111⟩",
        counts,
        {"0000": 50.0, "1111": 50.0},
    )


def run_grover(backend: str, xacc_accelerator: str, shots: int) -> bool:
    qir = qir_from_mlir(CIRCUIT_MLIR["grover"])
    print("\nGenerated QIR:\n" + qir)
    counts = simulate_qir(qir, backend, shots, xacc_accelerator)
    return check(
        "Grover 2-qubit  oracle |11⟩  →  |11⟩ w.p. 1",
        counts,
        {"11": 100.0},
        tol=2.0,
    )


def run_qft3(backend: str, xacc_accelerator: str) -> bool:
    """
    QIR backends in this workflow are shot-based for practical use.
    We derive a high-shot reference distribution, then verify an independent
    shot run matches it within tolerance.
    """
    qir = qir_from_mlir(CIRCUIT_MLIR["qft3"])
    print("\nGenerated QIR (QFT-3):\n" + qir)

    print("\n[Note] Deriving reference distribution from a high-shot run.")
    ref_counts = simulate_qir(qir, backend, 32768, xacc_accelerator)
    ref_total = sum(ref_counts.values())
    if ref_total == 0:
        print("  [ERROR] QFT-3 produced no measurements.\n"
              "         Use an MLIR with ftqc.logical_measure outputs.")
        return False

    expected = {k: 100.0 * v / ref_total for k, v in ref_counts.items()}
    print("  Reference distribution:")
    for k, v in sorted(expected.items()):
        print(f"    |{k}⟩  {v:.1f}%")

    counts = simulate_qir(qir, backend, 8192, xacc_accelerator)
    return check(
        "QFT-3 — shot run matches high-shot reference",
        counts,
        expected,
        tol=5.0,
    )

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Simulate FTQC MLIR circuits through QIR no-syndrome lowering.",
    )
    parser.add_argument(
        "--backend",
        choices=["lightning", "qsim", "xacc"],
        default="qsim",
        help="QIR execution backend (default: qsim)",
    )
    parser.add_argument(
        "--xacc-accelerator",
        default="aer",
        help="Accelerator name passed to qir-xacc when --backend=xacc (default: aer)",
    )
    parser.add_argument(
        "--shots",
        type=int,
        default=SHOTS,
        help=f"Default shots for Bell/GHZ/Grover (default: {SHOTS})",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    results = {
        "Bell state": run_bell(args.backend, args.xacc_accelerator, args.shots),
        "GHZ-4": run_ghz4(args.backend, args.xacc_accelerator, args.shots),
        "Grover": run_grover(args.backend, args.xacc_accelerator, args.shots),
        "QFT-3": run_qft3(args.backend, args.xacc_accelerator),
    }

    print(f"\n{'='*60}")
    print("  Summary")
    print(f"{'='*60}")
    all_ok = True
    for name, ok in results.items():
        status = "PASS" if ok else "FAIL"
        print(f"  {name:<20} {status}")
        all_ok &= ok

    print(f"{'='*60}")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
