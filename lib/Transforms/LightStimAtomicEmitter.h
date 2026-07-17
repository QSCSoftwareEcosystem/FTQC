//===- LightStimAtomicEmitter.h - FTQC -> LightStim atomic timeline -------===//
//
// Internal helper for phase-1/2/3 LightStim integration:
//   - atomic timeline canonicalization (Section 4.2 model)
//   - full descriptor emission with simulation configuration
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_LIGHTSTIM_ATOMIC_EMITTER_H
#define FTQC_LIGHTSTIM_ATOMIC_EMITTER_H

#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace mlir::ftqc {

namespace lsatomic {

struct AtomicDescriptorConfig {
  double physErrorRate = 1e-3;
  std::string decoderName = "pymatching";
  std::string decoderBackend = "cpu";
  unsigned maxErrors = 100;
  unsigned maxShots = 5000000;
  unsigned batchSize = 10000;
  unsigned numWorkers = 4;
  unsigned noiselessCheckShots = 100;
};

class LightStimAtomicEmitter {
public:
  LightStimAtomicEmitter(StringRef lightstimCode, int patchSpacing,
                         unsigned defaultSERounds, unsigned surgeryRounds)
      : lightstimCode_(lightstimCode.str()), patchSpacing_(patchSpacing),
        defaultSERounds_(defaultSERounds), surgeryRounds_(surgeryRounds) {}

  FailureOr<std::string> emitAtomicIR(ModuleOp mod) {
    auto prep = analyzeModule(mod);
    if (failed(prep))
      return failure();
    return buildAtomicIRJSON(funcName_);
  }

  FailureOr<std::string> emitAtomicDescriptor(ModuleOp mod,
                                              const AtomicDescriptorConfig &cfg) {
    auto prep = analyzeModule(mod);
    if (failed(prep))
      return failure();
    return buildAtomicDescriptorJSON(funcName_, cfg);
  }

private:
  struct PatchInfo {
    std::string id;
    std::string code;
    unsigned distance = 3;
    int offsetX = 0;
    int offsetY = 0;
    std::string initialBasis = "Z";
    std::string measurementBasis = "Z";
  };

  std::string lightstimCode_;
  int patchSpacing_;
  unsigned defaultSERounds_;
  unsigned surgeryRounds_;

  DenseMap<Value, unsigned> patchOfValue_;
  std::vector<PatchInfo> patches_;
  std::vector<std::string> timelineOps_;
  std::vector<std::string> pendingGates_;
  std::vector<unsigned> readoutOrder_;
  DenseMap<unsigned, std::string> readoutBasisByPatch_;
  std::string funcName_;
  unsigned couplerCounter_ = 0;

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
    return out.empty() ? "ftqc_lightstim_atomic" : out;
  }

