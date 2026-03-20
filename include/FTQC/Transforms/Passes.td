//===- Passes.td - FTQC Transform Pass Definitions ------------------------===//
//
// TableGen definitions for all FTQC transformation passes.
// Registered via mlir_tablegen(Passes.h.inc -gen-pass-decls ...).
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_PASSES_TD
#define FTQC_PASSES_TD

include "mlir/Pass/PassBase.td"

//===----------------------------------------------------------------------===//
// Encoding Passes
//===----------------------------------------------------------------------===//

def FTQCSteaneEncodePass : Pass<"ftqc-steane-encode", "mlir::ModuleOp"> {
  let summary = "Lower quantum.bit qubits to Steane [[7,1,3]] logical qubits";
  let description = [{
    Scans the IR for `quantum.alloc` operations and replaces each allocated
    physical qubit with a Steane-code logical qubit.  All gates acting on
    the physical qubits are replaced with their transversal logical equivalents.

    Non-Clifford gates (T, Rz) are replaced with `ftqc.logical_t` operations
    requiring magic state injection.

    Options:
      - `distance`: code distance (default 3, gives [[7,1,3]])
      - `insert-ec`: whether to insert EC rounds between gates (default true)
  }];

  let options = [
    Option<"distance", "distance", "unsigned", "3",
           "Code distance d for the Steane code (only 3 is standard)">,
    Option<"insertEC", "insert-ec", "bool", "true",
           "Insert syndrome extraction and correction rounds">,
    Option<"ecRounds", "ec-rounds", "unsigned", "3",
           "Number of syndrome measurement rounds per EC cycle">
  ];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect"
  ];
}

def FTQCSurfaceEncodePass : Pass<"ftqc-surface-encode", "mlir::ModuleOp"> {
  let summary = "Lower quantum.bit qubits to surface-code logical qubits";
  let description = [{
    Replaces physical qubits with distance-d rotated surface-code logical
    qubits.  CNOT gates are replaced with `ftqc.lattice_surgery` operations.
  }];

  let options = [
    Option<"distance", "distance", "unsigned", "3",
           "Surface-code distance d">,
    Option<"useLattice", "use-lattice-surgery", "bool", "true",
           "Use lattice surgery for 2-qubit gates">
  ];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect"
  ];
}

