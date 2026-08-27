// RUN: ftqc-opt %s | FileCheck %s
// Test: Basic FTQC type round-trip through parser/printer.

// CHECK-LABEL: func @test_basic_types
func.func @test_basic_types() {
  // CHECK: <<steane>>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  // CHECK: <6>
  %lq2, %syn = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)

  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
