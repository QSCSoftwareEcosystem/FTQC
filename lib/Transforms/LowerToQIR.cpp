// LowerToQIR.cpp - FTQC → QIR Base Profile LLVM IR translation //
//
// Purpose: Translates a post-pipeline FTQC module to QIR (*.ll)
// with full physical-qubit expansion.
//
// Physical expansion supported error-correcting codes:
//
// ═══════════════════════════════════════════════════════════════════════════
// Steane [[7,1,3]]:
// ═══════════════════════════════════════════════════════════════════════════
// Each `!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>` is lowered to a BLOCK of
// 7 physical qubits in QIR.  The logical operations are lowered as follows:
//
//   ftqc.init_zero  → 7 physical qubits + Steane [[7,1,3]] encoding circuit:
//                       h q[0]; h q[1]; h q[3];
//                       cnot q[0],q[2]; cnot q[0],q[4]; cnot q[0],q[6];
//                       cnot q[1],q[2]; cnot q[1],q[5]; cnot q[1],q[6];
//                       cnot q[3],q[4]; cnot q[3],q[5]; cnot q[3],q[6];
//
//   ftqc.init_plus  → same as init_zero, then transversal H (7 extra H gates)
//
//   ftqc.logical_{h,x,y,z,s,t}  → transversal single-qubit gate on all 7
//                                   physical qubits of the logical block
//
//   ftqc.logical_cnot            → 7 transversal CNOT gates (ctrl[i]→tgt[i])
//   ftqc.logical_cz              → 7 transversal CZ  gates
//   ftqc.logical_toffoli         → 7 transversal CCX gates
//
//   ftqc.logical_measure         → mz on all 7 physical qubits (7 results);
//                                   logical outcome = parity of the 7 bits
//                                   (recorded as 7 separate results; classical
//                                    post-processing computes the XOR)
//
// ═══════════════════════════════════════════════════════════════════════════
// Surface Code (distance d):
// ═══════════════════════════════════════════════════════════════════════════
// Each `!ftqc.logical_qubit<#ftqc.ecc<surface, d>>` is lowered to a BLOCK of
// d² physical qubits arranged in a d×d rotated lattice.  The logical
// operations are lowered as follows (with qubits indexed row-major as
// q[r][c] = base + r*d + c):
//
//   ftqc.init_zero  → d² physical qubits + Surface code encoding:
//                       (1) Hadamard on first row: h q[0][0..d-1]
//                       (2) Vertical CNOTs: cnot q[r][c],q[r+1][c]
//                           for all rows r=0..d-2, cols c=0..d-1
//                       (3) Horizontal CNOTs: cnot q[r][c],q[r][c+1]
//                           for all rows r=0..d-1, cols c=0..d-2
//
//   ftqc.init_plus  → same as init_zero, then transversal H (d² extra H gates)
//
//   ftqc.logical_{h,x,y,z,s,t}  → transversal single-qubit gate on all d²
//                                   physical qubits of the logical block
//
//   ftqc.logical_cnot            → d² transversal CNOT gates
//   ftqc.logical_cz              → d² transversal CZ  gates
//   ftqc.logical_toffoli         → d² transversal CCX gates
//
//   ftqc.logical_measure         → mz on all d² physical qubits (d² results);
//                                   logical outcome = parity of the d² bits
//                                   (recorded as d² separate results; classical
//                                    post-processing computes the XOR)
//
// FT-infrastructure ops (syndrome, correction, Pauli-frame) emit LLVM IR line
// comments when --include-comments=true (default).
//
// For unrecognized codes the pass falls back to 1-to-1 logical→physical
// mapping with a warning comment in the generated IR.
//
// Output conforms to QIR Base Profile v1.0:
//   https://github.com/qir-alliance/qir-spec
//
// Qubit / result pointer convention:
//   index 0  →  ptr null
//   index N  →  ptr inttoptr (i64 N to ptr)
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_FTQCLOWERTOQIRPASS
#include "FTQC/Transforms/Passes.h.inc"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <string>
#include <system_error>
#include <vector>

using namespace mlir;
using namespace mlir::ftqc;

