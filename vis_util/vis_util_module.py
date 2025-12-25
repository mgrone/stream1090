import numpy as np
import matplotlib.pyplot as plt
from dataclasses import dataclass
from typing import Optional, List, Tuple


# ============================================================
#  Constants (µs-based)
# ============================================================

TS_OFFSET_12MHZ = -1626
CLK_12MHZ = 12_000_000.0  # Hz

# 1 tick = 1 / 12 µs
# t_msg_us = ts12_corrected / 12.0


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
    fmt: SampleFormat
    memmap: np.memmap             # uint16 words: [I0, Q0, I1, Q1, ...]


@dataclass
class AdsbMessage:
    ts12_raw: int
    ts12_corrected: int
    hexmsg: str
    t_msg_us: float               # absolute message time (preamble start) in µs


# ============================================================
#  AVR Parsing → Message Time in µs
# ============================================================

def parse_avr_line(line: str) -> Optional[AdsbMessage]:
    """
    Parse a single AVR line into an AdsbMessage with t_msg_us in µs.
    """
    line = line.strip()
    if not line.startswith("@") or not line.endswith(";"):
        return None

    body = line[1:-1]
    ts_hex = body[:12]
    hexmsg = body[12:]

    ts12_raw = int(ts_hex, 16)
    ts12_corrected = ts12_raw + TS_OFFSET_12MHZ

    # t_msg_us = ts12_corrected / 12.0  (since 12 MHz → 12 ticks per µs)
    t_msg_us = ts12_corrected / 12.0

    return AdsbMessage(ts12_raw, ts12_corrected, hexmsg, t_msg_us)


def parse_avr_lines(lines: List[str]) -> List[AdsbMessage]:
    msgs: List[AdsbMessage] = []
    for line in lines:
        m = parse_avr_line(line)
        if m is not None:
            msgs.append(m)
    return msgs


# ============================================================
#  Hex → Bits and durations
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


def compute_message_core_duration_us(bits: np.ndarray) -> float:
    """
    Core duration in µs: preamble (8 µs) + message bits (56 or 112 µs).
    """
    preamble_us = 8.0
    num_bits = len(bits)
    if num_bits not in (56, 112):
        raise ValueError(f"Unexpected ADS-B message length: {num_bits} bits")
    return preamble_us + float(num_bits)


# ============================================================
#  Sample File Loader
# ============================================================

def load_sample_stream(filename: str,
                       fs: float,
                       fmt: SampleFormat) -> SampleStream:
    """
    Load a sample stream from a file as a memmap of words.
    All streams are assumed to start at time t = 0 (physical time).
    """
    mm = np.memmap(filename, dtype=fmt.dtype, mode="r")
    return SampleStream(filename, fs, fmt, mm)


# ============================================================
#  IQ Reconstruction and Magnitude
# ============================================================

def reconstruct_iq(raw: np.ndarray, fmt: SampleFormat) -> Tuple[np.ndarray, np.ndarray]:
    """
    Convert raw interleaved IQ samples into float32 I/Q arrays in [-1, +1].
    """
    if not fmt.is_interleaved_iq:
        raise ValueError("Only interleaved IQ formats are supported")

    raw = raw.astype(fmt.dtype, copy=False)

    if raw.size % 2 != 0:
        raise ValueError("Raw IQ array length must be even")

    I_raw = raw[0::2]
    Q_raw = raw[1::2]

    max_val = float(1 << (fmt.bits - 1))

    if np.issubdtype(raw.dtype, np.unsignedinteger):
        # Unsigned → shift to signed
        offset = max_val
        I = (I_raw.astype(np.float32) - offset) / max_val
        Q = (Q_raw.astype(np.float32) - offset) / max_val
    elif np.issubdtype(raw.dtype, np.signedinteger):
        I = I_raw.astype(np.float32) / max_val
        Q = Q_raw.astype(np.float32) / max_val
    else:
        raise ValueError(f"Unsupported dtype in SampleFormat: {fmt.dtype}")

    return I, Q


