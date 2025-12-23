import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
from matplotlib.lines import Line2D
from dataclasses import dataclass
from typing import Optional, Tuple


# ============================================================
#  Constants
# ============================================================

TS_OFFSET_12MHZ = -1625 #-1632 #-1627.5          # AVR timestamp is 128 us too late
CLK_12MHZ = 12_000_000.0         # timestamp clock


# ============================================================
#  Data Structures
# ============================================================

@dataclass
class SampleFormat:
    name: str
    dtype: str
    is_interleaved_iq: bool
    bits: int


@dataclass
class SampleStream:
    filename: str
    fs: float                     # complex sample rate in Hz
    ts12_start: int               # timestamp of first sample (12 MHz ticks)
    fmt: SampleFormat
    memmap: np.memmap             # uint16 words: [I0, Q0, I1, Q1, ...]


@dataclass
class AdsbMessage:
    ts12_raw: int                 # raw timestamp from AVR
    ts12_corrected: int           # corrected timestamp
    hexmsg: str                   # ADS-B hex payload


# ============================================================
#  AVR Parsing
# ============================================================

def parse_avr_line(line: str) -> Optional[AdsbMessage]:
    line = line.strip()
    if not line.startswith("@") or not line.endswith(";"):
        return None

    body = line[1:-1]
    ts_hex = body[:12]
    hexmsg = body[12:]

    ts12_raw = int(ts_hex, 16)
    ts12_corrected = ts12_raw + TS_OFFSET_12MHZ

    return AdsbMessage(ts12_raw, ts12_corrected, hexmsg)


# ============================================================
#  Hex → Bits
# ============================================================

def hex_to_bits(hexmsg: str) -> np.ndarray:
    hexmsg = hexmsg.strip()
    num_bits = len(hexmsg) * 4
    val = int(hexmsg, 16)

    bits = np.zeros(num_bits, dtype=np.uint8)
    for i in range(num_bits):
        shift = num_bits - 1 - i
        bits[i] = (val >> shift) & 1
    return bits


def compute_message_window_us(bits: np.ndarray) -> float:
    """Core window (no margins): preamble (8 us) + message bits (56 or 112 us)."""
    preamble_us = 8.0
    num_bits = len(bits)
    if num_bits not in (56, 112):
        raise ValueError(f"Unexpected ADS-B message length: {num_bits} bits")
    return preamble_us + num_bits


# ============================================================
#  Sample File Loader
# ============================================================

def load_sample_stream(filename: str,
                       fs: float,
                       ts12_start: int,
                       fmt: SampleFormat) -> SampleStream:
    mm = np.memmap(filename, dtype=fmt.dtype, mode="r")
    return SampleStream(filename, fs, ts12_start, fmt, mm)


# ============================================================
#  Timestamp → Complex Sample Index
# ============================================================

def ts12_to_sample_float(ts12: int, stream: SampleStream) -> float:
    """Return fractional complex-sample index."""
    return (ts12 - stream.ts12_start) * (stream.fs / CLK_12MHZ)


# ============================================================
#  Window Extraction (even-aligned)
# ============================================================

def extract_window(stream: SampleStream,
                   center_complex: float,
                   window_us: float,
                   pre_margin_us: float = 20.0,
                   post_margin_us: float = 20.0):
    """
    Extract a window around a fractional complex sample index with margins.
    Returns (raw_window_words, start_word_index, start_complex_effective).
    """

    window_c = stream.fs * (window_us * 1e-6)
    pre_c    = stream.fs * (pre_margin_us * 1e-6)
    post_c   = stream.fs * (post_margin_us * 1e-6)

    # Continuous complex indices
    start_c = center_complex - pre_c
    end_c   = center_complex + window_c + post_c

    # Continuous → integer word indices (2 words per complex sample)
    start_w = int(np.floor(2.0 * start_c))
    end_w   = int(np.ceil(2.0 * end_c))

    # Enforce even alignment (I/Q pairs)
    if start_w % 2 != 0:
        start_w -= 1
    if end_w % 2 != 0:
        end_w += 1

    # Clamp
    start_w = max(start_w, 0)
    end_w   = min(end_w, len(stream.memmap))

    raw_window = stream.memmap[start_w:end_w]

    # Effective complex index of the first I in the window
    start_c_effective = start_w / 2.0

    return raw_window, start_w, start_c_effective



