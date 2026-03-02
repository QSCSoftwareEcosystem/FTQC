//===- ResourceEstimation.cpp - FTQC resource counting --------------------===//
#include "FTQC/Analysis/ResourceEstimation.h"
#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>
#include <sstream>

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// ResourceReport::toString
//===----------------------------------------------------------------------===//

std::string ResourceReport::toString() const {
  std::ostringstream ss;
  ss << "=== FTQC Resource Report ===\n";
  ss << "Logical qubits      : " << logicalQubits << "\n";
  ss << "Logical T gates     : " << logicalTGates  << "\n";
  ss << "Logical CNOT gates  : " << logicalCnotGates << "\n";
  ss << "Logical Clifford    : " << logicalCliffordGates << "\n";
  ss << "EC rounds           : " << ecRounds << "\n";
  ss << "Distillation rounds : " << distillationRounds << "\n";
  ss << "---\n";
  ss << "Physical data qubits  : " << physicalDataQubits << "\n";
  ss << "Physical ancilla      : " << physicalAncillaQubits << "\n";
  ss << "Physical total        : " << physicalTotalQubits << "\n";
  ss << "Distillation qubits   : " << distillationQubits << "\n";
  ss << "---\n";
  ss << "Physical error rate   : " << physicalErrorRate << "\n";
  ss << "Est. logical error    : " << estimatedLogicalError << "\n";
  return ss.str();
}

void ResourceReport::computePhysical(unsigned physPerLogical,
                                      unsigned ancillaPerLogical,
                                      double physError,
                                      double threshold) {
  physicalErrorRate      = physError;
  physicalDataQubits     = logicalQubits * physPerLogical;
  physicalAncillaQubits  = logicalQubits * ancillaPerLogical;
  physicalTotalQubits    = physicalDataQubits + physicalAncillaQubits
                         + distillationQubits;

  // Leading order estimate: p_L ≈ (p/p_th)^(d+1)/2
  if (threshold > 0.0 && physError < threshold) {
    double ratio = physError / threshold;
    estimatedLogicalError = std::pow(ratio, 2.0) * logicalTGates;
  } else {
    estimatedLogicalError = 1.0;   // above threshold
  }
}

//===----------------------------------------------------------------------===//
// ResourceEstimator
//===----------------------------------------------------------------------===//

ResourceReport ResourceEstimator::estimate(func::FuncOp func) {
  report_ = ResourceReport{};

  // Walk the function and count operations.
  func.walk([this](Operation *op) { visitOp(op); });

  return report_;
}

ResourceReport ResourceEstimator::estimateModule(ModuleOp mod) {
  report_ = ResourceReport{};
  mod.walk([this](Operation *op) { visitOp(op); });
  return report_;
}

void ResourceEstimator::visitOp(Operation *op) {
  llvm::TypeSwitch<Operation*, void>(op)
    .Case<InitZeroOp, InitPlusOp>([&](auto) {
      ++report_.logicalQubits;
    })
    .Case<EncodeOp>([&](auto) {
      ++report_.logicalQubits;
    })
    .Case<LogicalTOp>([&](auto) {
      ++report_.logicalTGates;
      // 15-to-1 distillation: 15 noisy states per T gate.
      report_.distillationRounds += 1;
      report_.distillationQubits  = std::max(
          report_.distillationQubits, (uint64_t)105);  // 7 logical blocks × 15
    })
    .Case<LogicalCNOTOp, LatticeSurgeryOp>([&](auto) {
      ++report_.logicalCnotGates;
    })
    .Case<LogicalHOp, LogicalXOp, LogicalZOp, LogicalSOp, LogicalYOp,
          LogicalCZOp>([&](auto) {
      ++report_.logicalCliffordGates;
    })
    .Case<ExtractSyndromeOp, SteaneXSyndromeOp, SteaneZSyndromeOp,
          ShorSyndromeOp, FlagSyndromeOp, SurfaceRoundOp>([&](auto) {
      ++report_.ecRounds;
    })
    .Case<Distill15to1Op>([&](auto) {
      report_.distillationRounds += 1;
    })
    .Case<Distill7to1Op>([&](auto) {
      report_.distillationRounds += 1;
    })
    .Case<LogicalMeasureOp>([&](auto) {
      ++report_.logicalMeasurements;
    })
    .Default([](auto) {});
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCResourceEstimatePassImpl
    : public PassWrapper<FTQCResourceEstimatePassImpl,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCResourceEstimatePassImpl)

  FTQCResourceEstimatePassImpl() = default;
  explicit FTQCResourceEstimatePassImpl(bool pr) : printReport_(pr) {}

  StringRef getArgument() const override { return "ftqc-resource-estimate"; }

  void runOnOperation() override {
    auto mod = getOperation();
    ResourceEstimator est;
    auto report = est.estimateModule(mod);

    // Annotate each function.
    mod.walk([&](func::FuncOp func) {
      OpBuilder builder(func.getBody().front().getTerminator());
      builder.setInsertionPointToStart(&func.getBody().front());
      builder.create<ResourceAnnotationOp>(
          func.getLoc(),
          builder.getI64IntegerAttr(report.physicalTotalQubits),
          builder.getI64IntegerAttr(report.logicalQubits),
          builder.getI64IntegerAttr(report.logicalTGates),
          builder.getI64IntegerAttr(report.logicalCnotGates),
          builder.getI64IntegerAttr(report.ecRounds),
          builder.getF64FloatAttr(report.estimatedLogicalError));
    });

    if (printReport_)
      llvm::errs() << report.toString();
  }

  bool printReport_ = true;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createResourceEstimatePass() {
  return std::make_unique<FTQCResourceEstimatePassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createResourceEstimatePass(bool pr) {
  return std::make_unique<FTQCResourceEstimatePassImpl>(pr);
}
