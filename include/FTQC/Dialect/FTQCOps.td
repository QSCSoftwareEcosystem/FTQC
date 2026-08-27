//===- FTQCOps.td - FTQC Operation Definitions ----------------------------===//
//
// ODS definitions for all FTQC operations.  Grouped by protocol category.
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_OPS_TD
#define FTQC_OPS_TD

include "FTQC/Dialect/FTQCDialect.td"
include "FTQC/Dialect/FTQCTypes.td"
include "FTQC/Dialect/FTQCAttrs.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "mlir/IR/BuiltinTypes.td"

//===----------------------------------------------------------------------===//
// Base Op class
//===----------------------------------------------------------------------===//

class FTQC_Op<string mnem, list<Trait> traits = []>
    : Op<FTQC_Dialect, mnem, traits>;

//===----------------------------------------------------------------------===//
// 1. Encoding / Decoding Operations
//===----------------------------------------------------------------------===//

def FTQC_EncodeOp : FTQC_Op<"encode"> {
  let summary = "Encode a physical qubit into a logical qubit";
  let description = [{
    Encodes a `!quantum.bit` into a logical qubit protected by the given
    error-correcting code, producing a `!ftqc.logical_qubit`.

    The input qubit is consumed (SSA value).  For Steane [[7,1,3]] this
    generates a 7-qubit block in the output.

    Example:
    ```mlir
    %lq = ftqc.encode %phys : !quantum.bit
        -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
    ```
  }];

  let arguments = (ins AnyType:$physQubit);
  let results  = (outs FTQC_LogicalQubitType:$logicalQubit);
  let assemblyFormat =
    "$physQubit attr-dict `:` type($physQubit) `->` type($logicalQubit)";

  let hasVerifier = 1;
}

def FTQC_DecodeOp : FTQC_Op<"decode"> {
  let summary = "Decode a logical qubit back to a physical qubit";
  let description = [{
    Decodes a logical qubit block to recover a physical qubit.
    Used at the end of a fault-tolerant computation to extract the answer.

    Example:
    ```mlir
    %phys = ftqc.decode %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
        -> !quantum.bit
    ```
  }];

  let arguments = (ins FTQC_LogicalQubitType:$logicalQubit);
  let results  = (outs AnyType:$physQubit);
  let assemblyFormat =
    "$logicalQubit attr-dict `:` type($logicalQubit) `->` type($physQubit)";
}

def FTQC_InitZeroOp : FTQC_Op<"init_zero"> {
  let summary = "Initialize a logical |0⟩ state";
  let description = [{
    Allocates and initializes a new logical qubit in the logical |0⟩ state
    for the specified error-correcting code.

    Example:
    ```mlir
    %lq = ftqc.init_zero {code = #ftqc.ecc<steane>}
        : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    ```
  }];

  let results = (outs FTQC_LogicalQubitType:$logicalQubit);
  let assemblyFormat = "attr-dict `:` type($logicalQubit)";
}

def FTQC_InitPlusOp : FTQC_Op<"init_plus"> {
  let summary = "Initialize a logical |+⟩ = H|0⟩ state";
  let description = [{
    Allocates a new logical qubit in the logical |+⟩ state.
    Used as the target of a gate-teleportation T injection.

    Example:
    ```mlir
    %lq = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    ```
  }];
  let results = (outs FTQC_LogicalQubitType:$logicalQubit);
  let assemblyFormat = "attr-dict `:` type($logicalQubit)";
}

def FTQC_DeallocOp : FTQC_Op<"dealloc"> {
  let summary = "Release a logical qubit's physical qubits";
  let description = [{
    Releases all physical qubits backing a logical qubit block.
    The logical qubit SSA value must not be used after this operation.
  }];
  let arguments = (ins FTQC_LogicalQubitType:$logicalQubit);
  let assemblyFormat = "$logicalQubit attr-dict `:` type($logicalQubit)";
}

//===----------------------------------------------------------------------===//
// 2. Syndrome Extraction Operations
//===----------------------------------------------------------------------===//

