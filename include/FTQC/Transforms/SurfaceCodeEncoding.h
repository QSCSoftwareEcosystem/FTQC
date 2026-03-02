//===- SurfaceCodeEncoding.h - Surface Code Encoding Pass -----------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file SurfaceCodeEncoding.h
/// @brief Public API for the rotated surface-code fault-tolerant encoding pass.
///
/// ### Code background
/// The rotated surface code \[\[d², 1, d\]\] encodes one logical qubit into
/// d² physical data qubits arranged on a 2-D lattice, plus d²-1 ancilla
/// qubits for syndrome measurement.  It achieves the highest known
/// fault-tolerance threshold (~1%) under circuit-level depolarising noise.
///
/// Two-qubit logical gates are implemented via *lattice surgery*: adjacent
/// patches are temporarily merged by measuring their shared boundary
/// stabilisers, implementing a CNOT or CZ between logical qubits.
///
/// ### Physical qubit count
/// | Parameter | Formula |
/// |---|---|
/// | Data qubits | d² |
/// | Ancilla qubits | d²-1 |
/// | Total | 2d²-1 |
///
/// ### Syndrome cycle
/// One syndrome cycle measures all (d²-1)/2 X plaquettes and (d²-1)/2 Z
/// plaquettes in parallel using ancilla qubits, requiring 4 CNOT gates per
/// ancilla.

#ifndef FTQC_TRANSFORMS_SURFACECODEENCODING_H
#define FTQC_TRANSFORMS_SURFACECODEENCODING_H

#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"
#include <memory>

namespace mlir { class Pass; namespace ftqc {

/// @brief Populate rewrite patterns that lower `quantum.*` ops to FTQC
///        surface-code logical operations.
///
/// @param patterns   Pattern set to add the conversion patterns to.
/// @param distance   Surface-code distance d (default 3; physical qubits = 2d²-1).
/// @param useLattice If true, two-qubit gates use lattice surgery; otherwise a
///                   transversal approximation is used (less accurate).
void populateSurfaceEncodePatterns(mlir::RewritePatternSet &patterns,
                                   unsigned distance = 3,
                                   bool useLattice = true);

/// @brief Type converter for surface-code encoding.
///
/// Maps `!quantum.bit` → `!ftqc.logical_qubit<#ftqc.ecc<surface, d>>`.
class SurfaceTypeConverter : public mlir::TypeConverter {
public:
  /// @param ctx      MLIR context.
  /// @param distance Code distance d (default 3).
  explicit SurfaceTypeConverter(mlir::MLIRContext *ctx, unsigned distance = 3);
};

/// @brief Build a distance-d surface-code patch initialisation circuit.
///
/// Creates d² data qubits in `|0⟩` and d²-1 ancilla qubits, then applies
/// the first syndrome cycle to project into the code space.
///
/// @param builder   OpBuilder at the insertion point.
/// @param loc       Source location for emitted ops.
/// @param distance  Code distance d.
/// @return A register value holding d²  data qubits in the code space.
mlir::Value buildSurfaceInitCircuit(mlir::OpBuilder &builder,
                                    mlir::Location loc,
                                    unsigned distance);

/// @brief Result of one surface-code syndrome extraction cycle.
struct SurfaceSyndromeCycle {
  mlir::Value dataReg;                       ///< Data register after the cycle.
  llvm::SmallVector<mlir::Value> xSyndromes; ///< X-plaquette measurement results.
  llvm::SmallVector<mlir::Value> zSyndromes; ///< Z-plaquette measurement results.
};

/// @brief Build one complete surface-code syndrome extraction cycle.
///
/// Measures all X plaquettes (via CNOT: ancilla → data) and all Z plaquettes
/// (via CNOT: data → ancilla) in the rotated surface code.
///
/// @param builder   OpBuilder at the insertion point.
/// @param loc       Source location.
/// @param dataReg   Register of d² data qubits in the code space.
/// @param distance  Code distance d.
/// @return SurfaceSyndromeCycle containing the updated data register and all
///         X- and Z-syndrome measurement results.
SurfaceSyndromeCycle buildSurfaceSyndromeCycle(mlir::OpBuilder &builder,
                                               mlir::Location loc,
                                               mlir::Value dataReg,
                                               unsigned distance);

/// @brief Build a lattice-surgery merge operation for surface-code CNOT.
///
/// Implements a logical CNOT between two adjacent surface-code patches via the
/// *merge-and-split* lattice-surgery protocol.  The appropriate merge type
/// (XX or ZZ) is selected by @p xxMerge.
///
/// @param builder   OpBuilder at the insertion point.
/// @param loc       Source location.
/// @param patch0    Control logical qubit (surface-code patch).
/// @param patch1    Target logical qubit (surface-code patch).
/// @param distance  Code distance d of both patches.
/// @param xxMerge   If true, perform XX-type merge (logical CNOT control);
///                  otherwise ZZ-type merge (target side).
void buildLatticeSurgery(mlir::OpBuilder &builder, mlir::Location loc,
                         mlir::Value patch0, mlir::Value patch1,
                         unsigned distance, bool xxMerge);

} // namespace ftqc
} // namespace mlir

#endif // FTQC_TRANSFORMS_SURFACECODEENCODING_H
