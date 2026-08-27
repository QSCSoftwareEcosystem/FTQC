// RUN: ftqc-opt %s --ftqc-transversal-gates | FileCheck %s
// Test: Transversal X gate verification. (test 66)

// CHECK-LABEL: func @transversal_x_steane
func.func @transversal_x_steane(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // X is transversal on all CSS codes.
  // CHECK: ftqc.logical_x
  %out = ftqc.logical_x %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