//----------------------------------------------------------------------//
// QIREmitter
//----------------------------------------------------------------------//

namespace {

// ---------------------------------------------------------------------------
// Physical qubit block bookkeeping
//
// A logical qubit value maps to a contiguous block of n physical qubit
// indices.  For Steane [[7,1,3]], n = 7.  For other codes the block size is
// read from the type attribute; unrecognised codes fall back to n = 1.
// ---------------------------------------------------------------------------

using PhysBlock = std::vector<unsigned>; // indices of physical qubits

/// Per-function emission result.
struct QIRFuncResult {
  std::string funcName;
  std::string body;
  std::set<std::string> decls;
  unsigned numQubits  = 0;
  unsigned numResults = 0;
};

class QIREmitter {
public:
  explicit QIREmitter(bool includeComments)
      : includeComments(includeComments) {}

  std::string emitModule(ModuleOp mod) {
    auto moduleId = mod.getName().value_or("ftqc_module");

    std::vector<QIRFuncResult> results;
    for (auto func : mod.getOps<func::FuncOp>())
      results.push_back(emitFunc(func));

    std::set<std::string> allDecls;
    for (auto &r : results)
      allDecls.insert(r.decls.begin(), r.decls.end());

    allDecls.insert("declare void @__quantum__rt__initialize(ptr)");
    allDecls.insert("declare void @__quantum__rt__array_record_output(i64, ptr)");
    allDecls.insert("declare void @__quantum__rt__result_record_output(ptr, ptr)");

    bool hasMeas = allDecls.count(
        "declare void @__quantum__qis__mz__body(ptr, ptr writeonly) #1");

    unsigned totalQubits  = 0;
    unsigned totalResults = 0;
    for (auto &r : results) {
      totalQubits  = std::max(totalQubits,  r.numQubits);
      totalResults = std::max(totalResults, r.numResults);
    }

    std::string out;
    llvm::raw_string_ostream os(out);

    os << "; ModuleID = '" << moduleId << "'\n";
    os << "source_filename = \"" << moduleId << "\"\n\n";

    for (auto &r : results) {
      os << "define void @" << r.funcName << "() #0 {\n";
      os << "entry:\n";
      os << "  call void @__quantum__rt__initialize(ptr null)\n";
      os << r.body;
      // Record all measurement results (Base Profile: measurements before
      // classical results).
      os << "  call void @__quantum__rt__array_record_output(i64 "
         << r.numResults << ", ptr null)\n";
      for (unsigned i = 0; i < r.numResults; ++i)
        os << "  call void @__quantum__rt__result_record_output("
           << pref(i) << ", ptr null)\n";
      os << "  ret void\n";
      os << "}\n\n";
    }

    for (const auto &d : allDecls)
      os << d << "\n";
    os << "\n";

    os << "attributes #0 = { \"entry_point\" \"output_labeling_schema\" "
       << "\"qir_profiles\"=\"base\" "
       << "\"required_num_qubits\"=\"" << totalQubits << "\" "
       << "\"required_num_results\"=\"" << totalResults << "\" }\n";
    if (hasMeas)
      os << "attributes #1 = { \"irreversible\" }\n";
    os << "\n";

    os << "!llvm.module.flags = !{!0, !1, !2, !3}\n\n";
    os << "!0 = !{i32 1, !\"qir_major_version\", i32 1}\n";
    os << "!1 = !{i32 7, !\"qir_minor_version\", i32 0}\n";
    os << "!2 = !{i32 1, !\"dynamic_qubit_management\", i1 false}\n";
    os << "!3 = !{i32 1, !\"dynamic_result_management\", i1 false}\n";

    return out;
  }

private:
  bool includeComments;

  // Per-function state (reset by emitFunc).
  DenseMap<Value, PhysBlock> blockMap; // logical SSA val → physical indices
  unsigned nextQubit  = 0;
  unsigned nextResult = 0;

  void reset() {
    blockMap.clear();
    nextQubit  = 0;
    nextResult = 0;
  }

