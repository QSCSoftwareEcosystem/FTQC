// RUN: ftqc-opt %s | ftqc-opt | FileCheck %s
// Test: Full parse → print → parse round-trip of a realistic circuit.

// CHECK-LABEL: func @bell_state_ft
func.func @bell_state_ft()
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>) {
  // CHECK: ftqc.init_zero
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  // CHECK: ftqc.logical_h
  %h0 = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  // CHECK: ftqc.logical_cnot
  %c, %t = ftqc.logical_cnot %h0, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)

  // CHECK: ftqc.ec_round
  %c2, %syn0 = ftqc.ec_round %c
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
  %t2, %syn1 = ftqc.ec_round %t
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)

  func.return %c2, %t2
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
