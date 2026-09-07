// RUN: ftqc-opt -split-input-file %s --ftqc-resource-estimate | FileCheck %s
//
// Benchmark A9 — MULTI-CODE logical MEMORY at matched task, varying the ECC.
//
// The SAME logical-memory kernel (prepare |0_L>, one fault-tolerant syndrome
// extraction round, measure) is written four times, changing ONLY the
// `#ftqc.ecc<K,d>` type parameter.  No source below the type annotation
// changes: the ECC family and distance live in the *type* of the logical qubit,
// so the resource estimator derives a different physical-qubit footprint for
// each code directly from the IR.  This is the concrete payoff of the
// ECC-parameterized (Level-1) type system, and it exercises the surface and
// 2D color code families beyond the Steane baseline.
//
//   code                | physical qubits / logical qubit
//   Steane   [[7,1,3]]  | 13   (7 data + 6 ancilla)
//   surface  d = 3      | 17   (9 data + 8 ancilla)
//   surface  d = 5      | 49   (25 data + 24 ancilla)
//   2D color d = 3      | 11   (7 data + 4 ancilla)

// CHECK: physicalQubits = 13
func.func @memory_steane_d3() -> i1 {
  %q = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %o, %s = ftqc.extract_syndrome %q
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  %b = ftqc.logical_measure %o : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  return %b : i1
}

// -----

// CHECK: physicalQubits = 17
func.func @memory_surface_d3() -> i1 {
  %q = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  %o, %s = ftqc.extract_syndrome %q
      : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>, !ftqc.syndrome<16>)
  %b = ftqc.logical_measure %o : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>) -> i1
  return %b : i1
}

// -----

// CHECK: physicalQubits = 49
func.func @memory_surface_d5() -> i1 {
  %q = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  %o, %s = ftqc.extract_syndrome %q
      : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>, !ftqc.syndrome<48>)
  %b = ftqc.logical_measure %o : (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>) -> i1
  return %b : i1
}

// -----

// CHECK: physicalQubits = 11
func.func @memory_color_d3() -> i1 {
  %q = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
  %o, %s = ftqc.extract_syndrome %q
      : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>, !ftqc.syndrome<6>)
  %b = ftqc.logical_measure %o : (!ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>) -> i1
  return %b : i1
}
