// RUN: ftqc-opt %s --ftqc-fault-path 2>&1 | FileCheck %s
// Test: Leading-order fault path analysis on a single-qubit circuit. (test 71)

// CHECK: Fault path analysis for @single_qubit_fault_paths
// CHECK-LABEL: func @single_qubit_fault_paths
func.func @single_qubit_fault_paths() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %h = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq2, %syn = ftqc.ec_round %h
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
  %bit = ftqc.logical_measure %lq2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  func.return
}
