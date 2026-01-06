#!/usr/bin/env python3
import numpy as np
import subprocess
import argparse
from scipy.optimize import differential_evolution
import threading
import os


############################################################
# Global checkpoint state
############################################################

best_lock = threading.Lock()
best_score = float("inf")     # DE minimizes → lower = better
best_params = None


############################################################
# FIR generator: windowed-sinc low-pass
############################################################

def lowpass_fir(cutoff, fs, taps=33):
    """
    Simple windowed-sinc low-pass FIR.
    cutoff: cutoff frequency in Hz
    fs:     sample rate in Hz
    taps:   number of taps (odd recommended)
    """
    fc = cutoff / (fs / 2.0)

    n = np.arange(taps) - (taps - 1) / 2.0
    h = np.sinc(fc * n)
    h *= np.hamming(taps)
    h /= np.sum(h)

    return h.astype(np.float32)


############################################################
# Load magnitude file
############################################################

def load_mag(filename):
    return np.fromfile(filename, dtype=np.float32)


############################################################
# Build 33-tap linear-phase FIR from 17 parameters
############################################################

def build_linear_phase_fir(p):
    """
    p: 17 parameters
    returns: 33-tap symmetric FIR
    """
    p = np.asarray(p, dtype=np.float32)
    left = p[:-1]          # 16 values
    center = p[-1]         # 1 value
    right = left[::-1]     # mirror
    h = np.concatenate([left, [center], right])
    return h


############################################################
# Objective function: maximize DF-17 messages
############################################################

