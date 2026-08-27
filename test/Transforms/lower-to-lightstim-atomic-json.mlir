// RUN: ftqc-opt %s --ftqc-lower-to-lightstim-atomic-json="physical-error-rate=0.002 surgery-rounds=6 max-errors=77 max-shots=12345" 2>&1 | FileCheck %s
// RUN: not ftqc-opt %s --ftqc-lower-to-lightstim-atomic-json="surgery-rounds=0" 2>&1 | FileCheck %s --check-prefix=ZERO

// CHECK: "format": "ftqc-lightstim-atomic-experiment"
// CHECK: "version": 2
// CHECK: "type": "atomic_timeline"
// CHECK: "timeline"
// CHECK: "op": "syndrome_extraction"
// CHECK: "rounds": 1
// CHECK: "op": "activate_coupler"
// CHECK: "op": "syndrome_extraction"
// CHECK: "rounds": 6
// CHECK: "noise"
// CHECK: "p_1q": 2.000000e-03
// CHECK: "decoder"
// CHECK: "simulation"
// CHECK: "max_errors": 77
// CHECK: "max_shots": 12345
// ZERO: surgery-rounds must be >= 1

func.func @atomic_descriptor_example() {
  %q0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  %q1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>

  %r0, %r1, %syn = ftqc.lattice_surgery %q0, %q1 kind = 0
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
          !ftqc.syndrome<16>)

  %m0 = ftqc.logical_measure %r0
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>) -> i1
    %m1 = ftqc.logical_measure %r1 {basis = 1 : i32}
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>) -> i1

  func.return
}
