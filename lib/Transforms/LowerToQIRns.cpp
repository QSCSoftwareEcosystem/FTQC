//===- LowerToQIRns.cpp - FTQC → QIR Base Profile (no ECC expansion) ----===//
//
// Translates a post-pipeline FTQC module to QIR Base Profile LLVM IR
// without physical-qubit block expansion.
//
// Mapping mirrors LowerToQASM3 semantics:
//   ftqc.init_zero / init_plus   → qubit allocation (+ h for |+>)
//   ftqc.logical_{x,y,z,h,s,t}   → single QIS gate call
//   ftqc.logical_cnot            → single cnot
//   ftqc.logical_cz              → single cz
//   ftqc.logical_toffoli         → single ccx
//   ftqc.logical_measure         → single mz + result slot
//
// FT-infrastructure ops (syndrome, correction, Pauli-frame) emit LLVM IR
// comments when --include-comments=true (default).
//
// Output is written to stderr (or --output-file) and also attached to the
// module as the string attribute "ftqc.qir".
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_FTQCLOWERTOQIRNSPASS
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

namespace {

struct QIRnsFuncResult {
  std::string funcName;
  std::string body;
  std::set<std::string> decls;
  unsigned numQubits = 0;
  unsigned numResults = 0;
};

class QIRnsEmitter {
public:
  explicit QIRnsEmitter(bool includeComments)
      : includeComments(includeComments) {}

