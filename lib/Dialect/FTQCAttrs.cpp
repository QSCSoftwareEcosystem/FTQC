//===- FTQCAttrs.cpp - FTQC Attribute Implementations --------------------===//
//
// Extra method implementations for FTQC attributes beyond ODS defaults.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// ECCAttr methods
//===----------------------------------------------------------------------===//

ECCKind ECCAttr::getKind() const {
  return getImpl()->kind;
}

std::optional<uint32_t> ECCAttr::getDist() const {
  return getImpl()->dist;
}

ECCAttr ECCAttr::getOuter() const {
  return getImpl()->outer;
}

ECCAttr ECCAttr::getInner() const {
  return getImpl()->inner;
}

ClassicalCodeAttr ECCAttr::getCode1() const {
  return getImpl()->code1;
}

ClassicalCodeAttr ECCAttr::getCode2() const {
  return getImpl()->code2;
}

const StabilizerCode &ECCAttr::getStabilizerCode() const {
  return getImpl()->stabilizerCode;
}

unsigned ECCAttr::getPhysicalQubits() const {
  return getStabilizerCode().getN();
}

unsigned ECCAttr::getAncillaQubits() const {
  return getStabilizerCode().getNumStabilizers();
}

unsigned ECCAttr::getDistance() const {
  return getStabilizerCode().getD();
}

double ECCAttr::getFTThreshold() const {
  // Approximate fault-tolerance threshold for a code kind.  Returns the
  // depolarising-noise threshold below which error-correction improves fidelity
  // (i.e. p_phys < p_th).  Values are from the literature:
  //
  // | Code              | Threshold (approx.) |
  // |-------------------|---------------------|
  // | Steane [[7,1,3]]  | 1 %                 |
  // | Surface code      | 1 %                 |
  // | 2D color code     | 0.5–1.3 %           |
  // | Concatenated      | 0.5 %               |
  switch (getKind()) {
  case ECCKind::Steane:       return 1.0e-2;
  case ECCKind::Surface:      return 1.0e-2;
  case ECCKind::ColorCode:    return 1.0e-2;
  case ECCKind::Concatenated: return 5.0e-3;
  case ECCKind::Repetition:   return 0.5;
  default:                    return 1.0e-3;
  }
}

LogicalResult ECCAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                              ECCKind kind,
                              std::optional<uint32_t> dist,
                              ECCAttr outer,
                              ECCAttr inner,
                              ClassicalCodeAttr code1,
                              ClassicalCodeAttr code2) {
  switch (kind) {
  case ECCKind::Steane:
    break;

  case ECCKind::Surface:
  case ECCKind::ColorCode:
  case ECCKind::Repetition:
  case ECCKind::Compass:
    if (!dist) {
      emitError() << "this ECC kind requires a 'dist' parameter";
      return failure();
    }
    break;

  case ECCKind::Concatenated:
    if (!outer || !inner) {
      emitError() << "this ECC kind requires 'outer' and 'inner' parameters";
      return failure();
    }
    break;

  case ECCKind::HypergraphProduct:
    if (!code1 || !code2) {
      emitError() << "this ECC kind requires 'code1' and 'code2' parameters";
      return failure();
    }
    break;
  }
  return success();
}

namespace {

// Makes a StabilizerCode with the correct (given) [[n,k,d]] parameters and
// number of stabilizers, but without the correct parity-check matrices and
// logical operators. This will work for passes which only care about those
// parameters, but not for passes that need the Pauli strings, like
// --ftqc-expand-physical-generic.
// TODO: Replace all uses of this function by proper StabilizerCode builders.
StabilizerCode makeDummyCode(unsigned n, unsigned k, unsigned d,
                             unsigned numXStabilizers,
                             unsigned numZStabilizers) {
  std::vector<BinaryRow> Hx(numXStabilizers, BinaryRow(n, 0));
  std::vector<BinaryRow> Hz(numZStabilizers, BinaryRow(n, 0));
  return StabilizerCode::fromCSS(n, k, d, std::move(Hx), std::move(Hz));
}

} // namespace

ftqc::detail::ECCAttrStorage::ECCAttrStorage(const KeyTy &key) {
  std::tie(kind, dist, outer, inner, code1, code2) = key;

  switch (kind) {
  case ECCKind::Steane:
    stabilizerCode = StabilizerCode::makeSteane();
    break;
  case ECCKind::Surface:
    stabilizerCode = StabilizerCode::makeSurface(*dist);
    break;
  case ECCKind::ColorCode: { // 2D triangular color code
    unsigned n =
        (3 * *dist * *dist - 3 * *dist + 2) / 2; // roughly (3d^2 - 3d + 1)/2
    unsigned numStabilizers =
        (*dist * *dist - 1) / 2; // roughly half the physical qubit count
    stabilizerCode =
        makeDummyCode(n, 1, *dist, numStabilizers / 2, numStabilizers / 2);
    break;
  }
  case ECCKind::Concatenated:
    stabilizerCode = StabilizerCode::makeConcatenated(
        outer.getStabilizerCode(), inner.getStabilizerCode());
    break;
  case ECCKind::Repetition:
    stabilizerCode = StabilizerCode::makeRepetition(*dist);
    break;
  case ECCKind::Compass: { // compass code ~ surface code
    unsigned n = *dist * *dist;
    stabilizerCode = makeDummyCode(n, 1, *dist, (n - 1) / 2, (n - 1) / 2);
    break;
  }
  case ECCKind::HypergraphProduct:
    stabilizerCode = StabilizerCode::makeHypergraphProduct(
        code1.makeClassicalCode(), code2.makeClassicalCode());
    break;
  }
}

//===----------------------------------------------------------------------===//
// ClassicalCodeAttr extra methods
//===----------------------------------------------------------------------===//

ClassicalCode ClassicalCodeAttr::makeClassicalCode() const {
  unsigned n = getN();
  auto flatH = getH();
  std::vector<BinaryRow> H(flatH.size() / n, BinaryRow(n));
  for (unsigned i = 0; i < H.size(); ++i)
    for (unsigned j = 0; j < n; ++j)
      H[i][j] = flatH[i * n + j];
  return ClassicalCode::fromLinear(n, H);
}

//===----------------------------------------------------------------------===//
// PauliErrorAttr extra methods
//===----------------------------------------------------------------------===//

unsigned PauliErrorAttr::getWeight() const {
  unsigned w = 0;
  for (auto a : getPaulis()) {
    if (auto pa = mlir::dyn_cast<IntegerAttr>(a)) {
      if (pa.getInt() != 0)  // 0 = I
        ++w;
    }
  }
  return w;
}
