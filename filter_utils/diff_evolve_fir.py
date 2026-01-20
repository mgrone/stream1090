# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2025 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#


#!/usr/bin/env python3
import numpy as np
from scipy.optimize import differential_evolution
from scipy.signal import firwin2
import subprocess

# ============================================================
#  Config
# ============================================================

DATA_PATH = "../../samples/balcony_6M_4.raw"
FILTER_PATH= "./diff_evolve_fir_temp.txt"
FS = 6_000_000
FS_UP = 12_000_000 
NUMTAPS = 15

# Number of interior frequency points (controls shape complexity)
K = 5  # you can change this

#CENTER_SEED = [-0.185171, 1.531379, -1.351036, 0.291881, -0.086908 ]
#CENTER_SEED_MARGIN = 0.1


#CENTER_SEED = [-0.3699803648355714, 1.1340839591825858, -0.6962101473953, 0.7509089543505206, -0.18001058325125197]
#CENTER_SEED = [-0.3969400962617796, 1.11478949381048, -0.7079336291556649, 0.7810669853595449, -0.18581239035226063]
#CENTER_SEED = [-0.3976758731101197, 1.121038713666279, -0.5073016296482065, 0.8020675299866561, -0.2274269284556702]
#CENTER_SEED = upsample_vector([-0.185171, 1.531379, -1.351036,  0.291881, -0.086908], K)
#CENTER_SEED = [0.1957214039445784, 1.632414613644746, -1.4827299101065972, -0.30635142999486187, -0.029299815189840588]
#CENTER_SEED = [-0.06450675543009807, 0.25914537074327354, 0.6732494322832727, -0.22394861832395696, -0.5174161750498946, 0.48478921545763637, -0.4434163008674888, 0.3085847917997503]
#CENTER_SEED = [0.3969400962617796, 0.7079336291556649,1.11478949381048, -0.7810669853595449, -0.18581239035226063]
#CENTER_SEED = [-0.3969400962617796, 1.11478949381048, -0.7079336291556649, 0.7810669853595449, -0.18581239035226063]

# 10m -> 24m
#CENTER_SEED = [-0.42644709942782655, 1.9996087602250656, -1.9663945740567215, 0.4757880624279247, -0.2159365248939654]

# 6m -> 12m
#CENTER_SEED = [-0.11192266576928156, 1.2028505526916322, -0.5924694334494077, 0.6635899469799019, -0.40675250638587135]
#CENTER_SEED = [-0.41334447639112915, 1.2304346093963963, -0.9345840585000204, 0.5026669941950287, -0.06456264334932502]
CENTER_SEED = [-0.30480078491692353, 1.191383872925207, -0.8946397241760973, 0.7031294489517014, -0.29172370808888537]
CENTER_SEED_MARGIN = 0.5


# DC and Nyquist gains (before normalization)
G_DC = 1.0
G_NYQ = 0.0

GAIN_MIN = -2.0
GAIN_MAX = 2.0

bounds = [ (max(GAIN_MIN, c - CENTER_SEED_MARGIN), min(GAIN_MAX, c + CENTER_SEED_MARGIN)) for c in CENTER_SEED] # + [[0.002, 0.008]]
# CENTER_SEED = CENTER_SEED + [0.005]

#print(CENTER_SEED)
#print(bounds)

# ============================================================
#  Data loading
# ============================================================

#print("Loading IQ data...")
#I, Q = load_raw_u16(DATA_PATH)

# ============================================================
#  Low-pass firwin2 builder
# ============================================================

def build_lowpass_firwin2(params, K, g_dc=G_DC, g_nyq=G_NYQ):
    """
    params: length 2K
        [f1, ..., fK, g1, ..., gK]

    Returns:
        h: FIR taps (normalized)
        freq: frequency grid
        gain: gain grid (after low-pass projection)
    """
    params = np.asarray(params, dtype=np.float64)
    g_interior = params[:K]
    #dc = params[K]
    # 2) Build frequency and gain vectors with fixed endpoints
    #freq = np.linspace(0.0, 1.0, K + 2)
    freq = np.concatenate(([0.0], [0.25, 0.375, 0.5, 0.625, 0.75], [1.0]))
    g = np.concatenate(([g_dc], g_interior, [g_nyq]))
    
    # 4) Build filter
    h = firwin2(NUMTAPS, freq, g).astype(np.float32)

    # Normalize DC gain to 1 (sum of taps)
    h /= np.sum(h)

    return h, freq, g #, dc

# ============================================================
#  DE objective with best-so-far tracking
# ============================================================

best_score = -np.inf
best_total = -np.inf
best_params = None
best_taps = None

