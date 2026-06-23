//===- StabilizerUtils.cpp - Stabilizer formalism implementation ----------===//
#include "FTQC/Utils/StabilizerUtils.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <numeric>
#include <sstream>

using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// ClassicalCode methods
//===----------------------------------------------------------------------===//

ClassicalCode ClassicalCode::fromLinear(unsigned n, unsigned k, unsigned d,
                                        std::vector<BinaryRow> H) {
  ClassicalCode code;
  code.n_ = n; code.k_ = k; code.d_ = d;
  code.H_ = std::move(H);
  return code;
}

ClassicalCode ClassicalCode::makeRepetition(unsigned n) {
  std::vector<BinaryRow> H(n - 1, BinaryRow(n, 0));
  for (unsigned i = 0; i < n - 1; ++i) {
    H[i][i] = 1;
    H[i][i + 1] = 1;
  }
  return ClassicalCode::fromLinear(n, 1, n, H);
}

unsigned ClassicalCode::getTransposeN() const {
  return static_cast<unsigned>(H_.size());
}

unsigned ClassicalCode::getTransposeK() const {
  unsigned rankH = n_ - k_;
  return getTransposeN() - rankH;
}

unsigned ClassicalCode::getTransposeD() const {
  return d_; // TODO: Return correct value
}

std::string ClassicalCode::toString(bool includeMatrix) const {
  std::ostringstream ss;
  ss << "[" << n_ << "," << k_ << "," << d_ << "] code\n";
  ss << "  " << H_.size() << " parity checks\n";
  if (includeMatrix) {
    ss << "  H =\n";
    for (auto &row : H_) {
      ss << "    ";
      for (uint8_t x : row)
        ss << "1."[!x];
      ss << "\n";
    }
  }
  return ss.str();
}

//===----------------------------------------------------------------------===//
// StabilizerCode factory methods
//===----------------------------------------------------------------------===//

/// Steane [[7,1,3]] code.
///
/// Parity check matrix H (3×7):
///   H = [0 0 0 1 1 1 1]
///       [0 1 1 0 0 1 1]
///       [1 0 1 0 1 0 1]
///
/// For CSS code: Hx = Hz = H.
/// X-stabilizers act on qubits indicated by rows of Hx.
/// Z-stabilizers act on qubits indicated by rows of Hz.
StabilizerCode StabilizerCode::makeSteane() {
  // Rows of H (each row = list of qubits in the stabilizer)
  std::vector<BinaryRow> H(3, BinaryRow(7, 0));
  // Row 0: qubits 3,4,5,6
  H[0][3] = H[0][4] = H[0][5] = H[0][6] = 1;
  // Row 1: qubits 1,2,5,6
  H[1][1] = H[1][2] = H[1][5] = H[1][6] = 1;
  // Row 2: qubits 0,2,4,6
  H[2][0] = H[2][2] = H[2][4] = H[2][6] = 1;

  auto code = StabilizerCode::fromCSS(7, 1, 3, H, H);

  // Logical X: X0X1X2 (minimum weight representative, weight 3)
  code.logX_ = {Pauli::X, Pauli::X, Pauli::X,
                Pauli::I, Pauli::I, Pauli::I, Pauli::I};
  // Logical Z: Z0Z1Z2 (same support)
  code.logZ_ = {Pauli::Z, Pauli::Z, Pauli::Z,
                Pauli::I, Pauli::I, Pauli::I, Pauli::I};
  return code;
}

StabilizerCode StabilizerCode::fromCSS(unsigned n, unsigned k, unsigned d,
                                       std::vector<BinaryRow> Hx,
                                       std::vector<BinaryRow> Hz) {
  StabilizerCode code;
  code.n_ = n; code.k_ = k; code.d_ = d;
  code.Hx_ = std::move(Hx);
  code.Hz_ = std::move(Hz);
  return code;
}

