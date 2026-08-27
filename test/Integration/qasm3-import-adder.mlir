// RUN: %qasm3-import %S/adder.qasm | %ftqc-opt | FileCheck %s
//
// Test: 2-bit adder with Toffoli gates → FTQC MLIR.
// Verifies CCX → prepare_magic_ccz + logical_toffoli mapping.

// CHECK-LABEL: func.func @adder
// CHECK:         ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, dist = 3>>
// CHECK:         ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
// CHECK:         ftqc.logical_toffoli
// CHECK:         ftqc.logical_cnot
// CHECK:         ftqc.logical_measure
// CHECK:         func.return