  // -------------------------------------------------------------------------
  // Physical pointer helpers
  // -------------------------------------------------------------------------
  static std::string qref(unsigned idx) {
    if (idx == 0) return "ptr null";
    return "ptr inttoptr (i64 " + std::to_string(idx) + " to ptr)";
  }
  static std::string pref(unsigned idx) { return qref(idx); }

  // -------------------------------------------------------------------------
  // Block management
  // -------------------------------------------------------------------------

  /// Allocate n consecutive physical qubit indices and return them.
  PhysBlock allocBlock(unsigned n) {
    PhysBlock blk(n);
    for (unsigned i = 0; i < n; ++i)
      blk[i] = nextQubit++;
    return blk;
  }

  /// Register `v` to a block; return that block.
  const PhysBlock &assignBlock(Value v, PhysBlock blk) {
    blockMap[v] = std::move(blk);
    return blockMap[v];
  }

  /// Look up (or lazily create 1-qubit block) for v.
  const PhysBlock &getBlock(Value v) {
    auto it = blockMap.find(v);
    if (it != blockMap.end()) return it->second;
    // Fallback: allocate a single-qubit block.
    return assignBlock(v, allocBlock(1));
  }

  /// Propagate: newV gets the same physical block as oldV.
  void propagate(Value newV, Value oldV) {
    auto it = blockMap.find(oldV);
    if (it != blockMap.end())
      blockMap[newV] = it->second;
  }

  /// Determine physical block size from a LogicalQubitType (or 1 if unknown).
  static unsigned blockSizeFor(Type t) {
    auto lqt = dyn_cast<LogicalQubitType>(t);
    if (!lqt) return 1;
    return lqt.getCode().getPhysicalQubits();
  }

  static bool isSteane(Type t) {
    auto lqt = dyn_cast<LogicalQubitType>(t);
    if (!lqt) return false;
    return lqt.getCode().getKind() == ECCKind::Steane;
  }

  static bool isSurface(Type t) {
    auto lqt = dyn_cast<LogicalQubitType>(t);
    if (!lqt) return false;
    return lqt.getCode().getKind() == ECCKind::Surface;
  }

  static bool isColorCode(Type t) {
    auto lqt = dyn_cast<LogicalQubitType>(t);
    if (!lqt) return false;
    return lqt.getCode().getKind() == ECCKind::ColorCode;
  }

  static unsigned getSurfaceDistance(Type t) {
    auto lqt = dyn_cast<LogicalQubitType>(t);
    if (!lqt) return 0;
    return lqt.getCode().getDistance();
  }

  // -------------------------------------------------------------------------
  // Gate emission helpers
  // -------------------------------------------------------------------------
  void emitComment(llvm::raw_ostream &os, const char *msg) {
    if (includeComments) os << "  ; " << msg << "\n";
  }

  void gate1q(llvm::raw_ostream &os, std::set<std::string> &decls,
              const char *name, unsigned q) {
    std::string fn = std::string("@__quantum__qis__") + name + "__body";
    os << "  call void " << fn << "(" << qref(q) << ")\n";
    decls.insert("declare void " + fn + "(ptr)");
  }

  void gate2q(llvm::raw_ostream &os, std::set<std::string> &decls,
              const char *name, unsigned q0, unsigned q1) {
    std::string fn = std::string("@__quantum__qis__") + name + "__body";
    os << "  call void " << fn << "(" << qref(q0) << ", " << qref(q1) << ")\n";
    decls.insert("declare void " + fn + "(ptr, ptr)");
  }

  void gate3q(llvm::raw_ostream &os, std::set<std::string> &decls,
              const char *name, unsigned q0, unsigned q1, unsigned q2) {
    std::string fn = std::string("@__quantum__qis__") + name + "__body";
    os << "  call void " << fn << "(" << qref(q0) << ", " << qref(q1)
       << ", " << qref(q2) << ")\n";
    decls.insert("declare void " + fn + "(ptr, ptr, ptr)");
  }