StabilizerCode StabilizerCode::make422() {
  // [[4,2,2]] error-detecting code.
  // X stabilizer: X0X1X2X3
  // Z stabilizer: Z0Z1Z2Z3
  BinaryRow xRow(4, 1), zRow(4, 1);
  auto code = StabilizerCode::fromCSS(4, 2, 2, {xRow}, {zRow});
  return code;
}

StabilizerCode StabilizerCode::makeSurface(unsigned d) {
  // Rotated surface code [[d^2, 1, d]].
  // Simplified: d^2 data qubits, (d^2-1)/2 X and Z plaquettes each.
  unsigned n = d * d;
  unsigned stabs = n - 1;
  unsigned halfStabs = stabs / 2;

  std::vector<BinaryRow> Hx(halfStabs, BinaryRow(n, 0));
  std::vector<BinaryRow> Hz(halfStabs, BinaryRow(n, 0));

  // Fill in plaquette structure for d×d grid.
  // X plaquettes: bulk (d-1)×(d-1)/2 + boundary.
  // This is a simplified placeholder.
  unsigned plaq = 0;
  for (unsigned r = 0; r < d - 1 && plaq < halfStabs; ++r) {
    for (unsigned c = r % 2; c < d - 1 && plaq < halfStabs; c += 2, ++plaq) {
      // Plaquette at (r,c): involves qubits (r,c), (r,c+1), (r+1,c), (r+1,c+1)
      unsigned q0 = r * d + c,       q1 = r * d + c + 1;
      unsigned q2 = (r+1)*d + c,     q3 = (r+1)*d + c + 1;
      if (q0 < n) Hx[plaq][q0] = 1;
      if (q1 < n) Hx[plaq][q1] = 1;
      if (q2 < n) Hx[plaq][q2] = 1;
      if (q3 < n) Hx[plaq][q3] = 1;
    }
  }
  plaq = 0;
  for (unsigned r = 0; r < d - 1 && plaq < halfStabs; ++r) {
    for (unsigned c = (r+1)%2; c < d - 1 && plaq < halfStabs; c += 2, ++plaq) {
      unsigned q0 = r*d+c, q1 = r*d+c+1, q2 = (r+1)*d+c, q3 = (r+1)*d+c+1;
      if (q0<n) Hz[plaq][q0]=1;
      if (q1<n) Hz[plaq][q1]=1;
      if (q2<n) Hz[plaq][q2]=1;
      if (q3<n) Hz[plaq][q3]=1;
    }
  }

  return StabilizerCode::fromCSS(n, 1, d, Hx, Hz);
}

StabilizerCode StabilizerCode::makeHypergraphProduct(const ClassicalCode &code1,
                                                     const ClassicalCode &code2) {
  unsigned n1 = code1.getN(), n2 = code2.getN();
  unsigned k1 = code1.getK(), k2 = code2.getK();
  unsigned d1 = code1.getD(), d2 = code2.getD();
  unsigned nT1 = code1.getTransposeN(), nT2 = code2.getTransposeN();
  unsigned kT1 = code1.getTransposeK(), kT2 = code2.getTransposeK();
  unsigned dT1 = code1.getTransposeD(), dT2 = code2.getTransposeD();
  auto H1 = code1.getH(), H2 = code2.getH();

  unsigned n = n1 * n2 + nT1 * nT2;
  unsigned k = k1 * k2 + kT1 * kT2;
  unsigned d = std::min({d1, d2, dT1, dT2});
  std::vector<BinaryRow> Hx(nT1 * n2, BinaryRow(n, 0));
  std::vector<BinaryRow> Hz(n1 * nT2, BinaryRow(n, 0));

  // Hx = [ H1⊗I_n2 | I_r1⊗H2^T ]
  for (unsigned i1 = 0; i1 < nT1; ++i1) {
    for (unsigned j2 = 0; j2 < n2; ++j2) {
      auto &row = Hx[i1 * n2 + j2];
      for (unsigned j1 = 0; j1 < n1; ++j1)
        row[j1 * n2 + j2] = H1[i1][j1];
      for (unsigned i2 = 0; i2 < nT2; ++i2)
        row[n1 * n2 + i1 * nT2 + i2] = H2[i2][j2];
    }
  }

  // Hz = [ I_n1⊗H2 | H1^T⊗I_r2 ]
  for (unsigned j1 = 0; j1 < n1; ++j1) {
    for (unsigned i2 = 0; i2 < nT2; ++i2) {
      auto &row = Hz[j1 * nT2 + i2];
      for (unsigned j2 = 0; j2 < n2; ++j2)
        row[j1 * n2 + j2] = H2[i2][j2];
      for (unsigned i1 = 0; i1 < nT1; ++i1)
        row[n1 * n2 + i1 * nT2 + i2] = H1[i1][j1];
    }
  }

  // TODO: Set logical operators
  return StabilizerCode::fromCSS(n, k, d, Hx, Hz);
}

