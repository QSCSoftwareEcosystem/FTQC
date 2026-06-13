"""Print / export the qubit connectivity (coupling map / topology) of an IQM backend.

Usage:
    python3 topology.py [backend] [--plot] [--json PATH]

    backend : garnet (default) | apollo | adonis | deneb
    --plot  : save a PNG drawing of the qubit graph (<backend>_topology.png)
    --json  : write connectivity to a JSON file (default: <backend>_topology.json)
"""
import argparse
import json

from iqm.qiskit_iqm.fake_backends.fake_garnet import IQMFakeGarnet
from iqm.qiskit_iqm.fake_backends.fake_apollo import IQMFakeApollo
from iqm.qiskit_iqm.fake_backends.fake_adonis import IQMFakeAdonis
from iqm.qiskit_iqm.fake_backends.fake_deneb import IQMFakeDeneb

BACKENDS = {
    "garnet": IQMFakeGarnet,
    "apollo": IQMFakeApollo,
    "adonis": IQMFakeAdonis,
    "deneb": IQMFakeDeneb,
}


def _cz_error(ep, qa: str, qb: str):
    """Look up the cz depolarizing error for an edge (keys are directed tuples)."""
    table = ep.two_qubit_gate_depolarizing_error_parameters.get("cz", {})
    return table.get((qa, qb), table.get((qb, qa)))


def connectivity(backend) -> dict:
    """Build a JSON-serializable description of the backend topology + calibration."""
    cm = backend.coupling_map
    ep = backend.error_profile
    edges_idx = sorted({tuple(sorted(e)) for e in cm.get_edges()})

    edges = []
    for a, b in edges_idx:
        qa, qb = backend.index_to_qubit_name(a), backend.index_to_qubit_name(b)
        edges.append({
            "indices": [a, b],
            "qubits": [qa, qb],
            "cz_error": _cz_error(ep, qa, qb),
        })

    prx_err = ep.single_qubit_gate_depolarizing_error_parameters.get("prx", {})
    neighbours = {}
    for i in range(backend.num_qubits):
        q = backend.index_to_qubit_name(i)
        nbrs = sorted({n for e in edges_idx for n in e if i in e and n != i})
        neighbours[q] = {
            "index": i,
            "degree": len(nbrs),
            "neighbours": [backend.index_to_qubit_name(n) for n in nbrs],
            "t1_ns": ep.t1s.get(q),
            "t2_ns": ep.t2s.get(q),
            "prx_error": prx_err.get(q),
            "readout_error": ep.readout_errors.get(q),
        }

    return {
        "backend": backend.name,
        "calibration": ep.name,
        "num_qubits": backend.num_qubits,
        "qubits": list(backend.architecture.qubits),
        "native_gates": list(backend.operation_names),
        "gate_durations_ns": {
            "prx": ep.single_qubit_gate_durations.get("prx"),
            "cz": ep.two_qubit_gate_durations.get("cz"),
        },
        "num_edges": len(edges),
        "edges": edges,
        "neighbours": neighbours,
    }


def print_topology(topo: dict) -> None:
    print(f"Backend: {topo['backend']}")
    print(f"Qubits : {topo['num_qubits']}  ({', '.join(topo['qubits'])})")
    print(f"Native : {topo['native_gates']}")

    d = topo["gate_durations_ns"]
    print(f"Times  : prx {d['prx']} ns, cz {d['cz']} ns   (calibration: {topo['calibration']})")

    print(f"\nCoupling map ({topo['num_edges']} undirected edges):")
    for e in topo["edges"]:
        a, b = e["qubits"]
        print(f"  {a:<5} -- {b:<5}   {str(tuple(e['indices'])):<9} cz_err={e['cz_error']}")

    print("\nPer-qubit calibration:")
    print(f"  {'qubit':<6}{'deg':<5}{'T1(ns)':<10}{'T2(ns)':<10}{'prx_err':<10}readout_err(0/1)")
    for q, info in topo["neighbours"].items():
        ro = info["readout_error"] or {}
        print(f"  {q:<6}{info['degree']:<5}{info['t1_ns']!s:<10}{info['t2_ns']!s:<10}"
              f"{info['prx_error']!s:<10}{ro.get('0')}/{ro.get('1')}")


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
    p.add_argument("backend", nargs="?", default="garnet", choices=BACKENDS.keys())
    p.add_argument("--plot", action="store_true", help="save a PNG of the qubit graph")
    p.add_argument("--json", nargs="?", const="", default=None,
                   help="write connectivity JSON (optional PATH; default <backend>_topology.json)")
    args = p.parse_args()

    backend = BACKENDS[args.backend]()
    topo = connectivity(backend)
    print_topology(topo)

    # Always emit a JSON file; honour an explicit path if given.
    json_path = args.json if args.json else f"{args.backend}_topology.json"
    with open(json_path, "w") as f:
        json.dump(topo, f, indent=2)
    print(f"\nSaved connectivity -> {json_path}")

    if args.plot:
        #save_plot(backend, f"{args.backend}_topology.png")
        from qiskit.visualization import plot_coupling_map
        plot_coupling_map(backend.num_qubits, None, backend.coupling_map.get_edges()).savefig(f"{args.backend}_topology.png")


if __name__ == "__main__":
    main()
