// RUN: ftqc-opt %s | FileCheck %s
// Test: Surface code attribute round-trip.

// CHECK-LABEL: func @surface_d3
func.func @surface_d3() {
  // CHECK: <<surface, 3>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  ftqc.dealloc %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  func.return
}

// CHECK-LABEL: func @surface_d5
func.func @surface_d5() {
  // CHECK: <<surface, 5>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  ftqc.dealloc %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  func.return
}

// CHECK-LABEL: func @surface_d7
func.func @surface_d7() {
  // CHECK: <<surface, 7>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 7>>
  ftqc.dealloc %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, 7>>
  func.return
}
