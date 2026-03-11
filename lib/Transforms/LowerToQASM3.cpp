//===- LowerToQASM3.cpp - FTQC → OpenQASM 3.0 translation ----------------===//
//
// Translates a post-pipeline FTQC module to an OpenQASM 3.0 string.
//
// Gate mapping:
//   ftqc.init_zero / init_plus   → qubit declaration (+ h for |+>)
//   ftqc.logical_{x,y,z,h,s}    → x / y / z / h / s
//   ftqc.logical_t               → t  (magic-state consumed silently)
//   ftqc.logical_cnot            → cx
//   ftqc.logical_cz              → cz
//   ftqc.logical_toffoli         → ccx
//   ftqc.logical_measure         → bit bN = measure qN;
//
// FT-infrastructure ops (syndrome, correction, Pauli-frame) emit a QASM
// comment when --include-comments=true (default).
//
// Output is written to stderr (or --output-file) and also attached to the
// module as the string attribute "ftqc.qasm3" so that FileCheck tests can
// pattern-match it after combining stdout+stderr with  2>&1 | FileCheck %s.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

// Pull in the TableGen-generated base class for FTQCLowerToQASM3Pass.
// This provides getArgument(), getDescription(), clonePass(), option parsing,
// and the protected 'outputFile' / 'includeComments' Option<> members.
#define GEN_PASS_DEF_FTQCLOWERTOQASM3PASS
#include "FTQC/Transforms/Passes.h.inc"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"

#include <system_error>

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// QASM3Emitter
//===----------------------------------------------------------------------===//

namespace {

class QASM3Emitter {
public:
  explicit QASM3Emitter(bool includeComments)
      : includeComments(includeComments) {}

  /// Emit the full module and return the QASM 3.0 string.
  std::string emitModule(ModuleOp mod) {
    os << "OPENQASM 3.0;\n"
       << "include \"stdgates.inc\";\n";
    for (auto func : mod.getOps<func::FuncOp>())
      emitFunc(func);
    return os.str();
  }

private:
  bool includeComments;
  std::string buf;
  llvm::raw_string_ostream os{buf};

  /// Map: SSA qubit value → QASM qubit name.
  DenseMap<Value, std::string> qubitMap;
  int qubitIdx = 0;
  int bitIdx   = 0;

  std::string freshQubit() { return "q" + std::to_string(qubitIdx++); }
  std::string freshBit()   { return "b" + std::to_string(bitIdx++); }

  void reg(Value v, const std::string &name) { qubitMap[v] = name; }

  /// Propagate the qubit name through a gate: newV is the same physical qubit
  /// as oldV (just a new SSA use).
  void propagate(Value newV, Value oldV) {
    auto it = qubitMap.find(oldV);
    if (it != qubitMap.end())
      qubitMap[newV] = it->second;
  }

  /// Look up or lazily assign a qubit name.
  std::string q(Value v) {
    auto it = qubitMap.find(v);
    if (it != qubitMap.end())
      return it->second;
    // Fallback: assign a fresh name (should not happen with well-formed IR).
    std::string n = freshQubit();
    qubitMap[v] = n;
    os << "qubit " << n << "; // auto-allocated\n";
    return n;
  }

  void emitComment(const char *msg) {
    if (includeComments)
      os << "// " << msg << "\n";
  }

  void emitFunc(func::FuncOp func) {
    os << "\n// function @" << func.getName() << "\n";

    // Declare function-argument qubits.
    for (auto [i, arg] : llvm::enumerate(func.getArguments())) {
      if (isa<LogicalQubitType>(arg.getType())) {
        std::string name = "arg_" + std::to_string(i);
        reg(arg, name);
        os << "qubit " << name << ";\n";
      }
    }

    if (func.empty())
      return;

    for (auto &op : func.getBody().front())
      emitOp(&op);
  }

