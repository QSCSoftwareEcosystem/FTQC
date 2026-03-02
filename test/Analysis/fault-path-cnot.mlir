// RUN: ftqc-opt %s --ftqc-fault-path 2>&1 | FileCheck %s
// Test: Fault path analysis for a 2-qubit CNOT circuit. (test 72)

// CHECK: Fault path analysis for @cnot_fault_paths
// CHECK-LABEL: func @cnot_fault_paths
func.func @cnot_fault_paths() {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %h   = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c, %t = ftqc.logical_cnot %h, %lq1
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
