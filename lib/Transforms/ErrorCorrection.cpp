//===- ErrorCorrection.cpp - Error correction passes ----------------------===//
#include "FTQC/Transforms/ErrorCorrection.h"
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
// Insert correction after syndrome extraction
//===----------------------------------------------------------------------===//

namespace {

/// After each ExtractSyndromeOp, insert a CorrectOp.
struct InsertCorrectAfterSyndrome
    : public OpRewritePattern<ExtractSyndromeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ExtractSyndromeOp op,
                                PatternRewriter &rewriter) const override {
    // Check if already followed by a correction.
    for (auto *user : op.getOutLogicalQubit().getUsers())
      if (isa<CorrectOp>(user))
        return failure();

    rewriter.setInsertionPointAfter(op);
    rewriter.create<CorrectOp>(op.getLoc(),
                               op.getOutLogicalQubit().getType(),
                               op.getOutLogicalQubit(),
                               op.getSyndrome());
    return success();
  }
};

/// Insert correction after Steane X syndrome extraction.
struct InsertCorrectAfterSteaneX
    : public OpRewritePattern<SteaneXSyndromeOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(SteaneXSyndromeOp op,
                                PatternRewriter &rewriter) const override {
    for (auto *user : op.getOutLogicalQubit().getUsers())
      if (isa<CorrectOp>(user))
        return failure();
    rewriter.setInsertionPointAfter(op);
    rewriter.create<CorrectOp>(op.getLoc(),
                               op.getOutLogicalQubit().getType(),
                               op.getOutLogicalQubit(),
                               op.getXSyndrome());
    return success();
  }
};

/// Insert correction after Steane Z syndrome extraction.
struct InsertCorrectAfterSteaneZ
    : public OpRewritePattern<SteaneZSyndromeOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(SteaneZSyndromeOp op,
                                PatternRewriter &rewriter) const override {
    for (auto *user : op.getOutLogicalQubit().getUsers())
      if (isa<CorrectOp>(user))
        return failure();
    rewriter.setInsertionPointAfter(op);
    rewriter.create<CorrectOp>(op.getLoc(),
                               op.getOutLogicalQubit().getType(),
                               op.getOutLogicalQubit(),
                               op.getZSyndrome());
    return success();
  }
};

} // namespace

void mlir::ftqc::populateCorrectionPatterns(RewritePatternSet &patterns) {
  auto *ctx = patterns.getContext();
  patterns.add<InsertCorrectAfterSyndrome>(ctx);
  patterns.add<InsertCorrectAfterSteaneX>(ctx);
  patterns.add<InsertCorrectAfterSteaneZ>(ctx);
}

//===----------------------------------------------------------------------===//
// Pauli frame optimization
//===----------------------------------------------------------------------===//

namespace {

/// Replace CorrectOp + subsequent gate with pauli_frame_update.
/// This defers the physical correction to measurement time.
struct DeferCorrectionToFrame
    : public OpRewritePattern<CorrectOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(CorrectOp op,
                                PatternRewriter &rewriter) const override {
    auto *ctx = rewriter.getContext();
    auto frameType = PauliFrameType::get(ctx, 1);

    // Check if a frame already exists in scope.
    // In a full implementation we would walk to find or create the frame.
    // Here we create a new frame and update it.
    auto frame = rewriter.create<CreateFrameOp>(op.getLoc(), frameType);
    rewriter.replaceOpWithNewOp<PauliFrameUpdateOp>(
        op, frameType, frame.getFrame(), op.getSyndrome(),
        rewriter.getI32IntegerAttr(0));
    return success();
  }
};

} // namespace

void mlir::ftqc::populatePauliFramePatterns(RewritePatternSet &patterns) {
  patterns.add<DeferCorrectionToFrame>(patterns.getContext());
}

//===----------------------------------------------------------------------===//
// Passes
//===----------------------------------------------------------------------===//

namespace {

struct FTQCInsertCorrectionPassImpl
    : public PassWrapper<FTQCInsertCorrectionPassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCInsertCorrectionPassImpl)
  StringRef getArgument() const override { return "ftqc-insert-correction"; }
  void runOnOperation() override {
    auto func = getOperation();
    RewritePatternSet pats(&getContext());
    populateCorrectionPatterns(pats);
    if (failed(applyPatternsAndFoldGreedily(func, std::move(pats))))
      signalPassFailure();
  }
};

struct FTQCPauliFrameOptPassImpl
    : public PassWrapper<FTQCPauliFrameOptPassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCPauliFrameOptPassImpl)
  StringRef getArgument() const override { return "ftqc-pauli-frame-opt"; }
  void runOnOperation() override {
    auto func = getOperation();
    RewritePatternSet pats(&getContext());
    populatePauliFramePatterns(pats);
    if (failed(applyPatternsAndFoldGreedily(func, std::move(pats))))
      signalPassFailure();
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createInsertCorrectionPass() {
  return std::make_unique<FTQCInsertCorrectionPassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createPauliFrameOptPass() {
  return std::make_unique<FTQCPauliFrameOptPassImpl>();
}
