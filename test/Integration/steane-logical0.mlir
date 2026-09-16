// A single Steane [[7,1,3]] logical qubit prepared in |0_L> and measured.
//   --ftqc-lower-to-iqm-json=physical=true  expands it to a 7-qubit circuit.
// RUN: ftqc-opt %s --ftqc-lower-to-iqm-json="physical=true" 2>&1 | FileCheck %s
// The physical expansion emits IQM-native gates on QB loci.
// CHECK: QB1
// CHECK: prx
module {
  func.func @steane_logical0() -> i1 {
    %q = ftqc.init_zero : <<steane>>
    %m = ftqc.logical_measure %q : (<<steane>>) -> i1
    return %m : i1
  }
}
