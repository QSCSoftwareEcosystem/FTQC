//===- FaultPathAnalysis.h - Fault path enumeration -----------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file FaultPathAnalysis.h
/// @brief Fault path enumeration for fault-tolerant quantum circuits.
///
/// ### Background
/// A *fault path* of weight w is a set of w gate locations in a circuit where
/// physical errors can occur such that the combined error **escapes detection**
/// by the error-correcting code — i.e., the net Pauli propagates to a logical
/// error.
///
/// For a distance-d code, any fault path of weight < ⌈(d+1)/2⌉ is correctable.
/// Paths of weight t = ⌊d/2⌋ + 1 (the *order* of the analysis) are the
/// leading-order contributors to the logical error rate.
///
/// ### Error rate model
/// The logical error rate scales as:
/// @code{.unparsed}
///   p_L ≈ C_t × p^t
/// @endcode
/// where C_t is the number of distinct weight-t fault paths and p is the
/// physical gate error rate.  `FaultPathAnalysis::estimateLeadingOrderRate()`
/// computes this product.
///
/// ### Usage
/// @code{.cpp}
/// FaultPathAnalysis fpa(/*maxOrder=*/2);
/// auto paths = fpa.analyze(funcOp);
/// double rate = fpa.estimateLeadingOrderRate(/*p=*/1e-3);
/// unsigned count = fpa.countFaultPaths(2);
/// @endcode
///
/// @note The analysis is exact but exponential in `maxOrder`.  For large
///       circuits use maxOrder ≤ 3.
///
/// @see ThresholdAnalysis.h — threshold-level reasoning (complementary).
/// @see StabilizerUtils.h   — `StabilizerCode` used internally.

#ifndef FTQC_ANALYSIS_FAULTPATHANALYSIS_H
#define FTQC_ANALYSIS_FAULTPATHANALYSIS_H

#include "FTQC/Utils/PauliUtils.h"
#include "FTQC/Utils/StabilizerUtils.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Operation.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include <vector>

namespace mlir::ftqc {

//===----------------------------------------------------------------------===//
// FaultLocation
//===----------------------------------------------------------------------===//

/// @brief A single fault location: one operation in the circuit plus the type
///        of error that occurs there.
///
/// A fault location identifies *where* a physical error can be injected and
/// *what kind* of Pauli error it introduces on a specific qubit operand.
struct FaultLocation {
  mlir::Operation *op; ///< The gate at which the fault occurs.
  unsigned qubitIndex; ///< Index of the affected qubit operand of @p op.
  Pauli errorType;     ///< Pauli error type: X, Y, or Z (I is not a fault).
};

//===----------------------------------------------------------------------===//
// FaultPath
//===----------------------------------------------------------------------===//

/// @brief An ordered sequence of fault locations whose combined error is an
///        undetected (logical) error.
///
/// A fault path is *undetected* if the Pauli errors at all locations propagate
/// through the remainder of the circuit and cannot be identified from the
/// syndrome measurement — i.e., the net error commutes with all stabilisers
/// but anti-commutes with a logical operator.
struct FaultPath {
  llvm::SmallVector<FaultLocation, 4> locations; ///< Fault sites in circuit order.

  /// @brief Return the weight of this path (number of fault locations).
  /// @return Number of distinct fault sites.
  unsigned weight() const { return static_cast<unsigned>(locations.size()); }
};

//===----------------------------------------------------------------------===//
// FaultPathAnalysis
//===----------------------------------------------------------------------===//

/// @brief Enumerates all fault paths up to a given weight in an FTQC function.
///
/// The analyser walks every gate in the function, identifies which qubit values
/// each gate touches, propagates hypothetical Pauli errors through the
/// subsequent Clifford circuit using `CliffordTableau`, and checks whether the
/// resulting error is detectable by the code's stabilisers.
///
/// Results are accumulated across multiple calls to `analyze()`, making it
/// possible to aggregate statistics over an entire module by calling `analyze()`
/// on each function and then querying `countFaultPaths()` /
/// `estimateLeadingOrderRate()`.
class FaultPathAnalysis {
public:
  /// @brief Construct a fault-path analyser.
  /// @param maxOrder Maximum fault path weight to enumerate (≥ 1).
  ///                 Analysis time is O(G^maxOrder) where G is the gate count.
  explicit FaultPathAnalysis(unsigned maxOrder) : maxOrder_(maxOrder) {}

  /// @brief Run the analysis on a single function.
  ///
  /// Enumerates all combinations of ≤ `maxOrder` gate locations and checks
  /// whether each combination constitutes an undetected fault path.
  ///
  /// @param func The `func.func` operation to analyse.
  /// @return All fault paths found (weight 1 … maxOrder).
  std::vector<FaultPath> analyze(mlir::func::FuncOp func);

  /// @brief Estimate the leading-order logical error rate.
  ///
  /// Computes C_t × p^t, where t = `maxOrder` and C_t is the number of
  /// weight-t fault paths found by the most recent `analyze()` call.
  ///
  /// @param physicalErrorRate Physical gate error probability p ∈ (0, 1).
  /// @return Estimated logical error rate contribution from weight-t paths.
  double estimateLeadingOrderRate(double physicalErrorRate) const;

  /// @brief Count fault paths of a specific weight.
  ///
  /// @param order Target weight (must be ≤ `maxOrder`).
  /// @return Number of distinct undetected fault paths of exactly `order`.
  unsigned countFaultPaths(unsigned order) const;

private:
  unsigned maxOrder_;                    ///< Maximum enumeration depth.
  std::vector<FaultPath> faultPaths_;    ///< Paths accumulated by analyze().

  /// @brief Recursive DFS over gate-location combinations.
  ///
  /// @param locations    Flat list of all gate operations in the function.
  /// @param remainingOrder Remaining faults to place (decrements by 1 per call).
  /// @param current      Path under construction.
  /// @param code         The stabiliser code for detectability checks.
  void enumeratePaths(
      llvm::ArrayRef<mlir::Operation *> locations,
      unsigned remainingOrder,
      llvm::SmallVector<FaultLocation, 4> &current,
      const StabilizerCode &code);
};

} // namespace mlir::ftqc

#endif // FTQC_ANALYSIS_FAULTPATHANALYSIS_H
