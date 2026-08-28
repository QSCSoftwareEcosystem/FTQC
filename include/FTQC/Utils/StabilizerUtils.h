//===- StabilizerUtils.h - Stabilizer formalism utilities -----------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file StabilizerUtils.h
/// @brief Stabiliser code representation, syndrome computation, and decoding
///        utilities for the FTQC dialect.
///
/// ### Stabiliser codes
/// An [[n,k,d]] stabiliser code encodes k logical qubits into n physical
/// qubits with minimum distance d.  It is defined by a set of n-k commuting
/// Pauli generators (stabilisers) whose common +1 eigenspace is the code space.
///
/// This file provides:
///  - `Pauli` enum and type aliases (`PauliString`, `Syndrome`, `BinaryRow`)
///  - `ClassicalCode` — classical linear binary [n,k,d] code
///  - `StabilizerCode` — general [[n,k,d]] code via its [Hx|Hz] parity-check matrix
///  - `steane::` namespace — constants for the [[7,1,3]] Steane code
///  - `surface::` namespace — plaquette geometry for rotated surface codes
///  - Free decoder functions: `minimumWeightDecoder`, `lookupDecoder`, `verifyCorrectionSuccess`
///
/// ### CSS codes
/// CSS (Calderbank-Shor-Steane) codes have separate X and Z stabiliser generators.
/// Their parity-check matrices satisfy HxHz^T = 0 (generators commute).
/// The Steane and surface codes are both CSS.
///
/// ### Usage
/// @code{.cpp}
/// StabilizerCode code = StabilizerCode::makeSteane();
/// PauliString error = {Pauli::X, Pauli::I, Pauli::I, Pauli::I,
///                      Pauli::I, Pauli::I, Pauli::I};
/// Syndrome syn = code.computeSyndrome(error);
/// PauliString correction = code.decode(syn);
/// bool ok = verifyCorrectionSuccess(code, error, correction);
/// @endcode
///
/// @see PauliUtils.h  — Pauli algebra primitives used here.
/// @see SyndromeDecoder.h — decoder classes wrapping these utilities.

#ifndef FTQC_UTILS_STABILIZERUTILS_H
#define FTQC_UTILS_STABILIZERUTILS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ArrayRef.h"
#include <cstdint>
#include <vector>
#include <string>

namespace mlir::ftqc {

//===----------------------------------------------------------------------===//
// Types
//===----------------------------------------------------------------------===//

/// @brief Single-qubit Pauli operator.
///
/// Values: I=0 (identity), X=1, Y=2, Z=3.  The encoding is the binary
/// symplectic representation: X bit = (value & 1), Z bit = (value >> 1).
enum class Pauli : uint8_t { I = 0, X = 1, Y = 2, Z = 3 };

/// @brief n-qubit Pauli string (tensor product of single-qubit Paulis).
using PauliString = llvm::SmallVector<Pauli, 16>;

/// @brief Binary row vector over GF(2) used in parity-check matrices.
using BinaryRow = llvm::SmallVector<uint8_t, 64>;

/// @brief Syndrome bit vector (output of a round of stabiliser measurements).
using Syndrome = llvm::SmallVector<uint8_t, 8>;

/// @brief Basis vector for a vector space.
struct BasisVector {
  /// @brief The basis vector.
  BinaryRow vector;
  /// @brief Index i such that vector[i] == 1, and vector[j] == 0 for all j > i.
  unsigned pivotIndex;
};

//===----------------------------------------------------------------------===//
// ClassicalCode
//===----------------------------------------------------------------------===//

/// @brief Represents an [n,k,d] classical linear binary code via its
/// parity-check matrix.
class ClassicalCode {
public:
  ClassicalCode() = default;

  /// @brief Construct a linear code from an explicit parity-check matrix.
  ///
  /// @param n Total number of physical bits.
  /// @param H Parity-check matrix, each row of length n.
  /// @return Fully constructed `ClassicalCode`.
  static ClassicalCode fromLinear(unsigned n, std::vector<BinaryRow> H);

