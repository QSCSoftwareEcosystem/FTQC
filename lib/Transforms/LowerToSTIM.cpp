//===- LowerToSTIM.cpp - FTQC → STIM physical circuit translation ---------===//
//
// Expands logical FTQC IR into a STIM stabilizer circuit for QEC simulation.
//
// Physical qubit layout (Steane [[7,1,3]], 13 qubits per logical qubit):
//   Data  : base+0 .. base+6
//   X-anc : base+7  (Sx1), base+8  (Sx2), base+9  (Sx3)
//   Z-anc : base+10 (Sz1), base+11 (Sz2), base+12 (Sz3)
//
// Steane stabilizers (Sx_i == Sz_i for this CSS code):
//   Stab 1 : data offsets {0,2,4,6}
//   Stab 2 : data offsets {1,2,5,6}
//   Stab 3 : data offsets {3,4,5,6}
//
// Logical Z observable: XOR of all 7 data qubit Z-measurements.
//
// Noise model:
//   DEPOLARIZE1(p) after single-qubit Clifford gates
//   DEPOLARIZE2(p) after two-qubit Clifford gates (applied per pair)
//   X_ERROR(p)     after RESET and before M
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_FTQCLOWERTOSTIMPASS
#include "FTQC/Transforms/Passes.h.inc"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// Steane [[7,1,3]] code constants
//===----------------------------------------------------------------------===//

namespace {

// Number of physical qubits per logical qubit (data + X-anc + Z-anc).
static constexpr int STEANE_DATA  = 7;
static constexpr int STEANE_XANC  = 3;
static constexpr int STEANE_ZANC  = 3;
static constexpr int STEANE_TOTAL = STEANE_DATA + STEANE_XANC + STEANE_ZANC;

// Offsets within a block for X and Z ancilla.
static constexpr int XANC_OFF = STEANE_DATA;
static constexpr int ZANC_OFF = STEANE_DATA + STEANE_XANC;

// Number of syndrome bits per round (one per stabilizer generator per basis).
static constexpr int SYNDROME_BITS = STEANE_XANC + STEANE_ZANC; // 6

// Steane stabilizer support (relative data-qubit offsets, 0-indexed within block).
// Same for X and Z (CSS code property).
static const std::vector<std::vector<int>> STEANE_STABS = {
  {0, 2, 4, 6},  // Stabilizer 1
  {1, 2, 5, 6},  // Stabilizer 2
  {3, 4, 5, 6},  // Stabilizer 3
};

// Qubits on which H is applied during encoding of |0_L>:
// one "check qubit" per stabilizer (the first element of each support set).
static const std::vector<int> ENCODE_H_QUBITS = {0, 1, 3};

//===----------------------------------------------------------------------===//
// STIMEmitter
//===----------------------------------------------------------------------===//

/// Helper: format a list of qubit indices separated by spaces.
static std::string qubitList(const std::vector<int> &qs) {
  std::string s;
  for (int q : qs) {
    if (!s.empty()) s += " ";
    s += std::to_string(q);
  }
  return s;
}

/// Helper: format a noise probability with sufficient precision.
static std::string fmtP(double p) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.6g", p);
  return buf;
}

struct QubitBlock {
  int base;     ///< Absolute index of data qubit 0 in this block.
  int obsId;    ///< OBSERVABLE_INCLUDE index for this logical qubit.
  int prevRoundMeasureStart = -1; ///< Measurement index at start of last syndrome round (-1 = none).
};

class STIMEmitter {
public:
  STIMEmitter(double physErrorRate, unsigned syndromeRounds, bool includeComments)
      : p(physErrorRate), rounds(syndromeRounds),
        includeComments(includeComments) {}

  /// Walk the module and emit a STIM circuit string.
  std::string emitModule(ModuleOp mod) {
    for (auto func : mod.getOps<func::FuncOp>())
      emitFunc(func);
    return os.str();
  }

private:
  double   p;
  unsigned rounds;
  bool     includeComments;

  std::string              buf;
  llvm::raw_string_ostream os{buf};

