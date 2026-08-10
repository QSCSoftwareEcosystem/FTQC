// RUN: ftqc-opt %s --ftqc-qphys-init-layout="input-file=%S/qphys-init-layout.json" | FileCheck %s --check-prefix=MAPPED
// RUN: ftqc-opt %s --ftqc-qphys-init-layout | FileCheck %s --check-prefix=UNCHANGED
// RUN: not ftqc-opt %s --ftqc-qphys-init-layout="input-file=%S/does-not-exist.json" 2>&1 | FileCheck %s --check-prefix=NOT-FOUND
// RUN: not ftqc-opt %s --ftqc-qphys-init-layout="input-file=%S/qphys-init-layout-missing.json" 2>&1 | FileCheck %s --check-prefix=MISSING
module {
  func.func @physical_circuit() {
    %0 = ftqc.qphys.alloc {id = 0 : i64, role = "data"} : !ftqc.qphys_qubit
    %1 = ftqc.qphys.alloc {id = 1 : i64, role = "data"} : !ftqc.qphys_qubit
    %2 = ftqc.qphys.alloc {id = 2 : i64, role = "x_anc"} : !ftqc.qphys_qubit
    %3 = ftqc.qphys.alloc {id = 3 : i64, role = "z_anc"} : !ftqc.qphys_qubit
    %4 = ftqc.qphys.alloc {id = 4 : i64, role = "data"} : !ftqc.qphys_qubit
    return
  }
}
// MAPPED: ftqc.qphys.alloc {id = 5 : i64, role = "data"}
// MAPPED: ftqc.qphys.alloc {id = 1 : i64, role = "data"}
// MAPPED: ftqc.qphys.alloc {id = 7 : i64, role = "x_anc"}
// MAPPED: ftqc.qphys.alloc {id = 2 : i64, role = "z_anc"}
// MAPPED: ftqc.qphys.alloc {id = 6 : i64, role = "data"}
// UNCHANGED: ftqc.qphys.alloc {id = 1 : i64, role = "data"}
// NOT-FOUND: error: ftqc-qphys-init-layout: cannot open input file
// MISSING: error: ftqc-qphys-init-layout: missing or non-integer mapping for logical qubit id 1