//===----------------------------------------------------------------------===//
// Syndrome computation
//===----------------------------------------------------------------------===//

Syndrome StabilizerCode::computeSyndrome(llvm::ArrayRef<Pauli> error) const {
  assert(error.size() == n_ && "error must have n components");
  Syndrome syn;

  // X syndrome: apply Z stabilizers to X/Y errors.
  for (auto &row : Hz_) {
    uint8_t bit = 0;
    for (unsigned i = 0; i < n_; ++i)
      if (row[i] && (error[i] == Pauli::X || error[i] == Pauli::Y))
        bit ^= 1;
    syn.push_back(bit);
  }
  // Z syndrome: apply X stabilizers to Z/Y errors.
  for (auto &row : Hx_) {
    uint8_t bit = 0;
    for (unsigned i = 0; i < n_; ++i)
      if (row[i] && (error[i] == Pauli::Z || error[i] == Pauli::Y))
        bit ^= 1;
    syn.push_back(bit);
  }
  return syn;
}

//===----------------------------------------------------------------------===//
// Syndrome → correction (lookup table)
//===----------------------------------------------------------------------===//

const std::vector<PauliString>& StabilizerCode::getSyndromeLookupTable() const {
  if (!lutBuilt_) buildLUT();
  return syndromeLUT_;
}

void StabilizerCode::buildLUT() const {
  unsigned synWidth = static_cast<unsigned>(Hx_.size() + Hz_.size());
  unsigned tableSize = 1u << synWidth;
  syndromeLUT_.resize(tableSize, PauliString(n_, Pauli::I));

  // For each single-qubit Pauli error, compute its syndrome and store
  // the correction.
  for (unsigned q = 0; q < n_; ++q) {
    for (Pauli p : {Pauli::X, Pauli::Y, Pauli::Z}) {
      PauliString err(n_, Pauli::I);
      err[q] = p;
      auto syn = computeSyndrome(err);

      // Convert syndrome to integer index.
      unsigned idx = 0;
      for (auto b : syn) idx = (idx << 1) | b;

      if (idx < tableSize && syndromeLUT_[idx][0] == Pauli::I)
        syndromeLUT_[idx] = err;  // store the error as correction
    }
  }
  lutBuilt_ = true;
}

PauliString StabilizerCode::decode(llvm::ArrayRef<uint8_t> syndrome) const {
  if (!lutBuilt_) buildLUT();

  unsigned idx = 0;
  for (auto b : syndrome) idx = (idx << 1) | b;

  if (idx < syndromeLUT_.size())
    return syndromeLUT_[idx];

  return PauliString(n_, Pauli::I);  // no correction
}

const PauliString& StabilizerCode::getLogicalX(unsigned) const { return logX_; }
const PauliString& StabilizerCode::getLogicalZ(unsigned) const { return logZ_; }

