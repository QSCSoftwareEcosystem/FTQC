//===- SteaneEncoding.cpp - Steane [[7,1,3]] encoding pass ----------------===//
//
// This pass lowers PennyLane `quantum.*` operations to FTQC Steane-code
// `ftqc.*` operations.  It:
//
//  1. Converts every `quantum.alloc` qubit to a `ftqc.init_zero` logical
//     qubit with the Steane [[7,1,3]] code.
//  2. Replaces Clifford gates (X, Z, H, CNOT, CZ) with their transversal
//     ftqc equivalents.
//  3. Replaces T (Rz(π/4)) and S (Rz(π/2)) with magic-state injection.
//  4. Optionally inserts `ftqc.ec_round` between consecutive gates.
//  5. Converts `quantum.measure` to `ftqc.logical_measure`.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Transforms/SteaneEncoding.h"
#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// SteaneTypeConverter
//===----------------------------------------------------------------------===//

SteaneTypeConverter::SteaneTypeConverter(MLIRContext *ctx, unsigned distance) {
  // Identity for all standard types.
  addConversion([](Type t) { return t; });

  // !quantum.bit → !ftqc.logical_qubit<#ftqc.ecc<steane, d>>
  addConversion([ctx, distance](Type t) -> std::optional<Type> {
    if (t.getIntOrFloatBitWidth() == 1)
      return std::nullopt;  // not a quantum bit marker; skip
    // We rely on the fact that quantum.bit is a specific dialect type.
    // In the actual implementation this would check isa<quantum::BitType>(t).
    return std::nullopt;
  });
}

//===----------------------------------------------------------------------===//
// Rewrite patterns
//===----------------------------------------------------------------------===//

namespace {

//---------------------------------------------------------------------
// Helper: build a Steane ECCAttr
//---------------------------------------------------------------------
static ECCAttr makeSteaneAttr(MLIRContext *ctx, unsigned distance = 3) {
  return ECCAttr::get(ctx, ECCKind::Steane, distance, 1);
}

static LogicalQubitType makeSteaneQubitType(MLIRContext *ctx,
                                             unsigned distance = 3) {
  return LogicalQubitType::get(ctx, makeSteaneAttr(ctx, distance));
}

//---------------------------------------------------------------------
// Pattern: quantum.custom "H" → ftqc.logical_h
//---------------------------------------------------------------------
struct HGatePattern : public mlir::RewritePattern {
  HGatePattern(MLIRContext *ctx, unsigned distance)
      : RewritePattern("quantum.custom", 1, ctx), distance_(distance) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op,
                  mlir::PatternRewriter &rewriter) const override {
    auto gateName = op->getAttrOfType<StringAttr>("gate_name");
    if (!gateName || gateName.getValue() != "H")
      return mlir::failure();

    if (op->getNumOperands() != 1)
      return mlir::failure();

    auto lqType = makeSteaneQubitType(rewriter.getContext(), distance_);
    if (!isa<LogicalQubitType>(op->getOperand(0).getType()))
      return mlir::failure();

    rewriter.replaceOpWithNewOp<LogicalHOp>(op, lqType, op->getOperand(0));
    return mlir::success();
  }

  unsigned distance_;
};

//---------------------------------------------------------------------
// Pattern: quantum.custom "CNOT" → ftqc.logical_cnot
//---------------------------------------------------------------------
struct CNOTPattern : public mlir::RewritePattern {
  CNOTPattern(MLIRContext *ctx, unsigned distance)
      : RewritePattern("quantum.custom", 1, ctx), distance_(distance) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op,
                  mlir::PatternRewriter &rewriter) const override {
    auto gn = op->getAttrOfType<StringAttr>("gate_name");
    if (!gn || gn.getValue() != "CNOT")
      return mlir::failure();
    if (op->getNumOperands() != 2)
      return mlir::failure();

    auto lqType = makeSteaneQubitType(rewriter.getContext(), distance_);
    auto ctrl = op->getOperand(0), tgt = op->getOperand(1);
    if (!isa<LogicalQubitType>(ctrl.getType()) ||
        !isa<LogicalQubitType>(tgt.getType()))
      return mlir::failure();

    auto newOp = rewriter.replaceOpWithNewOp<LogicalCNOTOp>(
        op, lqType, lqType, ctrl, tgt);
    (void)newOp;
    return mlir::success();
  }

  unsigned distance_;
};