# ============================================================
#  IQ Reconstruction
# ============================================================
def reconstruct_iq(raw: np.ndarray, fmt: "SampleFormat"):
    """
    Convert raw interleaved IQ samples into float32 I/Q arrays in [-1, +1],
    using the bit depth specified in SampleFormat.
    """

    if not fmt.is_interleaved_iq:
        raise ValueError("Only interleaved IQ formats are supported")

    raw = raw.astype(fmt.dtype, copy=False)

    if raw.size % 2 != 0:
        raise ValueError("Raw IQ array length must be even")

    I_raw = raw[0::2]
    Q_raw = raw[1::2]

    # ------------------------------------------------------------
    # Normalize using fmt.bits
    # ------------------------------------------------------------
    max_val = float(1 << (fmt.bits - 1))  # 2^(bits-1)

    if np.issubdtype(raw.dtype, np.unsignedinteger):
        # Unsigned → shift to signed range
        offset = max_val
        I = (I_raw.astype(np.float32) - offset) / max_val
        Q = (Q_raw.astype(np.float32) - offset) / max_val

    elif np.issubdtype(raw.dtype, np.signedinteger):
        # Signed → direct normalization
        I = I_raw.astype(np.float32) / max_val
        Q = Q_raw.astype(np.float32) / max_val

    else:
        raise ValueError(f"Unsupported dtype in SampleFormat: {fmt.dtype}")

    return I, Q


# ============================================================
#  Magnitude
# ============================================================

def compute_magnitude(I: np.ndarray, Q: np.ndarray) -> np.ndarray:
    return np.sqrt(I*I + Q*Q)

# ============================================================
#  Bit Centers (purely rax.bar(x, mag1, width=1.0, color="tab:blue", alpha=0.9, zorder=2)elative, margin-aware)
# ============================================================

def bit_centers_in_samples(bits: np.ndarray,
                           fs: float,
                           pre_margin_us: float) -> np.ndarray:
    """
    Return bit-center positions in complex-sample indices,
    relative to the extracted window (same domain as mag1),
    assuming:
      - window time axis starts at -pre_margin_us
      - message (with preamble) starts at t=0
    """

    num_bits = len(bits)
    preamble_us = 8.0

    # Time (in us) of bit k center relative to window start:
    # t = pre_margin + preamble + (k + 0.5) * 1us
    bit_center_us = (
        pre_margin_us
        + preamble_us
        + (np.arange(num_bits) + 0.5) * 1.0
    )

    # Convert to complex-sample indices
    bit_centers = bit_center_us * (fs * 1e-6)

    return bit_centers


# ============================================================
#  Visualization (margin-aware shading + markers)
# ============================================================

def mag_to_dbfs(mag):
    return 20 * np.log10(mag + 1e-12)


