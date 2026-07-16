#!/bin/bash
SCRIPT_FILE=${BASH_SOURCE[0]}
PYTHON_EXE=python
version=$(python -V 2>&1 | grep -Po '(?<=Python )(.+)')
if [[ -z "$version" ]]
then
    PYTHON_EXE=python3
    version=$(python3 -V 2>&1 | grep -Po '(?<=Python )(.+)')
    if [[ -z "$version" ]]
    then
    echo "No Python is detected! Cannot source" 
    return
    fi
fi
SCRIPT=$($PYTHON_EXE -c "import os,sys; print(os.path.realpath(os.path.expanduser(sys.argv[1])))" ${SCRIPT_FILE})
TOPO_DIR=$(dirname -- "$SCRIPT")
FTQC_DIR=$(dirname -- "$TOPO_DIR")
cd $TOPO_DIR || exit 1
source $FTQC_DIR/realqpu/bin/activate
# Capture one timestamped snapshot topology/topology-<UTC>.json — the pattern the
# Django dashboard ingests. topology.py stamps the filename and embeds
# `generated_at`, so each run is a distinct point on the dashboard's time axis.
# IQM auth comes from the environment (IQM_TOKEN / RESONANCE_API_KEY, IQM_BASE_URL);
# any extra args ("$@") are forwarded to topology.py.
python3 ${TOPO_DIR}/topology.py crystal --json /dev/null --snapshot-dir "${TOPO_DIR}" "$@"
