//===- DecomposeMCX.cpp - Multi-controlled-X decomposition pass -----------===//
//
// Rewrites each `ftqc.logical_mcx` (a multi-controlled NOT with a variadic
// list of controls) into a network of `ftqc.logical_toffoli` gates:
//   * 0 controls -> logical_x
//   * 1 control  -> logical_cnot
//   * 2 controls -> logical_toffoli
//   * n controls -> clean-ancilla V-chain (Barenco et al., 1995) using
//                   (n-2) ancilla and 2(n-1)-1 Toffoli gates.
// Each Toffoli consumes a freshly prepared CCZ magic state, matching the
// dialect's magic-state-injection model.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include <array>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct DecomposeMCX : public OpRewritePattern<LogicalMCXOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(LogicalMCXOp op,
                                PatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    auto *ctx = rewriter.getContext();
    SmallVector<Value> ctrl(op.getControls().begin(), op.getControls().end());
    Value tgt = op.getTarget();
    Type lq = tgt.getType();
    unsigned n = ctrl.size();

    // Emit one Toffoli a,b -> c using a fresh CCZ magic state.
    auto toffoli = [&](Value a, Value b, Value c) -> std::array<Value, 3> {
      auto ms = rewriter.create<PrepareMagicCCZOp>(
          loc, MagicStateType::get(ctx, MagicKind::CCZ));
      auto t = rewriter.create<LogicalToffoliOp>(loc, lq, lq, lq, a, b, c,
                                                 ms.getState());
      return {t.getResult(0), t.getResult(1), t.getResult(2)};
    };

    if (n == 0) {
      auto x = rewriter.create<LogicalXOp>(loc, lq, tgt);
      rewriter.replaceOp(op, ValueRange{x.getResult()});
      return success();
    }
    if (n == 1) {
      auto cx = rewriter.create<LogicalCNOTOp>(loc, lq, lq, ctrl[0], tgt);
      rewriter.replaceOp(op, ValueRange{cx.getControlOut(), cx.getTargetOut()});
      return success();
    }
    if (n == 2) {
      auto r = toffoli(ctrl[0], ctrl[1], tgt);
      rewriter.replaceOp(op, ValueRange{r[0], r[1], r[2]});
      return success();
    }

    // n >= 3: clean-ancilla V-chain with (n-2) ancilla.
    SmallVector<Value> anc;
    for (unsigned i = 0; i < n - 2; ++i)
      anc.push_back(rewriter.create<InitZeroOp>(loc, lq).getLogicalQubit());

    // Compute the AND of all controls into anc[n-3].
    {
      auto r = toffoli(ctrl[0], ctrl[1], anc[0]);
      ctrl[0] = r[0];
      ctrl[1] = r[1];
      anc[0] = r[2];
    }
    for (unsigned i = 2; i <= n - 2; ++i) {
      auto r = toffoli(ctrl[i], anc[i - 2], anc[i - 1]);
      ctrl[i] = r[0];
      anc[i - 2] = r[1];
      anc[i - 1] = r[2];
    }
    // Flip the target controlled on the last control AND the AND-chain.
    {
      auto r = toffoli(ctrl[n - 1], anc[n - 3], tgt);
      ctrl[n - 1] = r[0];
      anc[n - 3] = r[1];
      tgt = r[2];
    }
    // Uncompute the ancilla back to |0>.
    for (unsigned i = n - 2; i >= 2; --i) {
      auto r = toffoli(ctrl[i], anc[i - 2], anc[i - 1]);
      ctrl[i] = r[0];
      anc[i - 2] = r[1];
      anc[i - 1] = r[2];
    }
    {
      auto r = toffoli(ctrl[0], ctrl[1], anc[0]);
      ctrl[0] = r[0];
      ctrl[1] = r[1];
      anc[0] = r[2];
    }
    for (Value a : anc)
      rewriter.create<DeallocOp>(loc, a);

    SmallVector<Value> outs(ctrl.begin(), ctrl.end());
    outs.push_back(tgt);
    rewriter.replaceOp(op, outs);
    return success();
  }
};

struct FTQCDecomposeMCXPassImpl
    : public PassWrapper<FTQCDecomposeMCXPassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCDecomposeMCXPassImpl)

  StringRef getArgument() const override { return "ftqc-decompose-mcx"; }
  StringRef getDescription() const override {
    return "Decompose logical_mcx into a Toffoli network with clean ancilla";
  }

  void runOnOperation() override {
    RewritePatternSet patterns(&getContext());
    patterns.add<DecomposeMCX>(&getContext());
    if (failed(applyPatternsAndFoldGreedily(getOperation(),
                                            std::move(patterns))))
      signalPassFailure();
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createDecomposeMCXPass() {
  return std::make_unique<FTQCDecomposeMCXPassImpl>();
}
