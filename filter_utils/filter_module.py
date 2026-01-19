import numpy as np
import subprocess
from scipy.signal import lfilter, lfilter_zi, freqz


# ============================================================
#  Loading helpers (kept)
# ============================================================

def load_raw_u16(filename: str):
    raw = np.fromfile(filename, dtype=np.uint16)
    if raw.size % 2:
        raise RuntimeError("Odd number of samples in raw IQ file")
    iq = (raw.astype(np.float32) - 2047.5) / 2047.5
    return iq[0::2], iq[1::2]


def load_raw_u8(filename: str):
    raw = np.fromfile(filename, dtype=np.uint8)
    if raw.size % 2:
        raise RuntimeError("Odd number of samples in raw IQ file")
    iq = (raw.astype(np.float32) - 127.5) / 127.5
    return iq[0::2], iq[1::2]


def load_magnitude(filename: str):
    return np.fromfile(filename, dtype=np.float32)


def load_filter(path: str) -> np.ndarray:
    return np.loadtxt(path, dtype=np.float32)


# ============================================================
#  DSP blocks
# ============================================================

def dc_block(x: np.ndarray, alpha: float = 0.000325521, zi: np.ndarray | None = None):
    b = np.array([1.0, -1.0], dtype=np.float32)
    a = np.array([1.0, -(1.0 - alpha)], dtype=np.float32)

    if zi is None:
        zi = np.zeros(1, dtype=np.float32)

    y, zf = lfilter(b, a, x.astype(np.float32), zi=zi.astype(np.float32))
    return y.astype(np.float32), zf.astype(np.float32)


def dc_block_iq(I: np.ndarray, Q: np.ndarray, alpha: float,
                zi_I: np.ndarray | None = None,
                zi_Q: np.ndarray | None = None):
    I_out, zi_I = dc_block(I, alpha, zi_I)
    Q_out, zi_Q = dc_block(Q, alpha, zi_Q)
    return I_out, Q_out, zi_I, zi_Q


def hackrf_correction(I: np.ndarray, Q: np.ndarray, hbc: float):
    I_corr = I.copy()
    Q_corr = Q.copy()
    I_corr[0::2] = -I_corr[0::2]
    Q_corr[0::2] = -Q_corr[0::2] * hbc
    Q_corr[1::2] = Q_corr[1::2] * hbc
    return I_corr, Q_corr


def filter_iq(I, Q, taps, zi_I=None, zi_Q=None):
    taps = np.asarray(taps, dtype=np.float32)
    L = len(taps) - 1

    if zi_I is None or zi_I.shape != (L,):
        zi_I = np.zeros(L, dtype=np.float32)

    if zi_Q is None or zi_Q.shape != (L,):
        zi_Q = np.zeros(L, dtype=np.float32)

    I_f, zi_I = lfilter(taps, 1.0, I, zi=zi_I)
    Q_f, zi_Q = lfilter(taps, 1.0, Q, zi=zi_Q)

    return I_f.astype(np.float32), Q_f.astype(np.float32), zi_I.astype(np.float32), zi_Q.astype(np.float32)



def filter_mag(mag: np.ndarray, taps: np.ndarray, zi=None):
    taps = np.asarray(taps, dtype=np.float32)
    L = len(taps) - 1

    if L < 0:
        raise ValueError("taps must have at least one coefficient")

    if L == 0:
        # 1-tap FIR: no state
        y = mag.astype(np.float32) * taps[0]
        return y.astype(np.float32), np.zeros(0, dtype=np.float32)

    if zi is None or zi.shape != (L,):
        zi = np.zeros(L, dtype=np.float32)

    y, zf = lfilter(taps, 1.0, mag.astype(np.float32), zi=zi.astype(np.float32))
    return y.astype(np.float32), zf.astype(np.float32)




def magnitude(I: np.ndarray, Q: np.ndarray) -> np.ndarray:
    return np.sqrt(I * I + Q * Q).astype(np.float32)


# ============================================================
#  stream1090 runner
# ============================================================

def _select_stream1090_exe(stream1090_dir: str, input_rate: int) -> str:
    exe_map = {
        6_000_000:  f"{stream1090_dir}/stream1090_6M",
        10_000_000: f"{stream1090_dir}/stream1090_10M",
        2_400_000:  f"{stream1090_dir}/stream1090",
    }
    if input_rate not in exe_map:
        raise ValueError(f"Unsupported input rate: {input_rate}")
    return exe_map[input_rate]