  // Maps each SSA value to its QubitBlock.
  DenseMap<Value, int> blockOf; ///< SSA value → index in blocks[]
  std::vector<QubitBlock> blocks;

  int nextQubit  = 0; ///< Next available physical qubit index.
  int nextObs    = 0; ///< Next OBSERVABLE_INCLUDE index.
  int measureCount = 0; ///< Total measurements emitted so far.

  //------------------------------------------------------------------------
  // Block management
  //------------------------------------------------------------------------

  /// Allocate a new QubitBlock and register SSA value → block.
  int allocBlock(Value v) {
    int idx = (int)blocks.size();
    QubitBlock blk;
    blk.base  = nextQubit;
    blk.obsId = nextObs++;
    nextQubit += STEANE_TOTAL;
    blocks.push_back(blk);
    blockOf[v] = idx;
    return idx;
  }

  /// Transfer block registration from oldV to newV (gate SSA propagation).
  void propagate(Value newV, Value oldV) {
    auto it = blockOf.find(oldV);
    if (it != blockOf.end())
      blockOf[newV] = it->second;
  }

  /// Look up the block for an SSA value (or crash — should never be missing).
  QubitBlock &blk(Value v) {
    auto it = blockOf.find(v);
    assert(it != blockOf.end() && "STIMEmitter: unmapped logical qubit value");
    return blocks[it->second];
  }

  /// Absolute index of data qubit i within a block.
  int data(QubitBlock &b, int i) { return b.base + i; }
  /// Absolute index of X ancilla i within a block.
  int xanc(QubitBlock &b, int i) { return b.base + XANC_OFF + i; }
  /// Absolute index of Z ancilla i within a block.
  int zanc(QubitBlock &b, int i) { return b.base + ZANC_OFF + i; }

  //------------------------------------------------------------------------
  // Emission helpers
  //------------------------------------------------------------------------

  void comment(const char *msg) {
    if (includeComments)
      os << "# " << msg << "\n";
  }

  /// Emit a RESET on a list of qubits (plus X_ERROR noise if p > 0).
  /// STIM uses "R" (not "RESET") as the reset gate name.
  void emitReset(const std::vector<int> &qs) {
    os << "R " << qubitList(qs) << "\n";
    if (p > 0)
      os << "X_ERROR(" << fmtP(p) << ") " << qubitList(qs) << "\n";
  }

  /// Emit a single-qubit gate on a list of qubits (plus DEPOLARIZE1 noise).
  void emit1Q(const char *gate, const std::vector<int> &qs) {
    os << gate << " " << qubitList(qs) << "\n";
    if (p > 0)
      os << "DEPOLARIZE1(" << fmtP(p) << ") " << qubitList(qs) << "\n";
  }

  /// Emit a two-qubit gate on pairs (ctrl0,tgt0, ctrl1,tgt1, ...).
  /// Each pair is listed interleaved: CX ctrl0 tgt0 ctrl1 tgt1 ...
  /// STIM DEPOLARIZE2 takes pairs in the same interleaved format.
  void emit2Q(const char *gate, const std::vector<int> &pairs) {
    os << gate;
    for (int q : pairs) os << " " << q;
    os << "\n";
    if (p > 0)
      os << "DEPOLARIZE2(" << fmtP(p) << ")";
    if (p > 0)
      for (int q : pairs) os << " " << q;
    if (p > 0) os << "\n";
  }

  /// Emit M on a list of qubits (plus X_ERROR noise before measurement).
  void emitM(const std::vector<int> &qs) {
    if (p > 0)
      os << "X_ERROR(" << fmtP(p) << ") " << qubitList(qs) << "\n";
    os << "M " << qubitList(qs) << "\n";
    measureCount += (int)qs.size();
  }

  //------------------------------------------------------------------------
  // Encoding circuit
  //------------------------------------------------------------------------

