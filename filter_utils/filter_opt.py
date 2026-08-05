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
import multiprocessing
import os
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

    p.add_argument("--num-taps", type=int, default=15,
                   help="Number of FIR taps")

    p.add_argument("--num-gain-points", type=int, default=7,
                   help="Number of interior gain points")

    p.add_argument("--margin", type=float, default=0.5,
                   help="Margin around center seed or resume vector")

    p.add_argument("--log", default="de_log.txt",
                   help="Log file path")

    p.add_argument("--maxiter", type=int, default=20)
    p.add_argument("--popsize", type=int, default=10)
    p.add_argument("--alpha", type=float, default=2.0)

    p.add_argument("--bounds-min", type=float, default=-2.0)
    p.add_argument("--bounds-max", type=float, default=2.0)

    p.add_argument("--resume",
                   help="Path to previous log file to resume from (uses best_params as new center)")
    p.add_argument("--resume-bounds", action="store_true",
                   help="When resuming, also reuse bounds from the log instead of recomputing from margin")

    p.add_argument("--df17-weight", type=float, default=1.0,
                   help="Weight for DF17 messages in the objective function")

    p.add_argument("--patience", type=int, default=3,
                   help="Stop after this many consecutive runs that fail to "
                        "improve the best score. 0 never stops, which is what "
                        "this script used to do")

    p.add_argument("--max-runs", type=int, default=0,
                   help="Hard cap on the number of runs, 0 for no cap")

    p.add_argument("--workers", type=int, default=1,
                   help="Parallel evaluations (-1 for all cores). Each one runs "
                        "a full stream1090 pass, so this scales close to linearly "
                        "until the disk gives out")

    return p.parse_args()


args = parse_args()

# ============================================================
#  Config
# ============================================================

STREAM1090_EXE = "../build/stream1090"
DATA_PATH = args.data


def filter_path():
    """Scratch file holding the taps for one evaluation.

    Per process, because with --workers several evaluations are in flight at
    once and a single shared path would have them overwrite each other's taps
    between the write and the run.
    """
    return f"./diff_evolve_fir_temp_{os.getpid()}.txt"
FS = args.fs
FS_UP = args.fs_up
NUMTAPS = args.num_taps
K = args.num_gain_points

# Dynamic center seed: smooth low-pass shape
CENTER_SEED = np.linspace(1.0, 0.0, K + 2)[1:-1].astype(np.float64)
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
BOUNDS_RE = re.compile(r"# \[([0-9eE+\-\.]+),\s*([0-9eE+\-\.]+)\]")

def _parse_float_list(s: str):
    return [float(x.strip()) for x in s.split(",") if x.strip()]

def load_best_params_and_bounds_from_log(path: str):
    best_params = None
    bounds = []

    with open(path, "r") as f:
        lines = f.readlines()

    for i, line in enumerate(lines):
        m = PARAMS_RE.match(line)
        if m:
            best_params = _parse_float_list(m.group(1))
            bounds = []
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

    freq = np.linspace(0.0, 1.0, K + 2)
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
#  Output parsing (unified one-pass parser)
# ============================================================

HEX = set(b"0123456789ABCDEFabcdef")

def is_hex(s: bytes) -> bool:
    return all(c in HEX for c in s)


def parse_frames(out: bytes):
    """
    One-pass parser:
    - total messages
    - long_count: number of 112-bit frames
    - df_counts: dict DF -> count
    """
    total = 0
    long_count = 0
    df_counts = {}

    for line in out.splitlines():
        if not line or line[0] not in (ord('@'), ord('<')) or not line.endswith(b";"):
            continue

        payload = line[1:-1]

        # Strip MLAT prefix
        if line[0] == ord('<'):
            frame_hex = payload[14:]
        else:
            frame_hex = payload[12:]

        # Validate hex
        if not is_hex(frame_hex):
            continue

        total += 1

        # Long frame = 112 bits = 28 hex chars
        if len(frame_hex) == 28:
            long_count += 1

        # Extract DF (first 5 bits)
        try:
            bits = bin(int(frame_hex, 16))[2:].zfill(len(frame_hex) * 4)
            df = int(bits[:5], 2)
            df_counts[df] = df_counts.get(df, 0) + 1
        except Exception:
            pass

    return total, long_count, df_counts


# ============================================================
#  Best-so-far tracking
# ============================================================

best_score = -np.inf
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