  /// @brief Construct the [d,1,d] repetition code.
  ///
  /// The repetition code encodes 1 logical bit as a block of d identical bits.
  ///
  /// @param d Code distance.
  /// @return `ClassicalCode` representing the [d,1,d] repetition code.
  static ClassicalCode makeRepetition(unsigned n);

  /// @brief Construct a code using the transposed parity-check matrix.
  ///
  /// If this is an [n,k,d] code with an r×n parity-check matrix H, then the
  /// transpose code is an [r,r-rank(H),dT] code with parity-check matrix H^T.
  ///
  /// @return `ClassicalCode` representing the transpose code.
  ClassicalCode transposeCode() const;

  //------------------------------------------------------------------------
  // Accessors
  //------------------------------------------------------------------------

  unsigned getN() const { return n_; } ///< Total physical bit count.
  unsigned getK() const { return basis_.size(); } ///< Logical bit count.
  unsigned getD() const { return d_; } ///< Code distance.

  llvm::ArrayRef<BinaryRow> getH() const { return H_; } ///< Parity-check matrix.

  /// @brief Get a basis for the codespace, i.e. for the kernel of H.
  /// @return Array of basis vectors, each with a unique pivot index.
  llvm::ArrayRef<BasisVector> getBasis() const { return basis_; }

  /// @brief Serialise the code to a human-readable description.
  /// @param includeMatrix Whether to include the parity-check matrix.
  /// @return String showing [n,k,d] parameters and parity-check matrix.
  std::string toString(bool includeMatrix = false) const;

private:
  unsigned n_ = 0, d_ = 0;
  std::vector<BinaryRow> H_;
  std::vector<BasisVector> basis_;

  void computeBasis();
  void computeDistance();
};

//===----------------------------------------------------------------------===//
// StabilizerCode
//===----------------------------------------------------------------------===//

/// @brief Represents an [[n,k,d]] stabiliser code via its parity-check matrix.
///
/// For CSS codes, the parity-check matrix is stored as two separate matrices:
///  - `Hx` — X stabiliser generators (each row is an X-type stabiliser).
///  - `Hz` — Z stabiliser generators (each row is a Z-type stabiliser).
///
/// Both matrices are (n-k)/2 × n binary matrices satisfying Hx Hz^T = 0.
/// The syndrome is measured as [sx | sz] where:
///  - sx = Hx · e_Z mod 2  (Z-type errors detected by X stabilisers)
///  - sz = Hz · e_X mod 2  (X-type errors detected by Z stabilisers)
///
/// A lookup table (LUT) is built lazily on the first call to `decode()` or
/// `getSyndromeLookupTable()` for codes with ≤ 20 stabilisers.
class StabilizerCode {
public:
  StabilizerCode() = default;

  /// @brief Construct a CSS code from explicit Hx and Hz matrices.
  ///
  /// @param n  Total number of physical qubits.
  /// @param k  Number of encoded logical qubits.
  /// @param d  Code distance (minimum weight of a logical operator).
  /// @param Hx X-stabiliser generators: (n-k)/2 rows, each of length n.
  /// @param Hz Z-stabiliser generators: (n-k)/2 rows, each of length n.
  /// @return Fully constructed `StabilizerCode`.
  static StabilizerCode fromCSS(
      unsigned n, unsigned k, unsigned d,
      std::vector<BinaryRow> Hx,
      std::vector<BinaryRow> Hz);

  /// @brief Construct the Steane [[7,1,3]] code.
  ///
  /// The Steane code encodes 1 logical qubit into 7 physical qubits with
  /// distance 3 (corrects any single-qubit error).  Its parity-check matrix
  /// is the [7,4,3] Hamming code's parity-check matrix applied to both X and Z.
  ///
  /// @return `StabilizerCode` representing the Steane [[7,1,3]] code.
  static StabilizerCode makeSteane();

  /// @brief Construct a distance-d rotated surface code.
  ///
  /// The rotated surface code encodes 1 logical qubit into d² physical qubits
  /// with d² - 1 ancilla qubits for syndrome extraction.
  ///
  /// @param d Code distance (must be odd and ≥ 3 for a proper error-correcting code).
  /// @return `StabilizerCode` representing the [[d², 1, d]] rotated surface code.
  static StabilizerCode makeSurface(unsigned d);