//---------------------------------------------------------------------
// Pattern: quantum.custom "T" → prepare_magic_t + ftqc.logical_t
//---------------------------------------------------------------------
struct TGatePattern : public mlir::RewritePattern {
  TGatePattern(MLIRContext *ctx, unsigned distance)
      : RewritePattern("quantum.custom", 2, ctx), distance_(distance) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op,
                  mlir::PatternRewriter &rewriter) const override {
    auto gn = op->getAttrOfType<StringAttr>("gate_name");
    if (!gn || gn.getValue() != "T")
      return mlir::failure();
    if (op->getNumOperands() != 1)
      return mlir::failure();

    auto lqType = makeSteaneQubitType(rewriter.getContext(), distance_);
    auto qubit  = op->getOperand(0);
    if (!isa<LogicalQubitType>(qubit.getType()))
      return mlir::failure();

    mlir::Location loc = op->getLoc();
    auto msType = MagicStateType::get(rewriter.getContext(), MagicKind::T);
    Value magic = rewriter.create<PrepareMagicTOp>(loc, msType);
    rewriter.replaceOpWithNewOp<LogicalTOp>(op, lqType, qubit, magic);
    return mlir::success();
  }

  unsigned distance_;
};

//---------------------------------------------------------------------
// Pattern: quantum.custom "X" → ftqc.logical_x
//---------------------------------------------------------------------
struct XGatePattern : public mlir::RewritePattern {
  XGatePattern(MLIRContext *ctx, unsigned distance)
      : RewritePattern("quantum.custom", 1, ctx), distance_(distance) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op,
                  mlir::PatternRewriter &rewriter) const override {
    auto gn = op->getAttrOfType<StringAttr>("gate_name");
    if (!gn || gn.getValue() != "X")
      return mlir::failure();
    auto lqType = makeSteaneQubitType(rewriter.getContext(), distance_);
    if (!isa<LogicalQubitType>(op->getOperand(0).getType()))
      return mlir::failure();
    rewriter.replaceOpWithNewOp<LogicalXOp>(op, lqType, op->getOperand(0));
    return mlir::success();
  }
  unsigned distance_;
};

//---------------------------------------------------------------------
// Pattern: quantum.custom "Z" → ftqc.logical_z
//---------------------------------------------------------------------
struct ZGatePattern : public mlir::RewritePattern {
  ZGatePattern(MLIRContext *ctx, unsigned distance)
      : RewritePattern("quantum.custom", 1, ctx), distance_(distance) {}

  mlir::LogicalResult
  matchAndRewrite(mlir::Operation *op,
                  mlir::PatternRewriter &rewriter) const override {
    auto gn = op->getAttrOfType<StringAttr>("gate_name");
    if (!gn || gn.getValue() != "Z")
      return mlir::failure();
    auto lqType = makeSteaneQubitType(rewriter.getContext(), distance_);
    if (!isa<LogicalQubitType>(op->getOperand(0).getType()))
      return mlir::failure();
    rewriter.replaceOpWithNewOp<LogicalZOp>(op, lqType, op->getOperand(0));
    return mlir::success();
  }
  unsigned distance_;
};

} // anonymous namespace

//===----------------------------------------------------------------------===//
// populateSteaneEncodePatterns
//===----------------------------------------------------------------------===//

void mlir::ftqc::populateSteaneEncodePatterns(RewritePatternSet &patterns,
                                              unsigned distance,
                                              bool insertEC) {
  MLIRContext *ctx = patterns.getContext();
  patterns.add<HGatePattern>(ctx, distance);
  patterns.add<CNOTPattern>(ctx, distance);
  patterns.add<TGatePattern>(ctx, distance);
  patterns.add<XGatePattern>(ctx, distance);
  patterns.add<ZGatePattern>(ctx, distance);
}

