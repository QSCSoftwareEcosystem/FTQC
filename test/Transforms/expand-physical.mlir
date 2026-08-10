// RUN: ftqc-opt --ftqc-expand-physical %s | FileCheck %s
// RUN: ftqc-opt --ftqc-expand-physical --ftqc-qphys-to-qasm3 %s | FileCheck %s --check-prefix=QASM
// RUN: ftqc-opt --ftqc-expand-physical --ftqc-qphys-to-iqm-json %s | FileCheck %s --check-prefix=IQM
//
// --ftqc-expand-physical unfolds each Steane [[7,1,3]] logical qubit into
// explicit physical qphys data + ancilla qubits (roles "data"/"x_anc"/"z_anc"),
// and --ftqc-qphys-to-qasm3 lowers that shared physical IR to OpenQASM 3.0.

module {
  func.func @hm() -> i1 {
    %0 = ftqc.init_zero : <<steane, 3>>
    %1 = ftqc.logical_h %0 : <<steane, 3>>
    %o, %xs = ftqc.steane_x_syndrome %1 : <<steane, 3>> -> (<<steane, 3>>, <6>)
    %o2, %zs = ftqc.steane_z_syndrome %o : <<steane, 3>> -> (<<steane, 3>>, <6>)
    %2 = ftqc.logical_measure %o2 : (<<steane, 3>>) -> i1
    return %2 : i1
  }
}

// The logical layer is fully lowered — no logical/syndrome ops survive.
// CHECK-LABEL: func.func @hm
// CHECK-NOT: ftqc.logical_
// CHECK-NOT: ftqc.steane_

// Data qubits plus X- and Z-syndrome ancillas are materialized as SSA values,
// and physical gates + measurement are emitted (order-independent).
// CHECK-DAG: ftqc.qphys.alloc {id = 0 : i64, role = "data"}
// CHECK-DAG: role = "x_anc"
// CHECK-DAG: role = "z_anc"
// CHECK-DAG: ftqc.qphys.h
// CHECK-DAG: ftqc.qphys.cx
// CHECK-DAG: ftqc.qphys.measure

// Shared physical IR lowers to physical-qubit QASM.
// QASM: OPENQASM 3.0;
// QASM: qubit q0;
// QASM: h q0;
// QASM: measure q

// The same physical IR retargets to IQM JSON: data QB1..QB7 + ancillas QB8..,
// physical gates as IQM native {prx, cz, measure}.
// IQM: "name":"hm"
// IQM-DAG: "name":"prx"
// IQM-DAG: "name":"cz"
// IQM-DAG: "name":"measure"
// IQM-DAG: "locus":["QB1"]
// IQM-DAG: "locus":["QB8"]
