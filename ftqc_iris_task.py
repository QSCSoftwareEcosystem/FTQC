#!/usr/bin/env python3
"""
ftqc_iris_task.py — IRIS dmem + @iris.kernel pipeline: QASM → physical circuit

Two @iris.kernel tasks exchange data through iris.dmem buffers:

  Task 1 (ftqc_qasm_import)   ftqc_qasm_opt   QASM bytes → logical MLIR bytes
  Task 2 (ftqc_mlir_opt_fn)   ftqc_mlir_opt   logical MLIR bytes → physical MLIR bytes

IRIS data flow:
  qasm_dmem ──h2d──► [ftqc_qasm_import] ──flush──► logical_dmem
                                                         │ h2d
                                                         ▼
                                              [ftqc_mlir_opt_fn] ──flush──► physical_dmem

Usage:
  source /Users/nqx/Development/iris.machq/install/setup.source
  python3 ftqc_iris_task.py [input.qasm] [options]

Options:
  --ecc-kind   steane | surface | color_code   (default: steane)
  --ecc-dist   code distance                   (default: 3)
  --func-name  MLIR function name              (default: circuit)
  --pipeline   physical pass pipeline          (default: ftqc-steane-pipeline)
  --out        output MLIR file                (default: <input>.physical.mlir)
"""

import sys, os, argparse, ctypes
from ctypes import c_int, c_size_t, c_void_p, POINTER, Structure
from pathlib import Path

import numpy as np

# ─── IRIS ─────────────────────────────────────────────────────────────────────

_IRIS_INSTALL = Path(os.environ.get("IRIS",
    "/Users/nqx/Development/iris.machq/install"))
sys.path.insert(0, str(_IRIS_INSTALL))
import iris
import sysconfig

def getlibwithext(lib_name):
    # Detect the platform and apply the correct extension
    if sys.platform == "darwin":
        full_name = f"{lib_name}.dylib"
    elif sys.platform.startswith("linux"):
        full_name = f"{lib_name}.so"
    elif sys.platform == "win32":
        full_name = f"{lib_name}.dll"
    else:
        raise OSError(f"Unsupported operating system: {sys.platform}")

    return full_name


# ─── FTQC shared library ──────────────────────────────────────────────────────
_HERE    = Path(__file__).parent
_LIBFTQC_NAME = os.path.join(_HERE, "install", "lib", "libftqc")
_LIBFTQC = getlibwithext(os.path.join(_HERE, "install", "lib", "libftqc"))

class FTQCCfg(Structure):
    """Mirror of the C FTQCCfg struct — all fields are fixed-size (no pointers)
    so the struct can be byte-copied into IRIS device memory safely."""
    _fields_ = [
        # Input / import
        ("ecc_kind",   ctypes.c_char * 64),
        ("ecc_dist",   c_int),
        ("func_name",  ctypes.c_char * 64),
        # Pre-built pipelines
        ("run_steane_pipeline",   c_int),
        ("run_surface_pipeline",  c_int),
        ("run_distill_pipeline",  c_int),
        # Encoding passes
        ("steane_encode",  c_int),
        ("surface_encode", c_int),
        ("use_lattice",    c_int),
        ("color_encode",   c_int),
        # Syndrome & correction passes
        ("insert_syndrome",   c_int),
        ("syndrome_method",   ctypes.c_char * 32),
        ("syndrome_rounds",   c_int),
        ("insert_correction", c_int),
        ("pauli_frame_opt",   c_int),
        # Magic-state passes
        ("magic_distill",        c_int),
        ("distill_protocol",     ctypes.c_char * 32),
        ("distill_levels",       c_int),
        ("distill_target_error", ctypes.c_double),
        # Lowering passes
        ("transversal_gates",         c_int),
        ("decompose_non_transversal", c_int),
        ("lower_to_quantum",          c_int),
        ("lower_to_stim",             c_int),
        ("lower_to_qir",              c_int),
        ("lower_to_qasm3",            c_int),
        ("lower_to_iqm_json",         c_int),
        ("lower_to_lattice_surgery",  c_int),
        # Analysis passes
        ("resource_estimate",    c_int),
        ("print_report",         c_int),
        ("fault_path",           c_int),
        ("fault_path_max_order", c_int),
        ("threshold_analysis",   c_int),
        ("physical_error_rate",  ctypes.c_double),
        ("target_error_rate",    ctypes.c_double),
    ]

# Map pipeline name → which flag to set in FTQCCfg
_PIPELINE_FLAGS = {
    "ftqc-steane-pipeline":  "run_steane_pipeline",
    "ftqc-surface-pipeline": "run_surface_pipeline",
    "ftqc-distill-pipeline": "run_distill_pipeline",
}

def make_cfg(ecc_kind: str, ecc_dist: int, func_name: str,
             pipeline: str = "") -> FTQCCfg:
    """Build a FTQCCfg from high-level parameters."""
    cfg = FTQCCfg()
    cfg.ecc_kind  = ecc_kind.encode()
    cfg.ecc_dist  = ecc_dist
    cfg.func_name = func_name.encode()
    flag = _PIPELINE_FLAGS.get(pipeline)
    if flag:
        setattr(cfg, flag, 1)
    return cfg

