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
// Configuration is passed via FTQCCfg.  NULL / zero fields fall back to
// built-in defaults (ecc_kind="steane", ecc_dist=3, func_name="circuit").
// cfg->pipeline is a comma-separated list of pass/pipeline names accepted by
// mlir::parsePassPipeline, e.g. "ftqc-insert-syndrome,ftqc-resource-estimate".
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
//
// mlir::registerAllPasses() and PassPipelineRegistration<> write to global
// tables; calling them more than once is undefined.  A std::once_flag guards
// the single init path.

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

// ─── Configuration resolution ────────────────────────────────────────────────
//
// Apply defaults for NULL / zero fields in a FTQCCfg.

struct ResolvedCfg {
    std::string eccKind;
    int         eccDist;
    std::string funcName;
    std::string pipeline;
};

static ResolvedCfg resolveCfg(const FTQCCfg *cfg) {
    ResolvedCfg r;
    r.eccKind  = (cfg && cfg->ecc_kind  && cfg->ecc_kind[0])  ? cfg->ecc_kind  : "steane";
    r.eccDist  = (cfg && cfg->ecc_dist  > 0)                  ? cfg->ecc_dist  : 3;
    r.funcName = (cfg && cfg->func_name && cfg->func_name[0]) ? cfg->func_name : "circuit";
    r.pipeline = (cfg && cfg->pipeline  && cfg->pipeline[0])  ? cfg->pipeline  : "";
    return r;
}

// ─── Core pipeline runner ─────────────────────────────────────────────────────
//
// Takes MLIR text, runs the given pass pipeline, and returns the output MLIR
// text in a malloc'd, NUL-terminated buffer.

static int runPipeline(const std::string &mlirText,
                       const std::string &pipeline,
                       void **out_data, size_t *out_size) {
    ensureRegistered();

    // Build a per-call dialect registry and context.
    mlir::DialectRegistry registry;
    mlir::registerAllDialects(registry);
    registry.insert<mlir::ftqc::FTQCDialect>();

    mlir::MLIRContext ctx(registry);
    ctx.loadAllAvailableDialects();

    // Wrap the MLIR text in a MemoryBuffer and parse.
    auto memBuf =
        llvm::MemoryBuffer::getMemBufferCopy(mlirText, "<ftqc-iris-input>");
    llvm::SourceMgr srcMgr;
    srcMgr.AddNewSourceBuffer(std::move(memBuf), llvm::SMLoc());

    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::parseSourceFile<mlir::ModuleOp>(srcMgr, &ctx);
    if (!module)
        return 1;

    // Build the pass manager from the pipeline string.
    mlir::PassManager pm(&ctx);
    if (!pipeline.empty()) {
        std::string errMsg;
        llvm::raw_string_ostream errOs(errMsg);
        if (mlir::failed(mlir::parsePassPipeline(pipeline, pm, errOs)))
            return 1;
    }

    if (mlir::failed(pm.run(*module)))
        return 1;

    // Serialize the output module to a string.
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

    ResolvedCfg r = resolveCfg(cfg);
    std::string mlirText(static_cast<const char *>(mlir_data), mlir_size);
    return runPipeline(mlirText, r.pipeline, out_data, out_size);
}

int ftqc_qasm_opt(const void    *qasm_data,
                  size_t         qasm_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
                  size_t        *out_size) {
    if (!qasm_data || !out_data || !out_size)
        return 1;

    ResolvedCfg r = resolveCfg(cfg);
    std::string qasmText(static_cast<const char *>(qasm_data), qasm_size);

    // Validate ECC kind before running.
    if (r.eccKind != "steane" &&
        r.eccKind != "surface" &&
        r.eccKind != "color_code")
        return 1;

    std::string mlirText = qasm3import::importQASM3(
        qasmText, r.eccKind, r.eccDist, r.funcName);

    return runPipeline(mlirText, r.pipeline, out_data, out_size);
}

int ftqc_iris_mlir_opt(const void *spec, const void    *mlir_data,
                  size_t         mlir_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
                  size_t        *out_size) {
    return ftqc_mlir_opt(mlir_data, mlir_size, cfg, out_data, out_size);
}
int ftqc_iris_qasm_opt(const void *spec, const void    *qasm_data,
                  size_t         qasm_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
                  size_t        *out_size) {
    return ftqc_qasm_opt(qasm_data, qasm_size, cfg, out_data, out_size);
}

void ftqc_free(void *data) {
    std::free(data);
}

} // extern "C"
