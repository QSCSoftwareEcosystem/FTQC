//===- MagicStateDistillation.cpp - Magic state distillation passes -------===//
//
// Implements the 15-to-1 Bravyi-Kitaev distillation protocol at the FTQC IR
// level and provides the gate teleportation primitive.
//
// Reference:
//   Bravyi & Kitaev, "Universal quantum computation with ideal Clifford gates
//   and noisy ancillas", PRA 71:022316 (2005).
//
//===----------------------------------------------------------------------===//

#include "FTQC/Transforms/MagicStateDistillation.h"
#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include <cmath>

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// 15-to-1 distillation circuit builder
//===----------------------------------------------------------------------===//

/// The Bravyi-Kitaev 15-to-1 protocol is based on the [[15,1,3]]
/// Reed-Muller code.  The logical T gate is transversal in this code.
///
/// Protocol summary:
///   1. Encode the target (output) qubit in the [[15,1,3]] code (|+⟩_L state).
///   2. Apply 15 transversal T gates using the 15 noisy magic states.
///   3. Decode: measure the X stabilizers of the [[15,1,3]] code.
///   4. If all stabilizer measurements return +1, output the qubit.
///      Otherwise discard (with probability scaling as 35ε^3).

DistillationResult
mlir::ftqc::build15to1Circuit(OpBuilder &builder, Location loc,
                               ValueRange inputs) {
  assert(inputs.size() == 15 && "15-to-1 requires exactly 15 inputs");

  auto *ctx    = builder.getContext();
  auto lqType  = inputs[0].getType();   // should be magic_state
  auto msType  = MagicStateType::get(ctx, MagicKind::T);

  // Step 1: Create the [[15,1,3]] encoded ancilla in |+⟩_L.
  // In actual implementation this allocates 15 physical (or logical) qubits
  // and applies the encoding circuit.
  // Here we emit an ftqc.distill_15to1 op that encapsulates the protocol.

  auto distillOp = builder.create<Distill15to1Op>(loc, msType, inputs);

  // The success flag would be a measurement result in a full implementation.
  Value successFlag = builder.create<arith::ConstantIntOp>(loc, 1, 1u);

  return DistillationResult{distillOp.getOutput(), successFlag};
}

DistillationResult
mlir::ftqc::build7to1Circuit(OpBuilder &builder, Location loc,
                              ValueRange inputs) {
  assert(inputs.size() == 7 && "7-to-1 requires exactly 7 inputs");
  auto *ctx   = builder.getContext();
  auto msType = MagicStateType::get(ctx, MagicKind::T);

  auto distillOp = builder.create<Distill7to1Op>(loc, msType, inputs);

  Value successFlag = builder.create<arith::ConstantIntOp>(loc, 1, 1u);
  return DistillationResult{distillOp.getOutput(), successFlag};
}

//===----------------------------------------------------------------------===//
// Gate teleportation
//===----------------------------------------------------------------------===//

/// T gate teleportation circuit:
///
///   data ──●── M_z ──────── → correction (Z^m)
///          │               ← data_out
///   |T⟩ ──X── ───────────
///
/// (where M_z is Z-basis measurement)
///
/// The correction is:  if m=1, apply S to data_out.
/// Final logical state: T|data⟩.

std::pair<Value, Value>
mlir::ftqc::buildTGateTeleportation(OpBuilder &builder, Location loc,
                                    Value dataQubit, Value magicState) {
  auto *ctx      = builder.getContext();
  auto lqType    = dyn_cast<LogicalQubitType>(dataQubit.getType());
  if (!lqType) {
    // Fallback: just wrap in logical_t.
    auto msType = dyn_cast<MagicStateType>(magicState.getType());
    if (!msType)
      return {dataQubit, nullptr};
    auto result = builder.create<LogicalTOp>(loc, dataQubit.getType(),
                                              dataQubit, magicState);
    Value bit = builder.create<arith::ConstantIntOp>(loc, 0, 1u);
    return {result.getResult(), bit};
  }

  auto teleportOp = builder.create<GateTeleportOp>(
      loc, lqType, builder.getI1Type(), dataQubit, magicState);
  return {teleportOp.getResult(), teleportOp.getMeasurement()};
}

std::pair<Value, Value>
mlir::ftqc::buildSGateTeleportation(OpBuilder &builder, Location loc,
                                    Value dataQubit, Value magicState) {
  // S gate via magic state: similar to T but with |S⟩ = S|+⟩.
  auto lqType = dataQubit.getType();
  auto teleportOp = builder.create<GateTeleportOp>(
      loc, lqType, builder.getI1Type(), dataQubit, magicState);
  return {teleportOp.getResult(), teleportOp.getMeasurement()};
}

//===----------------------------------------------------------------------===//
// Error rate calculations
//===----------------------------------------------------------------------===//

double mlir::ftqc::requiredInputError15to1(double targetOut, unsigned levels) {
  // Invert p_out ≈ 35 p_in^3  for each level.
  double p = targetOut;
  for (unsigned i = 0; i < levels; ++i)
    p = std::cbrt(p / 35.0);
  return p;
}

