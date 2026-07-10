//===- SurfaceCodeEncoding.cpp - Surface code encoding pass ---------------===//
//
// Lowers quantum circuits to surface-code logical operations.
// Uses lattice surgery for 2-qubit gates on separate 2D patches.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Transforms/SurfaceCodeEncoding.h"
#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// SurfaceTypeConverter
//===----------------------------------------------------------------------===//

SurfaceTypeConverter::SurfaceTypeConverter(MLIRContext *ctx, unsigned distance) {
  addConversion([](Type t) { return t; });
}

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

namespace {

static ECCAttr makeSurfaceAttr(MLIRContext *ctx, unsigned d) {
  return ECCAttr::get(ctx, ECCKind::Surface, d, 1, {}, {});
}

static LogicalQubitType makeSurfaceQubitType(MLIRContext *ctx, unsigned d) {
  return LogicalQubitType::get(ctx, makeSurfaceAttr(ctx, d));
}

/// quantum.custom "H" on surface code → ftqc.logical_h
struct SurfaceHPattern : public RewritePattern {
  SurfaceHPattern(MLIRContext *ctx, unsigned d)
      : RewritePattern("quantum.custom", 1, ctx), d_(d) {}

  LogicalResult matchAndRewrite(Operation *op,
                                PatternRewriter &rewriter) const override {
    auto gn = op->getAttrOfType<StringAttr>("gate_name");
    if (!gn || gn.getValue() != "H")
      return failure();
    if (!isa<LogicalQubitType>(op->getOperand(0).getType()))
      return failure();
    rewriter.replaceOpWithNewOp<LogicalHOp>(
        op, makeSurfaceQubitType(rewriter.getContext(), d_), op->getOperand(0));
    return success();
  }
  unsigned d_;
};

/// quantum.custom "CNOT" on surface code → ftqc.lattice_surgery
struct SurfaceCNOTPattern : public RewritePattern {
  SurfaceCNOTPattern(MLIRContext *ctx, unsigned d, bool useLattice)
      : RewritePattern("quantum.custom", 1, ctx),
        d_(d), useLattice_(useLattice) {}

  LogicalResult matchAndRewrite(Operation *op,
                                PatternRewriter &rewriter) const override {
    auto gn = op->getAttrOfType<StringAttr>("gate_name");
    if (!gn || gn.getValue() != "CNOT")
      return failure();
    if (op->getNumOperands() != 2)
      return failure();

    auto *ctx = rewriter.getContext();
    auto lqType  = makeSurfaceQubitType(ctx, d_);
    auto synType = SyndromeType::get(ctx, 2 * (d_ * d_ - 1));
    auto ctrl = op->getOperand(0), tgt = op->getOperand(1);

    if (!isa<LogicalQubitType>(ctrl.getType()) ||
        !isa<LogicalQubitType>(tgt.getType()))
      return failure();

    if (useLattice_) {
      // Use lattice surgery (XX merge = logical CNOT).
      rewriter.replaceOpWithNewOp<LatticeSurgeryOp>(
          op, lqType, lqType, synType, ctrl, tgt,
          rewriter.getI32IntegerAttr(0));  // kind=0 = XX merge
    } else {
      // Transversal CNOT (valid only if patches share the same 2D plane).
      rewriter.replaceOpWithNewOp<LogicalCNOTOp>(
          op, lqType, lqType, ctrl, tgt);
    }
    return success();
  }

  unsigned d_;
  bool useLattice_;
};

} // namespace

void mlir::ftqc::populateSurfaceEncodePatterns(RewritePatternSet &patterns,
                                               unsigned distance,
                                               bool useLattice) {
  auto *ctx = patterns.getContext();
  patterns.add<SurfaceHPattern>(ctx, distance);
  patterns.add<SurfaceCNOTPattern>(ctx, distance, useLattice);
}

//===----------------------------------------------------------------------===//
// Surface syndrome cycle builder
//===----------------------------------------------------------------------===//

SurfaceSyndromeCycle
mlir::ftqc::buildSurfaceSyndromeCycle(OpBuilder &builder, Location loc,
                                      Value dataReg, unsigned d) {
  auto *ctx = builder.getContext();
  // In a full implementation, this would emit individual CNOT gates for each
  // X- and Z-plaquette measurement.  Here we emit the high-level ops.
  unsigned numPlaquettes = d * d - 1;
  auto xSynType = SyndromeType::get(ctx, numPlaquettes / 2);
  auto zSynType = SyndromeType::get(ctx, numPlaquettes / 2);

  auto lqType = dyn_cast<LogicalQubitType>(dataReg.getType());
  if (!lqType)
    return {};

  auto xRound = builder.create<SurfaceRoundOp>(loc, lqType,
      SyndromeType::get(ctx, numPlaquettes), dataReg,
      builder.getI32IntegerAttr(1));

  SurfaceSyndromeCycle result;
  result.dataReg = xRound.getResult();
  return result;
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCSurfaceEncodePassImpl
    : public PassWrapper<FTQCSurfaceEncodePassImpl,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCSurfaceEncodePassImpl)

  FTQCSurfaceEncodePassImpl() = default;
  FTQCSurfaceEncodePassImpl(unsigned d, bool lattice)
      : distance_(d), useLattice_(lattice) {}

  StringRef getArgument() const override { return "ftqc-surface-encode"; }
  StringRef getDescription() const override {
    return "Encode quantum circuits using surface code";
  }

  void runOnOperation() override {
    auto mod = getOperation();
    auto *ctx = &getContext();

    mod.walk([&](func::FuncOp func) {
      RewritePatternSet patterns(ctx);
      populateSurfaceEncodePatterns(patterns, distance_, useLattice_);
      if (failed(applyPatternsAndFoldGreedily(func, std::move(patterns)))) {
        func.emitError("surface code encoding failed");
        signalPassFailure();
      }
    });
  }

  unsigned distance_ = 3;
  bool useLattice_ = true;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createSurfaceEncodePass() {
  return std::make_unique<FTQCSurfaceEncodePassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createSurfaceEncodePass(
    unsigned d, bool useLattice) {
  return std::make_unique<FTQCSurfaceEncodePassImpl>(d, useLattice);
}
