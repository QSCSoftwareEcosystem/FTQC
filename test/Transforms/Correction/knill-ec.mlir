// RUN: ftqc-opt %s | FileCheck %s
// Test: Knill EC via quantum teleportation. (test 61)

// CHECK-LABEL: func @knill_ec_single
func.func @knill_ec_single() {
  %data = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %anc0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %anc1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.knill_ec
  %corrected, %syn = ftqc.knill_ec %data, %anc0, %anc1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  ftqc.dealloc %corrected : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}