def FTQC_ExtractSyndromeOp : FTQC_Op<"extract_syndrome"> {
  let summary = "Fault-tolerantly extract an error syndrome";
  let description = [{
    Performs one round of syndrome extraction on a logical qubit block using
    ancilla qubits.  The `basis` attribute selects X or Z stabilizers.
    Both are measured if `basis` is omitted.

    The input logical qubit value is consumed and a new one returned
    (SSA immutable form), along with the syndrome result.

    Example:
    ```mlir
    %lq_out, %syn = ftqc.extract_syndrome %lq_in {basis = #ftqc.X}
        : !ftqc.logical_qubit<#ftqc.ecc<steane>>
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
    ```
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $logicalQubit,
    OptionalAttr<I32Attr>: $rounds   // number of syndrome extraction rounds
  );
  let results = (outs
    FTQC_LogicalQubitType: $outLogicalQubit,
    FTQC_SyndromeType    : $syndrome
  );
  let assemblyFormat = [{
    $logicalQubit attr-dict `:`
    type($logicalQubit) `->` `(` type($outLogicalQubit) `,` type($syndrome) `)`
  }];
  let hasVerifier = 1;
}

def FTQC_FlagSyndromeOp : FTQC_Op<"flag_syndrome"> {
  let summary = "Flag-qubit syndrome extraction (reduced ancilla)";
  let description = [{
    Implements the flag fault-tolerance protocol from Chao & Reichardt (2018).
    Uses a single ancilla per stabilizer plus one flag qubit, reducing ancilla
    overhead while maintaining fault-tolerance up to 1 fault per location.

    Returns the syndrome plus a flag bit indicating whether a hook error
    was detected.
  }];

  let arguments = (ins FTQC_LogicalQubitType:$logicalQubit);
  let results = (outs
    FTQC_LogicalQubitType: $outLogicalQubit,
    FTQC_SyndromeType    : $syndrome,
    I1                   : $flag
  );
  let assemblyFormat = [{
    $logicalQubit attr-dict `:`
    type($logicalQubit) `->`
    `(` type($outLogicalQubit) `,` type($syndrome) `,` `i1` `)`
  }];
}

def FTQC_ShorSyndromeOp : FTQC_Op<"shor_syndrome"> {
  let summary = "Shor-style cat-state syndrome extraction";
  let description = [{
    Syndrome extraction using cat-state ancilla (Shor's original approach).
    Prepares |cat⟩ = (|0...0⟩ + |1...1⟩)/√2, couples to data, and verifies
    the cat state before measuring.  Fault-tolerant to t = floor((d-1)/2)
    faults.
  }];

  let arguments = (ins FTQC_LogicalQubitType:$logicalQubit);
  let results = (outs
    FTQC_LogicalQubitType: $outLogicalQubit,
    FTQC_SyndromeType    : $syndrome
  );
  let assemblyFormat = [{
    $logicalQubit attr-dict `:`
    type($logicalQubit) `->` `(` type($outLogicalQubit) `,` type($syndrome) `)`
  }];
}

def FTQC_SteaneXSyndromeOp : FTQC_Op<"steane_x_syndrome"> {
  let summary = "Steane-method X-stabilizer syndrome extraction";
  let description = [{
    Steane's transversal syndrome extraction technique.  Prepares a
    transversal |+⟩^n ancilla block, applies CNOT, and measures in the
    Hadamard basis to obtain the X-parity checks in one transversal step.
  }];

  let arguments = (ins FTQC_LogicalQubitType:$logicalQubit);
  let results = (outs
    FTQC_LogicalQubitType: $outLogicalQubit,
    FTQC_SyndromeType    : $xSyndrome
  );
  let assemblyFormat = [{
    $logicalQubit attr-dict `:`
    type($logicalQubit) `->` `(` type($outLogicalQubit) `,` type($xSyndrome) `)`
  }];
}

def FTQC_SteaneZSyndromeOp : FTQC_Op<"steane_z_syndrome"> {
  let summary = "Steane-method Z-stabilizer syndrome extraction";

  let arguments = (ins FTQC_LogicalQubitType:$logicalQubit);
  let results = (outs
    FTQC_LogicalQubitType: $outLogicalQubit,
    FTQC_SyndromeType    : $zSyndrome
  );
  let assemblyFormat = [{
    $logicalQubit attr-dict `:`
    type($logicalQubit) `->` `(` type($outLogicalQubit) `,` type($zSyndrome) `)`
  }];
}

//===----------------------------------------------------------------------===//
// 3. Error Correction Operations
//===----------------------------------------------------------------------===//

def FTQC_CorrectOp : FTQC_Op<"correct"> {
  let summary = "Apply a Pauli correction based on decoded syndrome";
  let description = [{
    Decodes the syndrome and applies the inferred minimum-weight Pauli
    correction to the logical qubit block.

    Example:
    ```mlir
    %lq_corrected = ftqc.correct %lq, %syn
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
        -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
    ```
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $logicalQubit,
    FTQC_SyndromeType    : $syndrome
  );
  let results = (outs FTQC_LogicalQubitType:$correctedQubit);
  let assemblyFormat = [{
    $logicalQubit `,` $syndrome attr-dict `:`
    `(` type($logicalQubit) `,` type($syndrome) `)`
    `->` type($correctedQubit)
  }];
  let hasVerifier = 1;
}

def FTQC_PauliFrameUpdateOp : FTQC_Op<"pauli_frame_update"> {
  let summary = "Update a Pauli frame with syndrome-inferred corrections";
  let description = [{
    Accumulates the Pauli correction into a frame without physically applying
    it to the qubits.  Deferred corrections are applied at measurement time
    via `ftqc.logical_measure`.
  }];

  let arguments = (ins
    FTQC_PauliFrameType: $frame,
    FTQC_SyndromeType  : $syndrome,
    I32Attr            : $qubitIndex
  );
  let results = (outs FTQC_PauliFrameType:$outFrame);
  let assemblyFormat = [{
    $frame `,` $syndrome `,` $qubitIndex attr-dict `:`
    type($frame) `,` type($syndrome) `->` type($outFrame)
  }];
}

def FTQC_KnillECOp : FTQC_Op<"knill_ec"> {
  let summary = "Knill error correction via quantum teleportation";
  let description = [{
    Implements Knill's teleportation-based error correction protocol.
    Requires a pre-prepared ancilla pair in the code space.
    More resource-intensive than Steane EC but handles a broader class
    of errors including erasures.
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $data,
    FTQC_LogicalQubitType: $ancilla0,
    FTQC_LogicalQubitType: $ancilla1
  );
  let results = (outs
    FTQC_LogicalQubitType: $correctedData,
    FTQC_SyndromeType    : $syndrome
  );
  let assemblyFormat = [{
    $data `,` $ancilla0 `,` $ancilla1 attr-dict `:`
    `(` type($data) `,` type($ancilla0) `,` type($ancilla1) `)`
    `->` `(` type($correctedData) `,` type($syndrome) `)`
  }];
}

def FTQC_ECRoundOp : FTQC_Op<"ec_round"> {
  let summary = "A full syndrome-extract + correct round";
  let description = [{
    Combines syndrome extraction and correction into a single high-level
    operation, applying `rounds` cycles of syndrome measurement and using
    space-time majority voting before applying correction.
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $logicalQubit,
    DefaultValuedAttr<I32Attr, "1">: $rounds
  );
  let results = (outs
    FTQC_LogicalQubitType: $correctedQubit,
    FTQC_SyndromeType    : $syndrome
  );
  let assemblyFormat = [{
    $logicalQubit (`rounds` `=` $rounds^)? attr-dict `:`
    type($logicalQubit) `->` `(` type($correctedQubit) `,` type($syndrome) `)`
  }];
}

//===----------------------------------------------------------------------===//
// 4. Logical Gate Operations
//===----------------------------------------------------------------------===//

def FTQC_LogicalXOp : FTQC_Op<"logical_x", [Pure, SameOperandsAndResultType]> {
  let summary = "Transversal logical X gate";
  let arguments = (ins FTQC_LogicalQubitType:$qubit);
  let results  = (outs FTQC_LogicalQubitType:$result);
  let assemblyFormat = "$qubit attr-dict `:` type($qubit)";
  let hasFolder = 1;
}

def FTQC_LogicalZOp : FTQC_Op<"logical_z", [Pure, SameOperandsAndResultType]> {
  let summary = "Transversal logical Z gate";
  let arguments = (ins FTQC_LogicalQubitType:$qubit);
  let results  = (outs FTQC_LogicalQubitType:$result);
  let assemblyFormat = "$qubit attr-dict `:` type($qubit)";
  let hasFolder = 1;
}

def FTQC_LogicalYOp : FTQC_Op<"logical_y", [Pure, SameOperandsAndResultType]> {
  let summary = "Logical Y = iXZ gate";
  let arguments = (ins FTQC_LogicalQubitType:$qubit);
  let results  = (outs FTQC_LogicalQubitType:$result);
  let assemblyFormat = "$qubit attr-dict `:` type($qubit)";
}

def FTQC_LogicalHOp : FTQC_Op<"logical_h", [Pure, SameOperandsAndResultType]> {
  let summary = "Transversal logical Hadamard gate";
  let description = [{
    Applies a logical Hadamard gate via transversal H on all physical qubits
    (valid for CSS codes where H maps X-stabs ↔ Z-stabs).
  }];
  let arguments = (ins FTQC_LogicalQubitType:$qubit);
  let results  = (outs FTQC_LogicalQubitType:$result);
  let assemblyFormat = "$qubit attr-dict `:` type($qubit)";
  let hasFolder = 1;
}

def FTQC_LogicalSOp : FTQC_Op<"logical_s", [SameOperandsAndResultType]> {
  let summary = "Logical S gate (transversal in color code, or via magic state)";
  let description = [{
    For color codes: applies transversal S.
    For Steane/surface codes: injects via magic state |S⟩ = S|+⟩.
    The lowering pass selects the appropriate implementation.
  }];
  let arguments = (ins FTQC_LogicalQubitType:$qubit);
  let results  = (outs FTQC_LogicalQubitType:$result);
  let assemblyFormat = "$qubit attr-dict `:` type($qubit)";
}

def FTQC_LogicalTOp : FTQC_Op<"logical_t"> {
  let summary = "Logical T gate via magic state injection";
  let description = [{
    Performs the logical T gate via gate teleportation using a pre-distilled
    T magic state |T⟩.  Consumes one magic state.

    Example:
    ```mlir
    %ms   = ftqc.prepare_magic_t : !ftqc.magic_state
    %lq1  = ftqc.logical_t %lq0, %ms
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
        -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
    ```
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $qubit,
    FTQC_MagicStateType  : $magicState
  );
  let results = (outs FTQC_LogicalQubitType:$result);
  let assemblyFormat = [{
    $qubit `,` $magicState attr-dict `:`
    `(` type($qubit) `,` type($magicState) `)` `->` type($result)
  }];
}

def FTQC_LogicalCNOTOp : FTQC_Op<"logical_cnot", [Pure]> {
  let summary = "Transversal logical CNOT gate";
  let description = [{
    Transversal CNOT between two logical qubits in the same code.
    Both qubits must use the same ECC.
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $control,
    FTQC_LogicalQubitType: $target
  );
  let results = (outs
    FTQC_LogicalQubitType: $controlOut,
    FTQC_LogicalQubitType: $targetOut
  );
  let assemblyFormat = [{
    $control `,` $target attr-dict `:`
    `(` type($control) `,` type($target) `)`
    `->` `(` type($controlOut) `,` type($targetOut) `)`
  }];
  let hasVerifier = 1;
}

def FTQC_LogicalCZOp : FTQC_Op<"logical_cz", [Pure]> {
  let summary = "Logical CZ gate (via H + CNOT + H)";
  let arguments = (ins
    FTQC_LogicalQubitType: $qubit0,
    FTQC_LogicalQubitType: $qubit1
  );
  let results = (outs
    FTQC_LogicalQubitType: $result0,
    FTQC_LogicalQubitType: $result1
  );
  let assemblyFormat = [{
    $qubit0 `,` $qubit1 attr-dict `:`
    `(` type($qubit0) `,` type($qubit1) `)`
    `->` `(` type($result0) `,` type($result1) `)`
  }];
}

def FTQC_LogicalToffoliOp : FTQC_Op<"logical_toffoli"> {
  let summary = "Logical Toffoli (CCX) gate via CCZ magic state";
  let arguments = (ins
    FTQC_LogicalQubitType: $ctrl0,
    FTQC_LogicalQubitType: $ctrl1,
    FTQC_LogicalQubitType: $target,
    FTQC_MagicStateType  : $magicState
  );
  let results = (outs
    FTQC_LogicalQubitType: $outCtrl0,
    FTQC_LogicalQubitType: $outCtrl1,
    FTQC_LogicalQubitType: $outTarget
  );
  let assemblyFormat = [{
    $ctrl0 `,` $ctrl1 `,` $target `,` $magicState attr-dict `:`
    `(` type($ctrl0) `,` type($ctrl1) `,` type($target) `,`
        type($magicState) `)`
    `->` `(` type($outCtrl0) `,` type($outCtrl1) `,` type($outTarget) `)`
  }];
}

def FTQC_LogicalMeasureOp : FTQC_Op<"logical_measure"> {
  let summary = "Fault-tolerant logical measurement";
  let description = [{
    Performs a fault-tolerant measurement of a logical qubit.  If a Pauli
    frame is provided, deferred corrections are applied before measurement.
    Returns a classical bit and releases the logical qubit.
  }];

  let arguments = (ins
    FTQC_LogicalQubitType        : $logicalQubit,
    OptionalAttr<I32Attr>        : $basis,     // 0=Z, 1=X
    Optional<FTQC_PauliFrameType>: $frame
  );
  let results = (outs I1:$result);
  let assemblyFormat = [{
    $logicalQubit (`,` $frame^)? attr-dict `:`
    `(` type($logicalQubit) (`,` type($frame)^)? `)` `->` `i1`
  }];
}

//===----------------------------------------------------------------------===//
// 5. Magic State Operations
//===----------------------------------------------------------------------===//

def FTQC_PrepareMagicTOp : FTQC_Op<"prepare_magic_t"> {
  let summary = "Prepare a (possibly noisy) T magic state |T⟩ = T|+⟩";
  let description = [{
    Allocates and prepares a logical |T⟩ state.  Without distillation this
    has a physical error rate ε; after distillation it reaches O(ε^k).
  }];
  let results = (outs FTQC_MagicStateType:$state);
  let assemblyFormat = "attr-dict `:` type($state)";
}

def FTQC_PrepareMagicCCZOp : FTQC_Op<"prepare_magic_ccz"> {
  let summary = "Prepare a CCZ magic state for Toffoli injection";
  let results = (outs FTQC_MagicStateType:$state);
  let assemblyFormat = "attr-dict `:` type($state)";
}

def FTQC_Distill15to1Op : FTQC_Op<"distill_15to1"> {
  let summary = "15-to-1 Bravyi-Kitaev T state distillation";
  let description = [{
    Implements the [[15,1,3]] Reed-Muller distillation protocol of
    Bravyi & Kitaev (2005).  Consumes 15 noisy T states and outputs
    1 state with error O(ε^3).

    Example:
    ```mlir
    %out = ftqc.distill_15to1(%s0, %s1, ..., %s14 : !ftqc.magic_state x 15)
        -> !ftqc.magic_state
    ```
  }];

  let arguments = (ins Variadic<FTQC_MagicStateType>:$inputs);
  let results  = (outs FTQC_MagicStateType:$output);
  let assemblyFormat = [{
    `(` $inputs `:` type($inputs) `)` attr-dict `->` type($output)
  }];
  let hasVerifier = 1;  // must have exactly 15 inputs
}

def FTQC_Distill7to1Op : FTQC_Op<"distill_7to1"> {
  let summary = "7-to-1 T state distillation (Steane code inner code)";
  let description = [{
    Uses the [[7,1,3]] Steane code as the inner code for distillation,
    consuming 7 noisy T states to produce 1 improved state with error O(ε^3).
  }];
  let arguments = (ins Variadic<FTQC_MagicStateType>:$inputs);
  let results  = (outs FTQC_MagicStateType:$output);
  let assemblyFormat = [{
    `(` $inputs `:` type($inputs) `)` attr-dict `->` type($output)
  }];
  let hasVerifier = 1;
}

def FTQC_DistillCCZOp : FTQC_Op<"distill_ccz"> {
  let summary = "CCZ magic state distillation";
  let description = [{
    Distills a high-quality CCZ magic state from noisy inputs using the
    protocol of Haah et al. (2018), achieving error O(ε^2) with low overhead.
  }];
  let arguments = (ins Variadic<FTQC_MagicStateType>:$inputs);
  let results  = (outs FTQC_MagicStateType:$output);
  let assemblyFormat = [{
    `(` $inputs `:` type($inputs) `)` attr-dict `->` type($output)
  }];
}

def FTQC_GateTeleportOp : FTQC_Op<"gate_teleport"> {
  let summary = "Gate teleportation using a magic state";
  let description = [{
    Implements gate teleportation: applies the gate encoded by `magicState`
    to `qubit` via Bell measurement and single-qubit corrections.

    This is the underlying primitive for magic state injection.
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $qubit,
    FTQC_MagicStateType  : $magicState
  );
  let results = (outs
    FTQC_LogicalQubitType: $result,
    I1                   : $measurement  // correction bit
  );
  let assemblyFormat = [{
    $qubit `,` $magicState attr-dict `:`
    `(` type($qubit) `,` type($magicState) `)`
    `->` `(` type($result) `,` `i1` `)`
  }];
}

//===----------------------------------------------------------------------===//
// 6. Surface-code-specific Operations
//===----------------------------------------------------------------------===//

def FTQC_LatticeSurgeryOp : FTQC_Op<"lattice_surgery"> {
  let summary = "Lattice surgery merge/split on surface code patches";
  let description = [{
    Implements the lattice-surgery entangling gate between two surface-code
    logical qubits.  `kind` = 0 for XX merge (logical CNOT) and 1 for ZZ.

    This replaces the transversal CNOT when logical qubits are on separate
    2D patches and cannot physically overlap.
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $patch0,
    FTQC_LogicalQubitType: $patch1,
    I32Attr              : $kind
  );
  let results = (outs
    FTQC_LogicalQubitType: $result0,
    FTQC_LogicalQubitType: $result1,
    FTQC_SyndromeType    : $mergeSyndrome
  );
  let assemblyFormat = [{
    $patch0 `,` $patch1 `kind` `=` $kind attr-dict `:`
    `(` type($patch0) `,` type($patch1) `)`
    `->` `(` type($result0) `,` type($result1) `,` type($mergeSyndrome) `)`
  }];
}

def FTQC_SurfaceRoundOp : FTQC_Op<"surface_round"> {
  let summary = "One full cycle of surface-code stabilizer measurement";
  let description = [{
    Applies one complete d×d surface-code measurement cycle:
    measure all X plaquettes, then all Z plaquettes, decode, and optionally
    apply correction.  `cycles` specifies how many rounds to run.
  }];

  let arguments = (ins
    FTQC_LogicalQubitType: $patch,
    DefaultValuedAttr<I32Attr, "1">: $cycles
  );
  let results = (outs
    FTQC_LogicalQubitType: $result,
    FTQC_SyndromeType    : $syndrome
  );
  let assemblyFormat = [{
    $patch (`cycles` `=` $cycles^)? attr-dict `:`
    type($patch) `->` `(` type($result) `,` type($syndrome) `)`
  }];
}

//===----------------------------------------------------------------------===//
// 7. Pauli Frame Operations
//===----------------------------------------------------------------------===//

def FTQC_CreateFrameOp : FTQC_Op<"create_frame"> {
  let summary = "Create an empty Pauli correction frame";
  let results = (outs FTQC_PauliFrameType:$frame);
  let assemblyFormat = "attr-dict `:` type($frame)";
}

def FTQC_ApplyFrameOp : FTQC_Op<"apply_frame"> {
  let summary = "Apply all accumulated frame corrections to logical qubits";
  let description = [{
    Physically applies the Pauli corrections accumulated in `frame` to
    the corresponding logical qubits.  Used just before final measurement
    or before a non-Clifford gate.
  }];
  let arguments = (ins
    FTQC_PauliFrameType: $frame,
    Variadic<FTQC_LogicalQubitType>: $qubits
  );
  let results = (outs Variadic<FTQC_LogicalQubitType>:$outQubits);
  let assemblyFormat = [{
    $frame `,` `[` $qubits `]` attr-dict `:`
    type($frame) `,` `[` type($qubits) `]` `->` `[` type($outQubits) `]`
  }];
}

//===----------------------------------------------------------------------===//
// 8. Resource Annotation Operations
//===----------------------------------------------------------------------===//

def FTQC_ResourceAnnotationOp : FTQC_Op<"resource_annotation",
    [HasParent<"func::FuncOp">]> {
  let summary = "Annotate a function with FTQC resource estimates";
  let description = [{
    A marker operation placed at the top of a function that records
    static resource estimates computed by the analysis pass.
    Does not affect execution.
  }];
  let arguments = (ins
    I64Attr: $physicalQubits,
    I64Attr: $logicalQubits,
    I64Attr: $tGateCount,
    I64Attr: $cnotCount,
    I64Attr: $ecRounds,
    F64Attr: $estimatedLogicalErrorRate
  );
  let assemblyFormat = "attr-dict";
}

//===----------------------------------------------------------------------===//
// QPHYS physical layer — ftqc.qphys.*
//
// Produced by `--ftqc-expand-physical`, which unfolds each logical qubit into
// its physical block (data + syndrome ancillas).  These ops carry the concrete
// data/ancilla qubits as SSA values so a single physical IR can feed every
// back-end lowering.  Value-semantics, mirroring the logical gate ops above.
//===----------------------------------------------------------------------===//

def FTQC_QPhysAllocOp : FTQC_Op<"qphys.alloc"> {
  let summary = "Allocate a physical qubit in |0> with a hardware id and role";
  let description = [{
    Introduces one physical qubit.  `id` is the hardware qubit index; `role`
    labels it for readability ("data", "x_anc", "z_anc").  This is the op that
    makes data and ancilla qubits explicit in the IR.

    Example: `%q = ftqc.qphys.alloc {id = 7 : i64, role = "x_anc"} : !ftqc.qphys_qubit`
  }];
  let arguments = (ins I64Attr:$id, OptionalAttr<StrAttr>:$role);
  let results   = (outs FTQC_QPhysQubitType:$qubit);
  let assemblyFormat = "attr-dict `:` type($qubit)";
}

class FTQC_QPhys1Q<string mnem>
    : FTQC_Op<mnem, [SameOperandsAndResultType]> {
  let arguments = (ins FTQC_QPhysQubitType:$in);
  let results   = (outs FTQC_QPhysQubitType:$out);
  let assemblyFormat = "$in attr-dict `:` type($in)";
}

def FTQC_QPhysResetOp : FTQC_QPhys1Q<"qphys.reset"> {
  let summary = "Reset a physical qubit to |0>"; }
def FTQC_QPhysHOp : FTQC_QPhys1Q<"qphys.h"> {
  let summary = "Physical Hadamard gate"; }
def FTQC_QPhysXOp : FTQC_QPhys1Q<"qphys.x"> {
  let summary = "Physical Pauli-X gate"; }
def FTQC_QPhysZOp : FTQC_QPhys1Q<"qphys.z"> {
  let summary = "Physical Pauli-Z gate"; }
def FTQC_QPhysSOp : FTQC_QPhys1Q<"qphys.s"> {
  let summary = "Physical S (phase) gate"; }
def FTQC_QPhysTOp : FTQC_QPhys1Q<"qphys.t"> {
  let summary = "Physical T gate"; }

class FTQC_QPhys2Q<string mnem> : FTQC_Op<mnem> {
  let arguments = (ins FTQC_QPhysQubitType:$q0, FTQC_QPhysQubitType:$q1);
  let results   = (outs FTQC_QPhysQubitType:$o0, FTQC_QPhysQubitType:$o1);
  let assemblyFormat = "$q0 `,` $q1 attr-dict `:` type($q0)";
}

def FTQC_QPhysCXOp : FTQC_QPhys2Q<"qphys.cx"> {
  let summary = "Physical CNOT gate (q0 = control, q1 = target)"; }
def FTQC_QPhysCZOp : FTQC_QPhys2Q<"qphys.cz"> {
  let summary = "Physical CZ gate"; }
def FTQC_QPhysSwapOp : FTQC_QPhys2Q<"qphys.swap"> {
  let summary = "Physical SWAP gate (for routing passes)"; }

def FTQC_QPhysMeasureOp : FTQC_Op<"qphys.measure"> {
  let summary = "Measure a physical qubit in the Z basis";
  let arguments = (ins FTQC_QPhysQubitType:$in, OptionalAttr<StrAttr>:$key);
  let results   = (outs I1:$bit);
  let assemblyFormat = "$in attr-dict `:` type($in) `->` type($bit)";
}

#endif // FTQC_OPS_TD
