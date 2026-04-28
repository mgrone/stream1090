#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#
import argparse
import numpy as np
from scipy.optimize import differential_evolution
from scipy.signal import firwin2
import subprocess
from datetime import datetime
import re

# ============================================================
#  CLI
# ============================================================

def parse_args():
    p = argparse.ArgumentParser(
        description="Differential Evolution FIR optimizer for stream1090"
    )

    p.add_argument("--data", required=True,
                   help="Path to raw IQ sample file")

    p.add_argument("--fs", type=int, required=True,
                   help="Input sample rate (Hz)")

    p.add_argument("--fs-up", type=int, required=True,
                   help="Upsampled rate (Hz)")

    p.add_argument("--taps", type=int, default=15,
                   help="Number of FIR taps")

    p.add_argument("--k", type=int, default=9,
                   help="Number of interior gain points")

    p.add_argument("--margin", type=float, default=0.5,
                   help="Margin around center seed or resume vector")

    p.add_argument("--log", default="de_log.txt",
                   help="Log file path")

    p.add_argument("--maxiter", type=int, default=40)
    p.add_argument("--popsize", type=int, default=20)
    p.add_argument("--alpha", type=float, default=2.0)

    p.add_argument("--bounds-min", type=float, default=-2.0)
    p.add_argument("--bounds-max", type=float, default=2.0)

    p.add_argument("--resume",
                   help="Path to previous log file to resume from (uses best_params as new center)")
    p.add_argument("--resume-bounds", action="store_true",
                   help="When resuming, also reuse bounds from the log instead of recomputing from margin")

    p.add_argument("--df17-weight", type=float, default=1.0,
                   help="Weight for DF17 messages in the objective function")

    return p.parse_args()


args = parse_args()

# ============================================================
#  Config (center seed fixed for now)
# ============================================================

DATA_PATH = args.data
FILTER_PATH = "./diff_evolve_fir_temp.txt"
FS = args.fs
FS_UP = args.fs_up
NUMTAPS = args.taps
K = args.k

CENTER_SEED = [
    1.1316168334764158,
    0.20308185445041663,
    -0.18880172571207673,
    1.1015299202753106,
    -0.13136240939270896,
    0.3352500341043928,
    0.5515897246310204,
    0.30513096033801856,
    0.7708951792896465,
]

CENTER_SEED_MARGIN = args.margin

G_DC = 1.0
G_NYQ = 0.0

GAIN_MIN = args.bounds_min
GAIN_MAX = args.bounds_max

LOGFILE = args.log

# ============================================================
#  Resume helpers
# ============================================================

PARAMS_RE = re.compile(r"# Best params:\s*\[(.*)\]")
BOUNDS_RE = re.compile(r"# \[([0-9eE\+\-\.]+),\s*([0-9eE\+\-\.]+)\]")

def _parse_float_list(s: str):
    return [float(x.strip()) for x in s.split(",") if x.strip()]

def load_best_params_and_bounds_from_log(path: str):
    best_params = None
    bounds = []

    with open(path, "r") as f:
        lines = f.readlines()

    # scan forward, but keep last occurrence
    for i, line in enumerate(lines):
        m = PARAMS_RE.match(line)
        if m:
            best_params = _parse_float_list(m.group(1))
            # reset bounds; next lines will refill
            bounds = []
            # parse following lines as bounds until non-bound line
            j = i + 1
            while j < len(lines):
                bm = BOUNDS_RE.match(lines[j])
                if not bm:
                    break
                lo = float(bm.group(1))
                hi = float(bm.group(2))
                bounds.append((lo, hi))
                j += 1

    if best_params is None:
        raise ValueError(f"No 'Best params' found in log {path}")

    if bounds and len(bounds) != len(best_params):
        # if mismatch, ignore bounds (safer)
        bounds = []

    return np.array(best_params, dtype=np.float64), bounds


def make_bounds_around_center(center: np.ndarray, margin: float):
    bounds = []
    for c in center:
        lo = max(GAIN_MIN, c - margin)
        hi = min(GAIN_MAX, c + margin)
        bounds.append((lo, hi))
    return bounds


# ============================================================
#  FIR builder
# ============================================================

