// RUN: ftqc-opt %s | FileCheck %s
// Test: LogicalQubitType round-trip for all supported codes.

// CHECK-LABEL: func @steane_qubit
func.func @steane_qubit() {
  // CHECK: ftqc.init_zero : <#ftqc.ecc<steane, 3>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @surface_qubit_d3
func.func @surface_qubit_d3() {
  // CHECK: <#ftqc.ecc<surface, 3>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  func.return
}

// CHECK-LABEL: func @surface_qubit_d5
func.func @surface_qubit_d5() {
  // CHECK: <#ftqc.ecc<surface, 5>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  func.return
}

// CHECK-LABEL: func @color_code_qubit
func.func @color_code_qubit() {
  // CHECK: <#ftqc.ecc<color_code, 3>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
  func.return
}

// CHECK-LABEL: func @concat_qubit
func.func @concat_qubit() {
  // CHECK: <#ftqc.ecc<concat, 3, 2>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<concat, 3, 2>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<concat, 3, 2>>
  func.return
}
