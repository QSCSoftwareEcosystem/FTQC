OPENQASM 3.0;
include "stdgates.inc";

// 2-bit ripple-carry adder using Clifford+T decomposition.
// Inputs: a[0..1], b[0..1]; carry-out: cout.
qubit[2] a;
qubit[2] b;
qubit[1] cout;
bit[3]   result;

// Full-adder for bit 0: sum = a[0] XOR b[0]; carry = a[0] AND b[0]
// Toffoli for carry: ccx a[0], b[0], cout[0]
ccx a[0], b[0], cout[0];
cx  a[0], b[0];

// Full-adder for bit 1: also accounts for carry-in (cout[0])
ccx a[1], b[1], cout[0];
cx  a[1], b[1];
ccx b[1], cout[0], b[0];

// Measure outputs
result[0] = measure b[0];
result[1] = measure b[1];
result[2] = measure cout[0];
