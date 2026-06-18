// RUN: ftqc-opt --ftqc-lower-to-iqm-json %s 2>&1 | FileCheck %s
//
// Verify that --ftqc-lower-to-iqm-json emits correct IQM JSON for a Bell-state
// circuit and attaches the result as the module attribute `ftqc.iqm_json`.
//
// Gate decompositions exercised here:
//   init_zero     → no instruction (|0⟩ default)
//   logical_h     → prx(0.5,0.25) + prx(1.0,0.0)
//   logical_cnot  → H(target) + cz[ctrl,tgt] + H(target)
//   logical_measure → measure with key "mQBn_idx"

// CHECK: "name":"bell_state_ft"

// H on QB1 = 2 prx:
// CHECK: {"name":"prx","locus":["QB1"],"args":{"angle_t":0.5,"phase_t":0.25}}
// CHECK: {"name":"prx","locus":["QB1"],"args":{"angle_t":1,"phase_t":0}}

// CNOT(QB1→QB2) = H(QB2) + CZ + H(QB2):
// CHECK: {"name":"prx","locus":["QB2"],"args":{"angle_t":0.5,"phase_t":0.25}}
// CHECK: {"name":"prx","locus":["QB2"],"args":{"angle_t":1,"phase_t":0}}
// CHECK: {"name":"cz","locus":["QB1","QB2"],"args":{}}
// CHECK: {"name":"prx","locus":["QB2"],"args":{"angle_t":0.5,"phase_t":0.25}}
// CHECK: {"name":"prx","locus":["QB2"],"args":{"angle_t":1,"phase_t":0}}

// Measurements:
// CHECK: {"name":"measure","locus":["QB1"],"args":{"key":"mQB1_0"}}
// CHECK: {"name":"measure","locus":["QB2"],"args":{"key":"mQB2_1"}}

// Module attribute set:
// CHECK: ftqc.iqm_json

module {
  func.func @bell_state_ft() -> (i1, i1) {
    %q0 = ftqc.init_zero : <<steane, 3>>
    %q1 = ftqc.init_zero : <<steane, 3>>
    %q0h = ftqc.logical_h %q0 : <<steane, 3>>
    %c, %t = ftqc.logical_cnot %q0h, %q1 : (<<steane, 3>>, <<steane, 3>>) -> (<<steane, 3>>, <<steane, 3>>)
    %m0 = ftqc.logical_measure %c : (<<steane, 3>>) -> i1
    %m1 = ftqc.logical_measure %t : (<<steane, 3>>) -> i1
    return %m0, %m1 : i1, i1
  }
}