std::string StabilizerCode::toString(bool includeMatrices) const {
  std::ostringstream ss;
  ss << "[[" << n_ << "," << k_ << "," << d_ << "]] code\n";
  ss << "  " << Hx_.size() << " X stabilizers, "
     << Hz_.size() << " Z stabilizers\n";
  if (includeMatrices) {
    ss << "  Hx =\n";
    for (auto &row : Hx_) {
      ss << "    ";
      for (uint8_t x : row)
        ss << "X."[!x];
      ss << "\n";
    }
    ss << "  Hz =\n";
    for (auto &row : Hz_) {
      ss << "    ";
      for (uint8_t x : row)
        ss << "Z."[!x];
      ss << "\n";
    }
  }
  return ss.str();
}

std::string StabilizerCode::toGraphviz() const {
  std::ostringstream ss;
  ss << "graph {\n"
     << "  layout=neato;\n";
  for (size_t i = 0; i < Hx_.size(); ++i) {
    ss << "  x" << i << " [color=red];\n"
       << "  x" << i << " -- {";
    for (unsigned j = 0; j < n_; ++j)
      if (Hx_[i][j])
        ss << " d" << j;
    ss << " };\n";
  }
  for (size_t i = 0; i < Hz_.size(); ++i) {
    ss << "  z" << i << " [color=green];\n"
       << "  z" << i << " -- {";
    for (unsigned j = 0; j < n_; ++j)
      if (Hz_[i][j])
        ss << " d" << j;
    ss << " };\n";
  }
  ss << "}\n";
  return ss.str();
}

//===----------------------------------------------------------------------===//
// Surface code plaquette generator
//===----------------------------------------------------------------------===//

std::vector<surface::Plaquette> surface::generatePlaquettes(unsigned d) {
  std::vector<Plaquette> plaquettes;
  unsigned n = d * d;

  // X plaquettes (bulk).
  for (unsigned r = 0; r < d - 1; ++r) {
    for (unsigned c = 0; c < d - 1; ++c) {
      if ((r + c) % 2 != 0) continue;
      Plaquette p;
      p.isX = true;
      p.numQubits = 4;
      p.qubits[0] = r * d + c;
      p.qubits[1] = r * d + c + 1;
      p.qubits[2] = (r+1)*d + c;
      p.qubits[3] = (r+1)*d + c + 1;
      plaquettes.push_back(p);
    }
  }

  // Z plaquettes (bulk).
  for (unsigned r = 0; r < d - 1; ++r) {
    for (unsigned c = 0; c < d - 1; ++c) {
      if ((r + c) % 2 != 1) continue;
      Plaquette p;
      p.isX = false;
      p.numQubits = 4;
      p.qubits[0] = r * d + c;
      p.qubits[1] = r * d + c + 1;
      p.qubits[2] = (r+1)*d + c;
      p.qubits[3] = (r+1)*d + c + 1;
      plaquettes.push_back(p);
    }
  }

  return plaquettes;
}

//===----------------------------------------------------------------------===//
// Top-level decoder utilities
//===----------------------------------------------------------------------===//

PauliString mlir::ftqc::minimumWeightDecoder(const StabilizerCode &code,
                                              llvm::ArrayRef<uint8_t> syndrome) {
  return code.decode(syndrome);
}

PauliString mlir::ftqc::lookupDecoder(const StabilizerCode &code,
                                       llvm::ArrayRef<uint8_t> syndrome) {
  return code.decode(syndrome);
}

bool mlir::ftqc::verifyCorrectionSuccess(const StabilizerCode &code,
                                          llvm::ArrayRef<Pauli> error,
                                          llvm::ArrayRef<Pauli> correction) {
  assert(error.size() == correction.size());
  // Multiply error × correction; result should be in the stabilizer group.
  PauliString combined(error.size(), Pauli::I);
  for (size_t i = 0; i < error.size(); ++i) {
    // Simple XOR in GF(4) (approximate; ignores Y = iXZ phases)
    uint8_t e = static_cast<uint8_t>(error[i]);
    uint8_t c = static_cast<uint8_t>(correction[i]);
    combined[i] = static_cast<Pauli>(e ^ c);
  }
  // Check syndrome of combined.
  auto syn = code.computeSyndrome(combined);
  return std::all_of(syn.begin(), syn.end(), [](uint8_t b){ return b == 0; });
}
