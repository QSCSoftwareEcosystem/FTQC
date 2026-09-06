#!/usr/bin/env python3
"""plot_paper_figures.py -- generate the FTQC paper's data figures as PDFs.

Produces (into ftqc.paper/figs/):
  fig_stim_qec.pdf     FTQC+Stim+MWPM per-shot failure for the 4 benchmarks
  fig_compile_time.pdf compile time vs logical-qubit count (from scale_plots.tex)
  fig_passtime.pdf     per-pass in-pipeline time for the 4 benchmarks
  fig_mem_decoded.pdf  decoded logical error rate (Steane memory experiment)

Data provenance:
  - stim_qec / compile_time: the existing sweeps (sim_stim.py; scale_plots.tex).
  - passtime: mean of 60 `ftqc-opt -mlir-timing` runs.
  - mem_decoded: sim_stim_memory.py (FTQC -> Stim -> PyMatching MWPM).

Usage:  python3 plot_paper_figures.py   (requires matplotlib)
"""
import os, re
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # repo root (utils/..)
FIGS = os.path.join(ROOT, "ftqc.paper", "figs")
SCALE = os.path.join(ROOT, "ftqc.paper", "sections", "scale_plots.tex")

plt.rcParams.update({
    "font.size": 8, "axes.labelsize": 8, "legend.fontsize": 6.5,
    "xtick.labelsize": 7, "ytick.labelsize": 7, "figure.dpi": 150,
    "axes.grid": True, "grid.linestyle": ":", "grid.color": "0.7",
    "grid.linewidth": 0.5, "lines.linewidth": 1.3, "savefig.bbox": "tight",
    "pdf.fonttype": 42, "figure.autolayout": True,
})
COL = 3.3  # single-column width in inches


def save(fig, name):
    # Omit the PDF timestamp so regenerations are byte-reproducible (no spurious
    # git diffs against the committed figures).
    fig.savefig(os.path.join(FIGS, name), metadata={"CreationDate": None})
    plt.close(fig)
    print("wrote", os.path.join("ftqc.paper/figs", name))


# ---------------------------------------------------------------- stim_qec ----
def fig_stim_qec():
    p = [1e-4, 1.80e-4, 3.26e-4, 5.88e-4, 1.061e-3, 2.574e-3, 6.242e-3, 1.514e-2]
    series = {
        "Bell (26P)":   ([0.0088, 0.016, 0.0341, 0.0561, 0.0995, 0.2183, 0.401, 0.4946], "o", "tab:blue"),
        "GHZ (52P)":    ([0.0135, 0.0262, 0.0427, 0.0795, 0.1356, 0.2925, 0.4522, 0.5046], "s", "tab:orange"),
        "Grover (26P)": ([0.0361, 0.0619, 0.1067, 0.1797, 0.276, 0.4345, 0.4937, 0.502], "^", "tab:green"),
        "QFT (39P)":    ([0.0206, 0.043, 0.0707, 0.1259, 0.216, 0.3937, 0.4848, 0.4896], "D", "tab:red"),
    }
    fig, ax = plt.subplots(figsize=(COL, 1.7))
    xs = [1e-4, 2e-2]
    ax.plot(xs, xs, color="0.6", lw=0.8, label=r"$p_{\rm shot}=p$")
    ax.axvline(0.01, color="k", ls=":", lw=1.0, label=r"$p_{\rm th}$")
    for lab, (y, m, c) in series.items():
        ax.plot(p, y, marker=m, ms=3, color=c, label=lab)
    ax.set(xscale="log", yscale="log", xlim=(8e-5, 2e-2), ylim=(5e-4, 1.2),
           xlabel="Physical error rate $p$", ylabel=r"Per-shot failure $p_{\rm shot}$")
    ax.legend(loc="upper left", ncol=2, handlelength=1.4, columnspacing=1.0)
    save(fig, "fig_stim_qec.pdf")


# ------------------------------------------------------------- compile_time ---
def _parse_scale(macro):
    txt = open(SCALE).read()
    m = re.search(r"\\def\\" + macro + r"\{(.*?)\}", txt, re.S)
    if not m:
        return [], []
    pts = re.findall(r"\(([-\d.eE+]+),\s*([-\d.eE+]+)\)", m.group(1))
    xs = [float(a) for a, _ in pts]
    ys = [float(b) / 1000.0 for _, b in pts]  # us -> ms
    return xs, ys


