//===- ErrorCorrection.h - Error correction pass declarations -------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file ErrorCorrection.h
/// @brief Rewrite-pattern APIs for correction insertion and Pauli-frame
///        optimisation.
///
/// ### Correction insertion
/// `populateCorrectionPatterns()` inserts `ftqc.correct` ops after each
/// syndrome block.  A `ftqc.correct` op consumes syndrome measurement results
/// and applies the corresponding Pauli correction to the data qubit.
///
/// ### Pauli-frame optimisation
/// Physical correction gates (X, Z applied to a qubit) can be deferred to a
/// classical *Pauli frame*: a bit vector that records which Pauli corrections
/// are pending for each qubit.  Corrections are only applied at the final
/// measurement, saving a large fraction of physical gates in long circuits.
///
/// `populatePauliFramePatterns()` converts `ftqc.correct` ops into updates to
/// a `!ftqc.pauli_frame` value, removing the physical gates from the circuit.
///
/// @see createInsertCorrectionPass() — pass wrapping populateCorrectionPatterns()
/// @see createPauliFrameOptPass()   — pass wrapping populatePauliFramePatterns()

#ifndef FTQC_TRANSFORMS_ERRORCORRECTION_H
#define FTQC_TRANSFORMS_ERRORCORRECTION_H

#include "mlir/IR/PatternMatch.h"
#include <memory>

namespace mlir { class Pass; namespace ftqc {

/// @brief Populate patterns that insert `ftqc.correct` operations.
///
/// For each syndrome block (group of `ftqc.*_syndrome` ops followed by their
/// qubit outputs), a `ftqc.correct` op is inserted that:
///  1. Reads all syndrome bits from the block.
///  2. Calls the appropriate decoder (Steane direct formula, lookup table, etc.).
///  3. Emits the decoded Pauli correction on the data qubit.
///
/// @param patterns  Pattern set to populate.
void populateCorrectionPatterns(mlir::RewritePatternSet &patterns);

/// @brief Populate patterns for Pauli-frame optimisation (deferred correction).
///
/// Replaces each `ftqc.correct` op with a classical update to a
/// `!ftqc.pauli_frame` value.  The physical Pauli gates (X, Z) are eliminated
/// from the circuit; the frame is only applied at the final `ftqc.logical_measure`.
///
/// This optimisation is valid when:
///  - All operations between correction and measurement are Clifford (so the
///    frame can be propagated through them classically).
///  - The final measurement is in the computational or Hadamard basis.
///
/// @param patterns  Pattern set to populate.
void populatePauliFramePatterns(mlir::RewritePatternSet &patterns);

} // namespace ftqc
} // namespace mlir

#endif // FTQC_TRANSFORMS_ERRORCORRECTION_H
