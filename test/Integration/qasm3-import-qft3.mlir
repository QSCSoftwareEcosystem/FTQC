// RUN: %qasm3-import %S/qft3.qasm | %ftqc-opt | FileCheck %s
//
// Test: 3-qubit QFT (Clifford+T) OpenQASM 3.0 → FTQC MLIR.
// Verifies that T gates are expanded to prepare_magic_t + logical_t,
// and that CNOT and H gates map correctly.

// CHECK-LABEL: func.func @qft3
// CHECK:         ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
// CHECK:         ftqc.logical_h
// CHECK:         ftqc.prepare_magic_t : !ftqc.magic_state
// CHECK:         ftqc.logical_t
// CHECK:         ftqc.logical_cnot
// CHECK:         func.return
