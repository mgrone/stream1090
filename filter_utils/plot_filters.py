#!/usr/bin/env python3
import numpy as np
import argparse
import matplotlib.pyplot as plt
from scipy.signal import freqz


def load_filter(filename):
    """Load a 31‑tap FIR filter from a text file."""
    h = np.loadtxt(filename).astype(np.float32)
    if len(h) != 31:
        raise ValueError(f"{filename} does not contain 31 coefficients")
    return h


def main():
    ap = argparse.ArgumentParser(description="Plot and compare FIR impulse responses")
    ap.add_argument("filters", nargs="+",
                    help="List of filter coefficient files (each 31 floats)")
    ap.add_argument("--fs", type=float, default=None,
                    help="Sample rate (Hz) for frequency response")
    args = ap.parse_args()

    plt.figure(figsize=(12, 5))

    ############################################################
    # Impulse responses
    ############################################################
    plt.subplot(1, 2, 1)
    for fname in args.filters:
        h = load_filter(fname)
        plt.plot(h, marker="o", label=fname)

    plt.title("Impulse Responses (31 taps)")
    plt.xlabel("Tap index")
    plt.ylabel("Amplitude")
    plt.grid(True)
    plt.legend()

    ############################################################
    # Frequency responses
    ############################################################
    plt.subplot(1, 2, 2)
    for fname in args.filters:
        h = load_filter(fname)
        w, H = freqz(h, worN=2048)

        if args.fs:
            freqs = w * args.fs / (2 * np.pi)
            plt.plot(freqs, 20 * np.log10(np.abs(H) + 1e-12), label=fname)
            plt.xlabel("Frequency (Hz)")
        else:
            plt.plot(w / np.pi, 20 * np.log10(np.abs(H) + 1e-12), label=fname)
            plt.xlabel("Normalized frequency (×π rad/sample)")

    plt.title("Frequency Responses")
    plt.ylabel("Magnitude (dB)")
    plt.grid(True)
    plt.legend()

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
