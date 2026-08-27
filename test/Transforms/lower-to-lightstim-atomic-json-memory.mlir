// RUN: ftqc-opt %s --ftqc-lower-to-lightstim-atomic-json="default-se-rounds=2" 2>&1 | FileCheck %s
// RUN: not ftqc-opt %s --ftqc-lower-to-lightstim-atomic-json="default-se-rounds=0" 2>&1 | FileCheck %s --check-prefix=ZERO

// CHECK: "op": "initialize"
// CHECK: "op": "unitary_block"
// CHECK: "gate": "logical_h"
// CHECK: "op": "syndrome_extraction"
// CHECK: "rounds": 2
// CHECK: "op": "data_readout"
// ZERO: default-se-rounds must be >= 1

func.func @atomic_memory_example() {
  %q0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  %h = ftqc.logical_h %q0 : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  %m = ftqc.logical_measure %h {basis = 1 : i32}
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>) -> i1
  func.return
}
