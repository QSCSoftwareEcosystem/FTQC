// RUN: ftqc-opt %s | FileCheck %s
// Test: Z stabilizer syndrome extraction for Steane code. (test 26)

// CHECK-LABEL: func @steane_z_syndrome_clean
func.func @steane_z_syndrome_clean() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.steane_z_syndrome
  %lq2, %zsyn = ftqc.steane_z_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}

// CHECK-LABEL: func @full_xz_syndrome
func.func @full_xz_syndrome() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq2, %xsyn = ftqc.steane_x_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
  // CHECK: ftqc.steane_z_syndrome
  %lq3, %zsyn = ftqc.steane_z_syndrome %lq2
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
  %lq4 = ftqc.correct %lq3, %xsyn
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq5 = ftqc.correct %lq4, %zsyn
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  ftqc.dealloc %lq5 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
