//===- PauliUtils.h - Pauli group and Clifford utilities ------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file PauliUtils.h
/// @brief Pauli group algebra, binary symplectic representation, and Clifford
///        tableau utilities used throughout the FTQC codebase.
///
/// ### Pauli group overview
/// The single-qubit Pauli group is {I, X, Y, Z} × {1, i, -1, -i}.
/// The n-qubit Pauli group consists of tensor products of single-qubit Paulis
/// with a global phase.  Physically, only the support matters for error
/// correction (the phase is classical), so phases are tracked separately.
///
/// ### Binary symplectic representation
/// An n-qubit Pauli P = i^k X^a Z^b is encoded as the 2n-bit binary vector
/// @code{.unparsed}
///   [a₀ … aₙ₋₁ | b₀ … bₙ₋₁]   (a,b ∈ GF(2)ⁿ)
/// @endcode
/// This reduces Pauli multiplication to XOR and phase tracking to a scalar
/// product, enabling efficient algorithms for stabiliser simulation and decoding.
///
/// ### Clifford tableau (Aaronson-Gottesman)
/// `CliffordTableau` tracks how an n-qubit Clifford C transforms each generator
/// of the Pauli group.  Applying a Clifford to an error P gives C P C†.
/// The tableau is stored as a 2n × (2n+1) binary matrix (destabiliser rows
/// followed by stabiliser rows, plus a phase column).
///
/// ### Usage
/// @code{.cpp}
/// // Single-qubit product
/// auto [result, phase] = pauliProduct(Pauli::X, Pauli::Z);  // result=Y, phase=3 (-i)
///
/// // Propagate an error through a Clifford
/// CliffordTableau tab(2);
/// tab.applyCNOT(0, 1);
/// auto img = tab.conjugate({Pauli::X, Pauli::I});  // CNOT: XI → XX
/// @endcode
///
/// @see StabilizerUtils.h — stabiliser code representation.
/// @see SyndromeDecoder.h — syndrome decoding using these primitives.

#ifndef FTQC_UTILS_PAULIUTILS_H
#define FTQC_UTILS_PAULIUTILS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <string>
#include <vector>

