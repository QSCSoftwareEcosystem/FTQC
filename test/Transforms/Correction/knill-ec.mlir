// RUN: ftqc-opt %s | FileCheck %s
// Test: Knill EC via quantum teleportation. (test 61)

// CHECK-LABEL: func @knill_ec_single
func.func @knill_ec_single() {
  %data = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %anc0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %anc1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.knill_ec
  %corrected, %syn = ftqc.knill_ec %data, %anc0, %anc1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
  ftqc.dealloc %corrected : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
