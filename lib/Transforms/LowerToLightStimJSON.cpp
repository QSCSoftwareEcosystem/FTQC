//===- LowerToLightStimJSON.cpp - FTQC -> LightStim descriptor JSON --------===//
//
// Emits a compact JSON experiment descriptor for a Python LightStim adapter.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_FTQCLOWERTOLIGHTSTIMJSONPASS
#include "FTQC/Transforms/Passes.h.inc"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct PatchInfo {
  std::string id;
  std::string code;
  unsigned distance = 3;
  int offsetX = 0;
  int offsetY = 0;
  std::string initialBasis = "Z";
  std::string measurementBasis = "Z";
};

struct SurgeryInfo {
  std::string interaction;
  std::string patch0;
  std::string patch1;
  unsigned rounds = 5;
};

static std::string jsonEscape(StringRef text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  return out;
}

static std::string sanitizeName(StringRef name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
      out += c;
    else
      out += '_';
  }
  return out.empty() ? "ftqc_lightstim_experiment" : out;
}

class LightStimDescriptorEmitter {
public:
  LightStimDescriptorEmitter(StringRef lightstimCode, int patchSpacing,
                             unsigned surgeryRounds, double physErrorRate,
                             StringRef decoderName, StringRef decoderBackend,
                             unsigned maxErrors, unsigned maxShots,
                             unsigned batchSize, unsigned numWorkers,
                             unsigned noiselessCheckShots)
      : lightstimCode_(lightstimCode.str()), patchSpacing_(patchSpacing),
        surgeryRounds_(surgeryRounds), physErrorRate_(physErrorRate),
        decoderName_(decoderName.str()), decoderBackend_(decoderBackend.str()),
        maxErrors_(maxErrors), maxShots_(maxShots), batchSize_(batchSize),
        numWorkers_(numWorkers), noiselessCheckShots_(noiselessCheckShots) {}

  FailureOr<std::string> emitModule(ModuleOp mod) {
    std::vector<func::FuncOp> funcs;
    for (auto func : mod.getOps<func::FuncOp>())
      funcs.push_back(func);
    if (funcs.empty()) {
      mod.emitError("ftqc-lower-to-lightstim-json: module has no func.func");
      return failure();
    }
    if (funcs.size() > 1) {
      mod.emitError("ftqc-lower-to-lightstim-json: only one func.func is currently supported");
      return failure();
    }
    return emitFunc(funcs.front());
  }

private:
  std::string lightstimCode_;
  int patchSpacing_;
  unsigned surgeryRounds_;
  double physErrorRate_;
  std::string decoderName_;
  std::string decoderBackend_;
  unsigned maxErrors_;
  unsigned maxShots_;
  unsigned batchSize_;
  unsigned numWorkers_;
  unsigned noiselessCheckShots_;

  DenseMap<Value, unsigned> patchOfValue_;
  std::vector<PatchInfo> patches_;
  std::vector<SurgeryInfo> surgeries_;

  FailureOr<std::string> emitFunc(func::FuncOp func) {
    patchOfValue_.clear();
    patches_.clear();
    surgeries_.clear();

    if (!func.empty()) {
      for (auto &op : func.getBody().front()) {
        if (failed(processOp(&op)))
          return failure();
      }
    }

    std::string buf;
    llvm::raw_string_ostream os(buf);
    writeJSON(os, func.getName());
    os.flush();
    return buf;
  }

  LogicalQubitType logicalType(Value value) {
    return dyn_cast<LogicalQubitType>(value.getType());
  }

  FailureOr<unsigned> ensurePatch(Value value, StringRef initialBasis = "Z") {
    auto existing = patchOfValue_.find(value);
    if (existing != patchOfValue_.end())
      return existing->second;

    auto type = logicalType(value);
    if (!type)
      return emitError(value.getLoc(), "expected logical qubit value"), failure();
    if (type.getCode().getKind() != ECCKind::Surface)
      return emitError(value.getLoc(), "LightStim export currently supports only surface-code logical qubits"),
             failure();

    unsigned index = patches_.size();
    PatchInfo patch;
    patch.id = "patch_" + std::to_string(index);
    patch.code = lightstimCode_;
    patch.distance = type.getDistance();
    patch.offsetX = index == 0 ? 0 : static_cast<int>(index) * patchSpacing_;
    patch.offsetY = 0;
    patch.initialBasis = initialBasis.str();
    patch.measurementBasis = "Z";
    patches_.push_back(patch);
    patchOfValue_[value] = index;
    return index;
  }

