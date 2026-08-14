// RUN: ftqc-opt %s | FileCheck %s
// Test: Color code and concatenated code attributes.

// CHECK-LABEL: func @color_d3
func.func @color_d3() {
  // CHECK: <#ftqc.ecc<color_code, 3>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
  ftqc.dealloc %lq : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
  func.return
}

// CHECK-LABEL: func @color_d5
func.func @color_d5() {
  // CHECK: <#ftqc.ecc<color_code, 5>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<color_code, 5>>
  ftqc.dealloc %lq : !ftqc.logical_qubit<#ftqc.ecc<color_code, 5>>
  func.return
}

// CHECK-LABEL: func @concat_2levels
func.func @concat_2levels() {
  // CHECK: <#ftqc.ecc<concat, 3, 2>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<concat, 3, 2>>
  ftqc.dealloc %lq : !ftqc.logical_qubit<#ftqc.ecc<concat, 3, 2>>
  func.return
}
