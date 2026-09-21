//===- QPhysToIQMJSON.cpp - Physical qphys IR -> IQM JSON -----------------===//
//
// Retargets the IQM JSON back end to consume the *physical* `ftqc.qphys.*` IR
// (produced by --ftqc-expand-physical) instead of re-deriving the Steane
// physical layout inside the emitter.  Each qphys qubit maps to a hardware
// locus "QB<id+1>"; physical gates are decomposed into IQM native gates
// {prx, cz, measure} (angles in turns), using the same decompositions as the
// logical LowerToIQMJSON emitter.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <vector>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct FTQCQPhysToIQMJSONPassImpl
    : public PassWrapper<FTQCQPhysToIQMJSONPassImpl, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCQPhysToIQMJSONPassImpl)

  StringRef getArgument() const override { return "ftqc-qphys-to-iqm-json"; }
  StringRef getDescription() const override {
    return "Lower the physical qphys IR to IQM JSON";
  }

  // Instruction accumulator for the current function.
  std::vector<std::string> instrs;

  static std::string fmtF(double v) {
    char b[32];
    snprintf(b, sizeof(b), "%g", v);
    return b;
  }
  void prx(const std::string &q, double a, double p) {
    instrs.push_back("{\"name\":\"prx\",\"locus\":[\"" + q +
                     "\"],\"args\":{\"angle_t\":" + fmtF(a) +
                     ",\"phase_t\":" + fmtF(p) + "}}");
  }
  void cz(const std::string &a, const std::string &b) {
    instrs.push_back("{\"name\":\"cz\",\"locus\":[\"" + a + "\",\"" + b +
                     "\"],\"args\":{}}");
  }
  void meas(const std::string &q, const std::string &key, int n) {
    instrs.push_back("{\"name\":\"measure\",\"locus\":[\"" + q +
                     "\"],\"args\":{\"key\":\"" + key + "_" + q + "_" +
                     std::to_string(n) + "\"}}");
  }
  void reset(const std::string &q) {
    instrs.push_back("{\"name\":\"reset\",\"locus\":[\"" + q +
                     "\"],\"args\":{}}");
  }
  // IQM native decompositions (turns; identical to LowerToIQMJSON).
  void emitX(const std::string &q) { prx(q, 0.5, 0.0); }
  void emitZ(const std::string &q) {
    prx(q, 0.75, 0.0);
    prx(q, 0.5, 0.25);
    prx(q, 0.25, 0.0);
  }
  void emitS(const std::string &q) {
    prx(q, 0.75, 0.0);
    prx(q, 0.25, 0.25);
    prx(q, 0.25, 0.0);
  }
  void emitT(const std::string &q) {
    prx(q, 0.75, 0.0);
    prx(q, 0.125, 0.25);
    prx(q, 0.25, 0.0);
  }
  void emitH(const std::string &q) {
    prx(q, 0.25, 0.25);
    prx(q, 0.5, 0.0);
  }
  void emitCNOT(const std::string &c, const std::string &t) {
    emitH(t);
    cz(c, t);
    emitH(t);
  }

  void runOnOperation() override {
    ModuleOp module = getOperation();
    std::string buf;
    llvm::raw_string_ostream os(buf);

    module.walk([&](func::FuncOp func) {
      llvm::DenseMap<Value, std::string> loc; // qphys value -> "QB<n>"
      instrs.clear();
      int measCount = 0;
      auto L = [&](Value v) {
        return loc.count(v) ? loc[v] : std::string("QB?");
      };

      func.walk([&](Operation *op) {
        if (auto a = dyn_cast<QPhysAllocOp>(op)) {
          loc[a.getQubit()] = "QB" + std::to_string((int64_t)a.getId() + 1);
        } else if (auto g = dyn_cast<QPhysResetOp>(op)) {
          loc[g.getOut()] = L(g.getIn());
          //[FIXME] IQM Garnet does not support reset.
          // reset(L(g.getIn()));
        } else if (auto g = dyn_cast<QPhysHOp>(op)) {
          loc[g.getOut()] = L(g.getIn());
          emitH(L(g.getIn()));
        } else if (auto g = dyn_cast<QPhysXOp>(op)) {
          loc[g.getOut()] = L(g.getIn());
          emitX(L(g.getIn()));
        } else if (auto g = dyn_cast<QPhysZOp>(op)) {
          loc[g.getOut()] = L(g.getIn());
          emitZ(L(g.getIn()));
        } else if (auto g = dyn_cast<QPhysSOp>(op)) {
          loc[g.getOut()] = L(g.getIn());
          emitS(L(g.getIn()));
        } else if (auto g = dyn_cast<QPhysTOp>(op)) {
          loc[g.getOut()] = L(g.getIn());
          emitT(L(g.getIn()));
        } else if (auto g = dyn_cast<QPhysCXOp>(op)) {
          loc[g.getResult(0)] = L(g.getQ0());
          loc[g.getResult(1)] = L(g.getQ1());
          emitCNOT(L(g.getQ0()), L(g.getQ1()));
        } else if (auto g = dyn_cast<QPhysCZOp>(op)) {
          loc[g.getResult(0)] = L(g.getQ0());
          loc[g.getResult(1)] = L(g.getQ1());
          cz(L(g.getQ0()), L(g.getQ1()));
        } else if (auto g = dyn_cast<QPhysSwapOp>(op)) {
          loc[g.getResult(0)] = L(g.getQ0());
          loc[g.getResult(1)] = L(g.getQ1());
          emitCNOT(L(g.getQ0()), L(g.getQ1())); // SWAP = 3 CNOTs
          emitCNOT(L(g.getQ1()), L(g.getQ0()));
          emitCNOT(L(g.getQ0()), L(g.getQ1()));
        } else if (auto m = dyn_cast<QPhysMeasureOp>(op)) {
          std::string key = "m";
          auto keyAttr = m.getKey();
          if (keyAttr) {
            key = keyAttr.value().str();
          }
          auto stabAttr = m.getStabilizer();
          auto logAttr = m.getLogical();
          auto roleAttr = m.getRole();
          if (roleAttr && logAttr) {
            auto role = roleAttr.value().str();
            auto log = logAttr.value();
            if (role == "data") {
              key += "L" + std::to_string(log);
            } else {
              if (stabAttr) {
                auto stab = stabAttr.value();
                key +=
                    "L" + std::to_string(log) + "S" + std::to_string(stab % 6);
              } else {
                key += "L" + std::to_string(log);
              }
            }
          }
          meas(L(m.getIn()), key, measCount++);
        }
      });

      os << "{\n  \"name\":\"" << func.getName()
         << "\",\n  \"instructions\":[\n";
      for (size_t i = 0; i < instrs.size(); ++i)
        os << "    " << instrs[i] << (i + 1 < instrs.size() ? ",\n" : "\n");
      os << "  ]\n}\n";
    });

    os.flush();
    llvm::outs() << buf;
    module->setAttr("ftqc.iqm_json", StringAttr::get(&getContext(), buf));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createQPhysToIQMJSONPass() {
  return std::make_unique<FTQCQPhysToIQMJSONPassImpl>();
}