  void propagate(Value newValue, Value oldValue) {
    auto it = patchOfValue_.find(oldValue);
    if (it != patchOfValue_.end())
      patchOfValue_[newValue] = it->second;
  }

  void setMeasurement(Value value, StringRef basis) {
    auto it = patchOfValue_.find(value);
    if (it != patchOfValue_.end())
      patches_[it->second].measurementBasis = basis.str();
  }

  LogicalResult processOp(Operation *op) {
    return llvm::TypeSwitch<Operation *, LogicalResult>(op)
        .Case<InitZeroOp>([&](InitZeroOp o) {
          return failed(ensurePatch(o.getLogicalQubit(), "Z")) ? failure()
                                                               : success();
        })
        .Case<InitPlusOp>([&](InitPlusOp o) {
          return failed(ensurePatch(o.getLogicalQubit(), "X")) ? failure()
                                                               : success();
        })
        .Case<LogicalHOp>([&](LogicalHOp o) {
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalXOp>([&](LogicalXOp o) {
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalYOp>([&](LogicalYOp o) {
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalZOp>([&](LogicalZOp o) {
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalSOp>([&](LogicalSOp o) {
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalTOp>([&](LogicalTOp o) {
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalCNOTOp>([&](LogicalCNOTOp o) {
          propagate(o.getControlOut(), o.getControl());
          propagate(o.getTargetOut(), o.getTarget());
          return success();
        })
        .Case<LogicalCZOp>([&](LogicalCZOp o) {
          propagate(o.getResult0(), o.getQubit0());
          propagate(o.getResult1(), o.getQubit1());
          return success();
        })
        .Case<LatticeSurgeryOp>([&](LatticeSurgeryOp o) {
          auto p0 = ensurePatch(o.getPatch0());
          auto p1 = ensurePatch(o.getPatch1());
          if (failed(p0) || failed(p1))
            return failure();

          std::string interaction = o.getKind() == 0 ? "XX" : "ZZ";
          if (*p0 == 0)
            patches_[*p0].offsetX = patches_[*p0].offsetY = 0;
          if (interaction == "ZZ") {
            patches_[*p1].offsetX = 0;
            patches_[*p1].offsetY = patchSpacing_;
          } else {
            patches_[*p1].offsetX = patchSpacing_;
            patches_[*p1].offsetY = 0;
          }

          surgeries_.push_back(
              SurgeryInfo{interaction, patches_[*p0].id, patches_[*p1].id,
                          surgeryRounds_});
          patchOfValue_[o.getResult0()] = *p0;
          patchOfValue_[o.getResult1()] = *p1;
          return success();
        })
        .Case<LogicalMeasureOp>([&](LogicalMeasureOp o) {
          setMeasurement(o.getLogicalQubit(), "Z");
          return success();
        })
        .Case<DeallocOp>([&](DeallocOp) { return success(); })
        .Case<PrepareMagicTOp>([&](PrepareMagicTOp) { return success(); })
        .Case<PrepareMagicCCZOp>([&](PrepareMagicCCZOp) { return success(); })
        .Case<func::ReturnOp>([&](func::ReturnOp) { return success(); })
        .Default([&](Operation *unknown) {
          if (unknown->getName().getDialectNamespace() == "ftqc")
            unknown->emitRemark("ignored by ftqc-lower-to-lightstim-json");
          return success();
        });
  }

  void writeJSON(llvm::raw_ostream &os, StringRef funcName) {
    os << "{\n";
    os << "  \"format\": \"ftqc-lightstim-experiment\",\n";
    os << "  \"version\": 1,\n";
    os << "  \"experiment\": {\n";
    os << "    \"name\": \"" << jsonEscape(sanitizeName(funcName)) << "\",\n";
    os << "    \"type\": \"two_patch_lattice_surgery\"\n";
    os << "  },\n";

    os << "  \"patches\": [\n";
    for (size_t i = 0; i < patches_.size(); ++i) {
      const PatchInfo &p = patches_[i];
      os << "    {\n";
      os << "      \"id\": \"" << p.id << "\",\n";
      os << "      \"code\": \"" << jsonEscape(p.code) << "\",\n";
      os << "      \"distance\": " << p.distance << ",\n";
      os << "      \"offset\": [" << p.offsetX << ", " << p.offsetY << "],\n";
      os << "      \"initial_basis\": \"" << p.initialBasis << "\",\n";
      os << "      \"measurement_basis\": \"" << p.measurementBasis << "\"\n";
      os << "    }" << (i + 1 == patches_.size() ? "\n" : ",\n");
    }
    os << "  ],\n";

    os << "  \"operations\": [\n";
    for (size_t i = 0; i < surgeries_.size(); ++i) {
      const SurgeryInfo &s = surgeries_[i];
      os << "    {\n";
      os << "      \"type\": \"lattice_surgery\",\n";
      os << "      \"interaction\": \"" << s.interaction << "\",\n";
      os << "      \"patches\": [\"" << s.patch0 << "\", \"" << s.patch1 << "\"],\n";
      os << "      \"rounds\": " << s.rounds << ",\n";
      os << "      \"rotate_first_patch\": false\n";
      os << "    }" << (i + 1 == surgeries_.size() ? "\n" : ",\n");
    }
    os << "  ],\n";

    os << "  \"noise\": {\n";
    os << "    \"model\": \"circuit_level\",\n";
    os << "    \"rates\": {\n";
    os << "      \"p_1q\": " << physErrorRate_ << ",\n";
    os << "      \"p_2q\": " << physErrorRate_ << ",\n";
    os << "      \"p_meas\": " << physErrorRate_ << ",\n";
    os << "      \"p_reset\": " << physErrorRate_ << ",\n";
    os << "      \"p_idle\": " << physErrorRate_ << "\n";
    os << "    }\n";
    os << "  },\n";

    os << "  \"decoder\": {\n";
    os << "    \"name\": \"" << jsonEscape(decoderName_) << "\",\n";
    os << "    \"backend\": \"" << jsonEscape(decoderBackend_) << "\"\n";
    os << "  },\n";

    os << "  \"simulation\": {\n";
    os << "    \"max_errors\": " << maxErrors_ << ",\n";
    os << "    \"max_shots\": " << maxShots_ << ",\n";
    os << "    \"batch_size\": " << batchSize_ << ",\n";
    os << "    \"num_workers\": " << numWorkers_ << ",\n";
    os << "    \"noiseless_check_shots\": " << noiselessCheckShots_ << "\n";
    os << "  }\n";
    os << "}\n";
  }
};

struct FTQCLowerToLightStimJSONPassImpl
    : public ::impl::FTQCLowerToLightStimJSONPassBase<
          FTQCLowerToLightStimJSONPassImpl> {
  using FTQCLowerToLightStimJSONPassBase::FTQCLowerToLightStimJSONPassBase;

  void runOnOperation() override {
    auto mod = getOperation();
    LightStimDescriptorEmitter emitter(
        lightstimCode, patchSpacing, surgeryRounds, physErrorRate, decoderName,
        decoderBackend, maxErrors, maxShots, batchSize, numWorkers,
        noiselessCheckShots);

    auto json = emitter.emitModule(mod);
    if (failed(json)) {
      signalPassFailure();
      return;
    }

    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        mod.emitError("ftqc-lower-to-lightstim-json: cannot open output file '")
            << outputFile << "': " << ec.message();
        signalPassFailure();
        return;
      }
      file << *json;
    } else {
      llvm::errs() << *json;
    }

    if (attachModuleAttr)
      mod->setAttr("ftqc.lightstim_json",
                   StringAttr::get(mod.getContext(), *json));
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createLowerToLightStimJSONPass() {
  return std::make_unique<FTQCLowerToLightStimJSONPassImpl>();
}
