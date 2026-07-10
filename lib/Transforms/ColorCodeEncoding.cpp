//===- ColorCodeEncoding.cpp - 2D color code encoding pass ----------------===//
//
// Lowers quantum circuits to 2D triangular color-code logical operations.
// The 2D color code supports transversal Clifford + T gates, avoiding the
// need for magic-state distillation.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static ECCAttr makeColorAttr(MLIRContext *ctx, unsigned d) {
  return ECCAttr::get(ctx, ECCKind::ColorCode, d, 1, {}, {});
}

static LogicalQubitType makeColorQubitType(MLIRContext *ctx, unsigned d) {
  return LogicalQubitType::get(ctx, makeColorAttr(ctx, d));
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCColorCodeEncodePassImpl
    : public PassWrapper<FTQCColorCodeEncodePassImpl,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCColorCodeEncodePassImpl)

  FTQCColorCodeEncodePassImpl() = default;
  explicit FTQCColorCodeEncodePassImpl(unsigned d) : distance_(d) {}

  StringRef getArgument() const override { return "ftqc-color-encode"; }
  StringRef getDescription() const override {
    return "Encode quantum circuits using 2D triangular color code";
  }

  void runOnOperation() override {
    auto mod  = getOperation();
    auto *ctx = &getContext();

    // Walk all functions and retype alloc results to color-code logical qubits.
    auto ccType = makeColorQubitType(ctx, distance_);
    (void)ccType;

    // Stub: emit a remark and succeed.  Full lowering follows the same pattern
    // as SteaneEncoding / SurfaceCodeEncoding but with ColorCode ECCAttr.
    mod.emitRemark("ftqc-color-encode: color code encoding (stub)");
  }

  unsigned distance_ = 3;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createColorCodeEncodePass() {
  return std::make_unique<FTQCColorCodeEncodePassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createColorCodeEncodePass(unsigned d) {
  return std::make_unique<FTQCColorCodeEncodePassImpl>(d);
}
