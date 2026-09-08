// RUN: ftqc-opt %s --ftqc-expand-physical --ftqc-qphys-to-qir 2>&1 | FileCheck %s
//
// Level-2 back end: a logical Steane qubit is expanded to physical qphys IR by
// --ftqc-expand-physical (10 physical qubits: 7 data + 3 shared ancilla), then
// lowered to QIR (base profile).  Verifies the QIR emitter consumes the shared
// physical IR.

module {
  func.func @qir_phys() -> i1 {
    %q = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    %m = ftqc.logical_measure %q : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
    return %m : i1
  }
}

// CHECK: define void @qir_phys() #0 {
// CHECK: call void @__quantum__rt__initialize(ptr null)
// The |0_L> encoder: Hadamards on the check qubits then a CNOT cascade.
// CHECK: call void @__quantum__qis__h__body
// CHECK: call void @__quantum__qis__cnot__body
// Seven physical data-qubit measurements.
// CHECK: call void @__quantum__qis__mz__body
// CHECK: ret void
// CHECK: attributes #0 = {{.*}}"required_num_qubits"="10"
// CHECK: qir_major_version
