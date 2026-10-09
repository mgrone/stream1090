#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#
"""Find firwin2 gain points that reproduce a given filter response.

For a fixed frequency grid and window, scipy.signal.firwin2 is LINEAR in the
gains: it interpolates the gains onto a dense grid, applies a fixed linear
phase, takes an inverse FFT and multiplies by a fixed window, and every one
of those steps is linear. So the taps are  taps = T @ gains  for a matrix T
that depends only on (num_taps, grid, window). T is built by designing one
filter per unit gain vector.

The resulting taps are symmetric, so their frequency response is a real
amplitude A(w) = sum_n taps[n] * cos(w * (n - (num_taps-1)/2)) times a pure
delay. A(w) is therefore also linear in the gains, and fitting the target's
magnitude response becomes a bounded linear least-squares problem, which has
one global optimum and is solved exactly (no stochastic search needed).

The fit compares MAGNITUDE responses, not taps sample by sample: a target
with a different length or delay than the firwin2 design can't be compared
tap by tap, and firwin2 always produces linear phase anyway.

Caveats
  * A design with M taps has only ceil(M/2) independent taps, so more gain
    points than that cannot all be told apart (15 taps -> at most 8). The
    least-squares problem is then rank deficient. A tiny pull toward the
    target's own magnitude at each grid frequency (`regularization`) picks
    the one solution closest to it, so the result is deterministic.
  * The fit minimizes the signed amplitude A(w). Where the designed
    amplitude dips below zero (stopband side lobes) the true magnitude |A|
    differs from it; `rms_error` is always computed on the true magnitude.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from typing import Optional

import numpy as np
from scipy.optimize import lsq_linear
from scipy.signal import firwin2, freqz

# Same bounds the optimizer uses for a gain (GAIN_MIN, GAIN_MAX).
DEFAULT_GAIN_BOUNDS = (0.0, 2.0)
DENSE_POINTS = 2048  # response resolution used when the target is given as taps


@dataclass
class GainFit:
    gains: np.ndarray    # fitted gains, one per grid point
    freq: np.ndarray     # the grid they belong to (normalized, 0 = DC, 1 = Nyquist)
    taps: np.ndarray     # firwin2(num_taps, freq, gains, window=window)
    rms_error: float     # weighted RMS of |H_fit| - |H_target| on the target grid


def _check_grid(freq: np.ndarray, n: int) -> None:
    if len(freq) != n:
        raise ValueError(f"freq has {len(freq)} points, expected num_gain_points={n}")
    if not (np.isclose(freq[0], 0.0) and np.isclose(freq[-1], 1.0)
            and np.all(np.diff(freq) > 0)):
        raise ValueError("freq must increase strictly from exactly 0 to exactly 1")


def fit_gain_points_to_response(
    w: np.ndarray,
    h: np.ndarray,
    num_gain_points: int,
    window,
    num_taps: int,
    *,
    freq: Optional[np.ndarray] = None,
    weights: Optional[np.ndarray] = None,
    gain_bounds: tuple[float, float] = DEFAULT_GAIN_BOUNDS,
    regularization: float = 1e-6,
) -> GainFit:
    """Fit gain points to a frequency response as returned by scipy's freqz.

    w, h             freqz output: w in rad/sample (0..pi), h complex response.
    num_gain_points  number of gain points N to find.
    window           anything firwin2 accepts ('boxcar', 'hamming', ...).
    num_taps         number of taps of the firwin2 design.
    freq             normalized grid of the N gain points (strictly increasing,
                     exactly 0 .. 1). Default: N evenly spaced points. With the
                     optimizer's grid, pass make_freq_grid(cutoff_ratio, N).
    weights          optional weight per target frequency (default: all equal).
    gain_bounds      (lo, hi) limit for every gain.
    regularization   weight of the pull toward the target magnitude; only
                     matters along directions the taps cannot see.
    """
    n = num_gain_points
    if n < 2:
        raise ValueError("need at least 2 gain points")
    freq = np.linspace(0.0, 1.0, n) if freq is None else np.asarray(freq, dtype=np.float64)
    _check_grid(freq, n)

    w = np.asarray(w, dtype=np.float64)
    target = np.abs(np.asarray(h))
    x = w / np.pi
    if len(x) != len(target) or len(x) < 2 or np.any(np.diff(x) <= 0) \
            or x[0] < -1e-12 or x[-1] > 1 + 1e-12:
        raise ValueError("w must be increasing within 0..pi and match the length of h")

    wts = np.ones_like(x) if weights is None else np.asarray(weights, dtype=np.float64)
    if wts.shape != x.shape or np.any(wts < 0):
        raise ValueError("weights must be non-negative, one per target frequency")
    wts = wts / np.mean(wts)

    # Even tap counts are Type II filters: firwin2 requires zero gain at Nyquist.
    free = np.ones(n, dtype=bool)
    if num_taps % 2 == 0:
        free[-1] = False

    # taps = T @ gains, one column per unit gain vector
    taps_basis = np.zeros((num_taps, n))
    for j in np.flatnonzero(free):
        taps_basis[:, j] = firwin2(num_taps, freq, np.eye(n)[j], window=window)

    # amplitude(w_k) = sum_n taps[n] * cos(w_k * (n - delay))   (taps are symmetric)
    delay = (num_taps - 1) / 2.0
    cosines = np.cos(w[:, None] * (np.arange(num_taps)[None, :] - delay))
    amp = cosines @ taps_basis                      # (len(w), n)

    prior = np.interp(freq, x, target)              # target magnitude at each grid point
    root_w = np.sqrt(wts)[:, None]
    a = np.vstack([root_w * amp, np.sqrt(regularization) * np.eye(n)])[:, free]
    b = np.concatenate([root_w[:, 0] * target, np.sqrt(regularization) * prior])

    res = lsq_linear(a, b, bounds=gain_bounds, method="bvls", verbose=2)
    if not res.success:
        raise RuntimeError(f"least-squares fit failed: {res.message}")

    gains = np.zeros(n)
    gains[free] = res.x

    taps = firwin2(num_taps, freq, gains, window=window)
    _, h_fit = freqz(taps, worN=w)
    rms = float(np.sqrt(np.mean(wts * (np.abs(h_fit) - target) ** 2)))
    return GainFit(gains=gains, freq=freq, taps=taps, rms_error=rms)


def fit_gain_points(
    impulse_response: np.ndarray,
    num_gain_points: int,
    window,
    *,
    num_taps: Optional[int] = None,
    **kwargs,
) -> GainFit:
    """Fit gain points to an impulse response (a filter's taps).

    The magnitude response of impulse_response (via freqz) is the target.
    num_taps defaults to len(impulse_response); set it to approximate a
    longer filter with a shorter firwin2 design. Remaining keyword arguments
    (freq, weights, gain_bounds, regularization) are as in
    fit_gain_points_to_response.
    """
    taps = np.asarray(impulse_response, dtype=np.float64)
    if taps.ndim != 1 or len(taps) < 2:
        raise ValueError("impulse_response must be a 1-D array of taps")
    w, h = freqz(taps, worN=DENSE_POINTS)
    return fit_gain_points_to_response(
        w, h, num_gain_points, window,
        len(taps) if num_taps is None else num_taps, **kwargs)

def load_filter(filename) -> np.ndarray:
    """Load a FIR filter from a text file and normalize it."""
    h = np.loadtxt(filename).astype(np.float32)
    return h

def main():
    args = argparse.ArgumentParser(description="Filter converter")
    args.add_argument("--data", required=True,
                   help="Path taps file")
    
    args = args.parse_args()

    taps = load_filter(args.data)
    gf = fit_gain_points(taps, 9, "boxcar")
    print(f"# Best freq: {gf.freq.tolist()}")
    print(f"# Best params: {gf.gains.tolist()}")
    print(gf.rms_error)
if __name__ == "__main__":
    main()