def cfg_as_np(cfg: FTQCCfg) -> np.ndarray:
    """Return a numpy uint8 view of a FTQCCfg's raw bytes.
    Since FTQCCfg contains no pointers (only fixed arrays and plain values)
    the byte copy is self-contained and safe to pass to iris.dmem()."""
    return np.frombuffer(
        ctypes.string_at(ctypes.addressof(cfg), ctypes.sizeof(cfg)),
        dtype=np.uint8).copy()

_lib = ctypes.CDLL(str(_LIBFTQC))
_lib.ftqc_qasm_opt.argtypes = [c_void_p, c_size_t, POINTER(FTQCCfg),
                                POINTER(c_void_p), POINTER(c_size_t)]
_lib.ftqc_qasm_opt.restype  = c_int
_lib.ftqc_mlir_opt.argtypes = [c_void_p, c_size_t, POINTER(FTQCCfg),
                                POINTER(c_void_p), POINTER(c_size_t)]
_lib.ftqc_mlir_opt.restype  = c_int
_lib.ftqc_free.argtypes     = [c_void_p]
_lib.ftqc_free.restype      = None

_MAX_OUT = 1 << 19   # 512 KB pre-allocated output buffer

# ─── C-API wrapper ────────────────────────────────────────────────────────────

def _call_opt(fn, in_np, in_size, cfg, out_np, size_np):
    """Call ftqc_qasm_opt or ftqc_mlir_opt into a pre-allocated numpy buffer.
    cfg must be a FTQCCfg ctypes.Structure (not a numpy array)."""
    out_data = c_void_p()
    out_size = c_size_t()
    rc = fn(in_np.ctypes.data_as(c_void_p), c_size_t(in_size),
            ctypes.byref(cfg), ctypes.byref(out_data), ctypes.byref(out_size))
    if rc != 0:
        return rc
    n = out_size.value
    if n > len(out_np):
        _lib.ftqc_free(out_data)
        return -1   # buffer too small
    out_np[:n] = np.frombuffer(ctypes.string_at(out_data, n), dtype=np.uint8)
    size_np[0] = n
    _lib.ftqc_free(out_data)
    return 0

# ─── @iris.kernel task factories ─────────────────────────────────────────────
#
# in_   : read-only numpy params  → h2d before kernel
# flush_: write numpy params      → d2h (flush) after kernel
#
# FTQCCfg is captured via closure so it never enters the IRIS scalar pipeline
# (iris.get_scalar_conv only handles numeric types and str).

def make_qasm_kernel_iris(cfg_np):
    """Return an @iris.kernel for ftqc_iris_qasm_opt (native C via IRIS FFI).

    cfg_np must be a numpy uint8 array produced by cfg_as_np() — the raw bytes
    of a FTQCCfg struct.  Passing a numpy array (not a ctypes.Structure) lets
    iris.dmem() use ctypes.data_as() for the host pointer, which is correct.
    """
    @iris.kernel(in_=["qasm_in", "cfg"], flush_=["mlir_out", "mlir_size"], library=_LIBFTQC_NAME)
    def ftqc_iris_qasm_opt(spec, qasm_in, len_qasm_in, cfg, mlir_out, mlir_size):
        pass
    def call(mlir_out, mlir_size, qasm_in):
        return ftqc_iris_qasm_opt(qasm_in, np.int64(len(qasm_in)), cfg_np,
                                  mlir_out, mlir_size, gws=[1])
    return call


def make_qasm_kernel(cfg):
    """Return an @iris.kernel for ftqc_qasm_opt with cfg captured by closure."""
    @iris.kernel(in_=["qasm_in"], flush_=["mlir_out", "mlir_size"])
    def ftqc_qasm_import(spec, mlir_out, mlir_size, qasm_in):
        rc = _call_opt(_lib.ftqc_qasm_opt,
                       qasm_in, len(qasm_in), cfg, mlir_out, mlir_size)
        if rc != 0:
            raise RuntimeError(f"ftqc_qasm_opt rc={rc}")
        print(f"  [task1:qasm_import]  {len(qasm_in)} B  →"
              f"  {int(mlir_size[0])} B  logical MLIR")
    return ftqc_qasm_import


def make_mlir_kernel_iris(cfg):
    """Return an @iris.kernel for ftqc_mlir_opt with cfg captured by closure."""
    @iris.kernel(in_=["mlir_in", "cfg"], flush_=["mlir_out", "mlir_out_size"], library=_LIBFTQC_NAME)
    def ftqc_iris_mlir_opt(spec, mlir_in, in_size, cfg, mlir_out, mlir_out_size):
        pass
    def call(mlir_out, mlir_out_size, mlir_in, mlir_in_size):
        return ftqc_iris_mlir_opt(mlir_in, np.int64(len(mlir_in)), cfg, mlir_out, mlir_out_size, gws=[1])
    return call