def plot_mag_with_bits_single_panel(mag1: np.ndarray,
                                    bits: np.ndarray,
                                    fs: float,
                                    pre_margin_us: float,
                                    title: str = "ADS-B Diagnostic View"):
    """
    Single-panel diagnostic plot:
      - preamble pulses behind magnitude bars
      - bit spans behind magnitude bars
      - half-bit boundaries
      - preamble shading
      - legend
      - dBFS y-axis labels
    """

    fig, ax = plt.subplots(figsize=(14, 4))

    x = np.arange(len(mag1))

    preamble_us = 8.0
    message_us  = float(len(bits))

    # Convert to complex-sample counts
    pre_margin_samples = int(round(fs * pre_margin_us * 1e-6))
    preamble_samples   = int(round(fs * preamble_us * 1e-6))
    message_samples    = int(round(fs * message_us * 1e-6))

    preamble_start = pre_margin_samples
    preamble_end   = preamble_start + preamble_samples

    # ------------------------------------------------------------
    # PREAMBLE PULSES (drawn behind bars)
    # ------------------------------------------------------------
    pulse_times_us = [
        (0.0, 0.5),
        (1.0, 1.5),
        (3.5, 4.0),
        (4.5, 5.0),
    ]

    for start_us, end_us in pulse_times_us:
        xs = (pre_margin_us + start_us) * (fs * 1e-6)
        xe = (pre_margin_us + end_us)   * (fs * 1e-6)
        ax.axvspan(xs, xe, color="gray", alpha=0.4, zorder=1)

    # ------------------------------------------------------------
    # BIT SPANS (drawn behind bars)
    # ------------------------------------------------------------
    bit_start_us = pre_margin_us + preamble_us + np.arange(len(bits)) * 1.0
    bit_end_us   = bit_start_us + 1.0

    bit_start_samples = bit_start_us * (fs * 1e-6)
    bit_end_samples   = bit_end_us   * (fs * 1e-6)

    for b, xs, xe in zip(bits, bit_start_samples, bit_end_samples):
        color = "tab:green" if b == 1 else "tab:red"
        ax.axvspan(xs, xe, color=color, alpha=0.25, zorder=1)

    # ------------------------------------------------------------
    # HALF-BIT BOUNDARIES
    # ------------------------------------------------------------
    half_bit_us = bit_start_us + 0.5
    half_bit_samples = half_bit_us * (fs * 1e-6)

    for xb in half_bit_samples:
        ax.axvline(x=xb, color="black", linestyle="--",
                   alpha=0.3, linewidth=0.8, zorder=1)

    # ------------------------------------------------------------
    # PREAMBLE SHADING (behind bars)
    # ------------------------------------------------------------
    ax.axvspan(preamble_start, preamble_end,
               color="lightgray", alpha=0.3, zorder=0)

    # ------------------------------------------------------------
    # MAGNITUDE BARS (drawn last, on top)
    # ------------------------------------------------------------
    ax.bar(x, mag1, width=1.0, color="tab:blue", alpha=0.9,
           zorder=2, align="edge")

    # ------------------------------------------------------------
    # dBFS y-axis labels
    # ------------------------------------------------------------
    yticks = ax.get_yticks()
    ax.set_yticklabels([f"{mag_to_dbfs(y):.1f}" for y in yticks])
    ax.set_ylabel("Magnitude (dBFS)")

    # ------------------------------------------------------------
    # LEGEND
    # ------------------------------------------------------------
    from matplotlib.patches import Patch
    from matplotlib.lines import Line2D

    legend_elements = [
        Patch(facecolor="tab:blue", edgecolor="none", alpha=0.9, label="Magnitude"),
        Patch(facecolor="gray", edgecolor="none", alpha=0.4, label="Preamble pulses"),
        Patch(facecolor="tab:green", edgecolor="none", alpha=0.25, label="Bit = 1"),
        Patch(facecolor="tab:red", edgecolor="none", alpha=0.25, label="Bit = 0"),
        Line2D([0], [0], color="black", linestyle="--", alpha=0.3,
               label="Half-bit boundary"),
        Patch(facecolor="lightgray", edgecolor="none", alpha=0.3, label="Preamble region"),
    ]

    ax.legend(handles=legend_elements, loc="upper right")

    # ------------------------------------------------------------
    # Final formatting
    # ------------------------------------------------------------
    ax.set_title(title)
    ax.set_xlabel("Sample index (relative window)")
    ax.grid(True, axis="y", alpha=0.3)

    plt.tight_layout()
    plt.show()


# ============================================================
#  High-Level Workflow
# ============================================================

def inspect_message(stream: SampleStream,
                    msg: AdsbMessage,
                    pre_margin_us: float = 20.0,
                    post_margin_us: float = 20.0):

    bits = hex_to_bits(msg.hexmsg)
    window_us = compute_message_window_us(bits)

    center_complex = ts12_to_sample_float(msg.ts12_corrected, stream)

    raw_window, start_w, start_c = extract_window(
        stream,
        center_complex=center_complex,
        window_us=window_us,
        pre_margin_us=pre_margin_us,
        post_margin_us=post_margin_us
    )

    I, Q = reconstruct_iq(raw_window, stream.fmt)
    mag1 = compute_magnitude(I, Q)
    
    # Bit centers purely from relative timing
    bit_centers = bit_centers_in_samples(
        bits=bits,
        fs=stream.fs,
        pre_margin_us=pre_margin_us
    )

    return I, Q, mag1, bits, bit_centers, pre_margin_us
