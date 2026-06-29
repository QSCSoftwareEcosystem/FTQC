"""Print / export the qubit connectivity (coupling map / topology) of an IQM backend.

Usage:
    python3 topology.py [backend] [--plot] [--json PATH] [--token KEY]

    backend : garnet (default) | apollo | adonis | deneb     -- offline fake mocks
              crystal | default                              -- the REAL ORNL QPU
    --plot  : save a PNG drawing of the qubit graph (<backend>_topology.png)
    --json  : write connectivity to a JSON file (default: <backend>_topology.json)
    --token : API token for real backends (overrides the environment)

Real device
-----------
`crystal` (a.k.a. `default`) connects to the live ORNL IQM "Crystal" 20-qubit
machine and reads its topology + the latest calibration (quality-metric-set).
This needs a real API token and network access:

    IQM_BASE_URL                        server URL (default: https://qccsw.ccs.ornl.gov)
    IQM_TOKEN / RESONANCE_API_KEY       bearer token (or pass --token)

The token is never hard-coded: supply it via --token or one of those env vars.
The fake backends need no server, URL, or token.
"""
import argparse
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
# "crystal" is IQM's name for this architecture family; on the ORNL server the
# single device is published under the alias "default".
REAL_ALIASES = {
    "crystal": "default",
    "default": "default",
}

SERVER_URL = os.environ.get("IQM_BASE_URL", "https://qccsw.ccs.ornl.gov")
# Token is never hard-coded: it comes from --token, then IQM_TOKEN /
# RESONANCE_API_KEY. Stays None until a real backend is requested.
ENV_TOKEN = os.environ.get("IQM_TOKEN") or os.environ.get("RESONANCE_API_KEY")


# ── Calibration accessors ────────────────────────────────────────────────────
# A uniform interface so connectivity() works for both fake backends (which carry
# an `error_profile`) and the real device (which exposes a quality-metric-set).

class FakeCal:
    """Calibration read from a fake backend's `error_profile`."""

    def __init__(self, backend):
        ep = backend.error_profile
        self.name = ep.name
        self._ep = ep
        self._prx_err = ep.single_qubit_gate_depolarizing_error_parameters.get("prx", {})
        self._cz = ep.two_qubit_gate_depolarizing_error_parameters.get("cz", {})

    def t1_ns(self, q):
        return self._ep.t1s.get(q)

    def t2_ns(self, q):
        return self._ep.t2s.get(q)

    def prx_error(self, q):
        return self._prx_err.get(q)

    def cz_error(self, qa, qb):
        return self._cz.get((qa, qb), self._cz.get((qb, qa)))

    def readout_error(self, q):
        return self._ep.readout_errors.get(q)

    def prx_duration_ns(self):
        return self._ep.single_qubit_gate_durations.get("prx")

    def cz_duration_ns(self):
        return self._ep.two_qubit_gate_durations.get("cz")


