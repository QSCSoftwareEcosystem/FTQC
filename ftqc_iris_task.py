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

  --submit-iqm  lower the physical circuit to IQM JSON (Task 3) and run it on the
                IRIS `iris_iqm` device (Task 4), printing the measurement histogram
  --qc-alias    IQM device alias               (default: $IQM_QC_ALIAS or 'default')
  --shots       shot count                     (default: 1024)
  --heralding   none | zeros | null            (default: none)
  --iqm-lib     path to libiqm-qdmi-device.so  (else IRIS_LIB_IQM / IRIS default)
  --base-url    IQM server URL                 (else $IQM_BASE_URL)
  --keep-turns  submit prx angles in turns (default: convert to radians)

IQM submission reads auth from the environment (RESONANCE_API_KEY / IQM_TOKEN,
IQM_BASE_URL); these are mirrored to the IRIS_-prefixed names the iris_iqm device
actually reads. If IRIS can't locate libiqm-qdmi-device.so, point it via
--iqm-lib or IRIS_LIB_IQM.

It's used only for the physical path, and it does two things: route the circuit onto the device's connectivity, then submit it. There are
  three reasons it exists — and why it has to be a separate process:

  1. The physical circuit can't run as-is — it needs routing.
  A 7-qubit Steane block's encoder is 9 CNOTs with a fixed adjacency, and I verified that graph is not a subgraph of the 20-qubit device
  topology. The raw path (FTQC JSON → iris_iqm → QDMI → server) does no routing — I tested that the server rejects a CZ between non-adjacent
  qubits ("not allowed as locus for 'cz'"). So the circuit must be transpiled to physical qubits + SWAPs first. qiskit-iqm's transpile_to_IQM
  does exactly that. (Device-valid circuits like the 2-qubit bell don't need it and still go straight through iris_iqm.)

  2. qiskit-iqm can't live in the IRIS process.
  Importing qiskit-iqm into the process that drives the iris_iqm device breaks the QDMI library's symbol resolution
  (IQM_QDMI_device_session_alloc not found), and the iris_iqm device was unreliable for the larger 7-qubit job anyway. Isolating qiskit-iqm in a
  child process avoids that interference entirely, and I fall back to qiskit-iqm's own (proven) submission for the routed circuit.

  3. Python version split.
  iqm-client/qiskit-iqm ≥34 (needed to talk to this server) require Python ≥3.11, but the interpreter that drives IRIS here is 3.10. So the
  routing/transpilation has to run under a 3.12 interpreter regardless — a subprocess is the natural boundary. It defaults to IQM/realqpu.venv
  and is overridable via FTQC_ROUTER_PYTHON.

  So the flow is:
  - Tasks 1–3 (QASM → logical → physical MLIR → IQM JSON) run in-process via IRIS.
  - Physical circuits: the subprocess (3.12, qiskit-iqm) routes to device connectivity + submits + returns counts.
  - Already-device-valid circuits: submitted directly through the iris_iqm device, no subprocess.

  If IQM ever adds server-side routing, or the whole thing runs under Python ≥3.11 with a fixed iris_iqm for large jobs, the subprocess could be
  dropped and everything would go through iris_iqm.

"""

import sys, os, argparse, ctypes, json, re
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
_LIBFTQC = os.path.join(_HERE, "install", "lib", "libftqc")

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
        # Appended (must match the tail of the C FTQCCfg): emit prx as
        # angle/phase in radians instead of angle_t/phase_t in turns.
        ("lower_to_iqm_json_radians", c_int),
        # Expand each Steane logical qubit into a 7-physical-qubit block.
        ("lower_to_iqm_json_physical", c_int),
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

# ftqc-opt pass flag (CLI --ftqc-<pass>) → FTQCCfg field. Lets the user compose
# a custom Task-2 pipeline instead of a fixed --pipeline, mirroring ftqc-opt.
FTQC_PASS_FLAGS = {
    "ftqc-steane-encode":     "steane_encode",
    "ftqc-surface-encode":    "surface_encode",
    "ftqc-color-encode":      "color_encode",
    "ftqc-insert-syndrome":   "insert_syndrome",
    "ftqc-insert-correction": "insert_correction",
    "ftqc-pauli-frame-opt":   "pauli_frame_opt",
    "ftqc-transversal-gates": "transversal_gates",
    "ftqc-lower-to-quantum":  "lower_to_quantum",
    "ftqc-lower-to-stim":     "lower_to_stim",
    "ftqc-lower-to-qir":      "lower_to_qir",
    "ftqc-lower-to-qasm3":    "lower_to_qasm3",
    "ftqc-resource-estimate": "resource_estimate",
    "ftqc-magic-distill":     "magic_distill",
}

def make_cfg_from_flags(ecc_kind: str, ecc_dist: int, func_name: str,
                        fields: list) -> FTQCCfg:
    """Build a FTQCCfg with the given per-pass flag fields set (no pre-pipeline)."""
    cfg = FTQCCfg()
    cfg.ecc_kind  = ecc_kind.encode()
    cfg.ecc_dist  = ecc_dist
    cfg.func_name = func_name.encode()
    for f in fields:
        setattr(cfg, f, 1)
    return cfg

def cfg_as_np(cfg: FTQCCfg) -> np.ndarray:
    """Return a numpy uint8 view of a FTQCCfg's raw bytes.
    Since FTQCCfg contains no pointers (only fixed arrays and plain values)
    the byte copy is self-contained and safe to pass to iris.dmem()."""
    return np.frombuffer(
        ctypes.string_at(ctypes.addressof(cfg), ctypes.sizeof(cfg)),
        dtype=np.uint8).copy()

_MAX_OUT = 1 << 19   # 512 KB pre-allocated output buffer

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
    @iris.kernel(in_=["qasm_in", "cfg"], flush_=["mlir_out", "mlir_size"], library=_LIBFTQC)
    def ftqc_iris_qasm_opt(spec, qasm_in, len_qasm_in, cfg, mlir_out, mlir_size):
        pass
    def call(mlir_out, mlir_size, qasm_in):
        return ftqc_iris_qasm_opt(qasm_in, np.int64(len(qasm_in)), cfg_np,
                                  mlir_out, mlir_size, gws=[1])
    return call

def make_mlir_kernel_iris(cfg):
    """Return an @iris.kernel for ftqc_mlir_opt with cfg captured by closure."""
    @iris.kernel(in_=["mlir_in", "cfg"], flush_=["mlir_out", "mlir_out_size"], library=_LIBFTQC)
    def ftqc_iris_mlir_opt(spec, mlir_in, in_size, cfg, mlir_out, mlir_out_size):
        pass
    def call(mlir_out, mlir_out_size, mlir_in, mlir_in_size):
        # Pass the *actual* input length (mlir_in_size[0]), not len(mlir_in):
        # mlir_in is the pre-allocated buffer (zero-padded), and the C side does
        # std::string(data, size) — a too-large size feeds trailing NULs to the
        # MLIR parser ("unexpected character").
        return ftqc_iris_mlir_opt(mlir_in, np.int64(mlir_in_size[0]), cfg, mlir_out, mlir_out_size, gws=[1])
    return call


# ─── IQM device submission (iris_iqm) ─────────────────────────────────────────
#
# IRIS ships a built-in quantum device "iris_iqm" (model iris_iqm=12, type
# iris_qpu) that submits IQM-JSON circuits to an IQM Server endpoint through the
# QDMI C API in libiqm-qdmi-device.so.  Kernel ABI (DeviceIQM):
#   mem  idx0 qc_alias   mem  idx1 circuit_json   mem  idx2 heralding_mode
#   arg  idx3 num_shots  mem  idx4 output (CQireeResultRecord[] = {key,count})
# Session auth is read from the environment at first submit:
#   IRIS_LIB_IQM (lib path) · IQM_BASE_URL · RESONANCE_API_KEY/IQM_TOKEN · IQM_QC_ALIAS

def setup_iqm_env(iqm_lib: str | None = None) -> str | None:
    """Make IRIS able to find + enable the IQM device. Call BEFORE iris.init().

    IRIS resolves the library via getenv("IRIS_LIB_IQM") (falling back to its
    built-in default "libiqm-qdmi-device.so" on LD_LIBRARY_PATH). If the lib
    isn't already locatable we point IRIS_LIB_IQM at the QDMI-on-IQM install.
    """
    lib = iqm_lib or os.environ.get("IRIS_LIB_IQM") or os.environ.get("LIB_IQM")
    if not lib:
        for c in [
            _HERE.parent / os.path.join("IQM", "QDMI-on-IQM", "install", "lib", getlibwithext("libiqm-qdmi-device")),
            _HERE.parent / os.path.join("QDMI-on-IQM", "install", "lib", getlibwithext("libiqm-qdmi-device")),
        ]:
            if c.exists():
                lib = str(c)
                break
    if lib:
        os.environ["IRIS_LIB_IQM"] = lib          # IRIS reads getenv("IRIS_LIB_IQM")
    # Enable the iqm device while keeping openmp for the CPU lowering tasks.
    archs = os.environ.get("IRIS_ARCHS", "openmp")
    if "iqm" not in archs.lower():
        os.environ["IRIS_ARCHS"] = archs + ":iqm"
    # DeviceIQM reads its session config through IRIS's EnvironmentGet, which
    # looks up the IRIS_-prefixed name (e.g. getenv("IRIS_IQM_BASE_URL")). Mirror
    # the conventional vars so users can keep setting the unprefixed ones.
    for k in ("IQM_BASE_URL", "RESONANCE_API_KEY", "IQM_TOKEN", "IQM_QC_ALIAS"):
        v = os.environ.get(k)
        if v and not os.environ.get("IRIS_" + k):
            os.environ["IRIS_" + k] = v
    return lib


def make_iqm_kernel_iris():
    """Return a callable that submits an IQM-JSON circuit on the iris_iqm device.

    The kernel body is empty: the IQM device handles the launch natively, reading
    the args by slot index (KernelGet ignores the binary), so `library` is only a
    placeholder handle.
    """
    @iris.kernel(in_=["qc_alias", "circuit_json", "heralding"],
                 flush_=["output"], library=_LIBFTQC)
    def iqm_submit(spec, qc_alias, circuit_json, heralding, num_shots, output):
        pass
    def call(output, qc_alias, circuit_json, heralding, num_shots):
        return iqm_submit(qc_alias, circuit_json, heralding,
                          np.uint32(num_shots), output, gws=[1])
    return call


def _mlir_unescape(s: str) -> str:
    """Decode an MLIR string-attribute body: \\HH hex, \\\" and \\\\ ."""
    out = bytearray()
    i = 0
    while i < len(s):
        if s[i] == "\\":
            nxt = s[i + 1]
            if nxt == '"':
                out.append(0x22); i += 2
            elif nxt == "\\":
                out.append(0x5C); i += 2
            else:                              # \HH hex escape (e.g. \22, \0A)
                out.append(int(s[i + 1:i + 3], 16)); i += 3
        else:
            out.append(ord(s[i])); i += 1
    return out.decode("utf-8")


def extract_iqm_json(mlir_text: str) -> str:
    """Pull the IQM-JSON string out of the `ftqc.iqm_json` module attribute."""
    m = re.search(r'ftqc\.iqm_json = "((?:[^"\\]|\\.)*)"', mlir_text, re.DOTALL)
    if not m:
        raise RuntimeError("ftqc.iqm_json attribute not found in lowered MLIR "
                           "(was --ftqc-lower-to-iqm-json applied?)")
    return _mlir_unescape(m.group(1))


# Steane [[7,1,3]] Z-stabilizer supports (0-indexed), matching the emitter's
# encoder. The 3 checks form a Hamming syndrome that locates a single X (bit-flip)
# error — the correction relevant to a Z-basis logical readout.
STEANE_STAB = [[1, 2, 3, 4], [0, 2, 3, 5], [0, 1, 3, 6]]
_STEANE_DECODE = {sum((e in STEANE_STAB[k]) << k for k in range(3)): e
                  for e in range(7)}   # syndrome (1..7) -> qubit to flip

def steane_correct(databits: list) -> tuple:
    """(raw_logical, corrected_logical) from 7 physical Z-basis outcomes.
    Correction = classical Pauli-frame update from the data-computed syndrome
    (IQM has no real-time feed-forward, so it is applied in post-processing)."""
    raw = sum(databits) & 1
    syn = 0
    for k in range(3):
        syn |= (sum(databits[j] for j in STEANE_STAB[k]) & 1) << k
    corr = databits[:]
    if syn:
        corr[_STEANE_DECODE[syn]] ^= 1
    return raw, sum(corr) & 1


def strip_comments(circuit: dict) -> dict:
    """Drop `_comment` pseudo-instructions (emitted with include-comments=true);
    the device rejects unknown instruction names. prx angle units are handled by
    the lowering pass (radians vs turns), not here."""
    circuit["instructions"] = [i for i in circuit.get("instructions", [])
                               if i.get("name") != "_comment"]
    return circuit


# Routing must happen in a SEPARATE process: importing qiskit-iqm into the IRIS
# process breaks the QDMI device's symbol resolution (the iris_iqm session fails
# to allocate). This script (run by a Python with qiskit-iqm, >=3.11) reads
# {circuit,url,token,alias} on stdin and prints the device-routed IQM circuit.
_ROUTER_SCRIPT = r"""
import sys, json
d = json.load(sys.stdin)
from qiskit import QuantumCircuit
from iqm.qiskit_iqm import IQMProvider, transpile_to_IQM
c = d["circuit"]
be = IQMProvider(d["url"], token=d["token"], quantum_computer=d["alias"]).get_backend()
qubits = sorted({q for i in c["instructions"] for q in i["locus"]}, key=lambda s: int(s[2:]))
idx = {q: i for i, q in enumerate(qubits)}
nmeas = sum(1 for i in c["instructions"] if i["name"] == "measure")
qc = QuantumCircuit(len(qubits), nmeas); cb = 0
for ins in c["instructions"]:
    a = ins.get("args", {}); loc = [idx[q] for q in ins["locus"]]
    if ins["name"] == "prx": qc.r(a["angle"], a["phase"], loc[0])
    elif ins["name"] == "cz": qc.cz(loc[0], loc[1])
    elif ins["name"] == "measure": qc.measure(loc[0], cb); cb += 1
ser = be.serialize_circuit(transpile_to_IQM(qc, be))
print(json.dumps({"name": c.get("name", "circuit"),
                  "instructions": [{"name": op.name, "locus": list(op.locus),
                                    "args": dict(op.args)} for op in ser.instructions]}))
"""

# Route AND submit in one subprocess via qiskit-iqm (be.run). Used for physical
# circuits, which need routing that the raw iris_iqm/QDMI path can't do. Reads
# {circuit,url,token,alias,shots} on stdin, prints {"counts": {bitstring: n}}.
_ROUTE_SUBMIT_SCRIPT = r"""
import sys, json
d = json.load(sys.stdin)
from qiskit import QuantumCircuit
from iqm.qiskit_iqm import IQMProvider, transpile_to_IQM
c = d["circuit"]
be = IQMProvider(d["url"], token=d["token"], quantum_computer=d["alias"]).get_backend()
qubits = sorted({q for i in c["instructions"] for q in i["locus"]}, key=lambda s: int(s[2:]))
idx = {q: i for i, q in enumerate(qubits)}
nmeas = sum(1 for i in c["instructions"] if i["name"] == "measure")
qc = QuantumCircuit(len(qubits), nmeas); cb = 0; key_to_cb = {}
for ins in c["instructions"]:
    a = ins.get("args", {}); loc = [idx[q] for q in ins["locus"]]
    if ins["name"] == "prx": qc.r(a["angle"], a["phase"], loc[0])
    elif ins["name"] == "cz": qc.cz(loc[0], loc[1])
    elif ins["name"] == "measure":
        key_to_cb[a.get("key", "c%d" % cb)] = cb; qc.measure(loc[0], cb); cb += 1
counts = be.run(transpile_to_IQM(qc, be), shots=d["shots"]).result().get_counts()
# clbits survive transpilation (only qubits are remapped), so key_to_cb still
# identifies which bit of each outcome is which measurement.
print(json.dumps({"counts": {k.replace(" ", ""): int(v) for k, v in counts.items()},
                  "key_to_cb": key_to_cb}))
"""

def _router_python() -> str:
    """Python interpreter that has qiskit-iqm for the routing subprocess."""
    cand = os.environ.get("FTQC_ROUTER_PYTHON") or str(
        _HERE.parent / "IQM" / "realqpu.venv" / "bin" / "python")
    return cand

def _router_env() -> dict:
    """Env for the routing subprocess. The token is passed explicitly in the
    payload, so drop IQM_TOKEN/RESONANCE_API_KEY from the env — iqm-client
    refuses a token given both as an argument and via the environment."""
    return {k: v for k, v in os.environ.items()
            if k not in ("IQM_TOKEN", "RESONANCE_API_KEY")}

def route_and_submit_iqm(circuit: dict, server_url: str, token: str,
                         qc_alias: str, shots: int) -> dict:
    """Route + run a circuit on the real device via qiskit-iqm (subprocess).
    Returns a counts dict {bitstring: n}. Used for physical circuits."""
    import subprocess
    payload = json.dumps({"circuit": circuit, "url": server_url, "token": token,
                          "alias": qc_alias, "shots": shots})
    py = _router_python()
    r = subprocess.run([py, "-c", _ROUTE_SUBMIT_SCRIPT], input=payload,
                       capture_output=True, text=True, env=_router_env())
    if r.returncode != 0:
        raise RuntimeError(f"route+submit subprocess ({py}) failed:\n{r.stderr[-1500:]}")
    out = json.loads(r.stdout.strip().splitlines()[-1])
    return out["counts"], out["key_to_cb"]

def route_for_device(circuit: dict, server_url: str, token: str,
                     qc_alias: str) -> dict:
    """Map a logical-adjacency IQM circuit (radians) onto the device coupling
    graph via qiskit-iqm (in a subprocess). The raw QDMI path can't route, and a
    7-qubit Steane block's CZ pattern is not a device subgraph, so routing is
    required before submitting a physical circuit."""
    import subprocess
    payload = json.dumps({"circuit": circuit, "url": server_url,
                          "token": token, "alias": qc_alias})
    py = _router_python()
    r = subprocess.run([py, "-c", _ROUTER_SCRIPT], input=payload,
                       capture_output=True, text=True, env=_router_env())
    if r.returncode != 0:
        raise RuntimeError(f"routing subprocess ({py}) failed:\n{r.stderr[-1500:]}")
    return json.loads(r.stdout.strip().splitlines()[-1])


def submit_iqm_json(circuit_json: str, qc_alias: str, shots: int,
                    heralding: str) -> list[tuple[int, int]]:
    """Run one IRIS task on the iris_iqm (iris_qpu) device. Returns [(key,count)]."""
    def cstr(s):
        return np.frombuffer((s + "\0").encode(), dtype=np.uint8).copy()

    alias_np  = cstr(qc_alias)
    json_np   = cstr(circuit_json)
    herald_np = cstr(heralding)
    # Output is an array of CQireeResultRecord {uint64 key, uint64 count}; the
    # device reallocates it to (shots+1) records. encoded[0].key = N (#records).
    out_np = np.zeros((shots + 1) * 2, dtype=np.uint64)

    k_iqm = make_iqm_kernel_iris()
    t = k_iqm(out_np, alias_np, json_np, herald_np, shots)
    t.submit(iris.iris_qpu, sync=1)               # iris_qpu routes to DeviceIQM

    # encoded[0].key = N (#records). On a device-side job failure the buffer is
    # reallocated but never written, so clamp N to the valid range rather than
    # trusting a possibly-garbage value.
    n = max(0, min(int(out_np[0]), shots))
    return [(int(out_np[2 * i]), int(out_np[2 * i + 1])) for i in range(1, n + 1)]


def run(qasm_file: Path, ecc_kind: str, ecc_dist: int, func_name: str,
        pipeline: str, out_file: Path,
        submit_iqm: bool = False, qc_alias: str = "default",
        shots: int = 1024, heralding: str = "none",
        keep_turns: bool = False, pass_fields: list = None,
        physical: bool = False, route: bool = None,
        server_url: str = None, token: str = None) -> None:

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
    # Task 2: an explicit --ftqc-* pass list (if given) overrides --pipeline.
    if pass_fields:
        cfg_opt = make_cfg_from_flags(ecc_kind, ecc_dist, func_name, pass_fields)
    else:
        cfg_opt = make_cfg(ecc_kind, ecc_dist, func_name, pipeline=pipeline)

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

    if physical_size_np[0] == 0:
        iris.finalize()
        raise RuntimeError("Task 2 produced no output")

    # ── Write physical-circuit output (host-side; already flushed by sync=1) ───
    n        = int(physical_size_np[0])
    out_text = physical_np[:n].tobytes().decode()
    out_file.write_text(out_text)

    print(f"\nOutput: {out_file}  ({n} chars)")
    preview = out_text[:3000]
    if n > 3000:
        preview += f"\n... [{n - 3000} chars truncated]"
    print(preview)

    # ── Task 3 (lower → IQM JSON) + Task 4 (submit to iris_iqm) ────────────────
    if submit_iqm:
        # Task 3: physical MLIR → IQM JSON (lower_to_iqm_json only, no pipeline).
        # The pass emits prx in radians (angle/phase) by default — what modern
        # IQM station-control requires; --keep-turns asks for angle_t/phase_t.
        # physical=true expands each Steane logical qubit to a 7-qubit block.
        cfg_json = FTQCCfg()
        cfg_json.func_name = func_name.encode()
        cfg_json.lower_to_iqm_json = 1
        cfg_json.lower_to_iqm_json_radians = 0 if keep_turns else 1
        cfg_json.lower_to_iqm_json_physical = 1 if physical else 0
        json_mlir_np      = np.zeros(_MAX_OUT, dtype=np.uint8)
        json_mlir_size_np = np.zeros(2, dtype=np.uint64)
        k_json = make_mlir_kernel_iris(cfg_as_np(cfg_json))
        t3 = k_json(json_mlir_np, json_mlir_size_np, physical_np, physical_size_np)
        t3.submit(iris.iris_cpu, sync=1)

        if json_mlir_size_np[0] == 0:
            iris.finalize()
            raise RuntimeError("Task 3 (IQM JSON lowering) produced no output")

        jn = int(json_mlir_size_np[0])
        circuit = json.loads(extract_iqm_json(json_mlir_np[:jn].tobytes().decode()))
        strip_comments(circuit)
        nq = len({q for i in circuit["instructions"] for q in i["locus"]})
        units = "turns" if keep_turns else "radians"
        iqm_json_file = out_file.with_suffix(".iqm.json")
        iqm_json_file.write_text(json.dumps(circuit, indent=2))
        print(f"\nIQM JSON: {iqm_json_file}  "
              f"({len(circuit['instructions'])} instructions, {nq} qubits, "
              f"prx in {units}{', physical Steane' if physical else ''})")

        # A physical circuit uses logical adjacency, so it must be routed onto
        # the device coupling graph (the raw iris_iqm/QDMI path can't route).
        # Default: route when physical.
        if route is None:
            route = physical

        key_to_cb = None
        if route:
            # Route + submit via qiskit-iqm in a subprocess (needs Python>=3.11).
            # IRIS is finalized first: the lowering tasks are done, and we submit
            # through qiskit-iqm rather than the iris_iqm device.
            iris.finalize()
            if keep_turns:
                raise RuntimeError("routing needs radians; drop --keep-turns")
            print(f"\nRouting + submitting on device via qiskit-iqm "
                  f"(alias='{qc_alias}', shots={shots}) ...")
            counts, key_to_cb = route_and_submit_iqm(circuit, server_url, token,
                                                     qc_alias, shots)
            records = [(int(b, 2), n) for b, n in counts.items()]
            width = max((len(b) for b in counts), default=1)
        else:
            # Task 4: submit the device-valid JSON to the iris_iqm device.
            circuit_json = json.dumps(circuit)
            print(f"\nSubmitting to iris_iqm  (alias='{qc_alias}', shots={shots}, "
                  f"heralding='{heralding}') ...")
            records = submit_iqm_json(circuit_json, qc_alias, shots, heralding)
            iris.finalize()
            width = sum(1 for i in circuit["instructions"] if i.get("name") == "measure")
            counts = {format(k, "0{}b".format(max(width, 1))): n for k, n in records}

        if not records:
            print("  (no measurement records returned — check the device log above)")
            return
        total = sum(c for _, c in records) or 1
        print(f"\nIQM results ({total} shots, {len(records)} outcomes):")
        for key, count in sorted(records, key=lambda kc: -kc[1])[:12]:
            bits = format(key, "0{}b".format(max(width, 1)))
            print(f"  |{bits}>  {count:>6}  ({100.0 * count / total:5.1f}%)")

        if physical and key_to_cb:
            # Steane logical readout: locate the 7 data measurements (keys "mL*"),
            # then report the logical bit RAW (parity) vs CORRECTED (syndrome-decoded
            # Pauli-frame update). qiskit counts are little-endian over clbits.
            data_keys = sorted((k for k in key_to_cb if k.startswith("mL")),
                               key=lambda k: int(k.split("_")[-1]))
            if len(data_keys) == 7:
                raw = {0: 0, 1: 0}
                cor = {0: 0, 1: 0}
                for bits, n in counts.items():
                    b = bits[::-1]                       # b[cb] = clbit cb
                    data = [int(b[key_to_cb[k]]) for k in data_keys]
                    r, c = steane_correct(data)
                    raw[r] += n; cor[c] += n
                print("\nSteane logical bit  (raw = parity, corrected = syndrome-decoded):")
                for lbl, d in (("raw      ", raw), ("corrected", cor)):
                    print(f"  {lbl}  |0>={100.0*d[0]/total:5.1f}%  |1>={100.0*d[1]/total:5.1f}%")
        return

    iris.finalize()

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
    # IQM submission (an extra IRIS task on the iris_iqm device).
    ap.add_argument("--submit-iqm", action="store_true",
                    help="lower the physical circuit to IQM JSON and run it on "
                         "the iris_iqm device via an extra IRIS task")
    ap.add_argument("--qc-alias", default=os.environ.get("IQM_QC_ALIAS", "default"),
                    help="IQM device alias (default: $IQM_QC_ALIAS or 'default')")
    ap.add_argument("--shots", type=int, default=1024)
    ap.add_argument("--heralding", default="none", choices=["none", "zeros", "null"])
    ap.add_argument("--iqm-lib", default=None,
                    help=f"path to {getlibwithext('libiqm-qdmi-device')}; sets IRIS_LIB_IQM if IRIS "
                         "can't find it on its own")
    ap.add_argument("--base-url", default=None,
                    help="IQM server URL (sets IQM_BASE_URL for the iris_iqm device)")
    ap.add_argument("--keep-turns", action="store_true",
                    help="submit prx angles in turns as emitted (default: convert "
                         "to radians, which modern station-control requires)")
    ap.add_argument("--physical", action="store_true",
                    help="expand each Steane logical qubit into a 7-physical-qubit "
                         "block (|0_L> encoder + transversal gates + 7 measures)")
    ap.add_argument("--route", dest="route", action="store_true", default=None,
                    help="route the circuit onto the device coupling map via "
                         "qiskit-iqm before submit (default: on when --physical)")
    ap.add_argument("--no-route", dest="route", action="store_false",
                    help="submit without routing (raw circuit must be device-valid)")
    # ftqc-opt pass flags: compose a custom Task-2 pipeline (overrides --pipeline).
    g = ap.add_argument_group("ftqc-opt passes (compose Task 2; override --pipeline)")
    for flag in FTQC_PASS_FLAGS:
        g.add_argument(f"--{flag}", action="store_true")
    args = ap.parse_args()

    # Collect any --ftqc-* pass flags the user set → FTQCCfg field names.
    pass_fields = [field for flag, field in FTQC_PASS_FLAGS.items()
                   if getattr(args, flag.replace("-", "_"))]

    qasm_file = Path(args.qasm_file)
    if not qasm_file.exists():
        print(f"Error: {qasm_file} not found", file=sys.stderr)
        sys.exit(1)

    out_file = Path(args.out) if args.out else qasm_file.with_suffix(".physical.mlir")

    # The IQM device + its library must be wired up BEFORE iris.init() (in run()).
    if args.submit_iqm:
        if args.base_url:
            os.environ["IQM_BASE_URL"] = args.base_url
        lib = setup_iqm_env(args.iqm_lib)
        if not (os.environ.get("RESONANCE_API_KEY") or os.environ.get("IQM_TOKEN")):
            print("Warning: no RESONANCE_API_KEY / IQM_TOKEN set — the iris_iqm "
                  "device will fail to authenticate.", file=sys.stderr)

    task2_desc = " ".join("--" + f for f in FTQC_PASS_FLAGS
                          if getattr(args, f.replace("-", "_"))) or args.pipeline
    print(f"Input:    {qasm_file}")
    print(f"ECC:      {args.ecc_kind}  d={args.ecc_dist}")
    print(f"Task 2:   {task2_desc}")
    print(f"Output:   {out_file}")
    if args.submit_iqm:
        print(f"Submit:   iris_iqm  alias={args.qc_alias}  shots={args.shots}  "
              f"physical={args.physical}  lib={os.environ.get('IRIS_LIB_IQM', '(IRIS default)')}")
    print()

    run(qasm_file, args.ecc_kind, args.ecc_dist, args.func_name,
        args.pipeline, out_file,
        submit_iqm=args.submit_iqm, qc_alias=args.qc_alias, shots=args.shots,
        heralding=args.heralding, keep_turns=args.keep_turns,
        pass_fields=pass_fields, physical=args.physical, route=args.route,
        server_url=os.environ.get("IQM_BASE_URL"),
        token=os.environ.get("RESONANCE_API_KEY") or os.environ.get("IQM_TOKEN"))

if __name__ == "__main__":
    main()
