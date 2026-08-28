// RUN: ftqc-opt %s | FileCheck %s
// Test: LogicalQubitType round-trip for all supported codes.

// CHECK-LABEL: func @steane_qubit
func.func @steane_qubit() {
  // CHECK: ftqc.init_zero : <<steane>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}

// CHECK-LABEL: func @surface_qubit_d3
func.func @surface_qubit_d3() {
  // CHECK: <<surface, dist = 3>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}

// CHECK-LABEL: func @surface_qubit_d5
func.func @surface_qubit_d5() {
  // CHECK: <<surface, dist = 5>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  func.return
}

// CHECK-LABEL: func @color_code_qubit
func.func @color_code_qubit() {
  // CHECK: <<color_code, dist = 3>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>>
  func.return
}

// CHECK-LABEL: func @concat_qubit
func.func @concat_qubit() {
  // CHECK: <<concat, outer = <steane>, inner = <steane>>>
  %0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<concat, outer = <steane>, inner = <steane>>>
  ftqc.dealloc %0 : !ftqc.logical_qubit<#ftqc.ecc<concat, outer = <steane>, inner = <steane>>>
  func.return
}
