// RUN: ftqc-opt %s | FileCheck %s
// Test: ftqc.correct and ftqc.ec_round operations.

// CHECK-LABEL: func @correct_op
func.func @correct_op() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq2, %syn = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  // CHECK: ftqc.correct %{{.*}}, %{{.*}}
  %lq3 = ftqc.correct %lq2, %syn
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  ftqc.dealloc %lq3 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @ec_round_default
func.func @ec_round_default() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.ec_round %{{.*}}
  %lq2, %syn = ftqc.ec_round %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @ec_round_3cycles
func.func @ec_round_3cycles() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.ec_round %{{.*}} rounds = 3
  %lq2, %syn = ftqc.ec_round %lq rounds = 3
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}