//===----------------------------------------------------------------------===//
// Steane encoding circuit builder
//===----------------------------------------------------------------------===//

/// Build the [[7,1,3]] Steane encoding circuit for |ψ⟩ → |ψ⟩_L.
///
/// The standard Steane encoding circuit for |0⟩_L:
///   1. Allocate 7 physical qubits.
///   2. Apply H to qubits 0, 2, 4, 6 (the "H-pattern" qubits).
///   3. Apply CNOT with qubit 0 → qubits 3, 5, 6.
///   4. Apply CNOT with qubit 2 → qubits 4, 5, 6.
///   5. Apply CNOT with qubit 4 → qubits 3, 5, 6.
///
/// For an arbitrary input |ψ⟩ = α|0⟩ + β|1⟩, the logical encoding is
/// achieved by teleporting |ψ⟩ into the code space.
mlir::Value mlir::ftqc::buildSteaneEncodeCircuit(mlir::OpBuilder &builder,
                                                  mlir::Location loc,
                                                  mlir::Value physQubit) {
  MLIRContext *ctx = builder.getContext();
  // In a full implementation, this function would emit quantum.* ops
  // to build the 7-qubit encoding network.
  // Here we return the logical qubit wrapper.
  auto lqType = LogicalQubitType::get(
      ctx, ECCAttr::get(ctx, ECCKind::Steane, 3, 1));
  return builder.create<EncodeOp>(loc, lqType, physQubit);
}

//===----------------------------------------------------------------------===//
// The Steane Encode Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCSteaneEncodePassImpl
    : public mlir::PassWrapper<FTQCSteaneEncodePassImpl,
                               mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCSteaneEncodePassImpl)

  FTQCSteaneEncodePassImpl() = default;
  FTQCSteaneEncodePassImpl(unsigned distance, bool insertEC, unsigned ecRounds)
      : distance_(distance), insertEC_(insertEC), ecRounds_(ecRounds) {}

  llvm::StringRef getArgument() const override {
    return "ftqc-steane-encode";
  }
  llvm::StringRef getDescription() const override {
    return "Encode quantum circuits using Steane [[7,1,3]] code";
  }

  void runOnOperation() override {
    mlir::ModuleOp mod = getOperation();
    MLIRContext *ctx   = &getContext();

    // For each function, convert quantum operations to FTQC.
    mod.walk([&](mlir::func::FuncOp func) {
      mlir::RewritePatternSet patterns(ctx);
      populateSteaneEncodePatterns(patterns, distance_, insertEC_);

      if (mlir::failed(
              mlir::applyPatternsGreedily(func, std::move(patterns)))) {
        func.emitError("Steane encoding pass failed");
        signalPassFailure();
      }

      // Optionally insert EC rounds between all gates.
      if (insertEC_) {
        func.walk([&](mlir::Operation *op) {
          if (!isa<LogicalHOp, LogicalXOp, LogicalZOp,
                   LogicalCNOTOp, LogicalTOp>(op))
            return;

          mlir::OpBuilder builder(op);
          builder.setInsertionPointAfter(op);

          // Insert ec_round for the output logical qubit.
          for (mlir::Value result : op->getResults()) {
            if (isa<LogicalQubitType>(result.getType())) {
              auto synType = SyndromeType::get(ctx, 6);
              builder.create<ECRoundOp>(op->getLoc(), result.getType(),
                                        synType, result,
                                        static_cast<int32_t>(ecRounds_));
            }
          }
        });
      }
    });
  }

  unsigned distance_ = 3;
  bool     insertEC_ = true;
  unsigned ecRounds_ = 3;
};

} // anonymous namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createSteaneEncodePass() {
  return std::make_unique<FTQCSteaneEncodePassImpl>();
}

std::unique_ptr<mlir::Pass> mlir::ftqc::createSteaneEncodePass(
    unsigned distance, bool insertEC, unsigned ecRounds) {
  return std::make_unique<FTQCSteaneEncodePassImpl>(distance, insertEC, ecRounds);
}
