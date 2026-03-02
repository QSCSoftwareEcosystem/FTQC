//===- FaultPathAnalysis.cpp - Fault path analysis pass -------------------===//
#include "FTQC/Analysis/FaultPathAnalysis.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// FaultPathAnalysis
//===----------------------------------------------------------------------===//

std::vector<FaultPath> FaultPathAnalysis::analyze(func::FuncOp func) {
  faultPaths_.clear();

  // Collect all gate locations in the function.
  SmallVector<Operation*> locations;
  func.walk([&](Operation *op) {
    if (isa<LogicalHOp, LogicalXOp, LogicalZOp, LogicalCNOTOp,
             LogicalTOp, LogicalSOp, ExtractSyndromeOp,
             SteaneXSyndromeOp, SteaneZSyndromeOp>(op))
      locations.push_back(op);
  });

  // For each location, enumerate faults of all 3 Pauli types.
  // Simple leading-order analysis: just count weight-1 paths that
  // escape error detection.
  if (maxOrder_ >= 1) {
    for (auto *op : locations) {
      for (Pauli p : {Pauli::X, Pauli::Y, Pauli::Z}) {
        FaultPath path;
        path.locations.push_back({op, 0, p});
        // In a full implementation we would propagate the error through the
        // circuit and check if it becomes a logical error.
        // For now, add all as potential paths (conservative).
        faultPaths_.push_back(path);
      }
    }
  }

  return faultPaths_;
}

double FaultPathAnalysis::estimateLeadingOrderRate(double p) const {
  unsigned count = countFaultPaths(1);
  return count * std::pow(p, 1);
}

unsigned FaultPathAnalysis::countFaultPaths(unsigned order) const {
  unsigned count = 0;
  for (auto &fp : faultPaths_)
    if (fp.weight() == order)
      ++count;
  return count;
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCFaultPathPassImpl
    : public PassWrapper<FTQCFaultPathPassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCFaultPathPassImpl)

  FTQCFaultPathPassImpl() = default;
  explicit FTQCFaultPathPassImpl(unsigned mo) : maxOrder_(mo) {}

  StringRef getArgument() const override { return "ftqc-fault-path"; }

  void runOnOperation() override {
    auto func = getOperation();
    FaultPathAnalysis analysis(maxOrder_);
    auto paths = analysis.analyze(func);

    llvm::errs() << "Fault path analysis for @" << func.getName()
                 << ": found " << paths.size()
                 << " fault paths of order ≤ " << maxOrder_ << "\n";

    for (unsigned o = 1; o <= maxOrder_; ++o) {
      unsigned cnt = analysis.countFaultPaths(o);
      if (cnt > 0)
        llvm::errs() << "  Order-" << o << " paths: " << cnt
                     << " (rate ≈ " << cnt << " × p^" << o << ")\n";
    }
  }

  unsigned maxOrder_ = 1;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createFaultPathAnalysisPass() {
  return std::make_unique<FTQCFaultPathPassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createFaultPathAnalysisPass(unsigned mo) {
  return std::make_unique<FTQCFaultPathPassImpl>(mo);
}