namespace mlir::ftqc {

//===----------------------------------------------------------------------===//
// Pauli operator algebra
//===----------------------------------------------------------------------===//

enum class Pauli : uint8_t;  // forward-declared in StabilizerUtils.h

/// @brief Multiply two single-qubit Paulis.
///
/// Returns the product Pauli and the accumulated phase, where phase ∈ {0,1,2,3}
/// represents the complex factor {1, i, -1, -i}.
///
/// Product table (ignoring phase):
/// | ⊗ | I | X | Y | Z |
/// |---|---|---|---|---|
/// | I | I | X | Y | Z |
/// | X | X | I | Z | Y |
/// | Y | Y | Z | I | X |
/// | Z | Z | Y | X | I |
///
/// @param a First Pauli.
/// @param b Second Pauli.
/// @return `{a·b, phase}` where phase encodes the scalar factor.
std::pair<Pauli, int> pauliProduct(Pauli a, Pauli b);

/// @brief Check whether two single-qubit Paulis commute.
///
/// Two distinct non-identity Paulis anti-commute (return false);
/// identical Paulis or any Pauli with I always commute.
///
/// @param a First Pauli.
/// @param b Second Pauli.
/// @return True if a and b commute ([a, b] = 0).
bool pauliCommute(Pauli a, Pauli b);

/// @brief Convert a Pauli to its string name.
/// @param p A single-qubit Pauli.
/// @return One of "I", "X", "Y", "Z".
llvm::StringRef pauliToString(Pauli p);

/// @brief Parse a Pauli from its string name ("I", "X", "Y", "Z").
/// @param s String to parse (case-sensitive).
/// @return Corresponding Pauli; behaviour is undefined for unrecognised input.
Pauli stringToPauli(llvm::StringRef s);

/// @brief Tensor product of two Pauli strings (concatenation).
///
/// The result has length `a.size() + b.size()`, with `a` occupying the
/// first (lower-index) qubits.
///
/// @param a Left Pauli string (m qubits).
/// @param b Right Pauli string (n qubits).
/// @return n+m qubit Pauli string a ⊗ b.
llvm::SmallVector<Pauli, 32> tensorProduct(llvm::ArrayRef<Pauli> a,
                                            llvm::ArrayRef<Pauli> b);

/// @brief Element-wise (site-by-site) multiplication of two Pauli strings.
///
/// Both strings must have the same length.  The result is the element-wise
/// Pauli product; the net phase accumulates contributions from each site.
///
/// @param a First n-qubit Pauli string.
/// @param b Second n-qubit Pauli string (same length as `a`).
/// @return `{result, net_phase}` where net_phase ∈ {0,1,2,3}.
std::pair<llvm::SmallVector<Pauli, 32>, int>
multiplyPauliStrings(llvm::ArrayRef<Pauli> a, llvm::ArrayRef<Pauli> b);

/// @brief Check whether two n-qubit Pauli strings commute.
///
/// Two Pauli strings commute iff their site-wise commutators cancel, i.e.
/// the number of sites where they anti-commute is even.
///
/// @param a First n-qubit Pauli string.
/// @param b Second n-qubit Pauli string (same length).
/// @return True if a and b commute.
bool pauliStringsCommute(llvm::ArrayRef<Pauli> a, llvm::ArrayRef<Pauli> b);

/// @brief Compute the Hamming weight (support size) of a Pauli string.
///
/// Counts the number of non-identity entries.  For an error operator, this
/// is the physical error weight used in minimum-weight decoding.
///
/// @param p n-qubit Pauli string.
/// @return Number of qubits where p is not I.
unsigned pauliWeight(llvm::ArrayRef<Pauli> p);

/// @brief Conjugate a Pauli string by a named Clifford gate.
///
/// Applies the transformation P → G P G† where G is one of H, S, CNOT, CZ, X,
/// Y, Z, Sdg.  Only the qubit(s) specified by `targetQubit` (and `controlQubit`
/// for two-qubit gates) are updated; other qubits are left unchanged.
///
/// @param p           n-qubit Pauli string.
/// @param gate        Gate name: "H", "S", "Sdg", "X", "Y", "Z", "CNOT", "CZ".
/// @param targetQubit Index of the target qubit (0-indexed).
/// @param controlQubit Index of the control qubit (only for CNOT/CZ; default 0).
/// @return Conjugated n-qubit Pauli string G P G†.
llvm::SmallVector<Pauli, 32> conjugateByGate(llvm::ArrayRef<Pauli> p,
                                              llvm::StringRef gate,
                                              unsigned targetQubit,
                                              unsigned controlQubit = 0);

//===----------------------------------------------------------------------===//
// Binary symplectic representation
//===----------------------------------------------------------------------===//
//
// An n-qubit Pauli P = i^k X^a Z^b is represented as a 2n-bit binary vector
// [a | b] where a,b ∈ GF(2)^n.
//
// The symplectic inner product Λ(u,v) = a_u · b_v ⊕ b_u · a_v determines
// whether two Paulis commute (Λ = 0) or anticommute (Λ = 1).

/// @brief Convert a Pauli string to its binary symplectic representation.
///
/// The output is a 2n-element byte vector where:
///  - bytes 0…n-1 are the X bits (1 if P[i] ∈ {X, Y}),
///  - bytes n…2n-1 are the Z bits (1 if P[i] ∈ {Z, Y}).
///
/// @param p n-qubit Pauli string.
/// @return 2n-element GF(2) vector [X-part | Z-part].
std::vector<uint8_t> toSymplectic(llvm::ArrayRef<Pauli> p);

/// @brief Convert a binary symplectic vector back to a Pauli string.
///
/// The input must have even length 2n.  Bytes 0…n-1 are interpreted as X bits
/// and bytes n…2n-1 as Z bits.
///
/// @param sv 2n-element binary vector [X-part | Z-part].
/// @return Equivalent n-qubit Pauli string.
llvm::SmallVector<Pauli, 32> fromSymplectic(llvm::ArrayRef<uint8_t> sv);

/// @brief Compute the symplectic inner product of two binary symplectic vectors.
///
/// Λ(u, v) = (aᵤ · bᵥ) ⊕ (bᵤ · aᵥ) mod 2, where u = [aᵤ | bᵤ],
/// v = [aᵥ | bᵥ].  The result is 0 iff the two Paulis commute.
///
/// @param u First 2n-bit symplectic vector.
/// @param v Second 2n-bit symplectic vector (same length as `u`).
/// @return 0 if commuting, 1 if anti-commuting.
uint8_t symplecticInnerProduct(llvm::ArrayRef<uint8_t> u,
                               llvm::ArrayRef<uint8_t> v);

/// @brief GF(2) Gaussian elimination on a binary matrix.
///
/// Reduces @p matrix to row echelon form over GF(2) using elementary row
/// operations (XOR).  Used to find the rank of parity-check matrices and
/// to solve Ax = s for syndrome decoding.
///
/// @param matrix Rectangular binary matrix (rows × cols) to reduce.
/// @return Row echelon form of @p matrix (in-place copy).
std::vector<std::vector<uint8_t>>
gaussianEliminationGF2(std::vector<std::vector<uint8_t>> matrix);

/// @brief Compute the null space of a GF(2) matrix.
///
/// Returns a basis for ker(M) = {x : Mx = 0 mod 2}.  For a stabiliser
/// parity-check matrix H, the null space gives the logical operators.
///
/// @param matrix Binary matrix (rows × cols).
/// @return Basis vectors of the null space; each vector has `cols` entries.
std::vector<std::vector<uint8_t>>
nullSpaceGF2(const std::vector<std::vector<uint8_t>> &matrix);

//===----------------------------------------------------------------------===//
// Clifford tableau (Aaronson-Gottesman)
//===----------------------------------------------------------------------===//

/// @brief Minimal Clifford tableau for tracking Pauli propagation through
///        Clifford circuits.
///
/// Implements the Aaronson-Gottesman stabiliser formalism.  The tableau
/// stores a 2n × (2n+1) binary matrix:
///  - Rows 0…n-1: *destabilisers* — generators of the destabiliser group.
///  - Rows n…2n-1: *stabilisers* — generators of the stabiliser group.
///  - Column 2n: phase bit (0 = +1, 1 = -1 for each generator).
///
/// ### Complexity
///  - Gate application: O(n).
///  - Pauli conjugation: O(n²).
///  - Measurement: O(n²) amortised.
struct CliffordTableau {
  unsigned n; ///< Number of qubits.

