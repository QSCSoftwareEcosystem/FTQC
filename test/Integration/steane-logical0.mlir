// A single Steane [[7,1,3]] logical qubit prepared in |0_L> and measured.
//   --ftqc-lower-to-iqm-json=physical=true  expands it to a 7-qubit circuit.
module {
  func.func @steane_logical0() -> i1 {
    %q = ftqc.init_zero : <<steane>>
    %m = ftqc.logical_measure %q : (<<steane>>) -> i1
    return %m : i1
  }
}
