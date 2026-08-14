// RUN: ftqc-opt %s | FileCheck %s
// Test: All syndrome extraction operations.

// CHECK-LABEL: func @extract_syndrome
func.func @extract_syndrome() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.extract_syndrome
  // CHECK-SAME: <#ftqc.ecc<steane, 3>>
  // CHECK-SAME: <6>
  %lq2, %syn = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @flag_syndrome
func.func @flag_syndrome() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.flag_syndrome
  %lq2, %syn, %flag = ftqc.flag_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>, i1)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @shor_syndrome
func.func @shor_syndrome() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.shor_syndrome
  %lq2, %syn = ftqc.shor_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @steane_x_syndrome
func.func @steane_x_syndrome() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.steane_x_syndrome
  %lq2, %xsyn = ftqc.steane_x_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<3>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @steane_z_syndrome
func.func @steane_z_syndrome() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.steane_z_syndrome
  %lq2, %zsyn = ftqc.steane_z_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<3>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}