  /// @brief Construct a concatenated code.
  ///
  /// The concatenation of an [[N,K,D]] outer code and an [[n,1,d]] inner code
  /// is constructed by using logical qubits encoded using the inner code as
  /// "physical" qubits for the outer code. It uses Nn physical qubits to encode
  /// K logical qubits, with code distance at least Dd.
  ///
  /// @param outer The outer stabilizer code.
  /// @param inner The inner stabilizer code.
  /// @return `StabilizerCode` representing the concatenated code.
  static StabilizerCode makeConcatenated(const StabilizerCode &outer,
                                         const StabilizerCode &inner);

  /// @brief Construct an n-qubit repetition code.
  ///
  /// The repetition code is an [[n,1,1]] code, because it protects against bit
  /// flips, but not phase flips.
  ///
  /// @param n Number of physical qubits.
  /// @return `StabilizerCode` representing the n-qubit repetition code.
  static StabilizerCode makeRepetition(unsigned n);

  /// @brief Construct the [[4,2,2]] error-detecting code.
  ///
  /// The [[4,2,2]] code detects any single-qubit error but cannot correct it.
  /// It is useful as a code-capacity check or for small demonstrations.
  ///
  /// @return `StabilizerCode` representing the [[4,2,2]] code.
  static StabilizerCode make422();

  /// @brief Construct a hypergraph product code.
  ///
  /// A hypergraph product code is constructed from two classical linear binary
  /// codes. If the codes have parameters [n1,k1,d1] and [n2,k2,d2], and the
  /// transpose codes have parameters [nT1,kT1,dT1] and [nT2,kT2,dT2], then the
  /// hypergraph product code has parameters
  /// [[n1*n2+nT1*nT2, k1*k2+kT1*kT2, min(d1,d2,dT1,dT2)]]. This makes it
  /// possible to achieve [[n,O(n),O(√n)]] scaling.
  ///
  /// @param code1 The first classical linear binary code.
  /// @param code2 The second classical linear binary code.
  /// @return `StabilizerCode` representing the hypergraph product code.
  static StabilizerCode makeHypergraphProduct(const ClassicalCode &code1,
                                              const ClassicalCode &code2);

  //------------------------------------------------------------------------
  // Accessors
  //------------------------------------------------------------------------

  unsigned getN() const { return n_; } ///< Total physical qubit count.
  unsigned getK() const { return k_; } ///< Logical qubit count.
  unsigned getD() const { return d_; } ///< Code distance.

  /// @brief Return the number of stabiliser generators (Hx rows + Hz rows).
  unsigned getNumStabilizers() const {
    return static_cast<unsigned>(Hx_.size() + Hz_.size());
  }

  llvm::ArrayRef<BinaryRow> getHx() const { return Hx_; } ///< X-stabiliser matrix rows.
  llvm::ArrayRef<BinaryRow> getHz() const { return Hz_; } ///< Z-stabiliser matrix rows.

  //------------------------------------------------------------------------
  // Syndrome and decoding
  //------------------------------------------------------------------------

  /// @brief Compute the syndrome for a Pauli error on the data qubits.
  ///
  /// Evaluates Hx · e_Z ⊕ Hz · e_X mod 2 to yield the 2*(n-k) syndrome bits.
  ///
  /// @param error n-qubit Pauli string (the physical error to evaluate).
  /// @return Syndrome bit vector [sx | sz] of length 2*(n-k).
  Syndrome computeSyndrome(llvm::ArrayRef<Pauli> error) const;

  /// @brief Find the minimum-weight Pauli correction for a syndrome.
  ///
  /// Uses the syndrome lookup table if available (built lazily for small codes);
  /// otherwise falls back to an exhaustive minimum-weight search.
  ///
  /// @param syndrome Syndrome bit vector returned by `computeSyndrome()`.
  /// @return Minimum-weight Pauli correction consistent with `syndrome`.
  PauliString decode(llvm::ArrayRef<uint8_t> syndrome) const;

  /// @brief Return the precomputed syndrome → correction lookup table.
  ///
  /// The table is indexed by the integer value of the syndrome bit vector
  /// (little-endian).  Built lazily on first access.
  ///
  /// @return Reference to the lookup table (length 2^numStabilizers).
  const std::vector<PauliString>& getSyndromeLookupTable() const;