  void emitOp(Operation *op) {
    llvm::TypeSwitch<Operation *>(op)

      //----------------------------------------------------------------
      // Qubit lifecycle
      //----------------------------------------------------------------
      .Case<InitZeroOp>([&](InitZeroOp o) {
        std::string n = freshQubit();
        reg(o.getResult(), n);
        os << "qubit " << n << ";\n";
      })
      .Case<InitPlusOp>([&](InitPlusOp o) {
        std::string n = freshQubit();
        reg(o.getResult(), n);
        os << "qubit " << n << ";\n";
        os << "h " << n << ";\n";
      })
      .Case<DeallocOp>([&](DeallocOp) { emitComment("dealloc"); })

      //----------------------------------------------------------------
      // Single-qubit Clifford gates
      //----------------------------------------------------------------
      .Case<LogicalHOp>([&](LogicalHOp o) {
        std::string n = q(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "h " << n << ";\n";
      })
      .Case<LogicalXOp>([&](LogicalXOp o) {
        std::string n = q(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "x " << n << ";\n";
      })
      .Case<LogicalYOp>([&](LogicalYOp o) {
        std::string n = q(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "y " << n << ";\n";
      })
      .Case<LogicalZOp>([&](LogicalZOp o) {
        std::string n = q(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "z " << n << ";\n";
      })
      .Case<LogicalSOp>([&](LogicalSOp o) {
        std::string n = q(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "s " << n << ";\n";
      })

      //----------------------------------------------------------------
      // Magic states + T gate
      //----------------------------------------------------------------
      .Case<PrepareMagicTOp>([&](PrepareMagicTOp o) {
        // Magic state is consumed by logical_t; register but emit no gate.
        qubitMap[o.getResult()] = "__magic_t";
        emitComment("prepare_magic_t");
      })
      .Case<PrepareMagicCCZOp>([&](PrepareMagicCCZOp o) {
        qubitMap[o.getResult()] = "__magic_ccz";
        emitComment("prepare_magic_ccz");
      })
      .Case<LogicalTOp>([&](LogicalTOp o) {
        std::string n = q(o.getQubit());
        propagate(o.getResult(), o.getQubit());
        os << "t " << n << ";\n";
      })

      //----------------------------------------------------------------
      // Two-qubit gates
      //----------------------------------------------------------------
      .Case<LogicalCNOTOp>([&](LogicalCNOTOp o) {
        std::string c = q(o.getControl());
        std::string t = q(o.getTarget());
        propagate(o.getControlOut(), o.getControl());
        propagate(o.getTargetOut(), o.getTarget());
        os << "cx " << c << ", " << t << ";\n";
      })
      .Case<LogicalCZOp>([&](LogicalCZOp o) {
        std::string a = q(o.getQubit0());
        std::string b = q(o.getQubit1());
        propagate(o.getResult0(), o.getQubit0());
        propagate(o.getResult1(), o.getQubit1());
        os << "cz " << a << ", " << b << ";\n";
      })
      .Case<LogicalToffoliOp>([&](LogicalToffoliOp o) {
        std::string c0 = q(o.getCtrl0());
        std::string c1 = q(o.getCtrl1());
        std::string t  = q(o.getTarget());
        propagate(o.getOutCtrl0(), o.getCtrl0());
        propagate(o.getOutCtrl1(), o.getCtrl1());
        propagate(o.getOutTarget(), o.getTarget());
        os << "ccx " << c0 << ", " << c1 << ", " << t << ";\n";
      })

      //----------------------------------------------------------------
      // Measurement
      //----------------------------------------------------------------
      .Case<LogicalMeasureOp>([&](LogicalMeasureOp o) {
        std::string qn = q(o.getLogicalQubit());
        std::string bn = freshBit();
        os << "bit " << bn << " = measure " << qn << ";\n";
      })

      //----------------------------------------------------------------
      // Syndrome extraction — FT infrastructure only, no QASM gate.
      // Propagate the qubit name so downstream ops can find it.
      //----------------------------------------------------------------
      .Case<SteaneXSyndromeOp>([&](SteaneXSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment("steane_x_syndrome");
      })
      .Case<SteaneZSyndromeOp>([&](SteaneZSyndromeOp o) {
        propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
        emitComment("steane_z_syndrome");
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
      // Error correction — no QASM gate, propagate qubit.
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
      // Pauli frame — classical bookkeeping, no QASM gate.
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
      // Distillation ops — emit comment (pass handles these before QASM).
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
      // Resource annotation, encode/decode, func.return — skip silently.
      //----------------------------------------------------------------
      .Case<ResourceAnnotationOp>([&](ResourceAnnotationOp) {})
      .Case<EncodeOp>([&](EncodeOp)   { emitComment("encode (physical→logical)"); })
      .Case<DecodeOp>([&](DecodeOp)   { emitComment("decode (logical→physical)"); })
      .Case<func::ReturnOp>([&](func::ReturnOp) {})

      //----------------------------------------------------------------
      // Fallback for any op not handled above.
      //----------------------------------------------------------------
      .Default([&](Operation *unknownOp) {
        if (includeComments)
          os << "// unsupported: "
             << unknownOp->getName().getStringRef() << "\n";
      });
  }
};

} // namespace

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

// Use the TableGen-generated base class (impl::FTQCLowerToQASM3PassBase).
// It provides: getArgument(), getName(), clonePass(), getDependentDialects(),
// and the protected Option<> members 'outputFile' and 'includeComments'.
struct FTQCLowerToQASM3PassImpl
    : public ::impl::FTQCLowerToQASM3PassBase<FTQCLowerToQASM3PassImpl> {
  using FTQCLowerToQASM3PassBase::FTQCLowerToQASM3PassBase;

  void runOnOperation() override {
    auto mod = getOperation();

    QASM3Emitter emitter(includeComments);
    std::string qasm = emitter.emitModule(mod);

    // Write to file or stderr.
    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        mod.emitError("ftqc-lower-to-qasm3: cannot open output file '")
            << outputFile << "': " << ec.message();
        signalPassFailure();
        return;
      }
      file << qasm;
    } else {
      llvm::errs() << qasm;
    }

    // Attach QASM as a module attribute so downstream passes and FileCheck
    // tests can inspect it via the MLIR IR text (stdout).
    mod->setAttr("ftqc.qasm3",
                 StringAttr::get(mod.getContext(), qasm));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createLowerToQASM3Pass() {
  return std::make_unique<FTQCLowerToQASM3PassImpl>();
}
