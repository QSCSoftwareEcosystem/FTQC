# install iqm-client with qrisp extras

#!pip install -U "iqm-client>=34.0.3" "qrisp[iqm]"
# used for visualization
#!pip install matplotlib
#!pip install qrisp[iqm]
#!pip install iqm-client>=34.0.3


from qrisp.interface import IQMBackend

# Set use_timeslot=True if you have booked a timeslot
quantum_computer = IQMBackend(
    api_token = "93lj8ALd8qYuWRxKKh8vvn2FSt7UMSm0eSmFEXkwj4QBnw800zt3UJQq+rzNZELY",
    server_url = "https://qccsw.ccs.ornl.gov/")
    #use_timeslot=False)

from qrisp import *
from matplotlib import pyplot as plt

# This example creates a GHZ state, replace it with your own code
qv = QuantumVariable(3)
h(qv[0])
cx(qv[0], qv[1])
cx(qv[0], qv[2])

# Execute it on the quantum computing backend
# Transpilation is taken care of automatically
result = qv.get_measurement(backend = quantum_computer)

# Print and plot the result
print(result)

plt.bar(result.keys(), result.values())
plt.xlabel('Measurement outcomes')
plt.ylabel('Probability')
plt.tight_layout()
plt.savefig('real-iqm-result.png')   # always works, incl. headless runs
print('Saved plot -> real-iqm-result.png')
if plt.get_backend().lower() != 'agg':   # only show() under an interactive backend
    plt.show()
