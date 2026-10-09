#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#

from __future__ import annotations

import argparse
import re
import subprocess
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Optional

import numpy as np
from scipy.optimize import differential_evolution
from scipy.signal import firwin2

STREAM1090_EXE = "../build/stream1090"
FILTER_PATH = Path("./diff_evolve_fir_temp.txt")

# (input_mhz, default_output_mhz) pairs used when --fs-up is not given.
DEFAULT_RATE_PAIRS = [
    (2.4, 8.0),
    (6.0, 6.0),
    (10.0, 10.0),
]

GAIN_MIN = 0.0
GAIN_MAX = 2.0
MIN_GAIN_POINTS = 3

# Used when neither the command line nor a resume log provides a value.
DEFAULT_NUM_TAPS = 15
DEFAULT_NUM_GAIN_POINTS = 9
DEFAULT_CUTOFF_HZ = 2_000_000

# Starting shape for a fresh run (no --resume): (frequency in Hz, gain) pairs.
# This has been taken from 10 Msps run. It seems that most devices and sample rates all follow
# the same pattern in 0.. 2MHz
TEMPLATE_FS   = 10.00e6
TEMPLATE_FREQ = [0.0, 0.05714285714285715, 0.1142857142857143, 0.17142857142857143, 0.2285714285714286, 0.28571428571428575, 0.34285714285714286, 0.4, 1.0]
TEMPLATE_GAIN = [1.124463044844195, 1.3693865949524513, 0.20277441235276722, 1.0223461972779218, 0.5923711968688659, 0.9536190054362723, 0.07359419057156419, 0.0, 0.0]

# Convert this to pairs and frequency to baseband
TEMPLATE =  [
                [
                    float(f * (TEMPLATE_FS / 2.0)),
                    float(g)
                ]
                for f, g in zip(TEMPLATE_FREQ, TEMPLATE_GAIN)
            ]

TEMPLATE_TOL_HZ = 1.0  # grid points within this of the last entry still count as on it

# 'kaiser' is deliberately not listed: firwin2 rejects the plain string
# (ValueError: 'kaiser' must have parameters) because it needs a beta.
WINDOW_MODES = ("hamming", "hann", "blackman", "boxcar", "bartlett")


# ============================================================
#  CLI
# ============================================================

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Differential Evolution FIR optimizer for stream1090"
    )

    p.add_argument("--data", required=True,
                   help="Path to raw IQ sample file")
    p.add_argument("--fs", type=int, required=True,
                   help="Input sample rate (Hz)")
    p.add_argument("--fs-up", type=int, required=True,
                   help="Upsampled rate (Hz)")

    # These three default to None so a resume log can supply them:
    # command line > resume log > built-in default.
    p.add_argument("--num-taps", type=int, default=None,
                   help=f"Number of FIR taps (default: from --resume log, "
                        f"else {DEFAULT_NUM_TAPS})")
    p.add_argument("--num-gain-points", type=int, default=None,
                   help=f"Number of gain points on the frequency grid, "
                        f"including DC and Nyquist (default: from --resume "
                        f"log, else {DEFAULT_NUM_GAIN_POINTS}). If it differs "
                        f"from the log, the logged gains are interpolated.")
    p.add_argument("--cutoff", type=float, default=None,
                   help=f"Cutoff frequency in Hz, capped at fs/2 (default: from "
                        f"--resume log, else {DEFAULT_CUTOFF_HZ:,})")
    p.add_argument("--window", choices=WINDOW_MODES, default=None,
                   help="Window for firwin2 (default: from --resume log, else "
                        "hamming if the cutoff is fs/2, boxcar otherwise)")

    p.add_argument("--margin", type=float, default=0.2,
                   help="Margin around center seed or resume vector")
    p.add_argument("--log", default="de_log.txt",
                   help="Log file path")
    p.add_argument("--maxiter", type=int, default=5)
    p.add_argument("--popsize", type=int, default=5)
    p.add_argument("--alpha", type=float, default=2.0)
    p.add_argument("--resume",
                   help="Path to previous log file to resume from "
                        "(uses the most recent best_params as new center)")
    p.add_argument("--resume-bounds", action="store_true",
                   help="When resuming, reuse the logged bounds instead "
                        "of regenerating a fresh margin around best_params")

    return p.parse_args()


# ============================================================
#  Resuming from a log
# ============================================================

