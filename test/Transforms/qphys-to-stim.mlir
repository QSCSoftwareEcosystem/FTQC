// RUN: ftqc-opt %s --ftqc-expand-physical --ftqc-qphys-to-stim 2>&1 | FileCheck %s
//
// Level-2 STIM back end: a logical Steane memory kernel (init |0_L>, two
// syndrome rounds, measure) is expanded to physical qphys IR and lowered to a
// Stim circuit.  DETECTOR records are reconstructed from the round/stabilizer
// metadata stamped by --ftqc-expand-physical (a stabilizer measured in round r
// and r-1 gives a round-difference detector), and the data measurements XOR
// into the logical OBSERVABLE_INCLUDE.

module {
  func.func @mem() -> i1 {
    %q = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %o1, %s1 = ftqc.ec_round %q rounds = 1
        : <<steane>> -> (<<steane>>, <6>)
    %o2, %s2 = ftqc.ec_round %o1 rounds = 1
        : <<steane>> -> (<<steane>>, <6>)
    %b = ftqc.logical_measure %o2
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    return %b : i1
  }
}

// The |0_L> encoder + syndrome-extraction ancilla circuits.
// CHECK: H 0
// CHECK: CX
// CHECK: M
// A round-difference detector pairs a stabilizer across two rounds
// (six stabilizers per round, so the pair is seven measurement records apart).
// CHECK: DETECTOR rec[-1] rec[-7]
// The logical observable is the parity of the seven data measurements.
// CHECK: OBSERVABLE_INCLUDE(0)
