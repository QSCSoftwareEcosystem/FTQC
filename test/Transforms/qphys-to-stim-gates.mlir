// RUN: ftqc-opt %s --ftqc-qphys-to-stim 2>&1 | FileCheck %s
//
// Gate coverage for the Level-2 Stim back end: each qphys gate maps to the
// corresponding Stim instruction on the compact physical qubit indices.

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

// CHECK: H 0
// CHECK: X 0
// CHECK: Z 0
// CHECK: S 0
// CHECK: CX 0 1
// CHECK: CZ 0 1
// CHECK: SWAP 0 1
// CHECK: M 0