def FTQCColorCodeEncodePass : Pass<"ftqc-color-encode", "mlir::ModuleOp"> {
  let summary = "Lower to color-code logical qubits (supports transversal T)";
  let description = [{
    Encodes in a 2D triangular color code which supports a transversal T gate.
    This avoids the need for magic-state distillation for T gates.
  }];

  let options = [
    Option<"distance", "distance", "unsigned", "3",
           "Color code distance d">
  ];

  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

//===----------------------------------------------------------------------===//
// Syndrome and Error Correction Passes
//===----------------------------------------------------------------------===//

def FTQCInsertSyndromePass : Pass<"ftqc-insert-syndrome", "mlir::func::FuncOp"> {
  let summary = "Insert syndrome extraction operations between logical gates";
  let description = [{
    Inserts `ftqc.extract_syndrome` after every logical gate.  The `method`
    option selects the extraction protocol:

      - "steane"  : Steane transversal ancilla method
      - "shor"    : Shor cat-state ancilla method
      - "flag"    : Chao-Reichardt flag qubit protocol
  }];

  let options = [
    Option<"method", "method", "std::string", "\"steane\"",
           "Syndrome extraction method: steane, shor, flag">,
    Option<"rounds", "rounds", "unsigned", "3",
           "Number of syndrome rounds per EC cycle">
  ];

  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

def FTQCInsertCorrectionPass : Pass<"ftqc-insert-correction", "mlir::func::FuncOp"> {
  let summary = "Insert ftqc.correct after every syndrome extraction";
  let description = [{
    Follows every `ftqc.extract_syndrome` with a `ftqc.correct` to apply
    the minimum-weight-Pauli correction inferred from the syndrome.
  }];

  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

def FTQCPauliFrameOptPass : Pass<"ftqc-pauli-frame-opt", "mlir::func::FuncOp"> {
  let summary = "Replace physical corrections with Pauli-frame tracking";
  let description = [{
    Replaces `ftqc.correct` operations with `ftqc.pauli_frame_update`,
    deferring corrections to measurement time.  This reduces the circuit
    depth by eliminating feedback loops.
  }];

  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

//===----------------------------------------------------------------------===//
// Magic State Passes
//===----------------------------------------------------------------------===//

def FTQCMagicDistillPass : Pass<"ftqc-magic-distill", "mlir::func::FuncOp"> {
  let summary = "Replace prepare_magic_t with a distillation protocol";
  let description = [{
    Replaces each `ftqc.prepare_magic_t` with an inlined distillation
    subroutine.  The `protocol` option selects the algorithm:

      - "15to1": Bravyi-Kitaev 15-to-1 (error O(ε^3))
      - "7to1" : Steane inner 7-to-1 (error O(ε^3))
      - "auto" : choose based on target error rate
  }];

  let options = [
    Option<"protocol", "protocol", "std::string", "\"15to1\"",
           "Distillation protocol: 15to1, 7to1, auto">,
    Option<"levels", "levels", "unsigned", "1",
           "Number of distillation levels (recursion depth)">,
    Option<"targetError", "target-error", "double", "1e-10",
           "Target magic-state error rate">
  ];

  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

//===----------------------------------------------------------------------===//
// Lowering Passes
//===----------------------------------------------------------------------===//

def FTQCLowerToQuantumPass : Pass<"ftqc-lower-to-quantum", "mlir::ModuleOp"> {
  let summary = "Lower FTQC dialect to PennyLane quantum dialect";
  let description = [{
    Fully lowers all `ftqc.*` operations to `quantum.*` operations on
    physical qubits.  This is the final lowering pass in the FTQC pipeline.

    After this pass the IR is in the standard PennyLane quantum dialect
    form ready for simulation or compilation.
  }];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect",
    "mlir::arith::ArithDialect",
    "mlir::scf::SCFDialect"
  ];
}

def FTQCTransversalGatePass : Pass<"ftqc-transversal-gates", "mlir::func::FuncOp"> {
  let summary = "Verify and expand transversal gate implementations";
  let description = [{
    Verifies that each logical gate has a valid transversal implementation
    for the target code.  Non-transversal gates (T for Steane/surface) are
    flagged and optionally decomposed via magic state injection.
  }];

  let options = [
    Option<"decomposeNonTransversal", "decompose", "bool", "true",
           "Decompose non-transversal gates via magic state injection">
  ];

  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

//===----------------------------------------------------------------------===//
// Analysis Passes
//===----------------------------------------------------------------------===//

def FTQCResourceEstimatePass : Pass<"ftqc-resource-estimate", "mlir::ModuleOp"> {
  let summary = "Estimate physical qubit and gate counts for FTQC circuits";
  let description = [{
    Counts logical qubits, T gates, CNOT gates, EC rounds, and magic-state
    distillation overhead.  Annotates each function with
    `ftqc.resource_annotation` containing the estimates.
  }];

  let options = [
    Option<"printReport", "print-report", "bool", "true",
           "Print a human-readable resource estimate to stderr">
  ];
}

def FTQCFaultPathAnalysisPass : Pass<"ftqc-fault-path", "mlir::func::FuncOp"> {
  let summary = "Enumerate fault paths up to a given order";
  let description = [{
    Enumerates all fault paths of weight ≤ `maxOrder` through the circuit.
    A fault path is a sequence of at most `maxOrder` locations where errors
    can occur and still reach the output undetected.

    Annotates the IR with reachability information and reports the leading
    order fault rate.
  }];

  let options = [
    Option<"maxOrder", "max-order", "unsigned", "1",
           "Maximum fault order to enumerate (1 = leading order)">
  ];
}

def FTQCThresholdAnalysisPass : Pass<"ftqc-threshold", "mlir::ModuleOp"> {
  let summary = "Check whether circuit parameters are below fault-tolerance threshold";
  let description = [{
    For each logical qubit, estimates whether the circuit can maintain
    fault-tolerance given the physical error rate `physicalError`.
    Emits warnings for circuits above threshold.
  }];

  let options = [
    Option<"physicalError", "physical-error", "double", "1e-3",
           "Physical gate error rate p">,
    Option<"targetError", "target-error", "double", "1e-10",
           "Required logical error rate">
  ];
}


//===----------------------------------------------------------------------===//
// QASM 3.0 Translation Pass
//===----------------------------------------------------------------------===//

def FTQCLowerToQASM3Pass : Pass<"ftqc-lower-to-qasm3", "mlir::ModuleOp"> {
  let summary = "Translate FTQC dialect to OpenQASM 3.0";
  let description = [{
    Walks all `func.func` operations in the module and emits an OpenQASM 3.0
    representation of each circuit.

    Logical gate ops map directly to standard QASM gates:
      - `init_zero`       → `qubit qN;`
      - `init_plus`       → `qubit qN; h qN;`
      - `logical_h/x/y/z/s` → `h/x/y/z/s qN;`
      - `logical_t`       → `t qN;`  (magic state consumed silently)
      - `logical_cnot`    → `cx qA, qB;`
      - `logical_cz`      → `cz qA, qB;`
      - `logical_toffoli` → `ccx qA, qB, qC;`
      - `logical_measure` → `bit bN = measure qN;`

    Fault-tolerant infrastructure ops (syndrome extraction, error correction,
    Pauli-frame updates) emit QASM line comments when `include-comments=true`.

    The generated QASM string is written to the file path given by `output-file`
    (or to stderr if empty).  It is also attached to the module as the string
    attribute `ftqc.qasm3` so that FileCheck tests can verify it via the MLIR
    IR output (stdout).
  }];

  let options = [
    Option<"outputFile", "output-file", "std::string", "\"\"",
           "Write QASM 3.0 output to this path (empty = write to stderr)">,
    Option<"includeComments", "include-comments", "bool", "true",
           "Emit QASM comments for FT-only ops (syndrome, correction, frame)">
  ];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect"
  ];
}


//===----------------------------------------------------------------------===//
// STIM Physical Circuit Translation Pass
//===----------------------------------------------------------------------===//

def FTQCLowerToSTIMPass : Pass<"ftqc-lower-to-stim", "mlir::ModuleOp"> {
  let summary = "Translate FTQC dialect to a STIM stabilizer circuit for QEC simulation";
  let description = [{
    Expands each logical qubit in the FTQC IR into a physical qubit block for
    the specified error-correcting code and emits a STIM stabilizer circuit
    suitable for Monte Carlo QEC simulation.

    Qubit layout for Steane [[7,1,3]] per logical qubit (base index B):
      - Data qubits    : B+0 .. B+6
      - X ancilla      : B+7  (Sx1), B+8  (Sx2), B+9  (Sx3)
      - Z ancilla      : B+10 (Sz1), B+11 (Sz2), B+12 (Sz3)

    Stabilizers (Sx = Sz, CSS property):
      - Stab 1 : qubits {0,2,4,6}
      - Stab 2 : qubits {1,2,5,6}
      - Stab 3 : qubits {3,4,5,6}

    Logical gate mapping:
      - `init_zero`           → RESET + encoding (H + CX cascade)
      - `init_plus`           → init_zero + H on all data qubits
      - `logical_{h,x,y,z,s}` → transversal gate on all 7 data qubits
      - `logical_cnot`        → transversal CX (ctrl_i, tgt_i) i=0..6
      - `logical_cz`          → transversal CZ (qubit0_i, qubit1_i) i=0..6
      - `logical_t`           → approximated as S (comment emitted; STIM is Clifford-only)
      - `logical_measure`     → M all data qubits + OBSERVABLE_INCLUDE

    Syndrome extraction (emitted after each logical gate):
      - X syndrome: RESET anc, H anc, CX anc→data, H anc, M anc
      - Z syndrome: RESET anc, CX data→anc, M anc
      - DETECTOR records compare consecutive syndrome rounds

    Noise model (when physical-error-rate > 0):
      - After single-qubit gates : DEPOLARIZE1(p)
      - After two-qubit gates    : DEPOLARIZE2(p)
      - After RESET              : X_ERROR(p)
      - Before M                 : X_ERROR(p)

    The generated STIM string is written to `output-file` (or stderr if empty)
    and attached to the module as the `ftqc.stim` string attribute.
  }];

  let options = [
    Option<"outputFile", "output-file", "std::string", "\"\"",
           "Write STIM output to this path (empty = write to stderr)">,
    Option<"physErrorRate", "physical-error-rate", "double", "1e-3",
           "Physical gate depolarizing error rate p (0 = noiseless)">,
    Option<"syndromeRounds", "syndrome-rounds", "unsigned", "1",
           "Number of syndrome extraction rounds after each logical gate">,
    Option<"includeComments", "include-comments", "bool", "true",
           "Emit # comments for FT-only ops in the STIM output">
  ];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect"
  ];
}

#endif // FTQC_PASSES_TD