  /// @brief 2n × (2n+1) binary tableau (rows = generators, cols = X|Z|phase).
  std::vector<std::vector<uint8_t>> tableau;
  std::vector<int> phases; ///< Phase per row: 0 = +1, 1 = −1.

  /// @brief Construct the identity tableau for @p numQubits qubits.
  /// @param numQubits Number of logical qubits to track.
  explicit CliffordTableau(unsigned numQubits);

  /// @name Single-qubit Clifford gates
  /// Each gate updates the tableau rows that anticommute with the applied gate.
  ///@{
  void applyH(unsigned q);   ///< Hadamard on qubit q: X↔Z.
  void applyS(unsigned q);   ///< Phase gate on qubit q: X→Y, Z→Z.
  void applyX(unsigned q);   ///< Pauli X on qubit q: Z→-Z.
  void applyY(unsigned q);   ///< Pauli Y on qubit q: X→-X, Z→-Z.
  void applyZ(unsigned q);   ///< Pauli Z on qubit q: X→-X.
  void applySdg(unsigned q); ///< S† on qubit q: X→-Y, Z→Z.
  ///@}

  /// @name Two-qubit Clifford gates
  ///@{
  /// @brief Controlled-NOT gate.
  /// @param ctrl Control qubit index.
  /// @param tgt  Target qubit index.
  void applyCNOT(unsigned ctrl, unsigned tgt);

  /// @brief Controlled-Z gate.
  /// @param ctrl First qubit index.
  /// @param tgt  Second qubit index.
  void applyCZ(unsigned ctrl, unsigned tgt);
  ///@}

  /// @brief Propagate a Pauli error through the tracked Clifford: C P C†.
  ///
  /// Given a Pauli string P on n qubits, returns the image of P under
  /// conjugation by the Clifford represented by this tableau.
  ///
  /// @param p n-qubit Pauli string (must have length n).
  /// @return Image Pauli string C P C† (same length as `p`).
  llvm::SmallVector<Pauli, 32>
  conjugate(llvm::ArrayRef<Pauli> p) const;

  /// @brief Simulate a Z-basis measurement on qubit @p q.
  ///
  /// If the measurement outcome is deterministic (qubit is in a stabiliser
  /// eigenstate), returns 0 or 1.  If the outcome is random, collapses the
  /// state and returns the chosen outcome.
  ///
  /// @param q Qubit index to measure.
  /// @return Measurement outcome (0 or 1), or -1 if the state is indeterminate.
  int measureZ(unsigned q);

  /// @brief Serialise the tableau to a human-readable string.
  /// @return Multi-line string showing each row as a Pauli string + phase.
  std::string toString() const;
};

//===----------------------------------------------------------------------===//
// Transversal gate helpers
//===----------------------------------------------------------------------===//

/// @brief Check whether a logical gate has a transversal physical implementation.
///
/// A gate G is *transversal* for code C if it can be implemented by applying
/// a physical gate independently to each qubit of the code block, without
/// coupling qubits within the block.  Transversal gates are inherently
/// fault-tolerant because errors do not spread between physical qubits.
///
/// @param gateName  Logical gate name (e.g., "H", "CNOT", "T").
/// @param codeKind  ECC family string (e.g., "steane", "surface").
/// @return True if the gate is transversal for the specified code.
bool hasTransversalImplementation(llvm::StringRef gateName,
                                  llvm::StringRef codeKind);

/// @brief Build the physical qubit circuit for a transversal gate.
///
/// Returns the ordered sequence of physical gate operations that implement
/// the transversal logical gate on a code block of `blockSize` qubits.
/// Each entry is `(gate_name, physical_qubit_index)`.
///
/// @param gateName  Logical gate to implement (must be transversal for the code).
/// @param blockSize Physical qubits in the code block (e.g. 7 for Steane).
/// @param codeKind  ECC family string (e.g., "steane", "surface").
/// @return List of `(physical_gate, qubit_index)` pairs in application order.
std::vector<std::pair<std::string, unsigned>>
buildTransversalGateCircuit(llvm::StringRef gateName,
                            unsigned blockSize,
                            llvm::StringRef codeKind);

} // namespace mlir::ftqc

#endif // FTQC_UTILS_PAULIUTILS_H
