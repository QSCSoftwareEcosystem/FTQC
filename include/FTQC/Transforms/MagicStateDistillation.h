//===- MagicStateDistillation.h - Magic state distillation passes ---------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file MagicStateDistillation.h
/// @brief Lowering of `ftqc.prepare_magic_t` to distillation subroutines.
///
/// ### Magic states and fault-tolerant T gates
/// The T gate (π/8 rotation) is not in the Clifford group and therefore cannot
/// be implemented transversally on most error-correcting codes.  The standard
/// approach is *magic-state distillation*: prepare many noisy `|T⟩` states
/// from Clifford + T measurements, distil them to produce a high-fidelity
/// `|T⟩` magic state, then inject it via gate teleportation.
///
/// ### Supported protocols
///
/// | Protocol | Factory | Inputs | Output error |
/// |---|---|---|---|
/// | 15-to-1 (Bravyi-Kitaev 2005) | build15to1Circuit() | 15 noisy `|T⟩` | 35 ε³ |
/// | 7-to-1 (Steane-inner) | build7to1Circuit() | 7 noisy `|T⟩` | 7 ε³ |
///
/// ### Multi-level distillation
/// For extremely low target error rates, multiple levels are chained: the
/// output of one distillation round serves as input to the next.  The total
/// number of raw `|T⟩` states required for k levels of 15-to-1 is 15^k.
///
/// @see createMagicDistillPass() in Passes.h

#ifndef FTQC_TRANSFORMS_MAGICSTATEDISTILLATION_H
#define FTQC_TRANSFORMS_MAGICSTATEDISTILLATION_H

#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <string>

