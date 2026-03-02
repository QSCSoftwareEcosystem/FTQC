// RUN: ftqc-opt %s | FileCheck %s
// Test: SyndromeType round-trip for various widths.

// CHECK-LABEL: func @steane_syndrome
func.func @steane_syndrome() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: <6>
  %lq2, %syn = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @surface_d3_syndrome
func.func @surface_d3_syndrome() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  // CHECK: <16>
  %lq2, %syn = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>, !ftqc.syndrome<16>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  func.return
}
