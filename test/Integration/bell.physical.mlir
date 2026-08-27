module {
  func.func @circuit() -> (i1, i1) {
    ftqc.resource_annotation {cnotCount = 1 : i64, ecRounds = 0 : i64, estimatedLogicalErrorRate = 0.000000e+00 : f64, logicalQubits = 2 : i64, physicalQubits = 26 : i64, tGateCount = 0 : i64}
    %0 = ftqc.init_zero : <<steane>>
    %1 = ftqc.init_zero : <<steane>>
    %2 = ftqc.logical_h %0 : <<steane>>
    %correctedQubit, %syndrome = ftqc.ec_round %2 rounds = 3 : <<steane>> -> (<<steane>>, <6>)
    %controlOut, %targetOut = ftqc.logical_cnot %2, %1 : (<<steane>>, <<steane>>) -> (<<steane>>, <<steane>>)
    %correctedQubit_0, %syndrome_1 = ftqc.ec_round %controlOut rounds = 3 : <<steane>> -> (<<steane>>, <6>)
    %correctedQubit_2, %syndrome_3 = ftqc.ec_round %targetOut rounds = 3 : <<steane>> -> (<<steane>>, <6>)
    %3 = ftqc.logical_measure %controlOut : (<<steane>>) -> i1
    %4 = ftqc.logical_measure %targetOut : (<<steane>>) -> i1
    return %3, %4 : i1, i1
  }
}
