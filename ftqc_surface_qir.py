#!/usr/bin/env python3
"""
Lower fault-tolerant quantum circuits (FTQC) surface code encoded to
QIR (Quantum Intermediate Representation) Base Profile LLVM IR.

The FTQC dialect MLIR is lowered via the `--ftqc-lower-to-qir` pass
in ftqc-opt.  The resulting .ll file is written to ./out/.

Supported Circuits
------------------
  1q_h       : 1-qubit Hadamard
               |0_L> --- H --- Measure  [Expected: 50% |0_L>, 50% |1_L>]
  
  2q_bell    : 2-qubit Bell state
               |00_L> ─── HI ─── CNOT ─── Measure⊗Measure
               [Expected: 50% |00_L>, 50% |11_L>]
  
  4q_ghz     : 4-qubit GHZ state
               |0000_L> ─── H ─── CNOT-chain ─── Measure
               [Expected: 50% |0000_L>, 50% |1111_L>]
  
  3q_grover  : 3-qubit Grover search (search for |111_L>)
               Initialize superposition, apply oracle & diffusion,
               measure all qubits. [Expected: high probability |111_L>]
  
  3q_qft     : 3-qubit Quantum Fourier Transform
               Applies QFT circuit with H and controlled phase gates.
               [Expected: QFT of input |000_L>]

Usage
-----
  # Default circuit (1q_h) with default distance d=3:
  python ftqc_surface_qir.py
  
  # Circuit selection with distance:
  python ftqc_surface_qir.py --circuit 2q_bell --distance 5
  python ftqc_surface_qir.py --circuit 4q_ghz --distance 7
  python ftqc_surface_qir.py --circuit 3q_grover --distance 3
  python ftqc_surface_qir.py --circuit 3q_qft --distance 5
  
  # Other options available:
  python ftqc_surface_qir.py --circuit 2q_bell --distance 5 --qir-out out/surface_2q_bell.ll
  python ftqc_surface_qir.py --include-comments
  python ftqc_surface_qir.py --list-circuits 
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


# ============================================================================
# Circuit Definitions (FTQC Dialect MLIR with Surface Code)
# ============================================================================

def _ftqc_mlir_1q_h(distance: int) -> tuple[str, str]:
    """1-qubit Hadamard: |0_L> -> |+_L> = (|0_L> + |1_L>)/sqrt(2)"""
    lq_type = f"!ftqc.logical_qubit<#ftqc.ecc<surface, {distance}>>"
    mlir = textwrap.dedent(f"""\
        // Surface code (distance {distance}) encoded 1-qubit Hadamard circuit.
        // Logical circuit:  |0_L> --- H --- Measure
        // Expected: P(|0_L>) = 50%, P(|1_L>) = 50%

        module @surface_1q_h {{
          func.func @surface_1q_h() -> i1 {{
            %lq = ftqc.init_zero : {lq_type}
            %h  = ftqc.logical_h %lq : {lq_type}
            %b  = ftqc.logical_measure %h : ({lq_type}) -> i1
            func.return %b : i1
          }}
        }}
    """)
    return mlir, f"1-qubit Hadamard (surface d={distance})"


def _ftqc_mlir_2q_bell(distance: int) -> tuple[str, str]:
    """2-qubit Bell state: (|00_L> + |11_L>)/sqrt(2)"""
    lq_type = f"!ftqc.logical_qubit<#ftqc.ecc<surface, {distance}>>"
    mlir = textwrap.dedent(f"""\
        // Surface code (distance {distance}) encoded 2-qubit Bell state circuit (Φ+).
        // Logical circuit:
        //   q0: |0_L> ─── H ───●─── Measure
        //                      │
        //   q1: |0_L> ────────⊕─── Measure
        // Expected: P(|00_L>) = 50%, P(|11_L>) = 50%

        module @surface_2q_bell {{
          func.func @surface_2q_bell() -> (i1, i1) {{
            // Initialize two logical qubits to |0_L>
            %q0 = ftqc.init_zero : {lq_type}
            %q1 = ftqc.init_zero : {lq_type}
            
            // Apply Hadamard to first qubit: |0_L> -> |+_L>
            %h0 = ftqc.logical_h %q0 : {lq_type}
            
            // Apply logical CNOT: returns (control_out, target_out)
            %cn_ctrl, %cn_tgt = ftqc.logical_cnot %h0, %q1 :
              ({lq_type}, {lq_type}) -> ({lq_type}, {lq_type})
            
            // Measure both qubits
            %m0 = ftqc.logical_measure %cn_ctrl : ({lq_type}) -> i1
            %m1 = ftqc.logical_measure %cn_tgt : ({lq_type}) -> i1
            
            func.return %m0, %m1 : i1, i1
          }}
        }}
    """)
    return mlir, f"2-qubit Bell state (surface d={distance})"


def _ftqc_mlir_4q_ghz(distance: int) -> tuple[str, str]:
    """4-qubit GHZ state: (|0000_L> + |1111_L>)/sqrt(2)"""
    lq_type = f"!ftqc.logical_qubit<#ftqc.ecc<surface, {distance}>>"
    mlir = textwrap.dedent(f"""\
        // Surface code (distance {distance}) encoded 4-qubit GHZ state circuit.
        // Logical circuit:
        //   |0_L> ─── H ───●──────────●──── Measure
        //                  │          │
        //   |0_L> ────────⊕───●──────┼──── Measure
        //                     │      │
        //   |0_L> ───────────⊕───●───┼──── Measure
        //                        │   │
        //   |0_L> ──────────────⊕───┼──── Measure
        // Expected: P(|0000_L>) = 50%, P(|1111_L>) = 50%

        module @surface_4q_ghz {{
          func.func @surface_4q_ghz() -> (i1, i1, i1, i1) {{
            // Initialize four logical qubits to |0_L>
            %q0 = ftqc.init_zero : {lq_type}
            %q1 = ftqc.init_zero : {lq_type}
            %q2 = ftqc.init_zero : {lq_type}
            %q3 = ftqc.init_zero : {lq_type}
            
            // Apply Hadamard to first qubit
            %h0 = ftqc.logical_h %q0 : {lq_type}
            
            // CNOT cascade: each returns (ctrl_out, tgt_out)
            %cn01_c, %cn01_t = ftqc.logical_cnot %h0, %q1 :
              ({lq_type}, {lq_type}) -> ({lq_type}, {lq_type})
            %cn12_c, %cn12_t = ftqc.logical_cnot %cn01_t, %q2 :
              ({lq_type}, {lq_type}) -> ({lq_type}, {lq_type})
            %cn23_c, %cn23_t = ftqc.logical_cnot %cn12_t, %q3 :
              ({lq_type}, {lq_type}) -> ({lq_type}, {lq_type})
            
            // Measure all qubits
            %m0 = ftqc.logical_measure %cn01_c : ({lq_type}) -> i1
            %m1 = ftqc.logical_measure %cn12_c : ({lq_type}) -> i1
            %m2 = ftqc.logical_measure %cn23_c : ({lq_type}) -> i1
            %m3 = ftqc.logical_measure %cn23_t : ({lq_type}) -> i1
            
            func.return %m0, %m1, %m2, %m3 : i1, i1, i1, i1
          }}
        }}
    """)
    return mlir, f"4-qubit GHZ state (surface d={distance})"


def _ftqc_mlir_3q_grover(distance: int) -> tuple[str, str]:
    """3-qubit Grover search for |111_L>"""
    lq_type = f"!ftqc.logical_qubit<#ftqc.ecc<surface, {distance}>>"
    mlir = textwrap.dedent(f"""\
        // Surface code (distance {distance}) encoded 3-qubit Grover's algorithm.
        // Searching for marked state |111_L>
        // 
        // Algorithm:
        //   1. Initialize to |+++_L> (H on all)
        //   2. Oracle: mark |111_L> by phase flip (Z on all)
        //   3. Diffusion operator: 2|s><s| - I
        //   4. Measure all qubits

        module @surface_3q_grover {{
          func.func @surface_3q_grover() -> (i1, i1, i1) {{
            // Initialize three logical qubits to |0_L>
            %q0 = ftqc.init_zero : {lq_type}
            %q1 = ftqc.init_zero : {lq_type}
            %q2 = ftqc.init_zero : {lq_type}
            
            // Step 1: Initialize superposition with Hadamard on all qubits
            %h0 = ftqc.logical_h %q0 : {lq_type}
            %h1 = ftqc.logical_h %q1 : {lq_type}
            %h2 = ftqc.logical_h %q2 : {lq_type}
            
            // Step 2: Oracle - mark |111_L> by applying Z gates
            // (Z on all qubits implements the oracle for this state)
            %z0 = ftqc.logical_z %h0 : {lq_type}
            %z1 = ftqc.logical_z %h1 : {lq_type}
            %z2 = ftqc.logical_z %h2 : {lq_type}
            
            // Step 3: Diffusion operator - Hadamard layer
            %dh0 = ftqc.logical_h %z0 : {lq_type}
            %dh1 = ftqc.logical_h %z1 : {lq_type}
            %dh2 = ftqc.logical_h %z2 : {lq_type}
            
            // Step 4: Measure all qubits
            %m0 = ftqc.logical_measure %dh0 : ({lq_type}) -> i1
            %m1 = ftqc.logical_measure %dh1 : ({lq_type}) -> i1
            %m2 = ftqc.logical_measure %dh2 : ({lq_type}) -> i1
            
            func.return %m0, %m1, %m2 : i1, i1, i1
          }}
        }}
    """)
    return mlir, f"3-qubit Grover search (surface d={distance}, marked state: |111_L>)"


def _ftqc_mlir_3q_qft(distance: int) -> tuple[str, str]:
    """3-qubit Quantum Fourier Transform"""
    lq_type = f"!ftqc.logical_qubit<#ftqc.ecc<surface, {distance}>>"
    mlir = textwrap.dedent(f"""\
        // Surface code (distance {distance}) encoded 3-qubit Quantum Fourier Transform (QFT).
        // 
        // QFT circuit (input |000_L>):
        //   q0: ─── H ─── CZ(pi/2)─ q1 ─── CZ(pi/4)─ q2 ─────── Measure
        //   q1: ────────────────────┘     ──────────┘  
        //   q2: ──────────────────── H ─── CZ(pi/2)─── Measure
        // (Simplified; full QFT uses rotations with 2^k phases)

        module @surface_3q_qft {{
          func.func @surface_3q_qft() -> (i1, i1, i1) {{
            // Initialize three logical qubits to |0_L>
            %q0 = ftqc.init_zero : {lq_type}
            %q1 = ftqc.init_zero : {lq_type}
            %q2 = ftqc.init_zero : {lq_type}
            
            // QFT Layer 1: Hadamard on qubit 0
            %h0 = ftqc.logical_h %q0 : {lq_type}
            
            // QFT Layer 2: CZ(q0, q1) and CZ(q0, q2)
            %cz01_0, %cz01_1 = ftqc.logical_cz %h0, %q1 :
              ({lq_type}, {lq_type}) -> ({lq_type}, {lq_type})
            %cz02_0, %cz02_2 = ftqc.logical_cz %cz01_0, %q2 :
              ({lq_type}, {lq_type}) -> ({lq_type}, {lq_type})
            
            // QFT Layer 3: Hadamard on qubit 1
            %h1 = ftqc.logical_h %cz01_1 : {lq_type}
            
            // QFT Layer 4: CZ(q1, q2)
            %cz12_1, %cz12_2 = ftqc.logical_cz %h1, %cz02_2 :
              ({lq_type}, {lq_type}) -> ({lq_type}, {lq_type})
            
            // QFT Final Layer: Hadamard on qubit 2
            %h2 = ftqc.logical_h %cz12_2 : {lq_type}
            
            // Measure all qubits
            %m0 = ftqc.logical_measure %cz02_0 : ({lq_type}) -> i1
            %m1 = ftqc.logical_measure %cz12_1 : ({lq_type}) -> i1
            %m2 = ftqc.logical_measure %h2 : ({lq_type}) -> i1
            
            func.return %m0, %m1, %m2 : i1, i1, i1
          }}
        }}
    """)
    return mlir, f"3-qubit Quantum Fourier Transform (surface d={distance})"


# Circuit registry mapping circuit names to generator functions
CIRCUITS: dict[str, callable] = {
    "1q_h": _ftqc_mlir_1q_h,
    "2q_bell": _ftqc_mlir_2q_bell,
    "4q_ghz": _ftqc_mlir_4q_ghz,
    "3q_grover": _ftqc_mlir_3q_grover,
    "3q_qft": _ftqc_mlir_3q_qft,
}

DEFAULT_CIRCUIT = "1q_h"
DEFAULT_DISTANCE = 3


def with_surface_prefix(path: Path, distance: int) -> Path:
  """Ensure output filename starts with 'surface_<d>d_' while preserving directory."""
  required_prefix = f"surface_{distance}d_"
  stem = path.stem

  if stem.startswith(required_prefix):
    return path

  if stem.startswith("surface_"):
    stem = stem[len("surface_"):]

  return path.with_name(f"{required_prefix}{stem}{path.suffix}")


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
            "Lower surface code FTQC circuits to QIR Base Profile LLVM IR."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent("""\
            Available circuits:
              1q_h        - 1-qubit Hadamard (default)
              2q_bell     - 2-qubit Bell state
              4q_ghz      - 4-qubit GHZ state
              3q_grover   - 3-qubit Grover search
              3q_qft      - 3-qubit Quantum Fourier Transform
            
            Examples:
              python ftqc_surface_qir.py
              python ftqc_surface_qir.py --circuit 2q_bell --distance 5
              python ftqc_surface_qir.py --circuit 3q_grover --distance 3 --include-comments
              python ftqc_surface_qir.py --list-circuits
        """),
    )
    parser.add_argument(
        "--circuit",
        type=str,
        choices=list(CIRCUITS.keys()),
        default=DEFAULT_CIRCUIT,
        help=f"Circuit to lower (default: {DEFAULT_CIRCUIT})",
    )
    parser.add_argument(
        "--distance",
        type=int,
        default=DEFAULT_DISTANCE,
        help=f"Surface code distance d (default: {DEFAULT_DISTANCE})",
    )
    parser.add_argument(
        "--qir-out",
        type=Path,
        default=None,
        help=(
        "Output path for the QIR .ll file "
      "(default: out/surface_<d>d_<circuit_name>.ll; filename is auto-prefixed with 'surface_<d>d_')"
        ),
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
    parser.add_argument(
        "--list-circuits",
        action="store_true",
        help="List all available circuits and exit.",
    )
    args = parser.parse_args()

    if args.list_circuits:
        print("Available circuits:")
        print()
        for name, gen_fn in CIRCUITS.items():
            _, desc = gen_fn(DEFAULT_DISTANCE)
            print(f"  {name:15} - {desc}")
        return

    # Validate distance parameter
    if args.distance < 1:
        sys.exit("[ERROR] Surface code distance must be >= 1")

    # Generate FTQC MLIR for the selected circuit
    circuit_name = args.circuit
    gen_fn = CIRCUITS[circuit_name]
    ftqc_mlir, circuit_desc = gen_fn(args.distance)

    # Determine output file path
    if args.qir_out is None:
      qir_out = OUT_DIR / f"surface_{args.distance}d_{circuit_name}.ll"
    else:
      qir_out = with_surface_prefix(args.qir_out, args.distance)

    qir_out.parent.mkdir(parents=True, exist_ok=True)
    
    print("=" * 78)
    print(f"FTQC Dialect MLIR (Surface Code d={args.distance}):")
    print("=" * 78)
    print(ftqc_mlir.strip())

    print()
    print(f"Lowering to QIR via ftqc-opt --ftqc-lower-to-qir ...")
    qir_text = run_ftqc_opt(ftqc_mlir, qir_out, args.include_comments)

    print()
    print("=" * 78)
    print("Output: QIR Base Profile LLVM IR")
    print("=" * 78)
    print(qir_text)

    print()
    print(f"Wrote to {qir_out}")


if __name__ == "__main__":
    main()
