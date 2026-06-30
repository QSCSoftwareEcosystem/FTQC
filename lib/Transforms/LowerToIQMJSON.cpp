//===- LowerToIQMJSON.cpp - FTQC → IQM JSON translation ------------------===//
//
// Translates a post-pipeline FTQC module to an IQM JSON circuit suitable for
// IQM Resonance hardware or the IQM emulator (device alias with :mock suffix).
//
// Gate decompositions to IQM native gates {prx, cz, measure}.  prx angles are
// in *turns* (1 turn = 2π), so a π pulse is angle_t = 0.5 (matching IQM):
//   logical_x        → prx(0.5, 0.0)
//   logical_y        → prx(0.5, 0.25)
//   logical_h        → prx(0.25,0.25) ; prx(0.5,0.0)            (= X·Ry(π/2))
//   logical_z        → Rz(π)   = prx(0.75,0.0) ; prx(0.5,  0.25) ; prx(0.25,0.0)
//   logical_s        → Rz(π/2) = prx(0.75,0.0) ; prx(0.25, 0.25) ; prx(0.25,0.0)
//   logical_t        → Rz(π/4) = prx(0.75,0.0) ; prx(0.125,0.25) ; prx(0.25,0.0)
//   logical_cnot     → H(target) ; cz[ctrl,tgt] ; H(target)
//   logical_cz       → cz[QB{a}, QB{b}]
//   logical_toffoli  → standard 6-CZ Toffoli decomposition (Barenco et al.)
//   logical_measure  → measure locus=[QB{n}] key="mQB{n}"
//
// Syndrome ops (expand-syndromes=true, default):
//   SteaneZSyndrome  → H(anc) ; cz[data,anc] ; H(anc) ; measure(anc, "sz_...")
//   SteaneXSyndrome  → H(anc) ; H(data) ; cz[anc,data] ; H(data) ; H(anc) ;
//                       measure(anc, "sx_...")
//
// Output: one JSON object per func.func, written to --output-file (or stderr).
// Also attached to the module as the "ftqc.iqm_json" string attribute.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_FTQCLOWERTOIQMJSONPASS
#include "FTQC/Transforms/Passes.h.inc"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <system_error>
#include <vector>

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// IQMJSONEmitter
//===----------------------------------------------------------------------===//

namespace {

class IQMJSONEmitter {
public:
  explicit IQMJSONEmitter(bool includeComments, bool expandSyndromes,
                          bool emitRadians = false)
      : includeComments_(includeComments), expandSyndromes_(expandSyndromes),
        emitRadians_(emitRadians) {}

  std::string emitModule(ModuleOp mod) {
    bool first = true;
    for (auto func : mod.getOps<func::FuncOp>()) {
      if (!first) os_ << "\n";
      first = false;
      emitFunc(func);
    }
    return buf_;
  }

private:
  bool includeComments_;
  bool expandSyndromes_;
  bool emitRadians_ = false;

  std::string buf_;
  llvm::raw_string_ostream os_{buf_};

  // Per-function instruction buffer.
  std::string instBuf_;
  llvm::raw_string_ostream instOs_{instBuf_};
  bool firstInst_ = true;

  DenseMap<Value, std::string> qubitMap_;
  int qubitIdx_   = 0;   // shared counter for ALL qubits (data + ancilla)
  int measureIdx_ = 0;

  //----------------------------------------------------------------------
  // Qubit name helpers
  //----------------------------------------------------------------------

  // All qubits — data and ancilla — share the same QB{n} counter so the
  // locus names form a contiguous sequence QB1..QBn that IQM hardware expects.
  std::string freshQubit()   { return "QB" + std::to_string(++qubitIdx_); }
  std::string freshAncilla() { return freshQubit(); }

  void reg(Value v, const std::string &name) { qubitMap_[v] = name; }

  void propagate(Value newV, Value oldV) {
    auto it = qubitMap_.find(oldV);
    if (it != qubitMap_.end())
      qubitMap_[newV] = it->second;
  }

