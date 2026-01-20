from scipy.signal import firwin

numtaps = 15

fs = 10_000_000 
cutoff_hz = 2_000_000 
h = firwin(numtaps, cutoff_hz, fs=fs, width=0.02)

for x in h:
    print(x)