def run_stream1090(
    mag: np.ndarray,
    input_rate: int = 6_000_000,
    upsample_rate: int | None = None,
    stream1090_dir: str = "../build",
):
    if upsample_rate is None:
        upsample_rate = input_rate

    exe = _select_stream1090_exe(stream1090_dir, input_rate)
    args = [exe, "-m"]

    if input_rate != upsample_rate:
        args.extend(["-u", str(upsample_rate // 1_000_000)])

    proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    out, _ = proc.communicate(input=mag.tobytes())

    total = sum(line.startswith(b"@") for line in out.splitlines())
    return total, out


# ============================================================
#  Batch pipelines (arrays in → magnitude out)
# ============================================================

def run_mag_pipeline(mag: np.ndarray, taps: np.ndarray) -> np.ndarray:
    y, _ = filter_mag(mag, taps)
    return y


def run_raw_mag_pipeline(
    I: np.ndarray,
    Q: np.ndarray,
    taps: np.ndarray,
    dc: bool = False,
    dc_alpha: float = 0.001,
    hbc: float | None = 1.0,
) -> np.ndarray:
    zi_I = zi_Q = None
    if dc:
        I, Q, zi_I, zi_Q = dc_block_iq(I, Q, dc_alpha, zi_I, zi_Q)
    if hbc is not None:
        I, Q = hackrf_correction(I, Q, hbc)
    mag = magnitude(I, Q)
    y, _ = filter_mag(mag, taps)
    return y


def run_raw_iq_pipeline(
    I: np.ndarray,
    Q: np.ndarray,
    taps: np.ndarray,
    dc: bool = False,
    dc_alpha: float = 0.005,
    hbc: float | None = 1.0,
) -> np.ndarray:
    zi_I = zi_Q = None
    if dc:
        I, Q, zi_I, zi_Q = dc_block_iq(I, Q, dc_alpha, zi_I, zi_Q)
    if hbc is not None:
        I, Q = hackrf_correction(I, Q, hbc)
    I_f, Q_f, zi_I, zi_Q = filter_iq(I, Q, taps, zi_I, zi_Q)
    return magnitude(I_f, Q_f)


# ============================================================
#  Streaming processors
# ============================================================

class StreamIQProcessor:
    """
    Streaming processor for IQ-domain operations:
      - optional DC blocking (stateful)
      - optional HackRF bias correction
      - optional FIR filtering in IQ domain (stateful)
      - outputs magnitude

    If taps=None, FIR filtering is skipped (used in raw-mag mode).
    """

    def __init__(self, taps, dc=False, dc_alpha=0.005, hbc=1.0):
        self.dc = dc
        self.dc_alpha = dc_alpha
        self.hbc = hbc

        # -----------------------------
        # DC blocker state (1 element)
        # -----------------------------
        self.zi_dc_I = None
        self.zi_dc_Q = None

        # -----------------------------
        # FIR taps and state
        # -----------------------------
        if taps is None:
            # No FIR in IQ domain (raw-mag mode)
            self.taps = None
            self.zi_fir_I = None
            self.zi_fir_Q = None
        else:
            self.taps = np.asarray(taps, dtype=np.float32)
            L = len(self.taps) - 1
            self.zi_fir_I = np.zeros(L, dtype=np.float32)
            self.zi_fir_Q = np.zeros(L, dtype=np.float32)

    def process_chunk(self, I_chunk: np.ndarray, Q_chunk: np.ndarray) -> np.ndarray:
        """
        Process a chunk of IQ samples and return magnitude.
        Maintains internal state across chunks.
        """
        I = I_chunk.astype(np.float32)
        Q = Q_chunk.astype(np.float32)

        # -----------------------------
        # DC blocking (stateful)
        # -----------------------------
        if self.dc:
            I, Q, self.zi_dc_I, self.zi_dc_Q = dc_block_iq(
                I, Q, self.dc_alpha, self.zi_dc_I, self.zi_dc_Q
            )

        # -----------------------------
        # HackRF bias correction
        # -----------------------------
        if self.hbc is not None:
            I, Q = hackrf_correction(I, Q, self.hbc)

        # -----------------------------
        # FIR filtering in IQ domain
        # -----------------------------
        if self.taps is not None:
            I, Q, self.zi_fir_I, self.zi_fir_Q = filter_iq(
                I, Q, self.taps, self.zi_fir_I, self.zi_fir_Q
            )

        # -----------------------------
        # Magnitude output
        # -----------------------------
        return magnitude(I, Q)



class StreamMagProcessor:
    """
    Streaming processor for magnitude-domain FIR filtering.

    - taps: FIR coefficients (1D array)
    - maintains internal FIR state across chunks
    - process_chunk(mag_chunk) -> filtered magnitude
    """

    def __init__(self, taps):
        if taps is None:
            raise ValueError("StreamMagProcessor requires non-None taps")

        self.taps = np.asarray(taps, dtype=np.float32)
        L = len(self.taps) - 1

        # FIR state: length N-1 for N-tap FIR
        self.zi_fir = np.zeros(L, dtype=np.float32) if L > 0 else np.zeros(0, dtype=np.float32)

    def process_chunk(self, mag_chunk: np.ndarray) -> np.ndarray:
        """
        Process a chunk of magnitude samples and return filtered magnitude.
        Maintains internal FIR state across chunks.
        """
        mag = mag_chunk.astype(np.float32)

        # Handle trivial 1-tap FIR (no state)
        if len(self.taps) == 1:
            return mag * self.taps[0]

        # Normal FIR with state
        y, self.zi_fir = filter_mag(mag, self.taps, self.zi_fir)
        return y

