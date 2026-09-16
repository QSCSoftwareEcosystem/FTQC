# Helper commands for docs/ftqc-demo.tape.
# Sourced by VHS before recording starts.

cd "$FTQC_ROOT"
source "$HOME/.vcompile/bin/activate"
source "$FTQC_ROOT/build/setup.source" >/dev/null
export PATH="$FTQC_ROOT/build/src:$PATH"
export PS1=$'\033[1;36mftqc-demo \$ \033[0m'

dim() {
  perl -pe 's/^/\e[38;5;250m/; s/$/\e[0m/'
}

step() {
  printf '\033[1;33m%s\033[0m\n' "$*"
}

show_circuit() {
  step '1. Input a circuit with logical qubits'
  sed -n '1,10p' test/Integration/logical0.qasm | dim
}

logical_mlir() {
  step '2. Lower circuit into logical FTQC MLIR with specified code'
  qasm3-import test/Integration/logical0.qasm | sed -n '1,12p' | dim
}

resource_estimate() {
  step '3. Ask what fault tolerance will cost with chosen code'
  qasm3-import test/Integration/logical0.qasm \
    | ftqc-opt --ftqc-insert-syndrome --ftqc-resource-estimate 2>&1 \
    | sed -n '1,18p' \
    | dim
}

physical_mlir() {
  step '4. Lower logical gates into physical operations'
  local physical=/tmp/ftqc-logical0.physical.mlir
  local err=/tmp/ftqc-logical0.physical.err
  if ! ftqc-opt test/Integration/logical0.qasm --ftqc-steane-pipeline --ftqc-expand-physical \
      > "$physical" 2>"$err"; then
    sed -n '1,8p' "$err" | dim
    return 1
  fi
  {
    rg -m 3 'qphys.alloc' "$physical"
    printf '    ... one logical qubit becomes data + syndrome ancillas ...\n'
    rg 'qphys.h|qphys.cx|qphys.measure' "$physical" | sed -n '1,11p'
  } | dim
}

make_stim() {
  ftqc-opt test/Integration/logical0.qasm \
    --ftqc-lower-to-stim=physical-error-rate=0.0005\ syndrome-rounds=3\ include-comments=false \
    2>/tmp/logical0.stim >/dev/null
}

stim_lowering() {
  step '5. Emit a Stim model from the same circuit'
  make_stim
  {
    printf 'input: test/Integration/logical0.qasm\n'
    printf 'Stim circuit: '
    wc -l < /tmp/logical0.stim | tr -d '\n'
    printf ' lines, '
    rg -c '^DETECTOR' /tmp/logical0.stim | tr -d '\n'
    printf ' detectors, '
    rg -c '^OBSERVABLE_INCLUDE' /tmp/logical0.stim | tr -d '\n'
    printf ' observable\n'
    sed -n '1,8p' /tmp/logical0.stim
  } | dim
}

stim_ler() {
  step '6. Decode Stim shots into a logical error rate'
  make_stim
  python3 - /tmp/logical0.stim <<'PY' | dim
import sys, stim, pymatching
shots = 5000
circuit = stim.Circuit(open(sys.argv[1]).read())
dem = circuit.detector_error_model(decompose_errors=True)
matcher = pymatching.Matching.from_detector_error_model(dem)
det, obs = circuit.compile_detector_sampler(seed=7).sample(shots, separate_observables=True)
pred = matcher.decode_batch(det)
failures = int((pred != obs).any(axis=1).sum())
print('decoder: PyMatching MWPM')
print('physical p: 5.0e-4')
print('shots:', shots)
print('decoded failures:', failures)
print('decoded LER:', format(failures / shots, '.3e'))
PY
}

mapped_mlir() {
  step '7. Map physical qubits onto the IQM layout'
  local mapped=/tmp/ftqc-logical0.mapped.mlir
  local err=/tmp/ftqc-logical0.mapped.err
  if ! ftqc-opt test/Integration/logical0.qasm \
      --ftqc-steane-pipeline \
      --ftqc-expand-physical \
      --ftqc-qphys-init-layout=input-file=topology/init-layout-iqm20q-steane-3anc.json \
      > "$mapped" 2>"$err"; then
    sed -n '1,8p' "$err" | dim
    return 1
  fi
  {
    printf 'layout: topology/init-layout-iqm20q-steane-3anc.json\n'
    rg -m 6 'qphys.alloc' "$mapped"
    printf '    ... qphys ids now name selected IQM hardware qubits ...\n'
    rg -m 4 'qphys.h|qphys.cx|qphys.measure' "$mapped"
  } | dim
}

json_snippet() {
  python3 - "$1" <<'PY'
import json, sys
text = open(sys.argv[1]).read()
j, _ = json.JSONDecoder().raw_decode(text)
preview = {
    'name': j['name'],
    'instructions': j['instructions'][:4],
}
print(json.dumps(preview, indent=2))
PY
}

iqm_summary() {
  python3 - "$1" <<'PY'
import json, sys
text = open(sys.argv[1]).read()
j, _ = json.JSONDecoder().raw_decode(text)
inst = j['instructions']

def qkey(q):
    return int(q[2:]) if q.startswith('QB') and q[2:].isdigit() else q
qubits = sorted({q for i in inst for q in i.get('locus', [])}, key=qkey)
gates = sorted({i['name'] for i in inst})
print('layout: topology/init-layout-iqm20q-steane-3anc.json')
print('IQM JSON:', j['name'], len(inst), 'instructions')
print('hardware qubits:', ', '.join(qubits[:8]), '...')
print('native gates:', ', '.join(gates))
print('note: Steane operations lower to IQM native PRX/CZ gates')
PY
}

export_iqm() {
  step '8. Export the mapped circuit as IQM JSON'
  ftqc-opt test/Integration/logical0.qasm \
    --ftqc-steane-pipeline \
    --ftqc-expand-physical \
    --ftqc-qphys-init-layout=input-file=topology/init-layout-iqm20q-steane-3anc.json \
    --ftqc-qphys-to-iqm-json \
    >/tmp/ftqc-logical0.iqm.json 2>/dev/null
  iqm_summary /tmp/ftqc-logical0.iqm.json | dim
  json_snippet /tmp/ftqc-logical0.iqm.json | sed -n '1,14p' | dim
}