  static std::string join(const std::vector<std::string> &items, StringRef sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
      out += items[i];
      if (i + 1 != items.size())
        out += sep.str();
    }
    return out;
  }

  static std::string basisFromMeasure(std::optional<uint32_t> basis) {
    return basis.value_or(0) == 1 ? "X" : "Z";
  }

  void reset() {
    patchOfValue_.clear();
    patches_.clear();
    timelineOps_.clear();
    pendingGates_.clear();
    readoutOrder_.clear();
    readoutBasisByPatch_.clear();
    funcName_.clear();
    couplerCounter_ = 0;
  }

  LogicalQubitType logicalType(Value value) {
    return dyn_cast<LogicalQubitType>(value.getType());
  }

  FailureOr<unsigned> ensurePatch(Value value, StringRef initialBasis = "Z") {
    auto existing = patchOfValue_.find(value);
    if (existing != patchOfValue_.end())
      return existing->second;

    auto type = logicalType(value);
    if (!type) {
      emitError(value.getLoc()) << "expected logical qubit value";
      return failure();
    }
    if (type.getCode().getKind() != ECCKind::Surface) {
      emitError(value.getLoc())
          << "LightStim atomic export currently supports only surface-code logical qubits";
      return failure();
    }

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
    if (it != patchOfValue_.end()) {
      patches_[it->second].measurementBasis = basis.str();
      if (!readoutBasisByPatch_.count(it->second))
        readoutOrder_.push_back(it->second);
      readoutBasisByPatch_[it->second] = basis.str();
    }
  }

  static bool containsIndex(const std::vector<unsigned> &indices, unsigned idx) {
    for (unsigned x : indices)
      if (x == idx)
        return true;
    return false;
  }

  LogicalResult collectPatchIndices(ValueRange values,
                                    std::vector<unsigned> &indices) {
    for (Value v : values) {
      auto p = ensurePatch(v);
      if (failed(p))
        return failure();
      if (!containsIndex(indices, *p))
        indices.push_back(*p);
    }
    return success();
  }

  std::string patchArrayJSON(const std::vector<unsigned> &indices) {
    std::vector<std::string> ids;
    ids.reserve(indices.size());
    for (unsigned idx : indices)
      ids.push_back("\"" + patches_[idx].id + "\"");
    return "[" + join(ids, ", ") + "]";
  }

  void queueGate(StringRef gateName, const std::vector<unsigned> &patchIndices) {
    std::string gate = "{\"gate\": \"" + gateName.str() + "\", \"patches\": " +
                       patchArrayJSON(patchIndices) + "}";
    pendingGates_.push_back(gate);
  }

  void flushUnitaryBlock() {
    if (pendingGates_.empty())
      return;
    timelineOps_.push_back("{\"op\": \"unitary_block\", \"gates\": [" +
                           join(pendingGates_, ", ") + "]}");
    pendingGates_.clear();
  }

  void appendSyndromeEvent(unsigned rounds) {
    flushUnitaryBlock();
    timelineOps_.push_back("{\"op\": \"syndrome_extraction\", \"rounds\": " +
                           std::to_string(rounds) +
                           ", \"optimize_repeat\": true}");
  }

  void appendInitializeEvent(unsigned patchIdx, StringRef basis) {
    timelineOps_.push_back("{\"op\": \"initialize\", \"patch\": \"" +
                           patches_[patchIdx].id + "\", \"basis\": \"" +
                           basis.str() + "\"}");
  }

  void appendReadoutEventIfNeeded() {
    if (readoutOrder_.empty())
      return;
    std::vector<std::string> meas;
    meas.reserve(readoutOrder_.size());
    for (unsigned idx : readoutOrder_) {
      std::string basis = "Z";
      auto it = readoutBasisByPatch_.find(idx);
      if (it != readoutBasisByPatch_.end())
        basis = it->second;
      meas.push_back("{\"patch\": \"" + patches_[idx].id +
                     "\", \"basis\": \"" + basis + "\"}");
    }
    timelineOps_.push_back("{\"op\": \"data_readout\", \"measurements\": [" +
                           join(meas, ", ") + "]}");
  }

  LogicalResult processOp(Operation *op) {
    return llvm::TypeSwitch<Operation *, LogicalResult>(op)
        .Case<InitZeroOp>([&](InitZeroOp o) {
          auto p = ensurePatch(o.getLogicalQubit(), "Z");
          if (failed(p))
            return failure();
          patches_[*p].initialBasis = "Z";
          appendInitializeEvent(*p, "Z");
          return success();
        })
        .Case<InitPlusOp>([&](InitPlusOp o) {
          auto p = ensurePatch(o.getLogicalQubit(), "X");
          if (failed(p))
            return failure();
          patches_[*p].initialBasis = "X";
          appendInitializeEvent(*p, "X");
          return success();
        })
        .Case<LogicalHOp>([&](LogicalHOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getQubit()}, ids)))
            return failure();
          queueGate("logical_h", ids);
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalXOp>([&](LogicalXOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getQubit()}, ids)))
            return failure();
          queueGate("logical_x", ids);
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalYOp>([&](LogicalYOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getQubit()}, ids)))
            return failure();
          queueGate("logical_y", ids);
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalZOp>([&](LogicalZOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getQubit()}, ids)))
            return failure();
          queueGate("logical_z", ids);
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalSOp>([&](LogicalSOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getQubit()}, ids)))
            return failure();
          queueGate("logical_s", ids);
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalTOp>([&](LogicalTOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getQubit()}, ids)))
            return failure();
          queueGate("logical_t", ids);
          propagate(o.getResult(), o.getQubit());
          return success();
        })
        .Case<LogicalCNOTOp>([&](LogicalCNOTOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getControl(), o.getTarget()}, ids)))
            return failure();
          queueGate("logical_cnot", ids);
          propagate(o.getControlOut(), o.getControl());
          propagate(o.getTargetOut(), o.getTarget());
          return success();
        })
        .Case<LogicalCZOp>([&](LogicalCZOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getQubit0(), o.getQubit1()}, ids)))
            return failure();
          queueGate("logical_cz", ids);
          propagate(o.getResult0(), o.getQubit0());
          propagate(o.getResult1(), o.getQubit1());
          return success();
        })
        .Case<LogicalToffoliOp>([&](LogicalToffoliOp o) {
          std::vector<unsigned> ids;
          if (failed(collectPatchIndices(ValueRange{o.getCtrl0(), o.getCtrl1(), o.getTarget()}, ids)))
            return failure();
          queueGate("logical_toffoli", ids);
          propagate(o.getOutCtrl0(), o.getCtrl0());
          propagate(o.getOutCtrl1(), o.getCtrl1());
          propagate(o.getOutTarget(), o.getTarget());
          return success();
        })
        .Case<LatticeSurgeryOp>([&](LatticeSurgeryOp o) {
          flushUnitaryBlock();
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

          std::string coupler = "coupler_" + std::to_string(couplerCounter_++);
          timelineOps_.push_back(
              "{\"op\": \"activate_coupler\", \"name\": \"" + coupler +
              "\", \"interaction\": \"" + interaction + "\", \"patches\": [\"" +
              patches_[*p0].id + "\", \"" + patches_[*p1].id + "\"]}");
          appendSyndromeEvent(surgeryRounds_);
          timelineOps_.push_back("{\"op\": \"deactivate_coupler\", \"name\": \"" +
                                 coupler + "\"}");

          patchOfValue_[o.getResult0()] = *p0;
          patchOfValue_[o.getResult1()] = *p1;
          return success();
        })
        .Case<ExtractSyndromeOp>([&](ExtractSyndromeOp o) {
          propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
          appendSyndromeEvent(o.getRounds().value_or(defaultSERounds_));
          return success();
        })
        .Case<SurfaceRoundOp>([&](SurfaceRoundOp o) {
          propagate(o.getResult(), o.getPatch());
          appendSyndromeEvent(o.getCycles());
          return success();
        })
        .Case<ECRoundOp>([&](ECRoundOp o) {
          propagate(o.getCorrectedQubit(), o.getLogicalQubit());
          appendSyndromeEvent(o.getRounds());
          return success();
        })
        .Case<SteaneXSyndromeOp>([&](SteaneXSyndromeOp o) {
          propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
          appendSyndromeEvent(1);
          return success();
        })
        .Case<SteaneZSyndromeOp>([&](SteaneZSyndromeOp o) {
          propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
          appendSyndromeEvent(1);
          return success();
        })
        .Case<ShorSyndromeOp>([&](ShorSyndromeOp o) {
          propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
          appendSyndromeEvent(1);
          return success();
        })
        .Case<FlagSyndromeOp>([&](FlagSyndromeOp o) {
          propagate(o.getOutLogicalQubit(), o.getLogicalQubit());
          appendSyndromeEvent(1);
          return success();
        })
        .Case<CorrectOp>([&](CorrectOp o) {
          propagate(o.getCorrectedQubit(), o.getLogicalQubit());
          return success();
        })
        .Case<LogicalMeasureOp>([&](LogicalMeasureOp o) {
          flushUnitaryBlock();
          setMeasurement(o.getLogicalQubit(), basisFromMeasure(o.getBasis()));
          return success();
        })
        .Case<DeallocOp>([&](DeallocOp) { return success(); })
        .Case<PrepareMagicTOp>([&](PrepareMagicTOp) { return success(); })
        .Case<PrepareMagicCCZOp>([&](PrepareMagicCCZOp) { return success(); })
        .Case<func::ReturnOp>([&](func::ReturnOp) {
          flushUnitaryBlock();
          return success();
        })
        .Default([&](Operation *unknown) {
          if (unknown->getName().getDialectNamespace() == "ftqc")
            unknown->emitRemark("ignored by LightStim atomic exporter");
          return success();
        });
  }

  LogicalResult analyzeModule(ModuleOp mod) {
    reset();

    std::vector<func::FuncOp> funcs;
    for (auto func : mod.getOps<func::FuncOp>())
      funcs.push_back(func);
    if (funcs.empty()) {
      mod.emitError("LightStim atomic export: module has no func.func");
      return failure();
    }
    if (funcs.size() > 1) {
      mod.emitError("LightStim atomic export: only one func.func is currently supported");
      return failure();
    }

    func::FuncOp func = funcs.front();
    funcName_ = sanitizeName(func.getName());

    for (Value arg : func.getArguments()) {
      if (logicalType(arg)) {
        if (failed(ensurePatch(arg, "Z")))
          return failure();
      }
    }

    if (!func.empty()) {
      for (auto &op : func.getBody().front()) {
        if (failed(processOp(&op)))
          return failure();
      }
    }

    flushUnitaryBlock();
    appendReadoutEventIfNeeded();
    return success();
  }

  std::string buildAtomicIRJSON(StringRef experimentName) {
    std::string out;
    llvm::raw_string_ostream os(out);

    os << "{\n";
    os << "  \"format\": \"ftqc-lightstim-atomic-ir\",\n";
    os << "  \"version\": 2,\n";
    os << "  \"experiment\": {\n";
    os << "    \"name\": \"" << jsonEscape(experimentName) << "\",\n";
    os << "    \"type\": \"atomic_timeline\"\n";
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

    os << "  \"timeline\": [\n";
    for (size_t i = 0; i < timelineOps_.size(); ++i) {
      os << "    " << timelineOps_[i] << (i + 1 == timelineOps_.size() ? "\n" : ",\n");
    }
    os << "  ]\n";
    os << "}\n";

    os.flush();
    return out;
  }

  std::string buildAtomicDescriptorJSON(StringRef experimentName,
                                        const AtomicDescriptorConfig &cfg) {
    std::string out;
    llvm::raw_string_ostream os(out);

    os << "{\n";
    os << "  \"format\": \"ftqc-lightstim-atomic-experiment\",\n";
    os << "  \"version\": 2,\n";
    os << "  \"experiment\": {\n";
    os << "    \"name\": \"" << jsonEscape(experimentName) << "\",\n";
    os << "    \"type\": \"atomic_timeline\"\n";
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

    os << "  \"timeline\": [\n";
    for (size_t i = 0; i < timelineOps_.size(); ++i) {
      os << "    " << timelineOps_[i] << (i + 1 == timelineOps_.size() ? "\n" : ",\n");
    }
    os << "  ],\n";

    os << "  \"noise\": {\n";
    os << "    \"model\": \"circuit_level\",\n";
    os << "    \"rates\": {\n";
    os << "      \"p_1q\": " << cfg.physErrorRate << ",\n";
    os << "      \"p_2q\": " << cfg.physErrorRate << ",\n";
    os << "      \"p_meas\": " << cfg.physErrorRate << ",\n";
    os << "      \"p_reset\": " << cfg.physErrorRate << ",\n";
    os << "      \"p_idle\": " << cfg.physErrorRate << "\n";
    os << "    }\n";
    os << "  },\n";

    os << "  \"decoder\": {\n";
    os << "    \"name\": \"" << jsonEscape(cfg.decoderName) << "\",\n";
    os << "    \"backend\": \"" << jsonEscape(cfg.decoderBackend) << "\"\n";
    os << "  },\n";

    os << "  \"simulation\": {\n";
    os << "    \"max_errors\": " << cfg.maxErrors << ",\n";
    os << "    \"max_shots\": " << cfg.maxShots << ",\n";
    os << "    \"batch_size\": " << cfg.batchSize << ",\n";
    os << "    \"num_workers\": " << cfg.numWorkers << ",\n";
    os << "    \"noiseless_check_shots\": " << cfg.noiselessCheckShots << "\n";
    os << "  }\n";
    os << "}\n";

    os.flush();
    return out;
  }
};

} // namespace lsatomic

} // namespace mlir::ftqc

#endif // FTQC_LIGHTSTIM_ATOMIC_EMITTER_H
