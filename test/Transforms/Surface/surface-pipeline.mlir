// RUN: ftqc-opt %s --ftqc-surface-pipeline | FileCheck %s
// Test: Full surface code pipeline. (test 40)

// CHECK-LABEL: func @surface_bell
func.func @surface_bell() {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  %h = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  // CHECK: ftqc.logical_cnot
  %c, %t = ftqc.logical_cnot %h, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>)
  %b0 = ftqc.logical_measure %c
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>) -> i1
  %b1 = ftqc.logical_measure %t
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>) -> i1
  func.return
}
