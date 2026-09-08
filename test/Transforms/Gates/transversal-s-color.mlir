// RUN: ftqc-opt %s --ftqc-transversal-gates | FileCheck %s
// Test: Transversal S on color code (not Steane). (test 68)

// CHECK-LABEL: func @transversal_s_color_ok
func.func @transversal_s_color_ok(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>> {
  // S is transversal on 2D color codes.
  // No decomposition expected.
  // CHECK: ftqc.logical_s
  // CHECK-NOT: ftqc.logical_t
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
}

// CHECK-LABEL: func @s_steane_transversal
func.func @s_steane_transversal(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  // S is transversal on the Steane code (self-dual CSS) → no magic-state
  // injection, so the logical_s is preserved and no logical_t is introduced.
  // CHECK: ftqc.logical_s
  // CHECK-NOT: ftqc.logical_t
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
