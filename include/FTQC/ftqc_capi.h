//===- ftqc_capi.h - IRIS kernel C-API for ftqc-opt ------------------===//
//
// C-API library wrapping ftqc-opt pass infrastructure for use as IRIS task
// kernels in classical-quantum workflows.
//
// Two entry points:
//   ftqc_mlir_opt  - MLIR input in memory + pass-pipeline arguments
//   ftqc_qasm_opt  - OpenQASM 2/3 input in memory + import/pass arguments
//
// Both functions receive input as a raw memory pointer (no file I/O) and write
// output to a newly allocated buffer that the caller must release with
// ftqc_free().
//
// Argument format (argc / argv)
// ──────────────────────────────
// ftqc_mlir_opt — each argv[i] is a pass or pipeline name, e.g.:
//   "--ftqc-insert-syndrome"
//   "--ftqc-steane-pipeline"
//   "--ftqc-resource-estimate"
//   "--canonicalize"
//
// ftqc_qasm_opt — same as above, plus QASM import options:
//   "--qasm-ecc=steane"          (steane | surface | color_code, default: steane)
//   "--qasm-distance=3"          (code distance, default: 3)
//   "--qasm-func-name=circuit"   (MLIR function name, default: "circuit")
//
// Return value: 0 on success, non-zero on failure.
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_IRIS_CAPI_H
#define FTQC_IRIS_CAPI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run the ftqc-opt pass pipeline on MLIR text held in memory.
 *
 * @param mlir_data  Pointer to MLIR text; need not be NUL-terminated.
 * @param mlir_size  Byte length of mlir_data.
 * @param argc       Number of entries in argv.
 * @param argv       Pass or pipeline arguments (e.g. "--ftqc-insert-syndrome").
 *                   QASM-specific options (--qasm-*) are silently ignored.
 * @param out_data   On success, receives a malloc'd buffer with output MLIR
 *                   text (NUL-terminated).  Release with ftqc_free().
 * @param out_size   On success, receives the byte length of *out_data
 *                   (excluding the NUL terminator).
 * @return           0 on success, non-zero on failure.
 */
int ftqc_mlir_opt(const void  *mlir_data,
                       size_t       mlir_size,
                       int          argc,
                       const char  *argv[],
                       void       **out_data,
                       size_t      *out_size);

/**
 * Import an OpenQASM 2/3 circuit from memory, then run the ftqc-opt pass
 * pipeline on the resulting MLIR.
 *
 * @param qasm_data  Pointer to QASM source text; need not be NUL-terminated.
 * @param qasm_size  Byte length of qasm_data.
 * @param argc       Number of entries in argv.
 * @param argv       QASM import options and/or pass arguments, e.g.:
 *                     "--qasm-ecc=surface"
 *                     "--qasm-distance=5"
 *                     "--qasm-func-name=bell"
 *                     "--ftqc-insert-syndrome"
 * @param out_data   On success, receives a malloc'd buffer with output MLIR
 *                   text (NUL-terminated).  Release with ftqc_free().
 * @param out_size   On success, receives the byte length of *out_data
 *                   (excluding the NUL terminator).
 * @return           0 on success, non-zero on failure.
 */
int ftqc_qasm_opt(const void  *qasm_data,
                       size_t       qasm_size,
                       int          argc,
                       const char  *argv[],
                       void       **out_data,
                       size_t      *out_size);

/**
 * Release a buffer returned by ftqc_mlir_opt or ftqc_qasm_opt.
 * Passing NULL is safe and has no effect.
 */
void ftqc_free(void *data);

#ifdef __cplusplus
}
#endif

#endif /* FTQC_IRIS_CAPI_H */
