//===- PauliUtils.cpp - Pauli group and Clifford tableau ------------------===//
#include "FTQC/Utils/PauliUtils.h"
#include "FTQC/Utils/StabilizerUtils.h"

#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <sstream>

using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// Single-qubit Pauli algebra
//===----------------------------------------------------------------------===//

// Multiplication table for Paulis (without phase):
//   I·X=X, I·Y=Y, I·Z=Z
//   X·X=I, X·Y=iZ(→Z), X·Z=-iY(→Y)
//   Y·Y=I, Y·Z=iX(→X), Z·Z=I
// We return (result, phase) where phase ∈ {0,1,2,3} = {1,i,-1,-i}.
std::pair<Pauli, int> mlir::ftqc::pauliProduct(Pauli a, Pauli b) {
  // Use the lookup table: rows/cols = I,X,Y,Z (0,1,2,3)
  static const uint8_t prodTable[4][4] = {
    // a\b  I  X  Y  Z
    /*I*/  {0, 1, 2, 3},
    /*X*/  {1, 0, 3, 2},
    /*Y*/  {2, 3, 0, 1},
    /*Z*/  {3, 2, 1, 0}
  };
  static const int phaseTable[4][4] = {
    // a\b  I  X  Y  Z
    /*I*/  {0, 0, 0, 0},
    /*X*/  {0, 0, 1, 3},  // XY=iZ (phase=1), XZ=-iY (phase=3)
    /*Y*/  {0, 3, 0, 1},  // YX=-iZ (phase=3), YZ=iX (phase=1)
    /*Z*/  {0, 1, 3, 0},  // ZX=iY (phase=1), ZY=-iX (phase=3)
  };
  uint8_t ai = static_cast<uint8_t>(a), bi = static_cast<uint8_t>(b);
  return {static_cast<Pauli>(prodTable[ai][bi]), phaseTable[ai][bi]};
}

bool mlir::ftqc::pauliCommute(Pauli a, Pauli b) {
  // Paulis commute unless one is I or they are the same.
  if (a == Pauli::I || b == Pauli::I) return true;
  return a == b;
}

llvm::StringRef mlir::ftqc::pauliToString(Pauli p) {
  switch (p) {
  case Pauli::I: return "I";
  case Pauli::X: return "X";
  case Pauli::Y: return "Y";
  case Pauli::Z: return "Z";
  }
  return "?";
}

Pauli mlir::ftqc::stringToPauli(llvm::StringRef s) {
  if (s == "I") return Pauli::I;
  if (s == "X") return Pauli::X;
  if (s == "Y") return Pauli::Y;
  if (s == "Z") return Pauli::Z;
  return Pauli::I;
}

unsigned mlir::ftqc::pauliWeight(llvm::ArrayRef<Pauli> p) {
  unsigned w = 0;
  for (auto pi : p)
    if (pi != Pauli::I) ++w;
  return w;
}

bool mlir::ftqc::pauliStringsCommute(llvm::ArrayRef<Pauli> a,
                                      llvm::ArrayRef<Pauli> b) {
  assert(a.size() == b.size());
  // Two n-qubit Paulis commute iff they anticommute on an even number of sites.
  unsigned anticommuteCount = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    if (!pauliCommute(a[i], b[i]))
      ++anticommuteCount;
  }
  return anticommuteCount % 2 == 0;
}

//===----------------------------------------------------------------------===//
// Binary symplectic representation
//===----------------------------------------------------------------------===//

std::vector<uint8_t> mlir::ftqc::toSymplectic(llvm::ArrayRef<Pauli> p) {
  unsigned n = static_cast<unsigned>(p.size());
  std::vector<uint8_t> sv(2 * n, 0);
  for (unsigned i = 0; i < n; ++i) {
    switch (p[i]) {
    case Pauli::I: break;
    case Pauli::X: sv[i] = 1; break;
    case Pauli::Z: sv[n + i] = 1; break;
    case Pauli::Y: sv[i] = 1; sv[n + i] = 1; break;
    }
  }
  return sv;
}

llvm::SmallVector<Pauli, 32>
mlir::ftqc::fromSymplectic(llvm::ArrayRef<uint8_t> sv) {
  unsigned n = static_cast<unsigned>(sv.size() / 2);
  llvm::SmallVector<Pauli, 32> p(n, Pauli::I);
  for (unsigned i = 0; i < n; ++i) {
    bool x = sv[i], z = sv[n + i];
    if (x && z)  p[i] = Pauli::Y;
    else if (x)  p[i] = Pauli::X;
    else if (z)  p[i] = Pauli::Z;
  }
  return p;
}

uint8_t mlir::ftqc::symplecticInnerProduct(llvm::ArrayRef<uint8_t> u,
                                            llvm::ArrayRef<uint8_t> v) {
  assert(u.size() == v.size() && u.size() % 2 == 0);
  unsigned n = static_cast<unsigned>(u.size() / 2);
  uint8_t r = 0;
  for (unsigned i = 0; i < n; ++i)
    r ^= (u[i] & v[n+i]) ^ (u[n+i] & v[i]);
  return r;
}

//===----------------------------------------------------------------------===//
// GF(2) Gaussian elimination
//===----------------------------------------------------------------------===//

