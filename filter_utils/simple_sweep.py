# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2025 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#

from scipy.signal import firwin2
import numpy as np
from filter_module import (
    run_raw_iq_pipeline,
    run_stream1090,
    load_raw_u16
)

fs = 10_000_000
numtaps = 17
#f_mids = [0.1, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50]
f_mids = [0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80]
#f_mids = [0.70, 0.71, 0.72, 0.73, 0.80]
#f_mids = [0.1, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.80]
#f_mids = [0.33, 0.335, 0.34, 0.345, 0.35]
#f_mids = [0.31, 0.32, 0.33, 0.34, 0.35, 0.36, 0.37, 0.38, 0.39, 0.4]
#g_values = [-0.05, -0.1, -0.15, -0.2, -0.25]
results = []
best_total = -1
best_fmid = None
best_h = None


# Load once
I, Q = load_raw_u16("../../samples/caius_new_10M_raw.bin")

for f_mid in f_mids:
#for g_value in g_values:
    #f_mid = 0.33
    freq = [0.0, f_mid, 1.0]
    gain = [1.0, 0.0, 0.0]

    # Design filter
    h = firwin2(numtaps, freq, gain).astype(np.float32)

    # Normalize DC gain to 1 for fair comparison
    h /= np.sum(h)

    # Run pipeline
    mag = run_raw_iq_pipeline(I, Q, h)

    total, _ = run_stream1090(
        mag,
        input_rate=fs,
        upsample_rate=24000000,
        stream1090_dir="../build"
    )

    if total > best_total:
        best_total = total
        best_fmid = f_mid
        best_h = h.copy()


    results.append((f_mid, total))
    print(f"f_mid={f_mid:.2f} → messages={total}")


import matplotlib.pyplot as plt
from scipy.signal import freqz

# Unpack results
f_vals = [r[0] for r in results]
msg_vals = [r[1] for r in results]

fig, (ax_imp, ax_freq, ax_msg) = plt.subplots(1, 3, figsize=(18, 5))

# ------------------------------------------------------------
# 1. Impulse responses
# ------------------------------------------------------------
for f_mid in f_mids:
    freq = [0.0, f_mid, 1.0]
    gain = [1.0, -0.1, 0.0]

    h = firwin2(numtaps, freq, gain).astype(np.float32)
    h /= np.sum(h)

    ax_imp.plot(h, marker='o', label=f"{f_mid:.2f}")

ax_imp.set_title("Impulse Responses")
ax_imp.set_xlabel("Tap index")
ax_imp.set_ylabel("Amplitude")
ax_imp.grid(True)
ax_imp.legend(fontsize=7)


# ------------------------------------------------------------
# 2. Frequency responses
# ------------------------------------------------------------
for f_mid in f_mids:
    freq = [0.0, f_mid, 1.0]
    gain = [1.0, -0.1, 0.0]

    h = firwin2(numtaps, freq, gain).astype(np.float32)
    h /= np.sum(h)

    w, H = freqz(h, worN=2048)
    freqs_hz = (w / np.pi) * (fs / 2)

    ax_freq.plot(freqs_hz, 20 * np.log10(np.abs(H) + 1e-12), label=f"{f_mid:.2f}")

ax_freq.set_title("Frequency Responses")
ax_freq.set_xlabel("Frequency (Hz)")
ax_freq.set_ylabel("Magnitude (dB)")
ax_freq.grid(True)
ax_freq.legend(fontsize=7)


# ------------------------------------------------------------
# 3. Message count vs f_mid
# ------------------------------------------------------------
ax_msg.plot(f_vals, msg_vals, marker='o')
ax_msg.set_title("Messages vs f_mid")
ax_msg.set_xlabel("f_mid (normalized)")
ax_msg.set_ylabel("Message count")
ax_msg.grid(True)

plt.tight_layout()
plt.show()

# ------------------------------------------------------------
# Print best filter summary
# ------------------------------------------------------------
print("\n================ BEST FILTER ================")
print(f"Best f_mid: {best_fmid:.3f}")
print(f"Best message count: {best_total}")
print("Best taps:")
print(np.round(best_h, 6))
print("=============================================\n")