  std::string qname(Value v) {
    auto it = qubitMap_.find(v);
    if (it != qubitMap_.end())
      return it->second;
    // Fallback for malformed IR — assign a fresh name.
    std::string n = freshQubit();
    qubitMap_[v] = n;
    return n;
  }

  //----------------------------------------------------------------------
  // JSON instruction emitters
  //----------------------------------------------------------------------

  void emitInst(const std::string &name,
                const std::vector<std::string> &locus,
                const std::string &args) {
    if (!firstInst_) instOs_ << ",\n";
    firstInst_ = false;
    instOs_ << "    {\"name\":\"" << name << "\",\"locus\":[";
    for (size_t i = 0; i < locus.size(); ++i) {
      if (i) instOs_ << ",";
      instOs_ << "\"" << locus[i] << "\"";
    }
    instOs_ << "],\"args\":{" << args << "}}";
  }

  // Format a double as a JSON number that always carries a decimal point, so a
  // whole value (e.g. 0.0) is emitted as "0.0" not "0". The IQM validator types
  // prx angle/phase strictly as float and rejects an integer literal.
  static std::string jsonFloat(double v) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.9g", v);
    std::string s(b);
    if (s.find_first_of(".eEnN") == std::string::npos)  // no '.'/exp/inf/nan
      s += ".0";
    return s;
  }

  // Gate helpers pass angles in turns; emit either turns (angle_t/phase_t) or
  // radians (angle/phase = turns*2pi) depending on emitRadians_.
  void emitPRX(const std::string &q, double angle_t, double phase_t) {
    std::string args;
    if (emitRadians_) {
      constexpr double kTurn = 6.283185307179586; // 2*pi
      args = "\"angle\":" + jsonFloat(angle_t * kTurn) +
             ",\"phase\":" + jsonFloat(phase_t * kTurn);
    } else {
      char b[128];
      std::snprintf(b, sizeof(b),
                    "\"angle_t\":%.6g,\"phase_t\":%.6g", angle_t, phase_t);
      args = b;
    }
    emitInst("prx", {q}, args);
  }

  void emitCZ(const std::string &a, const std::string &b) {
    emitInst("cz", {a, b}, "");
  }

  void emitMeasure(const std::string &q, const std::string &key) {
    emitInst("measure", {q}, "\"key\":\"" + key + "\"");
  }

  // Emits a _comment pseudo-instruction for debugging.  Not valid for
  // hardware submission — disable with --include-comments=false.
  void emitComment(const std::string &msg) {
    if (!includeComments_) return;
    if (!firstInst_) instOs_ << ",\n";
    firstInst_ = false;
    instOs_ << "    {\"name\":\"_comment\",\"locus\":[],\"args\":"
            << "{\"msg\":\"" << msg << "\"}}";
  }

  //----------------------------------------------------------------------
  // IQM-native gate helpers.
  //
  // IQM prx(angle_t, phase_t) takes both angles in *turns* (1 turn = 2π):
  //     prx(a,p) = exp(-i·(2π·a)/2·(cos(2π·p)·X + sin(2π·p)·Y)).
  // So a π pulse (X) is angle_t = 0.5, and a π/2 pulse is angle_t = 0.25.
  // (emitPRX writes the values verbatim into "angle_t"/"phase_t".)
  //----------------------------------------------------------------------

  // X = Rx(π) = prx(0.5, 0.0)
  void emitX(const std::string &q)   { emitPRX(q, 0.5, 0.0); }
  // Y = Ry(π) = prx(0.5, 0.25)
  void emitY(const std::string &q)   { emitPRX(q, 0.5, 0.25); }

  // H = X · Ry(π/2)  — apply Ry(π/2) first, then X.
  //   Ry(π/2) = prx(0.25, 0.25),  X = prx(0.5, 0.0)
  void emitH(const std::string &q) {
    emitPRX(q, 0.25, 0.25);
    emitPRX(q, 0.5,  0.0);
  }

