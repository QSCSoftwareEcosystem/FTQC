// RUN: ftqc-opt %s | FileCheck %s
// Test: ftqc.init_zero and ftqc.init_plus operations.

// CHECK-LABEL: func @init_zero_steane
func.func @init_zero_steane() -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // CHECK: ftqc.init_zero : <<steane>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @init_plus_steane
func.func @init_plus_steane() -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // CHECK: ftqc.init_plus : <<steane>>
  %lq = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @init_zero_surface
func.func @init_zero_surface() -> !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>> {
  // CHECK: ftqc.init_zero : <<surface, dist = 3>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
}