  std::string emitModule(ModuleOp mod) {
    auto moduleId = mod.getName().value_or("ftqc_module");

    std::vector<QIRnsFuncResult> results;
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

    unsigned totalQubits = 0;
    unsigned totalResults = 0;
    for (auto &r : results) {
      totalQubits = std::max(totalQubits, r.numQubits);
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

  DenseMap<Value, unsigned> qubitMap; // logical SSA value -> QIR qubit index
  unsigned nextQubit = 0;
  unsigned nextResult = 0;

  void reset() {
    qubitMap.clear();
    nextQubit = 0;
    nextResult = 0;
  }

  static std::string qref(unsigned idx) {
    if (idx == 0)
      return "ptr null";
    return "ptr inttoptr (i64 " + std::to_string(idx) + " to ptr)";
  }

  static std::string pref(unsigned idx) { return qref(idx); }

  unsigned allocQubit() { return nextQubit++; }

  void reg(Value v, unsigned q) { qubitMap[v] = q; }

  void propagate(Value newV, Value oldV) {
    auto it = qubitMap.find(oldV);
    if (it != qubitMap.end())
      qubitMap[newV] = it->second;
  }

  unsigned q(Value v) {
    auto it = qubitMap.find(v);
    if (it != qubitMap.end())
      return it->second;
    unsigned idx = allocQubit();
    qubitMap[v] = idx;
    return idx;
  }

  void emitComment(llvm::raw_ostream &os, const char *msg) {
    if (includeComments)
      os << "  ; " << msg << "\n";
  }

  void gate1q(llvm::raw_ostream &os, std::set<std::string> &decls,
              const char *name, unsigned q0) {
    std::string fn = std::string("@__quantum__qis__") + name + "__body";
    os << "  call void " << fn << "(" << qref(q0) << ")\n";
    decls.insert("declare void " + fn + "(ptr)");
  }

  void gate2q(llvm::raw_ostream &os, std::set<std::string> &decls,
              const char *name, unsigned q0, unsigned q1) {
    std::string fn = std::string("@__quantum__qis__") + name + "__body";
    os << "  call void " << fn << "(" << qref(q0) << ", " << qref(q1)
       << ")\n";
    decls.insert("declare void " + fn + "(ptr, ptr)");
  }

  void gate3q(llvm::raw_ostream &os, std::set<std::string> &decls,
              const char *name, unsigned q0, unsigned q1, unsigned q2) {
    std::string fn = std::string("@__quantum__qis__") + name + "__body";
    os << "  call void " << fn << "(" << qref(q0) << ", " << qref(q1)
       << ", " << qref(q2) << ")\n";
    decls.insert("declare void " + fn + "(ptr, ptr, ptr)");
  }

  QIRnsFuncResult emitFunc(func::FuncOp func) {
    reset();
    QIRnsFuncResult r;
    r.funcName = func.getName().str();

    std::string bodyStr;
    llvm::raw_string_ostream os(bodyStr);

    for (auto arg : func.getArguments()) {
      if (isa<LogicalQubitType>(arg.getType()))
        reg(arg, allocQubit());
    }

    if (!func.empty())
      for (auto &op : func.getBody().front())
        emitOp(&op, os, r.decls);

    r.body = bodyStr;
    r.numQubits = nextQubit;
    r.numResults = nextResult;
    return r;
  }

  void emitOp(Operation *op, llvm::raw_ostream &os,
              std::set<std::string> &decls) {
    llvm::TypeSwitch<Operation *>(op)

        .Case<InitZeroOp>([&](InitZeroOp o) {
          reg(o.getResult(), allocQubit());
        })
        .Case<InitPlusOp>([&](InitPlusOp o) {
          unsigned qIdx = allocQubit();
          reg(o.getResult(), qIdx);
          gate1q(os, decls, "h", qIdx);
        })
        .Case<DeallocOp>([&](DeallocOp) { emitComment(os, "dealloc"); })

        .Case<LogicalHOp>([&](LogicalHOp o) {
          unsigned qn = q(o.getQubit());
          propagate(o.getResult(), o.getQubit());
          gate1q(os, decls, "h", qn);
        })
        .Case<LogicalXOp>([&](LogicalXOp o) {
          unsigned qn = q(o.getQubit());
          propagate(o.getResult(), o.getQubit());
          gate1q(os, decls, "x", qn);
        })
        .Case<LogicalYOp>([&](LogicalYOp o) {
          unsigned qn = q(o.getQubit());
          propagate(o.getResult(), o.getQubit());
          gate1q(os, decls, "y", qn);
        })
        .Case<LogicalZOp>([&](LogicalZOp o) {
          unsigned qn = q(o.getQubit());
          propagate(o.getResult(), o.getQubit());
          gate1q(os, decls, "z", qn);
        })
        .Case<LogicalSOp>([&](LogicalSOp o) {
          unsigned qn = q(o.getQubit());
          propagate(o.getResult(), o.getQubit());
          gate1q(os, decls, "s", qn);
        })

        .Case<PrepareMagicTOp>([&](PrepareMagicTOp o) {
          reg(o.getResult(), allocQubit());
          emitComment(os, "prepare_magic_t");
        })
        .Case<PrepareMagicCCZOp>([&](PrepareMagicCCZOp o) {
          reg(o.getResult(), allocQubit());
          emitComment(os, "prepare_magic_ccz");
        })
        .Case<LogicalTOp>([&](LogicalTOp o) {
          unsigned qn = q(o.getQubit());
          propagate(o.getResult(), o.getQubit());
          gate1q(os, decls, "t", qn);
        })

        .Case<LogicalCNOTOp>([&](LogicalCNOTOp o) {
          unsigned c = q(o.getControl());
          unsigned t = q(o.getTarget());
          propagate(o.getControlOut(), o.getControl());
          propagate(o.getTargetOut(), o.getTarget());
          gate2q(os, decls, "cnot", c, t);
        })
        .Case<LogicalCZOp>([&](LogicalCZOp o) {
          unsigned a = q(o.getQubit0());
          unsigned b = q(o.getQubit1());
          propagate(o.getResult0(), o.getQubit0());
          propagate(o.getResult1(), o.getQubit1());
          gate2q(os, decls, "cz", a, b);
        })
        .Case<LogicalToffoliOp>([&](LogicalToffoliOp o) {
          unsigned c0 = q(o.getCtrl0());
          unsigned c1 = q(o.getCtrl1());
          unsigned t = q(o.getTarget());
          propagate(o.getOutCtrl0(), o.getCtrl0());
          propagate(o.getOutCtrl1(), o.getCtrl1());
          propagate(o.getOutTarget(), o.getTarget());
          gate3q(os, decls, "ccx", c0, c1, t);
        })

        .Case<LogicalMeasureOp>([&](LogicalMeasureOp o) {
          unsigned qn = q(o.getLogicalQubit());
          unsigned rIdx = nextResult++;
          os << "  call void @__quantum__qis__mz__body(" << qref(qn) << ", "
             << pref(rIdx) << ")\n";
          decls.insert(
              "declare void @__quantum__qis__mz__body(ptr, ptr writeonly) #1");
        })

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

        .Case<CorrectOp>([&](CorrectOp o) {
          propagate(o.getCorrectedQubit(), o.getLogicalQubit());
          emitComment(os, "correct");
        })
        .Case<ECRoundOp>([&](ECRoundOp o) {
          propagate(o.getCorrectedQubit(), o.getLogicalQubit());
          emitComment(os, "ec_round");
        })

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

        .Case<ResourceAnnotationOp>([&](ResourceAnnotationOp) {})
        .Case<EncodeOp>([&](EncodeOp) { emitComment(os, "encode (physical→logical)"); })
        .Case<DecodeOp>([&](DecodeOp) { emitComment(os, "decode (logical→physical)"); })
        .Case<func::ReturnOp>([&](func::ReturnOp) {})

        .Default([&](Operation *unknownOp) {
          if (includeComments)
            os << "  ; unsupported: " << unknownOp->getName().getStringRef()
               << "\n";
        });
  }
};

} // namespace

namespace {

struct FTQCLowerToQIRnsPassImpl
    : public ::impl::FTQCLowerToQIRnsPassBase<FTQCLowerToQIRnsPassImpl> {
  using FTQCLowerToQIRnsPassBase::FTQCLowerToQIRnsPassBase;

  void runOnOperation() override {
    auto mod = getOperation();

    QIRnsEmitter emitter(includeComments);
    std::string qir = emitter.emitModule(mod);

    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        mod.emitError("ftqc-lower-to-qir-ns: cannot open output file '")
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

std::unique_ptr<mlir::Pass> mlir::ftqc::createLowerToQIRnsPass() {
  return std::make_unique<FTQCLowerToQIRnsPassImpl>();
}
