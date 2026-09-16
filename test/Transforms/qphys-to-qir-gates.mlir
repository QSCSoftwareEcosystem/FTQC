// RUN: ftqc-opt %s --ftqc-qphys-to-qir 2>&1 | FileCheck %s
//
// Gate coverage for the Level-2 QIR back end: fed physical qphys IR directly,
// each qphys gate maps to the corresponding QIR base-profile intrinsic and each
// qphys value keeps its compact QIR qubit index.

module {
  func.func @gates() -> i1 {
    %a = ftqc.qphys.alloc {id = 0 : i64, role = "data"} : !ftqc.qphys_qubit
    %b = ftqc.qphys.alloc {id = 1 : i64, role = "data"} : !ftqc.qphys_qubit
    %h = ftqc.qphys.h %a : !ftqc.qphys_qubit
    %x = ftqc.qphys.x %h : !ftqc.qphys_qubit
    %z = ftqc.qphys.z %x : !ftqc.qphys_qubit
    %s = ftqc.qphys.s %z : !ftqc.qphys_qubit
    %c0, %c1 = ftqc.qphys.cx %s, %b : !ftqc.qphys_qubit
    %z0, %z1 = ftqc.qphys.cz %c0, %c1 : !ftqc.qphys_qubit
    %w0, %w1 = ftqc.qphys.swap %z0, %z1 : !ftqc.qphys_qubit
    %m = ftqc.qphys.measure %w0 : !ftqc.qphys_qubit -> i1
    return %m : i1
  }
}

// CHECK: define void @gates() #0 {
// CHECK: call void @__quantum__qis__h__body(ptr null)
// CHECK: call void @__quantum__qis__x__body(ptr null)
// CHECK: call void @__quantum__qis__z__body(ptr null)
// CHECK: call void @__quantum__qis__s__body(ptr null)
// CHECK: call void @__quantum__qis__cnot__body(ptr null, ptr inttoptr (i64 1 to ptr))
// CHECK: call void @__quantum__qis__cz__body(ptr null, ptr inttoptr (i64 1 to ptr))
// CHECK: call void @__quantum__qis__swap__body(ptr null, ptr inttoptr (i64 1 to ptr))
// CHECK: call void @__quantum__qis__mz__body(ptr null, ptr null)
// CHECK: attributes #0 = {{.*}}"required_num_qubits"="2"
