//===- QPhysToSTIM.cpp - Physical qphys IR -> Stim stabilizer circuit -----===//
//
// Level-2 STIM back end: consumes the *physical* `ftqc.qphys.*` IR produced by
// --ftqc-expand-physical and emits a Stim circuit over physical qubits.  Unlike
// the logical LowerToSTIM (which re-derives the physical expansion internally),
// this back end simulates *exactly* the physical circuit the other Level-2
// emitters submit.  It reconstructs `DETECTOR` records and the logical
// `OBSERVABLE_INCLUDE` from the QEC-structure metadata (role / stabilizer /
// round / logical) that expand-physical stamps on each qphys.measure:
//   * a stabilizer's first measurement yields a detector against the expected
//     zero syndrome, while later rounds yield round-difference detectors;
//   * the data measurements of a logical qubit XOR into its logical observable.
//
// Depolarizing noise is added at rate `physical-error-rate` (0 = noiseless).
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <vector>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct FTQCQPhysToSTIMPassImpl
    : public PassWrapper<FTQCQPhysToSTIMPassImpl, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCQPhysToSTIMPassImpl)

  FTQCQPhysToSTIMPassImpl() = default;
  FTQCQPhysToSTIMPassImpl(const FTQCQPhysToSTIMPassImpl &o) : PassWrapper(o) {}

  Option<double> physErr{
      *this, "physical-error-rate",
      llvm::cl::desc("Depolarizing error rate per physical op (0 = noiseless)"),
      llvm::cl::init(0.0)};

  StringRef getArgument() const override { return "ftqc-qphys-to-stim"; }
  StringRef getDescription() const override {
    return "Lower the physical qphys IR to a Stim circuit (with detectors)";
  }

  void runOnOperation() override {
    ModuleOp module = getOperation();
    double p = physErr;

    std::string buf;
    llvm::raw_string_ostream os(buf);

    llvm::DenseMap<Value, unsigned> q;   // qphys value -> compact Stim qubit id
    unsigned nQ = 0;
    long nMeas = 0;                       // measurement records emitted so far
    llvm::DenseMap<int64_t, long> lastForStab;  // stabilizer id -> last meas index
    llvm::MapVector<int64_t, std::vector<long>> dataMeas; // logical -> data meas indices

    auto idOf = [&](Value v) -> unsigned {
      auto it = q.find(v);
      return it == q.end() ? 0u : it->second;
    };
    auto rec = [&](long absIdx) {         // Stim measurement-record reference
      return std::string("rec[") + std::to_string(-(nMeas - absIdx)) + "]";
    };
    auto n1 = [&](StringRef g, Value in, Value out) {
      unsigned i = idOf(in);
      q[out] = i;
      os << g << " " << i << "\n";
      if (p > 0.0)
        os << "DEPOLARIZE1(" << p << ") " << i << "\n";
    };
    auto n2 = [&](StringRef g, Value a, Value b, Value oa, Value ob) {
      unsigned ia = idOf(a), ib = idOf(b);
      q[oa] = ia;
      q[ob] = ib;
      os << g << " " << ia << " " << ib << "\n";
      if (p > 0.0)
        os << "DEPOLARIZE2(" << p << ") " << ia << " " << ib << "\n";
    };

    module.walk([&](func::FuncOp func) {
      func.walk([&](Operation *op) {
        if (auto a = dyn_cast<QPhysAllocOp>(op)) {
          q[a.getQubit()] = nQ++;
        } else if (auto r = dyn_cast<QPhysResetOp>(op)) {
          unsigned i = idOf(r.getIn());
          q[r.getOut()] = i;
          os << "R " << i << "\n";
        } else if (auto o = dyn_cast<QPhysHOp>(op)) {
          n1("H", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysXOp>(op)) {
          n1("X", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysZOp>(op)) {
          n1("Z", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysSOp>(op)) {
          n1("S", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysTOp>(op)) {
          // Stim is Clifford-only: approximate T by S (tracks Clifford errors).
          os << "# qphys.t approximated as S (Stim is Clifford-only)\n";
          n1("S", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysCXOp>(op)) {
          n2("CX", o.getQ0(), o.getQ1(), o.getResult(0), o.getResult(1));
        } else if (auto o = dyn_cast<QPhysCZOp>(op)) {
          n2("CZ", o.getQ0(), o.getQ1(), o.getResult(0), o.getResult(1));
        } else if (auto o = dyn_cast<QPhysSwapOp>(op)) {
          n2("SWAP", o.getQ0(), o.getQ1(), o.getResult(0), o.getResult(1));
        } else if (auto m = dyn_cast<QPhysMeasureOp>(op)) {
          unsigned i = idOf(m.getIn());
          if (p > 0.0)
            os << "X_ERROR(" << p << ") " << i << "\n";
          os << "M " << i << "\n";
          long cur = nMeas++;
          // The first measurement is compared against the implicit zero
          // syndrome.  Later measurements are compared with the same
          // stabilizer from the preceding round.
          if (auto sAttr = m.getStabilizer()) {
            int64_t sid = *sAttr;
            auto it = lastForStab.find(sid);
            if (it != lastForStab.end())
              os << "DETECTOR " << rec(cur) << " " << rec(it->second) << "\n";
            else
              os << "DETECTOR " << rec(cur) << "\n";
            lastForStab[sid] = cur;
          }
          // Data measurements feed the logical observable.
          if (auto role = m.getRole(); role && *role == "data") {
            int64_t lg = m.getLogical() ? *m.getLogical() : 0;
            dataMeas[lg].push_back(cur);
          }
        }
      });
    });

    // Logical observable per logical qubit = parity of its data measurements.
    long obs = 0;
    for (auto &kv : dataMeas) {
      os << "OBSERVABLE_INCLUDE(" << obs++ << ")";
      for (long a : kv.second)
        os << " " << rec(a);
      os << "\n";
    }

    os.flush();
    llvm::outs() << buf << "\n";
    module->setAttr("ftqc.stim_phys", StringAttr::get(&getContext(), buf));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createQPhysToSTIMPass() {
  return std::make_unique<FTQCQPhysToSTIMPassImpl>();
}
