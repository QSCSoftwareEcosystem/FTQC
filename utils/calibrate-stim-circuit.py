#!/usr/bin/env python3
"""
calibrate-stim-circuit.py

Update the noise instructions (DEPOLARIZE1, DEPOLARIZE2, X_ERROR) of a STIM
circuit using calibration data from an IQM quantum computer.

Qubit mapping: STIM qubit i  <->  IQM qubit "QB{i+1}".

Mapping of calibration values to STIM instructions
--------------------------------------------------
DEPOLARIZE1(p) q      p = 3/2 * prx_error[QB(q+1)]
DEPOLARIZE2(p) a b    p = 5/4 * cz_error[QB(a+1), QB(b+1)]
X_ERROR(p) q          p = (read_error["0"] + read_error["1"]) / 2  of QB(q+1)

Scaling rationale: IQM's prx_error / cz_error are average gate infidelities
r = 1 - F_avg. For a depolarizing channel applying a uniformly random
non-identity Pauli with total probability p, F_avg = 1 - p*d/(d+1), so
    1q (d=2): r = 2p/3   ->  p = 3r/2   (STIM DEPOLARIZE1)
    2q (d=4): r = 4p/5   ->  p = 5r/4   (STIM DEPOLARIZE2)
Use --no-scale to insert the raw errors instead.

Usage:
    python calibrate-stim-circuit.py --input-stim-circuit logical0-HZH_syndrome.stim \
                                     --calibration-data pathfinder_topology.json
"""
import argparse
import json
import re
import sys
from pathlib import Path

# STIM instruction line:  <indent><NAME>[(args)] [targets] [# comment]
LINE_RE = re.compile(
    r"^(?P<indent>\s*)(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?P<args>\([^)]*\))?(?P<targets>[^#\n]*)(?P<comment>#.*)?$"
)

DEPOL1_SCALE = 3.0 / 2.0   # p = 3/2 * r
DEPOL2_SCALE = 5.0 / 4.0   # p = 5/4 * r
# SWAP(a,b)=CNOT(a,b)CNOT(b,a)CNOT(a,b)
SWAP_SCALE = 10.0   # p = SWAP_SCALE * r 


def qname(stim_idx):
    return f"QB{stim_idx + 1}"


def fmt(p):
    return f"{p:.6e}"


class Calibration:
    def __init__(self, path, scale=True, missing_edge="mean"):
        with open(path) as f:
            data = json.load(f)
        self.scale = scale
        self.missing_edge = missing_edge
        self.warned = set()

        qubits = data.get("neighbours") or data.get("qubit_data") or {}
        if not qubits:
            sys.exit("error: no per-qubit data ('neighbours') in calibration file")

        self.prx = {}
        self.read = {}
        for name, q in qubits.items():
            if q.get("prx_error") is not None:
                self.prx[name] = float(q["prx_error"])
            ro = q.get("readout_error") or q.get("read_error")
            if ro and "0" in ro and "1" in ro:
                self.read[name] = (float(ro["0"]) + float(ro["1"])) / 2.0

        self.cz = {}
        for e in data.get("edges", []):
            if e.get("cz_error") is not None:
                self.cz[frozenset(e["qubits"])] = float(e["cz_error"])
        if not self.cz:
            sys.exit("error: no edges with cz_error in calibration file")
        self.cz_mean = sum(self.cz.values()) / len(self.cz)
        self.prx_mean = sum(self.prx.values()) / len(self.prx)
        self.read_mean = sum(self.read.values()) / len(self.read)

    def _warn(self, key, msg):
        if key not in self.warned:
            self.warned.add(key)
            print(f"warning: {msg}", file=sys.stderr)

    def depol1(self, q):
        n = qname(q)
        if n in self.prx:
            r = self.prx[n]
        else:
            self._warn(("p", n), f"no prx_error for {n}; using mean")
            r = self.prx_mean
        return min(r * DEPOL1_SCALE, 0.75) if self.scale else r

    def depol2(self, a, b):
        key = frozenset({qname(a), qname(b)})
        if key in self.cz:
            r = self.cz[key]
        else:
            if self.missing_edge == "error":
                sys.exit(f"error: no cz_error for pair {qname(a)}-{qname(b)}")
            self._warn(key, f"{qname(a)}-{qname(b)} is not a hardware coupler; "
                            f"using mean cz_error times SWAP_SCALE ({self.cz_mean * SWAP_SCALE:.4g})")
            r = self.cz_mean * SWAP_SCALE
        return min(r * DEPOL2_SCALE, 15.0 / 16.0) if self.scale else r

    def xerr(self, q):
        n = qname(q)
        if n in self.read:
            return self.read[n]
        self._warn(("r", n), f"no readout_error for {n}; using mean")
        return self.read_mean


def process_line(line, cal):
    """Return list of output lines for one input line."""
    m = LINE_RE.match(line.rstrip("\n"))
    if not m:
        return [line.rstrip("\n")]
    name = m["name"].upper()
    if name not in ("DEPOLARIZE1", "DEPOLARIZE2", "X_ERROR") or not m["args"]:
        return [line.rstrip("\n")]

    indent = m["indent"]
    comment = f" {m['comment']}" if m["comment"] else ""
    try:
        targets = [int(t) for t in m["targets"].split()]
    except ValueError:
        # qubit targets like rec[-1] / inverted targets: leave untouched
        return [line.rstrip("\n")]

    out = []
    if name == "DEPOLARIZE2":
        if len(targets) % 2:
            sys.exit(f"error: odd number of targets in: {line.strip()}")
        for i in range(0, len(targets), 2):
            a, b = targets[i], targets[i + 1]
            out.append(f"{indent}DEPOLARIZE2({fmt(cal.depol2(a, b))}) {a} {b}")
    else:
        f = cal.depol1 if name == "DEPOLARIZE1" else cal.xerr
        for t in targets:
            out.append(f"{indent}{name}({fmt(f(t))}) {t}")
    if out and comment:
        out[-1] += comment
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input-stim-circuit",
                    dest="circuit", required=True, help="input STIM circuit file")
    ap.add_argument("--calibration-data", required=True,
                    help="IQM calibration data JSON file")
    ap.add_argument("--no-scale", action="store_true",
                    help="do not convert average infidelity to depolarizing probability")
    ap.add_argument("--missing-edge", choices=["mean", "error"], default="mean",
                    help="for CX pairs that are not hardware couplers: use mean "
                         "cz_error (default) or abort")
    args = ap.parse_args()

    src = Path(args.circuit)
    dst = src.with_name(f"{src.stem}-calibrated{src.suffix}")
    cal = Calibration(args.calibration_data, scale=not args.no_scale,
                      missing_edge=args.missing_edge)

    out_lines = []
    with open(src) as f:
        for line in f:
            out_lines.extend(process_line(line, cal))
    dst.write_text("\n".join(out_lines) + "\n")
    print(f"wrote {dst}")


if __name__ == "__main__":
    main()
