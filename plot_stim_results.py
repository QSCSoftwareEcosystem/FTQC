#!/usr/bin/env python3
"""plot_stim_results.py — Generate publication-quality figures from STIM QEC simulation results.

Reads stim_results.csv produced by sim_stim.py and produces:
  Fig 1: Logical error rate vs physical error rate (all benchmarks, log-log)
  Fig 2: Suppression factor p_L/p vs p (linear-x, log-y)
  Fig 3: Physical qubit count vs logical qubit count (scalability)
  Fig 4: p_L vs p_physical for each benchmark with threshold annotation
  Fig 5: Composite summary panel (2×2)

Usage:
    qasm.venv/bin/python plot_stim_results.py [--input stim_results.csv] [--outdir figures/]
"""

import argparse, os, csv, math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
from matplotlib.lines import Line2D
from collections import defaultdict

# ---------------------------------------------------------------------------
# Style
# ---------------------------------------------------------------------------
plt.rcParams.update({
    "font.family":      "sans-serif",
    "font.size":        11,
    "axes.titlesize":   12,
    "axes.labelsize":   11,
    "legend.fontsize":  10,
    "lines.linewidth":  2.0,
    "lines.markersize": 6,
    "figure.dpi":       150,
    "axes.grid":        True,
    "grid.alpha":       0.3,
    "axes.spines.top":  False,
    "axes.spines.right":False,
})

COLORS = {
    "Bell":   "#1f77b4",
    "GHZ":    "#ff7f0e",
    "Grover": "#2ca02c",
    "QFT":    "#d62728",
}
MARKERS = {"Bell": "o", "GHZ": "s", "Grover": "^", "QFT": "D"}
LABEL = {
    "Bell":   "Bell (n=4 logical qubits)",
    "GHZ":    "GHZ  (n=3 logical qubits)",
    "Grover": "Grover (n=2 logical qubits)",
    "QFT":    "QFT  (n=3 logical qubits)",
}

P_THRESHOLD = 0.01   # Steane [[7,1,3]] threshold ≈ 1%

# Default figures directory (overridden by --figdir on the command line)
_DEFAULT_FIGDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "figures")

# ---------------------------------------------------------------------------
# Helper: save figure as PDF (→ figdir) and PNG (→ outdir)
# ---------------------------------------------------------------------------
def save_fig(fig, outdir, figdir, stem):
    """Save *fig* as <figdir>/<stem>.pdf (vector) and <outdir>/<stem>.png (raster)."""
    os.makedirs(figdir,  exist_ok=True)
    os.makedirs(outdir,  exist_ok=True)
    pdf_path = os.path.join(figdir, f"{stem}.pdf")
    fig.savefig(pdf_path, bbox_inches="tight")
    print(f"  Saved {pdf_path}")
    png_path = os.path.join(outdir, f"{stem}.png")
    fig.savefig(png_path, bbox_inches="tight")
    print(f"  Saved {png_path}")
    plt.close(fig)

# ---------------------------------------------------------------------------
# Load CSV
# ---------------------------------------------------------------------------
def load(path):
    data = defaultdict(lambda: {"p": [], "p_L": [], "n_phys": 0, "n_logical": 0})
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            bm = row["benchmark"]
            data[bm]["p"].append(float(row["p"]))
            data[bm]["p_L"].append(float(row["p_L"]))
            data[bm]["n_phys"]    = int(row["n_phys"])
            data[bm]["n_logical"] = int(row["n_logical"])
    return data

