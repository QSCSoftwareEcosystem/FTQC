// RUN: ftqc-opt %s | FileCheck %s
// Test: Logical X on surface code (transversal on top/bottom boundary). (test 35)

// CHECK-LABEL: func @surface_logical_x
func.func @surface_logical_x() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  // CHECK: ftqc.logical_x
  %lq2 = ftqc.logical_x %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}