BLOCK_START = "# ====="
PARAMS_RE = re.compile(r"# Best params:\s*\[(.*)\]")
FREQ_RE = re.compile(r"# Best freq:\s*\[(.*)\]")
BOUNDS_RE = re.compile(r"# \[([0-9eE+\-.]+),\s*([0-9eE+\-.]+)\]")
TAPS_RE = re.compile(r"# Number of taps:\s*(\d+)")
CUTOFF_RE = re.compile(r"# Cutoff:\s*([0-9eE+\-.]+)")
WINDOW_RE = re.compile(r"# Window mode:\s*(\S+)")


@dataclass
class ResumeData:
    """Everything recovered from one block of a previous log. Optional
    fields are None when the block didn't have a valid value for them."""

    params: np.ndarray
    num_taps: Optional[int]
    cutoff_hz: Optional[float]
    window_mode: Optional[str]
    freq: Optional[np.ndarray]
    bounds: Optional[list[tuple[float, float]]]


def _parse_float_list(s: str) -> list[float]:
    return [float(x.strip()) for x in s.split(",") if x.strip()]


def _valid_freq_grid(freq: np.ndarray) -> bool:
    """firwin2 needs a strictly increasing grid running from exactly 0 to 1."""
    return (
        len(freq) >= 2
        and bool(np.isclose(freq[0], 0.0))
        and bool(np.isclose(freq[-1], 1.0))
        and bool(np.all(np.diff(freq) > 0))
    )


def _parse_block(block: list[str]) -> Optional[ResumeData]:
    params = freq = num_taps = cutoff_hz = window_mode = None
    bounds: list[tuple[float, float]] = []

    for line in block:
        if (m := PARAMS_RE.match(line)):
            params = np.array(_parse_float_list(m.group(1)), dtype=np.float64)
        elif (m := FREQ_RE.match(line)):
            freq = np.array(_parse_float_list(m.group(1)), dtype=np.float64)
        elif (m := TAPS_RE.match(line)):
            num_taps = int(m.group(1))
        elif (m := CUTOFF_RE.match(line)):
            cutoff_hz = float(m.group(1))
        elif (m := WINDOW_RE.match(line)):
            window_mode = m.group(1)
        elif (m := BOUNDS_RE.match(line)):
            bounds.append((float(m.group(1)), float(m.group(2))))

    if params is None or len(params) == 0:
        return None

    # Drop anything inconsistent with the params rather than half-trusting it.
    if freq is not None and (len(freq) != len(params) or not _valid_freq_grid(freq)):
        print("[resume] Logged frequency grid is invalid for this block "
              "(wrong length, not 0..1, or not increasing) - ignoring it.")
        freq = None
    if bounds and len(bounds) != len(params):
        bounds = []
    if cutoff_hz is not None and cutoff_hz <= 0:
        cutoff_hz = None
    if window_mode is not None and window_mode not in WINDOW_MODES:
        print(f"[resume] Logged window '{window_mode}' is not supported - ignoring it.")
        window_mode = None

    return ResumeData(params, num_taps, cutoff_hz, window_mode, freq, bounds or None)


def load_resume_data(path: str) -> ResumeData:
    """Parse a log into blocks (each starts at a '# =====' line) and return
    the LAST block that contains '# Best params:'. Every field comes from
    that one block, so params, freq, bounds, taps and window always belong
    together."""
    blocks: list[list[str]] = []
    for line in Path(path).read_text().splitlines():
        if line.startswith(BLOCK_START):
            blocks.append([])
        elif blocks:
            blocks[-1].append(line)

    for block in reversed(blocks):
        data = _parse_block(block)
        if data is not None:
            return data

    raise ValueError(f"No block with 'Best params' found in log {path}")


# ============================================================
#  Configuration (static for the whole run)
# ============================================================

def _resolve(name: str, cli, logged, default):
    """command line > resume log > default; prints where the value came from."""
    if cli is not None:
        value, source = cli, "command line"
    elif logged is not None:
        value, source = logged, "resume log"
    else:
        value, source = default, "default"
    print(f"[config] {name} = {value}  ({source})")
    return value


def default_window_mode(cutoff_ratio: float) -> str:
    """Hamming works better when the response has to taper all the way to
    Nyquist (cutoff == fs/2); boxcar works better when the cutoff is well
    below Nyquist. Anything short of exactly fs/2 counts as the latter."""
    # return "hamming" if cutoff_ratio == 1.0 else "boxcar"
    return "boxcar"