std::vector<std::vector<uint8_t>>
mlir::ftqc::gaussianEliminationGF2(std::vector<std::vector<uint8_t>> mat) {
  if (mat.empty()) return mat;
  unsigned rows = static_cast<unsigned>(mat.size());
  unsigned cols = static_cast<unsigned>(mat[0].size());
  unsigned pivot = 0;

  for (unsigned c = 0; c < cols && pivot < rows; ++c) {
    // Find pivot row.
    unsigned pivotRow = pivot;
    while (pivotRow < rows && mat[pivotRow][c] == 0) ++pivotRow;
    if (pivotRow == rows) continue;

    std::swap(mat[pivot], mat[pivotRow]);

    // Eliminate column c in all other rows.
    for (unsigned r = 0; r < rows; ++r) {
      if (r != pivot && mat[r][c]) {
        for (unsigned cc = 0; cc < cols; ++cc)
          mat[r][cc] ^= mat[pivot][cc];
      }
    }
    ++pivot;
  }
  return mat;
}

//===----------------------------------------------------------------------===//
// Clifford tableau
//===----------------------------------------------------------------------===//

CliffordTableau::CliffordTableau(unsigned n) : n(n) {
  tableau.assign(2 * n, std::vector<uint8_t>(2 * n + 1, 0));
  phases.assign(2 * n, 0);
  // Initialize as identity: destabilizer i has X on qubit i,
  // stabilizer i has Z on qubit i.
  for (unsigned i = 0; i < n; ++i) {
    tableau[i][i] = 1;           // destabilizer: X_i
    tableau[n+i][n+i] = 1;      // stabilizer: Z_i
  }
}

void CliffordTableau::applyH(unsigned q) {
  // H: X↔Z, Y→-Y (phase flip on Y)
  for (unsigned i = 0; i < 2 * n; ++i) {
    // Update phase: +1 if X and Z both set for qubit q (Y term → -Y → phase flip)
    phases[i] ^= (tableau[i][q] & tableau[i][n+q]);
    std::swap(tableau[i][q], tableau[i][n+q]);
  }
}

void CliffordTableau::applyS(unsigned q) {
  // S: X→Y, Z→Z, Y→-X
  for (unsigned i = 0; i < 2 * n; ++i) {
    phases[i] ^= (tableau[i][q] & tableau[i][n+q]);
    tableau[i][n+q] ^= tableau[i][q];
  }
}

void CliffordTableau::applyX(unsigned q) {
  // X: Z→-Z (phase flip for Z component)
  for (unsigned i = 0; i < 2 * n; ++i)
    phases[i] ^= tableau[i][n+q];
}

void CliffordTableau::applyZ(unsigned q) {
  // Z: X→-X
  for (unsigned i = 0; i < 2 * n; ++i)
    phases[i] ^= tableau[i][q];
}

void CliffordTableau::applyY(unsigned q) {
  applyX(q); applyZ(q);
}

void CliffordTableau::applyCNOT(unsigned ctrl, unsigned tgt) {
  // CNOT: X_c→X_c X_t, Z_t→Z_c Z_t, X_t→X_t, Z_c→Z_c
  for (unsigned i = 0; i < 2 * n; ++i) {
    phases[i] ^= tableau[i][ctrl] & tableau[i][n+tgt] &
                 (tableau[i][tgt] ^ tableau[i][n+ctrl] ^ 1);
    tableau[i][tgt]   ^= tableau[i][ctrl];
    tableau[i][n+ctrl] ^= tableau[i][n+tgt];
  }
}

void CliffordTableau::applyCZ(unsigned q0, unsigned q1) {
  applyH(q1);
  applyCNOT(q0, q1);
  applyH(q1);
}

std::string CliffordTableau::toString() const {
  std::ostringstream ss;
  for (unsigned i = 0; i < 2*n; ++i) {
    ss << (phases[i] ? "-" : "+");
    for (unsigned j = 0; j < n; ++j) {
      bool x = tableau[i][j], z = tableau[i][n+j];
      ss << (x && z ? "Y" : x ? "X" : z ? "Z" : "I");
    }
    ss << "\n";
  }
  return ss.str();
}

//===----------------------------------------------------------------------===//
// Transversal gate helpers
//===----------------------------------------------------------------------===//

bool mlir::ftqc::hasTransversalImplementation(llvm::StringRef gate,
                                              llvm::StringRef code) {
  if (code == "steane")
    return gate == "X" || gate == "Z" || gate == "H" || gate == "CNOT";
  if (code == "surface")
    return gate == "X" || gate == "Z" || gate == "CNOT";
  if (code == "color_code")
    return gate == "X" || gate == "Z" || gate == "H" ||
           gate == "S" || gate == "T" || gate == "CNOT";
  return false;
}

std::vector<std::pair<std::string, unsigned>>
mlir::ftqc::buildTransversalGateCircuit(llvm::StringRef gate,
                                         unsigned blockSize,
                                         llvm::StringRef /*code*/) {
  std::vector<std::pair<std::string, unsigned>> circuit;
  for (unsigned q = 0; q < blockSize; ++q)
    circuit.push_back({gate.str(), q});
  return circuit;
}
