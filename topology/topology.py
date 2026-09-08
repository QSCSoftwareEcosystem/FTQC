"""Print / export the qubit connectivity (coupling map / topology) of an IQM backend.

Usage:
    python3 topology.py [backend] [--plot] [--json PATH] [--token KEY]

    backend : garnet (default) | apollo | adonis | deneb     -- offline fake mocks
              pathfinder | default                           -- the REAL ORNL QPU
    --plot  : save a PNG drawing of the qubit graph (<backend>_topology.png)
    --json  : write connectivity to a JSON file (default: <backend>_topology.json)
    --token : API token for real backends (overrides the environment)

Real device
-----------
`pathfinder` (a.k.a. `default`) connects to the live ORNL IQM "Pathfinder" (Radiance) 20-qubit
machine and reads its topology + the latest calibration (quality-metric-set).
This needs a real API token and network access:

    IQM_BASE_URL                        server URL (default: https://qccsw.ccs.ornl.gov)
    IQM_TOKEN / RESONANCE_API_KEY       bearer token (or pass --token)

The token is never hard-coded: supply it via --token or one of those env vars.
The fake backends need no server, URL, or token.
"""
import argparse
import datetime as dt
import json
import os
import re

from iqm.qiskit_iqm.fake_backends.fake_garnet import IQMFakeGarnet
from iqm.qiskit_iqm.fake_backends.fake_apollo import IQMFakeApollo
from iqm.qiskit_iqm.fake_backends.fake_adonis import IQMFakeAdonis
from iqm.qiskit_iqm.fake_backends.fake_deneb import IQMFakeDeneb

FAKE_BACKENDS = {
    "garnet": IQMFakeGarnet,
    "apollo": IQMFakeApollo,
    "adonis": IQMFakeAdonis,
    "deneb": IQMFakeDeneb,
}

# Real-device aliases the user can ask for -> the server's quantum-computer alias.
# "pathfinder" is the ORNL IQM Radiance 20-qubit device; the server publishes it
# under the alias "default".  "crystal" is kept as a legacy alias.
REAL_ALIASES = {
    "pathfinder": "default",
    "crystal": "default",   # legacy alias
    "default": "default",
}

SERVER_URL = os.environ.get("IQM_BASE_URL", "https://qccsw.ccs.ornl.gov")
# Token is never hard-coded: it comes from --token, then IQM_TOKEN /
# RESONANCE_API_KEY. Stays None until a real backend is requested.
ENV_TOKEN = os.environ.get("IQM_TOKEN") or os.environ.get("RESONANCE_API_KEY")


def _qkey(q):
    """Sort key that orders QB2 < QB10 numerically instead of lexically."""
    m = re.match(r"QB(\d+)", str(q))
    return int(m.group(1)) if m else 0


def _stats(values):
    """Mean / median / min / max / count over the non-null numbers in `values`."""
    xs = sorted(v for v in values if isinstance(v, (int, float)))
    if not xs:
        return None
    n = len(xs)
    median = xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2.0
    return {"mean": sum(xs) / n, "median": median, "min": xs[0], "max": xs[-1], "count": n}


# ── Calibration accessors ────────────────────────────────────────────────────
# A uniform interface so connectivity() works for both fake backends (which carry
# an `error_profile`) and the real device (which exposes a quality-metric-set).

