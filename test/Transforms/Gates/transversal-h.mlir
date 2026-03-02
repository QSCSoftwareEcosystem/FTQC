// RUN: ftqc-opt %s --ftqc-transversal-gates | FileCheck %s
// Test: Transversal H gate verification. (test 65)

// CHECK-LABEL: func @transversal_h_steane
func.func @transversal_h_steane(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  // H is transversal on Steane code.
  // CHECK: ftqc.logical_h
  %out = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}

// CHECK-LABEL: func @transversal_h_color
func.func @transversal_h_color(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>> {
  // CHECK: ftqc.logical_h
  %out = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
}