# ---------------------------------------------------------------------------
# Fig 1: p_L vs p — log-log (all benchmarks)
# ---------------------------------------------------------------------------
def fig_loglog(data, outdir, figdir):
    fig, ax = plt.subplots(figsize=(7, 5.5))

    p_range = np.logspace(-4, -1.8, 200)
    # p^2 / p_th guide line (d=3 code-distance scaling)
    guide = 0.14 * (p_range / P_THRESHOLD) ** 2
    ax.plot(p_range, guide, "k--", lw=1.2, alpha=0.5, label=r"$\propto (p/p_{th})^2$ guide")
    ax.axvline(P_THRESHOLD, color="gray", lw=1.0, ls=":", alpha=0.7, label=r"$p_{th} \approx 1\%$")
    ax.plot([1e-4, 0.015], [1e-4, 0.015], "k-", lw=1.0, alpha=0.35, label=r"$p_L = p$ (break-even)")

    for bm, d in data.items():
        ax.plot(d["p"], d["p_L"],
                color=COLORS[bm], marker=MARKERS[bm],
                label=LABEL[bm], zorder=3)

    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel(r"Physical error rate $p$")
    ax.set_ylabel(r"Logical error rate $p_L$ (per shot)")
    ax.set_title("QEC Simulation: Steane [[7,1,3]] — FTQC MLIR-generated STIM circuits\n"
                 "Benchmarks encoded as round-trip circuits, MWPM decoder (PyMatching)")
    ax.legend(loc="upper left", framealpha=0.85)
    ax.set_xlim(8e-5, 2e-2); ax.set_ylim(3e-3, 1.1)
    ax.xaxis.set_major_formatter(mticker.LogFormatterSciNotation())
    ax.yaxis.set_major_formatter(mticker.LogFormatterSciNotation())
    fig.tight_layout()
    save_fig(fig, outdir, figdir, "fig1_pL_vs_p_loglog")

# ---------------------------------------------------------------------------
# Fig 2: Suppression factor p_L/p vs p
# ---------------------------------------------------------------------------
def fig_suppression(data, outdir, figdir):
    fig, ax = plt.subplots(figsize=(7, 5))

    ax.axhline(1.0, color="gray", lw=1.2, ls="--", alpha=0.7, label=r"Break-even ($p_L/p = 1$)")
    ax.axvline(P_THRESHOLD, color="gray", lw=1.0, ls=":", alpha=0.7, label=r"$p_{th} \approx 1\%$")

    for bm, d in data.items():
        ratio = [pl / p for pl, p in zip(d["p_L"], d["p"])]
        ax.plot(d["p"], ratio,
                color=COLORS[bm], marker=MARKERS[bm],
                label=LABEL[bm], zorder=3)

    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel(r"Physical error rate $p$")
    ax.set_ylabel(r"Suppression factor $p_L / p$")
    ax.set_title("Error Suppression Factor vs Physical Error Rate\n"
                 "Values > 1 mean the code amplifies errors (circuit-level, not per-gate)")
    ax.legend(loc="upper right", framealpha=0.85)
    fig.tight_layout()
    save_fig(fig, outdir, figdir, "fig2_suppression")

# ---------------------------------------------------------------------------
# Fig 3: Scalability — physical qubits vs logical qubits
# ---------------------------------------------------------------------------
def fig_scalability(data, outdir, figdir):
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))

    # Left: bar chart of physical qubit counts
    ax = axes[0]
    benchmarks = list(data.keys())
    n_phys   = [data[bm]["n_phys"]    for bm in benchmarks]
    n_logical= [data[bm]["n_logical"] for bm in benchmarks]
    colors   = [COLORS[bm] for bm in benchmarks]
    bars = ax.bar(benchmarks, n_phys, color=colors, edgecolor="white", linewidth=1.5)
    for bar, nl, np_ in zip(bars, n_logical, n_phys):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                f"{nl}L→{np_}P", ha="center", va="bottom", fontsize=9, fontweight="bold")
    ax.set_ylabel("Physical qubits (Steane 13×n_L)")
    ax.set_title("Physical Qubit Overhead per Benchmark\n(Steane [[7,1,3]]: 13 physical/logical)")
    # Annotate Steane overhead line
    n_range = np.array([1, 5])
    ax.plot([], [], 'k--', lw=1, label="13 × n_logical")
    ax.set_ylim(0, max(n_phys) * 1.25)
    ax.legend()

    # Right: p_L at p=1e-3 (≈ near-threshold) for each benchmark
    ax2 = axes[1]
    p_target = 1e-3
    pL_at_target = []
    for bm in benchmarks:
        ps = np.array(data[bm]["p"])
        pLs= np.array(data[bm]["p_L"])
        idx = np.argmin(np.abs(ps - p_target))
        pL_at_target.append(pLs[idx])

    bars2 = ax2.bar(benchmarks, pL_at_target, color=colors, edgecolor="white", linewidth=1.5)
    ax2.axhline(p_target, color="k", ls="--", lw=1.2, label=rf"$p = {p_target:.0e}$ (break-even)")
    for bar, v in zip(bars2, pL_at_target):
        ax2.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.002,
                f"{v:.3f}", ha="center", va="bottom", fontsize=9, fontweight="bold")
    ax2.set_ylabel(r"$p_L$ at $p = 10^{-3}$")
    ax2.set_title(r"Logical Error Rate at $p = 10^{-3}$ (near threshold)")
    ax2.legend()

    fig.suptitle("FTQC MLIR → STIM: Scalability & Error Rate Summary", fontsize=13, fontweight="bold")
    fig.tight_layout()
    save_fig(fig, outdir, figdir, "fig3_scalability")

