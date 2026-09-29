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
// Implementation note: Every `!ftqc.logical_qubit`, using an [[n,k,d]] code
// with r_x X-stabilizers and r_z Z-stabilizers, is converted to n
// `!ftqc.qphys_qubit`s, r_x `!i1`s keeping track of the latest X-syndrome
// measurements, and r_z `!i1`s keeping track of the latest Z-syndrome
// measurements.  In this file, any variable named `values` is a sequence of
// these n + r_x + r_z values, in that order.  If a syndrome bit has not yet
// been measured, the corresponding value is set to `arith.constant 0`.
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

void applyPauliString(const PauliString &paulis, SmallVector<Value> &values,
                      OpBuilder &builder, Location loc) {
  for (size_t i = 0; i < paulis.size(); ++i) {
    switch (paulis[i]) {
    case Pauli::I:
      break;
    case Pauli::X:
      values[i] = QPhysXOp::create(builder, loc, values[i]);
      break;
    case Pauli::Z:
      values[i] = QPhysZOp::create(builder, loc, values[i]);
      break;
    case Pauli::Y:
      values[i] = QPhysXOp::create(builder, loc, values[i]);
      values[i] = QPhysZOp::create(builder, loc, values[i]);
      break;
    }
  }
}

void measureXSyndrome(const StabilizerCode &code, SmallVector<Value> &values,
                      bool addDetectors, SmallVector<Value> &syndrome,
                      uint64_t &nextId, OpBuilder &builder, Location loc) {
  for (size_t i = 0; i < code.getHx().size(); ++i) {
    auto &row = code.getHx()[i];
    Value ancilla =
        QPhysAllocOp::create(builder, loc, builder.getType<QPhysQubitType>(),
                             nextId++, builder.getStringAttr("x_anc"));
    ancilla = QPhysHOp::create(builder, loc, ancilla);
    for (size_t j = 0; j < code.getN(); ++j) {
      if (row[j]) {
        auto cnotOp = QPhysCXOp::create(builder, loc, ancilla, values[j]);
        ancilla = cnotOp.getO0();
        values[j] = cnotOp.getO1();
      }
    }
    ancilla = QPhysHOp::create(builder, loc, ancilla);
    Value measure = QPhysMeasureOp::create(builder, loc, ancilla,
                                           builder.getStringAttr("sx"), nullptr,
                                           nullptr, nullptr, nullptr);
    syndrome.push_back(measure);
    size_t index = code.getN() + i;
    if (addDetectors) {
      if (values[index].getDefiningOp<arith::ConstantIntOp>())
        QPhysDetectorOp::create(builder, loc, {measure});
      else
        QPhysDetectorOp::create(builder, loc, {values[index], measure});
    }
    values[index] = measure;
  }
}

void measureZSyndrome(const StabilizerCode &code, SmallVector<Value> &values,
                      bool addDetectors, SmallVector<Value> &syndrome,
                      uint64_t &nextId, OpBuilder &builder, Location loc) {
  for (size_t i = 0; i < code.getHz().size(); ++i) {
    auto &row = code.getHz()[i];
    Value ancilla =
        QPhysAllocOp::create(builder, loc, builder.getType<QPhysQubitType>(),
                             nextId++, builder.getStringAttr("z_anc"));
    for (size_t j = 0; j < code.getN(); ++j) {
      if (row[j]) {
        auto cnotOp = QPhysCXOp::create(builder, loc, values[j], ancilla);
        values[j] = cnotOp.getO0();
        ancilla = cnotOp.getO1();
      }
    }
    Value measure = QPhysMeasureOp::create(builder, loc, ancilla,
                                           builder.getStringAttr("sz"), nullptr,
                                           nullptr, nullptr, nullptr);
    syndrome.push_back(measure);
    size_t index = code.getN() + code.getHx().size() + i;
    if (addDetectors) {
      if (values[index].getDefiningOp<arith::ConstantIntOp>())
        QPhysDetectorOp::create(builder, loc, {measure});
      else
        QPhysDetectorOp::create(builder, loc, {values[index], measure});
    }
    values[index] = measure;
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
    SmallVector<Value> values, syndrome;
    values.reserve(n + op.getType().getCode().getAncillaQubits());
    for (unsigned i = 0; i < n; ++i) {
      Value qubit = QPhysAllocOp::create(
          rewriter, loc, rewriter.getType<QPhysQubitType>(), nextId++,
          rewriter.getStringAttr("data"));
      values.push_back(qubit);
    }
    Value const0 = arith::ConstantIntOp::create(rewriter, loc, 0, 1);
    values.append(op.getType().getCode().getAncillaQubits(), const0);
    // TODO: Use a fault-tolerant encoding scheme.
    measureXSyndrome(op.getType().getCode().getStabilizerCode(), values,
                     false, syndrome, nextId, rewriter, loc);
    rewriter.replaceOpWithMultiple(op, {std::move(values)});
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
    SmallVector<Value> values, syndrome;
    values.reserve(n + op.getType().getCode().getAncillaQubits());
    for (unsigned i = 0; i < n; ++i) {
      Value qubit = QPhysAllocOp::create(
          rewriter, loc, rewriter.getType<QPhysQubitType>(), nextId++,
          rewriter.getStringAttr("data"));
      qubit = QPhysHOp::create(rewriter, loc, qubit);
      values.push_back(qubit);
    }
    Value const0 = arith::ConstantIntOp::create(rewriter, loc, 0, 1);
    values.append(op.getType().getCode().getAncillaQubits(), const0);
    // TODO: Use a fault-tolerant encoding scheme.
    measureZSyndrome(op.getType().getCode().getStabilizerCode(), values,
                     false, syndrome, nextId, rewriter, loc);
    rewriter.replaceOpWithMultiple(op, {std::move(values)});
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
    SmallVector<Value> values = adaptor.getLogicalQubit(), syndrome;
    measureXSyndrome(code, values, true, syndrome, nextId, rewriter, op.getLoc());
    measureZSyndrome(code, values, true, syndrome, nextId, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(values), std::move(syndrome)});
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
    SmallVector<Value> values = adaptor.getLogicalQubit(), syndrome;
    measureXSyndrome(code, values, true, syndrome, nextId, rewriter, op.getLoc());
    measureZSyndrome(code, values, true, syndrome, nextId, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(values), std::move(syndrome)});
    return success();
  }
};

