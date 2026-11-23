// RUN: ftqc-opt %s --ftqc-canonicalize-to-lightstim-atomic="default-se-rounds=2 surgery-rounds=4" 2>&1 | FileCheck %s

// CHECK: "format": "ftqc-lightstim-atomic-ir"
// CHECK: "version": 2
// CHECK: "op": "initialize"
// CHECK: "op": "unitary_block"
// CHECK: "op": "syndrome_extraction"
// CHECK: "op": "activate_coupler"
// CHECK: "interaction": "ZZ"
// CHECK: "op": "deactivate_coupler"
// CHECK: "op": "data_readout"

func.func @atomic_debug_example() {
  %q0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  %q1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>

  %q0h = ftqc.logical_h %q0 : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>

    %q0se, %syn0 = ftqc.extract_syndrome %q0h {rounds = 3 : i32}
      : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>, !ftqc.syndrome<8>)

  %r0, %r1, %lsSyn = ftqc.lattice_surgery %q0se, %q1 kind = 1
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>,
          !ftqc.syndrome<16>)

    %m0 = ftqc.logical_measure %r0 {basis = 1 : i32}
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>) -> i1
  %m1 = ftqc.logical_measure %r1
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>) -> i1
  func.return
}
