import numpy as np
import argparse

def load_iq_u16(filename):
    """
    Load interleaved IQ samples stored as uint16 words.
    Only the lower 12 bits are used.
    """
    raw = np.fromfile(filename, dtype=np.uint16)
    if len(raw) % 2 != 0:
        raise RuntimeError("Input file has an odd number of words — not valid interleaved IQ")

    I_raw = raw[0::2] & 0x0FFF
    Q_raw = raw[1::2] & 0x0FFF

    # Convert to float32 in roughly [-1, 1]
    I = (I_raw.astype(np.float32) - 2047.5) / 2047.5
    Q = (Q_raw.astype(np.float32) - 2047.5) / 2047.5

    return I, Q


def dc_remove_iq(I, Q):
    """Remove DC from I and Q separately."""
    #I_dc = np.empty_like(I)
    #Q_dc = np.empty_like(Q)
    # alpha = 3.25521e-05
    alpha = 0.000325521
    avg = 0.0
    for i, s in enumerate(I):
        I[i] = I[i] - avg
        avg = avg + alpha * I[i] 
        Q[i] = Q[i] - avg 
        avg = avg + alpha * Q[i] 
    
    #avg  = (np.mean(I) + np.mean(Q)) * 0.5 
    #I_dc = I - avg
    #Q_dc = Q - avg
    return I, Q

def compute_magnitude(I, Q):
    return np.sqrt(I * I + Q * Q).astype(np.float32)

def main():
    ap = argparse.ArgumentParser(description="Convert 12-bit IQ (uint16) to magnitude stream")
    ap.add_argument("--infile", required=True, help="Input raw IQ file (uint16 interleaved)")
    ap.add_argument("--outfile", required=True, help="Output magnitude file")
    args = ap.parse_args()

    print("Loading data")
    # Load IQ
    I,Q = load_iq_u16(args.infile)

    print("DC removal")
    # DC removal on I and Q (complex plane centering)
    dc_remove_iq(I,Q)

    print("Compute magnitude")
    mag = compute_magnitude(I,Q)
    
    mag.astype(np.float32).tofile(args.outfile)
    print(f"Wrote {len(mag)} magnitude samples to {args.outfile}")


if __name__ == "__main__":
    main()
