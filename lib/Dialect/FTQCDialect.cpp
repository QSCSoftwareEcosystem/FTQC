//===- FTQCDialect.cpp - FTQC Dialect Registration ------------------------===//
//
// Registers the FTQC dialect, its types, attributes, and operations.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

// Pull in TableGen-generated dialect definitions.
#include "FTQC/Dialect/FTQCDialect.cpp.inc"

// Pull in enum implementations (symbolize/stringify for ECCKind, MagicKind, etc.)
#include "FTQC/Dialect/FTQCEnums.cpp.inc"

// Pull in attribute definitions.
#define GET_ATTRDEF_CLASSES
#include "FTQC/Dialect/FTQCAttrs.cpp.inc"

#define GET_TYPEDEF_CLASSES
#include "FTQC/Dialect/FTQCTypes.cpp.inc"

#define GET_OP_CLASSES
#include "FTQC/Dialect/FTQCOps.cpp.inc"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// FTQCDialect::initialize
//===----------------------------------------------------------------------===//

void FTQCDialect::initialize() {
  registerTypes();
  registerAttrs();
  registerOps();
}

void FTQCDialect::registerTypes() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "FTQC/Dialect/FTQCTypes.cpp.inc"
  >();
}

void FTQCDialect::registerAttrs() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "FTQC/Dialect/FTQCAttrs.cpp.inc"
  >();
}

void FTQCDialect::registerOps() {
  addOperations<
#define GET_OP_LIST
#include "FTQC/Dialect/FTQCOps.cpp.inc"
  >();
}
