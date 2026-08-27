// RUN: ftqc-opt %s | FileCheck %s
// Test: One full surface-code syndrome cycle. (test 38)

// CHECK-LABEL: func @surface_syndrome_cycle
func.func @surface_syndrome_cycle() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  // CHECK: ftqc.surface_round
  %lq2, %syn = ftqc.surface_round %lq
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>, !ftqc.syndrome<16>)
  %lq3 = ftqc.correct %lq2, %syn
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>, !ftqc.syndrome<16>)
      -> !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  ftqc.dealloc %lq3 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}