class FakeCal:
    """Calibration read from a fake backend's `error_profile`.

    Fake backends carry no Clifford RB, T2-Ramsey, availability or feedback-group
    information, so those accessors return None / empty to keep a uniform interface
    with `RealCal`.
    """

    def __init__(self, backend):
        ep = backend.error_profile
        self.name = ep.name
        self._ep = ep
        self._prx_err = ep.single_qubit_gate_depolarizing_error_parameters.get("prx", {})
        self._cz = ep.two_qubit_gate_depolarizing_error_parameters.get("cz", {})
        # Fakes have no calibration timestamp (static mock data).
        self.timestamp = None
        # No chip-level metadata for fakes: None => "treat every qubit as available".
        self.available_qubits = None
        self.all_qubits = None
        self.feedback_groups = []
        self._feedback_group_of = {}

    def t1_ns(self, q):
        return self._ep.t1s.get(q)

    def t2_echo_ns(self, q):
        return self._ep.t2s.get(q)

    def t2_ramsey_ns(self, q):
        return None

    def t2_ns(self, q):  # backward-compatible alias (echo)
        return self.t2_echo_ns(q)

    def prx_error(self, q):
        return self._prx_err.get(q)

    def prx_fidelity(self, q):
        e = self.prx_error(q)
        return (1.0 - e) if e is not None else None

    def cz_error(self, qa, qb):
        return self._cz.get((qa, qb), self._cz.get((qb, qa)))

    def cz_fidelity(self, qa, qb):
        e = self.cz_error(qa, qb)
        return (1.0 - e) if e is not None else None

    def clifford_cz_fidelity(self, qa, qb):
        return None

    def clifford_cz_error(self, qa, qb):
        return None

    def clifford_1q_fidelity(self, q):
        return None

    def readout_error(self, q):
        return self._ep.readout_errors.get(q)

    def readout_fidelity(self, q):
        return None

    def prx_duration_ns(self):
        return self._ep.single_qubit_gate_durations.get("prx")

    def cz_duration_ns(self):
        return self._ep.two_qubit_gate_durations.get("cz")

    def feedback_group(self, q):
        return None

    def is_available(self, q):
        return True


