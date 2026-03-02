// RUN: ftqc-opt %s | FileCheck %s
// Test: Full EC round (syndrome extract + correction). (test 27)

// CHECK-LABEL: func @full_ec_round
func.func @full_ec_round(%lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  // CHECK: ftqc.ec_round
  %lq2, %syn = ftqc.ec_round %lq rounds = 3
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  func.return %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}

// CHECK-LABEL: func @ec_between_gates
func.func @ec_between_gates() {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  %h = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.ec_round
  %hec, %syn0 = ftqc.ec_round %h rounds = 3
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)

  %c, %t = ftqc.logical_cnot %hec, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  // CHECK: ftqc.ec_round
  %cec, %synC = ftqc.ec_round %c rounds = 3
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)

  ftqc.dealloc %cec : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  ftqc.dealloc %t   : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}
