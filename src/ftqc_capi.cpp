//===- ftqc_capi.cpp - IRIS kernel C-API for ftqc-opt ----------------===//
//
// Implements the two IRIS kernel entry points declared in ftqc_capi.h:
//
//   ftqc_mlir_opt  — run a pass pipeline on an in-memory MLIR buffer
//   ftqc_qasm_opt  — import OpenQASM 2/3 from memory, then run a pipeline
//
// Neither function touches the filesystem or the llvm::cl singleton, so they
// are safe to call from multi-threaded IRIS task graphs.
//
// Configuration is passed via FTQCCfg.  All fields use fixed-size storage
// (no pointers), making the struct safe to byte-copy into IRIS device memory.
// Zero-initialised / empty fields fall back to built-in defaults.
//
//===----------------------------------------------------------------------===//

#include "FTQC/ftqc_capi.h"

#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"
#include "QASM3Importer.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Support/LogicalResult.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

// ─── One-time global registration ────────────────────────────────────────────

static std::once_flag gRegisterOnce;

static void ensureRegistered() {
    std::call_once(gRegisterOnce, []() {
        mlir::registerAllPasses();
        mlir::ftqc::registerFTQCPasses();

        mlir::PassPipelineRegistration<>(
            "ftqc-steane-pipeline",
            "Full Steane [[7,1,3]] fault-tolerant encoding pipeline",
            [](mlir::OpPassManager &pm) {
                mlir::ftqc::buildSteaneEncodePipeline(pm);
            });
        mlir::PassPipelineRegistration<>(
            "ftqc-surface-pipeline",
            "Full surface-code fault-tolerant encoding pipeline",
            [](mlir::OpPassManager &pm) {
                mlir::ftqc::buildSurfaceEncodePipeline(pm);
            });
        mlir::PassPipelineRegistration<>(
            "ftqc-distill-pipeline",
            "15-to-1 T state distillation pipeline",
            [](mlir::OpPassManager &pm) {
                mlir::ftqc::buildDistillationPipeline(pm);
            });
    });
}

// ─── Helper: field-with-default ──────────────────────────────────────────────

static std::string strOrDefault(const char *field, const char *def) {
    return (field && field[0]) ? std::string(field) : std::string(def);
}

// ─── Build PassManager from FTQCCfg flags ────────────────────────────────────
//
// Checks pre-built pipeline flags first; if none set, adds individual passes
// in a logical order based on the per-pass flags.

static void buildPassManager(mlir::PassManager &pm, const FTQCCfg *cfg) {
    if (!cfg) return;

    // Pre-built pipelines (mutually exclusive; first match wins)
    if (cfg->run_steane_pipeline) {
        unsigned dist = cfg->ecc_dist > 0 ? (unsigned)cfg->ecc_dist : 3u;
        mlir::ftqc::buildSteaneEncodePipeline(pm, dist);
        return;
    }
    if (cfg->run_surface_pipeline) {
        unsigned dist = cfg->ecc_dist > 0 ? (unsigned)cfg->ecc_dist : 3u;
        bool lattice = cfg->use_lattice != 0;
        mlir::ftqc::buildSurfaceEncodePipeline(pm, dist, lattice);
        return;
    }
    if (cfg->run_distill_pipeline) {
        std::string proto = strOrDefault(cfg->distill_protocol, "15to1");
        unsigned levels = cfg->distill_levels > 0 ? (unsigned)cfg->distill_levels : 1u;
        mlir::ftqc::buildDistillationPipeline(pm, proto, levels);
        return;
    }

    // Encoding passes
    if (cfg->steane_encode) {
        unsigned dist = cfg->ecc_dist > 0 ? (unsigned)cfg->ecc_dist : 3u;
        pm.addPass(mlir::ftqc::createSteaneEncodePass(dist, false, 3));
    }
    if (cfg->surface_encode) {
        unsigned dist = cfg->ecc_dist > 0 ? (unsigned)cfg->ecc_dist : 3u;
        pm.addPass(mlir::ftqc::createSurfaceEncodePass(dist, cfg->use_lattice != 0));
    }
    if (cfg->color_encode) {
        unsigned dist = cfg->ecc_dist > 0 ? (unsigned)cfg->ecc_dist : 3u;
        pm.addPass(mlir::ftqc::createColorCodeEncodePass(dist));
    }

    // Syndrome & correction passes
    if (cfg->insert_syndrome) {
        std::string method = strOrDefault(cfg->syndrome_method, "steane");
        unsigned rounds = cfg->syndrome_rounds > 0 ? (unsigned)cfg->syndrome_rounds : 3u;
        pm.addPass(mlir::ftqc::createInsertSyndromePass(method, rounds));
    }
    if (cfg->insert_correction)
        pm.addPass(mlir::ftqc::createInsertCorrectionPass());
    if (cfg->pauli_frame_opt)
        pm.addPass(mlir::ftqc::createPauliFrameOptPass());

    // Magic-state passes
    if (cfg->magic_distill) {
        std::string proto = strOrDefault(cfg->distill_protocol, "15to1");
        unsigned levels = cfg->distill_levels > 0 ? (unsigned)cfg->distill_levels : 1u;
        double terr = cfg->distill_target_error > 0.0 ? cfg->distill_target_error : 1e-10;
        pm.addPass(mlir::ftqc::createMagicDistillPass(proto, levels, terr));
    }

    // Lowering passes
    if (cfg->transversal_gates)
        pm.addPass(mlir::ftqc::createTransversalGatePass(cfg->decompose_non_transversal != 0));
    if (cfg->lower_to_quantum)
        pm.addPass(mlir::ftqc::createLowerToQuantumPass());
    if (cfg->lower_to_stim)
        pm.addPass(mlir::ftqc::createLowerToSTIMPass());
    if (cfg->lower_to_qir)
        pm.addPass(mlir::ftqc::createLowerToQIRPass());
    if (cfg->lower_to_qasm3)
        pm.addPass(mlir::ftqc::createLowerToQASM3Pass());
    if (cfg->lower_to_iqm_json)
        pm.addPass(mlir::ftqc::createLowerToIQMJSONPass());
    if (cfg->lower_to_lattice_surgery)
        pm.addPass(mlir::ftqc::createLowerToLatticeSurgeryPass());

    // Analysis passes
    if (cfg->resource_estimate)
        pm.addPass(mlir::ftqc::createResourceEstimatePass(cfg->print_report != 0));
    if (cfg->fault_path) {
        unsigned order = cfg->fault_path_max_order > 0 ? (unsigned)cfg->fault_path_max_order : 1u;
        pm.addPass(mlir::ftqc::createFaultPathAnalysisPass(order));
    }
    if (cfg->threshold_analysis) {
        double phys = cfg->physical_error_rate > 0.0 ? cfg->physical_error_rate : 1e-3;
        double tgt  = cfg->target_error_rate  > 0.0 ? cfg->target_error_rate  : 1e-10;
        pm.addPass(mlir::ftqc::createThresholdAnalysisPass(phys, tgt));
    }
}

