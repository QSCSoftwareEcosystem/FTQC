// RUN: ftqc-opt %s | FileCheck %s
// Test: Z stabilizer (vertex) measurements for surface code. (test 34)

// CHECK-LABEL: func @z_vertex_measure
func.func @z_vertex_measure() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  // Z stabilizers measured using ancilla and CZ → ancilla measurement.
  %lq2, %zsyn = ftqc.steane_z_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>, !ftqc.syndrome<4>)
  // CHECK: <4>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}
