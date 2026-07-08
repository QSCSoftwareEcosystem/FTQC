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
FTQC_DIR=$(dirname -- "$(dirname -- "$SCRIPT")")
cd $FTQC_DIR || exit 1
source $FTQC_DIR/realqpu/bin/activate
ts=$(date +%Y%m%d-%H%M%S)
python3 ${FTQC_DIR}/topology/topology.py crystal --json "${FTQC_DIR}/topology/topology-$ts.json"  
