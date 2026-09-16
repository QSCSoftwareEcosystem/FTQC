#!/usr/bin/env python3
"""gen_fig_pipeline.py -- generate the FTQC pipeline overview figure (Fig. 1).

Emits BOTH an editable draw.io source and a vector PDF from one node layout so
they never drift:
  ftqc.paper/figs/ftqc.drawio      (open/edit in draw.io / diagrams.net)
  ftqc.paper/figs/ftqc.drawio.pdf  (included by dialects.tex)

Two dialect levels are shown as labeled bands:
  Level 1 -- logical IR (ftqc.*): encoding, syndrome insertion, transversal
             decomposition, magic-state distillation, Pauli-frame optimization;
             analysis passes; and the direct lowerings to OpenQASM 3.0, QIR,
             Stim, and IQM JSON.
  Level 2 -- physical IR (ftqc.qphys.*): Expand-Physical bridges L1->L2, then
             QPhys init-layout and the physical lowerings QPhys->IQM JSON and
             QPhys->QASM3.

The PDF is rendered with matplotlib.  Usage:  python3 utils/gen_fig_pipeline.py
"""
import os, html

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIGS = os.path.join(ROOT, "ftqc.paper", "figs")

BLUE_FILL, BLUE_LINE   = "#dae8fc", "#6c8ebf"   # Level-1 transformation passes
GREEN_FILL, GREEN_LINE = "#d5e8d4", "#82b366"   # Level-2 (physical) passes
WHITE_FILL, DARK_LINE  = "#ffffff", "#333333"   # analysis / lowering / io

W, H = 890, 560

# labeled level bands: (label, x, y, w, h, fill, edge, textcolor)
BANDS = [
 ("Level 1  ·  logical IR  (ftqc.*)",
   6, 44, W-12, 244, "#f3f7fd", "#c7d8ef", "#274b7a"),
 ("Level 2  ·  physical IR  (ftqc.qphys.*)",
   6, 300, W-12, 250, "#eef7ee", "#c9e2c6", "#3d6b39"),
]

# id -> (label, x, y, w, h, kind)   kind: proc|phys|plain|doc
N = {
 "input": ("Input MLIR\nLogical Circuit",              18, 150, 122, 64, "doc"),
 # ---- Level 1: transformational lane ----
 "enc":   ("Encoding\n(Steane / Surface)",            196,  92, 130, 48, "proc"),
 "syn":   ("Syndrome\nInsertion",                     344,  92, 112, 48, "proc"),
 "trv":   ("Transversal gate\ndecomposition",         474,  92, 138, 48, "proc"),
 "mag":   ("Magic State\nDistillation",               344, 172, 112, 48, "proc"),
 "pf":    ("Pauli-frame\noptimization",               474, 172, 138, 48, "proc"),
 # ---- Level 1: analysis lane ----
 "fp":    ("Fault-Path\nanalysis",                    726,  90, 120, 44, "plain"),
 "th":    ("Threshold\nanalysis",                     726, 154, 120, 44, "plain"),
 "re":    ("Resource\nEstimation",                    690, 218, 116, 44, "plain"),
 # ---- Level 2: physical passes ----
 "exp":   ("Expand-Physical\n(logical → physical)",  196, 402, 150, 48, "phys"),
 "qpi":   ("QPhys\nInit-Layout",                      388, 402, 128, 48, "phys"),
 "qpq":   ("QPhys →\nQASM3",                         566, 322, 132, 42, "phys"),
 "oq2":   ("QASM3\n(physical)",                       740, 322, 138, 42, "doc"),
 "qpr":   ("QPhys →\nQIR",                           566, 376, 132, 42, "phys"),
 "or2":   ("QIR\n(physical)",                         740, 376, 138, 42, "doc"),
 "qps":   ("QPhys →\nStim",                          566, 430, 132, 42, "phys"),
 "os2":   ("Stim + detectors\n(physical QEC)",        740, 430, 138, 42, "doc"),
 "qpj":   ("QPhys →\nIQM JSON",                      566, 484, 132, 42, "phys"),
 "oj2":   ("IQM JSON\n(device-mapped)",               740, 484, 138, 42, "doc"),
}
# (src, dst, style)  style: s=straight, r=orthogonal elbow
E = [("input","enc","s"), ("enc","syn","s"), ("syn","trv","s"),
     ("trv","mag","r"), ("mag","pf","s"),
     ("pf","exp","r"),                                   # Level 1 -> Level 2 bridge
     ("exp","qpi","s"),
     ("qpi","qpq","s"), ("qpq","oq2","s"),
     ("qpi","qpr","s"), ("qpr","or2","s"),
     ("qpi","qps","s"), ("qps","os2","s"),
     ("qpi","qpj","s"), ("qpj","oj2","s")]

