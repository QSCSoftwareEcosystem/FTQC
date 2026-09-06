//===- ResourceEstimation.h - FTQC resource counting ----------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file ResourceEstimation.h
/// @brief Physical qubit and gate resource estimation for fault-tolerant circuits.
///
/// The resource estimator walks FTQC IR and accumulates logical and physical
/// resource totals, including syndrome-extraction rounds and magic-state
/// distillation overhead.
///
/// ### How estimation works
/// 1. Walk every op in a `func.func` (or entire `ModuleOp`).
/// 2. Classify each op using its type (encode, logical gate, syndrome, …).
/// 3. Accumulate counts in a `ResourceReport`.
/// 4. Optionally call `ResourceReport::computePhysical()` to derive physical
///    qubit counts from the logical counts + code parameters.
///
/// ### Usage
/// @code{.cpp}
/// ResourceEstimator est;
/// ResourceReport r = est.estimate(funcOp);
/// llvm::errs() << r.toString();
/// @endcode
///
/// The `--ftqc-resource-estimate` pass wraps this class and optionally attaches
/// a `ftqc.resource_annotation` op to each function.

#ifndef FTQC_ANALYSIS_RESOURCEESTIMATION_H
#define FTQC_ANALYSIS_RESOURCEESTIMATION_H

#include "FTQC/Dialect/FTQCAttrs.h"   // ECCKind
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Operation.h"
#include <cstdint>
#include <string>

namespace mlir {
namespace func { class FuncOp; }
namespace ftqc {

//===----------------------------------------------------------------------===//
// ResourceReport
//===----------------------------------------------------------------------===//

/// @brief Aggregated resource counts for a fault-tolerant quantum circuit.
///
/// Fields are split into three categories:
///  - **Logical** — gate-level counts in the logical (encoded) circuit.
///  - **Physical** — derived counts after accounting for the ECC overhead.
///  - **Distillation** — extra qubit cost for magic-state factories.
struct ResourceReport {
  //------------------------------------------------------------------------
  // Logical resource counts
  //------------------------------------------------------------------------
  uint64_t logicalQubits        = 0; ///< Number of distinct logical qubits allocated.
  uint64_t logicalTGates        = 0; ///< Number of logical T gates (require magic states).
  uint64_t logicalCnotGates     = 0; ///< Number of logical CNOT gates.
  uint64_t logicalCliffordGates = 0; ///< Other single-qubit Clifford gates (H, S, X, Z, Y).
  uint64_t toffoliGates         = 0; ///< logical_toffoli (+ Toffoli-equivalent of any raw logical_mcx).
  uint64_t cczStates            = 0; ///< CCZ magic states prepared (prepare_magic_ccz).
  uint64_t cczDistillationRounds = 0; ///< Explicit CCZ distillation factories (distill_ccz).
  uint64_t logicalMeasurements  = 0; ///< Number of `ftqc.logical_measure` ops.
  uint64_t ecRounds             = 0; ///< Total syndrome-extraction rounds inserted.

  /// Total T-count including Toffolis at the standard Clifford+T cost of 7 T
  /// per Toffoli (Nielsen & Chuang).  This is the reported T-equivalent metric.
  uint64_t totalTCount() const { return logicalTGates + 7u * toffoliGates; }

  //------------------------------------------------------------------------
  // Physical resource counts (derived after encoding)
  //------------------------------------------------------------------------
  uint64_t physicalDataQubits    = 0; ///< Data qubits (logicalQubits × qubits-per-logical).
  uint64_t physicalAncillaQubits = 0; ///< Ancilla qubits for syndrome extraction.
  uint64_t physicalTotalQubits   = 0; ///< physicalDataQubits + physicalAncillaQubits.
  uint64_t physicalGates         = 0; ///< Total physical gate count (estimated).

  //------------------------------------------------------------------------
  // Distillation overhead
  //------------------------------------------------------------------------
  uint64_t distillationRounds = 0; ///< Number of distillation calls in the circuit.
  uint64_t distillationQubits = 0; ///< Peak ancilla qubits for magic-state factories.

  //------------------------------------------------------------------------
  // Error estimates
  //------------------------------------------------------------------------
  double physicalErrorRate     = 0.0; ///< Assumed physical gate error rate (per gate).
  double estimatedLogicalError = 0.0; ///< Estimated logical error rate for full circuit.

