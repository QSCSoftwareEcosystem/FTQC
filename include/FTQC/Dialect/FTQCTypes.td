//===- FTQCTypes.td - FTQC Type Definitions -------------------------------===//
//
// MLIR type definitions for the FTQC (Fault-Tolerant Quantum Computing)
// dialect.  Types form a hierarchy above the `quantum` dialect's bit/reg.
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_TYPES_TD
#define FTQC_TYPES_TD

include "FTQC/Dialect/FTQCDialect.td"
include "FTQC/Dialect/FTQCAttrs.td"
include "mlir/IR/AttrTypeBase.td"

//===----------------------------------------------------------------------===//
// Type Base
//===----------------------------------------------------------------------===//

class FTQC_Type<string name, string mnem, list<Trait> traits = []>
    : TypeDef<FTQC_Dialect, name, traits> {
  let mnemonic = mnem;
}

//===----------------------------------------------------------------------===//
// LogicalQubitType — !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
//===----------------------------------------------------------------------===//

def FTQC_LogicalQubitType : FTQC_Type<"LogicalQubit", "logical_qubit"> {
  let summary = "A logical qubit protected by an error correcting code";
  let description = [{
    Represents a single logical qubit encoded in `n` physical qubits
    according to the error-correcting code described by the `code` attribute.

    The type carries the code descriptor so that passes can lower it to
    the correct number of `!quantum.bit` values.

    Examples:
    ```
    !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
    !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
    ```
  }];

  let parameters = (ins "ECCAttr":$code);
  let assemblyFormat = "`<` qualified($code) `>`";

  let extraClassDeclaration = [{
    /// Physical qubit count for this logical qubit.
    unsigned getPhysicalQubits() const {
      return getCode().getPhysicalQubits();
    }
    /// Code distance.
    unsigned getDistance() const {
      return getCode().getDistance();
    }
    /// Whether the code supports a transversal T gate.
    bool supportsTransversalT() const {
      return getCode().supportsTransversalT();
    }
  }];
}

//===----------------------------------------------------------------------===//
// LogicalRegType — !ftqc.logical_reg<n, #ftqc.ecc<steane, 3>>
//===----------------------------------------------------------------------===//

def FTQC_LogicalRegType : FTQC_Type<"LogicalReg", "logical_reg"> {
  let summary = "A register of logical qubits all protected by the same code";
  let description = [{
    A fixed-size array of logical qubits, analogous to `!quantum.reg`.

    Example: `!ftqc.logical_reg<7, #ftqc.ecc<steane, 3>>`
  }];

  let parameters = (ins "unsigned":$size, "ECCAttr":$code);
  let assemblyFormat = "`<` $size `,` $code `>`";

  let extraClassDeclaration = [{
    unsigned getTotalPhysicalQubits() const {
      return getSize() * getCode().getPhysicalQubits();
    }
  }];
}

//===----------------------------------------------------------------------===//
// SyndromeType — !ftqc.syndrome<6>
//===----------------------------------------------------------------------===//

def FTQC_SyndromeType : FTQC_Type<"Syndrome", "syndrome"> {
  let summary = "An n-bit error syndrome measurement result";
  let description = [{
    The result of a syndrome-extraction circuit.  `n` equals the number of
    stabilizer generators (= number of ancilla qubits in the extraction).

    For Steane [[7,1,3]]:  `!ftqc.syndrome<6>` (3 X-stabs + 3 Z-stabs)
    For distance-3 surface code:  `!ftqc.syndrome<8>` (4 X + 4 Z plaquettes)
  }];

  let parameters = (ins "unsigned":$width);
  let assemblyFormat = "`<` $width `>`";

  let extraClassDeclaration = [{
    /// Split syndrome into X-part and Z-part widths.
    unsigned getXWidth() const { return getWidth() / 2; }
    unsigned getZWidth() const { return getWidth() / 2; }
  }];
}

//===----------------------------------------------------------------------===//
// MagicStateType — !ftqc.magic_state
//===----------------------------------------------------------------------===//

def FTQC_MagicStateType : FTQC_Type<"MagicState", "magic_state"> {
  let summary = "A distilled magic state for non-Clifford gate injection";
  let description = [{
    Represents a single distilled magic state.  The kind specifies which
    non-Clifford gate can be injected via gate teleportation:

      - `T`   → |T⟩ = T|+⟩  (enables logical T gate)
      - `CCZ` → |CCZ⟩        (enables logical CCZ / Toffoli)
      - `CS`  → |CS⟩         (enables controlled-S)

    Example: `!ftqc.magic_state`  (defaults to T-type)
             `!ftqc.magic_state<CCZ>`
  }];

  let parameters = (ins DefaultValuedParameter<"MagicKind",
                         "MagicKind::T"> : $kind);
  let assemblyFormat = "(`<` $kind^ `>`)? ";

  let extraClassDeclaration = [{
    bool isTState()   const { return getKind() == MagicKind::T; }
    bool isCCZState() const { return getKind() == MagicKind::CCZ; }
  }];
}

//===----------------------------------------------------------------------===//
// PauliFrameType — !ftqc.pauli_frame
//===----------------------------------------------------------------------===//

def FTQC_PauliFrameType : FTQC_Type<"PauliFrame", "pauli_frame"> {
  let summary = "A Pauli correction frame tracking deferred corrections";
  let description = [{
    A classical data structure that accumulates Pauli corrections deferred
    from syndrome measurements.  Using a Pauli frame avoids applying physical
    correction operations; instead corrections are tracked classically and
    accounted for at measurement time.

    `width` is the number of logical qubits being tracked.
  }];

  let parameters = (ins "unsigned":$width);
  let assemblyFormat = "`<` $width `>`";

  let extraClassDeclaration = [{
    unsigned getNumLogicalQubits() const { return getWidth(); }
  }];
}

//===----------------------------------------------------------------------===//
// QPhysQubitType — !ftqc.qphys_qubit   (physical / hardware qubit)
//===----------------------------------------------------------------------===//

def FTQC_QPhysQubitType : FTQC_Type<"QPhysQubit", "qphys_qubit"> {
  let summary = "A single physical (hardware) qubit in the qphys layer";
  let description = [{
    The `qphys` layer sits *below* the logical `ftqc` types: after the
    `--ftqc-expand-physical` pass, each `!ftqc.logical_qubit` is unfolded into
    a block of `!ftqc.qphys_qubit` values (7 data + ancillas for Steane
    [[7,1,3]]).  Physical gates (`ftqc.qphys.*`) act on these values with
    ordinary quantum value-semantics, so back ends can lower a *single*
    physical IR instead of re-deriving the physical layout in every emitter.

    A `qphys_qubit` is produced by `ftqc.qphys.alloc` (which stamps a hardware
    `id` and a `role`) and threaded through physical gates until measured.
  }];
}

#endif // FTQC_TYPES_TD
