// RUN: ftqc-opt %s --ftqc-lower-to-stim='syndrome-rounds=0 physical-error-rate=0' 2>&1 | FileCheck %s

// X syndrome
// CHECK: R 13 14 15 16 17 18
// CHECK: H 13 14 15 16 17 18
// CHECK: CX 13 0 13 3 13 9
// CHECK: CX 14 1 14 4 14 9 14 10
// CHECK: CX 15 2 15 5 15 10
// CHECK: CX 16 3 16 6 16 11
// CHECK: CX 17 4 17 7 17 11 17 12
// CHECK: CX 18 5 18 8 18 12
// CHECK: H 13 14 15 16 17 18
// CHECK: M 13 14 15 16 17 18

// Z syndrome
// CHECK: R 19 20 21 22 23 24
// CHECK: CX 0 19 1 19 9 19
// CHECK: CX 1 20 2 20 10 20
// CHECK: CX 3 21 4 21 9 21 11 21
// CHECK: CX 4 22 5 22 10 22 12 22
// CHECK: CX 6 23 7 23 11 23
// CHECK: CX 7 24 8 24 12 24
// CHECK: M 19 20 21 22 23 24

// CHECK: DETECTOR(0) rec[-12]
// CHECK: DETECTOR(1) rec[-11]
// CHECK: DETECTOR(2) rec[-10]
// CHECK: DETECTOR(3) rec[-9]
// CHECK: DETECTOR(4) rec[-8]
// CHECK: DETECTOR(5) rec[-7]
// CHECK: DETECTOR(6) rec[-6]
// CHECK: DETECTOR(7) rec[-5]
// CHECK: DETECTOR(8) rec[-4]
// CHECK: DETECTOR(9) rec[-3]
// CHECK: DETECTOR(10) rec[-2]
// CHECK: DETECTOR(11) rec[-1]

func.func @hypergraph_product_memory() -> i1 {
  %lq0 = ftqc.init_zero : <<hypergraph_product, 3>>
  %lq1, %syn = ftqc.extract_syndrome %lq0 : <<hypergraph_product, 3>> -> (<<hypergraph_product, 3>>, <12>)
  %b0 = ftqc.logical_measure %lq1 : (<<hypergraph_product, 3>>) -> i1
  func.return %b0 : i1
}
