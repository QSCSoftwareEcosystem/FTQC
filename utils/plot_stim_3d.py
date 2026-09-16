#!/usr/bin/env python3
"""plot_stim_3d.py — 3D visualisation of STIM QEC simulation results.

Generates three complementary 3D figures:

  Fig 1  Waterfall surface  —  p_shot vs (log p, benchmark)
         One ribbon per benchmark showing the error-rate curve as a surface.

  Fig 2  Threshold landscape —  p_shot vs (log p, n_phys)
         Scatter + surface coloured by p_shot; threshold plane p_shot = p overlaid.

  Fig 3  Compiler-driven comparison  —  p_shot vs (log p, benchmark)
         Side-by-side ribbons: Python-built (sim_stim) vs ftqc-opt-driven
         (sim_stim_ftqc_loop), for benchmarks where both are valid.

Usage
-----
    qasm.venv/bin/python plot_stim_3d.py [--show] [--outdir figures/]
"""

import argparse, os, math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import cm
from mpl_toolkits.mplot3d import Axes3D          # noqa: F401
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
import csv

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PYTHON  = os.path.join(_DIR, "stim_results.csv")         # sim_stim.py output
CSV_FTQC    = os.path.join(_DIR, "stim_ftqc_results.csv")    # sim_stim_ftqc_loop.py output
OUTDIR      = os.path.join(_DIR, "figures")

BENCH_ORDER  = ["Bell", "GHZ", "Grover", "QFT"]
BENCH_COLORS = {"Bell": "#2196F3", "GHZ": "#4CAF50",
                "Grover": "#FF9800", "QFT": "#E91E63"}
BENCH_YPOS   = {b: i for i, b in enumerate(BENCH_ORDER)}   # y-axis position

# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------

def load_csv(path, p_col="p", pL_col="p_L"):
    """Return {benchmark: (p_array, pL_array)} from a CSV file."""
    data = {}
    if not os.path.exists(path):
        return data
    with open(path) as f:
        for row in csv.DictReader(f):
            bench = row["benchmark"]
            p  = float(row[p_col])
            pL_str = row.get(pL_col, "nan")
            pL = float(pL_str) if pL_str not in ("", "None", "nan") else float("nan")
            data.setdefault(bench, ([], []))
            data[bench][0].append(p)
            data[bench][1].append(pL)
    return {b: (np.array(ps), np.array(pLs)) for b, (ps, pLs) in data.items()}


# ---------------------------------------------------------------------------
# Fig 1: Waterfall ribbons — p_shot vs (log p, benchmark)
# ---------------------------------------------------------------------------

def fig_waterfall(data_py: dict, outdir: str, show: bool):
    fig = plt.figure(figsize=(12, 7))
    ax  = fig.add_subplot(111, projection="3d")

    for bench in BENCH_ORDER:
        if bench not in data_py:
            continue
        ps, pLs = data_py[bench]
        valid = ~np.isnan(pLs) & (pLs > 0) & (ps > 0)
        if valid.sum() < 2:
            continue
        lp  = np.log10(ps[valid])
        pL  = pLs[valid]
        ypos = BENCH_YPOS[bench]
        color = BENCH_COLORS[bench]

        # Ribbon: two rows at y=ypos and y=ypos+0.8
        xs = np.concatenate([lp, lp[::-1]])
        ys = np.full_like(xs, ypos)
        ys[len(lp):] = ypos + 0.7
        zs_bot = np.zeros_like(lp)
        zs_top = pL

        verts = []
        for i in range(len(lp) - 1):
            verts.append([
                (lp[i],   ypos,       pL[i]),
                (lp[i+1], ypos,       pL[i+1]),
                (lp[i+1], ypos + 0.7, pL[i+1]),
                (lp[i],   ypos + 0.7, pL[i]),
            ])
        poly = Poly3DCollection(verts, alpha=0.55, facecolor=color, edgecolor="none")
        ax.add_collection3d(poly)

        # Top edge line
        ax.plot(lp, np.full_like(lp, ypos + 0.35), pL,
                color=color, lw=2, label=bench)

    # Threshold plane  p_shot = p  (diagonal reference)
    lp_range = np.linspace(-4, -1.82, 60)
    pref     = 10 ** lp_range
    Y_plane  = np.array([0, len(BENCH_ORDER) - 0.3])
    LP_grid, Y_grid = np.meshgrid(lp_range, Y_plane)
    P_grid = 10 ** LP_grid
    ax.plot_surface(LP_grid, Y_grid, P_grid,
                    alpha=0.12, color="gray", edgecolor="none")
    ax.plot(lp_range, np.full_like(lp_range, -0.1), pref,
            "k--", lw=1, alpha=0.5, label="$p_{shot}=p$ (threshold)")

    ax.set_xlabel("log₁₀(p)", labelpad=8)
    ax.set_ylabel("Benchmark", labelpad=8)
    ax.set_zlabel("$p_{shot}$", labelpad=8)
    ax.set_yticks(list(BENCH_YPOS.values()))
    ax.set_yticklabels(list(BENCH_YPOS.keys()), fontsize=9)
    ax.set_zlim(0, 0.6)
    ax.set_title("STIM QEC — Per-Shot Failure Rate\nWaterfall by Benchmark",
                 fontsize=12, pad=15)
    ax.legend(loc="upper left", fontsize=8, framealpha=0.7)
    ax.view_init(elev=22, azim=-55)

    _save(fig, outdir, "stim_3d_waterfall", show)


