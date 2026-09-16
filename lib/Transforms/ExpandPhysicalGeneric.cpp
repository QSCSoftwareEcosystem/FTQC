//===- ExpandPhysicalGeneric.cpp - Generic logical -> physical (qphys) expansion
//
// Unfolds each `!ftqc.logical_qubit` into its physical block of
// `!ftqc.qphys_qubit` values and rewrites logical gates / syndrome ops into
// `ftqc.qphys.*` physical gate + measure circuits.  Data and ancilla qubits
// become explicit, labeled SSA values (role = "data" / "x_anc" / "z_anc").
//
// Scope: Steane, surface, concatenated, repetition, and hypergraph product
// codes.  Currently syndrome extraction, logical Pauli gates, qubit
// initialization and measurement are supported.  Other logical ops cause the
// pass to error out rather than emit physically wrong or dangling IR.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"
#include "FTQC/Utils/StabilizerUtils.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Func/Transforms/FuncConversions.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

#define GEN_PASS_DEF_FTQCEXPANDPHYSICALGENERICPASS
#include "FTQC/Transforms/Passes.h.inc"

#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <utility>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

void applyPauliString(const PauliString &paulis, SmallVector<Value> &qubits,
                      OpBuilder &builder, Location loc) {
  for (size_t i = 0; i < qubits.size(); ++i) {
    switch (paulis[i]) {
    case Pauli::I:
      break;
    case Pauli::X:
      qubits[i] = QPhysXOp::create(builder, loc, qubits[i]);
      break;
    case Pauli::Z:
      qubits[i] = QPhysZOp::create(builder, loc, qubits[i]);
      break;
    case Pauli::Y:
      qubits[i] = QPhysXOp::create(builder, loc, qubits[i]);
      qubits[i] = QPhysZOp::create(builder, loc, qubits[i]);
      break;
    }
  }
}

void measureXSyndrome(const StabilizerCode &code, SmallVector<Value> &qubits,
                      SmallVector<Value> &syndrome, uint64_t &nextId,
                      OpBuilder &builder, Location loc) {
  for (auto &row : code.getHx()) {
    Value ancilla =
        QPhysAllocOp::create(builder, loc, builder.getType<QPhysQubitType>(),
                             nextId++, builder.getStringAttr("x_anc"));
    ancilla = QPhysHOp::create(builder, loc, ancilla);
    for (size_t i = 0; i < qubits.size(); ++i) {
      if (row[i]) {
        auto cnotOp = QPhysCXOp::create(builder, loc, ancilla, qubits[i]);
        ancilla = cnotOp.getO0();
        qubits[i] = cnotOp.getO1();
      }
    }
    ancilla = QPhysHOp::create(builder, loc, ancilla);
    syndrome.push_back(QPhysMeasureOp::create(
        builder, loc, ancilla, builder.getStringAttr("sx"), nullptr, nullptr,
        nullptr, nullptr));
  }
}

void measureZSyndrome(const StabilizerCode &code, SmallVector<Value> &qubits,
                      SmallVector<Value> &syndrome, uint64_t &nextId,
                      OpBuilder &builder, Location loc) {
  for (auto &row : code.getHz()) {
    Value ancilla =
        QPhysAllocOp::create(builder, loc, builder.getType<QPhysQubitType>(),
                             nextId++, builder.getStringAttr("z_anc"));
    for (size_t i = 0; i < qubits.size(); ++i) {
      if (row[i]) {
        auto cnotOp = QPhysCXOp::create(builder, loc, qubits[i], ancilla);
        qubits[i] = cnotOp.getO0();
        ancilla = cnotOp.getO1();
      }
    }
    syndrome.push_back(QPhysMeasureOp::create(
        builder, loc, ancilla, builder.getStringAttr("sz"), nullptr, nullptr,
        nullptr, nullptr));
  }
}

//===----------------------------------------------------------------------===//
// Conversion patterns
//===----------------------------------------------------------------------===//

struct InitZeroPattern : public OpConversionPattern<InitZeroOp> {
  uint64_t &nextId;

  InitZeroPattern(const TypeConverter &typeConverter, MLIRContext *ctx,
                  uint64_t &nextId)
      : Base(typeConverter, ctx), nextId(nextId) {}