uint64_t mlir::ftqc::totalInputsRequired(unsigned levels) {
  // Each level of 15-to-1 requires 15 inputs.
  // For k levels: 15^k inputs at the bottom.
  uint64_t total = 1;
  for (unsigned i = 0; i < levels; ++i)
    total *= 15;
  return total;
}

//===----------------------------------------------------------------------===//
// Patterns
//===----------------------------------------------------------------------===//

namespace {

/// Replace ftqc.prepare_magic_t with a full 15-to-1 distillation circuit.
struct Distill15to1Pattern : public OpRewritePattern<PrepareMagicTOp> {
  Distill15to1Pattern(MLIRContext *ctx, unsigned levels, double targetError)
      : OpRewritePattern<PrepareMagicTOp>(ctx),
        levels_(levels), targetError_(targetError) {}

  LogicalResult matchAndRewrite(PrepareMagicTOp op,
                                PatternRewriter &rewriter) const override {
    Location loc   = op.getLoc();
    auto *ctx      = rewriter.getContext();
    auto msType    = MagicStateType::get(ctx, MagicKind::T);

    // Build the required number of noisy input states.
    uint64_t needed = totalInputsRequired(levels_);
    SmallVector<Value> noisyInputs;
    noisyInputs.reserve(needed);
    for (uint64_t i = 0; i < needed; ++i)
      noisyInputs.push_back(rewriter.create<PrepareMagicTOp>(loc, msType));

    // Recursively build distillation levels.
    // Level 0: raw states → 15-to-1.
    Value distilled;
    if (levels_ == 1) {
      // Take first 15.
      SmallVector<Value> first15(noisyInputs.begin(),
                                  noisyInputs.begin() + 15);
      auto dr = build15to1Circuit(rewriter, loc, first15);
      distilled = dr.outputState;
    } else {
      // Multi-level: each level takes 15 outputs of the previous level.
      SmallVector<Value> currentLevel(noisyInputs);
      for (unsigned lvl = 0; lvl < levels_; ++lvl) {
        SmallVector<Value> nextLevel;
        for (size_t i = 0; i + 14 < currentLevel.size(); i += 15) {
          SmallVector<Value> batch(currentLevel.begin() + i,
                                   currentLevel.begin() + i + 15);
          auto dr = build15to1Circuit(rewriter, loc, batch);
          nextLevel.push_back(dr.outputState);
        }
        currentLevel = nextLevel;
      }
      distilled = currentLevel.empty() ? Value{} : currentLevel[0];
    }

    if (!distilled)
      return failure();

    rewriter.replaceOp(op, distilled);
    return success();
  }

  unsigned levels_;
  double   targetError_;
};

/// Replace ftqc.prepare_magic_t with 7-to-1 distillation.
struct Distill7to1Pattern : public OpRewritePattern<PrepareMagicTOp> {
  Distill7to1Pattern(MLIRContext *ctx, unsigned levels)
      : OpRewritePattern<PrepareMagicTOp>(ctx, /*benefit=*/1),
        levels_(levels) {}

  LogicalResult matchAndRewrite(PrepareMagicTOp op,
                                PatternRewriter &rewriter) const override {
    Location loc = op.getLoc();
    auto *ctx    = rewriter.getContext();
    auto msType  = MagicStateType::get(ctx, MagicKind::T);

    SmallVector<Value> inputs;
    for (unsigned i = 0; i < 7; ++i)
      inputs.push_back(rewriter.create<PrepareMagicTOp>(loc, msType));

    auto dr = build7to1Circuit(rewriter, loc, inputs);
    rewriter.replaceOp(op, dr.outputState);
    return success();
  }

  unsigned levels_;
};

} // namespace

void mlir::ftqc::populateMagicDistillPatterns(RewritePatternSet &patterns,
                                              const std::string &protocol,
                                              unsigned levels,
                                              double targetError) {
  auto *ctx = patterns.getContext();
  if (protocol == "15to1" || protocol == "auto")
    patterns.add<Distill15to1Pattern>(ctx, levels, targetError);
  else if (protocol == "7to1")
    patterns.add<Distill7to1Pattern>(ctx, levels);
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCMagicDistillPassImpl
    : public PassWrapper<FTQCMagicDistillPassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCMagicDistillPassImpl)

  FTQCMagicDistillPassImpl() = default;
  FTQCMagicDistillPassImpl(std::string proto, unsigned lvls, double err)
      : protocol_(std::move(proto)), levels_(lvls), targetError_(err) {}

  StringRef getArgument() const override { return "ftqc-magic-distill"; }

  void runOnOperation() override {
    auto func = getOperation();
    auto *ctx = &getContext();

    RewritePatternSet patterns(ctx);
    populateMagicDistillPatterns(patterns, protocol_, levels_, targetError_);

      if (failed(applyPatternsAndFoldGreedily(func, std::move(patterns)))) {
      func.emitError("magic state distillation pass failed");
      signalPassFailure();
    }
  }

  std::string protocol_    = "15to1";
  unsigned    levels_      = 1;
  double      targetError_ = 1e-10;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createMagicDistillPass() {
  return std::make_unique<FTQCMagicDistillPassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createMagicDistillPass(
    const std::string &proto, unsigned lvls, double err) {
  return std::make_unique<FTQCMagicDistillPassImpl>(proto, lvls, err);
}
