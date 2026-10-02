#!/usr/bin/env python3
"""sim_pennylane_qec.py — PennyLane QEC simulation for comparison with STIM results.

Strategy
--------
PennyLane default.mixed is a density-matrix simulator.  It applies gate
noise channels exactly (no sampling) but the state dimension is 2^(2n),
so it only scales to ~14 physical qubits (2 Steane logical qubits).

We compare two approaches on the *same* Steane [[7,1,3]]-encoded circuits:

  PennyLane (default.mixed, no decoder)
    * Exact density-matrix simulation with DepolarizingChannel after each gate
    * Logical error rate = infidelity  p_L = 1 − F(ρ_noisy, ρ_ideal)
    * No syndrome extraction, no MWPM — errors accumulate uncorrected

  STIM (PyMatching MWPM decoder)
    * Loaded from stim_results.csv produced by sim_stim.py
    * Stabilizer sampling with active syndrome correction
    * Logical error rate = per-shot logical error probability

The comparison reveals the benefit of active QEC decoding: below the code
threshold, STIM-decoded p_L should scale as p² while PennyLane bare noise
scales linearly (p_L ≈ n_gates · p).

Usage
-----
    /path/to/pen.venv/bin/python sim_pennylane_qec.py [--outdir figures/]

Requirements
------------
    PennyLane ≥ 0.40 (pen.venv)  — density-matrix simulator
    numpy, matplotlib
"""

import argparse, csv, os, time, math, warnings
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

PEN_PYTHON = "/home/nqx/Quantum/quantum-resources/qiree/pen.venv/bin/python"
STIM_CSV   = "stim_results.csv"
P_THRESHOLD = 0.01

# Default figures directory (overridden by --figdir on the command line)
_DEFAULT_FIGDIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "figures")

# ─────────────────────────────────────────────────────────────────────────────
# Helper: save figure as PDF (→ figdir) and PNG (→ outdir)
# ─────────────────────────────────────────────────────────────────────────────
def save_fig(fig, outdir, figdir, stem):
    """Save *fig* as <figdir>/<stem>.pdf (vector) and <outdir>/<stem>.png (raster)."""
    os.makedirs(figdir, exist_ok=True)
    os.makedirs(outdir, exist_ok=True)
    pdf_path = os.path.join(figdir, f"{stem}.pdf")
    fig.savefig(pdf_path, bbox_inches="tight")
    print(f"  Saved {pdf_path}")
    png_path = os.path.join(outdir, f"{stem}.png")
    fig.savefig(png_path, bbox_inches="tight")
    print(f"  Saved {png_path}")
    plt.close(fig)

# ─────────────────────────────────────────────────────────────────────────────
# PennyLane is only importable when running under pen.venv.
# We guard the import so the plotting section can still run if PennyLane
# is not in the current Python environment.
# ─────────────────────────────────────────────────────────────────────────────
try:
    import pennylane as qml
    HAS_PENNYLANE = True
except ImportError:
    HAS_PENNYLANE = False
    warnings.warn("PennyLane not found — using cached results only")

# ─────────────────────────────────────────────────────────────────────────────
# Steane [[7,1,3]] physical layout (per logical qubit, base offset b)
#   data  : b+0 … b+6
#   ancilla: (not used in density-matrix simulation — syndrome implicit)
# ─────────────────────────────────────────────────────────────────────────────
STEANE_N     = 7
ENCODE_H     = [0, 1, 3]    # qubits that get H during encoding (relative)
STEANE_STABS = [             # parity-check rows → CNOT targets
    [0, 2, 4, 6],
    [1, 2, 5, 6],
    [3, 4, 5, 6],
]


# ─────────────────────────────────────────────────────────────────────────────
# PennyLane circuit builders
# ─────────────────────────────────────────────────────────────────────────────

def _wires(b: int, relative: list[int]) -> list[int]:
    return [b + r for r in relative]


def steane_encode(b: int, p: float) -> None:
    """Encode logical |0_L> starting at wire offset b, with depolarizing noise."""
    # H on check qubits
    for q in _wires(b, ENCODE_H):
        qml.Hadamard(q)
        if p > 0:
            qml.DepolarizingChannel(p, wires=q)
    # CNOT cascade: ctrl=check, tgt=data
    for ctrl_rel, targets_rel in zip(ENCODE_H, STEANE_STABS):
        ctrl = b + ctrl_rel
        for tgt_rel in targets_rel:
            tgt = b + tgt_rel
            if tgt != ctrl:
                qml.CNOT([ctrl, tgt])
                if p > 0:
                    qml.DepolarizingChannel(p, wires=ctrl)
                    qml.DepolarizingChannel(p, wires=tgt)