  //------------------------------------------------------------------------
  // ECC metadata (detected from IR qubit types)
  //------------------------------------------------------------------------
  ECCKind  eccKind     = ECCKind::Steane; ///< ECC family detected from logical qubit types.
  uint32_t eccDistance = 3;              ///< Code distance detected from logical qubit types.
  bool     eccDetected = false;          ///< True once ECC info has been read from IR.

  //------------------------------------------------------------------------
  // Circuit depth
  //------------------------------------------------------------------------
  uint64_t logicalDepth  = 0; ///< Critical path depth in logical gate count.
  uint64_t physicalDepth = 0; ///< Estimated physical circuit depth.

  /// @brief Serialise the report to a human-readable multi-line string.
  std::string toString() const;

  /// @brief Derive physical resource counts from logical counts + code params.
  ///
  /// Sets `physicalDataQubits`, `physicalAncillaQubits`, `physicalTotalQubits`,
  /// and `estimatedLogicalError` using leading-order fault-tolerance formulae.
  ///
  /// Logical error rate formula (leading order):
  ///   p_L ≈ 0.1 × (p / p_th)^⌈(d+1)/2⌉  per syndrome round per logical qubit
  ///   p_total ≈ p_L × ecRounds × logicalQubits
  ///
  /// @param physicalPerLogical  Physical data qubits per logical qubit.
  /// @param ancillaPerLogical   Ancilla qubits per logical qubit (reused each round).
  /// @param physicalError       Assumed physical gate error rate p.
  /// @param threshold           Code-specific fault-tolerance threshold p_th.
  /// @param distance            Code distance d (used in error-rate exponent).
  void computePhysical(unsigned physicalPerLogical, unsigned ancillaPerLogical,
                       double physicalError, double threshold,
                       unsigned distance = 3);
};

//===----------------------------------------------------------------------===//
// ResourceEstimator
//===----------------------------------------------------------------------===//

/// @brief Walks FTQC IR and accumulates resource counts into a ResourceReport.
///
/// The estimator is not an MLIR analysis class; it is a plain C++ object that
/// can be called from a pass or used standalone.
///
/// ### Op classification
/// | FTQC op category | Counted as |
/// |---|---|
/// | `ftqc.init_zero` | logicalQubits++ |
/// | `ftqc.logical_h/x/y/z/s` | logicalCliffordGates++ |
/// | `ftqc.logical_cnot/cz` | logicalCnotGates++ |
/// | `ftqc.logical_t` | logicalTGates++ |
/// | `ftqc.*_syndrome`, `ftqc.ec_round` | ecRounds++ |
/// | `ftqc.prepare_magic_t` | distillationRounds++ |
/// | `ftqc.logical_measure` | logicalMeasurements++ |
class ResourceEstimator {
public:
  /// @brief Walk a single function and accumulate resource counts.
  /// @param func  The `func.func` to analyse.
  /// @return Populated ResourceReport.
  ResourceReport estimate(mlir::func::FuncOp func);

  /// @brief Walk every function in a module and accumulate totals.
  /// @param mod  The top-level `ModuleOp`.
  /// @return Aggregated ResourceReport across all functions.
  ResourceReport estimateModule(mlir::ModuleOp mod);

private:
  ResourceReport report_;

  void visitOp(mlir::Operation *op);          ///< Dispatch to specialised visitor.
  void tryDetectECC(mlir::Type t);            ///< Extract ECC info from a logical qubit type.
  void visitEncode(mlir::Operation *op);      ///< Handle ftqc.init_zero / encode ops.
  void visitLogicalGate(mlir::Operation *op); ///< Handle logical gate ops.
  void visitSyndrome(mlir::Operation *op);    ///< Handle syndrome extraction ops.
  void visitCorrection(mlir::Operation *op);  ///< Handle ftqc.correct ops.
  void visitDistillation(mlir::Operation *op);///< Handle distillation ops.
  void visitMeasure(mlir::Operation *op);     ///< Handle ftqc.logical_measure.
};

} // namespace ftqc
} // namespace mlir

#endif // FTQC_ANALYSIS_RESOURCEESTIMATION_H
