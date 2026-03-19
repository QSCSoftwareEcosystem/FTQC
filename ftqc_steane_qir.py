#!/usr/bin/env python3
"""
Lower a 1-qubit Steane [[7,1,3]] fault-tolerant circuit directly to
QIR (Quantum Intermediate Representation) Base Profile LLVM IR.

The FTQC dialect MLIR is lowered via the new `--ftqc-lower-to-qir` pass
in ftqc-opt.  The resulting .ll file is written to ./out/.

Circuit
-------
  Logical |0_L> -- H -- Measure
  Expected: P(|0_L>) = 50%, P(|1_L>) = 50%
  (logical qubit is in |+_L> = (|0_L> + |1_L>) / sqrt(2))

Usage
-----
  python ftqc_steane_qir.py
  python ftqc_steane_qir.py --qir-out out/my_circuit.ll
  python ftqc_steane_qir.py --include-comments
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import textwrap
from pathlib import Path

REPO    = Path(__file__).parent
FTQC_OPT = REPO / "build" / "src" / "ftqc-opt"
OUT_DIR  = REPO / "out"

# Embedded FTQC dialect MLIR for the 1-qubit Steane H example
#
# Logical circuit:  |0_L> ─── H ─── Measure
#
# At the FTQC dialect level this is just three ops; the QIR lowering pass
# maps them directly to QIR runtime calls without needing Steane encoding
# passes (those are for physical-level analysis).  The logical semantics are
# preserved: the output QIR represents a logical 1-qubit H followed by a
# logical measurement.
#
FTQC_MLIR = textwrap.dedent("""\
    // Steane [[7,1,3]] encoded 1-qubit logical H circuit.
    //
    // Logical circuit:
    //   |0_L> --- H --- Measure
    //
    // Expected QIR output: single qubit, single result.
    // Logical measurement outcome: 50% |0_L>, 50% |1_L>
    //   (logical qubit is in the |+_L> superposition after H).

    module @steane_1q_h {
      func.func @steane_1q_h() -> i1 {
        // Initialise logical |0_L>
        %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

        // Apply logical Hadamard: |0_L> -> |+_L> = (|0_L> + |1_L>) / sqrt(2)
        %h = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

        // Logical measurement
        %b = ftqc.logical_measure %h
               : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1

        func.return %b : i1
      }
    }
""")


def run_ftqc_opt(mlir_src: str, qir_out: Path, include_comments: bool) -> str:
    """
    Write mlir_src to a temporary file, invoke ftqc-opt with the
    --ftqc-lower-to-qir pass, and return the generated QIR text.

    The pass writes the .ll directly to the out_dir directory; this function also
    returns the content for display.
    """
    if not FTQC_OPT.exists():
        sys.exit(
            f"[ERROR] ftqc-opt not found at {FTQC_OPT}\n"
            "        Run  cmake --build build --target ftqc-opt  first."
        )

    # Write MLIR to a temp file (avoids shell quoting issues with stdin).
    import tempfile, os
    with tempfile.NamedTemporaryFile(
        suffix=".mlir", mode="w", delete=False, encoding="utf-8"
    ) as tmp:
        tmp.write(mlir_src)
        tmp_path = tmp.name

    try:
        pass_opt = (
            f"output-file={qir_out}"
            f" include-comments={'true' if include_comments else 'false'}"
        )
        cmd = [str(FTQC_OPT), f"--ftqc-lower-to-qir={pass_opt}", tmp_path]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(
                f"[ERROR] ftqc-opt failed (exit {result.returncode}):\n"
                + result.stderr
            )
    finally:
        os.unlink(tmp_path)

    return qir_out.read_text(encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Lower a 1-qubit Steane [[7,1,3]] FTQC circuit to "
            "QIR Base Profile LLVM IR and save the .ll file."
        )
    )
    parser.add_argument(
        "--qir-out",
        type=Path,
        default=OUT_DIR / "steane_1q_h.ll",
        help="Output path for the QIR .ll file (default: out/steane_1q_h.ll)",
    )
    parser.add_argument(
        "--include-comments",
        action="store_true",
        default=False,
        help=(
            "Emit LLVM IR line comments for FT-only ops "
            "(syndrome, correction, Pauli frame)."
        ),
    )
    args = parser.parse_args()

    args.qir_out.parent.mkdir(parents=True, exist_ok=True)

    print("=" * 78)
    print("Input: FTQC dialect MLIR")
    print("=" * 78)
    print(FTQC_MLIR.strip())

    print()
    print(f"Lowering to QIR via ftqc-opt --ftqc-lower-to-qir ...")
    qir_text = run_ftqc_opt(FTQC_MLIR, args.qir_out, args.include_comments)

    print()
    print("=" * 78)
    print("Output: QIR Base Profile LLVM IR")
    print("=" * 78)
    print(qir_text)

    print(f"Wrote {args.qir_out}")


if __name__ == "__main__":
    main()