namespace mlir { class Pass; namespace ftqc {

/// @brief Populate patterns for magic-state distillation lowering.
///
/// Replaces every `ftqc.prepare_magic_t` op with an inline call to the
/// selected distillation circuit, optionally chained for multiple levels.
///
/// @param patterns     Pattern set to populate.
/// @param protocol     `"15to1"` or `"7to1"`.
/// @param levels       Number of distillation levels to chain (≥ 1).
/// @param targetError  Target output error rate (informational; used for
///                     warnings if unachievable with @p levels).
void populateMagicDistillPatterns(mlir::RewritePatternSet &patterns,
                                  const std::string &protocol,
                                  unsigned levels,
                                  double targetError);

//===----------------------------------------------------------------------===//
// DistillationResult
//===----------------------------------------------------------------------===//

/// @brief Return value of a distillation circuit builder.
struct DistillationResult {
  mlir::Value outputState;  ///< The distilled magic state (`!ftqc.magic_state<T>`).
  mlir::Value successFlag;  ///< `i1` value: 1 if distillation succeeded, 0 if a
                            ///< detectable error occurred (post-select on 1).
};

//===----------------------------------------------------------------------===//
// 15-to-1 Reed-Muller distillation (Bravyi & Kitaev 2005)
//===----------------------------------------------------------------------===//

/// @brief Build the Bravyi-Kitaev 15-to-1 distillation circuit.
///
/// Uses the \[\[15,1,3\]\] punctured Reed-Muller code.  The circuit consists
/// entirely of Clifford gates plus 15 T-gate injections (magic state inputs),
/// and detects any single T-gate error.
///
/// **Error suppression**: p_out ≈ 35 × p_in³
///
/// **Resource overhead**: 15 input `|T⟩` states → 1 output `|T⟩` state.
///
/// @param builder  OpBuilder at the insertion point.
/// @param loc      Source location for emitted ops.
/// @param inputs   Exactly 15 `!ftqc.magic_state<T>` values (noisy).
/// @return DistillationResult with the purified state and a success flag.
///
/// @pre  `inputs.size() == 15`
DistillationResult build15to1Circuit(mlir::OpBuilder &builder,
                                     mlir::Location loc,
                                     mlir::ValueRange inputs);

//===----------------------------------------------------------------------===//
// 7-to-1 Steane-inner distillation
//===----------------------------------------------------------------------===//

/// @brief Build the 7-to-1 distillation circuit using Steane \[\[7,1,3\]\] as
///        the inner code.
///
/// A lighter-weight alternative to 15-to-1.  Uses 7 input `|T⟩` states to
/// produce 1 distilled state.
///
/// **Error suppression**: p_out ≈ 7 × p_in³  (lower suppression than 15-to-1)
///
/// @param builder  OpBuilder at the insertion point.
/// @param loc      Source location.
/// @param inputs   Exactly 7 `!ftqc.magic_state<T>` values (noisy).
/// @return DistillationResult with the purified state and a success flag.
///
/// @pre  `inputs.size() == 7`
DistillationResult build7to1Circuit(mlir::OpBuilder &builder,
                                    mlir::Location loc,
                                    mlir::ValueRange inputs);

//===----------------------------------------------------------------------===//
// Gate teleportation
//===----------------------------------------------------------------------===//

/// @brief Implement a T gate via magic-state teleportation.
///
/// Applies T to @p dataQubit using the pre-distilled @p magicState `|T⟩`:
///  1. Entangle data and magic state with CNOT.
///  2. Measure magic state in the computational basis.
///  3. Apply classically conditioned S correction on the data qubit.
///
/// @param builder    OpBuilder at the insertion point.
/// @param loc        Source location.
/// @param dataQubit  The logical qubit on which T is applied.
/// @param magicState A distilled `!ftqc.magic_state<T>` resource.
/// @return {corrected_data_qubit, measurement_bit (i1)}.
std::pair<mlir::Value, mlir::Value>
buildTGateTeleportation(mlir::OpBuilder &builder, mlir::Location loc,
                        mlir::Value dataQubit, mlir::Value magicState);

/// @brief Implement an S gate via magic-state injection.
///
/// Analogous to T-gate teleportation but uses an `|S⟩` magic state.
///
/// @param builder    OpBuilder at the insertion point.
/// @param loc        Source location.
/// @param dataQubit  The logical qubit on which S is applied.
/// @param magicState A distilled `!ftqc.magic_state<T>` resource (|S⟩ = S|+⟩).
/// @return {corrected_data_qubit, measurement_bit (i1)}.
std::pair<mlir::Value, mlir::Value>
buildSGateTeleportation(mlir::OpBuilder &builder, mlir::Location loc,
                        mlir::Value dataQubit, mlir::Value magicState);

//===----------------------------------------------------------------------===//
// Error-rate estimation utilities
//===----------------------------------------------------------------------===//

/// @brief Estimate the 15-to-1 output error rate.
///
/// Leading-order formula: p_out ≈ 35 × p_in³.
///
/// @param pIn Per-gate physical error rate of the input `|T⟩` states.
/// @return Estimated output error rate after one round of 15-to-1 distillation.
inline double estimate15to1OutputError(double pIn) {
  return 35.0 * pIn * pIn * pIn;
}

/// @brief Estimate the 7-to-1 output error rate.
///
/// Leading-order formula: p_out ≈ 7 × p_in³.
///
/// @param pIn Per-gate physical error rate of the input `|T⟩` states.
/// @return Estimated output error rate after one round of 7-to-1 distillation.
inline double estimate7to1OutputError(double pIn) {
  return 7.0 * pIn * pIn * pIn;
}

/// @brief Required input error rate for a target output rate after k levels of
///        15-to-1 distillation.
///
/// Inverts the cascade formula p_out = 35^(1 + 1/3 + … + 1/3^(k-1)) × p_in^(3^k).
///
/// @param targetOutputError Desired output error rate (e.g. 1e-10).
/// @param levels            Number of 15-to-1 levels to chain.
/// @return Required per-state input error rate.
double requiredInputError15to1(double targetOutputError, unsigned levels);

/// @brief Total number of raw noisy `|T⟩` states needed for k-level 15-to-1.
///
/// The count is 15^k for k levels.
///
/// @param levels Number of distillation levels.
/// @return Total noisy input states required.
uint64_t totalInputsRequired(unsigned levels);

} // namespace ftqc
} // namespace mlir

#endif // FTQC_TRANSFORMS_MAGICSTATEDISTILLATION_H