def objective(p, mag):
    global best_score, best_params

    # Build symmetric FIR
    h = build_linear_phase_fir(p)

    # Normalize
    s = np.sum(h)
    if s == 0:
        return 0.0
    h /= s

    # Filter magnitude
    filtered = np.convolve(mag, h, mode="same").astype(np.float32)

    # Run stream1090
    p_stream = subprocess.Popen(
        ["../build/stream1090"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE
    )
    out, _ = p_stream.communicate(input=filtered.tobytes())

    # Count DF-17 messages
    df17 = 0
    for line in out.splitlines():
        if line.startswith(b"@") and line[13:15] == b"8d":
            df17 += 1

    score = -float(df17)

    # --- NEW: print score every time ---
    print(f"[score] DF17={df17}  score={score}")

    # --- Best-so-far checkpoint ---
    with best_lock:
        if score < best_score:
            best_score = score
            best_params = np.array(p, dtype=np.float32)

            np.savetxt("best_params.tmp", best_params)

            best_h = build_linear_phase_fir(best_params)
            best_h /= np.sum(best_h)
            np.savetxt("best_filter.tmp", best_h)

            with open("best_score.tmp", "w") as f:
                f.write(str(best_score))

            print(f"[checkpoint] New best DF17 score: {score}")

    return score


############################################################
# DE callback: save population each generation
############################################################

def save_checkpoint(population, convergence):
    # Save current population
    np.save("checkpoint_population.npy", population)

    # Save generation counter
    with open("checkpoint_generation.txt", "w") as f:
        f.write(str(save_checkpoint.generation))

    print(f"[checkpoint] Saved generation {save_checkpoint.generation}")
    save_checkpoint.generation += 1

save_checkpoint.generation = 0


############################################################
# Main
############################################################

def main():
    global best_score, best_params

    ap = argparse.ArgumentParser(
        description="Optimize 33-tap linear-phase FIR for ADS-B magnitude stream"
    )
    ap.add_argument("--infile", required=True,
                    help="Input magnitude file (float32)")
    ap.add_argument("--fs-mag", type=float, required=True,
                    help="Magnitude sample rate in Hz")
    ap.add_argument("--maxiter", type=int, default=30,
                    help="Number of optimization iterations")
    ap.add_argument("--start", help="Optional starting filter file (33 floats, text)")
    ap.add_argument("--popsize", type=int, default=15,
                    help="Population size multiplier (DE pop = popsize * dim)")
    args = ap.parse_args()

    print("Loading magnitude...")
    mag = load_mag(args.infile)
    fs_mag = args.fs_mag

    dim = 17  # 16 symmetric taps + center

    ########################################################
    # Try to resume from checkpoint
    ########################################################
    resume = False
    if os.path.exists("checkpoint_population.npy") and \
       os.path.exists("best_params.tmp") and \
       os.path.exists("best_score.tmp"):
        try:
            init_pop = np.load("checkpoint_population.npy")

            best_params_loaded = np.loadtxt("best_params.tmp").astype(np.float32)
            with open("best_score.tmp") as f:
                best_score_loaded = float(f.read().strip())

            if not np.isfinite(best_score_loaded):
                print("Warning: invalid best_score in checkpoint, resetting.")
                best_score_loaded = float("inf")

            best_params = best_params_loaded
            best_score = best_score_loaded

            if os.path.exists("checkpoint_generation.txt"):
                with open("checkpoint_generation.txt") as f:
                    save_checkpoint.generation = int(f.read().strip())
            else:
                save_checkpoint.generation = 0

            print("Resuming from checkpoint...")
            resume = True

        except Exception as e:
            print("Checkpoint found but failed to load:", e)
            print("Starting fresh.")
            resume = False

    ########################################################
    # Build starting population if not resuming
    ########################################################
    if not resume:
        if args.start:
            print(f"Loading starting filter from {args.start} ...")
            h0 = np.loadtxt(args.start).astype(np.float32)
            if len(h0) != 33:
                raise ValueError("Starting filter must have 33 coefficients")
            # Extract 17 parameters from symmetric 33-tap FIR
            p0 = np.concatenate([h0[:16], [h0[16]]])
        else:
            print("Using 2 MHz low-pass FIR as starting solution")
            h0 = lowpass_fir(cutoff=2e6, fs=fs_mag, taps=33)
            # Extract 17 parameters (left half + center)
            p0 = np.concatenate([h0[:16], [h0[16]]])

        print("Initial 17 parameters (from warm start):")
        print(p0)

        # Reset best score/params for a fresh run
        best_score = float("inf")
        best_params = None

        ####################################################
        # Optimization bounds and initial population
        ####################################################
        bounds = [(-1.0, 1.0)] * dim

        pop_mult = args.popsize
        npop = max(5, pop_mult * dim)

        rng = np.random.default_rng()
        init_pop = np.empty((npop, dim), dtype=np.float32)

        # First member = warm start
        init_pop[0, :] = p0

        # Remaining members = small perturbations of p0
        perturb_scale = 0.02  # ~2% noise

        for i in range(1, npop):
            noise = rng.normal(loc=0.0, scale=perturb_scale, size=dim)
            candidate = p0 + noise
            candidate = np.clip(candidate, -1.0, 1.0)
            init_pop[i, :] = candidate

        print(f"Population size: {npop}")
    else:
        # Bounds still needed for DE call
        bounds = [(-1.0, 1.0)] * dim

    ########################################################
    # Run differential evolution
    ########################################################
    print("Starting optimization...")
    result = differential_evolution(
        lambda p: objective(p, mag),
        bounds=bounds,
        maxiter=args.maxiter,
        polish=False,
        init=init_pop,
        workers=1,
        recombination=0.2,
        disp=True,
        callback=save_checkpoint
    )

    print("\nOptimization complete.")
    print("Best 17 parameters (from DE result):")
    print(result.x)

    # Build full 33-tap filter from DE result
    best_h = build_linear_phase_fir(result.x)
    best_h /= np.sum(best_h)

    print("\nBest 33-tap FIR coefficients:")
    print(best_h)

    np.savetxt("best_filter.txt", best_h)
    print("\nSaved best filter to best_filter.txt")


if __name__ == "__main__":
    main()
