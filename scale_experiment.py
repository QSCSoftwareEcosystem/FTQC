#!/usr/bin/env python3
"""
scale_experiment.py — FTQC compilation scaling study.

Generates Bell, GHZ, Grover, and QFT circuits from n=2 to n=100 qubits,
runs ftqc-opt with the full resource-estimation pipeline, and collects:
  - Physical qubit breakdown (data, ancilla, distillation)
  - EC rounds, T-gate count, CNOT count
  - Wall-clock compilation time (µs)

Output: qftc/latex/scale_data.csv  (raw numbers)
        qftc/latex/scale_plots.tex  (pgfplots coordinates, included by paper)
"""

import re, subprocess, time, sys, tempfile, os
from pathlib import Path

REPO     = Path(__file__).parent
FTQC_OPT = REPO / "build" / "src" / "ftqc-opt"
OUT_CSV  = REPO / "latex" / "scale_data.csv"
OUT_TEX  = REPO / "latex" / "scale_plots.tex"

ST = "!ftqc.logical_qubit<#ftqc.ecc<steane>>"
SU = "!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>"

PIPELINE = [
    "--ftqc-insert-syndrome",
    "--ftqc-transversal-gates",
    "--ftqc-resource-estimate",
]

# ── MLIR generators ──────────────────────────────────────────────────────────

def _cnot(ctrl, tgt, ctrl_out, tgt_out, ecc):
    return (
        f"  %{ctrl_out}, %{tgt_out} = ftqc.logical_cnot %{ctrl}, %{tgt}\n"
        f"      : ({ecc}, {ecc}) -> ({ecc}, {ecc})"
    )

def _cz(a, b, a_out, b_out, ecc):
    return (
        f"  %{a_out}, %{b_out} = ftqc.logical_cz %{a}, %{b}\n"
        f"      : ({ecc}, {ecc}) -> ({ecc}, {ecc})"
    )

