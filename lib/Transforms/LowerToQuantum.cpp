//===- LowerToQuantum.cpp - FTQC → quantum dialect lowering ---------------===//
//
// Final lowering pass: converts all `ftqc.*` operations to `quantum.*`
// physical-qubit operations in the PennyLane/Catalyst quantum dialect.
//
// Each logical qubit is unfolded into n physical `!quantum.bit` values.
// Logical gates are expanded into their transversal physical-gate sequences.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// Type conversion: !ftqc.logical_qubit → n × !quantum.bit (modeled as i1)
//===----------------------------------------------------------------------===//
// NOTE: In a real integration with PennyLane/Catalyst, these would be
// converted to `!quantum.bit` and `!quantum.reg` types.
// Here we use `i1` as a stand-in since the quantum dialect is not linked.

namespace {

class FTQCToQuantumTypeConverter : public TypeConverter {
public:
  FTQCToQuantumTypeConverter() {
    // Identity conversion for all non-FTQC types.
    addConversion([](Type t) -> std::optional<Type> {
      if (isa<LogicalQubitType, SyndromeType,
               MagicStateType, PauliFrameType>(t))
        return std::nullopt;  // handled below
      return t;
    });

    // !ftqc.logical_qubit<ecc> → tuple of n i1 (physical qubits).
    addConversion([](LogicalQubitType lqt) -> Type {
      // In production: return SmallVector of !quantum.bit.
      // For now: treat as i1.
      return IntegerType::get(lqt.getContext(), 1);
    });

    // !ftqc.syndrome<n> → vector<n × i1> = integer(n).
    addConversion([](SyndromeType st) -> Type {
      return IntegerType::get(st.getContext(), st.getWidth());
    });

    // !ftqc.magic_state → i1 (placeholder).
    addConversion([](MagicStateType mst) -> Type {
      return IntegerType::get(mst.getContext(), 1);
    });

    // !ftqc.pauli_frame<n> → integer(2n) (X and Z bits).
    addConversion([](PauliFrameType pft) -> Type {
      return IntegerType::get(pft.getContext(), 2 * pft.getWidth());
    });
  }
};

//===----------------------------------------------------------------------===//
// Conversion patterns
//===----------------------------------------------------------------------===//

/// ftqc.init_zero → quantum.alloc + encoding circuit (stub: constant 0).
struct InitZeroLowering : public OpConversionPattern<InitZeroOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(InitZeroOp op, OpAdaptor /*adaptor*/,
                                ConversionPatternRewriter &rewriter) const override {
    // Allocate n physical qubits (stub: produce i1 = 0).
    rewriter.replaceOpWithNewOp<arith::ConstantIntOp>(op, 0, 1);
    return success();
  }
};

/// ftqc.init_plus → quantum.alloc + H (stub: constant 1 for |+⟩ sign).
struct InitPlusLowering : public OpConversionPattern<InitPlusOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(InitPlusOp op, OpAdaptor,
                                ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<arith::ConstantIntOp>(op, 1, 1);
    return success();
  }
};

/// ftqc.logical_x → transversal X on all physical qubits.
/// In the stub implementation: XOR with 1.
struct LogicalXLowering : public OpConversionPattern<LogicalXOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(LogicalXOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    Value one = rewriter.create<arith::ConstantIntOp>(op.getLoc(), 1, 1);
    rewriter.replaceOpWithNewOp<arith::XOrIOp>(op, adaptor.getQubit(), one);
    return success();
  }
};

/// ftqc.logical_z → no physical effect on |0⟩/|1⟩ in computational basis
/// (just tracks classically). Stub: identity.
struct LogicalZLowering : public OpConversionPattern<LogicalZOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(LogicalZOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, adaptor.getQubit());
    return success();
  }
};

/// ftqc.logical_h → transversal H. Stub: XOR (classical approximation).
struct LogicalHLowering : public OpConversionPattern<LogicalHOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(LogicalHOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    // True implementation: apply H to each of the n physical qubits.
    rewriter.replaceOp(op, adaptor.getQubit());
    return success();
  }
};

/// ftqc.logical_measure → quantum.measure (stub: return input bit).
struct LogicalMeasureLowering : public OpConversionPattern<LogicalMeasureOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(LogicalMeasureOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, adaptor.getLogicalQubit());
    return success();
  }
};

