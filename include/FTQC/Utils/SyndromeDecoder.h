//===- SyndromeDecoder.h - Syndrome decoding algorithms -------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file SyndromeDecoder.h
/// @brief Syndrome decoding interface and concrete decoder implementations.
///
/// ### What is syndrome decoding?
/// After a round of stabiliser measurements, the measurement outcomes form a
/// *syndrome* bit vector.  A decoder maps this vector to a Pauli correction
/// operator that (ideally) returns the code block to the code space with high
/// probability.
///
/// ### Decoder hierarchy
/// @code{.unparsed}
///   SyndromeDecoder        (abstract interface)
///   ├── LookupTableDecoder  (O(1), exact, for ≤ 20 stabilisers)
///   ├── MinWeightDecoder    (exhaustive, correct by definition)
///   └── SteaneDecoder       ([[7,1,3]] direct binary formula)
/// @endcode
///
/// ### Choosing a decoder
/// | Code        | Stabilisers | Recommended decoder  |
/// |-------------|-------------|----------------------|
/// | Steane      | 6           | `SteaneDecoder`      |
/// | Surface d≤5 | ≤ 24        | `LookupTableDecoder` |
/// | Surface d>5 | > 24        | `MinWeightDecoder`   |
/// | Generic     | any         | `createDecoder()`    |
///
/// `createDecoder()` automatically selects the best decoder for the code.
///
/// ### Usage
/// @code{.cpp}
/// StabilizerCode steane = StabilizerCode::makeSteane();
/// auto dec = createDecoder(steane);
/// PauliString correction = dec->decode({0, 1, 0, 1, 0, 0});
/// @endcode
///
/// @see StabilizerUtils.h — `StabilizerCode`, `PauliString`, `Syndrome` types.
/// @see FaultPathAnalysis.h — uses decoders to classify fault paths.

#ifndef FTQC_UTILS_SYNDROMEDECODER_H
#define FTQC_UTILS_SYNDROMEDECODER_H

#include "FTQC/Utils/StabilizerUtils.h"
#include "llvm/ADT/DenseMap.h"
#include <functional>
#include <memory>

namespace mlir::ftqc {

//===----------------------------------------------------------------------===//
// SyndromeDecoder (abstract interface)
//===----------------------------------------------------------------------===//

/// @brief Abstract interface for syndrome decoders.
///
/// All decoders accept a syndrome bit vector and return a `PauliString`
/// correction.  Implementations may be exact (lookup table, exhaustive search)
/// or heuristic (belief propagation, MWPM).
class SyndromeDecoder {
public:
  virtual ~SyndromeDecoder() = default;

  /// @brief Decode a syndrome and return the inferred Pauli correction.
  ///
  /// @param syndrome Measured syndrome bits (length = `getSyndromeWidth()`).
  /// @return Minimum-weight (or heuristically good) Pauli correction.
  virtual PauliString decode(llvm::ArrayRef<uint8_t> syndrome) const = 0;

  /// @brief Return the number of syndrome bits this decoder handles.
  /// @return Length of the syndrome vector expected by `decode()`.
  virtual unsigned getSyndromeWidth() const = 0;
};

//===----------------------------------------------------------------------===//
// LookupTableDecoder
//===----------------------------------------------------------------------===//

/// @brief Exact decoder for small codes using a precomputed syndrome table.
///
/// Precomputes and stores the full syndrome → correction mapping at
/// construction time.  Decoding is O(1) thereafter.
///
/// ### Capacity
/// Supports codes with up to 20 stabilisers (2^20 ≈ 1M table entries).
/// For larger codes use `MinWeightDecoder`.
///
/// ### Table construction
/// The table is built by `buildTable()` (called automatically in the
/// constructor) using exhaustive enumeration of all correctable errors.
class LookupTableDecoder : public SyndromeDecoder {
public:
  /// @brief Construct the decoder and precompute the syndrome table.
  ///
  /// Construction time is O(2^m) where m is the number of stabilisers.
  ///
  /// @param code The stabiliser code to build the table for.
  explicit LookupTableDecoder(const StabilizerCode &code);

  /// @brief Decode a syndrome in O(1) using the precomputed table.
  ///
  /// @param syndrome Syndrome bits (length must equal `getSyndromeWidth()`).
  /// @return Pauli correction from the lookup table.
  PauliString decode(llvm::ArrayRef<uint8_t> syndrome) const override;

  /// @brief Return the syndrome width (number of stabilisers).
  unsigned getSyndromeWidth() const override { return syndromeWidth_; }

  /// @brief Build (or rebuild) the lookup table explicitly.
  ///
  /// Called automatically by the constructor; exposed for testing.
  void buildTable();

private:
  const StabilizerCode &code_; ///< Reference to the code (must outlive decoder).
  unsigned syndromeWidth_;     ///< Number of syndrome bits (= number of stabilisers).
  std::vector<PauliString> table_; ///< table_[syndrome_int] → correction.
};

//===----------------------------------------------------------------------===//
// MinWeightDecoder
//===----------------------------------------------------------------------===//

/// @brief Exact decoder via exhaustive minimum-weight search.
///
/// Searches for the minimum-weight Pauli correction consistent with the
/// syndrome by enumerating Pauli strings in order of increasing weight.
///
/// ### Complexity
/// Exponential in the number of physical qubits: O(3^n × n) per call.
/// Suitable for verification and codes with n ≤ 15.  For production use
/// on larger codes, replace with a proper MWPM implementation.
class MinWeightDecoder : public SyndromeDecoder {
public:
  /// @brief Construct the decoder for the given code.
  /// @param code The stabiliser code.
  explicit MinWeightDecoder(const StabilizerCode &code);

