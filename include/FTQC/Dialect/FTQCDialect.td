//===- FTQCDialect.td - FTQC Dialect Definition ---------------------------===//
//
// Fault-Tolerant Quantum Computing (FTQC) MLIR Dialect
// Based on PennyLane/Catalyst quantum dialect conventions.
//
// This dialect provides high-level IR abstractions for:
//   - Logical qubits encoded in error-correcting codes
//   - Fault-tolerant gate sets (transversal + magic state injection)
//   - Syndrome extraction and error correction protocols
//   - Magic state distillation (15-to-1, 7-to-1 Bravyi-Kitaev)
//   - Pauli frame tracking and deferred correction
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_DIALECT_TD
#define FTQC_DIALECT_TD

include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/FunctionInterfaces.td"

//===----------------------------------------------------------------------===//
// FTQC Dialect
//===----------------------------------------------------------------------===//

def FTQC_Dialect : Dialect {
  let name = "ftqc";
  let summary = "Fault-Tolerant Quantum Computing MLIR dialect";
  let description = [{
    The `ftqc` dialect provides a higher-level IR layer above the PennyLane
    `quantum` dialect for expressing fault-tolerant quantum computing (FTQC)
    protocols.

    ## Overview

    Physical quantum circuits expressed in the `quantum` dialect operate on
    `!quantum.bit` values with unitary gates. The `ftqc` dialect lifts this
    to operate on *logical* qubits encoded in quantum error-correcting codes
    (QECCs) such as the Steane [[7,1,3]] code or rotated surface codes.

    ## Type Hierarchy

      - `!ftqc.logical_qubit<code>` — A logical qubit protected by `code`
      - `!ftqc.syndrome<n>`        — An n-bit syndrome measurement vector
      - `!ftqc.magic_state`        — A distilled magic state (|T⟩ or |CCZ⟩)
      - `!ftqc.pauli_frame`        — A Pauli correction frame

    ## Supported Codes

      - Steane [[7,1,3]] — CSS code, transversal H/CNOT/X/Z
      - Surface code (distance d) — topological, low overhead
      - Color code (distance d)  — transversal T gate support
      - Concatenated codes       — threshold amplification

    ## Lowering

    The FTQC dialect is lowered to the `quantum` dialect via passes in
    `lib/Transforms/`. The pipeline is:

      ftqc-insert-syndrome → ftqc-insert-correction →
      ftqc-magic-distill  → ftqc-lower-to-quantum
  }];

  let cppNamespace = "::mlir::ftqc";

  let useDefaultTypePrinterParser  = 1;
  let useDefaultAttributePrinterParser = 1;

  let dependentDialects = [
    "mlir::func::FuncDialect",
    "mlir::arith::ArithDialect",
    "mlir::scf::SCFDialect"
  ];

  let extraClassDeclaration = [{
    void registerTypes();
    void registerAttrs();
    void registerOps();
  }];
}

#endif // FTQC_DIALECT_TD