  /// @brief Check whether a Pauli string is in the normaliser of the stabiliser group.
  ///
  /// A Pauli string is a *logical operator* if it commutes with all stabilisers
  /// but is not itself a stabiliser (it maps the code space to itself non-trivially).
  ///
  /// @param op n-qubit Pauli string to test.
  /// @return True if `op` is a non-trivial logical operator.
  bool isLogicalOperator(llvm::ArrayRef<Pauli> op) const;

  /// @brief Return the logical X operator for the specified encoded qubit.
  /// @param logicalIdx Index of the logical qubit (default 0).
  /// @return Reference to the stored logical X representative.
  const PauliString& getLogicalX(unsigned logicalIdx = 0) const;

  /// @brief Return the logical Z operator for the specified encoded qubit.
  /// @param logicalIdx Index of the logical qubit (default 0).
  /// @return Reference to the stored logical Z representative.
  const PauliString& getLogicalZ(unsigned logicalIdx = 0) const;

  /// @brief Serialise the code to a human-readable description.
  /// @param includeMatrices Whether to include the parity-check matrices.
  /// @param includeLogicalOps Whether to include the logical X and Z operators.
  /// @return String showing [[n,k,d]] parameters and parity-check matrices.
  std::string toString(bool includeMatrices = false,
                       bool includeLogicalOps = false) const;

  /// @brief Serialize the code's Tanner graph to Graphviz format.
  std::string toGraphviz() const;

private:
  unsigned n_ = 0, k_ = 0, d_ = 0;
  std::vector<BinaryRow> Hx_, Hz_;
  mutable std::vector<PauliString> syndromeLUT_; ///< Lazily built LUT.
  mutable bool lutBuilt_ = false;

  std::vector<PauliString> logX_, logZ_; ///< Representative logical operators.

  void buildLUT() const; ///< Build the syndrome lookup table.
};

//===----------------------------------------------------------------------===//
// Steane [[7,1,3]] code constants
//===----------------------------------------------------------------------===//

/// @brief Compile-time constants for the Steane [[7,1,3]] code.
///
/// The Steane code parity-check matrix H (3×7) is identical to the
/// [7,4,3] Hamming code's parity-check matrix:
/// @code{.unparsed}
///   H = [ 0 0 0 1 1 1 1 ]   (row 0)
///       [ 0 1 1 0 0 1 1 ]   (row 1)
///       [ 1 0 1 0 1 0 1 ]   (row 2)
/// @endcode
///
/// X stabilisers: apply X on each row's support.
/// Z stabilisers: apply Z on each row's support.
///
/// Logical operators:
///  - X_L = X₀X₁X₂   (weight-3 coset representative)
///  - Z_L = Z₀Z₁Z₂
namespace steane {
  constexpr unsigned kN = 7;           ///< Physical qubit count.
  constexpr unsigned kK = 1;           ///< Logical qubit count.
  constexpr unsigned kD = 3;           ///< Code distance.
  constexpr unsigned kStabilizers = 6; ///< Total stabilisers (3 X + 3 Z).

  /// @brief Qubit indices for each row of the Steane parity-check matrix.
  ///
  /// `kHrows[i]` is the set of qubit indices where row i of H is 1:
  ///  - Row 0: {3,4,5,6}
  ///  - Row 1: {1,2,5,6}
  ///  - Row 2: {0,2,4,6}
  inline const unsigned kHrows[3][4] = {
    {3, 4, 5, 6},
    {1, 2, 5, 6},
    {0, 2, 4, 6}
  };