@dataclass(frozen=True)
class Config:
    """Fully resolved run configuration. Built once at startup (after the
    resume log, if any, has been read) and never modified afterwards."""

    data_path: str
    fs: int
    fs_up: int
    cutoff_hz: float
    window_mode: str
    num_taps: int
    num_gain_points: int
    margin: float
    logfile: str
    maxiter: int
    popsize: int
    alpha: float
    resume: Optional[str]
    resume_bounds: bool

    @property
    def nyquist_hz(self) -> float:
        return self.fs / 2

    @property
    def cutoff_ratio(self) -> float:
        return self.cutoff_hz / self.nyquist_hz

    @property
    def max_total_calls(self) -> int:
        return (self.maxiter + 1) * self.popsize * self.num_gain_points

    @property
    def fs_mhz(self) -> float:
        return self.fs / 1_000_000.0

    @property
    def fs_up_mhz(self) -> float:
        return self.fs_up / 1_000_000.0

    @classmethod
    def from_args(cls, args: argparse.Namespace, resume: Optional[ResumeData]) -> "Config":
        num_taps = _resolve(
            "num_taps", args.num_taps,
            resume.num_taps if resume else None, DEFAULT_NUM_TAPS)

        nyquist = args.fs / 2
        cutoff_hz = _resolve(
            "cutoff_hz", args.cutoff,
            resume.cutoff_hz if resume else None, DEFAULT_CUTOFF_HZ)
        if cutoff_hz <= 0:
            raise ValueError(f"cutoff must be > 0 Hz, got {cutoff_hz}")
        if cutoff_hz > nyquist:
            print(f"[config] cutoff {cutoff_hz} Hz is above Nyquist ({nyquist} Hz) "
                  f"- using Nyquist")
            cutoff_hz = nyquist

        window_mode = _resolve(
            "window", args.window,
            resume.window_mode if resume else None,
            default_window_mode(cutoff_hz / nyquist))
        num_gain_points = _resolve(
            "num_gain_points", args.num_gain_points,
            len(resume.params) if resume else None, DEFAULT_NUM_GAIN_POINTS)

        if num_gain_points < MIN_GAIN_POINTS:
            raise ValueError(f"num_gain_points must be >= {MIN_GAIN_POINTS}, "
                             f"got {num_gain_points}")

        return cls(
            data_path=args.data,
            fs=args.fs,
            fs_up=args.fs_up,
            cutoff_hz=cutoff_hz,
            window_mode=window_mode,
            num_taps=num_taps,
            num_gain_points=num_gain_points,
            margin=args.margin,
            logfile=args.log,
            maxiter=args.maxiter,
            popsize=args.popsize,
            alpha=args.alpha,
            resume=args.resume,
            resume_bounds=args.resume_bounds,
        )


# ============================================================
#  Initial search state (fresh or resumed)
# ============================================================

def make_freq_grid(cutoff_ratio: float, k: int) -> np.ndarray:
    """Normalized frequency grid with k points from 0.0 to 1.0 (Nyquist).
    If the cutoff is below Nyquist, k-1 points are spread over 0..cutoff and
    the last point sits at Nyquist."""
    if cutoff_ratio == 1.0:
        return np.linspace(0.0, 1.0, k)
    return np.concatenate((np.linspace(0.0, cutoff_ratio, k - 1), [1.0]))


def center_seed(freq: np.ndarray, nyquist_hz: float) -> np.ndarray:
    """Starting gains for a fresh run: TEMPLATE interpolated at the grid
    frequencies (normalized freq * nyquist_hz = Hz), and 0.0 for every grid
    point above the template's last frequency."""
    template_hz, template_gain = zip(*TEMPLATE)
    freq_hz = freq * nyquist_hz
    gains = np.interp(freq_hz, template_hz, template_gain)
    gains[freq_hz > template_hz[-1] + TEMPLATE_TOL_HZ] = 0.0
    #print(freq.tolist())
    #print(gains.tolist())
    return np.clip(gains, GAIN_MIN, GAIN_MAX)


def make_bounds_around_center(center: np.ndarray, margin: float) -> list[tuple[float, float]]:
    return [(max(GAIN_MIN, c - margin), min(GAIN_MAX, c + margin)) for c in center]


def remap_gain_vector(old_vals: np.ndarray, old_freq: np.ndarray, new_freq: np.ndarray) -> np.ndarray:
    """Linearly interpolate gains defined at old_freq onto new_freq."""
    return np.clip(np.interp(new_freq, old_freq, old_vals), GAIN_MIN, GAIN_MAX)


