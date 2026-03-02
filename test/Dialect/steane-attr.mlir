// RUN: ftqc-opt %s | FileCheck %s
// Test: Steane ECC attribute round-trip.

// CHECK-LABEL: func @steane_attr
func.func @steane_attr() {
  // CHECK: <<steane, 3>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  ftqc.dealloc %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @code_properties_attr
func.func @code_properties_attr() attributes {
    // CHECK: #ftqc.code_props<n = 7, k = 1, d = 3>
    ftqc.code = #ftqc.code_props<n = 7, k = 1, d = 3>} {
  func.return
}
