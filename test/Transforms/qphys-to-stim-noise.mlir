// RUN: ftqc-opt %s --ftqc-expand-physical \
// RUN:   --ftqc-qphys-to-stim="physical-error-rate=0.001" 2>&1 | FileCheck %s
//
// The physical-error-rate option adds a depolarizing noise model: DEPOLARIZE1
// after single-qubit gates, DEPOLARIZE2 after two-qubit gates, and X_ERROR
// before measurements.  A two-round memory kernel also yields round-difference
// detectors and a logical observable.

func.func @mem() -> i1 {
  %q = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %o1, %s1 = ftqc.ec_round %q rounds = 1 : <<steane>> -> (<<steane>>, <6>)
  %o2, %s2 = ftqc.ec_round %o1 rounds = 1 : <<steane>> -> (<<steane>>, <6>)
  %b = ftqc.logical_measure %o2 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  return %b : i1
}

// CHECK-DAG: DEPOLARIZE1(1.000000e-03)
// CHECK-DAG: DEPOLARIZE2(1.000000e-03)
// CHECK-DAG: X_ERROR(1.000000e-03)
// CHECK-DAG: DETECTOR rec[-1] rec[-7]
// CHECK-DAG: OBSERVABLE_INCLUDE(0)