  /// @brief Decode a syndrome by exhaustive minimum-weight search.
  ///
  /// @param syndrome Syndrome bits.
  /// @return Minimum-weight Pauli correction consistent with `syndrome`.
  PauliString decode(llvm::ArrayRef<uint8_t> syndrome) const override;

  /// @brief Return the syndrome width.
  unsigned getSyndromeWidth() const override { return syndromeWidth_; }

private:
  const StabilizerCode &code_; ///< Reference to the code.
  unsigned syndromeWidth_;     ///< Number of syndrome bits.
};

//===----------------------------------------------------------------------===//
// SteaneDecoder
//===----------------------------------------------------------------------===//

/// @brief Direct-formula decoder for the Steane [[7,1,3]] code.
///
/// The Steane code has the special property that the 3-bit Z-stabiliser
/// syndrome directly encodes the binary column index of the erroneous qubit
/// in H, and vice versa for X errors.  This gives an O(1) decoder with no
/// lookup table.
///
/// ### Syndrome layout
/// The 6-bit syndrome vector is split as [sx(3 bits) | sz(3 bits)]:
///  - `sz` (bits 3–5): identifies the qubit with an X error.
///  - `sx` (bits 0–2): identifies the qubit with a Z error.
///  - A Y error is detected as simultaneous X and Z on the same qubit.
///
/// ### H column encoding
/// @code{.unparsed}
///   Qubit 0: 0b100   Qubit 1: 0b010   Qubit 2: 0b110
///   Qubit 3: 0b001   Qubit 4: 0b101   Qubit 5: 0b011   Qubit 6: 0b111
/// @endcode
class SteaneDecoder : public SyndromeDecoder {
public:
  /// @brief Construct the Steane decoder.
  SteaneDecoder();

  /// @brief Decode a 6-bit Steane syndrome.
  ///
  /// @param syndrome 6-bit syndrome vector [sx | sz] (length must be 6).
  /// @return 7-qubit Pauli correction (at most weight 1).
  PauliString decode(llvm::ArrayRef<uint8_t> syndrome) const override;

  /// @brief Return the syndrome width (always 6 for the Steane code).
  unsigned getSyndromeWidth() const override { return 6; }

  /// @brief Decode only the X-error component from the Z-stabiliser syndrome.
  ///
  /// @param zSyndrome 3-bit Z-stabiliser syndrome byte.
  /// @param[out] errorQubit Set to the index of the erroneous qubit (0–6),
  ///                        or 7 if no X error.
  /// @return `Pauli::X` if an error was found, `Pauli::I` otherwise.
  Pauli decodeXError(uint8_t zSyndrome, unsigned &errorQubit) const;

  /// @brief Decode only the Z-error component from the X-stabiliser syndrome.
  ///
  /// @param xSyndrome 3-bit X-stabiliser syndrome byte.
  /// @param[out] errorQubit Set to the index of the erroneous qubit (0–6),
  ///                        or 7 if no Z error.
  /// @return `Pauli::Z` if an error was found, `Pauli::I` otherwise.
  Pauli decodeZError(uint8_t xSyndrome, unsigned &errorQubit) const;

private:
  /// @brief Steane H matrix column encoding: `kHColumns[j]` is the 3-bit
  ///        binary pattern that qubit j contributes to the syndrome.
  static constexpr uint8_t kHColumns[7] = {
    0b100, ///< Qubit 0: appears in row 2 only.
    0b010, ///< Qubit 1: appears in row 1 only.
    0b110, ///< Qubit 2: appears in rows 1 and 2.
    0b001, ///< Qubit 3: appears in row 0 only.
    0b101, ///< Qubit 4: appears in rows 0 and 2.
    0b011, ///< Qubit 5: appears in rows 0 and 1.
    0b111, ///< Qubit 6: appears in all three rows.
  };
};

//===----------------------------------------------------------------------===//
// Factory
//===----------------------------------------------------------------------===//

/// @brief Create the best decoder for the given stabiliser code.
///
/// Selection policy:
///  - Steane [[7,1,3]] → `SteaneDecoder` (direct formula, fastest).
///  - Codes with ≤ 20 stabilisers → `LookupTableDecoder` (O(1) decode).
///  - Larger codes → `MinWeightDecoder` (exhaustive, always correct).
///
/// @param code The stabiliser code to decode.
/// @return Owned pointer to the selected decoder, ready to use.
std::unique_ptr<SyndromeDecoder> createDecoder(const StabilizerCode &code);

} // namespace mlir::ftqc

#endif // FTQC_UTILS_SYNDROMEDECODER_H
