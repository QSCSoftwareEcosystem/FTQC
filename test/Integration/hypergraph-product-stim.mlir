// RUN: ftqc-opt %s --ftqc-expand-physical-generic --ftqc-qphys-to-stim 2>&1 | FileCheck %s

// X syndrome
// CHECK: H 19
// CHECK: CX 19 0
// CHECK: CX 19 3
// CHECK: CX 19 9
// CHECK: H 19
// CHECK: M 19
// CHECK: H 20
// CHECK: CX 20 1
// CHECK: CX 20 4
// CHECK: CX 20 9
// CHECK: CX 20 10
// CHECK: H 20
// CHECK: M 20
// CHECK: H 21
// CHECK: CX 21 2
// CHECK: CX 21 5
// CHECK: CX 21 10
// CHECK: H 21
// CHECK: M 21
// CHECK: H 22
// CHECK: CX 22 3
// CHECK: CX 22 6
// CHECK: CX 22 11
// CHECK: H 22
// CHECK: M 22
// CHECK: H 23
// CHECK: CX 23 4
// CHECK: CX 23 7
// CHECK: CX 23 11
// CHECK: CX 23 12
// CHECK: H 23
// CHECK: M 23
// CHECK: H 24
// CHECK: CX 24 5
// CHECK: CX 24 8
// CHECK: CX 24 12
// CHECK: H 24
// CHECK: M 24

// Z syndrome
// CHECK: CX 0 25
// CHECK: CX 1 25
// CHECK: CX 9 25
// CHECK: M 25
// CHECK: CX 1 26
// CHECK: CX 2 26
// CHECK: CX 10 26
// CHECK: M 26
// CHECK: CX 3 27
// CHECK: CX 4 27
// CHECK: CX 9 27
// CHECK: CX 11 27
// CHECK: M 27
// CHECK: CX 4 28
// CHECK: CX 5 28
// CHECK: CX 10 28
// CHECK: CX 12 28
// CHECK: M 28
// CHECK: CX 6 29
// CHECK: CX 7 29
// CHECK: CX 11 29
// CHECK: M 29
// CHECK: CX 7 30
// CHECK: CX 8 30
// CHECK: CX 12 30
// CHECK: M 30

// COM: CHECK: DETECTOR(0) rec[-12]
// COM: CHECK: DETECTOR(1) rec[-11]
// COM: CHECK: DETECTOR(2) rec[-10]
// COM: CHECK: DETECTOR(3) rec[-9]
// COM: CHECK: DETECTOR(4) rec[-8]
// COM: CHECK: DETECTOR(5) rec[-7]
// COM: CHECK: DETECTOR(6) rec[-6]
// COM: CHECK: DETECTOR(7) rec[-5]
// COM: CHECK: DETECTOR(8) rec[-4]
// COM: CHECK: DETECTOR(9) rec[-3]
// COM: CHECK: DETECTOR(10) rec[-2]
// COM: CHECK: DETECTOR(11) rec[-1]

#repetition3 = #ftqc.classical_code<3, [1,1,0, 0,1,1]>
#surface3 = #ftqc.ecc<hypergraph_product, code1 = #repetition3, code2 = #repetition3>

func.func @hypergraph_product_memory() -> i1 {
  %lq0 = ftqc.init_zero : <#surface3>
  %lq1, %syn = ftqc.extract_syndrome %lq0 : <#surface3> -> (<#surface3>, <12>)
  %b0 = ftqc.logical_measure %lq1 : (<#surface3>) -> i1
  func.return %b0 : i1
}
