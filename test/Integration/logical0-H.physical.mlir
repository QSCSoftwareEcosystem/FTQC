module {
  func.func @circuit() -> i1 {
    ftqc.resource_annotation {cnotCount = 0 : i64, ecRounds = 0 : i64, estimatedLogicalErrorRate = 0.000000e+00 : f64, logicalQubits = 1 : i64, physicalQubits = 13 : i64, tGateCount = 0 : i64}
    %0 = ftqc.init_zero : <#ftqc.ecc<steane, 3>>
    %1 = ftqc.logical_measure %0 : (<#ftqc.ecc<steane, 3>>) -> i1
    return %1 : i1
  }
}