  LogicalResult
  matchAndRewrite(InitZeroOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    unsigned n = op.getType().getPhysicalQubits();
    Location loc = op.getLoc();
    SmallVector<Value> qubits, syndrome;
    qubits.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
      Value qubit = QPhysAllocOp::create(
          rewriter, loc, rewriter.getType<QPhysQubitType>(), nextId++,
          rewriter.getStringAttr("data"));
      qubits.push_back(qubit);
    }
    // TODO: Use a fault-tolerant encoding scheme.
    measureXSyndrome(op.getType().getCode().getStabilizerCode(), qubits,
                     syndrome, nextId, rewriter, loc);
    rewriter.replaceOpWithMultiple(op, {std::move(qubits)});
    return success();
  }
};

struct InitPlusPattern : public OpConversionPattern<InitPlusOp> {
  uint64_t &nextId;

  InitPlusPattern(const TypeConverter &typeConverter, MLIRContext *ctx,
                  uint64_t &nextId)
      : Base(typeConverter, ctx), nextId(nextId) {}

  LogicalResult
  matchAndRewrite(InitPlusOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    unsigned n = op.getType().getPhysicalQubits();
    Location loc = op.getLoc();
    SmallVector<Value> qubits, syndrome;
    qubits.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
      Value qubit = QPhysAllocOp::create(
          rewriter, loc, rewriter.getType<QPhysQubitType>(), nextId++,
          rewriter.getStringAttr("data"));
      qubit = QPhysHOp::create(rewriter, loc, qubit);
      qubits.push_back(qubit);
    }
    // TODO: Use a fault-tolerant encoding scheme.
    measureZSyndrome(op.getType().getCode().getStabilizerCode(), qubits,
                     syndrome, nextId, rewriter, loc);
    rewriter.replaceOpWithMultiple(op, {std::move(qubits)});
    return success();
  }
};

struct ExtractSyndromePattern : public OpConversionPattern<ExtractSyndromeOp> {
  uint64_t &nextId;

  ExtractSyndromePattern(const TypeConverter &typeConverter, MLIRContext *ctx,
                         uint64_t &nextId)
      : Base(typeConverter, ctx), nextId(nextId) {}

  LogicalResult
  matchAndRewrite(ExtractSyndromeOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getLogicalQubit().getType().getCode().getStabilizerCode();
    SmallVector<Value> qubits = adaptor.getLogicalQubit(), syndrome;
    measureXSyndrome(code, qubits, syndrome, nextId, rewriter, op.getLoc());
    measureZSyndrome(code, qubits, syndrome, nextId, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(qubits), std::move(syndrome)});
    return success();
  }
};

struct ECRoundPattern : public OpConversionPattern<ECRoundOp> {
  uint64_t &nextId;

  ECRoundPattern(const TypeConverter &typeConverter, MLIRContext *ctx,
                 uint64_t &nextId)
      : Base(typeConverter, ctx), nextId(nextId) {}

  LogicalResult
  matchAndRewrite(ECRoundOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getLogicalQubit().getType().getCode().getStabilizerCode();
    SmallVector<Value> qubits = adaptor.getLogicalQubit(), syndrome;
    measureXSyndrome(code, qubits, syndrome, nextId, rewriter, op.getLoc());
    measureZSyndrome(code, qubits, syndrome, nextId, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(qubits), std::move(syndrome)});
    return success();
  }
};

struct LogicalXPattern : public OpConversionPattern<LogicalXOp> {
  using Base::Base;

  LogicalResult
  matchAndRewrite(LogicalXOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getQubit().getType().getCode().getStabilizerCode();
    SmallVector<Value> qubits = adaptor.getQubit();
    applyPauliString(code.getLogicalX(), qubits, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(qubits)});
    return success();
  }
};

struct LogicalZPattern : public OpConversionPattern<LogicalZOp> {
  using Base::Base;

  LogicalResult
  matchAndRewrite(LogicalZOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getQubit().getType().getCode().getStabilizerCode();
    SmallVector<Value> qubits = adaptor.getQubit();
    applyPauliString(code.getLogicalZ(), qubits, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(qubits)});
    return success();
  }
};

struct LogicalYPattern : public OpConversionPattern<LogicalYOp> {
  using Base::Base;

  LogicalResult
  matchAndRewrite(LogicalYOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getQubit().getType().getCode().getStabilizerCode();
    SmallVector<Value> qubits = adaptor.getQubit();
    applyPauliString(code.getLogicalX(), qubits, rewriter, op.getLoc());
    applyPauliString(code.getLogicalZ(), qubits, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(qubits)});
    return success();
  }
};

