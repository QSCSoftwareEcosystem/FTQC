// RUN: ftqc-opt %s --ftqc-expand-physical-generic --ftqc-qphys-to-stim 2>&1 | FileCheck %s

// Initialization
// CHECK: H 13
// CHECK: CX 13 0
// CHECK: CX 13 3
// CHECK: CX 13 9
// CHECK: H 13
// CHECK: M 13
// CHECK: H 14
// CHECK: CX 14 1
// CHECK: CX 14 4
// CHECK: CX 14 9
// CHECK: CX 14 10
// CHECK: H 14
// CHECK: M 14
// CHECK: H 15
// CHECK: CX 15 2
// CHECK: CX 15 5
// CHECK: CX 15 10
// CHECK: H 15
// CHECK: M 15
// CHECK: H 16
// CHECK: CX 16 3
// CHECK: CX 16 6
// CHECK: CX 16 11
// CHECK: H 16
// CHECK: M 16
// CHECK: H 17
// CHECK: CX 17 4
// CHECK: CX 17 7
// CHECK: CX 17 11
// CHECK: CX 17 12
// CHECK: H 17
// CHECK: M 17
// CHECK: H 18
// CHECK: CX 18 5
// CHECK: CX 18 8
// CHECK: CX 18 12
// CHECK: H 18
// CHECK: M 18

// X syndrome
// CHECK: H 19
// CHECK: CX 19 0
// CHECK: CX 19 3
// CHECK: CX 19 9
// CHECK: H 19
// CHECK: M 19
// CHECK: DETECTOR rec[-7] rec[-1]
// CHECK: H 20
// CHECK: CX 20 1
// CHECK: CX 20 4
// CHECK: CX 20 9
// CHECK: CX 20 10
// CHECK: H 20
// CHECK: M 20
// CHECK: DETECTOR rec[-7] rec[-1]
// CHECK: H 21
// CHECK: CX 21 2
// CHECK: CX 21 5
// CHECK: CX 21 10
// CHECK: H 21
// CHECK: M 21
// CHECK: DETECTOR rec[-7] rec[-1]
// CHECK: H 22
// CHECK: CX 22 3
// CHECK: CX 22 6
// CHECK: CX 22 11
// CHECK: H 22
// CHECK: M 22
// CHECK: DETECTOR rec[-7] rec[-1]
// CHECK: H 23
// CHECK: CX 23 4
// CHECK: CX 23 7
// CHECK: CX 23 11
// CHECK: CX 23 12
// CHECK: H 23
// CHECK: M 23
// CHECK: DETECTOR rec[-7] rec[-1]
// CHECK: H 24
// CHECK: CX 24 5
// CHECK: CX 24 8
// CHECK: CX 24 12
// CHECK: H 24
// CHECK: M 24
// CHECK: DETECTOR rec[-7] rec[-1]

// Z syndrome
// CHECK: CX 0 25
// CHECK: CX 1 25
// CHECK: CX 9 25
// CHECK: M 25
// CHECK: DETECTOR rec[-1]
// CHECK: CX 1 26
// CHECK: CX 2 26
// CHECK: CX 10 26
// CHECK: M 26
// CHECK: DETECTOR rec[-1]
// CHECK: CX 3 27
// CHECK: CX 4 27
// CHECK: CX 9 27
// CHECK: CX 11 27
// CHECK: M 27
// CHECK: DETECTOR rec[-1]
// CHECK: CX 4 28
// CHECK: CX 5 28
// CHECK: CX 10 28
// CHECK: CX 12 28
// CHECK: M 28
// CHECK: DETECTOR rec[-1]
// CHECK: CX 6 29
// CHECK: CX 7 29
// CHECK: CX 11 29
// CHECK: M 29
// CHECK: DETECTOR rec[-1]
// CHECK: CX 7 30
// CHECK: CX 8 30
// CHECK: CX 12 30
// CHECK: M 30
// CHECK: DETECTOR rec[-1]

// Measurement
// CHECK: M 0
// CHECK: M 1
// CHECK: M 2
// CHECK: M 3
// CHECK: M 4
// CHECK: M 5
// CHECK: M 6
// CHECK: M 7
// CHECK: M 8
// CHECK: M 9
// CHECK: M 10
// CHECK: M 11
// CHECK: M 12
// CHECK: DETECTOR rec[-19] rec[-13] rec[-12] rec[-4]
// CHECK: DETECTOR rec[-18] rec[-12] rec[-11] rec[-3]
// CHECK: DETECTOR rec[-17] rec[-10] rec[-9] rec[-4] rec[-2]
// CHECK: DETECTOR rec[-16] rec[-9] rec[-8] rec[-3] rec[-1]
// CHECK: DETECTOR rec[-15] rec[-7] rec[-6] rec[-2]
// CHECK: DETECTOR rec[-14] rec[-6] rec[-5] rec[-1]
// CHECK: OBSERVABLE_INCLUDE(0) rec[-11] rec[-8] rec[-5]

#repetition3 = #ftqc.classical_code<3, [1,1,0, 0,1,1]>
#surface3 = #ftqc.ecc<hypergraph_product, code1 = #repetition3, code2 = #repetition3>

func.func @hypergraph_product_memory() -> i1 {
  %lq0 = ftqc.init_zero : <#surface3>
  %lq1, %syn = ftqc.extract_syndrome %lq0 : <#surface3> -> (<#surface3>, <12>)
  %b0 = ftqc.logical_measure %lq1 : (<#surface3>) -> i1
  func.return %b0 : i1
}
