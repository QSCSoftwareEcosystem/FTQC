// RUN: ftqc-opt %s | FileCheck %s
// Test: Logical Z on surface code (left/right boundary string). (test 36)
func.func @surface_logical_z() {
  %lq = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  // CHECK: ftqc.logical_z
  %lq2 = ftqc.logical_z %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}
