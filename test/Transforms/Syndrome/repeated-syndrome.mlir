// RUN: ftqc-opt %s | FileCheck %s
// Test: Repeated syndrome measurement for space-time majority vote. (test 56)

// CHECK-LABEL: func @repeated_syndrome_3rounds
func.func @repeated_syndrome_3rounds() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // Three rounds of syndrome extraction.
  // CHECK-COUNT-3: ftqc.extract_syndrome
  %lq1, %syn1 = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  %lq2, %syn2 = ftqc.extract_syndrome %lq1
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  %lq3, %syn3 = ftqc.extract_syndrome %lq2
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  // Apply correction based on majority of syn1, syn2, syn3.
  %lq4 = ftqc.correct %lq3, %syn3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  ftqc.dealloc %lq4 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}
