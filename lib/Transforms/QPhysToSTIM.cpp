//===- QPhysToSTIM.cpp - Physical qphys IR -> Stim stabilizer circuit -----===//
//
// Level-2 STIM back end: consumes the *physical* `ftqc.qphys.*` IR produced by
// --ftqc-expand-physical and emits a Stim circuit over physical qubits.  Unlike
// the logical LowerToSTIM (which re-derives the physical expansion internally),
// this back end simulates *exactly* the physical circuit the other Level-2
// emitters submit.  It reconstructs `DETECTOR` records and the logical
// `OBSERVABLE_INCLUDE` from the QEC-structure metadata (role / stabilizer /
// round / logical) that expand-physical stamps on each qphys.measure:
//   * a stabilizer measured in round r and r-1 yields a round-difference
//     detector (always deterministic under no noise);
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

#define GEN_PASS_DEF_FTQCQPHYSTOSTIMPASS
#include "FTQC/Transforms/Passes.h.inc"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <system_error>
#include <vector>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct FTQCQPhysToSTIMPassImpl
    : public ::impl::FTQCQPhysToSTIMPassBase<FTQCQPhysToSTIMPassImpl> {
  using FTQCQPhysToSTIMPassBase::FTQCQPhysToSTIMPassBase;

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
    llvm::DenseMap<Value, long> measIdx;
    long obs = 0;
    bool error = false;

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
          // Round-difference detector: same stabilizer, consecutive rounds.
          if (auto sAttr = m.getStabilizer()) {
            int64_t sid = *sAttr;
            auto it = lastForStab.find(sid);
            if (it != lastForStab.end())
              os << "DETECTOR " << rec(cur) << " " << rec(it->second) << "\n";
            lastForStab[sid] = cur;
          }
          // Data measurements feed the logical observable.
          if (auto role = m.getRole(); role && *role == "data") {
            int64_t lg = m.getLogical() ? *m.getLogical() : 0;
            dataMeas[lg].push_back(cur);
          }
          measIdx[m] = cur;
        } else if (auto d = dyn_cast<QPhysDetectorOp>(op)) {
          os << "DETECTOR";
          for (Value v : d.getMeasurements()) {
            if (auto it = measIdx.find(v); it != measIdx.end()) {
              os << " " << rec(it->second);
            } else {
              d.emitError("ftqc-qphys-to-stim: could not determine "
                          "corresponding measurement for detector operand");
              error = true;
              return;
            }
          }
          os << "\n";
        } else if (auto o = dyn_cast<QPhysObservableOp>(op)) {
          os << "OBSERVABLE_INCLUDE(" << obs++ << ")";
          for (Value v : o.getMeasurements()) {
            if (auto it = measIdx.find(v); it != measIdx.end()) {
              os << " " << rec(it->second);
            } else {
              o.emitError("ftqc-qphys-to-stim: could not determine "
                          "corresponding measurement for observable operand");
              error = true;
              return;
            }
          }
          os << "\n";
        }
      });
    });

    // Logical observable per logical qubit = parity of its data measurements.
    for (auto &kv : dataMeas) {
      os << "OBSERVABLE_INCLUDE(" << obs++ << ")";
      for (long a : kv.second)
        os << " " << rec(a);
      os << "\n";
    }

    if (error) {
      signalPassFailure();
      return;
    }

    os.flush();
    module->setAttr("ftqc.stim_phys", StringAttr::get(&getContext(), buf));

    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        module.emitError("ftqc-qphys-to-stim: cannot open output file '")
            << outputFile << "': " << ec.message();
        signalPassFailure();
        return;
      }
      file << buf;
    } else {
      llvm::errs() << buf;
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createQPhysToSTIMPass() {
  return std::make_unique<FTQCQPhysToSTIMPassImpl>();
}