def build_lowpass_firwin2(params, K, g_dc=G_DC, g_nyq=G_NYQ):
    params = np.asarray(params, dtype=np.float64)
    g_interior = params[:K]

    freq = np.concatenate((
        [0.0],
        [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9],
        [1.0],
    ))
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

HEX = set(b"0123456789ABCDEFabcdef")

def is_hex(s: bytes) -> bool:
    return all(c in HEX for c in s)

def count_messages(out: bytes) -> int:
    count = 0
    for line in out.splitlines():
        if not line:
            continue

        # Must start with '@' or '<'
        if line[0] not in (ord('@'), ord('<')):
            continue

        # Must end with ';'
        if not line.endswith(b";"):
            continue

        payload = line[1:-1]

        if not is_hex(payload):
            continue

        count += 1

    return count


def extract_df_from_frame_hex(frame_hex: bytes) -> int:
    bits = bin(int(frame_hex, 16))[2:].zfill(len(frame_hex) * 4)
    return int(bits[:5], 2)


def count_df17(out: bytes) -> int:
    count = 0
    for line in out.splitlines():
        if not line or line[0] not in (ord('@'), ord('<')) or not line.endswith(b";"):
            continue

        payload = line[1:-1]

        # Skip timestamp + RSSI if present
        if line[0] == ord('<'):
            # < ts(12) rssi(2) frame(...) >
            frame_hex = payload[14:]
        else:
            # @ ts(12) frame(...)
            frame_hex = payload[12:]

        try:
            df = extract_df_from_frame_hex(frame_hex)
            if df == 17:
                count += 1
        except Exception:
            pass

    return count

# ============================================================
#  Best-so-far tracking
# ============================================================

best_score = -np.inf      # scalar objective
best_total = -np.inf
best_df17 = -np.inf
best_params = None
best_taps = None

# ============================================================
#  DE objective
# ============================================================

DEFAULT_RATE_PAIRS = [
    (2.4, 8.0),
    (6.0, 6.0),
    (10.0, 10.0),
]

def default_output_rate(input_mhz: float) -> float:
    for (inp, out) in DEFAULT_RATE_PAIRS:
        if inp == input_mhz:
            return out
    raise ValueError(f"No default output rate for input rate {input_mhz}")