  /// Emit the encoding circuit for |0_L> for a single block.
  /// Starting from RESET (all in |0>), apply H on check qubits and CX cascade
  /// to entangle into the code space (satisfies all X and Z stabilizers).
  void emitEncoding(QubitBlock &b) {
    comment("--- Encoding |0_L> ---");

    // RESET all 7 data qubits.
    std::vector<int> all7;
    for (int i = 0; i < STEANE_DATA; ++i) all7.push_back(b.base + i);
    emitReset(all7);

    // H on the check qubits (first member of each stabilizer support).
    std::vector<int> hQubits;
    for (int off : ENCODE_H_QUBITS) hQubits.push_back(b.base + off);
    emit1Q("H", hQubits);

    // CX cascade: for each stabilizer, ctrl = check qubit, tgts = rest of support.
    for (size_t s = 0; s < STEANE_STABS.size(); ++s) {
      const auto &support = STEANE_STABS[s];
      int ctrl = b.base + support[0];
      for (size_t j = 1; j < support.size(); ++j) {
        int tgt = b.base + support[j];
        emit2Q("CX", {ctrl, tgt});
      }
    }
  }

  //------------------------------------------------------------------------
  // Syndrome extraction
  //------------------------------------------------------------------------

  /// Emit one full syndrome extraction round for a block.
  /// Emits X-syndrome (3 bits) then Z-syndrome (3 bits) → 6 measurements.
  /// Emits DETECTOR records comparing to the previous round (or 0 if first).
  void emitSyndromeRound(QubitBlock &b) {
    int roundStart = measureCount; // will be updated after M instructions

    comment("-- X syndrome --");
    // Prepare X ancilla.
    std::vector<int> xAncs;
    for (int i = 0; i < STEANE_XANC; ++i) xAncs.push_back(xanc(b, i));
    emitReset(xAncs);
    emit1Q("H", xAncs);

    // CX ancilla → data for each X stabilizer.
    for (int s = 0; s < STEANE_XANC; ++s) {
      int ctrl = xanc(b, s);
      std::vector<int> pairs;
      for (int off : STEANE_STABS[s]) {
        pairs.push_back(ctrl);
        pairs.push_back(data(b, off));
      }
      emit2Q("CX", pairs);
    }
    emit1Q("H", xAncs);
    emitM(xAncs); // 3 measurements

    comment("-- Z syndrome --");
    // Prepare Z ancilla.
    std::vector<int> zAncs;
    for (int i = 0; i < STEANE_ZANC; ++i) zAncs.push_back(zanc(b, i));
    emitReset(zAncs);

    // CX data → ancilla for each Z stabilizer.
    for (int s = 0; s < STEANE_ZANC; ++s) {
      int tgt = zanc(b, s);
      std::vector<int> pairs;
      for (int off : STEANE_STABS[s]) {
        pairs.push_back(data(b, off));
        pairs.push_back(tgt);
      }
      emit2Q("CX", pairs);
    }
    emitM(zAncs); // 3 measurements

    // Total 6 measurements for this round.
    // Now emit DETECTOR records.
    // After the M instructions: measureCount = roundStart + SYNDROME_BITS.
    //
    // Syndrome bit i (0..5) was measurement (roundStart + i).
    // Its offset from the current end (measureCount): SYNDROME_BITS - i.
    //
    // If prevRoundMeasureStart >= 0, also compare to the same stabilizer from
    // the previous round.  That bit was measurement (prevRoundMeasureStart + i),
    // so its offset from the current end: measureCount - (prevRoundMeasureStart + i).

    for (int i = 0; i < SYNDROME_BITS; ++i) {
      int curOff = SYNDROME_BITS - i;  // offset of this round's syndrome bit i
      os << "DETECTOR";
      if (includeComments)
        os << "(" << i << ")"; // optional coordinate for visualisation
      os << " rec[-" << curOff << "]";
      if (b.prevRoundMeasureStart >= 0) {
        int prevOff = measureCount - (b.prevRoundMeasureStart + i);
        os << " rec[-" << prevOff << "]";
      }
      os << "\n";
    }

    b.prevRoundMeasureStart = roundStart;
  }

  /// Emit `rounds` syndrome rounds for a block.
  void emitSyndrome(QubitBlock &b) {
    for (unsigned r = 0; r < rounds; ++r)
      emitSyndromeRound(b);
  }

  //------------------------------------------------------------------------
  // Logical gate emission
  //------------------------------------------------------------------------