class RealCal:
    """Calibration parsed from the server's quality-metric-set + architecture.

    Each observation is `{dut_field, value, unit, uncertainty}`; the dut_field
    encodes the metric and the qubit / qubit-pair, e.g.
        characterization.model.QB1.t1_time                  (T1, seconds)
        characterization.model.QB1.t2_time                  (T2 Ramsey, seconds)
        characterization.model.QB1.t2_echo_time             (T2 echo, seconds)
        metrics.rb.prx.drag_crf_sx.QB1.fidelity:par=d2      -> PRX gate fidelity
        metrics.rb.clifford.xy_sx.QB1.fidelity:par=d2       -> 1Q Clifford avg gate fidelity
        metrics.rb.clifford.uz_cz.QB2__QB5.fidelity:par=d2  -> 2Q Clifford avg gate fidelity (per connection)
        metrics.irb.cz.crf_crf.QB2__QB5.fidelity:par=d2     -> CZ gate fidelity
        metrics.ssro.measure.constant.QB1.fidelity          -> single-qubit readout fidelity
        metrics.ssro.measure.constant.QB1.error_0_to_1      (readout 0->1)
        metrics.ssro.measure.constant.QB1.error_1_to_0      (readout 1->0)

    Chip-level metadata is read from the client alongside the metrics:
      - available qubits   : dynamic quantum architecture (currently calibrated/usable)
      - full chip qubits   : static quantum architecture
      - feedback groups    : classical fast-feedback (feed-forward) groups; each group
                             is numbered 1..N and every qubit is tagged with its number
    """

    def __init__(self, client):
        qms = client.get_quality_metric_set()
        self.name = f"{qms.dut_label} @ {qms.created_timestamp:%Y-%m-%d %H:%M UTC}"
        # Machine-parseable ISO-8601 timestamp of the calibration (for time series).
        self.timestamp = qms.created_timestamp.isoformat()
        self._t1, self._t2r, self._t2e = {}, {}, {}
        self._prx_fid, self._cz_fid = {}, {}
        self._clif_cz_fid, self._clif_1q_fid = {}, {}
        self._ro01, self._ro10, self._ro_fid = {}, {}, {}

        for o in qms.observations:
            d = o if isinstance(o, dict) else (
                o.model_dump() if hasattr(o, "model_dump") else o.__dict__)
            field, value = d["dut_field"], d["value"]

            if (m := re.match(r"characterization\.model\.(QB\d+)\.t1_time$", field)):
                self._t1[m.group(1)] = value
            elif (m := re.match(r"characterization\.model\.(QB\d+)\.t2_echo_time$", field)):
                self._t2e[m.group(1)] = value
            elif (m := re.match(r"characterization\.model\.(QB\d+)\.t2_time$", field)):
                self._t2r[m.group(1)] = value
            elif (m := re.match(r"metrics\.rb\.prx\..*?\.(QB\d+)\.fidelity", field)):
                self._prx_fid[m.group(1)] = value
            # 2Q Clifford (pair) must be tested before the 1Q Clifford pattern.
            elif (m := re.match(r"metrics\.rb\.clifford\..*?\.(QB\d+)__(QB\d+)\.fidelity", field)):
                self._clif_cz_fid[frozenset(m.group(1, 2))] = value
            elif (m := re.match(r"metrics\.rb\.clifford\..*?\.(QB\d+)\.fidelity", field)):
                self._clif_1q_fid[m.group(1)] = value
            elif (m := re.match(r"metrics\.irb\.cz\..*?\.(QB\d+)__(QB\d+)\.fidelity", field)):
                self._cz_fid[frozenset(m.group(1, 2))] = value
            elif (m := re.match(r"metrics\.ssro\.measure\.constant\.(QB\d+)\.fidelity$", field)):
                self._ro_fid[m.group(1)] = value
            elif (m := re.match(r"metrics\.ssro\.measure\.constant\.(QB\d+)\.error_0_to_1$", field)):
                self._ro01[m.group(1)] = value
            elif (m := re.match(r"metrics\.ssro\.measure\.constant\.(QB\d+)\.error_1_to_0$", field)):
                self._ro10[m.group(1)] = value

        # ── Chip-level metadata: availability + classical feed-forward groups ──
        self.available_qubits = None  # None => availability unknown; treat all as available
        self.all_qubits = None
        self.feedback_groups = []
        self._feedback_group_of = {}

        try:
            self.available_qubits = set(client.get_dynamic_quantum_architecture().qubits)
        except Exception as e:
            print(f"warning: could not read dynamic architecture (availability): {e}")
        try:
            self.all_qubits = list(client.get_static_quantum_architecture().qubits)
        except Exception as e:
            print(f"warning: could not read static architecture (full chip): {e}")
        try:
            groups = client.get_feedback_groups()
            self.feedback_groups = [sorted(g, key=_qkey) for g in groups]
            for i, group in enumerate(self.feedback_groups, start=1):
                for q in group:
                    self._feedback_group_of[q] = i
        except Exception as e:
            print(f"warning: could not read classical feedback groups: {e}")

    @staticmethod
    def _ns(seconds):
        return seconds * 1e9 if seconds is not None else None

    @staticmethod
    def _err(fidelity):
        return 1.0 - fidelity if fidelity is not None else None

    def t1_ns(self, q):
        return self._ns(self._t1.get(q))

    def t2_ramsey_ns(self, q):
        return self._ns(self._t2r.get(q))

    def t2_echo_ns(self, q):
        return self._ns(self._t2e.get(q))

    def t2_ns(self, q):  # backward-compatible alias (echo)
        return self.t2_echo_ns(q)

    def prx_error(self, q):
        return self._err(self._prx_fid.get(q))

    def prx_fidelity(self, q):
        return self._prx_fid.get(q)

    def cz_error(self, qa, qb):
        return self._err(self._cz_fid.get(frozenset((qa, qb))))

    def cz_fidelity(self, qa, qb):
        return self._cz_fid.get(frozenset((qa, qb)))

    def clifford_cz_fidelity(self, qa, qb):
        return self._clif_cz_fid.get(frozenset((qa, qb)))

    def clifford_cz_error(self, qa, qb):
        return self._err(self.clifford_cz_fidelity(qa, qb))

    def clifford_1q_fidelity(self, q):
        return self._clif_1q_fid.get(q)

    def readout_error(self, q):
        e0, e1 = self._ro01.get(q), self._ro10.get(q)
        if e0 is None and e1 is None:
            return None
        return {"0": e0, "1": e1}

    def readout_fidelity(self, q):
        return self._ro_fid.get(q)

    def prx_duration_ns(self):
        return None  # not exposed by the quality-metric-set

    def cz_duration_ns(self):
        return None

    def feedback_group(self, q):
        return self._feedback_group_of.get(q)

    def is_available(self, q):
        return True if self.available_qubits is None else (q in self.available_qubits)


