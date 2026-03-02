//===- FTQCDialect.h - FTQC Dialect Declaration ---------------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file FTQCDialect.h
/// @brief Top-level include for the FTQC MLIR dialect.
///
/// The **FTQC dialect** (`ftqc`) is an MLIR dialect for expressing
/// fault-tolerant quantum computations at the *logical* level.  Programs are
/// written in terms of logical qubits backed by an error-correcting code (ECC)
/// and a pass pipeline progressively lowers the IR towards physical operations.
///
/// ### Dialect namespace
/// All FTQC operations, types, and attributes live under the `"ftqc"` mnemonic.
///
/// ### Required dependent dialects
/// The following dialects must be loaded before creating FTQC ops:
/// - `arith` — integer/float constants used in ancilla circuits
/// - `func`  — function boundaries within which logical circuits live
/// - `scf`   — structured control flow for syndrome-extraction loops
///
/// ### Typical usage
/// @code{.cpp}
/// MLIRContext ctx;
/// ctx.loadDialect<ftqc::FTQCDialect>();
/// @endcode
///
/// @see FTQCOps.h
/// @see FTQCTypes.h
/// @see FTQCAttrs.h

#ifndef FTQC_DIALECT_H
#define FTQC_DIALECT_H

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Dialect.h"

// Include the TableGen-generated dialect declaration.
#include "FTQC/Dialect/FTQCDialect.h.inc"

#endif // FTQC_DIALECT_H