/// ftqc.extract_syndrome → syndrome extraction circuit (stub: zero syndrome).
struct ExtractSyndromeLowering : public OpConversionPattern<ExtractSyndromeOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(ExtractSyndromeOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    auto *ctx = rewriter.getContext();
    auto synType = dyn_cast<SyndromeType>(op.getSyndrome().getType());
    unsigned w = synType ? synType.getWidth() : 6;
    Value synOut = rewriter.create<arith::ConstantIntOp>(
        op.getLoc(), 0, w);
    SmallVector<Value> results = {adaptor.getLogicalQubit(), synOut};
    rewriter.replaceOp(op, results);
    return success();
  }
};

/// ftqc.correct → apply correction (stub: pass through).
struct CorrectLowering : public OpConversionPattern<CorrectOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(CorrectOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, adaptor.getLogicalQubit());
    return success();
  }
};

/// ftqc.prepare_magic_t → prepare |T⟩ state (stub: i1 = 1).
struct PrepareMagicTLowering : public OpConversionPattern<PrepareMagicTOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(PrepareMagicTOp op, OpAdaptor,
                                ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<arith::ConstantIntOp>(op, 1, 1);
    return success();
  }
};

/// ftqc.logical_t → T gate via magic state (stub: AND).
struct LogicalTLowering : public OpConversionPattern<LogicalTOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(LogicalTOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<arith::AndIOp>(
        op, adaptor.getQubit(), adaptor.getMagicState());
    return success();
  }
};

/// ftqc.logical_cnot → transversal CNOT (stub: XOR).
struct LogicalCNOTLowering : public OpConversionPattern<LogicalCNOTOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(LogicalCNOTOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    Value ctrl = adaptor.getControl(), tgt = adaptor.getTarget();
    Value newTgt = rewriter.create<arith::XOrIOp>(op.getLoc(), tgt, ctrl);
    rewriter.replaceOp(op, {ctrl, newTgt});
    return success();
  }
};

/// ftqc.ec_round → stub: no-op (syndrome = 0, qubit pass-through).
struct ECRoundLowering : public OpConversionPattern<ECRoundOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(ECRoundOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    auto synType = dyn_cast<SyndromeType>(op.getSyndrome().getType());
    unsigned w = synType ? synType.getWidth() : 6;
    Value synZero = rewriter.create<arith::ConstantIntOp>(op.getLoc(), 0, w);
    rewriter.replaceOp(op, {adaptor.getLogicalQubit(), synZero});
    return success();
  }
};

/// ftqc.dealloc → no-op.
struct DeallocLowering : public OpConversionPattern<DeallocOp> {
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(DeallocOp op, OpAdaptor,
                                ConversionPatternRewriter &rewriter) const override {
    rewriter.eraseOp(op);
    return success();
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCLowerToQuantumPassImpl
    : public PassWrapper<FTQCLowerToQuantumPassImpl,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCLowerToQuantumPassImpl)
  StringRef getArgument() const override { return "ftqc-lower-to-quantum"; }

  void runOnOperation() override {
    auto mod = getOperation();
    auto *ctx = &getContext();

    FTQCToQuantumTypeConverter typeConverter;
    ConversionTarget target(*ctx);

    // Mark all standard dialects as legal.
    target.addLegalDialect<arith::ArithDialect,
                            func::FuncDialect,
                            scf::SCFDialect>();

    // Mark all FTQC ops as illegal (must be converted).
    target.addIllegalDialect<FTQCDialect>();

    RewritePatternSet patterns(ctx);
    patterns.add<InitZeroLowering,
                 InitPlusLowering,
                 LogicalXLowering,
                 LogicalZLowering,
                 LogicalHLowering,
                 LogicalMeasureLowering,
                 ExtractSyndromeLowering,
                 CorrectLowering,
                 PrepareMagicTLowering,
                 LogicalTLowering,
                 LogicalCNOTLowering,
                 ECRoundLowering,
                 DeallocLowering>(typeConverter, ctx);

    if (failed(applyFullConversion(mod, target, std::move(patterns)))) {
      mod.emitError("FTQC → quantum lowering failed");
      signalPassFailure();
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createLowerToQuantumPass() {
  return std::make_unique<FTQCLowerToQuantumPassImpl>();
}