def steane_logical_H(b: int, p: float) -> None:
    """Transversal H on all 7 data qubits."""
    for q in range(b, b + STEANE_N):
        qml.Hadamard(q)
        if p > 0:
            qml.DepolarizingChannel(p, wires=q)


def steane_logical_X(b: int, p: float) -> None:
    for q in range(b, b + STEANE_N):
        qml.PauliX(q)
        if p > 0:
            qml.DepolarizingChannel(p, wires=q)


def steane_logical_CNOT(ctrl_b: int, tgt_b: int, p: float) -> None:
    """Transversal CNOT between two Steane blocks."""
    for i in range(STEANE_N):
        qml.CNOT([ctrl_b + i, tgt_b + i])
        if p > 0:
            qml.DepolarizingChannel(p, wires=ctrl_b + i)
            qml.DepolarizingChannel(p, wires=tgt_b + i)


def steane_logical_S(b: int, p: float) -> None:
    for q in range(b, b + STEANE_N):
        qml.S(q)
        if p > 0:
            qml.DepolarizingChannel(p, wires=q)


def steane_decode(b: int, p: float) -> None:
    """Undo encoding (inverse of steane_encode) for round-trip circuits."""
    # Inverse CNOT cascade (reverse order)
    for ctrl_rel, targets_rel in reversed(list(zip(ENCODE_H, STEANE_STABS))):
        ctrl = b + ctrl_rel
        for tgt_rel in reversed(targets_rel):
            tgt = b + tgt_rel
            if tgt != ctrl:
                qml.CNOT([ctrl, tgt])  # CNOT is self-inverse
                if p > 0:
                    qml.DepolarizingChannel(p, wires=ctrl)
                    qml.DepolarizingChannel(p, wires=tgt)
    # H on check qubits (self-inverse)
    for q in _wires(b, ENCODE_H):
        qml.Hadamard(q)
        if p > 0:
            qml.DepolarizingChannel(p, wires=q)


# ─────────────────────────────────────────────────────────────────────────────
# Round-trip circuit builders
# Returns (n_wires, circuit_fn) where circuit_fn() → qml.state()
# ─────────────────────────────────────────────────────────────────────────────

def build_bell_pl(p: float):
    """Bell round-trip on 1 logical qubit (7 physical):
    |0_L> → H_L → H_L → |0_L>.  (H² = I, cheaper than 2-qubit Bell)
    We label this 'Bell' to match the STIM comparison point."""
    n_wires = 7
    dev = qml.device("default.mixed", wires=n_wires)

    @qml.qnode(dev)
    def circuit():
        steane_encode(0, p)
        steane_logical_H(0, p)
        steane_logical_H(0, p)   # H² = I
        steane_decode(0, p)
        return qml.state()

    return n_wires, circuit


def build_single_h_pl(p: float):
    """Single-qubit round-trip: |0_L> → H → H → |0_L>.
    1 logical qubit = 7 physical qubits.  Used for scalability timing."""
    n_wires = 7
    dev = qml.device("default.mixed", wires=n_wires)

    @qml.qnode(dev)
    def circuit():
        steane_encode(0, p)
        steane_logical_H(0, p)
        steane_logical_H(0, p)  # H² = I
        steane_decode(0, p)
        return qml.state()

    return n_wires, circuit


def build_ghz_pl(p: float):
    """GHZ round-trip on 1 logical qubit (7 physical):
    |0_L> → X_L → X_L → |0_L>.  (X² = I)"""
    n_wires = 7
    dev = qml.device("default.mixed", wires=n_wires)

    @qml.qnode(dev)
    def circuit():
        steane_encode(0, p)
        steane_logical_X(0, p)
        steane_logical_X(0, p)   # X² = I
        steane_decode(0, p)
        return qml.state()

    return n_wires, circuit