def make_mlir_kernel(cfg):
    """Return an @iris.kernel for ftqc_mlir_opt with cfg captured by closure."""
    @iris.kernel(in_=["mlir_in", "mlir_in_size"], flush_=["mlir_out", "mlir_out_size"])
    def ftqc_mlir_opt_fn(spec, mlir_out, mlir_out_size, mlir_in, mlir_in_size):
        in_size = int(mlir_in_size[0])
        rc = _call_opt(_lib.ftqc_mlir_opt,
                       mlir_in, in_size, cfg, mlir_out, mlir_out_size)
        if rc != 0:
            raise RuntimeError(f"ftqc_mlir_opt rc={rc}")
        print(f"  [task2:mlir_opt]     {in_size} B  →"
              f"  {int(mlir_out_size[0])} B  physical MLIR")
    return ftqc_mlir_opt_fn

# ─── Pipeline ─────────────────────────────────────────────────────────────────

def run(qasm_file: Path, ecc_kind: str, ecc_dist: int, func_name: str,
        pipeline: str, out_file: Path) -> None:

    qasm_bytes = qasm_file.read_bytes()

    # ── Numpy backing arrays ──────────────────────────────────────────────────
    qasm_np          = np.frombuffer(qasm_bytes, dtype=np.uint8).copy()
    logical_np       = np.zeros(_MAX_OUT, dtype=np.uint8)
    logical_size_np  = np.zeros(2, dtype=np.uint64)  # [0]=size; [1] keeps nelems>1 so IRIS passes as array
    physical_np      = np.zeros(_MAX_OUT, dtype=np.uint8)
    physical_size_np = np.zeros(2, dtype=np.uint64)  # [0]=size; [1] keeps nelems>1

    # ── FTQCCfg ───────────────────────────────────────────────────────────────
    # cfg_import: no pipeline (QASM → logical MLIR only, no passes)
    # cfg_opt:    full pipeline for logical → physical conversion
    cfg_import = make_cfg(ecc_kind, ecc_dist, func_name, pipeline="")
    cfg_opt    = make_cfg(ecc_kind, ecc_dist, func_name, pipeline=pipeline)

    # Convert to numpy bytes so iris.dmem() uses the correct host pointer.
    # FTQCCfg has no pointer fields — all fields are inline values/arrays —
    # so the byte copy is self-contained.
    cfg_import_np = cfg_as_np(cfg_import)
    cfg_opt_np    = cfg_as_np(cfg_opt)

    iris.init()

    # ── Build kernels (cfg captured by closure) ───────────────────────────────
    k_qasm = make_qasm_kernel_iris(cfg_import_np)
    k_mlir = make_mlir_kernel_iris(cfg_opt_np)

    # ── Task 1: QASM → logical MLIR ──────────────────────────────────────────
    t1 = k_qasm(logical_np, logical_size_np, qasm_np)
    t1.submit(iris.iris_cpu, sync=1)

    if logical_size_np[0] == 0:
        iris.finalize()
        raise RuntimeError("Task 1 produced no output")

    # ── Task 2: logical MLIR → physical circuit ───────────────────────────────
    t2 = k_mlir(physical_np, physical_size_np, logical_np, logical_size_np)
    t2.submit(iris.iris_cpu, sync=1)

    iris.finalize()

    if physical_size_np[0] == 0:
        raise RuntimeError("Task 2 produced no output")

    # ── Write output ──────────────────────────────────────────────────────────
    n        = int(physical_size_np[0])
    out_text = physical_np[:n].tobytes().decode()
    out_file.write_text(out_text)

    print(f"\nOutput: {out_file}  ({n} chars)")
    preview = out_text[:3000]
    if n > 3000:
        preview += f"\n... [{n - 3000} chars truncated]"
    print(preview)

# ─── CLI ──────────────────────────────────────────────────────────────────────

def main():
    default_qasm = str(_HERE / "test" / "Integration" / "bell.qasm")

    ap = argparse.ArgumentParser(
        description="IRIS @iris.kernel FTQC QASM → physical circuit pipeline")
    ap.add_argument("qasm_file", nargs="?", default=default_qasm)
    ap.add_argument("--ecc-kind",  default="steane",
                    choices=["steane", "surface", "color_code"])
    ap.add_argument("--ecc-dist",  type=int, default=3)
    ap.add_argument("--func-name", default="circuit")
    ap.add_argument("--pipeline",  default="ftqc-steane-pipeline")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    qasm_file = Path(args.qasm_file)
    if not qasm_file.exists():
        print(f"Error: {qasm_file} not found", file=sys.stderr)
        sys.exit(1)

    out_file = Path(args.out) if args.out else qasm_file.with_suffix(".physical.mlir")

    print(f"Input:    {qasm_file}")
    print(f"ECC:      {args.ecc_kind}  d={args.ecc_dist}")
    print(f"Pipeline: {args.pipeline}")
    print(f"Output:   {out_file}")
    print()

    run(qasm_file, args.ecc_kind, args.ecc_dist, args.func_name,
        args.pipeline, out_file)

if __name__ == "__main__":
    main()
