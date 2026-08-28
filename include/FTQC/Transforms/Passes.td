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

def FTQCLowerToIntelQSCppPass : Pass<"ftqc-lower-to-iqs-cpp", "mlir::ModuleOp"> {
  let summary = "Emit a standalone C++ source file using the Intel-QS QubitRegister API";
  let description = [{
    Translates a FTQC logical circuit to a compilable C++ source file that uses
    the Intel-QS (qHiPSTER) QubitRegister<ComplexDP> or NoisyQureg<ComplexDP>
    API for state-vector simulation.

    Gate mapping:
      init_zero              → qubit index, initial |0⟩ from "base" init
      init_plus              → qubit index + psi.ApplyHadamard()
      logical_h/x/y/z       → psi.ApplyHadamard / ApplyPauliX/Y/Z
      logical_s              → psi.Apply1QubitGate(S_matrix)
      logical_t              → psi.ApplyT()
      logical_cnot           → psi.ApplyCPauliX(ctrl, tgt)
      logical_cz             → psi.ApplyCPauliZ(ctrl, tgt)
      logical_toffoli        → psi.ApplyToffoli(c0, c1, tgt)
      logical_measure        → Born-rule collapse + record result
      syndrome / EC ops      → comment (stubs for ideal simulation)

    Compile and run the emitted file:
      g++ -std=c++17 -I${IQS_ROOT}/include circuit.cpp \
          -L${IQS_ROOT}/build/lib -lintelqs_lib -lm -o sim
      ./sim [shots]

    With --with-noise the emitter uses NoisyQureg<ComplexDP> and inserts
    ApplyNoiseGate() calls after each gate, parameterised by --t1 and --t2.
  }];

  let options = [
    Option<"outputFile", "output-file", "std::string", "\"\"",
           "Write emitted C++ to this path (default: stderr)">,
    Option<"withNoise", "with-noise", "bool", "false",
           "Use NoisyQureg<ComplexDP> with SSE noise model">,
    Option<"t1Time", "t1", "double", "1000.0",
           "T1 coherence time (used with --with-noise)">,
    Option<"t2Time", "t2", "double", "500.0",
           "T2 coherence time (used with --with-noise)">,
    Option<"rngSeed", "rng-seed", "unsigned", "42",
           "RNG seed for Born-rule measurement sampling">
  ];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect"
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
// QIR Base Profile Translation Pass
//===----------------------------------------------------------------------===//

def FTQCLowerToQIRPass : Pass<"ftqc-lower-to-qir", "mlir::ModuleOp"> {
  let summary = "Translate FTQC dialect to QIR Base Profile LLVM IR (.ll)";
  let description = [{
    Walks all `func.func` operations in the module and emits a QIR Base
    Profile LLVM IR representation of each circuit.

    Logical gate ops map to QIR runtime calls:
      - `init_zero`          → qubit index (no gate emitted)
      - `init_plus`          → qubit index + `__quantum__qis__h__body`
      - `logical_h/x/y/z/s`  → `__quantum__qis__{h,x,y,z,s}__body`
      - `logical_t`          → `__quantum__qis__t__body`
      - `logical_cnot`       → `__quantum__qis__cnot__body`
      - `logical_cz`         → `__quantum__qis__cz__body`
      - `logical_toffoli`    → `__quantum__qis__ccx__body`
      - `logical_measure`    → `__quantum__qis__mz__body`

    Fault-tolerant infrastructure ops (syndrome extraction, error correction,
    Pauli-frame updates) emit LLVM IR line comments when `include-comments=true`.

    Qubits and results follow the QIR Base Profile opaque-pointer convention:
      qubit/result 0  →  ptr null
      qubit/result N  →  ptr inttoptr (i64 N to ptr)

    All measurement results are recorded at function exit via
    `__quantum__rt__result_record_output`.  The emitted function carries
    the `entry_point` and `qir_profiles="base"` attributes required by
    QIR-aware toolchains (e.g. pyqir, QIR Runner).

    The generated IR string is written to the file path given by `output-file`
    (or to stderr if empty).  It is also attached to the module as the string
    attribute `ftqc.qir` for downstream passes and FileCheck tests.
  }];

  let options = [
    Option<"outputFile", "output-file", "std::string", "\"\"",
           "Write QIR .ll output to this path (empty = write to stderr)">,
    Option<"includeComments", "include-comments", "bool", "true",
           "Emit IR comments for FT-only ops (syndrome, correction, frame)">
  ];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect"
  ];
}

