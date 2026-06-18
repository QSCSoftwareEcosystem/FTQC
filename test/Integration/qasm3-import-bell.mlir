// RUN: %qasm3-import %S/bell.qasm | %ftqc-opt | FileCheck %s
// RUN: %qasm3-import --ecc=surface --distance=5 %S/bell.qasm \
// RUN:   | %ftqc-opt | FileCheck %s --check-prefix=SURFACE
//
// Test: OpenQASM 3.0 Bell state → FTQC MLIR (default Steane [[7,1,3]])
//       and surface code (d=5) variant.

// CHECK-LABEL: func.func @bell
// CHECK:         ftqc.init_zero : {{.*steane, 3.*}}
// CHECK:         ftqc.logical_h
// CHECK:         ftqc.logical_cnot
// CHECK:         ftqc.logical_measure
// CHECK:         return

// SURFACE-LABEL: func.func @bell
// SURFACE:         ftqc.init_zero : {{.*surface, 5.*}}
// SURFACE:         ftqc.logical_h
// SURFACE:         ftqc.logical_cnot
// SURFACE:         ftqc.logical_measure
