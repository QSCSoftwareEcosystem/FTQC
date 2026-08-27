// RUN: ftqc-opt %s | FileCheck %s
// Test: Distance-3 surface code encoding. (test 31)

// CHECK-LABEL: func @surface_d3_init
func.func @surface_d3_init() -> !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>> {
  // CHECK: ftqc.init_zero : <<surface, dist = 3>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
}

// CHECK-LABEL: func @surface_d3_ec
func.func @surface_d3_ec() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  // CHECK: ftqc.surface_round
  %lq2, %syn = ftqc.surface_round %lq cycles = 3
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>, !ftqc.syndrome<16>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}