def remap_bounds(
    old_bounds: list[tuple[float, float]], old_freq: np.ndarray, new_freq: np.ndarray
) -> list[tuple[float, float]]:
    """Same interpolation, applied to the lower and upper bounds independently."""
    los = remap_gain_vector(np.array([lo for lo, _ in old_bounds]), old_freq, new_freq)
    his = remap_gain_vector(np.array([hi for _, hi in old_bounds]), old_freq, new_freq)
    return list(zip(los.tolist(), his.tolist()))


@dataclass
class InitialState:
    center: np.ndarray                      # starting gains (DE x0)
    bounds: list[tuple[float, float]]       # search bounds for the first round
    freq: np.ndarray                        # frequency grid


def resolve_initial_state(cfg: Config, resume: Optional[ResumeData]) -> InitialState:
    new_freq = make_freq_grid(cfg.cutoff_ratio, cfg.num_gain_points)

    if resume is None:
        center = center_seed(new_freq, cfg.nyquist_hz)
        return InitialState(center, make_bounds_around_center(center, cfg.margin), new_freq)

    old_k = len(resume.params)

    if old_k == cfg.num_gain_points:
        # Same grid size: reuse the logged frequencies exactly when we have them.
        if resume.freq is not None:
            freq = resume.freq
        else:
            print("[resume] No valid frequency grid in the log - generating a fresh one.")
            freq = new_freq
        center, bounds = resume.params, resume.bounds
    else:
        # Different grid size: interpolate gains (and bounds) onto the new grid.
        if resume.freq is not None:
            old_freq = resume.freq
        else:
            old_freq = make_freq_grid(cfg.cutoff_ratio, old_k)
            print("[resume] No valid frequency grid in the log - reconstructing the old "
                  "one from the current cutoff. Only correct if --fs is unchanged.")
        print(f"[resume] Interpolating {old_k} logged gain points onto "
              f"{cfg.num_gain_points} points.")
        freq = new_freq
        center = remap_gain_vector(resume.params, old_freq, new_freq)
        bounds = remap_bounds(resume.bounds, old_freq, new_freq) if resume.bounds else None

    if not (cfg.resume_bounds and bounds):
        bounds = make_bounds_around_center(center, cfg.margin)

    return InitialState(center, bounds, freq)


# ============================================================
#  Filter construction
# ============================================================

def make_fixed_point(taps: np.ndarray) -> np.ndarray:
    """Quantize taps to 16-bit signed fixed point and back to float,
    matching the precision the hardware/software FIR will actually use.
    (Currently not applied in make_objective.)"""
    q = np.clip(np.round(taps * 32767).astype(int), -32768, 32767)
    return (q / 32767).astype(float)


def build_lowpass_firwin2(gains: np.ndarray, freq: np.ndarray, cfg: Config) -> np.ndarray:
    """Design cfg.num_taps taps with firwin2 from one gain per grid point.
    freq is copied, never modified: the grid belongs to the optimizer state."""
    grid = np.array(freq, dtype=np.float64)
    grid[0], grid[-1] = 0.0, 1.0  # firwin2 requires exactly 0 and 1
    taps = firwin2(cfg.num_taps, grid, np.asarray(gains, dtype=np.float64),
                   window=cfg.window_mode)
    return taps.astype(np.float32)


def write_filter_file(taps: np.ndarray, path: Path = FILTER_PATH) -> None:
    path.write_text("\n".join(f"{t}" for t in taps) + "\n")


# ============================================================
#  stream1090 invocation + output parsing
# ============================================================

HEX = set(b"0123456789ABCDEFabcdef")


def _is_hex(s: bytes) -> bool:
    return all(c in HEX for c in s)


def parse_frames(out: bytes) -> tuple[int, int, dict[int, int]]:
    """One-pass parse of stream1090's output.

    Returns (total_messages, long_frame_count, {downlink_format: count}).
    """
    total = 0
    long_count = 0
    df_counts: dict[int, int] = {}

    for line in out.splitlines():
        if not line or line[0] not in (ord("@"), ord("<")) or not line.endswith(b";"):
            continue

        payload = line[1:-1]
        frame_hex = payload[14:] if line[0] == ord("<") else payload[12:]  # strip MLAT prefix

        if not _is_hex(frame_hex):
            continue

        total += 1
        if len(frame_hex) == 28:  # 112 bits
            long_count += 1

        try:
            bits = bin(int(frame_hex, 16))[2:].zfill(len(frame_hex) * 4)
            df = int(bits[:5], 2)
            df_counts[df] = df_counts.get(df, 0) + 1
        except ValueError:
            pass

    return total, long_count, df_counts