  /// @brief Syndrome-to-error-qubit table for single X errors.
  ///
  /// Indexed by the 3-bit Z-stabiliser syndrome value.  Entry 7 indicates
  /// no error (syndrome = 0).  The actual decoder uses matrix inversion for
  /// correctness; this table is illustrative / fast-path for weight-1 errors.
  inline const unsigned kXCorrectionTable[8] = {
    7,  ///< 000 → no error.
    0,  ///< 001 → qubit 0.
    1,  ///< 010 → qubit 1.
    2,  ///< 011 → qubit 2.
    3,  ///< 100 → qubit 3.
    4,  ///< 101 → qubit 4.
    5,  ///< 110 → qubit 5.
    6,  ///< 111 → qubit 6.
  };
} // namespace steane

//===----------------------------------------------------------------------===//
// Surface code utilities
//===----------------------------------------------------------------------===//

/// @brief Geometry utilities for the rotated surface code.
///
/// The rotated surface code on a d×d lattice has:
///  - d² data qubits
///  - (d²-1)/2 X plaquettes (bulk) + boundary X plaquettes
///  - (d²-1)/2 Z plaquettes (bulk) + boundary Z plaquettes
///  - d²-1 ancilla qubits total
namespace surface {

/// @brief An X or Z stabiliser plaquette on the surface code lattice.
///
/// Bulk plaquettes act on 4 qubits; boundary plaquettes act on 2 or 3.
struct Plaquette {
  bool isX;             ///< True for X-type plaquette, false for Z-type.
  unsigned qubits[4];   ///< Qubit indices (up to 4); unused entries are 0.
  unsigned numQubits;   ///< Actual number of qubits in this plaquette (2–4).
};

/// @brief Generate the plaquette list for a distance-d rotated surface code.
///
/// Returns X plaquettes first, then Z plaquettes.  Qubit indices are
/// assigned in row-major order on the d×d lattice.
///
/// @param d Code distance (≥ 2).
/// @return List of Plaquette descriptors (total: d²-1 entries).
std::vector<Plaquette> generatePlaquettes(unsigned d);

/// @brief Number of data qubits for a distance-d surface code.
/// @param d Code distance.
/// @return d² data qubits.
inline unsigned physicalDataQubits(unsigned d)    { return d * d; }

/// @brief Number of ancilla qubits for a distance-d surface code.
/// @param d Code distance.
/// @return d²-1 ancilla qubits (one per plaquette).
inline unsigned physicalAncillaQubits(unsigned d) { return d * d - 1; }

/// @brief Total physical qubit count for a distance-d surface code.
/// @param d Code distance.
/// @return 2d²-1 qubits (data + ancilla).
inline unsigned physicalTotalQubits(unsigned d)   { return 2 * d * d - 1; }

} // namespace surface

//===----------------------------------------------------------------------===//
// Generic decoder free functions
//===----------------------------------------------------------------------===//

/// @brief Minimum-weight perfect matching decoder (exhaustive, for small codes).
///
/// Searches for the minimum-weight Pauli correction consistent with the
/// syndrome by exhaustive enumeration.  Correct by definition but exponential
/// in the code size.  Suitable for verification and codes with n ≤ 15.
///
/// @param code     The stabiliser code.
/// @param syndrome Measured syndrome bits [sx | sz].
/// @return Minimum-weight Pauli correction string.
PauliString minimumWeightDecoder(const StabilizerCode &code,
                                 llvm::ArrayRef<uint8_t> syndrome);

/// @brief Lookup-table decoder (exact, O(1) after precomputation).
///
/// Decodes by table lookup; the table is built on first call and cached.
/// Suitable for codes with at most 16 stabilisers (64KB table).
///
/// @param code     The stabiliser code (must have ≤ 20 stabilisers).
/// @param syndrome Measured syndrome bits.
/// @return Correction Pauli string from the precomputed table.
PauliString lookupDecoder(const StabilizerCode &code,
                          llvm::ArrayRef<uint8_t> syndrome);

/// @brief Verify that a correction successfully eliminates an error.
///
/// Checks that `error · correction` is in the stabiliser group (i.e., the
/// combined Pauli is undetectable and not a logical error).
///
/// @param code       The stabiliser code.
/// @param error      The original Pauli error that occurred.
/// @param correction The correction Pauli returned by a decoder.
/// @return True if `error · correction` is a stabiliser (correction succeeds).
bool verifyCorrectionSuccess(const StabilizerCode &code,
                             llvm::ArrayRef<Pauli> error,
                             llvm::ArrayRef<Pauli> correction);

} // namespace mlir::ftqc

#endif // FTQC_UTILS_STABILIZERUTILS_H