  // Rz(θ) = Rx(π/2) · Ry(θ) · Rx(-π/2)  (conjugating Y by Rx(π/2) gives Z).
  // Circuit order (first applied first): Rx(-π/2), Ry(θ), Rx(π/2), i.e.
  //   prx(0.75,0.0), prx(θ/2π, 0.25), prx(0.25,0.0).
  void emitZ(const std::string &q) {   // Rz(π):    Ry(π)    = prx(0.5,  0.25)
    emitPRX(q, 0.75, 0.0); emitPRX(q, 0.5,   0.25); emitPRX(q, 0.25, 0.0);
  }
  void emitS(const std::string &q) {   // Rz(π/2):  Ry(π/2)  = prx(0.25, 0.25)
    emitPRX(q, 0.75, 0.0); emitPRX(q, 0.25,  0.25); emitPRX(q, 0.25, 0.0);
  }
  void emitT(const std::string &q) {   // Rz(π/4):  Ry(π/4)  = prx(0.125,0.25)
    emitPRX(q, 0.75, 0.0); emitPRX(q, 0.125, 0.25); emitPRX(q, 0.25, 0.0);
  }
  void emitTdg(const std::string &q) { // Rz(-π/4): Ry(-π/4) = prx(0.125,0.75)
    emitPRX(q, 0.75, 0.0); emitPRX(q, 0.125, 0.75); emitPRX(q, 0.25, 0.0);
  }

  // CNOT(ctrl, tgt) = H(tgt) · CZ(ctrl,tgt) · H(tgt)
  void emitCNOT(const std::string &ctrl, const std::string &tgt) {
    emitH(tgt);
    emitCZ(ctrl, tgt);
    emitH(tgt);
  }

  // Toffoli: standard 6-CZ decomposition (Barenco et al., 7 T/T†, 6 CNOT).
  //   H t; CNOT c1,t; T†t; CNOT c0,t; Tt; CNOT c1,t; T†t; CNOT c0,t;
  //   Tc1; Tt; Ht; CNOT c0,c1; Tc0; T†c1; CNOT c0,c1
  void emitToffoli(const std::string &c0,
                   const std::string &c1,
                   const std::string &t) {
    emitH(t);
    emitCNOT(c1, t);  emitTdg(t);
    emitCNOT(c0, t);  emitT(t);
    emitCNOT(c1, t);  emitTdg(t);
    emitCNOT(c0, t);
    emitT(c1);        emitT(t);
    emitH(t);
    emitCNOT(c0, c1); emitT(c0); emitTdg(c1);
    emitCNOT(c0, c1);
  }

  //----------------------------------------------------------------------
  // Function and op emission
  //----------------------------------------------------------------------

  void emitFunc(func::FuncOp func) {
    qubitIdx_   = 0;
    measureIdx_ = 0;
    instBuf_.clear();
    firstInst_ = true;
    qubitMap_.clear();

    // Assign QB{n} names to logical-qubit function arguments.
    for (auto arg : func.getArguments()) {
      if (isa<LogicalQubitType>(arg.getType()))
        reg(arg, freshQubit());
    }

    if (!func.empty()) {
      for (auto &op : func.getBody().front())
        emitOp(&op);
    }

    os_ << "{\n"
        << "  \"name\":\"" << func.getName().str() << "\",\n"
        << "  \"instructions\":[\n"
        << instBuf_ << "\n"
        << "  ]\n"
        << "}";
  }