  // -------------------------------------------------------------------------
  // Steane [[7,1,3]] encoding circuit
  //
  // Produces |0_L> from 7 |0> ancilla qubits starting at base index `b`:
  //   H on b+0, b+1, b+3
  //   CNOT(b+0, b+2), CNOT(b+0, b+4), CNOT(b+0, b+6)
  //   CNOT(b+1, b+2), CNOT(b+1, b+5), CNOT(b+1, b+6)
  //   CNOT(b+3, b+4), CNOT(b+3, b+5), CNOT(b+3, b+6)
  // -------------------------------------------------------------------------
  void emitSteaneEncoding(llvm::raw_ostream &os, std::set<std::string> &decls,
                           unsigned b) {
    os << "  ; --- Steane [[7,1,3]] encoding: encode |0_L> ---\n";
    // Hadamards on generators (qubits 0, 1, 3 within the block).
    gate1q(os, decls, "h", b + 0);
    gate1q(os, decls, "h", b + 1);
    gate1q(os, decls, "h", b + 3);
    // CNOT cascade.
    gate2q(os, decls, "cnot", b + 0, b + 2);
    gate2q(os, decls, "cnot", b + 0, b + 4);
    gate2q(os, decls, "cnot", b + 0, b + 6);
    gate2q(os, decls, "cnot", b + 1, b + 2);
    gate2q(os, decls, "cnot", b + 1, b + 5);
    gate2q(os, decls, "cnot", b + 1, b + 6);
    gate2q(os, decls, "cnot", b + 3, b + 4);
    gate2q(os, decls, "cnot", b + 3, b + 5);
    gate2q(os, decls, "cnot", b + 3, b + 6);
    os << "  ; --- end Steane encoding ---\n";
  }

  // -------------------------------------------------------------------------
  // Surface code encoding circuit (distance d)
  //
  // Produces |0_L> from d² physical qubits arranged in a d×d rotated surface code.
  // 
  // Algorithm outline:
  //   - Qubits indexed in row-major order: q[row][col] = b + row*d + col
  //   - Step 1: Hadamard on first row (data qubits)
  //   - Step 2: Vertical CNOT chains (column-wise entanglement)
  //             For each column: CNOT(q[r][c], q[r+1][c]) for r=0..d-2
  //   - Step 3: Horizontal CNOT chains (row-wise entanglement)
  //             For each row: CNOT(q[r][c], q[r][c+1]) for c=0..d-2
  //
  // Result: A d×d lattice of entangled qubits with plaquette structure
  // suitable for syndrome measurement and error correction.
  // -------------------------------------------------------------------------
  void emitSurfaceEncoding(llvm::raw_ostream &os, std::set<std::string> &decls,
                            unsigned b, unsigned d) {
    os << "  ; --- Surface code (d=" << d << ") encoding: encode |0_L> ---\n";
    
    // Step 1: Apply Hadamard on first row of qubits
    os << "  ; Initialize first row with Hadamard\n";
    for (unsigned i = 0; i < d; ++i) {
      gate1q(os, decls, "h", b + i);
    }
    
    // Step 2: Create vertical CNOT chains (column-wise)
    // Each column has a chain of CNOTs to entangle qubits
    os << "  ; Create vertical entanglement (column-wise CNOT chains)\n";
    for (unsigned col = 0; col < d; ++col) {
      for (unsigned row = 0; row < d - 1; ++row) {
        unsigned q1 = b + row * d + col;
        unsigned q2 = b + (row + 1) * d + col;
        gate2q(os, decls, "cnot", q1, q2);
      }
    }
    
    // Step 3: Create horizontal CNOT chains (row-wise)
    // Each row has alternating CNOTs to create plaquette structure
    os << "  ; Create horizontal entanglement (row-wise CNOT chains)\n";
    for (unsigned row = 0; row < d; ++row) {
      for (unsigned col = 0; col < d - 1; ++col) {
        unsigned q1 = b + row * d + col;
        unsigned q2 = b + row * d + (col + 1);
        gate2q(os, decls, "cnot", q1, q2);
      }
    }
    
    os << "  ; --- end Surface code encoding ---\n";
  }