# ---------------------------------------------------------------------------
# Fig 4: Per-benchmark subplots with threshold annotation
# ---------------------------------------------------------------------------
def fig_per_benchmark(data, outdir, figdir):
    bms = list(data.keys())
    fig, axes = plt.subplots(2, 2, figsize=(12, 9), sharex=True, sharey=True)

    for ax, bm in zip(axes.flat, bms):
        d = data[bm]
        ps  = np.array(d["p"])
        pLs = np.array(d["p_L"])

        # Shading: above/below threshold
        ax.axvspan(1e-4, P_THRESHOLD, alpha=0.07, color="green", label="below threshold")
        ax.axvspan(P_THRESHOLD, 2e-2, alpha=0.07, color="red",   label="above threshold")
        ax.axvline(P_THRESHOLD, color="gray", lw=1.0, ls=":", alpha=0.8)
        ax.plot(ps, ps, "k-", lw=1.0, alpha=0.35, label=r"$p_L = p$")

        # Fit p^2 scaling to first 6 points
        log_p  = np.log10(ps[:6])
        log_pL = np.log10(pLs[:6])
        slope, intercept = np.polyfit(log_p, log_pL, 1)
        p_fit = np.logspace(-4, -2, 100)
        pL_fit = 10**intercept * p_fit**slope
        ax.plot(p_fit, pL_fit, "--", color=COLORS[bm], lw=1.2, alpha=0.6,
                label=rf"fit: $p_L \propto p^{{{slope:.1f}}}$")

        ax.plot(ps, pLs, color=COLORS[bm], marker=MARKERS[bm],
                lw=2.0, label=f"{bm} ({d['n_logical']}L, {d['n_phys']}P)")

        ax.set_xscale("log"); ax.set_yscale("log")
        ax.set_xlim(8e-5, 2e-2); ax.set_ylim(3e-3, 1.1)
        ax.set_title(f"{bm} — {d['n_logical']} logical qubits → {d['n_phys']} physical")
        ax.legend(loc="upper left", fontsize=9, framealpha=0.85)
        ax.xaxis.set_major_formatter(mticker.LogFormatterSciNotation())
        ax.yaxis.set_major_formatter(mticker.LogFormatterSciNotation())

    for ax in axes[1]:
        ax.set_xlabel(r"Physical error rate $p$")
    for ax in axes[:, 0]:
        ax.set_ylabel(r"Logical error rate $p_L$")

    fig.suptitle("Per-Benchmark QEC Simulation (Steane [[7,1,3]], MWPM decoder)\n"
                 "FTQC MLIR dialect → STIM physical circuits", fontsize=13, fontweight="bold")
    fig.tight_layout()
    save_fig(fig, outdir, figdir, "fig4_per_benchmark")