  void emitOp(Operation *op) {
    llvm::TypeSwitch<Operation *>(op)

      //----------------------------------------------------------------
      // Qubit lifecycle
      //----------------------------------------------------------------
      .Case<InitZeroOp>([&](InitZeroOp o) {
        // |0⟩ is the default reset state — no instruction needed.
        reg(o.getResult(), freshQubit());
      })
      .Case<InitPlusOp>([&](InitPlusOp o) {
        std::string n = freshQubit();
        reg(o.getResult(), n);
        emitH(n);
      })
      .Case<DeallocOp>([&](DeallocOp) {})

      //----------------------------------------------------------------
      // Single-qubit Clifford gates
      //----------------------------------------------------------------
      .Case<LogicalHOp>([&](LogicalHOp o) {
        std::string n = qname(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitH(n);
      })
      .Case<LogicalXOp>([&](LogicalXOp o) {
        std::string n = qname(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitX(n);
      })
      .Case<LogicalYOp>([&](LogicalYOp o) {
        std::string n = qname(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitY(n);
      })
      .Case<LogicalZOp>([&](LogicalZOp o) {
        std::string n = qname(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitZ(n);
      })
      .Case<LogicalSOp>([&](LogicalSOp o) {
        std::string n = qname(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitS(n);
      })

      //----------------------------------------------------------------
      // Magic states + T gate
      //----------------------------------------------------------------
      .Case<PrepareMagicTOp>([&](PrepareMagicTOp o) {
        qubitMap_[o.getResult()] = "__magic_t";
        emitComment("prepare_magic_t");
      })
      .Case<PrepareMagicCCZOp>([&](PrepareMagicCCZOp o) {
        qubitMap_[o.getResult()] = "__magic_ccz";
        emitComment("prepare_magic_ccz");
      })
      .Case<LogicalTOp>([&](LogicalTOp o) {
        std::string n = qname(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        emitT(n);
      })

      //----------------------------------------------------------------
      // Two-qubit gates
      //----------------------------------------------------------------
      .Case<LogicalCNOTOp>([&](LogicalCNOTOp o) {
        std::string c = qname(o.getControl());
        std::string t = qname(o.getTarget());
        propagate(o.getControlOut(), o.getControl());
        propagate(o.getTargetOut(), o.getTarget());
        emitCNOT(c, t);
      })
      .Case<LogicalCZOp>([&](LogicalCZOp o) {
        std::string a = qname(o.getQubit0());
        std::string b = qname(o.getQubit1());
        propagate(o.getResult0(), o.getQubit0());
        propagate(o.getResult1(), o.getQubit1());
        emitCZ(a, b);
      })
      .Case<LogicalToffoliOp>([&](LogicalToffoliOp o) {
        std::string c0 = qname(o.getCtrl0());
        std::string c1 = qname(o.getCtrl1());
        std::string t  = qname(o.getTarget());
        propagate(o.getOutCtrl0(), o.getCtrl0());
        propagate(o.getOutCtrl1(), o.getCtrl1());
        propagate(o.getOutTarget(), o.getTarget());
        emitToffoli(c0, c1, t);
      })

      //----------------------------------------------------------------
      // Measurement
      //----------------------------------------------------------------
      .Case<LogicalMeasureOp>([&](LogicalMeasureOp o) {
        std::string qn  = qname(o.getLogicalQubit());
        std::string key = "m" + qn + "_" + std::to_string(measureIdx_++);
        emitMeasure(qn, key);
      })

      //----------------------------------------------------------------
      // Syndrome extraction
      //----------------------------------------------------------------
      .Case<SteaneZSyndromeOp>([&](SteaneZSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        if (expandSyndromes_) {
          std::string data = qname(o.getLogicalQubit());
          std::string anc  = freshAncilla();
          std::string key  = "sz_" + anc + "_" + std::to_string(measureIdx_++);
          emitH(anc);
          emitCZ(data, anc);
          emitH(anc);
          emitMeasure(anc, key);
        } else {
          emitComment("steane_z_syndrome");
        }
      })
      .Case<SteaneXSyndromeOp>([&](SteaneXSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        if (expandSyndromes_) {
          std::string data = qname(o.getLogicalQubit());
          std::string anc  = freshAncilla();
          std::string key  = "sx_" + anc + "_" + std::to_string(measureIdx_++);
          // X-syndrome: ancilla measures in X basis relative to data
          emitH(anc);
          emitH(data);
          emitCZ(anc, data);
          emitH(data);
          emitH(anc);
          emitMeasure(anc, key);
        } else {
          emitComment("steane_x_syndrome");
        }
      })
      .Case<ExtractSyndromeOp>([&](ExtractSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment("extract_syndrome");
      })
      .Case<FlagSyndromeOp>([&](FlagSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment("flag_syndrome");
      })
      .Case<ShorSyndromeOp>([&](ShorSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment("shor_syndrome");
      })

      //----------------------------------------------------------------
      // Error correction — propagate qubit, no physical gate.
      //----------------------------------------------------------------
      .Case<CorrectOp>([&](CorrectOp o) {
        propagate(o.getCorrectedQubit(), o.getLogicalQubit());
        emitComment("correct");
      })
      .Case<ECRoundOp>([&](ECRoundOp o) {
        propagate(o.getCorrectedQubit(), o.getLogicalQubit());
        emitComment("ec_round");
      })

      //----------------------------------------------------------------
      // Pauli frame — classical bookkeeping, no physical gate.
      //----------------------------------------------------------------
      .Case<PauliFrameUpdateOp>([&](PauliFrameUpdateOp) {
        emitComment("pauli_frame_update");
      })
      .Case<CreateFrameOp>([&](CreateFrameOp) {
        emitComment("create_frame");
      })
      .Case<ApplyFrameOp>([&](ApplyFrameOp o) {
        for (auto [in, out] : llvm::zip(o.getQubits(), o.getOutQubits()))
          propagate(out, in);
        emitComment("apply_frame");
      })

      //----------------------------------------------------------------
      // Distillation ops.
      //----------------------------------------------------------------
      .Case<Distill15to1Op>([&](Distill15to1Op) {
        emitComment("distill_15to1");
      })
      .Case<Distill7to1Op>([&](Distill7to1Op) {
        emitComment("distill_7to1");
      })
      .Case<GateTeleportOp>([&](GateTeleportOp o) {
        propagate(o.getResult(), o.getQubit());
        emitComment("gate_teleport");
      })

      //----------------------------------------------------------------
      // Annotations, encode/decode, func.return — skip silently.
      //----------------------------------------------------------------
      .Case<ResourceAnnotationOp>([&](ResourceAnnotationOp) {})
      .Case<EncodeOp>([&](EncodeOp)   { emitComment("encode"); })
      .Case<DecodeOp>([&](DecodeOp)   { emitComment("decode"); })
      .Case<func::ReturnOp>([&](func::ReturnOp) {})

      .Default([&](Operation *u) {
        emitComment("unsupported:" + u->getName().getStringRef().str());
      });
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCLowerToIQMJSONPassImpl
    : public ::impl::FTQCLowerToIQMJSONPassBase<FTQCLowerToIQMJSONPassImpl> {
  using FTQCLowerToIQMJSONPassBase::FTQCLowerToIQMJSONPassBase;

  void runOnOperation() override {
    auto mod = getOperation();

    IQMJSONEmitter emitter(includeComments, expandSyndromes, emitRadians);
    std::string json = emitter.emitModule(mod);

    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        mod.emitError("ftqc-lower-to-iqm-json: cannot open output file '")
            << outputFile << "': " << ec.message();
        signalPassFailure();
        return;
      }
      file << json;
    } else {
      llvm::errs() << json;
    }

    mod->setAttr("ftqc.iqm_json", StringAttr::get(mod.getContext(), json));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createLowerToIQMJSONPass() {
  return std::make_unique<FTQCLowerToIQMJSONPassImpl>();
}

std::unique_ptr<mlir::Pass>
mlir::ftqc::createLowerToIQMJSONPass(bool emitRadians) {
  FTQCLowerToIQMJSONPassOptions opts;
  opts.emitRadians = emitRadians;
  return std::make_unique<FTQCLowerToIQMJSONPassImpl>(opts);
}
