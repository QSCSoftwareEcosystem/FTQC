// RUN: ftqc-opt %s | FileCheck %s
// Test: Lattice surgery merge for surface-code CNOT. (test 39)

// CHECK-LABEL: func @lattice_surgery_xx
func.func @lattice_surgery_xx() {
  %ctrl = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  %tgt  = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  // XX merge implements logical CNOT via lattice surgery.
  // CHECK: ftqc.lattice_surgery
  // CHECK-SAME: kind = 0
  %c2, %t2, %msyn = ftqc.lattice_surgery %ctrl, %tgt kind = 0
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
          !ftqc.syndrome<16>)
  ftqc.dealloc %c2 : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  ftqc.dealloc %t2 : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  func.return
}
