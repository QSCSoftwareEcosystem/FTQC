// RUN: ftqc-opt %s | FileCheck %s
// Test: Distance-5 surface code – larger patch. (test 32)

// CHECK-LABEL: func @surface_d5
func.func @surface_d5() {
  // d=5: 25 data qubits, 24 ancilla. Syndrome width = 2*(25-1) = 48.
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  // CHECK: <<surface, 5>>
  %lq2, %syn = ftqc.surface_round %lq cycles = 5
      : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>, !ftqc.syndrome<48>)
  // CHECK: <48>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  func.return
}
