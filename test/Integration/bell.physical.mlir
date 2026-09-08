// Physical (post-pipeline) IR for a 2-logical Steane Bell circuit; round-trips
// through ftqc-opt (parse/print stability of the logical op set).
// RUN: ftqc-opt %s | FileCheck %s
// CHECK-LABEL: func.func @circuit
// CHECK: ftqc.init_zero
// CHECK: ftqc.logical_h
// CHECK: ftqc.ec_round
// CHECK: ftqc.logical_cnot
// CHECK: ftqc.logical_measure
module {
  func.func @circuit() -> (i1, i1) {
    ftqc.resource_annotation {cnotCount = 1 : i64, ecRounds = 0 : i64, estimatedLogicalErrorRate = 0.000000e+00 : f64, logicalQubits = 2 : i64, physicalQubits = 26 : i64, tGateCount = 0 : i64}
    %0 = ftqc.init_zero : <<steane, 3>>
    %1 = ftqc.init_zero : <<steane, 3>>
    %2 = ftqc.logical_h %0 : <<steane, 3>>
    %correctedQubit, %syndrome = ftqc.ec_round %2 rounds = 3 : <<steane, 3>> -> (<<steane, 3>>, <6>)
    %controlOut, %targetOut = ftqc.logical_cnot %2, %1 : (<<steane, 3>>, <<steane, 3>>) -> (<<steane, 3>>, <<steane, 3>>)
    %correctedQubit_0, %syndrome_1 = ftqc.ec_round %controlOut rounds = 3 : <<steane, 3>> -> (<<steane, 3>>, <6>)
    %correctedQubit_2, %syndrome_3 = ftqc.ec_round %targetOut rounds = 3 : <<steane, 3>> -> (<<steane, 3>>, <6>)
    %3 = ftqc.logical_measure %controlOut : (<<steane, 3>>) -> i1
    %4 = ftqc.logical_measure %targetOut : (<<steane, 3>>) -> i1
    return %3, %4 : i1, i1
  }
}