  // -------------------------------------------------------------------------
  // 2D triangular color-code encoding circuit (distance d)
  //
  // Physical block size is N(d) = (3d^2 - 3d + 2)/2.
  //
  // This emitter constructs a triangular-style connectivity pattern over the
  // allocated block by arranging qubits in d rows with increasing row lengths,
  // then applying:
  //   1) H on the first row,
  //   2) horizontal CNOT chains per row,
  //   3) inter-row CNOTs to two neighbors in the next row.
  //
  // The intent is to produce an explicit color-code-specific entangling network
  // (rather than generic fallback / no-op encoding) while preserving full
  // compatibility with existing block sizing and transversal gate lowering.
  // -------------------------------------------------------------------------
  void emitColorCodeEncoding(llvm::raw_ostream &os,
                             std::set<std::string> &decls,
                             unsigned b,
                             unsigned d,
                             unsigned n) {
    os << "  ; --- 2D triangular color-code (d=" << d
       << ") encoding: encode |0_L> ---\n";

    if (n == 0) {
      os << "  ; [warn] empty color-code block\n";
      os << "  ; --- end color-code encoding ---\n";
      return;
    }

    // Row lengths chosen to match N(d) for the current dialect formula:
    //   row[0] = d
    //   row[r] = d + r - 1, for r in [1, d-1]
    // Sum = d + sum_{k=d}^{2d-2} k = (3d^2 - 3d + 2)/2.
    std::vector<unsigned> rowLens;
    if (d <= 1) {
      rowLens.push_back(n);
    } else {
      rowLens.reserve(d);
      rowLens.push_back(d);
      for (unsigned r = 1; r < d; ++r)
        rowLens.push_back(d + r - 1);
    }

    unsigned planned = 0;
    for (unsigned len : rowLens)
      planned += len;

    // Robustness guard: if a future formula changes N(d), keep lowering valid.
    if (planned != n) {
      os << "  ; [warn] color-code row plan size mismatch (planned=" << planned
         << ", block=" << n << "), using single-row fallback\n";
      rowLens.clear();
      rowLens.push_back(n);
    }

    std::vector<unsigned> rowStart(rowLens.size(), 0);
    for (unsigned r = 1; r < rowLens.size(); ++r)
      rowStart[r] = rowStart[r - 1] + rowLens[r - 1];

    auto idx = [&](unsigned row, unsigned col) -> unsigned {
      return b + rowStart[row] + col;
    };

    // Step 1: initialize first row in superposition.
    os << "  ; Initialize first row with Hadamard\n";
    for (unsigned c = 0; c < rowLens[0]; ++c)
      gate1q(os, decls, "h", idx(0, c));

    // Step 2: horizontal entanglement within rows.
    os << "  ; Horizontal entanglement (row-wise CNOT chains)\n";
    for (unsigned r = 0; r < rowLens.size(); ++r) {
      for (unsigned c = 0; c + 1 < rowLens[r]; ++c)
        gate2q(os, decls, "cnot", idx(r, c), idx(r, c + 1));
    }

    // Step 3: inter-row triangular connectivity.
    os << "  ; Inter-row triangular entanglement\n";
    for (unsigned r = 0; r + 1 < rowLens.size(); ++r) {
      unsigned downSame = std::min(rowLens[r], rowLens[r + 1]);
      for (unsigned c = 0; c < downSame; ++c)
        gate2q(os, decls, "cnot", idx(r, c), idx(r + 1, c));

      unsigned downDiag = (rowLens[r + 1] > 0)
                              ? std::min(rowLens[r], rowLens[r + 1] - 1)
                              : 0;
      for (unsigned c = 0; c < downDiag; ++c)
        gate2q(os, decls, "cnot", idx(r, c), idx(r + 1, c + 1));
    }

    os << "  ; --- end color-code encoding ---\n";
  }

  // -------------------------------------------------------------------------
  // Function emission
  // -------------------------------------------------------------------------
  QIRFuncResult emitFunc(func::FuncOp func) {
    reset();
    QIRFuncResult r;
    r.funcName = func.getName().str();

    std::string bodyStr;
    llvm::raw_string_ostream os(bodyStr);

    // Pre-allocate blocks for function arguments.
    for (auto arg : func.getArguments()) {
      if (isa<LogicalQubitType>(arg.getType()))
        assignBlock(arg, allocBlock(blockSizeFor(arg.getType())));
    }

    if (!func.empty())
      for (auto &op : func.getBody().front())
        emitOp(&op, os, r.decls);

    r.body       = bodyStr;
    r.numQubits  = nextQubit;
    r.numResults = nextResult;
    return r;
  }

