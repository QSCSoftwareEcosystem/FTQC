// RUN: ftqc-opt %s --ftqc-fault-path 2>&1 | FileCheck %s
// Test: Fault path analysis for a 2-qubit CNOT circuit. (test 72)

// CHECK: Fault path analysis for @cnot_fault_paths
// CHECK-LABEL: func @cnot_fault_paths
func.func @cnot_fault_paths() {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %h   = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %c, %t = ftqc.logical_cnot %h, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %b0 = ftqc.logical_measure %c
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b1 = ftqc.logical_measure %t
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  func.return
}
