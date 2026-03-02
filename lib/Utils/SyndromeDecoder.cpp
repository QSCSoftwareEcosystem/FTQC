//===- SyndromeDecoder.cpp - Syndrome decoder implementations -------------===//
#include "FTQC/Utils/SyndromeDecoder.h"
#include <cassert>
#include <stdexcept>

using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// LookupTableDecoder
//===----------------------------------------------------------------------===//

LookupTableDecoder::LookupTableDecoder(const StabilizerCode &code)
    : code_(code),
      syndromeWidth_(static_cast<unsigned>(
          code.getHx().size() + code.getHz().size())) {
  if (syndromeWidth_ <= 20)  // only feasible for small codes
    buildTable();
}

void LookupTableDecoder::buildTable() {
  table_ = code_.getSyndromeLookupTable();
}

PauliString LookupTableDecoder::decode(llvm::ArrayRef<uint8_t> syndrome) const {
  unsigned idx = 0;
  for (auto b : syndrome) idx = (idx << 1) | b;
  if (idx < table_.size()) return table_[idx];
  return PauliString(code_.getN(), Pauli::I);
}

//===----------------------------------------------------------------------===//
// MinWeightDecoder
//===----------------------------------------------------------------------===//

MinWeightDecoder::MinWeightDecoder(const StabilizerCode &code)
    : code_(code),
      syndromeWidth_(static_cast<unsigned>(
          code.getHx().size() + code.getHz().size())) {}

PauliString MinWeightDecoder::decode(llvm::ArrayRef<uint8_t> syndrome) const {
  // Exhaustive search up to weight t = floor((d-1)/2).
  unsigned n = code_.getN();
  unsigned t = (code_.getD() - 1) / 2;

  // Weight-0 check (no error).
  PauliString noErr(n, Pauli::I);
  if (code_.computeSyndrome(noErr) == Syndrome(syndrome.begin(), syndrome.end()))
    return noErr;

  // Weight-1 single-qubit errors.
  for (unsigned q = 0; q < n && t >= 1; ++q) {
    for (Pauli p : {Pauli::X, Pauli::Y, Pauli::Z}) {
      PauliString err(n, Pauli::I);
      err[q] = p;
      auto syn = code_.computeSyndrome(err);
      if (syn == Syndrome(syndrome.begin(), syndrome.end()))
        return err;
    }
  }

  // Weight-2 (if d ≥ 5).
  if (t >= 2) {
    for (unsigned q0 = 0; q0 < n; ++q0) {
      for (unsigned q1 = q0 + 1; q1 < n; ++q1) {
        for (Pauli p0 : {Pauli::X, Pauli::Y, Pauli::Z}) {
          for (Pauli p1 : {Pauli::X, Pauli::Y, Pauli::Z}) {
            PauliString err(n, Pauli::I);
            err[q0] = p0; err[q1] = p1;
            auto syn = code_.computeSyndrome(err);
            if (syn == Syndrome(syndrome.begin(), syndrome.end()))
              return err;
          }
        }
      }
    }
  }

  return PauliString(n, Pauli::I);  // give up
}

//===----------------------------------------------------------------------===//
// SteaneDecoder
//===----------------------------------------------------------------------===//

SteaneDecoder::SteaneDecoder() = default;

PauliString SteaneDecoder::decode(llvm::ArrayRef<uint8_t> syndrome) const {
  // Syndrome format: [zSyn0, zSyn1, zSyn2, xSyn0, xSyn1, xSyn2]
  assert(syndrome.size() == 6);

  PauliString result(7, Pauli::I);

  // X-error from Z-stabilizer syndrome (bits 0..2).
  uint8_t zSyn = (syndrome[0]<<2) | (syndrome[1]<<1) | syndrome[2];
  unsigned xErrQubit = 7;
  Pauli xErr = decodeXError(zSyn, xErrQubit);
  if (xErrQubit < 7) result[xErrQubit] = xErr;

  // Z-error from X-stabilizer syndrome (bits 3..5).
  uint8_t xSyn = (syndrome[3]<<2) | (syndrome[4]<<1) | syndrome[5];
  unsigned zErrQubit = 7;
  Pauli zErr = decodeZError(xSyn, zErrQubit);
  if (zErrQubit < 7) {
    if (result[zErrQubit] == Pauli::X && zErr == Pauli::Z)
      result[zErrQubit] = Pauli::Y;
    else if (result[zErrQubit] == Pauli::I)
      result[zErrQubit] = zErr;
  }

  return result;
}

Pauli SteaneDecoder::decodeXError(uint8_t zSyn, unsigned &errorQubit) const {
  // Find which column of H equals zSyn.
  for (unsigned q = 0; q < 7; ++q) {
    if (kHColumns[q] == zSyn) {
      errorQubit = q;
      return Pauli::X;
    }
  }
  errorQubit = 7;  // no error
  return Pauli::I;
}

Pauli SteaneDecoder::decodeZError(uint8_t xSyn, unsigned &errorQubit) const {
  for (unsigned q = 0; q < 7; ++q) {
    if (kHColumns[q] == xSyn) {
      errorQubit = q;
      return Pauli::Z;
    }
  }
  errorQubit = 7;
  return Pauli::I;
}

//===----------------------------------------------------------------------===//
// Factory
//===----------------------------------------------------------------------===//

std::unique_ptr<SyndromeDecoder>
mlir::ftqc::createDecoder(const StabilizerCode &code) {
  // Use the Steane decoder for Steane code (fast formula).
  if (code.getN() == 7 && code.getK() == 1 && code.getD() == 3)
    return std::make_unique<SteaneDecoder>();

  // Use lookup table for small codes.
  unsigned synWidth = static_cast<unsigned>(
      code.getHx().size() + code.getHz().size());
  if (synWidth <= 16)
    return std::make_unique<LookupTableDecoder>(code);

  // Fall back to min-weight exhaustive search.
  return std::make_unique<MinWeightDecoder>(code);
}
