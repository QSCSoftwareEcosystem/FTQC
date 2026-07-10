//===- FTQCAttrs.h - FTQC Attribute Declarations --------------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file FTQCAttrs.h
/// @brief Declarations for all FTQC dialect attributes and ECC helper utilities.
///
/// ### Enumerations (from FTQCEnums.h.inc)
/// | Enum | Values | Usage |
/// |---|---|---|
/// | `ECCKind` | `steane`, `surface`, `color_code`, `concat`, `repetition`, `compass` | Identifies the error-correcting code family |
/// | `PauliKind` | `I`, `X`, `Y`, `Z` | Single-qubit Pauli operator |
/// | `SyndromeBasis` | `X`, `Z` | Stabiliser measurement basis |
/// | `MagicKind` | `T`, `CCZ` | Magic state type |
///
/// ### Attributes (from FTQCAttrs.h.inc)
/// | Attribute | Mnemonic | Fields | Description |
/// |---|---|---|---|
/// | `ECCAttr` | `#ftqc.ecc` | `kind`, `distance`, `levels` | Identifies the QEC code parameterising a logical qubit |
/// | `CodePropertiesAttr` | `#ftqc.code_props` | `n`, `k`, `d` | Static [[n,k,d]] parameters |
/// | `DistillationParamsAttr` | `#ftqc.distill_params` | `inputCount`, `outputCount` | Distillation protocol shape |
///
/// ### ECC helper utilities
/// Free functions that compute code-specific properties from an `ECCKind` +
/// distance pair without requiring an `MLIRContext`:

#ifndef FTQC_ATTRS_H
#define FTQC_ATTRS_H

#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"

#include "FTQC/Utils/StabilizerUtils.h"

// Pull in the enum declarations first.
#include "FTQC/Dialect/FTQCEnums.h.inc"

#define GET_ATTRDEF_CLASSES
#include "FTQC/Dialect/FTQCAttrs.h.inc"

namespace mlir::ftqc {

//===----------------------------------------------------------------------===//
// Helper utilities for ECC attribute queries
//===----------------------------------------------------------------------===//

/// @brief Total physical qubit count for a given ECC.
///
/// Returns the number of physical data qubits required to encode one logical
/// qubit using @p kind at code distance @p distance.  For concatenated codes,
/// @p levels specifies the number of concatenation levels.
///
/// | Code    | Formula                              |
/// |---------|--------------------------------------|
/// | Steane  | 7^levels                             |
/// | Surface | d^2                                  |
/// | Color   | (3d^2 - 3d + 1) / 2  (triangular)  |
///
/// @param kind     ECC family (steane, surface, …).
/// @param distance Code distance d (must be odd for CSS codes).
/// @param levels   Concatenation depth.
/// @param code1    First classical code, used to construct some code families.
/// @param code2    Second classical code, used to construct some code families.
/// @return Number of physical data qubits per logical qubit.
unsigned getPhysicalQubitsForCode(ECCKind kind, unsigned distance,
                                  unsigned levels,
                                  ClassicalCodeAttr code1,
                                  ClassicalCodeAttr code2);

/// @brief Ancilla qubit count per syndrome-extraction round.
///
/// Returns the number of ancilla qubits consumed in a single round of syndrome
/// measurement for the given code.  These qubits are reset between rounds.
///
/// @param kind     ECC family.
/// @param distance Code distance d.
/// @param code1    First classical code, used to construct some code families.
/// @param code2    Second classical code, used to construct some code families.
/// @return Number of ancilla qubits per round.
unsigned getAncillaQubitsForCode(ECCKind kind, unsigned distance,
                                 ClassicalCodeAttr code1,
                                 ClassicalCodeAttr code2);

/// @brief Approximate fault-tolerance threshold for a code kind.
///
/// Returns the depolarising-noise threshold below which error-correction
/// improves fidelity (i.e. p_phys < p_th).  Values are from the literature:
///
/// | Code              | Threshold (approx.) |
/// |-------------------|---------------------|
/// | Steane [[7,1,3]]  | 1 %                 |
/// | Surface code      | 1 %                 |
/// | 2D color code     | 0.5–1.3 %           |
/// | Concatenated      | 0.5 %               |
///
/// @param kind ECC family.
/// @return Threshold as a probability in [0, 1].
double getFTThreshold(ECCKind kind);

/// @brief Returns true if the code belongs to the CSS (Calderbank-Shor-Steane)
///        family.
///
/// CSS codes have separate X and Z stabiliser generators, enabling
/// straightforward syndrome extraction with transversal CNOT ancillas.
///
/// @param kind ECC family.
/// @return True for steane, surface, color_code, repetition; false otherwise.
bool isCSSCode(ECCKind kind);

/// @brief Returns true if the code supports a transversal T gate.
///
/// A transversal T allows fault-tolerant T-gate application without magic-state
/// distillation.  Currently only the 3D color code has this property; for all
/// other codes, T gates require magic-state injection (see MagicStateDistillation.h).
///
/// @param kind ECC family.
/// @return True only for `ECCKind::ColorCode` (3D variant).
bool hasTransversalT(ECCKind kind);

} // namespace mlir::ftqc

#endif // FTQC_ATTRS_H
