// Physical (post-pipeline) IR for a single Steane |0_L> prepare-and-measure
// (H^4 = I optimized away); round-trips through ftqc-opt.
// RUN: ftqc-opt %s | FileCheck %s
// CHECK-LABEL: func.func @circuit
// CHECK: ftqc.init_zero
// CHECK: ftqc.logical_measure
module {
  func.func @circuit() -> i1 {
    ftqc.resource_annotation {cnotCount = 0 : i64, ecRounds = 0 : i64, estimatedLogicalErrorRate = 0.000000e+00 : f64, logicalQubits = 1 : i64, physicalQubits = 13 : i64, tGateCount = 0 : i64}
    %0 = ftqc.init_zero : <<steane>>
    %1 = ftqc.logical_measure %0 : (<<steane>>) -> i1
    return %1 : i1
  }
}