def FTQCLowerToQIRnsPass : Pass<"ftqc-lower-to-qir-ns", "mlir::ModuleOp"> {
  let summary = "Translate FTQC dialect to QIR Base Profile LLVM IR (no ECC expansion)";
  let description = [{
    Walks all `func.func` operations in the module and emits a QIR Base
    Profile LLVM IR representation without physical ECC block expansion.

    This pass intentionally mirrors `ftqc-lower-to-qasm3` semantics:
      - each logical qubit is mapped 1:1 to one QIR qubit pointer;
      - logical gates emit a single corresponding QIR QIS call;
      - `init_plus` emits an `h` after allocation;
      - `logical_measure` emits a single `mz` per logical qubit.

    Fault-tolerant infrastructure ops (syndrome extraction, error correction,
    Pauli-frame updates) emit LLVM IR line comments when `include-comments=true`.

    The generated IR string is written to the file path given by `output-file`
    (or to stderr if empty).  It is also attached to the module as the string
    attribute `ftqc.qir` for downstream passes and FileCheck tests.
  }];

  let options = [
    Option<"outputFile", "output-file", "std::string", "\"\"",
           "Write QIR .ll output to this path (empty = write to stderr)">,
    Option<"includeComments", "include-comments", "bool", "true",
           "Emit IR comments for FT-only ops (syndrome, correction, frame)">
  ];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect"
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
// IQM JSON Translation Pass
//===----------------------------------------------------------------------===//

def FTQCLowerToIQMJSONPass : Pass<"ftqc-lower-to-iqm-json", "mlir::ModuleOp"> {
  let summary = "Translate FTQC dialect to IQM JSON circuit format";
  let description = [{
    Walks all `func.func` operations in the module and emits an IQM JSON
    circuit representation suitable for submission to IQM Resonance hardware
    or the IQM emulator (`:mock` alias suffix).

    Logical gate ops are decomposed into IQM native gates:
      - `init_zero`       → (qubit allocated as QBn)
      - `init_plus`       → qubit allocated + H decomposition
      - `logical_x`       → prx(1.0, 0.0)
      - `logical_y`       → prx(1.0, 0.25)
      - `logical_h`       → prx(0.5, 0.25) + prx(1.0, 0.0)
      - `logical_z`       → Euler Rz(π):   prx(0.5,0.0) prx(1.0,0.25) prx(1.5,0.0)
      - `logical_s`       → Euler Rz(π/2): prx(0.5,0.0) prx(0.5,0.25) prx(1.5,0.0)
      - `logical_t`       → Euler Rz(π/4): prx(0.5,0.0) prx(0.25,0.25) prx(1.5,0.0)
      - `logical_cnot`    → H(target) + cz[ctrl,tgt] + H(target)
      - `logical_cz`      → cz(QBa, QBb)
      - `logical_toffoli` → standard 6-CZ Toffoli decomposition (Barenco et al.)
      - `logical_measure` → measure with key "mQBn"

    When `expand-syndromes=true` (default), syndrome ops are expanded into
    physical ancilla circuits:
      - `SteaneZSyndromeOp`: H(anc), cz[data,anc], H(anc), measure(anc)
      - `SteaneXSyndromeOp`: H(anc), H(data), cz[anc,data], H(data), H(anc), measure(anc)

    The generated JSON string is written to the file path given by `output-file`
    (or to stderr if empty).  It is also attached to the module as the string
    attribute `ftqc.iqm_json`.
  }];

  let options = [
    Option<"outputFile", "output-file", "std::string", "\"\"",
           "Write IQM JSON output to this path (empty = write to stderr)">,
    Option<"includeComments", "include-comments", "bool", "true",
           "Emit JSON string fields for FT-only ops (syndrome, correction, frame)">,
    Option<"expandSyndromes", "expand-syndromes", "bool", "true",
           "Expand syndrome ops into physical ancilla CZ circuits">,
    Option<"emitRadians", "radians", "bool", "false",
           "Emit prx args as angle/phase in radians (=value*2pi) instead of "
           "angle_t/phase_t in turns; required by modern IQM station-control">,
    Option<"physical", "physical", "bool", "false",
           "Expand each Steane logical qubit into a 7-physical-qubit block: "
           "emit the |0_L> encoder, transversal gates, and 7 physical measures">
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


//===----------------------------------------------------------------------===//
// Lattice Surgery Surface-Code Translation Pass
//===----------------------------------------------------------------------===//

def FTQCLowerToLatticeSurgeryPass : Pass<"ftqc-lower-to-lattice-surgery", "mlir::ModuleOp"> {
  let summary = "Translate FTQC dialect to a STIM surface-code circuit via lattice surgery";
  let description = [{
    Expands each logical FTQC qubit into a rotated surface-code (d=3) physical
    block (17 qubits: 9 data + 4 X-ancilla + 4 Z-ancilla) and emits a STIM
    stabilizer circuit.

    Two-qubit gates (CNOT, CZ) are implemented via lattice surgery using
    Stim's MPP (multi-Pauli product) instruction:
      1. Joint XX measurement across the logical X representatives of ctrl/tgt.
      2. Syndrome rounds on both patches.
      3. Joint ZZ measurement across the logical Z representatives of ctrl/tgt.
      4. Syndrome rounds on both patches.

    Single-qubit Clifford gates use transversal implementations on all 9 data
    qubits.  T gates are approximated as S (STIM is Clifford-only).

    Physical layout per logical qubit (rotated d=3 surface code, 17 qubits):
      Data qubits (9): base+0 .. base+8   (3×3 grid)
      X-ancilla  (4):  base+9 .. base+12
      Z-ancilla  (4):  base+13 .. base+16

    Stabilizers (data-qubit offset sets within a block):
      X-type: {0,1,3,4}, {2,5}, {3,6}, {4,5,7,8}
      Z-type: {0,1}, {1,2,4,5}, {3,4,6,7}, {7,8}

    Logical Z representative (left column): {0, 3, 6}
    Logical X representative (top row):     {0, 1, 2}

    OBSERVABLE_INCLUDE(i) tracks logical Z of logical qubit i via the XOR of
    its left-column data measurements at final readout.

    The generated STIM string is written to `output-file` (or stderr if empty)
    and attached to the module as the `ftqc.stim_ls` string attribute.
  }];

  let options = [
    Option<"outputFile", "output-file", "std::string", "\"\"",
           "Write STIM output to this path (empty = write to stderr)">,
    Option<"physErrorRate", "physical-error-rate", "double", "1e-3",
           "Physical gate depolarizing error rate p">,
    Option<"syndromeRounds", "syndrome-rounds", "unsigned", "1",
           "Syndrome extraction rounds after each logical gate">,
    Option<"includeComments", "include-comments", "bool", "true",
           "Emit # comments in the STIM output">
  ];

  let dependentDialects = [
    "mlir::ftqc::FTQCDialect",
    "mlir::func::FuncDialect"
  ];
}

//===----------------------------------------------------------------------===//
// Physical expansion (qphys layer)
//===----------------------------------------------------------------------===//

def FTQCExpandPhysicalPass : Pass<"ftqc-expand-physical", "mlir::func::FuncOp"> {
  let summary = "Unfold Steane logical qubits into physical qphys data + ancilla qubits";
  let description = [{
    Lowers the logical `ftqc` IR to the physical `ftqc.qphys.*` layer: each
    `!ftqc.logical_qubit` becomes a block of `!ftqc.qphys_qubit` values
    (Steane [[7,1,3]]: 7 data + 3 X-ancilla + 3 Z-ancilla), and logical gates /
    syndrome ops become physical gate + measure circuits.  This materializes the
    data and ancilla qubits as explicit, labeled SSA values so routing/SWAP and
    the back-end lowerings can operate on one physical IR.
  }];
  let dependentDialects = ["mlir::ftqc::FTQCDialect", "mlir::arith::ArithDialect"];
}

def FTQCExpandPhysicalGenericPass : Pass<"ftqc-expand-physical-generic", "mlir::ModuleOp"> {
  let summary = "Unfold generic logical qubits into physical qphys data + ancilla qubits";
  let description = [{
    Lowers the logical `ftqc` IR to the physical `ftqc.qphys.*` layer: each
    `!ftqc.logical_qubit` becomes a block of `!ftqc.qphys_qubit` values, and
    logical gates / syndrome ops become physical gate + measure circuits.
    Currently syndrome extraction, logical Pauli gates, qubit initialization and
    measurement are supported, using Steane, surface, concatenated, repetition,
    or hypergraph product codes.  This materializes the data and ancilla qubits
    as explicit, labeled SSA values so routing/SWAP and the back-end lowerings
    can operate on one physical IR.
  }];
  let dependentDialects = ["mlir::ftqc::FTQCDialect", "mlir::arith::ArithDialect"];
}

def FTQCQPhysQubitReusePass : Pass<"ftqc-qphys-qubit-reuse", "mlir::func::FuncOp"> {
  let summary = "Renumber qubits to fit within max-qubits, reusing free qubits";
  let description = [{
    Renumbers physical qubits to use IDs in the range [0, max-qubits). Once this
    limit is reached, new allocations reuse a free ID from the least recently
    used qubit, so that as many operations can be performed in parallel as
    possible. This pass fails if the number of live qubits ever exceeds
    max-qubits; no attempt is currently made to reorder operations to get under
    the limit. No attempt is currently made to track qubits across blocks, so
    each function must have a single block and only use qubits allocated in that
    block.
  }];
  let options = [
    Option<"maxQubits", "max-qubits", "uint64_t", "0",
           "Maximum number of qubits to use in the output circuit">
  ];
  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

def FTQCQPhysToQASM3Pass : Pass<"ftqc-qphys-to-qasm3", "mlir::ModuleOp"> {
  let summary = "Lower the physical qphys IR to OpenQASM 3.0";
  let description = [{
    Translates an `ftqc.qphys.*` physical circuit (produced by
    `--ftqc-expand-physical`) to OpenQASM 3.0, demonstrating that back ends can
    consume the shared physical IR rather than re-deriving the physical layout.
  }];
  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

def FTQCQPhysToIQMJSONPass : Pass<"ftqc-qphys-to-iqm-json", "mlir::ModuleOp"> {
  let summary = "Lower the physical qphys IR to IQM JSON";
  let description = [{
    Retargets the IQM JSON back end onto the shared `ftqc.qphys.*` physical IR
    (produced by `--ftqc-expand-physical`): each qphys qubit maps to hardware
    locus "QB<id+1>" and physical gates are decomposed into IQM native gates
    {prx, cz, measure}.  The physical (data + ancilla) layout comes from the IR,
    not from layout logic duplicated inside the emitter.
  }];
  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}


def FTQCQPhysInitLayoutPass : Pass<"ftqc-qphys-init-layout", "mlir::ModuleOp"> {
  let summary = "Map each physical qphys qubit to a hardware qubit index on the backend";
  let description = [{
    Maps each `ftqc.qphys_qubit` qubit in the physical circuit (produced by
    `--ftqc-expand-physical`) to a hardware qubit index on the backend by
    updating the `id` attribute of its `ftqc.qphys.alloc` operation according
    to the qubit-id mapping in an initial-layout JSON file.
  }];
  let options = [
    Option<"inputFile", "input-file", "std::string", "\"\"",
           "Initial-layout JSON file containing logical-to-hardware qubit mappings">
  ];
  let dependentDialects = ["mlir::ftqc::FTQCDialect"];
}

#endif // FTQC_PASSES_TD