def compute_magnitude(I: np.ndarray, Q: np.ndarray) -> np.ndarray:
    return np.sqrt(I*I + Q*Q)


# ============================================================
#  Global Time Window (covers all messages)
# ============================================================

def compute_global_time_window_us(msgs: List[AdsbMessage],
                                  pre_margin_us: float,
                                  post_margin_us: float) -> Tuple[float, float]:
    """
    Compute a single global time window [t_win_start_us, t_win_end_us]
    in µs that covers all messages plus margins.
    """
    if not msgs:
        raise ValueError("No messages provided")

    t_min_us = min(m.t_msg_us for m in msgs)
    t_max_us = max(m.t_msg_us for m in msgs)

    # Compute worst-case core duration across messages
    core_durations_us = []
    for m in msgs:
        bits = hex_to_bits(m.hexmsg)
        core_durations_us.append(compute_message_core_duration_us(bits))
    core_max_us = max(core_durations_us)

    t_win_start_us = t_min_us - pre_margin_us
    t_win_end_us = t_max_us + core_max_us + post_margin_us

    return t_win_start_us, t_win_end_us


# ============================================================
#  Per-Stream Extraction for Global Time Window
# ============================================================

def extract_stream_window_for_time_us(stream: SampleStream,
                                      t_win_start_us: float,
                                      t_win_end_us: float) -> Tuple[np.ndarray, np.ndarray, int, int]:

    fs = stream.fs

    if stream.fmt.is_interleaved_iq:
        n_start = int(np.floor(t_win_start_us * (fs / 1e6)))
        n_end   = int(np.ceil(t_win_end_us  * (fs / 1e6)))

        total_complex = len(stream.memmap) // 2
        n_start = max(n_start, 0)
        n_end   = min(n_end, total_complex)

        start_word = 2 * n_start
        end_word   = 2 * n_end

        raw_window = stream.memmap[start_word:end_word]

        I, Q = reconstruct_iq(raw_window, stream.fmt)
        mag1 = compute_magnitude(I, Q)

    else:
        # Magnitude-only stream
        n_start = int(np.floor(t_win_start_us * (fs / 1e6)))
        n_end   = int(np.ceil(t_win_end_us  * (fs / 1e6)))

        total_mag = len(stream.memmap)
        n_start = max(n_start, 0)
        n_end   = min(n_end, total_mag)

        raw_window = stream.memmap[n_start:n_end]

        mag1 = raw_window.astype(np.float32)

    # Time axis in µs
    n = np.arange(n_start, n_end, dtype=np.float64)
    t_sample_us = n * (1e6 / fs)
    t_rel_us = t_sample_us - t_win_start_us

    return mag1, t_rel_us, n_start, n_end



# ============================================================
#  Plotting Utilities (µs axis)
# ============================================================
def mag_to_dbfs(mag: np.ndarray) -> np.ndarray:
    return 20 * np.log10(np.maximum(mag, 1e-12))


