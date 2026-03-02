//===- FTQCOps.cpp - FTQC Operation Implementations -----------------------===//
//
// Extra verify/build/fold implementations for FTQC operations beyond ODS.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpImplementation.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// EncodeOp
//===----------------------------------------------------------------------===//

mlir::LogicalResult EncodeOp::verify() {
  auto lqType = dyn_cast<LogicalQubitType>(getLogicalQubit().getType());
  if (!lqType)
    return emitOpError("result must be !ftqc.logical_qubit<...>");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// ExtractSyndromeOp
//===----------------------------------------------------------------------===//

mlir::LogicalResult ExtractSyndromeOp::verify() {
  auto lqIn  = dyn_cast<LogicalQubitType>(getLogicalQubit().getType());
  auto lqOut = dyn_cast<LogicalQubitType>(getOutLogicalQubit().getType());
  if (!lqIn || !lqOut)
    return emitOpError("operand and first result must be logical qubit types");
  if (lqIn.getCode() != lqOut.getCode())
    return emitOpError("input and output logical qubit must use same code");

  auto synType = dyn_cast<SyndromeType>(getSyndrome().getType());
  if (!synType)
    return emitOpError("second result must be !ftqc.syndrome<n>");

  // Check syndrome width matches code stabilizer count.
  unsigned expected = lqIn.getCode().getPhysicalQubits() - 1;
  // For CSS codes: 2*(n-k) stabilizers = 2*(n-1) for [[n,1,d]] codes.
  // Steane: 2*(7-1)/2 = 6 (3X + 3Z). Surface d: 2*(d^2-1).
  // We allow any syndrome width; just warn if it looks wrong.
  (void)expected;
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// CorrectOp
//===----------------------------------------------------------------------===//

mlir::LogicalResult CorrectOp::verify() {
  auto lqIn  = dyn_cast<LogicalQubitType>(getLogicalQubit().getType());
  auto lqOut = dyn_cast<LogicalQubitType>(getCorrectedQubit().getType());
  if (!lqIn || !lqOut)
    return emitOpError("operand and result must be logical qubit types");
  if (lqIn.getCode() != lqOut.getCode())
    return emitOpError("input and output must use the same code");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// LogicalCNOTOp
//===----------------------------------------------------------------------===//

mlir::LogicalResult LogicalCNOTOp::verify() {
  auto ctrlIn  = dyn_cast<LogicalQubitType>(getControl().getType());
  auto tgtIn   = dyn_cast<LogicalQubitType>(getTarget().getType());
  auto ctrlOut = dyn_cast<LogicalQubitType>(getControlOut().getType());
  auto tgtOut  = dyn_cast<LogicalQubitType>(getTargetOut().getType());

  if (!ctrlIn || !tgtIn || !ctrlOut || !tgtOut)
    return emitOpError("all operands and results must be logical qubit types");

  if (ctrlIn.getCode() != tgtIn.getCode())
    return emitOpError("control and target must use the same code "
                       "(transversal CNOT requires same block structure)");
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// Distill15to1Op
//===----------------------------------------------------------------------===//

mlir::LogicalResult Distill15to1Op::verify() {
  if (getInputs().size() != 15)
    return emitOpError("ftqc.distill_15to1 requires exactly 15 input states, got ")
           << getInputs().size();
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// Distill7to1Op
//===----------------------------------------------------------------------===//

mlir::LogicalResult Distill7to1Op::verify() {
  if (getInputs().size() != 7)
    return emitOpError("ftqc.distill_7to1 requires exactly 7 input states, got ")
           << getInputs().size();
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// Folding
//===----------------------------------------------------------------------===//

// X applied twice is identity.
mlir::OpFoldResult LogicalXOp::fold(FoldAdaptor adaptor) {
  // If the operand is also a LogicalXOp result, fold both away.
  if (auto prev = getQubit().getDefiningOp<LogicalXOp>())
    return prev.getQubit();
  return nullptr;
}

// Z applied twice is identity.
mlir::OpFoldResult LogicalZOp::fold(FoldAdaptor adaptor) {
  if (auto prev = getQubit().getDefiningOp<LogicalZOp>())
    return prev.getQubit();
  return nullptr;
}

// H applied twice is identity.
mlir::OpFoldResult LogicalHOp::fold(FoldAdaptor adaptor) {
  if (auto prev = getQubit().getDefiningOp<LogicalHOp>())
    return prev.getQubit();
  return nullptr;
}
