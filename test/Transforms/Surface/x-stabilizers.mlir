// RUN: ftqc-opt %s | FileCheck %s
// Test: X stabilizer (plaquette) measurements for surface code. (test 33)

// CHECK-LABEL: func @x_plaquette_measure
func.func @x_plaquette_measure() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  // X stabilizers measured using ancilla and CNOT → ancilla measurement.
  %lq2, %xsyn = ftqc.steane_x_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>, !ftqc.syndrome<4>)
  // CHECK: <4>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}
