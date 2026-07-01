IQM_BASE_URL=https://qccsw.ccs.ornl.gov python3 ftqc_iris_task.py --submit-iqm --qc-alias default --shots 512
IQM_BASE_URL=https://qccsw.ccs.ornl.gov python3 ftqc_iris_task.py test/Integration/logical0.qasm --physical --submit-iqm --qc-alias default --shots 512