# ---------------------------------------------------------------------------
# Fig 5: 2×2 composite summary
# ---------------------------------------------------------------------------
def fig_composite(data, outdir, figdir):
    fig = plt.figure(figsize=(13, 10))
    gs  = fig.add_gridspec(2, 2, hspace=0.38, wspace=0.32)
    ax_ll  = fig.add_subplot(gs[0, 0])   # log-log pL vs p
    ax_sup = fig.add_subplot(gs[0, 1])   # suppression factor
    ax_bar = fig.add_subplot(gs[1, 0])   # physical qubit bar
    ax_fit = fig.add_subplot(gs[1, 1])   # slope (code distance scaling)

    # ---- top-left: log-log ----
    p_range = np.logspace(-4, -1.8, 200)
    guide   = 0.14 * (p_range / P_THRESHOLD) ** 2
    ax_ll.plot(p_range, guide, "k--", lw=1.0, alpha=0.5, label=r"$(p/p_{th})^2$")
    ax_ll.plot([1e-4, 0.015], [1e-4, 0.015], "k-", lw=0.8, alpha=0.35, label=r"$p_L=p$")
    ax_ll.axvline(P_THRESHOLD, color="gray", lw=0.8, ls=":", alpha=0.6)
    for bm, d in data.items():
        ax_ll.plot(d["p"], d["p_L"], color=COLORS[bm], marker=MARKERS[bm],
                   label=bm, markersize=5)
    ax_ll.set_xscale("log"); ax_ll.set_yscale("log")
    ax_ll.set_xlabel(r"$p$"); ax_ll.set_ylabel(r"$p_L$")
    ax_ll.set_title(r"$p_L$ vs $p$ (log-log)")
    ax_ll.legend(fontsize=9)

    # ---- top-right: suppression ----
    ax_sup.axhline(1.0, color="gray", lw=1.0, ls="--", alpha=0.7)
    ax_sup.axvline(P_THRESHOLD, color="gray", lw=0.8, ls=":", alpha=0.6)
    for bm, d in data.items():
        ratio = [pl / p for pl, p in zip(d["p_L"], d["p"])]
        ax_sup.plot(d["p"], ratio, color=COLORS[bm], marker=MARKERS[bm],
                    label=bm, markersize=5)
    ax_sup.set_xscale("log"); ax_sup.set_yscale("log")
    ax_sup.set_xlabel(r"$p$"); ax_sup.set_ylabel(r"$p_L / p$")
    ax_sup.set_title("Suppression factor")
    ax_sup.legend(fontsize=9)

    # ---- bottom-left: qubit count bar ----
    benchmarks = list(data.keys())
    n_phys = [data[bm]["n_phys"] for bm in benchmarks]
    colors = [COLORS[bm] for bm in benchmarks]
    ax_bar.bar(benchmarks, n_phys, color=colors, edgecolor="white")
    ax_bar.set_ylabel("Physical qubits")
    ax_bar.set_title("Physical qubit overhead")
    for i, (bm, np_) in enumerate(zip(benchmarks, n_phys)):
        ax_bar.text(i, np_ + 0.3, str(np_), ha="center", va="bottom", fontsize=10, fontweight="bold")

    # ---- bottom-right: fitted p^k slope for each benchmark ----
    slopes = {}
    for bm, d in data.items():
        ps  = np.array(d["p"])
        pLs = np.array(d["p_L"])
        mask = ps < P_THRESHOLD
        if mask.sum() >= 3:
            slope, _ = np.polyfit(np.log10(ps[mask]), np.log10(pLs[mask]), 1)
            slopes[bm] = slope
    bm_labels = list(slopes.keys())
    slope_vals = [slopes[bm] for bm in bm_labels]
    bar_colors = [COLORS[bm] for bm in bm_labels]
    ax_fit.bar(bm_labels, slope_vals, color=bar_colors, edgecolor="white")
    ax_fit.axhline(2.0, color="k", ls="--", lw=1.0, alpha=0.6, label="d=3 ideal (slope=2)")
    ax_fit.set_ylabel("Fitted slope $k$ in $p_L \\propto p^k$")
    ax_fit.set_title(r"Error scaling exponent below $p_{th}$")
    ax_fit.legend(fontsize=9)
    for i, (bm, sv) in enumerate(zip(bm_labels, slope_vals)):
        ax_fit.text(i, sv + 0.02, f"{sv:.2f}", ha="center", va="bottom", fontsize=10, fontweight="bold")

    fig.suptitle("FTQC MLIR → STIM QEC Simulation Summary\n"
                 "Steane [[7,1,3]], depolarizing noise, MWPM (PyMatching)",
                 fontsize=13, fontweight="bold")
    save_fig(fig, outdir, figdir, "fig5_composite_summary")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input",  default="stim_results.csv",
                    help="CSV produced by sim_stim.py")
    ap.add_argument("--outdir", default="paper_results",
                    help="Directory for PNG raster outputs")
    ap.add_argument("--figdir", default=_DEFAULT_FIGDIR,
                    help="Directory for PDF vector outputs (default: qftc/figures/)")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    os.makedirs(args.figdir, exist_ok=True)
    data = load(args.input)
    print(f"Loaded {sum(len(d['p']) for d in data.values())} data points for "
          f"{len(data)} benchmarks: {list(data.keys())}")
    print(f"Generating figures — PDFs → {args.figdir}/  PNGs → {args.outdir}/")

    fig_loglog(data, args.outdir, args.figdir)
    fig_suppression(data, args.outdir, args.figdir)
    fig_scalability(data, args.outdir, args.figdir)
    fig_per_benchmark(data, args.outdir, args.figdir)
    fig_composite(data, args.outdir, args.figdir)
    print("Done.")

if __name__ == "__main__":
    main()