  void emitH(QubitBlock &b) {
    comment("logical_h (transversal)");
    std::vector<int> all7;
    for (int i = 0; i < STEANE_DATA; ++i) all7.push_back(b.base + i);
    emit1Q("H", all7);
  }

  void emitX(QubitBlock &b) {
    comment("logical_x (transversal)");
    std::vector<int> all7;
    for (int i = 0; i < STEANE_DATA; ++i) all7.push_back(b.base + i);
    emit1Q("X", all7);
  }

  void emitZ(QubitBlock &b) {
    comment("logical_z (transversal)");
    std::vector<int> all7;
    for (int i = 0; i < STEANE_DATA; ++i) all7.push_back(b.base + i);
    emit1Q("Z", all7);
  }

  void emitY(QubitBlock &b) {
    comment("logical_y (transversal)");
    std::vector<int> all7;
    for (int i = 0; i < STEANE_DATA; ++i) all7.push_back(b.base + i);
    emit1Q("Y", all7);
  }

  void emitS(QubitBlock &b) {
    comment("logical_s (transversal)");
    std::vector<int> all7;
    for (int i = 0; i < STEANE_DATA; ++i) all7.push_back(b.base + i);
    emit1Q("S", all7);
  }

  void emitT_approx(QubitBlock &b) {
    // T is non-Clifford; STIM only supports stabilizer circuits.
    // Approximate as S for QEC simulation purposes (tracks Clifford errors correctly).
    comment("logical_t approximated as S (STIM is Clifford-only)");
    std::vector<int> all7;
    for (int i = 0; i < STEANE_DATA; ++i) all7.push_back(b.base + i);
    emit1Q("S", all7);
  }

  void emitCNOT(const QubitBlock &ctrl, const QubitBlock &tgt) {
    comment("logical_cnot (transversal CX)");
    std::vector<int> pairs;
    for (int i = 0; i < STEANE_DATA; ++i) {
      pairs.push_back(ctrl.base + i);
      pairs.push_back(tgt.base + i);
    }
    emit2Q("CX", pairs);
  }

  void emitCZ(const QubitBlock &q0, const QubitBlock &q1) {
    comment("logical_cz (transversal CZ)");
    std::vector<int> pairs;
    for (int i = 0; i < STEANE_DATA; ++i) {
      pairs.push_back(q0.base + i);
      pairs.push_back(q1.base + i);
    }
    emit2Q("CZ", pairs);
  }

  /// Emit logical Z measurement: M all 7 data qubits + OBSERVABLE_INCLUDE.
  void emitMeasure(QubitBlock &b) {
    comment("logical_measure → M all data + OBSERVABLE_INCLUDE");
    std::vector<int> all7;
    for (int i = 0; i < STEANE_DATA; ++i) all7.push_back(b.base + i);
    emitM(all7); // 7 measurements

    // Logical Z = XOR of all 7 data qubit Z-measurement results.
    os << "OBSERVABLE_INCLUDE(" << b.obsId << ")";
    for (int i = 0; i < STEANE_DATA; ++i)
      os << " rec[-" << (STEANE_DATA - i) << "]";
    os << "\n";
  }

  //------------------------------------------------------------------------
  // Function walk
  //------------------------------------------------------------------------

  void emitFunc(func::FuncOp func) {
    if (includeComments)
      os << "# === function @" << func.getName().str() << " ===\n";

    // First pass: allocate blocks for function arguments.
    for (auto arg : func.getArguments()) {
      if (isa<LogicalQubitType>(arg.getType()))
        allocBlock(arg);
    }

    if (func.empty()) return;

    for (auto &op : func.getBody().front())
      emitOp(&op);
  }

