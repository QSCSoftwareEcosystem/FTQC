//===- FTQCAttrs.h - FTQC Attribute Declarations --------------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file FTQCAttrs.h
/// @brief Declarations for all FTQC dialect attributes.
///
/// ### Enumerations (from FTQCEnums.h.inc)
/// | Enum | Values | Usage |
/// |---|---|---|
/// | `ECCKind` | `steane`, `surface`, `color_code`, `concat`, `repetition`, `compass`, `hypergraph_product` | Identifies the error-correcting code family |
/// | `PauliKind` | `I`, `X`, `Y`, `Z` | Single-qubit Pauli operator |
/// | `SyndromeBasis` | `X`, `Z` | Stabiliser measurement basis |
/// | `MagicKind` | `T`, `CCZ` | Magic state type |
///
/// ### Attributes (from FTQCAttrs.h.inc)
/// | Attribute | Mnemonic | Fields | Description |
/// |---|---|---|---|
/// | `ECCAttr` | `#ftqc.ecc` | `kind`, `dist`, `levels`, `code1`, `code2` | Identifies the QEC code parameterising a logical qubit |
/// | `CodePropertiesAttr` | `#ftqc.code_props` | `n`, `k`, `d` | Static [[n,k,d]] parameters |
/// | `DistillationParamsAttr` | `#ftqc.distill_params` | `inputCount`, `outputCount` | Distillation protocol shape |

#ifndef FTQC_ATTRS_H
#define FTQC_ATTRS_H

#include "mlir/IR/Attributes.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"

#include "FTQC/Utils/StabilizerUtils.h"

// Pull in the enum declarations first.
#include "FTQC/Dialect/FTQCEnums.h.inc"

namespace mlir::ftqc::detail {
struct ECCAttrStorage;
}

#define GET_ATTRDEF_CLASSES
#include "FTQC/Dialect/FTQCAttrs.h.inc"

#include <optional>
#include <tuple>

namespace mlir::ftqc::detail {

// MLIR creates a unique ECCAttrStorage instance for each distinct #ftqc.ecc
// attribute. This struct would usually be generated from the TableGen
// definition, but we implement it manually to add a StabilizerCode member.
// This gives us a place to cache the StabilizerCode after building it only
// once for each distinct code.

struct ECCAttrStorage : public AttributeStorage {
  using KeyTy = std::tuple<
    ECCKind,                 // kind
    std::optional<uint32_t>, // dist
    std::optional<uint32_t>, // levels
    ClassicalCodeAttr,       // code1
    ClassicalCodeAttr        // code2
  >;

  ECCAttrStorage(const KeyTy &key);

  static ECCAttrStorage *construct(AttributeStorageAllocator &allocator,
                                   const KeyTy &key) {
    return new (allocator.allocate<ECCAttrStorage>()) ECCAttrStorage(key);
  }

  bool operator==(const KeyTy &key) const { return getAsKey() == key; }

  KeyTy getAsKey() const {
    return std::make_tuple(kind, dist, levels, code1, code2);
  }

  ECCKind kind;
  std::optional<uint32_t> dist;
  std::optional<uint32_t> levels;
  ClassicalCodeAttr code1, code2;
  StabilizerCode stabilizerCode;
};

} // namespace mlir::ftqc::detail

#endif // FTQC_ATTRS_H
