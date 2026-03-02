// RUN: ftqc-opt %s | FileCheck %s
// Test: Complete Clifford gate set on Steane code. (test 70)
// {X, Z, Y, H, S, CNOT, CZ} all expressed as FTQC logical ops.

// CHECK-LABEL: func @full_clifford_set
func.func @full_clifford_set(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %lq1: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  // CHECK: ftqc.logical_x
  %x = ftqc.logical_x %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_z
  %z = ftqc.logical_z %x : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_h
  %h = ftqc.logical_h %z : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_s
  %s = ftqc.logical_s %h : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_cnot
  %c2, %t2 = ftqc.logical_cnot %s, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  ftqc.dealloc %t2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %c2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
