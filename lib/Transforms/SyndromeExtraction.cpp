//===- SyndromeExtraction.cpp - Syndrome extraction pass ------------------===//
//
// Inserts syndrome extraction operations after every logical gate.
// Supports: Steane transversal, Shor cat-state, and Chao-Reichardt flag.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static SyndromeType getSyndromeTypeForCode(MLIRContext *ctx,
                                           LogicalQubitType lqType) {
  auto code = lqType.getCode();
  unsigned width = 0;
  switch (code.getKind()) {
  case ECCKind::Steane:
    width = 6;   // 3 X + 3 Z stabilizers
    break;
  case ECCKind::Surface: {
    unsigned d = code.getDistance();
    width = 2 * (d * d - 1);
    break;
  }
  case ECCKind::ColorCode: {
    unsigned d = code.getDistance();
    width = 2 * (d * d - 1) / 2;
    break;
  }
  default:
    width = 6;
    break;
  }
  return SyndromeType::get(ctx, width);
}

//===----------------------------------------------------------------------===//
// Patterns – insert syndrome extraction after logical gates
//===----------------------------------------------------------------------===//

namespace {

/// After each logical gate, insert an extract_syndrome.
template <typename GateOp>
struct InsertSyndromeAfterGate : public OpRewritePattern<GateOp> {
  InsertSyndromeAfterGate(MLIRContext *ctx, const std::string &method,
                           unsigned rounds)
      : OpRewritePattern<GateOp>(ctx),
        method_(method), rounds_(rounds) {}

  LogicalResult matchAndRewrite(GateOp op,
                                PatternRewriter &rewriter) const override {
    // Don't insert if any result is already consumed by a syndrome/EC op.
    for (Value result : op->getResults()) {
      for (auto *user : result.getUsers()) {
        if (isa<ExtractSyndromeOp, ECRoundOp, CorrectOp,
                SteaneXSyndromeOp, SteaneZSyndromeOp,
                ShorSyndromeOp, FlagSyndromeOp>(user))
          return failure();
      }
    }

    auto *ctx  = rewriter.getContext();
    Location loc = op.getLoc();

    // Use the first LogicalQubit result.
    Value lqResult;
    for (Value v : op->getResults()) {
      if (isa<LogicalQubitType>(v.getType())) {
        lqResult = v;
        break;
      }
    }
    if (!lqResult)
      return failure();

    auto lqType  = dyn_cast<LogicalQubitType>(lqResult.getType());
    auto synType = getSyndromeTypeForCode(ctx, lqType);

    rewriter.setInsertionPointAfter(op);

    if (method_ == "steane") {
      // Steane method: separate X and Z syndrome extraction.
      auto xOp = rewriter.create<SteaneXSyndromeOp>(
          loc, lqType, synType, lqResult);
      auto zOp = rewriter.create<SteaneZSyndromeOp>(
          loc, lqType, synType, xOp.getOutLogicalQubit());
      (void)zOp;
    } else if (method_ == "shor") {
      rewriter.create<ShorSyndromeOp>(loc, lqType, synType, lqResult);
    } else {  // flag
      auto flagOp = rewriter.create<FlagSyndromeOp>(
          loc, lqType, synType, rewriter.getI1Type(), lqResult);
      (void)flagOp;
    }

    return success();
  }

  std::string method_;
  unsigned    rounds_;
};

} // namespace

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCInsertSyndromePassImpl
    : public PassWrapper<FTQCInsertSyndromePassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCInsertSyndromePassImpl)

  FTQCInsertSyndromePassImpl() = default;
  FTQCInsertSyndromePassImpl(std::string m, unsigned r)
      : method_(std::move(m)), rounds_(r) {}

  StringRef getArgument() const override { return "ftqc-insert-syndrome"; }

  void runOnOperation() override {
    auto func = getOperation();
    auto *ctx = &getContext();

    RewritePatternSet patterns(ctx);
    patterns.add<InsertSyndromeAfterGate<LogicalHOp>>(ctx, method_, rounds_);
    patterns.add<InsertSyndromeAfterGate<LogicalXOp>>(ctx, method_, rounds_);
    patterns.add<InsertSyndromeAfterGate<LogicalZOp>>(ctx, method_, rounds_);
    patterns.add<InsertSyndromeAfterGate<LogicalCNOTOp>>(ctx, method_, rounds_);
    patterns.add<InsertSyndromeAfterGate<LogicalTOp>>(ctx, method_, rounds_);

    if (failed(applyPatternsAndFoldGreedily(func, std::move(patterns))))
      signalPassFailure();
  }

  std::string method_ = "steane";
  unsigned    rounds_ = 3;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createInsertSyndromePass() {
  return std::make_unique<FTQCInsertSyndromePassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createInsertSyndromePass(
    const std::string &m, unsigned r) {
  return std::make_unique<FTQCInsertSyndromePassImpl>(m, r);
}
