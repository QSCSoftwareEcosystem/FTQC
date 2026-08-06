//===- QPhysToQASM3.cpp - Physical qphys IR -> OpenQASM 3.0 ---------------===//
//
// Demonstrates that a back end can consume the *physical* `ftqc.qphys.*` IR
// (produced by --ftqc-expand-physical) instead of re-deriving the physical
// qubit layout inside every emitter.  Walks the physical ops in order, tracks
// each qphys value's hardware id, and prints physical-qubit OpenQASM 3.0.
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

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct FTQCQPhysToQASM3PassImpl
    : public PassWrapper<FTQCQPhysToQASM3PassImpl, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCQPhysToQASM3PassImpl)

  StringRef getArgument() const override { return "ftqc-qphys-to-qasm3"; }
  StringRef getDescription() const override {
    return "Lower the physical qphys IR to OpenQASM 3.0";
  }

  void runOnOperation() override {
    ModuleOp module = getOperation();
    std::string buf;
    llvm::raw_string_ostream os(buf);
    os << "OPENQASM 3.0;\ninclude \"stdgates.inc\";\n";

    module.walk([&](func::FuncOp func) {
      llvm::DenseMap<Value, int64_t> hw; // qphys value -> hardware qubit id
      int64_t maxId = -1;
      int bit = 0;
      std::string body;
      llvm::raw_string_ostream bs(body);

      auto id = [&](Value v) { return hw.count(v) ? hw[v] : -1; };

      func.walk([&](Operation *op) {
        if (auto a = dyn_cast<QPhysAllocOp>(op)) {
          int64_t q = (int64_t)a.getId();
          hw[a.getQubit()] = q;
          maxId = std::max(maxId, q);
        } else if (auto r = dyn_cast<QPhysResetOp>(op)) {
          hw[r.getOut()] = id(r.getIn());
          bs << "reset q" << id(r.getIn()) << ";\n";
        } else if (auto g = dyn_cast<QPhysHOp>(op)) {
          hw[g.getOut()] = id(g.getIn());
          bs << "h q" << id(g.getIn()) << ";\n";
        } else if (auto g = dyn_cast<QPhysXOp>(op)) {
          hw[g.getOut()] = id(g.getIn());
          bs << "x q" << id(g.getIn()) << ";\n";
        } else if (auto g = dyn_cast<QPhysZOp>(op)) {
          hw[g.getOut()] = id(g.getIn());
          bs << "z q" << id(g.getIn()) << ";\n";
        } else if (auto g = dyn_cast<QPhysSOp>(op)) {
          hw[g.getOut()] = id(g.getIn());
          bs << "s q" << id(g.getIn()) << ";\n";
        } else if (auto g = dyn_cast<QPhysTOp>(op)) {
          hw[g.getOut()] = id(g.getIn());
          bs << "t q" << id(g.getIn()) << ";\n";
        } else if (auto g = dyn_cast<QPhysCXOp>(op)) {
          hw[g.getResult(0)] = id(g.getQ0());
          hw[g.getResult(1)] = id(g.getQ1());
          bs << "cx q" << id(g.getQ0()) << ", q" << id(g.getQ1()) << ";\n";
        } else if (auto g = dyn_cast<QPhysCZOp>(op)) {
          hw[g.getResult(0)] = id(g.getQ0());
          hw[g.getResult(1)] = id(g.getQ1());
          bs << "cz q" << id(g.getQ0()) << ", q" << id(g.getQ1()) << ";\n";
        } else if (auto g = dyn_cast<QPhysSwapOp>(op)) {
          hw[g.getResult(0)] = id(g.getQ0());
          hw[g.getResult(1)] = id(g.getQ1());
          bs << "swap q" << id(g.getQ0()) << ", q" << id(g.getQ1()) << ";\n";
        } else if (auto m = dyn_cast<QPhysMeasureOp>(op)) {
          bs << "bit b" << bit++ << " = measure q" << id(m.getIn()) << ";\n";
        }
      });

      os << "\n// function @" << func.getName() << "\n";
      for (int64_t q = 0; q <= maxId; ++q)
        os << "qubit q" << q << ";\n";
      os << bs.str();
    });

    os.flush();
    llvm::outs() << buf << "\n";
    module->setAttr("ftqc.qasm3_phys",
                    StringAttr::get(&getContext(), buf));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createQPhysToQASM3Pass() {
  return std::make_unique<FTQCQPhysToQASM3PassImpl>();
}