struct LogicalMeasurePattern : public OpConversionPattern<LogicalMeasureOp> {
  using Base::Base;

  LogicalResult
  matchAndRewrite(LogicalMeasureOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getLogicalQubit().getType().getCode().getStabilizerCode();
    auto &paulis = op.getBasis() == 1 ? code.getLogicalX() : code.getLogicalZ();
    Location loc = op.getLoc();
    ValueRange qubits = adaptor.getLogicalQubit();
    Value acc;
    for (size_t i = 0; i < qubits.size(); ++i) {
      Value qubit = qubits[i];
      switch (paulis[i]) {
      case Pauli::I:
        break;
      case Pauli::Y:
        qubit = QPhysSOp::create(rewriter, loc, qubit);
        qubit = QPhysSOp::create(rewriter, loc, qubit);
        qubit = QPhysSOp::create(rewriter, loc, qubit);
        [[fallthrough]];
      case Pauli::X:
        qubit = QPhysHOp::create(rewriter, loc, qubit);
        [[fallthrough]];
      case Pauli::Z:
        Value bit = QPhysMeasureOp::create(rewriter, loc, qubit,
                                           rewriter.getStringAttr("m"), nullptr,
                                           nullptr, nullptr, nullptr);
        acc = acc ? arith::XOrIOp::create(rewriter, loc, acc, bit) : bit;
        // In principle we should undo the basis change for X and Y measurements
        // here, but that can be omitted since LogicalMeasureOp releases the
        // logical qubit.
        break;
      }
    }
    rewriter.replaceOp(op, acc);
    return success();
  }
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct FTQCExpandPhysicalGenericPassImpl
    : public ::impl::FTQCExpandPhysicalGenericPassBase<
          FTQCExpandPhysicalGenericPassImpl> {
  using FTQCExpandPhysicalGenericPassBase::FTQCExpandPhysicalGenericPassBase;

  void runOnOperation() override {
    auto mod = getOperation();
    auto *ctx = &getContext();

    TypeConverter typeConverter;
    typeConverter.addConversion([](Type type) { return type; });
    typeConverter.addConversion(
        [ctx](LogicalQubitType lqt, SmallVectorImpl<Type> &types) {
          types.resize(lqt.getPhysicalQubits(), QPhysQubitType::get(ctx));
          return success();
        });
    typeConverter.addConversion(
        [ctx](SyndromeType st, SmallVectorImpl<Type> &types) {
          types.resize(st.getWidth(), IntegerType::get(ctx, 1));
          return success();
        });

    ConversionTarget target(*ctx);
    target.addDynamicallyLegalDialect<FTQCDialect>([](Operation *op) {
      return op->getName().getStringRef().starts_with("ftqc.qphys.");
    });
    target.addLegalOp<arith::XOrIOp>();
    target.addDynamicallyLegalOp<func::FuncOp>([&](func::FuncOp op) {
      return typeConverter.isSignatureLegal(op.getFunctionType());
    });
    target.addDynamicallyLegalOp<func::CallOp, func::ReturnOp>(
        [&](Operation *op) { return typeConverter.isLegal(op); });

    RewritePatternSet patterns(ctx);
    uint64_t nextId = 0;
    patterns.add<InitZeroPattern>(typeConverter, ctx, nextId);
    patterns.add<InitPlusPattern>(typeConverter, ctx, nextId);
    patterns.add<ExtractSyndromePattern>(typeConverter, ctx, nextId);
    patterns.add<ECRoundPattern>(typeConverter, ctx, nextId);
    patterns.add<LogicalXPattern>(typeConverter, ctx);
    patterns.add<LogicalZPattern>(typeConverter, ctx);
    patterns.add<LogicalYPattern>(typeConverter, ctx);
    patterns.add<LogicalMeasurePattern>(typeConverter, ctx);
    populateFunctionOpInterfaceTypeConversionPattern<func::FuncOp>(
        patterns, typeConverter);
    populateCallOpTypeConversionPattern(patterns, typeConverter);
    populateReturnOpTypeConversionPattern(patterns, typeConverter);

    if (failed(applyPartialConversion(mod, target, std::move(patterns)))) {
      mod.emitError("ftqc-expand-physical-generic: pass failed");
      signalPassFailure();
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createExpandPhysicalGenericPass() {
  return std::make_unique<FTQCExpandPhysicalGenericPassImpl>();
}
