// RUN: ftqc-opt -split-input-file %s | FileCheck %s
// RUN: ftqc-opt -split-input-file %s --ftqc-resource-estimate | FileCheck %s --check-prefix=RES
// RUN: ftqc-opt %s --ftqc-lower-to-lattice-surgery | FileCheck %s --check-prefix=LOWER
//
// Benchmark A7 — Logical CNOT via LATTICE SURGERY (surface code merge/split).
//
// Two surface-code logical qubits on separate 2D patches cannot overlap for a
// transversal CNOT; the entangling gate is realized instead by an XX merge
// (kind = 0), driven by the `ftqc.lattice_surgery` op and the
// `--ftqc-lower-to-lattice-surgery` back end.  This exercises the routing /
// lattice-surgery path (Level 1 logical IR) that the direct transversal back
// ends do not use.  d = 3 and d = 5 patches show the code-distance dependence of
// the merge-syndrome width (2(d^2-1) = 16 for d=3, 48 for d=5) and of the
// physical-qubit footprint reported by the estimator.

// CHECK-LABEL: func @cnot_via_merge_d3
func.func @cnot_via_merge_d3() {
  %ctrl = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  %tgt  = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>

  // XX merge implements the logical CNOT between two separated patches.
  // CHECK: ftqc.lattice_surgery
  // CHECK-SAME: kind = 0
  %c2, %t2, %msyn = ftqc.lattice_surgery %ctrl, %tgt kind = 0
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>,
          !ftqc.syndrome<16>)

  ftqc.dealloc %c2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  ftqc.dealloc %t2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  return
}

// -----

// CHECK-LABEL: func @cnot_via_merge_d5
func.func @cnot_via_merge_d5() {
  %ctrl = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %tgt  = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>

  // CHECK: ftqc.lattice_surgery
  // CHECK-SAME: kind = 0
  // Merge-syndrome width scales as 2(d^2-1) = 48 for d = 5.
  %c2, %t2, %msyn = ftqc.lattice_surgery %ctrl, %tgt kind = 0
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
          !ftqc.syndrome<48>)

  ftqc.dealloc %c2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  ftqc.dealloc %t2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  return
}

// The resource estimator accounts for both surface patches per distance
// (d=3: 2 x 17 = 34; d=5: 2 x 49 = 98).
// RES: physicalQubits = 34
// RES: physicalQubits = 98

// The lattice-surgery back end emits a stabilizer circuit for each merge.
// LOWER: function @cnot_via_merge_d3
// LOWER: function @cnot_via_merge_d5