def is_lowpass(h, min_dc=0.1, max_hf_ratio=0.7):
    """
    Low-pass sanity check for arbitrary-length FIR filters.

    Conditions:
      - DC gain (sum of taps) must be positive and above threshold
      - Nyquist gain must be sufficiently smaller than DC gain
    """
    h = np.asarray(h, dtype=np.float32)
    N = len(h)

    # DC gain
    H0 = float(np.sum(h))
    if H0 <= min_dc:
        return False

    # Nyquist response: multiply by (-1)^n
    signs = np.power(-1.0, np.arange(N, dtype=np.float32))
    Hpi = float(np.sum(h * signs))

    # Low-pass condition: Nyquist must be small relative to DC
    if abs(Hpi) >= max_hf_ratio * H0:
        return False

    return True

def count_ext_squitter(out):
    total = sum(line.startswith(b"@") and len(line) > 13 and line[13] == ord("8") for line in out.splitlines())
    return total

def evaluate_filter(params):
    global best_score, best_params, best_taps

    # Build low-pass filter from params
    h, freq, gain = build_lowpass_firwin2(params, K)

    # Low-pass constraint
    if not is_lowpass(h):
        return 1e9

    # Write taps.txt
    with open(FILTER_PATH, "w") as f:
        for t in h:
            f.write(f"{t}\n")

    # Run stream1090 with raw IQ piped in
    cmd = [
        "bash", "-c",
        f"cat {DATA_PATH} | ../build/stream1090_6M -f {FILTER_PATH} -u {FS_UP // 1_000_000}"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out,_ = proc.communicate()


    total = sum(line.startswith(b"@") for line in out.splitlines())

    df17 = count_ext_squitter(out)

    score = df17 + total  # maximize messages
    print(score)
   

    # Track best-so-far
    if score > best_score:
        best_score = score
        best_total = total
        best_params = params.copy()
        best_taps = h.copy()

        # 🔥 Append to log file
        with open("de_log.txt", "a") as f:
            f.write(f"======== {FS/1_000_000} -> {FS_UP/1_000_000} with {NUMTAPS} taps ============================\n")
            f.write(f"New best score: {best_score}\n")
            f.write(f"New best total: {best_total}\n")
            f.write(f"Best params: {best_params.tolist()}\n")
            f.write("Best taps:\n")
            for t in best_taps:
                f.write(f"{t}\n")
            f.write("\n")


    # Progress print
    print(f"Eval params={np.round(params, 4)} → messages={total}   ")
    print(f"Eval bounds={np.round(bounds, 4)}")
    print(f"| Best so far: {best_score} @ {np.round(best_params, 4) if best_params is not None else None}")
    print(f"| Best so far: {best_taps} @ {np.round(best_params, 4) if best_params is not None else None}")

    # DE minimizes → return negative
    return -score

# ============================================================
#  DE setup and run (looped)
# ============================================================

isFirstRun = True
center = np.array(CENTER_SEED, dtype=np.float64)

while True:
    print("Starting Differential Evolution...")
    #numIts = 10
    #if (isFirstRun):
    #    numIts = 1

    result = differential_evolution(
        evaluate_filter,
        bounds,
        maxiter=40,
        #maxiter=numIts,
        #popsize=12,
        popsize=20,
        mutation=(0.5, 1.0),
        recombination=0.7,
        polish=False,
        workers=1,
        x0=center,
    )

    print("\n================ FINAL BEST ================")
    print(f"Best params: {np.round(best_params, 6)}")
    print(f"Best ext-squitter count: {best_score}")
    print(f"Best message count: {best_total}")
    print("Best taps:")
    print(np.round(best_taps, 8))
    print("===========================================\n")


    # Extract best and second-best from DE result
    energies = result.population_energies
    pop = result.population
    idx_sorted = np.argsort(energies)

    best = pop[idx_sorted[0]]
    second = pop[idx_sorted[1]]

    # Compute per-parameter margins based on distance
    alpha = 2.0  # scaling factor, tune as needed
    margins = alpha * np.abs(best - second)

    # Clamp margins to a safe range
    margins = np.clip(margins, 0.05, 0.5)

    # Build per-parameter bounds
    bounds = []
    for i in range(K):
        low  = max(GAIN_MIN, best[i] - margins[i])
        high = min(GAIN_MAX, best[i] + margins[i])
        bounds.append((low, high))
    
    #dc_min = max(0.0, best[K] - margins[K])
    #dc_max = min(1.0, best[K] + margins[K])
    #bounds.append((dc_min, dc_max))

    print("Per-parameter margins:", margins)

    # 🔥 update center for next DE iteration
    center = best_params.copy()
    isFirstRun = False
    # 🔥 reset best trackers for next loop
    # best_score = -np.inf
    # best_params = None
    # best_taps = None