def summarize(topo: dict) -> dict:
    """Overall calibration summary (mean/median/min/max) over qubits & connections."""
    nb = list(topo["neighbours"].values())
    ed = topo["edges"]
    return {
        "cz_gate_fidelity": _stats([e["cz_fidelity"] for e in ed]),
        "clifford_averaged_gate_fidelity": _stats([e["clifford_cz_fidelity"] for e in ed]),
        "clifford_1q_gate_fidelity": _stats([n["clifford_1q_fidelity"] for n in nb]),
        "prx_gate_fidelity": _stats([n["prx_fidelity"] for n in nb]),
        "single_qubit_readout_fidelity": _stats([n["readout_fidelity"] for n in nb]),
        "t1_ns": _stats([n["t1_ns"] for n in nb]),
        "t2_ramsey_ns": _stats([n["t2_ramsey_ns"] for n in nb]),
        "t2_echo_ns": _stats([n["t2_echo_ns"] for n in nb]),
    }


def connectivity(backend, cal, name=None) -> dict:
    """Build a JSON-serializable description of the backend topology + calibration."""
    cm = backend.coupling_map
    edges_idx = sorted({tuple(sorted(e)) for e in cm.get_edges()})

    edges = []
    for a, b in edges_idx:
        qa, qb = backend.index_to_qubit_name(a), backend.index_to_qubit_name(b)
        edges.append({
            "indices": [a, b],
            "qubits": [qa, qb],
            "cz_error": cal.cz_error(qa, qb),
            "cz_fidelity": cal.cz_fidelity(qa, qb),
            "clifford_cz_error": cal.clifford_cz_error(qa, qb),
            "clifford_cz_fidelity": cal.clifford_cz_fidelity(qa, qb),
        })

    # Backend indices only cover the qubits the backend exposes. Also fold in any
    # full-chip qubits the calibration knows about so unavailable ones still appear.
    backend_names = [backend.index_to_qubit_name(i) for i in range(backend.num_qubits)]
    idx_of = {n: i for i, n in enumerate(backend_names)}
    all_names = sorted(set(backend_names) | set(cal.all_qubits or []), key=_qkey)

    neighbours = {}
    for q in all_names:
        i = idx_of.get(q)
        # A qubit is available only if the calibration says so *and* the backend
        # actually exposes it (has an index / can be targeted).
        available = cal.is_available(q) and (i is not None)
        if i is not None:
            nbrs = sorted({backend.index_to_qubit_name(n)
                           for e in edges_idx for n in e if i in e and n != i}, key=_qkey)
        else:
            nbrs = []
        neighbours[q] = {
            "index": i,
            "available": available,
            "feedback_group": cal.feedback_group(q),
            "degree": len(nbrs),
            "neighbours": nbrs,
            "t1_ns": cal.t1_ns(q),
            "t2_ramsey_ns": cal.t2_ramsey_ns(q),
            "t2_echo_ns": cal.t2_echo_ns(q),
            "prx_error": cal.prx_error(q),
            "prx_fidelity": cal.prx_fidelity(q),
            "clifford_1q_fidelity": cal.clifford_1q_fidelity(q),
            "readout_error": cal.readout_error(q),
            "readout_fidelity": cal.readout_fidelity(q),
        }

    available_qubits = sorted((q for q, n in neighbours.items() if n["available"]), key=_qkey)
    unavailable_qubits = sorted((q for q, n in neighbours.items() if not n["available"]), key=_qkey)

    topo = {
        "backend": name or backend.name,
        "calibration": cal.name,
        "calibration_timestamp": getattr(cal, "timestamp", None),
        "num_qubits": backend.num_qubits,
        "qubits": list(backend.architecture.qubits),
        "available_qubits": available_qubits,
        "unavailable_qubits": unavailable_qubits,
        "feedback_groups": cal.feedback_groups,
        "native_gates": list(backend.operation_names),
        "gate_durations_ns": {
            "prx": cal.prx_duration_ns(),
            "cz": cal.cz_duration_ns(),
        },
        "num_edges": len(edges),
        "edges": edges,
        "neighbours": neighbours,
    }
    topo["summary"] = summarize(topo)
    return topo