struct LogicalXPattern : public OpConversionPattern<LogicalXOp> {
  using Base::Base;

  LogicalResult
  matchAndRewrite(LogicalXOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getQubit().getType().getCode().getStabilizerCode();
    SmallVector<Value> values = adaptor.getQubit();
    applyPauliString(code.getLogicalX(), values, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(values)});
    return success();
  }
};

struct LogicalZPattern : public OpConversionPattern<LogicalZOp> {
  using Base::Base;

  LogicalResult
  matchAndRewrite(LogicalZOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getQubit().getType().getCode().getStabilizerCode();
    SmallVector<Value> values = adaptor.getQubit();
    applyPauliString(code.getLogicalZ(), values, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(values)});
    return success();
  }
};

struct LogicalYPattern : public OpConversionPattern<LogicalYOp> {
  using Base::Base;

  LogicalResult
  matchAndRewrite(LogicalYOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getQubit().getType().getCode().getStabilizerCode();
    SmallVector<Value> values = adaptor.getQubit();
    applyPauliString(code.getLogicalX(), values, rewriter, op.getLoc());
    applyPauliString(code.getLogicalZ(), values, rewriter, op.getLoc());
    rewriter.replaceOpWithMultiple(op, {std::move(values)});
    return success();
  }
};

struct LogicalMeasurePattern : public OpConversionPattern<LogicalMeasureOp> {
  using Base::Base;

  LogicalResult
  matchAndRewrite(LogicalMeasureOp op, OneToNOpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    auto code = op.getLogicalQubit().getType().getCode().getStabilizerCode();
    auto isXBasis = op.getBasis() == 1;
    auto &paulis = isXBasis ? code.getLogicalX() : code.getLogicalZ();
    auto H = isXBasis ? code.getHx() : code.getHz();
    size_t baseIndex = code.getN() + (isXBasis ? 0 : code.getHx().size());
    Location loc = op.getLoc();
    ValueRange values = adaptor.getLogicalQubit();
    SmallVector<Value> bits, observable;
    Value acc;
    for (size_t i = 0; i < code.getN(); ++i) {
      Value qubit = values[i];
      if (isXBasis)
        // In principle we should undo this basis change after measurement, but
        // it can be omitted since LogicalMeasureOp releases the logical qubit.
        qubit = QPhysHOp::create(rewriter, loc, qubit);
      Value bit = QPhysMeasureOp::create(rewriter, loc, qubit,
                                         rewriter.getStringAttr("m"), nullptr,
                                         nullptr, nullptr, nullptr);
      bits.push_back(bit);
      if (paulis[i] == (isXBasis ? Pauli::X : Pauli::Z)) {
        observable.push_back(bit);
        acc = acc ? arith::XOrIOp::create(rewriter, loc, acc, bit) : bit;
      } else if (paulis[i] != Pauli::I) {
        op.emitError()
            << "ftqc-expand-physical-generic: only CSS codes where logical "
            << "ZX"[isXBasis] << " operators are products of " << "ZX"[isXBasis]
            << " and I are supported";
        return failure();
      }
    }
    for (size_t i = 0; i < H.size(); ++i) {
      auto &row = H[i];
      SmallVector<Value> detector;
      if (!values[baseIndex + i].getDefiningOp<arith::ConstantIntOp>())
        detector.push_back(values[baseIndex + i]);
      for (size_t j = 0; j < code.getN(); ++j)
        if (row[j])
          detector.push_back(bits[j]);
      QPhysDetectorOp::create(rewriter, loc, detector);
    }
    QPhysObservableOp::create(rewriter, loc, observable);
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
          types.append(lqt.getPhysicalQubits(), QPhysQubitType::get(ctx));
          types.append(lqt.getCode().getAncillaQubits(), IntegerType::get(ctx, 1));
          return success();
        });
    typeConverter.addConversion(
        [ctx](SyndromeType st, SmallVectorImpl<Type> &types) {
          types.append(st.getWidth(), IntegerType::get(ctx, 1));
          return success();
        });

    ConversionTarget target(*ctx);
    target.addDynamicallyLegalDialect<FTQCDialect>([](Operation *op) {
      return op->getName().getStringRef().starts_with("ftqc.qphys.");
    });
    target.addLegalOp<arith::ConstantIntOp, arith::XOrIOp>();
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