  void emitOp(Operation *op) {
    llvm::TypeSwitch<Operation *>(op)

      //--------------------------------------------------------------
      // Lifecycle
      //--------------------------------------------------------------
      .Case<InitZeroOp>([&](InitZeroOp o) {
        int idx = allocBlock(o.getResult());
        auto &b = blocks[idx];
        emitEncoding(b);
        emitSyndrome(b);
      })
      .Case<InitPlusOp>([&](InitPlusOp o) {
        int idx = allocBlock(o.getResult());
        auto &b = blocks[idx];
        emitEncoding(b);
        // Apply logical H to get |+_L>.
        emitH(b);
        emitSyndrome(b);
      })
      .Case<DeallocOp>([&](DeallocOp) {
        comment("dealloc (no physical op in STIM)");
      })

      //--------------------------------------------------------------
      // Single-qubit Clifford gates
      //--------------------------------------------------------------
      .Case<LogicalHOp>([&](LogicalHOp o) {
        auto &b = blk(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitH(b);
        emitSyndrome(b);
      })
      .Case<LogicalXOp>([&](LogicalXOp o) {
        auto &b = blk(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitX(b);
        emitSyndrome(b);
      })
      .Case<LogicalZOp>([&](LogicalZOp o) {
        auto &b = blk(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitZ(b);
        emitSyndrome(b);
      })
      .Case<LogicalYOp>([&](LogicalYOp o) {
        auto &b = blk(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitY(b);
        emitSyndrome(b);
      })
      .Case<LogicalSOp>([&](LogicalSOp o) {
        auto &b = blk(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitS(b);
        emitSyndrome(b);
      })

      //--------------------------------------------------------------
      // Magic states + T gate
      //--------------------------------------------------------------
      .Case<PrepareMagicTOp>([&](PrepareMagicTOp) {
        comment("prepare_magic_t (consumed by logical_t)");
      })
      .Case<PrepareMagicCCZOp>([&](PrepareMagicCCZOp) {
        comment("prepare_magic_ccz (consumed by logical_toffoli)");
      })
      .Case<LogicalTOp>([&](LogicalTOp o) {
        auto &b = blk(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitT_approx(b);
        emitSyndrome(b);
      })

      //--------------------------------------------------------------
      // Two-qubit gates
      //--------------------------------------------------------------
      .Case<LogicalCNOTOp>([&](LogicalCNOTOp o) {
        auto &bc = blk(o.getControl());
        auto &bt = blk(o.getTarget());
        propagate(o.getControlOut(), o.getControl());
        propagate(o.getTargetOut(), o.getTarget());
        emitCNOT(bc, bt);
        emitSyndrome(bc);
        emitSyndrome(bt);
      })
      .Case<LogicalCZOp>([&](LogicalCZOp o) {
        auto &b0 = blk(o.getQubit0());
        auto &b1 = blk(o.getQubit1());
        propagate(o.getResult0(), o.getQubit0());
        propagate(o.getResult1(), o.getQubit1());
        emitCZ(b0, b1);
        emitSyndrome(b0);
        emitSyndrome(b1);
      })
      .Case<LogicalToffoliOp>([&](LogicalToffoliOp o) {
        // Toffoli = CCX; approximate as CX ctrl0→tgt + CX ctrl1→tgt in STIM.
        comment("logical_toffoli approximated as two CX gates (STIM Clifford only)");
        auto &bc0 = blk(o.getCtrl0());
        auto &bc1 = blk(o.getCtrl1());
        auto &bt  = blk(o.getTarget());
        propagate(o.getOutCtrl0(), o.getCtrl0());
        propagate(o.getOutCtrl1(), o.getCtrl1());
        propagate(o.getOutTarget(), o.getTarget());
        emitCNOT(bc0, bt);
        emitCNOT(bc1, bt);
        emitSyndrome(bc0);
        emitSyndrome(bc1);
        emitSyndrome(bt);
      })

      //--------------------------------------------------------------
      // Measurement
      //--------------------------------------------------------------
      .Case<LogicalMeasureOp>([&](LogicalMeasureOp o) {
        auto &b = blk(o.getLogicalQubit());
        emitMeasure(b);
      })

      //--------------------------------------------------------------
      // Syndrome extraction ops — emit another syndrome round.
      //--------------------------------------------------------------
      .Case<SteaneXSyndromeOp>([&](SteaneXSyndromeOp o) {
        auto &b = blk(o.getLogicalQubit());
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        comment("steane_x_syndrome → physical syndrome round");
        emitSyndromeRound(b);
      })
      .Case<SteaneZSyndromeOp>([&](SteaneZSyndromeOp o) {
        auto &b = blk(o.getLogicalQubit());
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        comment("steane_z_syndrome → physical syndrome round");
        emitSyndromeRound(b);
      })
      .Case<ExtractSyndromeOp>([&](ExtractSyndromeOp o) {
        auto &b = blk(o.getLogicalQubit());
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        comment("extract_syndrome → physical syndrome round");
        emitSyndromeRound(b);
      })
      .Case<ShorSyndromeOp>([&](ShorSyndromeOp o) {
        auto &b = blk(o.getLogicalQubit());
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        comment("shor_syndrome → Steane-style physical syndrome round (approx)");
        emitSyndromeRound(b);
      })
      .Case<FlagSyndromeOp>([&](FlagSyndromeOp o) {
        auto &b = blk(o.getLogicalQubit());
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        comment("flag_syndrome → Steane-style physical syndrome round (approx)");
        emitSyndromeRound(b);
      })

      //--------------------------------------------------------------
      // Error correction / frame ops — propagate qubit, emit comment.
      //--------------------------------------------------------------
      .Case<CorrectOp>([&](CorrectOp o) {
        propagate(o.getCorrectedQubit(), o.getLogicalQubit());
        comment("correct (decoded classically in STIM)");
      })
      .Case<ECRoundOp>([&](ECRoundOp o) {
        auto &b = blk(o.getLogicalQubit());
        propagate(o.getCorrectedQubit(), o.getLogicalQubit());
        emitSyndrome(b);
      })
      .Case<PauliFrameUpdateOp>([&](PauliFrameUpdateOp) {
        comment("pauli_frame_update (classical in STIM context)");
      })
      .Case<CreateFrameOp>([&](CreateFrameOp) {})
      .Case<ApplyFrameOp>([&](ApplyFrameOp o) {
        for (auto [in, out] : llvm::zip(o.getQubits(), o.getOutQubits()))
          propagate(out, in);
      })

      //--------------------------------------------------------------
      // Distillation / resource / encode / decode — skip silently.
      //--------------------------------------------------------------
      .Case<Distill15to1Op>([&](Distill15to1Op) { comment("distill_15to1 (skipped)"); })
      .Case<Distill7to1Op>([&](Distill7to1Op) { comment("distill_7to1 (skipped)"); })
      .Case<GateTeleportOp>([&](GateTeleportOp o) {
        propagate(o.getResult(), o.getQubit());
        comment("gate_teleport (skipped)");
      })
      .Case<ResourceAnnotationOp>([&](ResourceAnnotationOp) {})
      .Case<EncodeOp>([&](EncodeOp) { comment("encode"); })
      .Case<DecodeOp>([&](DecodeOp) { comment("decode"); })
      .Case<func::ReturnOp>([&](func::ReturnOp) {})

      //--------------------------------------------------------------
      // Fallback.
      //--------------------------------------------------------------
      .Default([&](Operation *unknownOp) {
        if (includeComments)
          os << "# unsupported: "
             << unknownOp->getName().getStringRef().str() << "\n";
      });
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCLowerToSTIMPassImpl
    : public ::impl::FTQCLowerToSTIMPassBase<FTQCLowerToSTIMPassImpl> {
  using FTQCLowerToSTIMPassBase::FTQCLowerToSTIMPassBase;

  void runOnOperation() override {
    auto mod = getOperation();

    STIMEmitter emitter(physErrorRate, syndromeRounds, includeComments);
    std::string stim = emitter.emitModule(mod);

    // Write to file or stderr.
    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        mod.emitError("ftqc-lower-to-stim: cannot open output file '")
            << outputFile << "': " << ec.message();
        signalPassFailure();
        return;
      }
      file << stim;
    } else {
      llvm::errs() << stim;
    }

    // Attach the STIM string as a module attribute for FileCheck testing.
    mod->setAttr("ftqc.stim", StringAttr::get(mod.getContext(), stim));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createLowerToSTIMPass() {
  return std::make_unique<FTQCLowerToSTIMPassImpl>();
}
