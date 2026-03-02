//===- FTQCTypes.cpp - FTQC Type Implementations --------------------------===//
#include "FTQC/Dialect/FTQCTypes.h"

using namespace mlir;
using namespace mlir::ftqc;

bool mlir::ftqc::isLogicalQubitType(Type t) {
  return isa<LogicalQubitType>(t);
}
bool mlir::ftqc::isSyndromeType(Type t) {
  return isa<SyndromeType>(t);
}
bool mlir::ftqc::isMagicStateType(Type t) {
  return isa<MagicStateType>(t);
}
bool mlir::ftqc::isPauliFrameType(Type t) {
  return isa<PauliFrameType>(t);
}
