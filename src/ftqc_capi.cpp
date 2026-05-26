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
// Pass pipeline parsing
// ─────────────────────
// argv entries that start with "--" and are not "--qasm-*" options are treated
// as pass or pipeline names.  The leading "--" is stripped and the names are
// joined with "," to form an mlir::parsePassPipeline-compatible string, e.g.:
//
//   argv = {"--ftqc-insert-syndrome", "--ftqc-resource-estimate"}
//   → pipeline = "ftqc-insert-syndrome,ftqc-resource-estimate"
//
// Named pipelines registered at library init (ftqc-steane-pipeline, etc.) are
// resolved the same way.
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
#include <vector>

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

// ─── Argument parsing ────────────────────────────────────────────────────────

struct ParsedArgs {
    std::string eccKind  = "steane";
    int         eccDist  = 3;
    std::string funcName = "circuit";
    std::string pipeline; // comma-separated pass/pipeline names
};

static ParsedArgs parseArgs(int argc, const char *argv[]) {
    ParsedArgs out;
    std::vector<std::string> passes;

    for (int i = 0; i < argc; ++i) {
        if (!argv[i]) continue;
        std::string a(argv[i]);

        // QASM import options — consumed here, not forwarded to the pass manager.
        if (a.substr(0, 11) == "--qasm-ecc=") {
            out.eccKind = a.substr(11);
            continue;
        }
        if (a.substr(0, 16) == "--qasm-distance=") {
            out.eccDist = std::stoi(a.substr(16));
            continue;
        }
        if (a.substr(0, 17) == "--qasm-func-name=") {
            out.funcName = a.substr(17);
            continue;
        }

        // Every other "--name" or "--name=value" entry is a pass/pipeline opt.
        if (a.size() > 2 && a[0] == '-' && a[1] == '-')
            passes.push_back(a.substr(2));
    }

    for (size_t i = 0; i < passes.size(); ++i) {
        if (i) out.pipeline += ',';
        out.pipeline += passes[i];
    }
    return out;
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

int ftqc_mlir_opt(const void  *mlir_data,
                       size_t       mlir_size,
                       int          argc,
                       const char  *argv[],
                       void       **out_data,
                       size_t      *out_size) {
    if (!mlir_data || !out_data || !out_size)
        return 1;

    ParsedArgs args = parseArgs(argc, argv);
    std::string mlirText(static_cast<const char *>(mlir_data), mlir_size);
    return runPipeline(mlirText, args.pipeline, out_data, out_size);
}

int ftqc_qasm_opt(const void  *qasm_data,
                       size_t       qasm_size,
                       int          argc,
                       const char  *argv[],
                       void       **out_data,
                       size_t      *out_size) {
    if (!qasm_data || !out_data || !out_size)
        return 1;

    ParsedArgs args = parseArgs(argc, argv);
    std::string qasmText(static_cast<const char *>(qasm_data), qasm_size);

    // Validate ECC kind before running.
    if (args.eccKind != "steane" &&
        args.eccKind != "surface" &&
        args.eccKind != "color_code")
        return 1;

    std::string mlirText = qasm3import::importQASM3(
        qasmText, args.eccKind, args.eccDist, args.funcName);

    return runPipeline(mlirText, args.pipeline, out_data, out_size);
}

void ftqc_free(void *data) {
    std::free(data);
}

} // extern "C"
