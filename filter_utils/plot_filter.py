#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2025 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#


import numpy as np
import argparse
import matplotlib.pyplot as plt
from scipy.signal import freqz


def load_filter(filename):
    """Load a FIR filter from a text file and normalize it."""
    h = np.loadtxt(filename).astype(np.float32)

    s = np.sum(h)
    if s == 0:
        raise ValueError("Loaded filter has zero sum and cannot be normalized")

    h /= s
    return h



def main():
    ap = argparse.ArgumentParser(description="Plot and compare FIR impulse responses")
    ap.add_argument("filters", nargs="+",
                    help="List of filter coefficient files")
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

    plt.title("Impulse Responses")
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
