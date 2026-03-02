//===- SteaneEncoding.h - Steane [[7,1,3]] Encoding Pass ------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file SteaneEncoding.h
/// @brief Public API for the Steane \[\[7,1,3\]\] fault-tolerant encoding pass.
///
/// ### Code background
/// The Steane code is a \[\[7,1,3\]\] CSS code based on the classical
/// \[7,4\] Hamming code.  It encodes one logical qubit into 7 physical qubits,
/// corrects any single-qubit error, and supports *transversal* Clifford gates
/// (H, S, CNOT, CZ, X, Y, Z).  T gates require magic-state injection.
///
/// ### Syndrome extraction
/// Steane's transversal ancilla method prepares a transversal ancilla block in
/// \|+⟩^7 (for X stabilisers) or \|0⟩^7 (for Z stabilisers), entangles it
/// with the data block, and measures it.  This gives 3 X-syndrome bits and 3
/// Z-syndrome bits in a single step.
///
/// ### Usage
/// This header is included by the Steane encoding pass implementation and by
/// any pass that needs to build Steane circuits programmatically:
/// @code{.cpp}
/// RewritePatternSet patterns(&ctx);
/// populateSteaneEncodePatterns(patterns, /*distance=*/3, /*insertEC=*/true);
/// @endcode

#ifndef FTQC_TRANSFORMS_STEANEENCODING_H
#define FTQC_TRANSFORMS_STEANEENCODING_H

#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"
#include <memory>

namespace mlir {
class Pass;
namespace ftqc {

/// @brief Populate rewrite patterns that lower `quantum.*` ops to FTQC
///        Steane-code operations.
///
/// Converts physical-qubit ops from a generic `quantum` dialect into FTQC
/// logical operations parameterised with `#ftqc.ecc<steane, @p distance>`.
///
/// @param patterns  Pattern set to add the conversion patterns to.
/// @param distance  Code distance; currently only 3 (\[\[7,1,3\]\]) is
///                  fully supported.
/// @param insertEC  If true, syndrome extraction ops are inserted after each
///                  logical gate during the lowering.
void populateSteaneEncodePatterns(mlir::RewritePatternSet &patterns,
                                  unsigned distance = 3,
                                  bool insertEC = true);

/// @brief Type converter for Steane encoding.
///
/// Maps physical-qubit types to FTQC logical-qubit types:
/// - `!quantum.bit`      → `!ftqc.logical_qubit<#ftqc.ecc<steane, d>>`
/// - `!quantum.reg<n>`   → `n` logical qubits of the same ECC type
///
/// Used together with `populateSteaneEncodePatterns()` inside a
/// `DialectConversionTarget`.
class SteaneTypeConverter : public mlir::TypeConverter {
public:
  /// @param ctx      MLIR context used to construct FTQC types.
  /// @param distance Steane code distance (default 3).
  explicit SteaneTypeConverter(mlir::MLIRContext *ctx, unsigned distance = 3);
};

/// @brief Build the 7-qubit Steane-code encoding circuit for a logical |0⟩_L.
///
/// Emits into @p builder the CNOT/H network that maps one physical `|0⟩` to
/// the logical codeword `|0⟩_L` in the Steane \[\[7,1,3\]\] code.
///
/// @param builder   OpBuilder positioned at the insertion point.
/// @param loc       Source location for emitted ops.
/// @param physQubit Input single physical qubit (must be in state `|0⟩`).
/// @return A 7-qubit register value holding the encoded logical qubit.
mlir::Value buildSteaneEncodeCircuit(mlir::OpBuilder &builder,
                                     mlir::Location loc,
                                     mlir::Value physQubit);

/// @brief Emit the Steane-code transversal Hadamard circuit.
///
/// Applies H to each of the 7 physical qubits in the code block.  The
/// transversal H maps logical H_L = H^⊗7 on the Steane code.
///
/// @param builder    OpBuilder positioned at the insertion point.
/// @param loc        Source location.
/// @param physQubits 7-element range of physical data qubits.
/// @param outQubits  Output vector; receives the 7 post-H qubit values.
void buildTransversalH(mlir::OpBuilder &builder, mlir::Location loc,
                       mlir::ValueRange physQubits,
                       llvm::SmallVectorImpl<mlir::Value> &outQubits);

/// @brief Emit the Steane-code transversal CNOT circuit.
///
/// Applies CNOT qubit-by-qubit across two 7-qubit blocks.  The transversal
/// CNOT implements logical CNOT_L between the two encoded qubits.
///
/// @param builder    OpBuilder positioned at the insertion point.
/// @param loc        Source location.
/// @param ctrlQubits 7-element range of control block physical qubits.
/// @param tgtQubits  7-element range of target block physical qubits.
/// @param outCtrl    Output vector; receives the 7 post-CNOT control qubits.
/// @param outTgt     Output vector; receives the 7 post-CNOT target qubits.
void buildTransversalCNOT(mlir::OpBuilder &builder, mlir::Location loc,
                          mlir::ValueRange ctrlQubits,
                          mlir::ValueRange tgtQubits,
                          llvm::SmallVectorImpl<mlir::Value> &outCtrl,
                          llvm::SmallVectorImpl<mlir::Value> &outTgt);

/// @brief Build the Steane X-stabiliser syndrome extraction circuit.
///
/// Prepares a transversal `|+⟩^7` ancilla block, applies 7 CNOT gates
/// (ancilla → data), measures each ancilla in the Hadamard basis, and returns
/// the 6 syndrome measurement results (3 X-parity checks from the syndrome
/// of the Z stabiliser measurement).
///
/// @param builder    OpBuilder positioned at the insertion point.
/// @param loc        Source location.
/// @param dataQubits 7-element range of data block physical qubits.
/// @return 6 ancilla measurement results (`i1` values): 3 X-syndrome bits.
llvm::SmallVector<mlir::Value, 6>
buildSteaneXSyndrome(mlir::OpBuilder &builder, mlir::Location loc,
                     mlir::ValueRange dataQubits);

/// @brief Build the Steane Z-stabiliser syndrome extraction circuit.
///
/// Prepares a transversal `|0⟩^7` ancilla block, applies 7 CNOT gates
/// (data → ancilla), measures each ancilla in the computational basis.
///
/// @param builder    OpBuilder positioned at the insertion point.
/// @param loc        Source location.
/// @param dataQubits 7-element range of data block physical qubits.
/// @return 6 ancilla measurement results (`i1` values): 3 Z-syndrome bits.
llvm::SmallVector<mlir::Value, 6>
buildSteaneZSyndrome(mlir::OpBuilder &builder, mlir::Location loc,
                     mlir::ValueRange dataQubits);

} // namespace ftqc
} // namespace mlir

#endif // FTQC_TRANSFORMS_STEANEENCODING_H
