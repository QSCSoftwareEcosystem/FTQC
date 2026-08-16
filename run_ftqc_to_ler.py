#!/usr/bin/env python3
"""One-command FTQC MLIR -> LightStim LER pipeline.

Pipeline stages:
  1) FTQC canonical atomic IR emission (v2 timeline IR)
  2) FTQC atomic descriptor emission (v2 executable descriptor)
  3) LightStim simulation run + LER summary extraction

This script is intentionally thin orchestration over existing tools:
  - ftqc-opt passes
  - run_lightstim_descriptor.py runner
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class PipelineError(RuntimeError):
    """Raised when one stage of the integrated FTQC->LER pipeline fails."""


@dataclass(frozen=True)
class CmdResult:
    cmd: list[str]
    stdout: str
    stderr: str


def _format_pass_options(options: dict[str, Any]) -> str:
    """Format mlir pass options as `k=v k=v` string."""
    parts: list[str] = []
    for key, value in options.items():
        if isinstance(value, bool):
            parts.append(f"{key}={'true' if value else 'false'}")
        else:
            parts.append(f"{key}={value}")
    return " ".join(parts)


def run_checked(cmd: list[str], *, cwd: Path | None = None) -> CmdResult:
    proc = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise PipelineError(
            "Command failed with exit code "
            f"{proc.returncode}: {' '.join(cmd)}\n"
            f"--- stdout ---\n{proc.stdout}\n"
            f"--- stderr ---\n{proc.stderr}"
        )
    return CmdResult(cmd=cmd, stdout=proc.stdout, stderr=proc.stderr)


def _extract_value(pattern: str, text: str, cast: type) -> Any:
    match = re.search(pattern, text, flags=re.MULTILINE)
    if not match:
        return None
    return cast(match.group(1))


def parse_ler_report_text(output_text: str) -> dict[str, Any]:
    """Parse key simulation metrics from run_lightstim_descriptor output."""
    report: dict[str, Any] = {
        "experiment": _extract_value(r"^experiment:\s*(.+)$", output_text, str),
        "decoder": _extract_value(r"^decoder:\s*(.+)$", output_text, str),
        "shots": _extract_value(r"^shots:\s*(\d+)$", output_text, int),
        "post_selected_shots": _extract_value(r"^post-selected shots:\s*(\d+)$", output_text, int),
        "logical_errors": _extract_value(r"^logical errors:\s*(\d+)$", output_text, int),
    }

    ler_match = re.search(
        r"^LER:\s*([0-9eE+\-.]+)\s*\+/-\s*([0-9eE+\-.]+)",
        output_text,
        flags=re.MULTILINE,
    )
    if ler_match:
        report["logical_error_rate"] = float(ler_match.group(1))
        report["logical_error_rate_error_bar_95"] = float(ler_match.group(2))
    else:
        report["logical_error_rate"] = None
        report["logical_error_rate_error_bar_95"] = None

    return report


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    ftqc_root = repo_root / "ftqc"

    parser = argparse.ArgumentParser(description="Run FTQC MLIR -> LightStim -> LER in one command.")
    parser.add_argument("mlir_input", type=Path, help="Path to FTQC MLIR input file.")
    parser.add_argument(
        "--ftqc-opt",
        type=Path,
        default=ftqc_root / "build" / "src" / "ftqc-opt",
        help="Path to ftqc-opt binary.",
    )
    parser.add_argument(
        "--python",
        type=Path,
        default=Path(sys.executable),
        help="Python executable used to run integrate/run_lightstim_descriptor.py.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "out",
        help="Output directory for emitted descriptor/artifacts.",
    )

    parser.add_argument("--patch-spacing", type=int, default=10)
    parser.add_argument("--default-se-rounds", type=int, default=1)
    parser.add_argument("--surgery-rounds", type=int, default=5)
    parser.add_argument("--physical-error-rate", type=float, default=1e-3)
    parser.add_argument("--decoder", default="pymatching")
    parser.add_argument("--decoder-backend", default="cpu")
    parser.add_argument("--max-errors", type=int, default=100)
    parser.add_argument("--max-shots", type=int, default=1_000_000)
    parser.add_argument("--batch-size", type=int, default=10_000)
    parser.add_argument("--num-workers", type=int, default=4)
    parser.add_argument("--noiseless-check-shots", type=int, default=100)

    parser.add_argument(
        "--skip-noiseless-check",
        action="store_true",
        help="Forwarded to run_lightstim_descriptor.py.",
    )
    parser.add_argument(
        "--progress",
        action="store_true",
        help="Forwarded to run_lightstim_descriptor.py.",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_args()

    mlir_input = args.mlir_input.resolve()
    if not mlir_input.exists():
        raise PipelineError(f"MLIR input does not exist: {mlir_input}")

    ftqc_opt = args.ftqc_opt.expanduser().resolve()
    if not ftqc_opt.exists():
        raise PipelineError(f"ftqc-opt binary not found: {ftqc_opt}")

    py_exe = args.python.expanduser()
    if not py_exe.exists():
        raise PipelineError(f"Python executable not found: {py_exe}")

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    stem = mlir_input.stem
    atomic_ir_json = out_dir / f"{stem}.atomic_ir.json"
    atomic_ir_mlir = out_dir / f"{stem}.atomic_ir.mlir"
    atomic_desc_json = out_dir / f"{stem}.atomic_descriptor.json"
    atomic_desc_mlir = out_dir / f"{stem}.atomic_descriptor.mlir"
    ler_report_json = out_dir / f"{stem}.ler_report.json"

    canonical_opts = _format_pass_options(
        {
            "output-file": atomic_ir_json,
            "patch-spacing": args.patch_spacing,
            "default-se-rounds": args.default_se_rounds,
            "surgery-rounds": args.surgery_rounds,
            "attach-module-attr": True,
        }
    )
    canonical_cmd = [
        str(ftqc_opt),
        str(mlir_input),
        f"--ftqc-canonicalize-to-lightstim-atomic={canonical_opts}",
    ]
    canonical_res = run_checked(canonical_cmd)
    atomic_ir_mlir.write_text(canonical_res.stdout, encoding="utf-8")

    lower_opts = _format_pass_options(
        {
            "output-file": atomic_desc_json,
            "patch-spacing": args.patch_spacing,
            "default-se-rounds": args.default_se_rounds,
            "surgery-rounds": args.surgery_rounds,
            "physical-error-rate": args.physical_error_rate,
            "decoder": args.decoder,
            "decoder-backend": args.decoder_backend,
            "max-errors": args.max_errors,
            "max-shots": args.max_shots,
            "batch-size": args.batch_size,
            "num-workers": args.num_workers,
            "noiseless-check-shots": args.noiseless_check_shots,
            "attach-module-attr": True,
        }
    )
    lower_cmd = [
        str(ftqc_opt),
        str(mlir_input),
        f"--ftqc-lower-to-lightstim-atomic-json={lower_opts}",
    ]
    lower_res = run_checked(lower_cmd)
    atomic_desc_mlir.write_text(lower_res.stdout, encoding="utf-8")

    run_script = Path(__file__).resolve().parent / "run_lightstim_descriptor.py"
    if not run_script.exists():
        raise PipelineError(f"Descriptor runner not found: {run_script}")

    run_cmd = [str(py_exe), str(run_script), str(atomic_desc_json)]
    if args.skip_noiseless_check:
        run_cmd.append("--skip-noiseless-check")
    if args.progress:
        run_cmd.append("--progress")

    run_res = run_checked(run_cmd, cwd=Path(__file__).resolve().parent)

    # Preserve runner output on stdout for interactive workflow UX.
    if run_res.stdout:
        print(run_res.stdout, end="")

    parsed = parse_ler_report_text(run_res.stdout)
    report = {
        "pipeline": {
            "mlir_input": str(mlir_input),
            "atomic_ir_json": str(atomic_ir_json),
            "atomic_ir_mlir": str(atomic_ir_mlir),
            "atomic_descriptor_json": str(atomic_desc_json),
            "atomic_descriptor_mlir": str(atomic_desc_mlir),
            "ler_report_json": str(ler_report_json),
            "ftqc_opt": str(ftqc_opt),
            "python": str(py_exe),
        },
        "runtime": {
            "patch_spacing": args.patch_spacing,
            "default_se_rounds": args.default_se_rounds,
            "surgery_rounds": args.surgery_rounds,
            "physical_error_rate": args.physical_error_rate,
            "decoder": args.decoder,
            "decoder_backend": args.decoder_backend,
            "max_errors": args.max_errors,
            "max_shots": args.max_shots,
            "batch_size": args.batch_size,
            "num_workers": args.num_workers,
            "noiseless_check_shots": args.noiseless_check_shots,
            "skip_noiseless_check": args.skip_noiseless_check,
        },
        "simulation": parsed,
        "raw_runner_stdout": run_res.stdout,
        "raw_runner_stderr": run_res.stderr,
    }

    ler_report_json.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(f"pipeline artifacts written under: {out_dir}")
    print(f"ler report: {ler_report_json}")


if __name__ == "__main__":
    main()