# ---------------------------------------------------------------------------
# Fig 2: Threshold landscape — p_shot vs (log p, n_phys)
# ---------------------------------------------------------------------------

def fig_landscape(data_py: dict, outdir: str, show: bool):
    fig = plt.figure(figsize=(12, 7))
    ax  = fig.add_subplot(111, projection="3d")

    n_phys_map = {"Bell": 52, "GHZ": 52, "Grover": 26, "QFT": 39}

    all_lp, all_np, all_pL, all_col = [], [], [], []
    for bench in BENCH_ORDER:
        if bench not in data_py:
            continue
        ps, pLs = data_py[bench]
        valid = ~np.isnan(pLs) & (pLs > 0) & (ps > 0)
        if valid.sum() < 2:
            continue
        lp  = np.log10(ps[valid])
        pL  = pLs[valid]
        np_ = np.full_like(lp, n_phys_map.get(bench, 26))
        col = BENCH_COLORS[bench]

        ax.plot(lp, np_, pL, color=col, lw=2.5, label=bench, zorder=5)
        ax.scatter(lp, np_, pL, color=col, s=25, zorder=6)

        all_lp.extend(lp.tolist())
        all_np.extend(np_.tolist())
        all_pL.extend(pL.tolist())

    # Threshold plane  p_shot = p
    lp_range = np.linspace(-4, -1.82, 50)
    np_range = np.linspace(20, 60, 10)
    LP_g, NP_g = np.meshgrid(lp_range, np_range)
    PL_g = 10 ** LP_g
    ax.plot_surface(LP_g, NP_g, PL_g,
                    alpha=0.12, color="gray", edgecolor="none")

    ax.set_xlabel("log₁₀(p)", labelpad=8)
    ax.set_ylabel("Physical qubits $n_{phys}$", labelpad=8)
    ax.set_zlabel("$p_{shot}$", labelpad=8)
    ax.set_zlim(0, 0.6)
    ax.set_title("STIM QEC — Error Rate vs Circuit Size\nGray plane: $p_{shot}=p$ threshold",
                 fontsize=12, pad=15)
    ax.legend(loc="upper left", fontsize=9, framealpha=0.7)
    ax.view_init(elev=25, azim=-60)

    _save(fig, outdir, "stim_3d_landscape", show)


# ---------------------------------------------------------------------------
# Fig 3: Compiler-loop comparison ribbons
# ---------------------------------------------------------------------------

