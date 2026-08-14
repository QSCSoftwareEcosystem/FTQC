//===- FTQCOps.h - FTQC Operation Declarations ----------------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file FTQCOps.h
/// @brief Declarations for all FTQC dialect operations.
///
/// This header pulls in the TableGen-generated operation classes for the FTQC
/// dialect.  The operations are organised into five groups:
///
/// | Group | Ops | Description |
/// |---|---|---|
/// | Initialisation | `ftqc.init_zero`, `ftqc.prepare_magic_t` | Allocate logical qubits / magic states |
/// | Logical gates | `ftqc.logical_h`, `ftqc.logical_x`, `ftqc.logical_z`, `ftqc.logical_y`, `ftqc.logical_s`, `ftqc.logical_cnot`, `ftqc.logical_cz`, `ftqc.logical_t` | Single- and two-qubit fault-tolerant gates |
/// | Syndrome extraction | `ftqc.steane_x_syndrome`, `ftqc.steane_z_syndrome`, `ftqc.shor_syndrome`, `ftqc.flag_syndrome`, `ftqc.extract_syndrome` | Code-specific syndrome measurement ops |
/// | Error correction | `ftqc.correct`, `ftqc.ec_round` | Apply correction and track error-correction rounds |
/// | Measurement & metadata | `ftqc.logical_measure`, `ftqc.resource_annotation` | Measurement and analysis annotations |
///
/// ### Assembly syntax examples
/// @code{.mlir}
/// // Initialise a Steane [[7,1,3]] logical qubit
/// %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
///
/// // Transversal Hadamard
/// %out = ftqc.logical_h %lq : <#ftqc.ecc<steane, 3>>
///
/// // Steane X-stabiliser syndrome extraction
/// %q2, %syn = ftqc.steane_x_syndrome %out
///     : <#ftqc.ecc<steane, 3>> -> (<#ftqc.ecc<steane, 3>>, <6>)
/// @endcode
///
/// @note Individual operation semantics are documented in FTQCOps.td.
/// @see FTQCTypes.h — MLIR type definitions used as operand/result types.
/// @see FTQCAttrs.h — ECC attribute that parameterises logical qubit types.

#ifndef FTQC_OPS_H
#define FTQC_OPS_H

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Interfaces/InferTypeOpInterface.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCTypes.h"

#define GET_OP_CLASSES
#include "FTQC/Dialect/FTQCOps.h.inc"

#endif // FTQC_OPS_H
