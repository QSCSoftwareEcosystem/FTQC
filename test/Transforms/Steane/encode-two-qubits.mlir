// RUN: ftqc-opt %s | FileCheck %s
// Test: Two-qubit circuit expressed entirely in FTQC dialect.
// (test 20)

// CHECK-LABEL: func @two_qubit_encoded
func.func @two_qubit_encoded() {
  // CHECK: ftqc.init_zero
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // CHECK: ftqc.logical_h
  %h0 = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // CHECK: ftqc.logical_cnot
  %c, %t = ftqc.logical_cnot %h0, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  %b0 = ftqc.logical_measure %c
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  %b1 = ftqc.logical_measure %t
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  func.return
}