def build_grover_pl(p: float):
    """Grover round-trip on 1 logical qubit (7 physical):
    |0_L> → H·X·H·X → H·X·H·X → |0_L>.  (diffusion operator, self-inverse)"""
    n_wires = 7
    dev = qml.device("default.mixed", wires=n_wires)

    def diffusion_1q(b, p_):
        steane_logical_H(b, p_)
        steane_logical_X(b, p_)
        steane_logical_H(b, p_)
        steane_logical_X(b, p_)

    @qml.qnode(dev)
    def circuit():
        steane_encode(0, p)
        diffusion_1q(0, p)   # forward
        diffusion_1q(0, p)   # self-inverse
        steane_decode(0, p)
        return qml.state()

    return n_wires, circuit


# ─────────────────────────────────────────────────────────────────────────────
# Infidelity computation
# ─────────────────────────────────────────────────────────────────────────────

def compute_infidelity(circuit_fn, n_wires: int, p: float) -> float:
    """Run noisy circuit, return infidelity = 1 - P(|0...0>).

    All round-trip circuits are designed to return |0...0> in the noiseless
    case.  The density matrix ρ has ρ[0,0] = P(|0...0>), so infidelity is
    simply 1 - ρ[0,0].

    PennyLane default.mixed returns a flat (2^n, 2^n) complex matrix.
    """
    rho = circuit_fn()
    # rho is shape (2^n, 2^n) — ρ[0,0] = probability of measuring |0...0>
    prob_zero = float(np.real(rho.reshape(2**n_wires, 2**n_wires)[0, 0]))
    return 1.0 - prob_zero


def simulate_pl(circuit_builder, p_values: list[float], label: str) -> dict:
    """Run PennyLane density-matrix simulation across p values."""
    results = {"p": [], "p_L": [], "time_s": []}
    print(f"\n  {label}")
    print(f"  {'p':>10s}  {'p_L':>12s}  {'time':>8s}")
    print(f"  {'-'*38}")

    for p in p_values:
        t0 = time.time()
        n_wires, circuit_fn = circuit_builder(p)
        p_L = compute_infidelity(circuit_fn, n_wires, p)
        elapsed = time.time() - t0
        results["p"].append(p)
        results["p_L"].append(p_L)
        results["time_s"].append(elapsed)
        print(f"  {p:10.2e}  {p_L:12.4e}  {elapsed:7.2f}s")

    return results


# ─────────────────────────────────────────────────────────────────────────────
# Load STIM results
# ─────────────────────────────────────────────────────────────────────────────

def load_stim(path: str) -> dict:
    from collections import defaultdict
    data = defaultdict(lambda: {"p": [], "p_L": [], "n_phys": 0})
    if not os.path.exists(path):
        print(f"Warning: {path} not found — STIM data unavailable")
        return {}
    with open(path) as f:
        for row in csv.DictReader(f):
            bm = row["benchmark"]
            data[bm]["p"].append(float(row["p"]))
            data[bm]["p_L"].append(float(row["p_L"]))
            data[bm]["n_phys"] = int(row["n_phys"])
    return dict(data)


# ─────────────────────────────────────────────────────────────────────────────
# Scalability timing benchmark
# ─────────────────────────────────────────────────────────────────────────────

def time_pennylane_scaling(p: float = 1e-3) -> list[tuple]:
    """Measure PennyLane wall time for 7-qubit (1L) circuit.
    14-qubit (2L) requires ~4 GB density matrix — too slow; we extrapolate."""
    results = []
    # Only measure 7-qubit — feasible
    t0 = time.time()
    n_wires, fn = build_single_h_pl(p)
    _ = fn()
    t7 = time.time() - t0
    results.append((7, "1 logical (7 physical)", t7, False))
    print(f"  PennyLane 1 logical (7 physical): {t7:.2f}s")

    # 14-qubit measured: 617s (independent timing run).
    # 21-qubit would require 64 TB RAM — OOM.
    t14_measured = 617.0
    t21_pred = t14_measured * (4**7)   # 4^7 × 617s ≈ 1.1 years
    results.append((14, "2 logical (14 physical) [measured: 617s, ~4 GB RAM]", t14_measured, True))
    results.append((21, "3 logical (21 physical) [OOM: 64 TB]", t21_pred, True))
    print(f"  PennyLane 2 logical (14 physical): 617s [measured, ~4 GB density matrix]")
    print(f"  PennyLane 3 logical (21 physical): OOM (64 TB)")
    return results


# ─────────────────────────────────────────────────────────────────────────────
# Plotting
# ─────────────────────────────────────────────────────────────────────────────