def default_output_rate(input_mhz: float) -> float:
    for inp, out in DEFAULT_RATE_PAIRS:
        if inp == input_mhz:
            return out
    raise ValueError(f"No default output rate for input rate {input_mhz}")


def run_stream1090(
    cfg: Config,
    *,
    filter_path: Optional[Path] = None,
    use_builtin_filter: bool = False,
) -> tuple[int, int, dict[int, int]]:
    """Run stream1090 against cfg.data_path and parse its output.

    Pass filter_path to use an external filter file (-f), or
    use_builtin_filter=True to use stream1090's built-in filter (-q).
    Passing neither runs stream1090 with no filtering.
    """
    output_mhz = cfg.fs_up_mhz if cfg.fs_up else default_output_rate(cfg.fs_mhz)

    cmd = [STREAM1090_EXE, "-s", str(cfg.fs_mhz), "-u", str(output_mhz)]
    if filter_path is not None:
        cmd += ["-f", str(filter_path)]
    elif use_builtin_filter:
        cmd.append("-q")

    with open(cfg.data_path, "rb") as data_file:
        proc = subprocess.run(cmd, stdin=data_file, stdout=subprocess.PIPE)

    return parse_frames(proc.stdout)


def score_of(total: int, long_count: int) -> int:
    return total + long_count


# ============================================================
#  Logging
# ============================================================

