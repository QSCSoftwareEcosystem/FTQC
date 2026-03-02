// RUN: ftqc-opt %s | FileCheck %s
// Test: Shor syndrome extraction on multi-qubit circuit. (test 55)

// CHECK-LABEL: func @shor_multi
func.func @shor_multi() {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c, %t = ftqc.logical_cnot %lq0, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  // CHECK: ftqc.shor_syndrome
  %c2, %syn0 = ftqc.shor_syndrome %c
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  // CHECK: ftqc.shor_syndrome
  %t2, %syn1 = ftqc.shor_syndrome %t
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  ftqc.dealloc %c2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  ftqc.dealloc %t2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}