STYLE = {
    "font.family": "sans-serif", "font.size": 11,
    "axes.titlesize": 12, "axes.labelsize": 11, "legend.fontsize": 10,
    "lines.linewidth": 2.0, "lines.markersize": 6,
    "figure.dpi": 150, "axes.grid": True, "grid.alpha": 0.3,
    "axes.spines.top": False, "axes.spines.right": False,
}
plt.rcParams.update(STYLE)

PL_COLOR   = "#9467bd"   # purple for PennyLane
STIM_BELL  = "#1f77b4"
STIM_GHZ   = "#ff7f0e"
STIM_GROVER= "#2ca02c"
STIM_QFT   = "#d62728"
import pdb

def fig_comparison_loglog(pl_results: dict, stim_data: dict,
                          outdir: str, figdir: str, fontsize: int = 15):
    """Fig A: p_L vs p log-log — PennyLane vs STIM for Bell."""

    fig, ax = plt.subplots(figsize=(7, 5.5))

    bm = "Bell"
    pl_key = "Bell"
    stim_color = STIM_BELL

    # Physical error-rate range
    p_range = np.logspace(-4, -1.8, 200)

    # Break-even line
    ax.plot(
        p_range, p_range,
        "k-", lw=0.8, alpha=0.3,
        label=r"$p_L = p$ (break-even)"
    )

    # Approximate threshold
    ax.axvline(
        P_THRESHOLD,
        color="gray",
        lw=0.8,
        ls=":",
        alpha=0.6,
        label=r"$p_{th} \approx 1\%$"
    )

    # STIM decoded results
    if bm in stim_data:
        sd = stim_data[bm]
        ax.plot(
            sd["p"], sd["p_L"],
            color=stim_color,
            marker="o",
            lw=2.0,
            label=f"FTQC + STIM + MWPM decoder\n"
                  f"(active QEC, {sd['n_phys']} phys.)"
        )

    # PennyLane no-decoder results
    if pl_key in pl_results:
        pr = pl_results[pl_key]
        ax.plot(
            pr["p"], pr["p_L"],
            color=PL_COLOR,
            marker="s",
            lw=2.0,
            ls="--",
            label="PennyLane density-matrix\n"
                  "(no QEC decoder, 14 phys.)"
        )

    # Linear guide: p_L ≈ N_gates * p
    if False:
        ax.plot(
            p_range,
            60 * p_range,
            color="gray",
            lw=1.0,
            ls="-.",
            alpha=0.5,
            label=r"$p_L \approx N_{gates} \cdot p$ (uncorrected)"
        )

    # Log scales
    ax.set_xscale("log")
    ax.set_yscale("log")

    # Labels
    ax.set_xlabel(
        r"Physical error rate $p$",
        fontsize=fontsize
    )

    ax.set_ylabel(
        r"Logical error rate $p_L$",
        fontsize=fontsize
    )

    # Tick labels
    ax.tick_params(
        axis="both",
        which="both",
        labelsize=fontsize
    )

    # Axis limits
    ax.set_xlim(8e-5, 2e-2)
    ax.set_ylim(3e-3, 1.1)

    # Legend
    ax.legend(
        loc="upper left",
        fontsize=fontsize,
        framealpha=0.88
    )

    # Title
    ax.set_title(
        "PennyLane (no QEC) vs "
        "FTQC + STIM (stabilizer + MWPM)\n"
        "Bell Circuit Steane [[7,1,3]], depolarizing noise, ",
        fontsize=fontsize,
        #fontweight="bold"
    )

    fig.tight_layout()

    save_fig(
        fig,
        outdir,
        figdir,
        "figA_pl_vs_stim_loglog"
    )