  // -------------------------------------------------------------------------
  // Op dispatch (physical-level expansion)
  // -------------------------------------------------------------------------
  void emitOp(Operation *op, llvm::raw_ostream &os,
              std::set<std::string> &decls) {
    llvm::TypeSwitch<Operation *>(op)

      // ------------------------------------------------------------------
      // Qubit lifecycle
      // ------------------------------------------------------------------
      .Case<InitZeroOp>([&](InitZeroOp o) {
        unsigned n = blockSizeFor(o.getType());
        PhysBlock blk = allocBlock(n);
        unsigned base = blk[0];
        assignBlock(o.getResult(), std::move(blk));

        if (isSteane(o.getType())) {
          emitSteaneEncoding(os, decls, base);
        } else if (isSurface(o.getType())) {
          unsigned d = getSurfaceDistance(o.getType());
          emitSurfaceEncoding(os, decls, base, d);
        } else if (isColorCode(o.getType())) {
          unsigned d = getSurfaceDistance(o.getType());
          emitColorCodeEncoding(os, decls, base, d, n);
        } else {
          emitComment(os, "init_zero (non-encoded: no encoding emitted)");
        }
      })
      .Case<InitPlusOp>([&](InitPlusOp o) {
        unsigned n = blockSizeFor(o.getType());
        PhysBlock blk = allocBlock(n);
        unsigned base = blk[0];
        assignBlock(o.getResult(), std::move(blk));

        if (isSteane(o.getType())) {
          // |+_L> = H_L |0_L> = transversal-H after encoding.
          emitSteaneEncoding(os, decls, base);
          os << "  ; --- transversal H for |+_L> ---\n";
          for (unsigned i = 0; i < n; ++i)
            gate1q(os, decls, "h", base + i);
        } else if (isSurface(o.getType())) {
          // |+_L> = H_L |0_L> = transversal-H after encoding.
          unsigned d = getSurfaceDistance(o.getType());
          emitSurfaceEncoding(os, decls, base, d);
          os << "  ; --- transversal H for |+_L> ---\n";
          for (unsigned i = 0; i < n; ++i)
            gate1q(os, decls, "h", base + i);
        } else if (isColorCode(o.getType())) {
          // |+_L> = H_L |0_L> = transversal-H after encoding.
          unsigned d = getSurfaceDistance(o.getType());
          emitColorCodeEncoding(os, decls, base, d, n);
          os << "  ; --- transversal H for |+_L> ---\n";
          for (unsigned i = 0; i < n; ++i)
            gate1q(os, decls, "h", base + i);
        } else {
          gate1q(os, decls, "h", base);
        }
      })
      .Case<DeallocOp>([&](DeallocOp) { emitComment(os, "dealloc"); })

      // ------------------------------------------------------------------
      // Single-qubit Clifford gates — emit transversally (one gate per
      // physical qubit in the block).
      // ------------------------------------------------------------------
      .Case<LogicalHOp>([&](LogicalHOp o) {
        const PhysBlock &blk = getBlock(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "  ; logical_h (transversal H on " << blk.size() << " qubits)\n";
        for (unsigned q : blk) gate1q(os, decls, "h", q);
      })
      .Case<LogicalXOp>([&](LogicalXOp o) {
        const PhysBlock &blk = getBlock(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "  ; logical_x (transversal X)\n";
        for (unsigned q : blk) gate1q(os, decls, "x", q);
      })
      .Case<LogicalYOp>([&](LogicalYOp o) {
        const PhysBlock &blk = getBlock(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "  ; logical_y (transversal Y)\n";
        for (unsigned q : blk) gate1q(os, decls, "y", q);
      })
      .Case<LogicalZOp>([&](LogicalZOp o) {
        const PhysBlock &blk = getBlock(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "  ; logical_z (transversal Z)\n";
        for (unsigned q : blk) gate1q(os, decls, "z", q);
      })
      .Case<LogicalSOp>([&](LogicalSOp o) {
        const PhysBlock &blk = getBlock(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "  ; logical_s (transversal S)\n";
        for (unsigned q : blk) gate1q(os, decls, "s", q);
      })

      // ------------------------------------------------------------------
      // Magic states + T gate
      // ------------------------------------------------------------------
      .Case<PrepareMagicTOp>([&](PrepareMagicTOp o) {
        unsigned n = blockSizeFor(o.getType());
        assignBlock(o.getResult(), allocBlock(n));
        emitComment(os, "prepare_magic_t (classical distillation resource)");
      })
      .Case<PrepareMagicCCZOp>([&](PrepareMagicCCZOp o) {
        unsigned n = blockSizeFor(o.getType());
        assignBlock(o.getResult(), allocBlock(n));
        emitComment(os, "prepare_magic_ccz");
      })
      .Case<LogicalTOp>([&](LogicalTOp o) {
        const PhysBlock &blk = getBlock(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "  ; logical_t (transversal T)\n";
        for (unsigned q : blk) gate1q(os, decls, "t", q);
      })

      // ------------------------------------------------------------------
      // Two-qubit gates — transversal (ctrl[i] → tgt[i] for all i)
      // ------------------------------------------------------------------
      .Case<LogicalCNOTOp>([&](LogicalCNOTOp o) {
        const PhysBlock &cblk = getBlock(o.getControl());
        const PhysBlock &tblk = getBlock(o.getTarget());
        propagate(o.getControlOut(), o.getControl());
        propagate(o.getTargetOut(), o.getTarget());
        unsigned n = std::min(cblk.size(), tblk.size());
        os << "  ; logical_cnot (transversal CNOT on " << n << " pairs)\n";
        for (unsigned i = 0; i < n; ++i)
          gate2q(os, decls, "cnot", cblk[i], tblk[i]);
      })
      .Case<LogicalCZOp>([&](LogicalCZOp o) {
        const PhysBlock &ablk = getBlock(o.getQubit0());
        const PhysBlock &bblk = getBlock(o.getQubit1());
        propagate(o.getResult0(), o.getQubit0());
        propagate(o.getResult1(), o.getQubit1());
        unsigned n = std::min(ablk.size(), bblk.size());
        os << "  ; logical_cz (transversal CZ on " << n << " pairs)\n";
        for (unsigned i = 0; i < n; ++i)
          gate2q(os, decls, "cz", ablk[i], bblk[i]);
      })
      .Case<LogicalToffoliOp>([&](LogicalToffoliOp o) {
        const PhysBlock &c0blk = getBlock(o.getCtrl0());
        const PhysBlock &c1blk = getBlock(o.getCtrl1());
        const PhysBlock &tblk  = getBlock(o.getTarget());
        propagate(o.getOutCtrl0(), o.getCtrl0());
        propagate(o.getOutCtrl1(), o.getCtrl1());
        propagate(o.getOutTarget(), o.getTarget());
        unsigned n = std::min({c0blk.size(), c1blk.size(), tblk.size()});
        os << "  ; logical_toffoli (transversal CCX on " << n << " triples)\n";
        for (unsigned i = 0; i < n; ++i)
          gate3q(os, decls, "ccx", c0blk[i], c1blk[i], tblk[i]);
      })

      // ------------------------------------------------------------------
      // Measurement — measure all n physical qubits; logical outcome
      // is the parity of the n results (classical post-processing).
      // ------------------------------------------------------------------
      .Case<LogicalMeasureOp>([&](LogicalMeasureOp o) {
        const PhysBlock &blk = getBlock(o.getLogicalQubit());
        unsigned n = blk.size();
        os << "  ; logical_measure → " << n << " physical mz operations\n";
        os << "  ; logical outcome = parity(r[0..N-1]) (classical XOR)\n";
        for (unsigned q : blk) {
          unsigned rIdx = nextResult++;
          os << "  call void @__quantum__qis__mz__body("
             << qref(q) << ", " << pref(rIdx) << ")\n";
        }
        decls.insert(
            "declare void @__quantum__qis__mz__body(ptr, ptr writeonly) #1");
      })

      // ------------------------------------------------------------------
      // Syndrome extraction — FT infrastructure; propagate block.
      // ------------------------------------------------------------------
      .Case<SteaneXSyndromeOp>([&](SteaneXSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment(os, "steane_x_syndrome");
      })
      .Case<SteaneZSyndromeOp>([&](SteaneZSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment(os, "steane_z_syndrome");
      })
      .Case<ExtractSyndromeOp>([&](ExtractSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment(os, "extract_syndrome");
      })
      .Case<FlagSyndromeOp>([&](FlagSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment(os, "flag_syndrome");
      })
      .Case<ShorSyndromeOp>([&](ShorSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment(os, "shor_syndrome");
      })

      // ------------------------------------------------------------------
      // Error correction — no QIR gate; propagate block.
      // ------------------------------------------------------------------
      .Case<CorrectOp>([&](CorrectOp o) {
        propagate(o.getCorrectedQubit(), o.getLogicalQubit());
        emitComment(os, "correct");
      })
      .Case<ECRoundOp>([&](ECRoundOp o) {
        propagate(o.getCorrectedQubit(), o.getLogicalQubit());
        emitComment(os, "ec_round");
      })

      // ------------------------------------------------------------------
      // Pauli frame — classical bookkeeping; no QIR gate.
      // ------------------------------------------------------------------
      .Case<PauliFrameUpdateOp>([&](PauliFrameUpdateOp) {
        emitComment(os, "pauli_frame_update");
      })
      .Case<CreateFrameOp>([&](CreateFrameOp) {
        emitComment(os, "create_frame");
      })
      .Case<ApplyFrameOp>([&](ApplyFrameOp o) {
        for (auto [in, out] : llvm::zip(o.getQubits(), o.getOutQubits()))
          propagate(out, in);
        emitComment(os, "apply_frame");
      })

      // ------------------------------------------------------------------
      // Distillation ops — comments only.
      // ------------------------------------------------------------------
      .Case<Distill15to1Op>([&](Distill15to1Op) {
        emitComment(os, "distill_15to1");
      })
      .Case<Distill7to1Op>([&](Distill7to1Op) {
        emitComment(os, "distill_7to1");
      })
      .Case<GateTeleportOp>([&](GateTeleportOp o) {
        propagate(o.getResult(), o.getQubit());
        emitComment(os, "gate_teleport");
      })

      // ------------------------------------------------------------------
      // Resource annotation, encode/decode, func.return — skip silently.
      // ------------------------------------------------------------------
      .Case<ResourceAnnotationOp>([&](ResourceAnnotationOp) {})
      .Case<EncodeOp>([&](EncodeOp)   { emitComment(os, "encode"); })
      .Case<DecodeOp>([&](DecodeOp)   { emitComment(os, "decode"); })
      .Case<func::ReturnOp>([&](func::ReturnOp) {})

      // ------------------------------------------------------------------
      // Fallback.
      // ------------------------------------------------------------------
      .Default([&](Operation *u) {
        if (includeComments)
          os << "  ; unsupported: " << u->getName().getStringRef() << "\n";
      });
  }
};

} // namespace

//----------------------------------------------------------------------//
// Pass definition
//----------------------------------------------------------------------//

namespace {

struct FTQCLowerToQIRPassImpl
    : public ::impl::FTQCLowerToQIRPassBase<FTQCLowerToQIRPassImpl> {
  using FTQCLowerToQIRPassBase::FTQCLowerToQIRPassBase;

  void runOnOperation() override {
    auto mod = getOperation();

    QIREmitter emitter(includeComments);
    std::string qir = emitter.emitModule(mod);

    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        mod.emitError("ftqc-lower-to-qir: cannot open output file '")
            << outputFile << "': " << ec.message();
        signalPassFailure();
        return;
      }
      file << qir;
    } else {
      llvm::errs() << qir;
    }

    mod->setAttr("ftqc.qir", StringAttr::get(mod.getContext(), qir));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createLowerToQIRPass() {
  return std::make_unique<FTQCLowerToQIRPassImpl>();
}


