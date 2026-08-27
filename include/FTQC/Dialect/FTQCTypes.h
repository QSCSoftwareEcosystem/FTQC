//===- FTQCTypes.h - FTQC Type Declarations -------------------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file FTQCTypes.h
/// @brief Declarations for all FTQC dialect types and type predicate utilities.
///
/// ### FTQC type overview
///
/// | MLIR type | Mnemonic | Description |
/// |---|---|---|
/// | `LogicalQubitType` | `!ftqc.logical_qubit<#ftqc.ecc<kind,...>>` | One logical qubit encoded in ECC `kind` with parameters `...` |
/// | `SyndromeType` | `!ftqc.syndrome<w>` | A `w`-bit classical syndrome vector from one stabiliser round |
/// | `MagicStateType` | `!ftqc.magic_state<kind>` | A distilled magic state of type `kind` (T or CCZ) |
/// | `PauliFrameType` | `!ftqc.pauli_frame<n>` | Classical Pauli frame tracking deferred corrections on `n` qubits |
///
/// ### Relationship to ECC attributes
/// `LogicalQubitType` carries an `ECCAttr` parameter (`#ftqc.ecc<kind, ...>`)
/// that encodes the ECC family and parameters.  All passes that operate on
/// logical qubits query this attribute to select the right encoding strategy.
///
/// ### Example
/// @code{.mlir}
/// // A Steane [[7,1,3]] logical qubit
/// !ftqc.logical_qubit<#ftqc.ecc<steane>>
///
/// // A 6-bit syndrome register (3 X + 3 Z stabilisers for Steane code)
/// !ftqc.syndrome<6>
///
/// // A T magic state ready for gate teleportation
/// !ftqc.magic_state<T>
/// @endcode

#ifndef FTQC_TYPES_H
#define FTQC_TYPES_H

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Types.h"

#include "FTQC/Dialect/FTQCAttrs.h"

#define GET_TYPEDEF_CLASSES
#include "FTQC/Dialect/FTQCTypes.h.inc"

namespace mlir::ftqc {

//===----------------------------------------------------------------------===//
// Type utility predicates
//===----------------------------------------------------------------------===//

/// @brief Test whether an MLIR type is a logical qubit type.
/// @param t  Any `mlir::Type`.
/// @return True iff @p t is an instance of `LogicalQubitType`.
bool isLogicalQubitType(mlir::Type t);

/// @brief Test whether an MLIR type is a syndrome register type.
/// @param t  Any `mlir::Type`.
/// @return True iff @p t is an instance of `SyndromeType`.
bool isSyndromeType(mlir::Type t);

/// @brief Test whether an MLIR type is a magic state type.
/// @param t  Any `mlir::Type`.
/// @return True iff @p t is an instance of `MagicStateType`.
bool isMagicStateType(mlir::Type t);

/// @brief Test whether an MLIR type is a Pauli frame type.
///
/// Pauli frames track deferred error corrections classically, avoiding
/// redundant physical corrections mid-circuit.
///
/// @param t  Any `mlir::Type`.
/// @return True iff @p t is an instance of `PauliFrameType`.
bool isPauliFrameType(mlir::Type t);

} // namespace mlir::ftqc

#endif // FTQC_TYPES_H
