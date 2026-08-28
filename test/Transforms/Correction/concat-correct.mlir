// RUN: ftqc-opt %s | FileCheck %s
// Test: Error correction on concatenated code. (test 63)

// CHECK-LABEL: func @concat_ec
func.func @concat_ec() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<concat, outer = <steane>, inner = <steane>>>
  // CHECK: ftqc.ec_round
  %lq2, %syn = ftqc.ec_round %lq rounds = 3
      : !ftqc.logical_qubit<#ftqc.ecc<concat, outer = <steane>, inner = <steane>>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<concat, outer = <steane>, inner = <steane>>>, !ftqc.syndrome<12>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<concat, outer = <steane>, inner = <steane>>>
  func.return
}
