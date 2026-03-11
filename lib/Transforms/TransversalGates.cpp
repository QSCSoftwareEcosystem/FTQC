//===- TransversalGates.cpp - Transversal gate verification pass ----------===//
//
// Verifies that logical gates have valid transversal implementations for the
// target code, and optionally decomposes non-transversal gates via magic state
// injection.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"
#include "FTQC/Utils/PauliUtils.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// Transversal gate table
//===----------------------------------------------------------------------===//

/// Returns true if `gate` is transversal for the given code kind.
/// Reference: Eastin & Knill theorem + specific code analyses.
static bool isTransversal(StringRef gate, ECCKind kind) {
  switch (kind) {
  case ECCKind::Steane:
    // Steane [[7,1,3]] transversal gates: X, Z, H, CNOT, S†, CZ
    // Non-transversal: T, S (need magic state injection)
    return gate == "X"    || gate == "Z"    || gate == "H"  ||
           gate == "CNOT" || gate == "CZ"   || gate == "Sdg";
  case ECCKind::Surface:
    // Surface code transversal: X, Z, CNOT (via lattice surgery)
    // H is possible but requires code deformation
    return gate == "X"    || gate == "Z"    || gate == "CNOT";
  case ECCKind::ColorCode:
    // 2D color code: transversal Clifford + T
    return gate == "X"    || gate == "Z"    || gate == "H"  ||
           gate == "CNOT" || gate == "S"    || gate == "T"  ||
           gate == "CZ";
  case ECCKind::Concatenated:
    // Depends on base code; use Steane properties.
    return gate == "X"    || gate == "Z"    || gate == "H"  || gate == "CNOT";
  default:
    return gate == "X"    || gate == "Z";
  }
}

//===----------------------------------------------------------------------===//
// Pattern: LogicalSOp on non-color-code → magic state injection
//===----------------------------------------------------------------------===//

namespace {

struct DecomposeNonTransversalS : public OpRewritePattern<LogicalSOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(LogicalSOp op,
                                PatternRewriter &rewriter) const override {
    auto lqType = dyn_cast<LogicalQubitType>(op.getQubit().getType());
    if (!lqType)
      return failure();

    ECCKind kind = lqType.getCode().getKind();
    if (kind == ECCKind::ColorCode)
      return failure();  // transversal S is OK for color code

    auto *ctx  = rewriter.getContext();
    Location loc = op.getLoc();

    // Decompose S into T·T·Z^† ... actually S = T^2 so:
    // We inject two T gates (or use an |S⟩ magic state).
    auto msType  = MagicStateType::get(ctx, MagicKind::T);
    auto magic0  = rewriter.create<PrepareMagicTOp>(loc, msType);
    auto magic1  = rewriter.create<PrepareMagicTOp>(loc, msType);

    auto lqOut = lqType;
    auto t0    = rewriter.create<LogicalTOp>(loc, lqOut, op.getQubit(),
                                             magic0.getState());
    auto t1    = rewriter.create<LogicalTOp>(loc, lqOut, t0.getResult(),
                                             magic1.getState());
    rewriter.replaceOp(op, t1.getResult());
    return success();
  }
};

/// Annotate T gates on codes without transversal T with a diagnostic warning.
struct VerifyTransversalT : public OpRewritePattern<LogicalTOp> {
  VerifyTransversalT(MLIRContext *ctx, bool decompose)
      : OpRewritePattern<LogicalTOp>(ctx), decompose_(decompose) {}

  LogicalResult matchAndRewrite(LogicalTOp op,
                                PatternRewriter &rewriter) const override {
    auto lqType = dyn_cast<LogicalQubitType>(op.getQubit().getType());
    if (!lqType)
      return failure();

    ECCKind kind = lqType.getCode().getKind();
    if (kind == ECCKind::ColorCode)
      return failure();  // transversal T OK

    // T on Steane/surface code must use magic state; check magic state is present.
    if (!op.getMagicState()) {
      op.emitWarning("logical_t requires a magic state operand "
                     "(use ftqc.prepare_magic_t) for this code kind");
    }
    return failure();  // don't transform; just verify
  }

  bool decompose_;
};

} // namespace

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCTransversalGatePassImpl
    : public PassWrapper<FTQCTransversalGatePassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCTransversalGatePassImpl)

  FTQCTransversalGatePassImpl() = default;
  explicit FTQCTransversalGatePassImpl(bool decompose)
      : decompose_(decompose) {}

  StringRef getArgument() const override { return "ftqc-transversal-gates"; }

  void runOnOperation() override {
    auto func = getOperation();
    auto *ctx = &getContext();

    RewritePatternSet patterns(ctx);
    if (decompose_)
      patterns.add<DecomposeNonTransversalS>(ctx);
    patterns.add<VerifyTransversalT>(ctx, decompose_);

    if (failed(applyPatternsAndFoldGreedily(func, std::move(patterns))))
      signalPassFailure();
  }

  bool decompose_ = true;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createTransversalGatePass() {
  return std::make_unique<FTQCTransversalGatePassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createTransversalGatePass(bool d) {
  return std::make_unique<FTQCTransversalGatePassImpl>(d);
}
