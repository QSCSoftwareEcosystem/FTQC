//===- ThresholdAnalysis.h - Fault-tolerance threshold analysis -----------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file ThresholdAnalysis.h
/// @brief Fault-tolerance threshold analysis for FTQC circuits.
///
/// ### Background
/// A quantum error-correcting code has a *fault-tolerance threshold* p_th: if
/// the physical gate error rate p < p_th, increasing the code distance d
/// exponentially suppresses the logical error rate.  Above the threshold,
/// adding more qubits makes things worse.
///
/// The leading-order logical error rate formula is:
/// @code{.unparsed}
///   p_L ≈ A × (p / p_th)^{⌈(d+1)/2⌉}
/// @endcode
/// where A is a code-dependent prefactor (typically O(1)).
///
/// ### Threshold table (approximate, depolarising noise model)
/// | Code                | Threshold |
/// |---------------------|-----------|
/// | Steane \[\[7,1,3\]\]   | ~1 %    |
/// | Surface code        | ~1 %      |
/// | 2D color code       | ~0.5–1.3% |
/// | Concatenated Steane | ~0.5 %    |
/// | Repetition code     | ~50 % (bit-flip only) |
///
/// ### Usage
/// @code{.cpp}
/// ThresholdAnalyzer analyzer(/*physicalError=*/1e-3, /*targetLogicalError=*/1e-10);
/// ThresholdResult r = analyzer.analyze(ECCKind::Surface, /*d=*/5, /*gates=*/1000);
/// if (!r.belowThreshold)
///   llvm::errs() << "WARNING: above threshold!\n";
/// llvm::errs() << r.toString();
/// @endcode

#ifndef FTQC_ANALYSIS_THRESHOLDANALYSIS_H
#define FTQC_ANALYSIS_THRESHOLDANALYSIS_H

#include "FTQC/Dialect/FTQCAttrs.h"
#include <cstdint>
#include <string>

namespace mlir::ftqc {

//===----------------------------------------------------------------------===//
// ThresholdValues
//===----------------------------------------------------------------------===//

/// @brief Known fault-tolerance threshold values from the literature.
///
/// All thresholds assume a depolarising noise model (equal probability of X,
/// Y, and Z errors per physical gate).  Real thresholds are circuit-level and
/// noise-model dependent.
struct ThresholdValues {
  /// @brief Return the threshold for a given code kind.
  /// @param kind ECC family.
  /// @return Approximate fault-tolerance threshold in [0, 1].
  static double getThreshold(ECCKind kind);

  static constexpr double kSteaneThreshold     = 0.01;  ///< Steane [[7,1,3]] ~1%.
  static constexpr double kSurfaceThreshold    = 0.01;  ///< Surface code ~1%.
  static constexpr double kColorCodeThreshold  = 0.01;  ///< 2D color code ~1% (model dependent).
  static constexpr double kConcatThreshold     = 0.005; ///< Concatenated Steane ~0.5%.
  static constexpr double kRepetitionThreshold = 0.5;   ///< Repetition code (bit-flip only).
};

//===----------------------------------------------------------------------===//
// ThresholdResult
//===----------------------------------------------------------------------===//

/// @brief Result of a threshold analysis query.
struct ThresholdResult {
  bool     belowThreshold;         ///< True if p_phys < p_th (error correction improves fidelity).
  double   physicalRate;           ///< The physical error rate that was analysed.
  double   threshold;              ///< Code-specific threshold p_th.
  double   safetyMargin;           ///< p_th - p_phys; positive means the circuit is safe.
  uint32_t recommendedDistance;    ///< Minimum d needed to hit the target logical error rate.
  double   achievableLogicalError; ///< p_L at `recommendedDistance`.

  /// @brief Serialise the result to a human-readable string.
  std::string toString() const;
};

//===----------------------------------------------------------------------===//
// ThresholdAnalyzer
//===----------------------------------------------------------------------===//

/// @brief Analyses whether a given physical error rate is below the threshold
///        for a specific ECC and reports the recommended code distance.
class ThresholdAnalyzer {
public:
  /// @param physicalError      Physical gate error rate (probability per gate).
  /// @param targetLogicalError Target logical error rate per logical gate.
  ThresholdAnalyzer(double physicalError, double targetLogicalError)
      : physicalError_(physicalError), targetError_(targetLogicalError) {}

  /// @brief Analyse a single encoded code block.
  ///
  /// @param kind      ECC family.
  /// @param distance  Code distance d.
  /// @param gateCount Number of logical gates in the circuit (used to bound the
  ///                  total logical error probability).
  /// @return ThresholdResult with all computed fields filled.
  ThresholdResult analyze(ECCKind kind, uint32_t distance,
                          uint64_t gateCount) const;

  /// @brief Estimate the logical error rate using the leading-order formula.
  ///
  /// Formula: p_L ≈ A × (p / p_th)^{⌈(d+1)/2⌉}
  /// where A is a code prefactor (~O(d²) for surface codes).
  ///
  /// @param kind         ECC family.
  /// @param distance     Code distance d.
  /// @param physicalRate Physical gate error rate p.
  /// @return Estimated logical error rate p_L per logical gate.
  static double estimateLogicalErrorRate(ECCKind kind, uint32_t distance,
                                         double physicalRate);

  /// @brief Find the minimum code distance to achieve a target logical rate.
  ///
  /// Binary-searches for the smallest odd d such that
  /// `estimateLogicalErrorRate(kind, d, physicalRate) ≤ targetLogicalRate`.
  ///
  /// @param kind              ECC family.
  /// @param physicalRate      Physical gate error rate p.
  /// @param targetLogicalRate Desired p_L per logical gate.
  /// @return Minimum distance d (returns 99 if impossible at any reasonable d).
  static uint32_t minimumDistanceForTarget(ECCKind kind, double physicalRate,
                                           double targetLogicalRate);

private:
  double physicalError_; ///< Physical error rate set at construction.
  double targetError_;   ///< Target logical error rate set at construction.
};

} // namespace mlir::ftqc

#endif // FTQC_ANALYSIS_THRESHOLDANALYSIS_H