SEP_X = [178, 700]                       # lane dividers (within Level-1 band)
SEP_Y = (60, 284)
TITLES = [("Transformational & Optimization passes", 180, 700),
          ("Analysis Passes",                        700, 872)]


def _waypoints(a, b, st):
    ax, ay, aw, ah = N[a][1:5]
    bx, by, bw, bh = N[b][1:5]
    if st == "s":
        if bx >= ax + aw:
            return [(ax+aw, ay+ah/2), (bx, by+bh/2)]
        return [(ax, ay+ah/2), (bx+bw, by+bh/2)]
    x1, y1 = ax+aw/2, ay+ah
    x2, y2 = bx+bw/2, by
    my = (y1 + y2) / 2
    return [(x1, y1), (x1, my), (x2, my), (x2, y2)]


def build_pdf(path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import FancyBboxPatch, PathPatch, FancyArrowPatch
    from matplotlib.path import Path

    fig, ax = plt.subplots(figsize=(W/100, H/100))
    ax.set_xlim(0, W); ax.set_ylim(H, 0); ax.axis("off")
    fig.subplots_adjust(left=0, right=1, top=1, bottom=0)

    # level bands (behind everything)
    for label, x, y, w, h, fill, edge, tc in BANDS:
        ax.add_patch(FancyBboxPatch((x+3, y+3), w-6, h-6,
                     boxstyle="round,pad=3,rounding_size=10",
                     fc=fill, ec=edge, lw=1.2, zorder=0.5))
        ax.text(x+14, y+15, label, ha="left", va="center", fontsize=10.5,
                fontweight="bold", color=tc, zorder=5)

    # lane dividers + lane titles (Level 1)
    for x in SEP_X:
        ax.plot([x, x], [SEP_Y[0], SEP_Y[1]], ls=(0, (5, 4)), lw=1.0,
                color="#9bb3d1", zorder=1)
    for label, x1, x2 in TITLES:
        ax.text((x1+x2)/2, 74, label, ha="center", va="center",
                fontsize=9.5, fontweight="bold", color="#274b7a")

    # edges
    for a, b, st in E:
        pts = _waypoints(a, b, st)
        for i in range(len(pts)-2):
            (xa, ya), (xb, yb) = pts[i], pts[i+1]
            ax.plot([xa, xb], [ya, yb], color="#333333", lw=1.3, zorder=2)
        ax.add_patch(FancyArrowPatch(pts[-2], pts[-1], arrowstyle="-|>",
                     mutation_scale=11, lw=1.3, color="#333333", zorder=2))

    # nodes
    for label, x, y, w, h, kind in N.values():
        if kind == "doc":
            wob = 8
            verts = [(x, y), (x+w, y), (x+w, y+h-wob),
                     (x+0.75*w, y+h+wob), (x+0.5*w, y+h-wob),
                     (x+0.25*w, y+h-2.4*wob), (x, y+h-wob), (x, y)]
            codes = [Path.MOVETO, Path.LINETO, Path.LINETO,
                     Path.CURVE3, Path.CURVE3, Path.CURVE3, Path.CURVE3, Path.CLOSEPOLY]
            ax.add_patch(PathPatch(Path(verts, codes), fc=WHITE_FILL,
                                   ec=DARK_LINE, lw=1.3, zorder=3))
        else:
            fc, ec = {"proc": (BLUE_FILL, BLUE_LINE),
                      "phys": (GREEN_FILL, GREEN_LINE),
                      "plain": (WHITE_FILL, DARK_LINE)}[kind]
            ax.add_patch(FancyBboxPatch((x+4, y+4), w-8, h-8,
                         boxstyle="round,pad=4,rounding_size=8",
                         fc=fc, ec=ec, lw=1.3, zorder=3))
        ax.text(x+w/2, y+h/2, label.replace("->", "→"), ha="center",
                va="center", fontsize=9.0, color="#111111", zorder=4,
                linespacing=1.12)

    fig.savefig(path, format="pdf", metadata={"CreationDate": None})
    plt.close(fig)


def _esc(s): return html.escape(s, quote=True)


def build_drawio(path):
    def style(kind):
        return {"doc":  "shape=document;whiteSpace=wrap;html=1;boundedLbl=1;fillColor=#ffffff;strokeColor=#333333;",
                "proc": f"rounded=1;whiteSpace=wrap;html=1;fillColor={BLUE_FILL};strokeColor={BLUE_LINE};",
                "phys": f"rounded=1;whiteSpace=wrap;html=1;fillColor={GREEN_FILL};strokeColor={GREEN_LINE};",
                "plain":"rounded=1;whiteSpace=wrap;html=1;fillColor=#ffffff;strokeColor=#333333;"}[kind]
    c = ['<mxCell id="0"/>', '<mxCell id="1" parent="0"/>']
    for i,(label,x,y,w,h,fill,edge,tc) in enumerate(BANDS):
        c.append(f'<mxCell id="band{i}" value="{_esc(label)}" '
                 f'style="rounded=1;whiteSpace=wrap;html=1;fillColor={fill};'
                 f'strokeColor={edge};verticalAlign=top;align=left;spacingLeft=10;'
                 f'spacingTop=6;fontStyle=1;fontColor={tc};fontSize=13;" '
                 f'vertex="1" parent="1"><mxGeometry x="{x}" y="{y}" width="{w}" '
                 f'height="{h}" as="geometry"/></mxCell>')
    for x in SEP_X:
        c.append(f'<mxCell id="sep{x}" style="endArrow=none;dashed=1;strokeColor=#9bb3d1;" '
                 f'edge="1" parent="1"><mxGeometry relative="1" as="geometry">'
                 f'<mxPoint x="{x}" y="{SEP_Y[0]}" as="sourcePoint"/>'
                 f'<mxPoint x="{x}" y="{SEP_Y[1]}" as="targetPoint"/></mxGeometry></mxCell>')
    for i,(label,x1,x2) in enumerate(TITLES):
        c.append(f'<mxCell id="ttl{i}" value="{_esc(label)}" '
                 f'style="text;html=1;align=center;fontSize=12;fontStyle=1;fontColor=#274b7a;" '
                 f'vertex="1" parent="1"><mxGeometry x="{(x1+x2)/2-90:.0f}" y="62" '
                 f'width="180" height="22" as="geometry"/></mxCell>')
    for nid,(label,x,y,w,h,kind) in N.items():
        c.append(f'<mxCell id="{nid}" value="{_esc(label)}" style="{style(kind)}" '
                 f'vertex="1" parent="1"><mxGeometry x="{x}" y="{y}" width="{w}" '
                 f'height="{h}" as="geometry"/></mxCell>')
    for a,b,st in E:
        es = "edgeStyle=orthogonalEdgeStyle;rounded=0;html=1;" if st=="r" else "html=1;rounded=0;"
        c.append(f'<mxCell id="e_{a}_{b}" style="{es}" edge="1" parent="1" '
                 f'source="{a}" target="{b}"><mxGeometry relative="1" as="geometry"/></mxCell>')
    body = "\n        ".join(c)
    xml = (f'<mxfile host="app.diagrams.net"><diagram name="FTQC pipeline">'
           f'<mxGraphModel dx="900" dy="540" grid="0" gridSize="10" guides="1" '
           f'tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" '
           f'pageWidth="{W}" pageHeight="{H}" math="0" shadow="0"><root>\n        '
           f'{body}\n      </root></mxGraphModel></diagram></mxfile>')
    with open(path, "w") as f:
        f.write(xml)


def main():
    os.makedirs(FIGS, exist_ok=True)
    build_pdf(os.path.join(FIGS, "ftqc.drawio.pdf"))
    build_drawio(os.path.join(FIGS, "ftqc.drawio"))
    print("wrote ftqc.paper/figs/ftqc.drawio.pdf and ftqc.paper/figs/ftqc.drawio")


if __name__ == "__main__":
    main()