def evaluate_builtin_filter():
    input_mhz = FS / 1_000_000.0

    if FS_UP is not None:
        output_mhz = FS_UP / 1_000_000.0
    else:
        output_mhz = default_output_rate(input_mhz)

    exe = "../build/stream1090"

    cmd = [
        "bash", "-c",
        f"cat {DATA_PATH} | {exe} "
        f"-s {input_mhz} "
        f"-u {output_mhz} "
        f"-q"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out, _ = proc.communicate()

    total = count_messages(out)
    df17 = count_df17(out)
    score = total + args.df17_weight * df17

    return score, total, df17


def evaluate_filter(params):
    global best_score, best_params, best_taps, best_total, best_df17, bounds

    h, freq, gain = build_lowpass_firwin2(params, K)

    if not is_lowpass(h):
        return 1e9

    with open(FILTER_PATH, "w") as f:
        for t in h:
            f.write(f"{t}\n")

    input_mhz = FS / 1_000_000.0

    if FS_UP is not None:
        output_mhz = FS_UP / 1_000_000.0
    else:
        output_mhz = default_output_rate(input_mhz)

    exe = "../build/stream1090"

    cmd = [
        "bash", "-c",
        f"cat {DATA_PATH} | {exe} "
        f"-s {input_mhz} "
        f"-u {output_mhz} "
        f"-f {FILTER_PATH}"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out, _ = proc.communicate()

    total = count_messages(out)
    df17 = count_df17(out)

    score = total + args.df17_weight * df17
    print(score)

    if score > best_score:
        best_score = score
        best_total = total
        best_df17 = df17
        best_params = params.copy()
        best_taps = h.copy()

        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        with open(LOGFILE, "a") as f:
            f.write("# ================================================================\n")
            f.write(f"# Instance: {DATA_PATH}\n")
            f.write(f"# Time: {timestamp}\n")
            f.write(f"# FS: {FS/1_000_000} MHz → FS_UP: {FS_UP/1_000_000} MHz\n")
            f.write(f"# Number of taps: {NUMTAPS}\n")
            f.write(f"# Best score: {best_score}\n")
            f.write(f"# Best message count: {best_total}\n")
            f.write(f"# Best DF17 count: {best_df17}\n")
            f.write(f"# Best params: {best_params.tolist()}\n")
            f.write("# Current bounds:\n")
            for lo, hi in bounds:
                f.write(f"# [{lo:.6f}, {hi:.6f}]\n")
            f.write("# Best taps:\n")
            for t in best_taps:
                f.write(f"{t}\n")
            f.write("\n")

    print(f"Eval params={np.round(params, 4)} → total={total}, df17={df17}, score={score}")
    print(f"Eval bounds={np.round(bounds, 4)}")
    print(f"| Best so far: score={best_score}, total={best_total}, df17={best_df17} @ {np.round(best_params, 4) if best_params is not None else None}")

    return -score

# ============================================================
#  DE loop
# ============================================================

# decide center + bounds (seed vs resume)
if args.resume:
    resume_center, resume_bounds = load_best_params_and_bounds_from_log(args.resume)

    if args.resume_bounds and resume_bounds:
        center = resume_center
        bounds = resume_bounds
    else:
        center = resume_center
        bounds = make_bounds_around_center(center, args.margin)
else:
    center = np.array(CENTER_SEED, dtype=np.float64)
    bounds = make_bounds_around_center(center, CENTER_SEED_MARGIN)

# baseline with built-in filter (per dataset)
baseline_score, baseline_total, baseline_df17 = evaluate_builtin_filter()
best_score = baseline_score
best_total = baseline_total
best_df17 = baseline_df17
best_params = None
best_taps = None

timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

with open(LOGFILE, "a") as f:
    f.write("# ==================== BASELINE ====================\n")
    f.write(f"# Time: {timestamp}\n")
    f.write(f"# Instance: {DATA_PATH}\n")
    f.write(f"# FS: {FS/1_000_000} MHz → FS_UP: {FS_UP/1_000_000} MHz\n")
    f.write(f"# Number of taps: {NUMTAPS}\n")
    f.write(f"# Built-in score: {baseline_score}\n")
    f.write(f"# Built-in message count: {baseline_total}\n")
    f.write(f"# Built-in DF17 count: {baseline_df17}\n")
    f.write("\n")

while True:
    print("Starting Differential Evolution...")

    result = differential_evolution(
        evaluate_filter,
        bounds,
        maxiter=args.maxiter,
        popsize=args.popsize,
        mutation=(0.5, 1.0),
        recombination=0.7,
        polish=False,
        workers=1,
        x0=center,
    )

    print("\n================ END OF RUN ====================")
    print(f"Best params: {np.round(best_params, 6)}")
    print(f"Best score: {best_score}")
    print(f"Best message count: {best_total}")
    print(f"Best DF17 count: {best_df17}")
    print("Best taps:")
    print(np.round(best_taps, 8))
    print("===============================================\n")

    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    with open(LOGFILE, "a") as f:
        f.write("# ==================== END OF RUN ====================\n")
        f.write(f"# Time: {timestamp}\n")
        f.write(f"# Instance: {DATA_PATH}\n")
        f.write(f"# FS: {FS/1_000_000} MHz → FS_UP: {FS_UP/1_000_000} MHz\n")
        f.write(f"# Number of taps: {NUMTAPS}\n")
        f.write(f"# Best params: {best_params.tolist()}\n")
        f.write(f"# Best score: {best_score}\n")
        f.write(f"# Best message count: {best_total}\n")
        f.write(f"# Best DF17 count: {best_df17}\n")
        f.write("# Current bounds:\n")
        for lo, hi in bounds:
            f.write(f"# [{lo:.6f}, {hi:.6f}]\n")
        f.write("# Best taps:\n")
        for t in best_taps:
            f.write(f"{t}\n")
        f.write("\n")

    energies = result.population_energies
    pop = result.population
    idx_sorted = np.argsort(energies)

    best = pop[idx_sorted[0]]
    second = pop[idx_sorted[1]]

    alpha = args.alpha
    margins = alpha * np.abs(best - second)
    margins = np.clip(margins, 0.05, 0.5)

    bounds = []
    for i in range(K):
        low = max(GAIN_MIN, best[i] - margins[i])
        high = min(GAIN_MAX, best[i] + margins[i])
        bounds.append((low, high))

    print("# Per-parameter margins:", margins)

    center = best_params.copy()