class RealCal:
    """Calibration parsed from the server's quality-metric-set observations.

    Each observation is `{dut_field, value, unit, uncertainty}`; the dut_field
    encodes the metric and the qubit / qubit-pair, e.g.
        characterization.model.QB1.t1_time                 (seconds)
        metrics.rb.prx.drag_crf_sx.QB1.fidelity:par=d2     -> prx_error = 1 - fid
        metrics.irb.cz.crf_crf.QB2__QB5.fidelity:par=d2    -> cz_error  = 1 - fid
        metrics.ssro.measure.constant.QB1.error_0_to_1     (readout 0->1)
        metrics.ssro.measure.constant.QB1.error_1_to_0     (readout 1->0)
    """

    def __init__(self, client):
        qms = client.get_quality_metric_set()
        self.name = f"{qms.dut_label} @ {qms.created_timestamp:%Y-%m-%d %H:%M UTC}"
        self._t1, self._t2 = {}, {}
        self._prx_fid, self._cz_fid = {}, {}
        self._ro01, self._ro10 = {}, {}

        for o in qms.observations:
            d = o if isinstance(o, dict) else (
                o.model_dump() if hasattr(o, "model_dump") else o.__dict__)
            field, value = d["dut_field"], d["value"]

            if (m := re.match(r"characterization\.model\.(QB\d+)\.t1_time$", field)):
                self._t1[m.group(1)] = value
            elif (m := re.match(r"characterization\.model\.(QB\d+)\.t2_echo_time$", field)):
                self._t2[m.group(1)] = value
            elif (m := re.match(r"metrics\.rb\.prx\..*?\.(QB\d+)\.fidelity", field)):
                self._prx_fid[m.group(1)] = value
            elif (m := re.match(r"metrics\.irb\.cz\..*?\.(QB\d+)__(QB\d+)\.fidelity", field)):
                self._cz_fid[frozenset(m.group(1, 2))] = value
            elif (m := re.match(r"metrics\.ssro\.measure\.constant\.(QB\d+)\.error_0_to_1$", field)):
                self._ro01[m.group(1)] = value
            elif (m := re.match(r"metrics\.ssro\.measure\.constant\.(QB\d+)\.error_1_to_0$", field)):
                self._ro10[m.group(1)] = value

    @staticmethod
    def _ns(seconds):
        return seconds * 1e9 if seconds is not None else None

    @staticmethod
    def _err(fidelity):
        return 1.0 - fidelity if fidelity is not None else None

    def t1_ns(self, q):
        return self._ns(self._t1.get(q))

    def t2_ns(self, q):
        return self._ns(self._t2.get(q))

    def prx_error(self, q):
        return self._err(self._prx_fid.get(q))

    def cz_error(self, qa, qb):
        return self._err(self._cz_fid.get(frozenset((qa, qb))))

    def readout_error(self, q):
        e0, e1 = self._ro01.get(q), self._ro10.get(q)
        if e0 is None and e1 is None:
            return None
        return {"0": e0, "1": e1}

    def prx_duration_ns(self):
        return None  # not exposed by the quality-metric-set

    def cz_duration_ns(self):
        return None


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
        })

    neighbours = {}
    for i in range(backend.num_qubits):
        q = backend.index_to_qubit_name(i)
        nbrs = sorted({n for e in edges_idx for n in e if i in e and n != i})
        neighbours[q] = {
            "index": i,
            "degree": len(nbrs),
            "neighbours": [backend.index_to_qubit_name(n) for n in nbrs],
            "t1_ns": cal.t1_ns(q),
            "t2_ns": cal.t2_ns(q),
            "prx_error": cal.prx_error(q),
            "readout_error": cal.readout_error(q),
        }

    return {
        "backend": name or backend.name,
        "calibration": cal.name,
        "num_qubits": backend.num_qubits,
        "qubits": list(backend.architecture.qubits),
        "native_gates": list(backend.operation_names),
        "gate_durations_ns": {
            "prx": cal.prx_duration_ns(),
            "cz": cal.cz_duration_ns(),
        },
        "num_edges": len(edges),
        "edges": edges,
        "neighbours": neighbours,
    }


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

    d = topo["gate_durations_ns"]
    print(f"Times  : prx {d['prx']} ns, cz {d['cz']} ns   (calibration: {topo['calibration']})")

    print(f"\nCoupling map ({topo['num_edges']} undirected edges):")
    for e in topo["edges"]:
        a, b = e["qubits"]
        print(f"  {a:<5} -- {b:<5}   {str(tuple(e['indices'])):<9} cz_err={_num(e['cz_error'], '.4e')}")

    print("\nPer-qubit calibration:")
    print(f"  {'qubit':<6}{'deg':<5}{'T1(ns)':<14}{'T2(ns)':<14}{'prx_err':<12}readout_err(0/1)")
    for q, info in topo["neighbours"].items():
        ro = info["readout_error"] or {}
        print(f"  {q:<6}{info['degree']:<5}"
              f"{_num(info['t1_ns'], ',.0f'):<14}{_num(info['t2_ns'], ',.0f'):<14}"
              f"{_num(info['prx_error'], '.3e'):<12}"
              f"{_num(ro.get('0'), '.4f')}/{_num(ro.get('1'), '.4f')}")


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
    p.add_argument("--token", default=ENV_TOKEN,
                   help="API token for real backends (default: $IQM_TOKEN / $RESONANCE_API_KEY)")
    args = p.parse_args()

    backend, cal = build_backend(args.backend, token=args.token)
    topo = connectivity(backend, cal, name=args.backend)
    print_topology(topo)

    # Always emit a JSON file; honour an explicit path if given.
    json_path = args.json if args.json else f"{args.backend}_topology.json"
    with open(json_path, "w") as f:
        json.dump(topo, f, indent=2)
    print(f"\nSaved connectivity -> {json_path}")

    if args.plot:
        from qiskit.visualization import plot_coupling_map
        plot_coupling_map(backend.num_qubits, None, backend.coupling_map.get_edges()).savefig(f"{args.backend}_topology.png")
        #save_plot(backend, f"{args.backend}_topology.png")


if __name__ == "__main__":
    main()
