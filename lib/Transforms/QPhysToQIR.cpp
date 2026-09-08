//===- QPhysToQIR.cpp - Physical qphys IR -> QIR (LLVM IR) ----------------===//
//
// Level-2 back end: consumes the *physical* `ftqc.qphys.*` IR produced by
// --ftqc-expand-physical and emits QIR (base profile) over physical qubits,
// instead of re-deriving the physical layout inside the emitter.  Companion to
// QPhysToQASM3 / QPhysToIQMJSON.  Walks the physical ops in program order,
// assigns each qphys value a compact QIR qubit index, and prints one QIR
// function per func::FuncOp.
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

#include <set>
#include <string>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

/// QIR pointer literal for a qubit/result index (index 0 prints as `ptr null`).
static std::string ref(unsigned idx) {
  if (idx == 0)
    return "ptr null";
  return "ptr inttoptr (i64 " + std::to_string(idx) + " to ptr)";
}

struct FTQCQPhysToQIRPassImpl
    : public PassWrapper<FTQCQPhysToQIRPassImpl, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCQPhysToQIRPassImpl)

  StringRef getArgument() const override { return "ftqc-qphys-to-qir"; }
  StringRef getDescription() const override {
    return "Lower the physical qphys IR to QIR (base profile)";
  }

  void runOnOperation() override {
    ModuleOp module = getOperation();
    auto moduleId = module.getName().value_or("ftqc_module");

    std::set<std::string> decls;
    std::string funcs;
    llvm::raw_string_ostream fs(funcs);
    unsigned totalQubits = 0, totalResults = 0;

    auto decl1 = [&](StringRef n) {
      decls.insert(("declare void @__quantum__qis__" + n + "__body(ptr)").str());
    };
    auto decl2 = [&](StringRef n) {
      decls.insert(
          ("declare void @__quantum__qis__" + n + "__body(ptr, ptr)").str());
    };

    module.walk([&](func::FuncOp func) {
      llvm::DenseMap<Value, unsigned> qidx; // qphys value -> compact QIR index
      unsigned nQ = 0, nR = 0;
      std::string body;
      llvm::raw_string_ostream bs(body);

      auto idOf = [&](Value v) -> unsigned {
        auto it = qidx.find(v);
        return it == qidx.end() ? 0u : it->second;
      };
      auto g1 = [&](StringRef n, Value in, Value out) {
        unsigned q = idOf(in);
        qidx[out] = q;
        bs << "  call void @__quantum__qis__" << n << "__body(" << ref(q) << ")\n";
        decl1(n);
      };
      auto g2 = [&](StringRef n, Value a, Value b, Value oa, Value ob) {
        unsigned qa = idOf(a), qb = idOf(b);
        qidx[oa] = qa;
        qidx[ob] = qb;
        bs << "  call void @__quantum__qis__" << n << "__body(" << ref(qa)
           << ", " << ref(qb) << ")\n";
        decl2(n);
      };

      func.walk([&](Operation *op) {
        if (auto a = dyn_cast<QPhysAllocOp>(op)) {
          qidx[a.getQubit()] = nQ++;
        } else if (auto r = dyn_cast<QPhysResetOp>(op)) {
          g1("reset", r.getIn(), r.getOut());
        } else if (auto o = dyn_cast<QPhysHOp>(op)) {
          g1("h", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysXOp>(op)) {
          g1("x", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysZOp>(op)) {
          g1("z", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysSOp>(op)) {
          g1("s", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysTOp>(op)) {
          g1("t", o.getIn(), o.getOut());
        } else if (auto o = dyn_cast<QPhysCXOp>(op)) {
          g2("cnot", o.getQ0(), o.getQ1(), o.getResult(0), o.getResult(1));
        } else if (auto o = dyn_cast<QPhysCZOp>(op)) {
          g2("cz", o.getQ0(), o.getQ1(), o.getResult(0), o.getResult(1));
        } else if (auto o = dyn_cast<QPhysSwapOp>(op)) {
          g2("swap", o.getQ0(), o.getQ1(), o.getResult(0), o.getResult(1));
        } else if (auto m = dyn_cast<QPhysMeasureOp>(op)) {
          bs << "  call void @__quantum__qis__mz__body(" << ref(idOf(m.getIn()))
             << ", " << ref(nR) << ")\n";
          ++nR;
          decls.insert("declare void @__quantum__qis__mz__body(ptr, "
                       "ptr writeonly) #1");
        }
      });

      totalQubits  = std::max(totalQubits, nQ);
      totalResults = std::max(totalResults, nR);

      fs << "define void @" << func.getName() << "() #0 {\nentry:\n";
      fs << "  call void @__quantum__rt__initialize(ptr null)\n";
      fs << bs.str();
      fs << "  call void @__quantum__rt__array_record_output(i64 " << nR
         << ", ptr null)\n";
      for (unsigned i = 0; i < nR; ++i)
        fs << "  call void @__quantum__rt__result_record_output(" << ref(i)
           << ", ptr null)\n";
      fs << "  ret void\n}\n\n";
    });

    decls.insert("declare void @__quantum__rt__initialize(ptr)");
    decls.insert("declare void @__quantum__rt__array_record_output(i64, ptr)");
    decls.insert("declare void @__quantum__rt__result_record_output(ptr, ptr)");
    bool hasMeas =
        decls.count("declare void @__quantum__qis__mz__body(ptr, ptr writeonly) #1");

    std::string out;
    llvm::raw_string_ostream os(out);
    os << "; ModuleID = '" << moduleId << "'\n";
    os << "source_filename = \"" << moduleId << "\"\n\n";
    fs.flush();
    os << funcs;
    for (const auto &d : decls)
      os << d << "\n";
    os << "\nattributes #0 = { \"entry_point\" \"output_labeling_schema\" "
       << "\"qir_profiles\"=\"base\" "
       << "\"required_num_qubits\"=\"" << totalQubits << "\" "
       << "\"required_num_results\"=\"" << totalResults << "\" }\n";
    if (hasMeas)
      os << "attributes #1 = { \"irreversible\" }\n";
    os << "\n!llvm.module.flags = !{!0, !1, !2, !3}\n\n";
    os << "!0 = !{i32 1, !\"qir_major_version\", i32 1}\n";
    os << "!1 = !{i32 7, !\"qir_minor_version\", i32 0}\n";
    os << "!2 = !{i32 1, !\"dynamic_qubit_management\", i1 false}\n";
    os << "!3 = !{i32 1, !\"dynamic_result_management\", i1 false}\n";
    os.flush();

    llvm::outs() << out << "\n";
    module->setAttr("ftqc.qir_phys", StringAttr::get(&getContext(), out));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createQPhysToQIRPass() {
  return std::make_unique<FTQCQPhysToQIRPassImpl>();
}
