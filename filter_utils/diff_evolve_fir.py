#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2025 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#

import numpy as np
from scipy.optimize import differential_evolution
from scipy.signal import firwin2
import subprocess

# ============================================================
#  Config
# ============================================================

DATA_PATH = "../../samples/balcony_6M_small.raw"
FILTER_PATH = "./diff_evolve_fir_temp.txt"
FS = 6_000_000
FS_UP = 12_000_000
NUMTAPS = 15

# Number of interior frequency points
K = 5

CENTER_SEED = [-0.30480078491692353, 1.191383872925207,
               -0.8946397241760973, 0.7031294489517014,
               -0.29172370808888537]
CENTER_SEED_MARGIN = 0.5

G_DC = 1.0
G_NYQ = 0.0

GAIN_MIN = -2.0
GAIN_MAX = 2.0

bounds = [
    (max(GAIN_MIN, c - CENTER_SEED_MARGIN),
     min(GAIN_MAX, c + CENTER_SEED_MARGIN))
    for c in CENTER_SEED
]

# ============================================================
#  FIR builder
# ============================================================

def build_lowpass_firwin2(params, K, g_dc=G_DC, g_nyq=G_NYQ):
    params = np.asarray(params, dtype=np.float64)
    g_interior = params[:K]

    freq = np.concatenate(([0.0], [0.25, 0.375, 0.5, 0.625, 0.75], [1.0]))
    g = np.concatenate(([g_dc], g_interior, [g_nyq]))

    h = firwin2(NUMTAPS, freq, g).astype(np.float32)
    h /= np.sum(h)

    return h, freq, g

# ============================================================
#  Low-pass sanity check
# ============================================================

def is_lowpass(h, min_dc=0.1, max_hf_ratio=0.7):
    h = np.asarray(h, dtype=np.float32)
    N = len(h)

    H0 = float(np.sum(h))
    if H0 <= min_dc:
        return False

    signs = np.power(-1.0, np.arange(N, dtype=np.float32))
    Hpi = float(np.sum(h * signs))

    if abs(Hpi) >= max_hf_ratio * H0:
        return False

    return True

# ============================================================
#  Output parsing
# ============================================================

def count_ext_squitter(out):
    return sum(
        line.startswith(b"@") and len(line) > 13 and line[13] == ord("8")
        for line in out.splitlines()
    )

# ============================================================
#  Best-so-far tracking
# ============================================================

best_score = -np.inf
best_total = -np.inf
best_params = None
best_taps = None

# ============================================================
#  DE objective
# ============================================================

def evaluate_filter(params):
    global best_score, best_params, best_taps, best_total

    h, freq, gain = build_lowpass_firwin2(params, K)

    if not is_lowpass(h):
        return 1e9

    with open(FILTER_PATH, "w") as f:
        for t in h:
            f.write(f"{t}\n")

    if FS == 2_400_000:
        exe = "stream1090"
    elif FS == 6_000_000:
        exe = "stream1090_6M"
    elif FS == 10_000_000:
        exe = "stream1090_10M"
    else:
        raise ValueError(f"Unsupported FS={FS}")

    cmd = [
        "bash", "-c",
        f"cat {DATA_PATH} | ../build/{exe} -f {FILTER_PATH} -u {FS_UP // 1_000_000}"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out, _ = proc.communicate()

    total = sum(line.startswith(b"@") for line in out.splitlines())
    df17 = count_ext_squitter(out)

    score = df17 + total
    print(score)

    # ========================================================
    #  Best-so-far logging
    # ========================================================
    if score > best_score:
        best_score = score
        best_total = total
        best_params = params.copy()
        best_taps = h.copy()

        from datetime import datetime
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        with open("de_log.txt", "a") as f:
            f.write("# ================================================================\n")
            f.write(f"# Instance: {DATA_PATH}\n")
            f.write(f"# Time: {timestamp}\n")
            f.write(f"# FS: {FS/1_000_000} MHz → FS_UP: {FS_UP/1_000_000} MHz\n")
            f.write(f"# Number of taps: {NUMTAPS}\n")
            f.write(f"# Best score: {best_score}\n")
            f.write(f"# Best total: {best_total}\n")
            f.write(f"# Best params: {best_params.tolist()}\n")

            # NEW: write current bounds
            f.write("# Current bounds:\n")
            for i, (lo, hi) in enumerate(bounds):
                f.write(f"# [{lo:.6f}, {hi:.6f}]\n")

            f.write("# Best taps: \n")

            # taps remain un-commented
            for t in best_taps:
                f.write(f"{t}\n")

            f.write("\n")


    print(f"Eval params={np.round(params, 4)} → messages={total}")
    print(f"Eval bounds={np.round(bounds, 4)}")
    print(f"| Best so far: {best_score} @ {np.round(best_params, 4) if best_params is not None else None}")

    return -score

# ============================================================
#  DE loop
# ============================================================

center = np.array(CENTER_SEED, dtype=np.float64)

while True:
    print("Starting Differential Evolution...")

    result = differential_evolution(
        evaluate_filter,
        bounds,
        maxiter=40,
        popsize=20,
        mutation=(0.5, 1.0),
        recombination=0.7,
        polish=False,
        workers=1,
        x0=center,   # ← restored as requested
    )

    print("\n================ END OF RUN ====================")
    print(f"Best params: {np.round(best_params, 6)}")
    print(f"Best ext-squitter count: {best_score}")
    print(f"Best message count: {best_total}")
    print("Best taps:")
    print(np.round(best_taps, 8))
    print("===============================================\n")

    # Also append to log
    from datetime import datetime
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    with open("de_log.txt", "a") as f:
        f.write("# ==================== END OF RUN ====================\n")
        f.write(f"# Time: {timestamp}\n")
        f.write(f"# Instance: {DATA_PATH}\n")
        f.write(f"# Best params: {best_params.tolist()}\n")
        f.write(f"# Best ext-squitter count: {best_score}\n")
        f.write(f"# Best message count: {best_total}\n")
        f.write("# Best taps:\n")

        # taps remain un-commented
        for t in best_taps:
            f.write(f"{t}\n")

        f.write("\n")


    energies = result.population_energies
    pop = result.population
    idx_sorted = np.argsort(energies)

    best = pop[idx_sorted[0]]
    second = pop[idx_sorted[1]]

    alpha = 2.0
    margins = alpha * np.abs(best - second)
    margins = np.clip(margins, 0.05, 0.5)

    bounds = []
    for i in range(K):
        low = max(GAIN_MIN, best[i] - margins[i])
        high = min(GAIN_MAX, best[i] + margins[i])
        bounds.append((low, high))

    print("# Per-parameter margins:", margins)

    center = best_params.copy()
