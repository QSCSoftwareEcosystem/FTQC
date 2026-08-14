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
// Helper: physical qubit count per code
//===----------------------------------------------------------------------===//

unsigned mlir::ftqc::getPhysicalQubitsForCode(ECCKind kind, unsigned d,
                                              unsigned levels) {
  unsigned base = 0;
  switch (kind) {
  case ECCKind::Steane:
    base = 7;   // [[7,1,3]] regardless of d (d=3 only valid)
    break;
  case ECCKind::Surface:
    base = d * d;   // rotated surface code: d^2 data qubits
    break;
  case ECCKind::ColorCode:
    // 2D triangular color code: roughly (3d^2 - 3d + 1)/2 physical per logical
    base = (3 * d * d - 3 * d + 2) / 2;
    break;
  case ECCKind::Concatenated:
    // k levels of Steane: 7^levels
    base = 1;
    for (unsigned i = 0; i < levels; ++i)
      base *= 7;
    break;
  case ECCKind::Repetition:
    base = d;   // d-qubit repetition code
    break;
  case ECCKind::Compass:
    base = d * d;   // compass code ~ surface code
    break;
  case ECCKind::HypergraphProduct:
    // We cannot compute the number of physical qubits in a hypergraph product
    // code, without knowing the two classical codes from which it is
    // constructed. This shouldn't matter, since this function is currently only
    // used from ECCAttr::getPhysicalQubits(), and ECCAttr::verify() prevents
    // using it with this kind.
    break;
  }
  return base;
}

unsigned mlir::ftqc::getAncillaQubitsForCode(ECCKind kind, unsigned d) {
  switch (kind) {
  case ECCKind::Steane:
    return 6;   // 3 X + 3 Z stabilizers → 6 ancilla per round
  case ECCKind::Surface:
    return (d * d - 1);  // (d^2-1) plaquette ancilla
  case ECCKind::ColorCode:
    return (d * d - 1) / 2;  // roughly half the physical qubit count
  case ECCKind::Concatenated:
    return 6;   // per level (approximate)
  case ECCKind::Repetition:
    return d - 1;
  default:
    return d * d;
  }
}

double mlir::ftqc::getFTThreshold(ECCKind kind) {
  switch (kind) {
  case ECCKind::Steane:      return 1.0e-2;
  case ECCKind::Surface:     return 1.0e-2;
  case ECCKind::ColorCode:   return 1.0e-2;
  case ECCKind::Concatenated:return 5.0e-3;
  case ECCKind::Repetition:  return 0.5;
  default:                   return 1.0e-3;
  }
}

bool mlir::ftqc::isCSSCode(ECCKind kind) {
  return kind == ECCKind::Steane ||
         kind == ECCKind::Surface ||
         kind == ECCKind::ColorCode ||
         kind == ECCKind::HypergraphProduct;
}

bool mlir::ftqc::hasTransversalT(ECCKind kind) {
  // Only 3D color codes support transversal T; 2D color codes support
  // a transversal Hadamard but T requires magic state injection in general.
  // For FTQC purposes we mark ColorCode as supporting it via code-switching.
  return kind == ECCKind::ColorCode;
}

//===----------------------------------------------------------------------===//
// ECCAttr extra methods
//===----------------------------------------------------------------------===//

unsigned ECCAttr::getPhysicalQubits() const {
  return getPhysicalQubitsForCode(getKind(), getDistance(), getLevels());
}

unsigned ECCAttr::getAncillaQubits() const {
  return getAncillaQubitsForCode(getKind(), getDistance());
}

bool ECCAttr::isCSS() const {
  return mlir::ftqc::isCSSCode(getKind());
}

bool ECCAttr::supportsTransversalT() const {
  return mlir::ftqc::hasTransversalT(getKind());
}

double ECCAttr::getFTThreshold() const {
  return mlir::ftqc::getFTThreshold(getKind());
}

StabilizerCode ECCAttr::makeStabilizerCode() const {
  switch (getKind()) {
  case ECCKind::Steane:
    return StabilizerCode::makeSteane();
  case ECCKind::Surface:
    return StabilizerCode::makeSurface(getDistance());
  default:
    return StabilizerCode(); // TODO: Handle remaining cases.
  }
}

LogicalResult ECCAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                              ECCKind kind, uint32_t distance,
                              uint32_t levels) {
  if (kind == ECCKind::HypergraphProduct) {
    emitError() << "#ftqc.ecc<hypergraph_product, d> is not supported; "
                   "use #ftqc.hypergraph_product<code1, code2> instead";
    return failure();
  }
  return success();
}

//===----------------------------------------------------------------------===//
// HypergraphProductAttr extra methods
//===----------------------------------------------------------------------===//

StabilizerCode HypergraphProductAttr::makeStabilizerCode() const {
  return StabilizerCode::makeHypergraphProduct(getCode1().makeClassicalCode(),
                                               getCode2().makeClassicalCode());
}

unsigned HypergraphProductAttr::getPhysicalQubits() const {
  return getCode1().getN() * getCode2().getN() +
         (getCode1().getH().size() / getCode1().getN()) *
             (getCode2().getH().size() / getCode2().getN());
}

unsigned HypergraphProductAttr::getAncillaQubits() const {
  return (getCode1().getH().size() / getCode1().getN()) * getCode2().getN() +
         getCode1().getN() * (getCode2().getH().size() / getCode2().getN());
}

uint32_t HypergraphProductAttr::getDistance() const {
  return makeStabilizerCode().getD();
}

ECCKind HypergraphProductAttr::getKind() const {
  return ECCKind::HypergraphProduct;
}

bool HypergraphProductAttr::isCSS() const {
  return true;
}

bool HypergraphProductAttr::supportsTransversalT() const {
  return false;
}

double HypergraphProductAttr::getFTThreshold() const {
  return mlir::ftqc::getFTThreshold(ECCKind::HypergraphProduct);
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
