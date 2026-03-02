// RUN: ftqc-opt %s | FileCheck %s
// Test: Basic FTQC type round-trip through parser/printer.

// CHECK-LABEL: func @test_basic_types
func.func @test_basic_types() {
  // CHECK: <<steane, 3>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // CHECK: <6>
  %lq2, %syn = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)

  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}
