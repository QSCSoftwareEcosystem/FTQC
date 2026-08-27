// RUN: ftqc-opt %s | FileCheck %s
// Test: Fault-tolerant logical measurement on Steane code. (test 29)

// CHECK-LABEL: func @ft_measure_z
func.func @ft_measure_z() -> i1 {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // Apply EC round before measurement for fault-tolerance.
  %lq2, %syn = ftqc.ec_round %lq rounds = 3
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
  // CHECK: ftqc.logical_measure
  %bit = ftqc.logical_measure %lq2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  func.return %bit : i1
}

// CHECK-LABEL: func @ft_measure_x_basis
func.func @ft_measure_x_basis() -> i1 {
  %lq = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq2, %syn = ftqc.ec_round %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
  // Measure in X basis: H then Z-measure.
  %h = ftqc.logical_h %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.logical_measure
  %bit = ftqc.logical_measure %h
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  func.return %bit : i1
}
