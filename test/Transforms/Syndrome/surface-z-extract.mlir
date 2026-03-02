// RUN: ftqc-opt %s | FileCheck %s
// Test: Surface code Z stabilizer extraction. (test 52)
func.func @surface_z_stabilizers() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  // CHECK: ftqc.surface_round
  %lq2, %syn = ftqc.surface_round %lq cycles = 1
      : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>, !ftqc.syndrome<16>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  func.return
}