// ─── Core pipeline runner ─────────────────────────────────────────────────────

static int runPipeline(const std::string &mlirText, const FTQCCfg *cfg,
                       void **out_data, size_t *out_size) {
    ensureRegistered();

    mlir::DialectRegistry registry;
    mlir::registerAllDialects(registry);
    registry.insert<mlir::ftqc::FTQCDialect>();

    mlir::MLIRContext ctx(registry);
    ctx.loadAllAvailableDialects();

    auto memBuf =
        llvm::MemoryBuffer::getMemBufferCopy(mlirText, "<ftqc-iris-input>");
    llvm::SourceMgr srcMgr;
    srcMgr.AddNewSourceBuffer(std::move(memBuf), llvm::SMLoc());

    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::parseSourceFile<mlir::ModuleOp>(srcMgr, &ctx);
    if (!module)
        return 1;

    mlir::PassManager pm(&ctx);
    buildPassManager(pm, cfg);

    if (mlir::failed(pm.run(*module)))
        return 1;

    std::string result;
    llvm::raw_string_ostream os(result);
    module->print(os);
    os.flush();

    *out_size = result.size();
    *out_data = std::malloc(result.size() + 1);
    if (!*out_data)
        return 1;
    std::memcpy(*out_data, result.data(), result.size());
    static_cast<char *>(*out_data)[result.size()] = '\0';
    return 0;
}

// ─── Public C-API ────────────────────────────────────────────────────────────

extern "C" {

int ftqc_mlir_opt(const void    *mlir_data,
                  size_t         mlir_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
                  size_t        *out_size) {
    if (!mlir_data || !out_data || !out_size)
        return 1;
    std::string mlirText(static_cast<const char *>(mlir_data), mlir_size);
    return runPipeline(mlirText, cfg, out_data, out_size);
}

int ftqc_qasm_opt(const void    *qasm_data,
                  size_t         qasm_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
                  size_t        *out_size) {
    if (!qasm_data || !out_data || !out_size)
        return 1;

    std::string eccKind  = cfg ? strOrDefault(cfg->ecc_kind,  "steane")  : "steane";
    int         eccDist  = (cfg && cfg->ecc_dist > 0) ? cfg->ecc_dist : 3;
    std::string funcName = cfg ? strOrDefault(cfg->func_name, "circuit") : "circuit";

    if (eccKind != "steane" && eccKind != "surface" && eccKind != "color_code")
        return 1;

    std::string qasmText(static_cast<const char *>(qasm_data), qasm_size);
    std::string mlirText = qasm3import::importQASM3(qasmText, eccKind, eccDist, funcName);
    return runPipeline(mlirText, cfg, out_data, out_size);
}

// ─── copy_to_buf ─────────────────────────────────────────────────────────────
//
// Shared helper for the IRIS kernel variants: runs the pipeline via the
// standard (malloc) entry point, then copies the result into the caller-
// supplied IRIS device buffer and frees the temporary allocation.

static int copy_to_buf(void *tmp_data, size_t tmp_size,
                       void *out_buf, size_t *out_size) {
    if (!tmp_data) return 1;
    std::memcpy(out_buf, tmp_data, tmp_size);
    *out_size = tmp_size;
    std::free(tmp_data);
    return 0;
}

int ftqc_iris_mlir_opt(const void    *spec,
                       const void    *mlir_data,
                       size_t         mlir_size,
                       const FTQCCfg *cfg,
                       void          *out_buf,
                       size_t        *out_size) {
    (void)spec;
    void *tmp = NULL; size_t sz = 0;
    if (ftqc_mlir_opt(mlir_data, mlir_size, cfg, &tmp, &sz) != 0) return 1;
    return copy_to_buf(tmp, sz, out_buf, out_size);
}

int ftqc_iris_qasm_opt(const void    *spec,
                       const void    *qasm_data,
                       size_t         qasm_size,
                       const FTQCCfg *cfg,
                       void          *out_buf,
                       size_t        *out_size) {
    (void)spec;
    void *tmp = NULL; size_t sz = 0;
    if (ftqc_qasm_opt(qasm_data, qasm_size, cfg, &tmp, &sz) != 0) return 1;
    return copy_to_buf(tmp, sz, out_buf, out_size);
}

void ftqc_free(void *data) {
    std::free(data);
}

} // extern "C"
