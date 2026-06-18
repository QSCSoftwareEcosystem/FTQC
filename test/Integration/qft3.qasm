OPENQASM 3.0;
include "stdgates.inc";

// 3-qubit QFT (Clifford+T decomposition, no Tdg)
// q[0] = MSB, q[2] = LSB
qubit[3] q;

// QFT on q[0]
h q[0];
t q[0];
cx q[1], q[0];
t q[0];
cx q[1], q[0];
t q[0];
cx q[2], q[0];
t q[0];
cx q[2], q[0];

// QFT on q[1]
h q[1];
t q[1];
cx q[2], q[1];
t q[1];
cx q[2], q[1];

// QFT on q[2]
h q[2];

// SWAP q[0] <-> q[2] (3 CNOTs)
cx q[0], q[2];
cx q[2], q[0];
cx q[0], q[2];