def fig_comparison(data_py: dict, data_ftqc: dict, outdir: str, show: bool):
    fig = plt.figure(figsize=(14, 7))
    ax  = fig.add_subplot(111, projection="3d")

    # Only Grover has valid p_shot in both (Bell/GHZ saturate in ftqc loop;
    # QFT has 0 observables).  We plot all benchmarks and let the viewer see
    # the difference.
    ybase = 0
    yticks, ylabels = [], []

    for bench in BENCH_ORDER:
        color = BENCH_COLORS[bench]
        ybase_bench = ybase

        def _ribbon(ps, pLs, y0, y1, alpha, ls, label):
            valid = ~np.isnan(pLs) & (pLs > 0) & (ps > 0)
            if valid.sum() < 2:
                return
            lp = np.log10(ps[valid])
            pL = pLs[valid]
            ymid = (y0 + y1) / 2
            ax.plot(lp, np.full_like(lp, ymid), pL,
                    color=color, lw=2, linestyle=ls, label=label)
            verts = []
            for i in range(len(lp) - 1):
                verts.append([
                    (lp[i],   y0, pL[i]),
                    (lp[i+1], y0, pL[i+1]),
                    (lp[i+1], y1, pL[i+1]),
                    (lp[i],   y1, pL[i]),
                ])
            poly = Poly3DCollection(verts, alpha=alpha,
                                    facecolor=color, edgecolor="none")
            ax.add_collection3d(poly)

        if bench in data_py:
            ps, pLs = data_py[bench]
            _ribbon(ps, pLs, ybase, ybase + 0.45, 0.5, "-",
                    f"{bench} Python-built")

        if bench in data_ftqc:
            ps, pLs = data_ftqc[bench]
            _ribbon(ps, pLs, ybase + 0.55, ybase + 1.0, 0.3, "--",
                    f"{bench} ftqc-opt")

        ytick = ybase + 0.5
        yticks.append(ytick)
        ylabels.append(bench)
        ybase += 1.6

    # Legend proxies
    from matplotlib.lines import Line2D
    proxies = [
        Line2D([0], [0], color="gray",  lw=2, ls="-",  label="Python-built (sim_stim)"),
        Line2D([0], [0], color="gray",  lw=2, ls="--", label="ftqc-opt driven (sim_stim_ftqc_loop)"),
        Line2D([0], [0], color=BENCH_COLORS["Bell"],   lw=3, label="Bell"),
        Line2D([0], [0], color=BENCH_COLORS["GHZ"],    lw=3, label="GHZ"),
        Line2D([0], [0], color=BENCH_COLORS["Grover"], lw=3, label="Grover"),
        Line2D([0], [0], color=BENCH_COLORS["QFT"],    lw=3, label="QFT"),
    ]
    ax.legend(handles=proxies, loc="upper left", fontsize=7.5, framealpha=0.8)

    ax.set_xlabel("log₁₀(p)", labelpad=8)
    ax.set_ylabel("Benchmark", labelpad=8)
    ax.set_zlabel("$p_{shot}$", labelpad=8)
    ax.set_yticks(yticks)
    ax.set_yticklabels(ylabels, fontsize=9)
    ax.set_zlim(0, 0.65)
    ax.set_title("STIM QEC — Python-Built vs FTQC-Compiler-Driven\n"
                 "Solid: sim_stim.py  |  Dashed: sim_stim_ftqc_loop.py",
                 fontsize=11, pad=15)
    ax.view_init(elev=20, azim=-50)

    _save(fig, outdir, "stim_3d_comparison", show)


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------

def _save(fig, outdir, name, show):
    os.makedirs(outdir, exist_ok=True)
    pdf = os.path.join(outdir, f"{name}.pdf")
    png = os.path.join(outdir, f"{name}.png")
    fig.savefig(pdf, bbox_inches="tight", dpi=150)
    fig.savefig(png, bbox_inches="tight", dpi=150)
    print(f"  Saved {pdf}")
    print(f"  Saved {png}")
    if show:
        plt.show()
    plt.close(fig)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--show",   action="store_true",
                        help="Open interactive matplotlib window")
    parser.add_argument("--outdir", default=OUTDIR,
                        help="Output directory for PDF/PNG files")
    parser.add_argument("--csv-python", default=CSV_PYTHON,
                        help="CSV from sim_stim.py")
    parser.add_argument("--csv-ftqc",   default=CSV_FTQC,
                        help="CSV from sim_stim_ftqc_loop.py")
    args = parser.parse_args()

    os.chdir(_DIR)

    print(f"\nLoading Python-built results : {args.csv_python}")
    data_py   = load_csv(args.csv_python, p_col="p", pL_col="p_L")
    print(f"  Benchmarks: {list(data_py.keys())}")

    print(f"Loading FTQC-driven results  : {args.csv_ftqc}")
    data_ftqc = load_csv(args.csv_ftqc,   p_col="p", pL_col="p_shot")
    print(f"  Benchmarks: {list(data_ftqc.keys())}\n")

    print("Generating Fig 1: Waterfall surface ...")
    fig_waterfall(data_py, args.outdir, args.show)

    print("Generating Fig 2: Threshold landscape ...")
    fig_landscape(data_py, args.outdir, args.show)

    print("Generating Fig 3: Compiler comparison ribbons ...")
    fig_comparison(data_py, data_ftqc, args.outdir, args.show)

    print("\nDone.")


if __name__ == "__main__":
    main()
