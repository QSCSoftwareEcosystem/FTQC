//===- ThresholdAnalysis.cpp - Threshold analysis pass --------------------===//
#include "FTQC/Analysis/ThresholdAnalysis.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <sstream>

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// ThresholdValues
//===----------------------------------------------------------------------===//

double ThresholdValues::getThreshold(ECCKind kind) {
  switch (kind) {
  case ECCKind::Steane:      return kSteaneThreshold;
  case ECCKind::Surface:     return kSurfaceThreshold;
  case ECCKind::ColorCode:   return kColorCodeThreshold;
  case ECCKind::Concatenated:return kConcatThreshold;
  case ECCKind::Repetition:  return kRepetitionThreshold;
  default:                   return 1.0e-3;
  }
}

//===----------------------------------------------------------------------===//
// ThresholdAnalyzer
//===----------------------------------------------------------------------===//

/// Leading-order logical error rate: p_L ≈ A * (p/p_th)^t
/// where t = ceil((d+1)/2), A ≈ 1 (code-dependent constant).
double ThresholdAnalyzer::estimateLogicalErrorRate(ECCKind kind, uint32_t d,
                                                   double p) {
  double pth = ThresholdValues::getThreshold(kind);
  if (p >= pth) return 1.0;  // above threshold
  double t = std::ceil((d + 1.0) / 2.0);
  return std::pow(p / pth, t);
}

uint32_t ThresholdAnalyzer::minimumDistanceForTarget(ECCKind kind, double p,
                                                     double target) {
  double pth = ThresholdValues::getThreshold(kind);
  if (p >= pth) return UINT32_MAX;  // unachievable

  // Solve (p/pth)^t = target for t: t = log(target) / log(p/pth).
  double ratio = p / pth;
  if (ratio <= 0.0 || ratio >= 1.0) return 3;
  double t = std::log(target) / std::log(ratio);
  // t = ceil((d+1)/2) → d = 2*ceil(t) - 1.
  uint32_t d = static_cast<uint32_t>(2.0 * std::ceil(t) - 1.0);
  return std::max(d, (uint32_t)3);
}

ThresholdResult ThresholdAnalyzer::analyze(ECCKind kind, uint32_t d,
                                           uint64_t gates) const {
  double pth    = ThresholdValues::getThreshold(kind);
  double pL     = estimateLogicalErrorRate(kind, d, physicalError_) * gates;
  uint32_t recD = minimumDistanceForTarget(kind, physicalError_, targetError_);

  ThresholdResult r;
  r.belowThreshold       = (physicalError_ < pth);
  r.physicalRate         = physicalError_;
  r.threshold            = pth;
  r.safetyMargin         = pth - physicalError_;
  r.recommendedDistance  = recD;
  r.achievableLogicalError = pL;
  return r;
}

std::string ThresholdResult::toString() const {
  std::ostringstream ss;
  ss << (belowThreshold ? "[OK]   " : "[WARN] ")
     << "p = " << physicalRate
     << "  threshold = " << threshold
     << "  margin = " << safetyMargin << "\n"
     << "  Recommended d = " << recommendedDistance
     << "  p_L ≈ " << achievableLogicalError << "\n";
  return ss.str();
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCThresholdPassImpl
    : public PassWrapper<FTQCThresholdPassImpl, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCThresholdPassImpl)

  FTQCThresholdPassImpl() = default;
  FTQCThresholdPassImpl(double pe, double te)
      : physicalError_(pe), targetError_(te) {}

  StringRef getArgument() const override { return "ftqc-threshold"; }

  void runOnOperation() override {
    auto mod = getOperation();
    ThresholdAnalyzer analyzer(physicalError_, targetError_);

    mod.walk([&](InitZeroOp op) {
      auto lqType = dyn_cast<LogicalQubitType>(op.getLogicalQubit().getType());
      if (!lqType) return;
      auto code  = lqType.getCode();
      auto result = analyzer.analyze(code.getKind(), code.getDistance(), 1000);
      if (!result.belowThreshold)
        op.emitWarning("circuit may be above fault-tolerance threshold: ")
            << result.toString();
    });
  }

  double physicalError_ = 1e-3;
  double targetError_   = 1e-10;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createThresholdAnalysisPass() {
  return std::make_unique<FTQCThresholdPassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createThresholdAnalysisPass(
    double pe, double te) {
  return std::make_unique<FTQCThresholdPassImpl>(pe, te);
}