def plot_stream_single(
    t_rel_us: np.ndarray,
    mag1: np.ndarray,
    msgs: List[AdsbMessage],
    t_win_start_us: float,
    fs: float,
    title: str
):
    fig, ax = plt.subplots(figsize=(14, 4))

    sample_period_us = 1e6 / fs
    t_centers = t_rel_us + sample_period_us * 0.5

    ax.bar(
        t_centers,
        mag1,
        width=sample_period_us,
        align="center",
        color="tab:blue",
        alpha=0.9,
        zorder=2
    )

    # ------------------------------------------------------------
    # Secondary axis: dBFS labels from linear ticks
    # ------------------------------------------------------------
    ax2 = ax.twinx()
    ax2.set_ylabel("Magnitude (dBFS)")

    # Get current linear ticks
    yticks = ax.get_yticks()

    # Convert each tick to dBFS
    dbfs_labels = [f"{mag_to_dbfs(np.array([y]))[0]:.1f}" for y in yticks]

    # Apply to secondary axis
    ax2.set_yticks(yticks)
    ax2.set_ylim(ax.get_ylim())
    ax2.set_yticklabels(dbfs_labels)

    # ------------------------------------------------------------
    # ADS-B overlays
    # ------------------------------------------------------------
    preamble_us = 8.0

    for m in msgs:
        bits = hex_to_bits(m.hexmsg)
        num_bits = len(bits)
        t_msg_rel_us = m.t_msg_us - t_win_start_us

        pulses = [(0.0, 0.5), (1.0, 1.5), (3.5, 4.0), (4.5, 5.0)]
        for s, e in pulses:
            ax.axvspan(t_msg_rel_us + s, t_msg_rel_us + e,
                       color="gray", alpha=0.3, zorder=1)

        bit_start_us = t_msg_rel_us + preamble_us + np.arange(num_bits)
        bit_end_us   = bit_start_us + 1.0

        for b, xs, xe in zip(bits, bit_start_us, bit_end_us):
            color = "tab:green" if b == 1 else "tab:red"
            ax.axvspan(xs, xe, color=color, alpha=0.25, zorder=1)

        for xb in (bit_start_us + 0.5):
            ax.axvline(x=xb, color="black", linestyle="--",
                       alpha=0.3, linewidth=0.6, zorder=1)

    # ------------------------------------------------------------
    # Labels and layout
    # ------------------------------------------------------------
    ax.set_xlabel("Time relative to global window start (µs)")
    ax.set_ylabel("Magnitude (linear)")
    ax.set_title(title)
    ax.grid(True, axis="y", alpha=0.3)

    plt.tight_layout()
    plt.show()


def plot_streams_overlay(
    streams_mag_and_time: List[Tuple[str, np.ndarray, np.ndarray, float]],
    msgs: List[AdsbMessage],
    t_win_start_us: float,
    title: str = "ADS-B Multi-Stream Overlay (µs axis)"
):
    """
    Multi-stream overlay plot:
      - Each stream plotted as bars (linear magnitude)
      - Bars centered on sample times
      - Primary y-axis: linear magnitude
      - Secondary y-axis: dBFS (derived from linear ticks)
      - ADS-B overlays (preamble, bits, half-bits)
    """

    fig, ax = plt.subplots(figsize=(14, 4))

    # ------------------------------------------------------------
    # Plot each stream as a line plot
    # ------------------------------------------------------------
    for label, mag1, t_rel_us, fs in streams_mag_and_time:
        sample_period_us = 1e6 / fs
        t_centers = t_rel_us + sample_period_us * 0.5

        ax.plot(
            t_centers,
            mag1,
            linewidth=1.0,
            alpha=0.9,
            label=label,
            zorder=2
        )


    # ------------------------------------------------------------
    # Secondary axis: dBFS labels derived from linear ticks
    # ------------------------------------------------------------
    ax2 = ax.twinx()
    ax2.set_ylabel("Magnitude (dBFS)")

    # Match y-limits exactly
    ax2.set_ylim(ax.get_ylim())

    # Convert linear ticks → dBFS labels
    yticks = ax.get_yticks()
    dbfs_labels = [f"{mag_to_dbfs(np.array([y]))[0]:.1f}" for y in yticks]

    ax2.set_yticks(yticks)
    ax2.set_yticklabels(dbfs_labels)

    # ------------------------------------------------------------
    # ADS-B overlays
    # ------------------------------------------------------------
    preamble_us = 8.0

    for m in msgs:
        bits = hex_to_bits(m.hexmsg)
        num_bits = len(bits)

        t_msg_rel_us = m.t_msg_us - t_win_start_us

        # Preamble pulses
        pulses = [(0.0, 0.5), (1.0, 1.5), (3.5, 4.0), (4.5, 5.0)]
        for start_us, end_us in pulses:
            ax.axvspan(
                t_msg_rel_us + start_us,
                t_msg_rel_us + end_us,
                color="gray",
                alpha=0.15,
                zorder=1
            )

        # Bit spans
        bit_start_us = t_msg_rel_us + preamble_us + np.arange(num_bits)
        bit_end_us   = bit_start_us + 1.0

        for b, xs, xe in zip(bits, bit_start_us, bit_end_us):
            color = "tab:green" if b == 1 else "tab:red"
            ax.axvspan(xs, xe, color=color, alpha=0.15, zorder=1)

        # Half-bit boundaries
        for xb in (bit_start_us + 0.5):
            ax.axvline(
                x=xb,
                color="black",
                linestyle="--",
                alpha=0.25,
                linewidth=0.5,
                zorder=1
            )

    # ------------------------------------------------------------
    # Labels, legend, layout
    # ------------------------------------------------------------
    ax.set_xlabel("Time relative to global window start (µs)")
    ax.set_ylabel("Magnitude (linear)")
    ax.set_title(title)
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend(loc="upper right")

    plt.tight_layout()
    plt.show()



