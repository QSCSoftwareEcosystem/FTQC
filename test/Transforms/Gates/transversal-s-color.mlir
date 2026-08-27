// RUN: ftqc-opt %s --ftqc-transversal-gates | FileCheck %s
// Test: Transversal S on color code (not Steane). (test 68)

// CHECK-LABEL: func @transversal_s_color_ok
func.func @transversal_s_color_ok(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>> {
  // S is transversal on 2D color codes.
  // No decomposition expected.
  // CHECK: ftqc.logical_s
  // CHECK-NOT: ftqc.logical_t
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>>
}

// CHECK-LABEL: func @s_steane_decomposed
func.func @s_steane_decomposed(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // S on Steane → decomposed into two T gates via magic-state injection.
  // CHECK: ftqc.logical_t
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