def fig_compile_time():
    fig, ax = plt.subplots(figsize=(COL, 1.7))
    for macro, lab, c in [("coordsBellTimeUs", "Bell", "tab:blue"),
                          ("coordsGHZTimeUs", "GHZ", "tab:red"),
                          ("coordsGroverTimeUs", "Grover", "tab:green")]:
        xs, ys = _parse_scale(macro)
        if xs:
            ax.plot(xs, ys, color=c, label=lab)
    ax.set(xlabel="Logical qubits $n$", ylabel="Bell / GHZ / Grover (ms)",
           xlim=(0, 102), ylim=(0, None))
    ax2 = ax.twinx()
    xs, ys = _parse_scale("coordsQFTTimeUs")
    if xs:
        ax2.plot(xs, ys, color="tab:orange", ls="--", label="QFT")
    ax2.set_ylabel("QFT (ms)")
    ax2.grid(False)
    l1, la1 = ax.get_legend_handles_labels()
    l2, la2 = ax2.get_legend_handles_labels()
    ax.legend(l1 + l2, la1 + la2, loc="upper left", handlelength=1.4)
    save(fig, "fig_compile_time.pdf")


# ---------------------------------------------------------------- passtime ----
def fig_passtime():
    ben: list = ["Bell", "GHZ-4", "Grover-2", "QFT-3"]
    passes = {
        "Parse":          ([741.7, 815.0, 903.3, 1161.7], "tab:blue"),
        "InsertSyndrome": ([200.0, 211.7, 235.0, 330.0], "tab:orange"),
        "Transversal":    ([0.0, 0.0, 0.0, 1.7], "tab:green"),
        "ResourceEst":    ([198.3, 200.0, 200.0, 203.3], "tab:red"),
    }
    fig, ax = plt.subplots(figsize=(COL, 1.7))
    x = list(range(len(ben)))
    bottom = [0.0] * len(ben)
    for lab, (vals, c) in passes.items():
        ax.bar(list(x), vals, bottom=bottom, color=c, label=lab, width=0.6,
               edgecolor="white", linewidth=0.4)
        bottom = [b + v for b, v in zip(bottom, vals)]
    ax.set_xticks(list(x))
    ax.set_xticklabels(ben)
    ax.set_ylabel(r"In-pipeline time ($\mu$s)")
    ax.grid(axis="x", visible=False)
    ax.legend(loc="upper left", ncol=2, handlelength=1.2, columnspacing=1.0)
    save(fig, "fig_passtime.pdf")


# ------------------------------------------------------------- mem_decoded ----
def fig_mem_decoded():
    p = [1.0e-3, 1.577e-3, 2.487e-3, 3.922e-3, 6.185e-3, 9.753e-3,
         1.538e-2, 2.425e-2, 3.825e-2, 6.032e-2, 9.512e-2, 1.5e-1]
    decoded = [2.62e-2, 3.89e-2, 6.29e-2, 9.63e-2, 1.493e-1, 2.171e-1,
               3.020e-1, 4.064e-1, 4.699e-1, 4.972e-1, 4.978e-1, 4.970e-1]
    raw = [6.34e-2, 9.40e-2, 1.427e-1, 2.033e-1, 2.833e-1, 3.676e-1,
           4.362e-1, 4.795e-1, 4.946e-1, 4.980e-1, 5.040e-1, 4.998e-1]
    p_ana = [1.0e-3, 2.49e-3, 6.18e-3, 1.538e-2, 3.825e-2, 9.51e-2, 2.366e-1, 5.883e-1]
    fig, ax = plt.subplots(figsize=(COL, 1.7))
    xs = [1e-3, 1.5e-1]
    ax.plot(xs, xs, color="0.6", lw=0.8, label=r"$p_L=p$")
    ax.plot(p, decoded, "o-", ms=3, color="tab:blue", label="decoded $p_L$ (MWPM)")
    ax.plot(p, raw, "s--", ms=3, color="tab:red", label="undecoded")
    ax.plot(p[:len(p_ana)], p_ana, "^:", ms=3.5, color="tab:green",
            label=r"analytic $p_{\rm total}$")
    ax.set(xscale="log", yscale="log", xlim=(8e-4, 1.8e-1), ylim=(8e-4, 1.1),
           xlabel="Physical error rate $p$", ylabel="Logical error rate")
    ax.legend(loc="upper left", handlelength=1.4)
    save(fig, "fig_mem_decoded.pdf")


if __name__ == "__main__":
    os.makedirs(FIGS, exist_ok=True)
    fig_stim_qec()
    fig_compile_time()
    fig_passtime()
    fig_mem_decoded()
    print("done")