def score_taps(h):
    """Run stream1090 once over the data with these taps and score the result."""
    path = filter_path()
    with open(path, "w") as f:
        for t in h:
            f.write(f"{t}\n")

    input_mhz = FS / 1_000_000.0

    if FS_UP is not None:
        output_mhz = FS_UP / 1_000_000.0
    else:
        output_mhz = default_output_rate(input_mhz)

    cmd = [
        "bash", "-c",
        f"cat {DATA_PATH} | {STREAM1090_EXE} "
        f"-s {input_mhz} "
        f"-u {output_mhz} "
        f"-f {path}"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out, _ = proc.communicate()

    total, long_count, df_counts = parse_frames(out)
    df17 = df_counts.get(17, 0)
    score = total + long_count
    return score, total, df17


def evaluate_builtin_filter():
    input_mhz = FS / 1_000_000.0

    if FS_UP is not None:
        output_mhz = FS_UP / 1_000_000.0
    else:
        output_mhz = default_output_rate(input_mhz)

    cmd = [
        "bash", "-c",
        f"cat {DATA_PATH} | {STREAM1090_EXE} "
        f"-s {input_mhz} "
        f"-u {output_mhz} "
        f"-q"
    ]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out, _ = proc.communicate()

    total, long_count, df_counts = parse_frames(out)
    df17 = df_counts.get(17, 0)
    # score = total + args.df17_weight * df17
    score = total + long_count
    # score = total + df_counts.get(17, 0)
    #score = df_counts.get(17, 0) + df_counts.get(11, 0) * 0.25

    return score, total, df17


def evaluate_filter(params):
    global best_score, best_params, best_taps, best_total, best_df17, bounds

    h, freq, gain = build_lowpass_firwin2(params, K)

    if not is_lowpass(h):
        return 1e9

    score, total, df17 = score_taps(h)

    # With --workers the evaluation runs in a child process, so updating these
    # globals here would only touch that child's copy and be thrown away. The
    # parent recovers the winner from the optimiser's result instead. Still
    # report each result, otherwise a parallel run prints nothing for hours.
    if args.workers != 1:
        print(f"[{os.getpid()}] total={total}, df17={df17}, score={score}",
              flush=True)
        return -score

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

if args.resume:
    resume_center, resume_bounds = load_best_params_and_bounds_from_log(args.resume)

    if args.resume_bounds and resume_bounds:
        center = resume_center
        bounds = resume_bounds
    else:
        center = resume_center
        bounds = make_bounds_around_center(center, args.margin)
else:
    center = CENTER_SEED.copy()
    bounds = make_bounds_around_center(center, CENTER_SEED_MARGIN)

baseline_score, baseline_total, baseline_df17 = evaluate_builtin_filter()
# best_score = baseline_score
# best_total = baseline_total
# best_df17 = baseline_df17
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

def log_improvement():
    """Append the current best, in the format --resume reads back."""
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


def on_generation(intermediate_result):
    """Record the best after each generation when running in parallel.

    The workers cannot do this themselves, so without it a parallel run would
    write nothing until it finished and a crash would lose everything. scipy
    calls this in the parent, once per generation, which is cheap next to the
    generation itself.
    """
    global best_score, best_params, best_taps, best_total, best_df17

    score = -intermediate_result.fun
    if score <= best_score:
        return

    best_params = np.asarray(intermediate_result.x).copy()
    best_taps = build_lowpass_firwin2(best_params, K)[0]
    # fun carries the score but not the breakdown the log wants
    best_score, best_total, best_df17 = score_taps(best_taps)
    print(f"| generation improved: score={best_score}, total={best_total}, "
          f"df17={best_df17}", flush=True)
    log_improvement()


def open_pool(n):
    """Worker pool for parallel evaluation, or None to stay in this process.

    Forced onto the fork start method: this script does its work at module
    level, so under spawn every worker would re-import it and start its own
    optimisation run.
    """
    if n == 1:
        return None
    if n < 0:
        n = multiprocessing.cpu_count()
    return multiprocessing.get_context("fork").Pool(n)


pool = open_pool(args.workers)

runs = 0
stale = 0

while True:
    runs += 1
    score_before = best_score
    print("Starting Differential Evolution...")

    result = differential_evolution(
        evaluate_filter,
        bounds,
        maxiter=args.maxiter,
        popsize=args.popsize,
        mutation=(0.5, 1.0),
        recombination=0.7,
        polish=False,
        # a parallel map has to score a whole generation before any of it can
        # be used, so the population is updated once per generation
        workers=pool.map if pool else 1,
        updating="deferred" if pool else "immediate",
        callback=on_generation if pool else None,
        x0=center,
    )

    if pool and -result.fun > best_score:
        # the winner was scored in a child, so its taps and counts never made
        # it back here; recompute them once from the parameters that won
        best_params = np.asarray(result.x)
        best_taps = build_lowpass_firwin2(best_params, K)[0]
        best_score, best_total, best_df17 = score_taps(best_taps)

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
    margins = np.clip(margins, 0.025, 0.5)

    bounds = []
    for i in range(K):
        low = max(GAIN_MIN, best[i] - margins[i])
        high = min(GAIN_MAX, best[i] + margins[i])
        bounds.append((low, high))

    print("# Per-parameter margins:", margins)

    center = best_params.copy()

    # Each run narrows the bounds around the current best, so once a few of
    # them in a row bring nothing the schedule has stopped finding anything and
    # the remaining ones are just spending evaluations. The objective is
    # deterministic for a given tap set and data, so "no improvement" is exact
    # rather than a noise threshold.
    stale = 0 if best_score > score_before else stale + 1
    print(f"# Run {runs}: best {best_score}, "
          f"{'improved' if stale == 0 else f'stale for {stale}'}")

    if args.patience and stale >= args.patience:
        print(f"\nConverged: {stale} run{'' if stale == 1 else 's'} without "
              "improvement, stopping.")
        break

    if args.max_runs and runs >= args.max_runs:
        print(f"\nReached the cap of {args.max_runs} "
              f"run{'' if args.max_runs == 1 else 's'}, stopping.")
        break

if pool:
    pool.close()
    pool.join()

print(f"\nFinished after {runs} run{'' if runs == 1 else 's'}. "
      f"Best score {best_score} "
      f"({best_total} messages, {best_df17} DF17).")
print(f"Best taps written to {LOGFILE}")