# ============================================================
#  High-Level Workflow / Example Usage
# ============================================================

if __name__ == "__main__":
    # --------------------------------------------------------
    # Example: two streams of the same capture
    #   - original 10 MHz
    #   - upsampled 24 MHz (or any other)
    # --------------------------------------------------------

    # Example AVR lines (you'll replace these with real data)
    avr_lines = [
        "@000006182b6e5d3c84819cf8ca;",
        "@0000061832fc5d4868034df541;",
        "@0000061836345d40803e167a4d;",
        "@0000061840905d4cae65d9778e;",
        #"@000002a12f4c5d4cabdbe93fe7;",
        #"@000002a138758d4d22c358a584c4d5b10c6d2a4a;",
    ]

    msgs = parse_avr_lines(avr_lines)
    if not msgs:
        raise RuntimeError("No valid AVR messages parsed")

    # Define sample formats
    fmt_u12 = SampleFormat(
        name="RAW_U12_IN_U16_IQ",
        dtype="int16",
        is_interleaved_iq=True,
        bits=16
    )

    # Streams (replace filenames and fs as needed)
    streams: List[SampleStream] = [
        load_sample_stream("../../samples/wh_6msps.bin", 6_000_000.0, fmt_u12),
        #load_sample_stream("../samples/capture_10MHz.bin", 10_000_000.0, fmt_u12),
        #load_sample_stream("../samples/capture_24MHz.bin", 24_000_000.0, fmt_u12),
    ]

    pre_margin_us = 20.0
    post_margin_us = 20.0

    # Global time window covering all messages (µs)
    t_win_start_us, t_win_end_us = compute_global_time_window_us(
        msgs,
        pre_margin_us=pre_margin_us,
        post_margin_us=post_margin_us
    )

    # Per-stream extraction + per-stream plot
    streams_mag_and_time: List[Tuple[str, np.ndarray, np.ndarray]] = []

    for stream in streams:
        mag1, t_rel_us, n_start, n_end = extract_stream_window_for_time_us(
            stream,
            t_win_start_us=t_win_start_us,
            t_win_end_us=t_win_end_us
        )

        label = f"{stream.filename} (fs={stream.fs/1e6:.1f} MHz)"
        streams_mag_and_time.append((label, mag1, t_rel_us))

        plot_stream_single(
            t_rel_us=t_rel_us,
            mag1=mag1,
            msgs=msgs,
            t_win_start_us=t_win_start_us,
            fs=stream.fs,
            title=f"Single Stream: {label}"
        )

    # Combined overlay plot for all streams
    plot_streams_overlay(
        streams_mag_and_time=streams_mag_and_time,
        msgs=msgs,
        t_win_start_us=t_win_start_us,
        title="ADS-B Multi-Stream Overlay (µs axis)"
    )