def build_backend(key, token=None):
    """Return (backend, calibration) for a fake or real backend key."""
    if key in FAKE_BACKENDS:
        backend = FAKE_BACKENDS[key]()
        return backend, FakeCal(backend)

    # Real device: connect to the server and read its architecture + calibration.
    if not token:
        raise SystemExit(
            f"error: backend '{key}' is a real device and needs an API token.\n"
            "       Pass --token <KEY>, or set IQM_TOKEN / RESONANCE_API_KEY.")

    from iqm.qiskit_iqm import IQMProvider
    from iqm.iqm_client import IQMClient

    # iqm-client rejects a token passed both as an arg and via $IQM_TOKEN
    # ("sources must not be mixed"). We always pass it explicitly, so drop the
    # env copy first to leave a single source.
    os.environ.pop("IQM_TOKEN", None)

    alias = REAL_ALIASES[key]
    print(f"Connecting to {SERVER_URL} (alias '{alias}') ...")
    backend = IQMProvider(SERVER_URL, token=token, quantum_computer=alias).get_backend()
    client = IQMClient(SERVER_URL, token=token, quantum_computer=alias)
    return backend, RealCal(client)


def _num(v, fmt):
    """Format a possibly-None number for display."""
    return format(v, fmt) if isinstance(v, (int, float)) else str(v)


def print_topology(topo: dict) -> None:
    print(f"Backend: {topo['backend']}")
    print(f"Qubits : {topo['num_qubits']}  ({', '.join(topo['qubits'])})")
    print(f"Native : {topo['native_gates']}")
    if topo.get("unavailable_qubits"):
        print(f"Unavailable qubits: {', '.join(topo['unavailable_qubits'])}")
    for i, group in enumerate(topo.get("feedback_groups") or [], start=1):
        print(f"Feedback group {i}: {', '.join(group)}")

    d = topo["gate_durations_ns"]
    print(f"Times  : prx {d['prx']} ns, cz {d['cz']} ns   (calibration: {topo['calibration']})")

    print(f"\nCoupling map ({topo['num_edges']} undirected edges):")
    for e in topo["edges"]:
        a, b = e["qubits"]
        print(f"  {a:<5} -- {b:<5}   {str(tuple(e['indices'])):<9} "
              f"cz_err={_num(e['cz_error'], '.4e')}  "
              f"clifford_err={_num(e['clifford_cz_error'], '.4e')}")

    print("\nPer-qubit calibration:")
    print(f"  {'qubit':<6}{'idx':<5}{'av':<4}{'grp':<5}{'deg':<5}"
          f"{'T1(ns)':<13}{'T2Ram(ns)':<13}{'T2Echo(ns)':<13}"
          f"{'prx_err':<12}{'ro_fid':<10}readout_err(0/1)")
    for q, info in topo["neighbours"].items():
        ro = info["readout_error"] or {}
        print(f"  {q:<6}{_num(info['index'], 'd'):<5}"
              f"{('Y' if info['available'] else 'n'):<4}"
              f"{_num(info['feedback_group'], 'd'):<5}{info['degree']:<5}"
              f"{_num(info['t1_ns'], ',.0f'):<13}"
              f"{_num(info['t2_ramsey_ns'], ',.0f'):<13}{_num(info['t2_echo_ns'], ',.0f'):<13}"
              f"{_num(info['prx_error'], '.3e'):<12}{_num(info['readout_fidelity'], '.4f'):<10}"
              f"{_num(ro.get('0'), '.4f')}/{_num(ro.get('1'), '.4f')}")

    summary = topo.get("summary") or {}
    print("\nSummary report (mean over qubits / connections):")

    def _line(label, key, fmt):
        st = summary.get(key)
        if st:
            print(f"  {label:<36}{format(st['mean'], fmt):<14}"
                  f"(min {format(st['min'], fmt)}, max {format(st['max'], fmt)}, n={st['count']})")

    _line("CZ gate fidelity", "cz_gate_fidelity", ".5f")
    _line("Clifford avg gate fidelity (2Q)", "clifford_averaged_gate_fidelity", ".5f")
    _line("Clifford avg gate fidelity (1Q)", "clifford_1q_gate_fidelity", ".5f")
    _line("PRX gate fidelity", "prx_gate_fidelity", ".5f")
    _line("Single-qubit readout fidelity", "single_qubit_readout_fidelity", ".5f")
    _line("T1 time (ns)", "t1_ns", ",.0f")
    _line("T2 time Ramsey (ns)", "t2_ramsey_ns", ",.0f")
    _line("T2 time echo (ns)", "t2_echo_ns", ",.0f")