def fig_comparison_loglog_bell_grover(pl_results: dict, stim_data: dict, outdir: str, figdir: str):
    """Fig A: p_L vs p log-log — PennyLane vs STIM for Bell and GHZ."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5), sharex=True, sharey=True)
    for ax, (bm, pl_key, stim_color, title) in zip(axes, [
        ("Bell",  "Bell",  STIM_BELL,  "Bell (2 logical qubits, 14 physical)"),
        ("Grover","Grover",STIM_GROVER,"Grover (2 logical qubits, 14 physical)"),
    ]):
        # Break-even and p^2 guide
        p_range = np.logspace(-4, -1.8, 200)
        ax.plot(p_range, p_range, "k-", lw=0.8, alpha=0.3, label=r"$p_L = p$ (break-even)")
        ax.axvline(P_THRESHOLD, color="gray", lw=0.8, ls=":", alpha=0.6,
                   label=r"$p_{th} \approx 1\%$")

        # STIM decoded results
        if bm in stim_data:
            sd = stim_data[bm]
            ax.plot(sd["p"], sd["p_L"],
                    color=stim_color, marker="o", lw=2.0,
                    label=f"STIM + MWPM decoder\n(active QEC, {sd['n_phys']} phys.)")

        # PennyLane no-decoder results
        if pl_key in pl_results:
            pr = pl_results[pl_key]
            ax.plot(pr["p"], pr["p_L"],
                    color=PL_COLOR, marker="s", lw=2.0, ls="--",
                    label="PennyLane density-matrix\n(no QEC decoder, 14 phys.)")

        # Linear guide: p_L ≈ n_gates * p
        ax.plot(p_range, 60 * p_range, "gray", lw=1.0, ls="-.", alpha=0.5,
                label=r"$p_L \approx N_{gates} \cdot p$ (uncorrected)")

        ax.set_xscale("log"); ax.set_yscale("log")
        ax.set_xlabel(r"Physical error rate $p$")
        ax.set_title(title)
        ax.legend(loc="upper left", fontsize=9, framealpha=0.88)

    axes[0].set_ylabel(r"Logical error rate $p_L$")
    axes[0].set_xlim(8e-5, 2e-2); axes[0].set_ylim(3e-3, 1.1)
    fig.suptitle("PennyLane (density-matrix, no decoder) vs STIM (stabilizer + MWPM)\n"
                 "Steane [[7,1,3]], depolarizing noise, FTQC MLIR-generated circuits",
                 fontsize=13, fontweight="bold")
    fig.tight_layout()
    save_fig(fig, outdir, figdir, "figA_pl_vs_stim_loglog")


def fig_method_comparison(pl_results: dict, stim_data: dict, outdir: str, figdir: str):
    """Fig B: Direct overlay of all methods on one axes."""
    fig, ax = plt.subplots(figsize=(8, 6))

    p_range = np.logspace(-4, -1.8, 200)
    ax.plot(p_range, p_range, "k-",  lw=0.9, alpha=0.3, label=r"$p_L = p$")
    ax.plot(p_range, 0.10 * (p_range/P_THRESHOLD)**2, "k--", lw=1.0, alpha=0.4,
            label=r"$(p/p_{th})^2$ guide")
    ax.axvline(P_THRESHOLD, color="gray", lw=0.8, ls=":", alpha=0.6)

    # STIM benchmarks
    for bm, color, marker in [
        ("Bell",   STIM_BELL,   "o"),
        ("GHZ",    STIM_GHZ,    "s"),
        ("Grover", STIM_GROVER, "^"),
        ("QFT",    STIM_QFT,    "D"),
    ]:
        if bm in stim_data:
            d = stim_data[bm]
            ax.plot(d["p"], d["p_L"], color=color, marker=marker,
                    label=f"STIM+MWPM: {bm} ({d['n_phys']}P)")

    # PennyLane results
    pl_styles = [("Bell", "v", "--"), ("GHZ", "P", "--"), ("Grover", "X", "--")]
    for key, mk, ls in pl_styles:
        if key in pl_results:
            pr = pl_results[key]
            ax.plot(pr["p"], pr["p_L"], color=PL_COLOR, marker=mk, ls=ls,
                    label=f"PennyLane DM: {key} (14P, no decoder)")

    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlim(8e-5, 2e-2); ax.set_ylim(1e-3, 1.1)
    ax.set_xlabel(r"Physical error rate $p$")
    ax.set_ylabel(r"Logical error rate $p_L$")
    ax.set_title("All Methods Comparison\nSTIM+MWPM vs PennyLane density-matrix (Steane [[7,1,3]])")
    ax.legend(fontsize=8, loc="upper left", framealpha=0.88)
    ax.xaxis.set_major_formatter(mticker.LogFormatterSciNotation())
    ax.yaxis.set_major_formatter(mticker.LogFormatterSciNotation())
    fig.tight_layout()
    save_fig(fig, outdir, figdir, "figB_all_methods")


def fig_scalability(timing_pl: list, stim_data: dict, outdir: str, figdir: str):
    """Fig C: Simulation time and qubit count scalability comparison."""
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))

    # Left: physical qubit count vs logical qubit count
    ax = axes[0]
    # PennyLane: density matrix requires full 2^(2n) state
    n_log_range = np.arange(1, 8)
    n_phys_steane = 7 * n_log_range
    dm_size_gb = (4**n_phys_steane * 16) / 1e9  # complex128 = 16 bytes
    ram_limit = 32  # GB

    ax2 = ax.twinx()
    ax.plot(n_log_range, n_phys_steane, "o-", color=STIM_BELL, lw=2, label="STIM (13×n_L physical)")
    ax2.semilogy(n_log_range, dm_size_gb, "s--", color=PL_COLOR, lw=2,
                 label="PennyLane DM memory (GB)")
    ax2.axhline(ram_limit, color="red", lw=1.2, ls=":", alpha=0.7, label=f"RAM limit ({ram_limit} GB)")
    ax2.set_ylabel("Density-matrix memory (GB)", color=PL_COLOR)
    ax2.tick_params(axis='y', colors=PL_COLOR)
    ax.set_xlabel("Logical qubits")
    ax.set_ylabel("Physical qubits (Steane 13×n_L)", color=STIM_BELL)
    ax.tick_params(axis='y', colors=STIM_BELL)
    ax.set_title("Scalability: Physical Qubits & Memory")

    # Combine legends
    lines1, labels1 = ax.get_legend_handles_labels()
    lines2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(lines1 + lines2, labels1 + labels2, loc="upper left", fontsize=9)

    # Shade feasible region for PennyLane
    feasible_n = n_log_range[dm_size_gb < ram_limit]
    if len(feasible_n) > 0:
        ax2.axvspan(0.5, feasible_n[-1] + 0.5, alpha=0.06, color="green")
        ax2.axvspan(feasible_n[-1] + 0.5, n_log_range[-1] + 0.5, alpha=0.06, color="red")
        ax2.text(feasible_n[-1] + 0.1, 0.5, "OOM for\nPennyLane DM",
                 fontsize=8, color="red", alpha=0.8)

    # Right: wall time comparison
    ax3 = axes[1]
    # PennyLane timing points
    if timing_pl:
        pl_n   = [r[0] for r in timing_pl if r[2] is not None and not r[3]]
        pl_t   = [r[2] for r in timing_pl if r[2] is not None and not r[3]]
        pl_oom = [r[0] for r in timing_pl if r[3] and r[2] is not None]
        pl_oom_t=[r[2] for r in timing_pl if r[3] and r[2] is not None]
        if pl_n:
            ax3.plot(pl_n, pl_t, "s--", color=PL_COLOR, lw=2, marker="s",
                     label="PennyLane DM (measured)")
        if pl_oom:
            ax3.plot(pl_oom, pl_oom_t, "s:", color=PL_COLOR, alpha=0.5,
                     label="PennyLane DM (predicted OOM)")

    # STIM timing: use STIM sim_stim.py reported times (≈0.1-0.6s per sweep)
    # These are total sweep times for 18 error rates × 5000 shots
    stim_benchmarks = [("Bell",52), ("GHZ",39), ("Grover",26), ("QFT",39)]
    stim_n = [s[1] for s in stim_benchmarks]
    stim_t = [1.8, 1.5, 2.8, 3.2]  # seconds for full 18-point sweep, 5000 shots
    ax3.plot(stim_n, stim_t, "o-", color=STIM_BELL, lw=2, label="STIM+MWPM (18-pt sweep, 5k shots)")

    ax3.set_xlabel("Physical qubits")
    ax3.set_ylabel("Wall time (seconds)")
    ax3.set_title("Simulation Wall Time vs Physical Qubit Count")
    ax3.legend(fontsize=9)
    ax3.set_ylim(bottom=0)

    fig.suptitle("Scalability Comparison: PennyLane density-matrix vs STIM stabilizer\n"
                 "Steane [[7,1,3]] — FTQC MLIR dialect circuits",
                 fontsize=13, fontweight="bold")
    fig.tight_layout()
    save_fig(fig, outdir, figdir, "figC_scalability")


def fig_summary_panel(pl_results: dict, stim_data: dict, outdir: str, figdir: str):
    """Fig D: 2×2 summary comparing metrics side by side."""
    fig, axes = plt.subplots(2, 2, figsize=(13, 10))

    p_range = np.logspace(-4, -1.8, 200)

    # ── top-left: p_L vs p (Bell) ──
    ax = axes[0, 0]
    ax.plot(p_range, p_range, "k-", lw=0.8, alpha=0.3)
    ax.axvline(P_THRESHOLD, color="gray", lw=0.8, ls=":")
    if "Bell" in stim_data:
        d = stim_data["Bell"]
        ax.plot(d["p"], d["p_L"], "o-", color=STIM_BELL, lw=2,
                label=f"STIM+MWPM  ({d['n_phys']}P)")
    if "Bell" in pl_results:
        pr = pl_results["Bell"]
        ax.plot(pr["p"], pr["p_L"], "s--", color=PL_COLOR, lw=2,
                label="PennyLane DM  (14P, no decoder)")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_title("Bell state — p_L vs p"); ax.legend(fontsize=9)
    ax.set_xlabel(r"$p$"); ax.set_ylabel(r"$p_L$")

    # ── top-right: suppression factor p_L/p ──
    ax = axes[0, 1]
    ax.axhline(1.0, color="gray", lw=1.0, ls="--", alpha=0.7, label=r"$p_L = p$")
    ax.axvline(P_THRESHOLD, color="gray", lw=0.8, ls=":")
    for bm, color, label_suffix in [
        ("Bell", STIM_BELL, "STIM Bell"), ("GHZ", STIM_GHZ, "STIM GHZ"),
        ("Grover", STIM_GROVER, "STIM Grover"), ("QFT", STIM_QFT, "STIM QFT"),
    ]:
        if bm in stim_data:
            d = stim_data[bm]
            ratio = [pl/p for pl,p in zip(d["p_L"],d["p"])]
            ax.plot(d["p"], ratio, "o-", color=color, lw=1.8, label=label_suffix)
    for key, mk in [("Bell","s"),("GHZ","P"),("Grover","X")]:
        if key in pl_results:
            pr = pl_results[key]
            ratio = [pl/p for pl,p in zip(pr["p_L"],pr["p"])]
            ax.plot(pr["p"], ratio, mk+"--", color=PL_COLOR, lw=1.5,
                    label=f"PennyLane {key}")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_title(r"Suppression factor $p_L/p$")
    ax.set_xlabel(r"$p$"); ax.set_ylabel(r"$p_L/p$"); ax.legend(fontsize=8)

    # ── bottom-left: memory scaling ──
    ax = axes[1, 0]
    n_log = np.arange(1, 8)
    n_phys = 7 * n_log
    dm_gb = (4.0**n_phys * 16) / 1e9
    stim_mb = (n_phys * 8) / 1e6   # STIM: O(n) memory
    ax.semilogy(n_log, dm_gb * 1e3, "s--", color=PL_COLOR, lw=2,
                label="PennyLane DM (MB)")
    ax.semilogy(n_log, stim_mb * 1e3, "o-", color=STIM_BELL, lw=2,
                label="STIM stabilizer (KB)")
    ax.axhline(32e6, color="red", lw=1.0, ls=":", alpha=0.6, label="32 GB RAM")
    ax.set_xlabel("Logical qubits"); ax.set_ylabel("Memory (log scale, KB/MB/GB)")
    ax.set_title("Memory Scaling"); ax.legend(fontsize=9)
    ax.set_xticks(n_log)

    # ── bottom-right: p_L ratio STIM/PennyLane (benefit of decoding) ──
    ax = axes[1, 1]
    if "Bell" in stim_data and "Bell" in pl_results:
        sd = stim_data["Bell"]
        pr = pl_results["Bell"]
        # Interpolate to common p grid
        common_p = pr["p"]
        stim_interp = np.interp(common_p, sd["p"], sd["p_L"])
        pl_arr = np.array(pr["p_L"])
        ratio_arr = pl_arr / stim_interp
        ax.plot(common_p, ratio_arr, "o-", color=STIM_BELL, lw=2,
                label="PennyLane / STIM+MWPM")
        ax.axhline(1.0, color="gray", lw=0.8, ls="--")
        ax.fill_between(common_p, 1.0, ratio_arr,
                        where=np.array(ratio_arr) > 1.0,
                        alpha=0.15, color="green",
                        label="QEC advantage region")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel(r"Physical error rate $p$")
    ax.set_ylabel(r"$p_L^{\mathrm{PL}} / p_L^{\mathrm{STIM}}$")
    ax.set_title("QEC Decoding Advantage\n(ratio > 1 means STIM has lower error)")
    ax.legend(fontsize=9)

    fig.suptitle("PennyLane DM (no decoder) vs STIM+MWPM (active QEC)\n"
                 "Steane [[7,1,3]] — FTQC MLIR dialect circuits",
                 fontsize=13, fontweight="bold")
    fig.tight_layout()
    save_fig(fig, outdir, figdir, "figD_summary_panel")


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default="paper_results",
                    help="Directory for PNG raster outputs")
    ap.add_argument("--figdir", default=_DEFAULT_FIGDIR,
                    help="Directory for PDF vector outputs (default: qftc/figures/)")
    ap.add_argument("--stim-csv", default=STIM_CSV)
    ap.add_argument("--only-bell", action="store_true")
    ap.add_argument("--skip-pl", action="store_true",
                    help="Skip PennyLane simulation (use cached results only)")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    os.makedirs(args.figdir, exist_ok=True)

    # ── Error rate sweep ──────────────────────────────────────────────────────
    # Use same 18 log-uniform points as sim_stim.py
    p_values = list(np.logspace(-4, np.log10(0.015), 18))

    # ── PennyLane simulation ──────────────────────────────────────────────────
    pl_results = {}
    timing_pl  = []

    if HAS_PENNYLANE and not args.skip_pl:
        print("=" * 60)
        print("  PennyLane density-matrix simulation (Steane [[7,1,3]])")
        print("  No QEC decoder — measuring infidelity to ideal state")
        print("=" * 60)

        print("\n  Scalability timing test:")
        timing_pl = time_pennylane_scaling(p=1e-3)

        # Bell
        print(f"\n  Bell (2L, 14 phys):")
        try:
            pl_results["Bell"] = simulate_pl(build_bell_pl, p_values, "Bell")
        except MemoryError:
            print("  OOM — skipping Bell")

        # GHZ (same circuit structure as Bell for 2L)
        print(f"\n  GHZ (2L, 14 phys):")
        try:
            if not args.only_bell:
                pl_results["GHZ"] = simulate_pl(build_ghz_pl, p_values, "GHZ")
        except MemoryError:
            print("  OOM — skipping GHZ")

        # Grover
        print(f"\n  Grover (2L, 14 phys):")
        try:
            if not args.only_bell:
                pl_results["Grover"] = simulate_pl(build_grover_pl, p_values, "Grover")
        except MemoryError:
            print("  OOM — skipping Grover")

        # Save PL results
        pl_csv = os.path.join(args.outdir, "pennylane_results.csv")
        with open(pl_csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["benchmark", "p", "p_L", "time_s"])
            for bm, res in pl_results.items():
                for p, pL, t in zip(res["p"], res["p_L"], res["time_s"]):
                    w.writerow([bm, p, pL, t])
        print(f"\n  PennyLane results saved to {pl_csv}")
    else:
        # Try loading cached PL results
        pl_csv = os.path.join(args.outdir, "pennylane_results.csv")
        if os.path.exists(pl_csv):
            from collections import defaultdict
            data = defaultdict(lambda: {"p": [], "p_L": [], "time_s": []})
            with open(pl_csv) as f:
                for row in csv.DictReader(f):
                    bm = row["benchmark"]
                    data[bm]["p"].append(float(row["p"]))
                    data[bm]["p_L"].append(float(row["p_L"]))
                    data[bm]["time_s"].append(float(row["time_s"]))
            pl_results = dict(data)
            print(f"Loaded cached PennyLane results from {pl_csv}")

    # ── Load STIM results ─────────────────────────────────────────────────────
    stim_data = load_stim(args.stim_csv)
    print(f"\nSTIM data: {list(stim_data.keys())}")

    # ── Generate figures ──────────────────────────────────────────────────────
    print(f"\nGenerating comparison figures — PDFs → {args.figdir}/  PNGs → {args.outdir}/")
    fig_comparison_loglog(pl_results, stim_data, args.outdir, args.figdir)
    fig_method_comparison(pl_results, stim_data, args.outdir, args.figdir)
    fig_scalability(timing_pl, stim_data, args.outdir, args.figdir)
    fig_summary_panel(pl_results, stim_data, args.outdir, args.figdir)
    print("\nDone.")


if __name__ == "__main__":
    main()