def write_log_entry(
    cfg: Config,
    header: str,
    *,
    score: Optional[float] = None,
    total: Optional[int] = None,
    df17: Optional[int] = None,
    freq: Optional[np.ndarray] = None,
    params: Optional[np.ndarray] = None,
    bounds: Optional[list[tuple[float, float]]] = None,
    taps: Optional[np.ndarray] = None,
) -> None:
    """Append one block to the log file. Only the fields passed are written,
    so this covers the baseline, per-improvement, and end-of-run entries.
    Every block starts with a '# =====' line, which load_resume_data()
    uses to split the log back into blocks."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    if header:
        lines = [f"# ==================== {header} ====================\n"]
    else:
        lines = ["# ==================================================\n"]
    lines.append(f"# Time: {timestamp}\n")
    lines.append(f"# Instance: {cfg.data_path}\n")
    lines.append(f"# FS: {cfg.fs_mhz} MHz -> FS_UP: {cfg.fs_up_mhz} MHz\n")
    lines.append(f"# Cutoff: {cfg.cutoff_hz} Hz\n")
    lines.append(f"# Number of taps: {cfg.num_taps}\n")
    lines.append(f"# Window mode: {cfg.window_mode}\n")

    if score is not None:
        lines.append(f"# Best score: {score}\n")
    if total is not None:
        lines.append(f"# Best message count: {total}\n")
    if df17 is not None:
        lines.append(f"# Best DF17 count: {df17}\n")
    if freq is not None:
        lines.append(f"# Best freq: {freq.tolist()}\n")
    if params is not None:
        lines.append(f"# Best params: {params.tolist()}\n")
    if bounds is not None:
        lines.append("# Current bounds:\n")
        lines += [f"# [{lo:.6f}, {hi:.6f}]\n" for lo, hi in bounds]
    if taps is not None:
        lines.append("# Best taps:\n")
        lines += [f"{t}\n" for t in taps]

    lines.append("\n")

    with open(cfg.logfile, "a") as f:
        f.writelines(lines)


# ============================================================
#  Optimizer state + objective
# ============================================================

@dataclass
class OptimizerState:
    """Mutable state of a running optimization.

    freq is the frequency grid candidates are built on. It lives here
    (not in Config) so the gain points could later be allowed to move.
    The best_* fields describe the best candidate seen so far, including
    the grid it was built on."""

    freq: np.ndarray
    num_calls: int = 0

    best_score: float = -np.inf
    best_total: int = -1
    best_df17: int = -1
    best_params: Optional[np.ndarray] = None
    best_freq: Optional[np.ndarray] = None
    best_taps: Optional[np.ndarray] = None

    def maybe_record(
        self,
        cfg: Config,
        score: float,
        total: int,
        df17: int,
        params: np.ndarray,
        taps: np.ndarray,
        bounds: list[tuple[float, float]],
    ) -> bool:
        """Record and log this candidate if it beats the best so far."""
        if score <= self.best_score:
            return False

        self.best_score, self.best_total, self.best_df17 = score, total, df17
        self.best_params = params.copy()
        self.best_freq = self.freq.copy()
        self.best_taps = taps.copy()

        write_log_entry(
            cfg, "",
            score=self.best_score, total=self.best_total, df17=self.best_df17,
            freq=self.best_freq, params=self.best_params,
            bounds=bounds, taps=self.best_taps,
        )
        return True


def make_objective(cfg: Config, state: OptimizerState, bounds_ref: list):
    """Returns a function(params) -> -score for differential_evolution.
    bounds_ref is passed by reference (a single-element list) so the
    objective always logs against the bounds currently in effect, even
    though differential_evolution only calls it with `params`."""

    def objective(params: np.ndarray) -> float:
        taps = build_lowpass_firwin2(params, state.freq, cfg)
        write_filter_file(taps)

        total, long_count, df_counts = run_stream1090(cfg, filter_path=FILTER_PATH)
        df17 = df_counts.get(17, 0)
        score = score_of(total, long_count)

        state.num_calls += 1
        improved = state.maybe_record(cfg, score, total, df17, params, taps, bounds_ref[0])

        print(f"Eval params={np.round(params, 4)} -> total={total}, "
              f"df17={df17}, score={score}")
        print(f"Eval bounds={np.round(bounds_ref[0], 4)}")
        print(f"| Best so far: score={state.best_score}, total={state.best_total}, "
              f"df17={state.best_df17} @ "
              f"{np.round(state.best_params, 4) if state.best_params is not None else None}"
              f"{'  (new best)' if improved else ''}")
        print(f"| {state.num_calls} of at most {cfg.max_total_calls} completed in this run")

        return -score

    return objective


# ============================================================
#  Main loop
# ============================================================

def next_round_bounds(
    result, best_params: np.ndarray, alpha: float
) -> list[tuple[float, float]]:
    """Shrink the search bounds around the best individual, scaled by its
    distance to the second-best (wider gap -> wider next-round margin)."""
    energies = result.population_energies
    pop = result.population
    idx_sorted = np.argsort(energies)

    best, second = pop[idx_sorted[0]], pop[idx_sorted[1]]
    margins = np.clip(alpha * np.abs(best - second), 0.025, 0.5)

    print("# Per-parameter margins:", margins)
    return [
        (max(GAIN_MIN, best_params[i] - margins[i]),
         min(GAIN_MAX, best_params[i] + margins[i]))
        for i in range(len(best_params))
    ]


def main() -> None:
    args = parse_args()
    resume = load_resume_data(args.resume) if args.resume else None
    cfg = Config.from_args(args, resume)
    init = resolve_initial_state(cfg, resume)

    baseline_total, baseline_long, baseline_df = run_stream1090(
        cfg, use_builtin_filter=True
    )
    baseline_score = score_of(baseline_total, baseline_long)
    write_log_entry(
        cfg, "BASELINE",
        score=baseline_score, total=baseline_total, df17=baseline_df.get(17, 0),
    )

    state = OptimizerState(freq=init.freq)
    center = init.center
    bounds_ref = [init.bounds]  # mutable box so the objective sees live bounds

    while True:
        state.num_calls = 0
        print("Starting Differential Evolution...")

        objective = make_objective(cfg, state, bounds_ref)
        result = differential_evolution(
            objective,
            bounds_ref[0],
            maxiter=cfg.maxiter,
            popsize=cfg.popsize,
            mutation=(0.5, 1.0),
            recombination=0.7,
            polish=False,
            workers=1,
            x0=center,
        )

        print("\n================ END OF RUN ====================")
        print(f"Best freq: {np.round(state.best_freq, 6)}")
        print(f"Best params: {np.round(state.best_params, 6)}")
        print(f"Best score: {state.best_score}")
        print(f"Best message count: {state.best_total}")
        print(f"Best DF17 count: {state.best_df17}")
        print("Best taps:")
        print(np.round(state.best_taps, 8))
        print("===============================================\n")

        write_log_entry(
            cfg, "END OF RUN",
            score=state.best_score, total=state.best_total, df17=state.best_df17,
            freq=state.best_freq, params=state.best_params,
            bounds=bounds_ref[0], taps=state.best_taps,
        )

        bounds_ref[0] = next_round_bounds(result, state.best_params, cfg.alpha)
        center = state.best_params.copy()


if __name__ == "__main__":
    main()