def save_plot(backend, path: str) -> None:
    from qiskit.visualization import plot_coupling_map

    fig = plot_coupling_map(
        backend.num_qubits,
        None,
        backend.coupling_map.get_edges(),
        qubit_labels=list(backend.architecture.qubits),
    )
    fig.savefig(path, bbox_inches="tight", dpi=150)
    print(f"\nSaved plot -> {path}")


def main() -> None:
    p = argparse.ArgumentParser(description="IQM backend topology")
    p.add_argument("backend", nargs="?", default="garnet",
                   choices=list(FAKE_BACKENDS) + list(REAL_ALIASES))
    p.add_argument("--plot", action="store_true", help="save a PNG of the qubit graph")
    p.add_argument("--json", nargs="?", const="", default=None,
                   help="write connectivity JSON (optional PATH; default <backend>_topology.json)")
    p.add_argument("--snapshot-dir", default=None,
                   help="also write a timestamped snapshot 'topology-<UTC>.json' into "
                        "this directory (consumed by the Django dashboard)")
    p.add_argument("--token", default=ENV_TOKEN,
                   help="API token for real backends (default: $IQM_TOKEN / $RESONANCE_API_KEY)")
    args = p.parse_args()

    backend, cal = build_backend(args.backend, token=args.token)
    topo = connectivity(backend, cal, name=args.backend)
    # Wall-clock capture time — each run is a distinct point on the dashboard's
    # time axis, even when the underlying calibration set has not changed.
    now = dt.datetime.now(dt.timezone.utc)
    topo["generated_at"] = now.isoformat()
    print_topology(topo)

    # Always emit a JSON file; honour an explicit path if given.
    json_path = args.json if args.json else f"{args.backend}_topology.json"
    with open(json_path, "w") as f:
        json.dump(topo, f, indent=2)
    print(f"\nSaved connectivity -> {json_path}")

    # Timestamped snapshot for the time-variant dashboard (topology-*.json).
    if args.snapshot_dir is not None:
        os.makedirs(args.snapshot_dir, exist_ok=True)
        stamp = now.strftime("%Y%m%dT%H%M%S_%f")
        snap_path = os.path.join(args.snapshot_dir, f"topology-{stamp}.json")
        with open(snap_path, "w") as f:
            json.dump(topo, f, indent=2)
        print(f"Saved snapshot   -> {snap_path}")

    if args.plot:
        from qiskit.visualization import plot_coupling_map
        plot_coupling_map(backend.num_qubits, None, backend.coupling_map.get_edges()).savefig(f"{args.backend}_topology.png")
        #save_plot(backend, f"{args.backend}_topology.png")


if __name__ == "__main__":
    main()