def gen_bell(n):
    """n qubits (n even): n/2 independent Bell pairs, Steane d=3."""
    assert n % 2 == 0
    body, meas = [], []
    for p in range(n // 2):
        a, b = p * 2, p * 2 + 1
        body += [
            f"  %qi{a} = ftqc.init_zero : {ST}",
            f"  %qi{b} = ftqc.init_zero : {ST}",
            f"  %bh{p} = ftqc.logical_h %qi{a} : {ST}",
            _cnot(f"bh{p}", f"qi{b}", f"bc{p}", f"bt{p}", ST),
            f"  %bm{a} = ftqc.logical_measure %bc{p} : ({ST}) -> i1",
            f"  %bm{b} = ftqc.logical_measure %bt{p} : ({ST}) -> i1",
        ]
        meas += [f"%bm{a}", f"%bm{b}"]
    ret_vals  = ", ".join(meas)
    ret_types = ", ".join(["i1"] * n)
    return (
        f"func.func @bell_{n}() -> ({ret_types}) {{\n"
        + "\n".join(body) + "\n"
        + f"  func.return {ret_vals} : {ret_types}\n}}"
    )


def gen_ghz(n):
    """n qubits: GHZ state, Steane d=3."""
    body = []
    for i in range(n):
        body.append(f"  %gq{i} = ftqc.init_zero : {ST}")
    body.append(f"  %gh0 = ftqc.logical_h %gq0 : {ST}")
    cur_ctrl = "gh0"
    t_vals = []
    for i in range(1, n):
        ctrl_out, tgt_out = f"gcc{i}", f"gct{i}"
        body.append(_cnot(cur_ctrl, f"gq{i}", ctrl_out, tgt_out, ST))
        cur_ctrl = ctrl_out
        t_vals.append(tgt_out)

    meas = [f"  %gm0 = ftqc.logical_measure %{cur_ctrl} : ({ST}) -> i1"]
    ret_m = ["%gm0"]
    for i, tv in enumerate(t_vals, 1):
        meas.append(f"  %gm{i} = ftqc.logical_measure %{tv} : ({ST}) -> i1")
        ret_m.append(f"%gm{i}")
    body += meas
    ret_vals  = ", ".join(ret_m)
    ret_types = ", ".join(["i1"] * n)
    return (
        f"func.func @ghz_{n}() -> ({ret_types}) {{\n"
        + "\n".join(body) + "\n"
        + f"  func.return {ret_vals} : {ret_types}\n}}"
    )


def gen_grover(n):
    """
    n-qubit Grover (1 iteration), surface code d=5.

    Circuit structure for n qubits:
      Phase 1 — Superposition : n × H
      Oracle                  : (n−1) × CZ chain (marks |1…1⟩)
      Diffusion               : n×H, n×X, (n−1)×CZ, n×X, n×H
    """
    assert n >= 2
    body = []
    cur = []
    for i in range(n):
        body.append(f"  %grq{i} = ftqc.init_zero : {SU}")
        cur.append(f"grq{i}")

    # Phase 1: H on all
    for i in range(n):
        out = f"grh1_{i}"
        body.append(f"  %{out} = ftqc.logical_h %{cur[i]} : {SU}")
        cur[i] = out

    def cz_chain(cur, tag):
        for i in range(n - 1):
            a_out, b_out = f"{tag}{i}a", f"{tag}{i}b"
            body.append(_cz(cur[i], cur[i + 1], a_out, b_out, SU))
            cur[i]     = a_out
            cur[i + 1] = b_out

    # Oracle: CZ chain
    cz_chain(cur, "gro_")

    # Diffusion: H, X, CZ chain, X, H
    for i in range(n):
        out = f"grdh1_{i}"
        body.append(f"  %{out} = ftqc.logical_h %{cur[i]} : {SU}")
        cur[i] = out
    for i in range(n):
        out = f"grdx1_{i}"
        body.append(f"  %{out} = ftqc.logical_x %{cur[i]} : {SU}")
        cur[i] = out
    cz_chain(cur, "grdc_")
    for i in range(n):
        out = f"grdx2_{i}"
        body.append(f"  %{out} = ftqc.logical_x %{cur[i]} : {SU}")
        cur[i] = out
    for i in range(n):
        out = f"grdh2_{i}"
        body.append(f"  %{out} = ftqc.logical_h %{cur[i]} : {SU}")
        cur[i] = out

    meas, ret_m = [], []
    for i in range(n):
        body.append(f"  %grm{i} = ftqc.logical_measure %{cur[i]} : ({SU}) -> i1")
        ret_m.append(f"%grm{i}")
    ret_vals  = ", ".join(ret_m)
    ret_types = ", ".join(["i1"] * n)
    return (
        f"func.func @grover_{n}() -> ({ret_types}) {{\n"
        + "\n".join(body) + "\n"
        + f"  func.return {ret_vals} : {ret_types}\n}}"
    )


def gen_qft(n):
    """
    n-qubit QFT (function-argument form), Steane d=3.

    Controlled-phase R_k decomposed as 2×T + 2×CNOT (magic-state injection).
    SWAP at the end via 3×CNOT each.
    """
    st = ST
    arg_list = ",\n    ".join(
        [f"    %qa{i} : {st}" for i in range(n)]
    )
    ret_list = ",\n        ".join([st] * n)
    body = []
    cur  = [f"qa{i}" for i in range(n)]
    mc   = [0]   # magic counter

    def fresh_magic():
        name = f"mt{mc[0]}"
        body.append(f"  %{name} = ftqc.prepare_magic_t : !ftqc.magic_state")
        mc[0] += 1
        return name

    def t_gate(q_in, q_out):
        m = fresh_magic()
        body.append(
            f"  %{q_out} = ftqc.logical_t %{q_in}, %{m}\n"
            f"      : ({st}, !ftqc.magic_state) -> {st}"
        )

    def cnot(ctrl, tgt, co, to):
        body.append(_cnot(ctrl, tgt, co, to, st))

    # ── main QFT ──────────────────────────────────────────────────────────
    for i in range(n):
        # H on qubit i
        h_out = f"qh_{i}"
        body.append(f"  %{h_out} = ftqc.logical_h %{cur[i]} : {st}")
        cur[i] = h_out

        # Controlled-R_k from qubit j → qubit i, for j = i+1 .. n-1
        for j in range(i + 1, n):
            # CRz(π/2^{j-i}): decomposed as T + CNOT + T + CNOT on (ctrl=cur[j], tgt=cur[i])
            t0 = f"qt0_{i}_{j}"; t_gate(cur[i], t0); cur[i] = t0
            co0 = f"qco0_{i}_{j}"; to0 = f"qto0_{i}_{j}"
            cnot(cur[j], cur[i], co0, to0); cur[j] = co0; cur[i] = to0
            t1 = f"qt1_{i}_{j}"; t_gate(cur[i], t1); cur[i] = t1
            co1 = f"qco1_{i}_{j}"; to1 = f"qto1_{i}_{j}"
            cnot(cur[j], cur[i], co1, to1); cur[j] = co1; cur[i] = to1

    # ── SWAP pairs (bit-reversal permutation) ─────────────────────────────
    lo, hi = 0, n - 1
    sw = 0
    while lo < hi:
        # SWAP via 3 CNOTs
        co0 = f"sw{sw}co0"; to0 = f"sw{sw}to0"
        cnot(cur[lo], cur[hi], co0, to0); cur[lo] = co0; cur[hi] = to0
        co1 = f"sw{sw}co1"; to1 = f"sw{sw}to1"
        cnot(cur[hi], cur[lo], co1, to1); cur[hi] = co1; cur[lo] = to1
        co2 = f"sw{sw}co2"; to2 = f"sw{sw}to2"
        cnot(cur[lo], cur[hi], co2, to2); cur[lo] = co2; cur[hi] = to2
        lo += 1; hi -= 1; sw += 1

    # Build return value and type lists with one element per line for clarity
    ret_val_lines  = ",\n      ".join([f"%{cur[i]}" for i in range(n)])
    ret_type_lines = ",\n      ".join([st] * n)
    return (
        f"func.func @qft_{n}(\n"
        f"    {arg_list})\n"
        f"    -> ({ret_type_lines}) {{\n"
        + "\n".join(body) + "\n"
        + f"  func.return {ret_val_lines}\n"
        + f"      : {ret_type_lines}\n"
        + "}"   # plain closing brace — NOT inside f-string (no escaping needed)
    )


# ── Runner ───────────────────────────────────────────────────────────────────

ANNOT_RE = re.compile(
    r"ftqc\.resource_annotation\s*\{([^}]+)\}"
)
FIELD_RE = re.compile(r"(\w+)\s*=\s*([0-9.e+\-]+)")


def parse_annotation(ir_text):
    m = ANNOT_RE.search(ir_text)
    if not m:
        return {}
    fields = {}
    for k, v in FIELD_RE.findall(m.group(1)):
        try:
            fields[k] = int(v)
        except ValueError:
            fields[k] = float(v)
    return fields


def run_one(mlir_src, passes=PIPELINE, repeat=3):
    """Write mlir_src to a temp file, run ftqc-opt, return (fields, time_us)."""
    with tempfile.NamedTemporaryFile(suffix=".mlir", mode="w",
                                     delete=False) as f:
        f.write(mlir_src)
        fname = f.name
    try:
        best = float("inf")
        fields = {}
        for _ in range(repeat):
            t0 = time.perf_counter()
            res = subprocess.run(
                [str(FTQC_OPT)] + passes + [fname],
                capture_output=True, text=True
            )
            elapsed = (time.perf_counter() - t0) * 1e6  # µs
            if res.returncode != 0:
                return None, None, res.stderr
            best = min(best, elapsed)
        fields = parse_annotation(res.stdout)
        return fields, best, None
    finally:
        os.unlink(fname)


# ── Main sweep ───────────────────────────────────────────────────────────────

# Bell / GHZ / Grover: even n from 2 to 100
# QFT: n from 2 to 50 (quadratic gate count)
BELL_SIZES   = list(range(2, 102, 2))
GHZ_SIZES    = list(range(2, 102, 2))
GROVER_SIZES = list(range(2, 102, 2))
QFT_SIZES    = list(range(2, 52, 1))

rows = []   # (benchmark, n, logQ, tGates, cnotCount, ecRounds, physTotal, time_us)

def sweep(name, sizes, gen_fn, ecc_label):
    print(f"\n── {name} sweep ──", flush=True)
    for n in sizes:
        try:
            src = gen_fn(n)
        except Exception as e:
            print(f"  n={n:3d}  GEN FAIL: {e}", flush=True)
            continue
        mlir = f"module {{\n{src}\n}}"
        fields, t_us, err = run_one(mlir)
        if fields is None:
            print(f"  n={n:3d}  RUN FAIL: {err[:80]}", flush=True)
            continue
        lq   = fields.get("logicalQubits", 0)
        tg   = fields.get("tGateCount", 0)
        cn   = fields.get("cnotCount", 0)
        ec   = fields.get("ecRounds", 0)
        ph   = fields.get("physicalQubits", 0)   # field name in IR
        # physical breakdown from ECC formulas
        if "steane" in ecc_label:
            data_q    = lq * 7
            ancilla_q = lq * 6
        else:  # surface d=5
            data_q    = lq * 25
            ancilla_q = lq * 24
        distil_q = max(0, ph - data_q - ancilla_q)
        rows.append((name, n, lq, tg, cn, ec, data_q, ancilla_q, distil_q, ph, t_us))
        print(f"  n={n:3d}  logQ={lq:3d}  T={tg:6d}  EC={ec:6d}"
              f"  phys={ph:6d}  t={t_us:8.1f}µs", flush=True)


sweep("Bell",   BELL_SIZES,   gen_bell,   "steane")
sweep("GHZ",    GHZ_SIZES,    gen_ghz,    "steane")
sweep("Grover", GROVER_SIZES, gen_grover, "surface")
sweep("QFT",    QFT_SIZES,    gen_qft,    "steane")

# ── Write CSV ────────────────────────────────────────────────────────────────

header = ("benchmark,n,logQ,tGates,cnotCount,ecRounds,"
          "dataQubits,ancillaQubits,distilQubits,physTotal,time_us")
with open(OUT_CSV, "w") as f:
    f.write(header + "\n")
    for r in rows:
        f.write(",".join(str(x) for x in r) + "\n")
print(f"\nCSV written: {OUT_CSV}", flush=True)

# ── Generate pgfplots coordinate blocks ──────────────────────────────────────

def coords(name, col_idx):
    """Return pgfplots coordinates string for benchmark `name`, column `col_idx`."""
    pts = [(r[1], r[col_idx]) for r in rows if r[0] == name]
    return " ".join(f"({x},{y})" for x, y in pts)

# Column indices: n=1, logQ=2, tGates=3, cnotCount=4, ecRounds=5,
# dataQ=6, ancillaQ=7, distilQ=8, physTotal=9, time_us=10

with open(OUT_TEX, "w") as f:
    f.write("% Auto-generated by scale_experiment.py — do not edit manually\n")
    f.write("%\n")
    for bench in ("Bell", "GHZ", "Grover", "QFT"):
        tag = bench.lower()
        f.write(f"% ── {bench} ──\n")
        for label, idx in [
            ("DataQubits",    6),
            ("AncillaQubits", 7),
            ("DistilQubits",  8),
            ("PhysTotal",     9),
            ("ECRounds",      5),
            ("TGates",        3),
            ("TimeUs",       10),
        ]:
            pts = coords(bench, idx)
            f.write(f"\\def\\coords{bench}{label}{{{pts}}}\n")
        f.write("\n")

print(f"pgfplots data written: {OUT_TEX}", flush=True)
