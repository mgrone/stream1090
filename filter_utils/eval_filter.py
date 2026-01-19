import argparse
import numpy as np
from filter_module import (
    load_filter,
    load_raw_u8,
    load_raw_u16,
    load_magnitude,
    run_mag_pipeline,
    run_raw_mag_pipeline,
    run_raw_iq_pipeline,
    run_stream1090,
    StreamIQProcessor,
    StreamMagProcessor,
)


def chunk_reader_iq_u16(path, chunk_samples):
    with open(path, "rb") as f:
        while True:
            data = np.fromfile(f, dtype=np.uint16, count=2 * chunk_samples)
            if data.size == 0:
                break
            iq = (data.astype(np.float32) - 2047.5) / 2047.5
            I = iq[0::2]
            Q = iq[1::2]
            yield I, Q


def chunk_reader_iq_u8(path, chunk_samples):
    with open(path, "rb") as f:
        while True:
            data = np.fromfile(f, dtype=np.uint8, count=2 * chunk_samples)
            if data.size == 0:
                break
            iq = (data.astype(np.float32) - 127.5) / 127.5
            I = iq[0::2]
            Q = iq[1::2]
            yield I, Q


def chunk_reader_mag(path, chunk_samples):
    with open(path, "rb") as f:
        while True:
            data = np.fromfile(f, dtype=np.float32, count=chunk_samples)
            if data.size == 0:
                break
            yield data


def main():
    ap = argparse.ArgumentParser(description="ADS-B FIR test pipeline")
    ap.add_argument("--filter", required=True, help="The filter text file")
    ap.add_argument("--infile", required=True, help="Sample file")
    ap.add_argument("--fs", type=int, required=True, help="Sample rate of the input")
    ap.add_argument("--mode", choices=["mag", "raw-mag", "raw-iq"], default="mag")
    ap.add_argument("--hbc", type=float, default=1.0)
    ap.add_argument("--dc", action="store_true")
    ap.add_argument("--save-mag", help="Save magnitude to file")
    ap.add_argument("--no1090", action="store_true", help="Do NOT run stream1090")
    ap.add_argument("--upsample-rate", type=int, default=None,
                    help="Defaults to stream1090's default if omitted")
    ap.add_argument("--stream", action="store_true",
                    help="Process input in streaming chunks")
    ap.add_argument("--chunk-samples", type=int, default=200000,
                    help="Chunk size in complex samples for streaming")
    ap.add_argument("--stream1090-dir", default="../build")
    args = ap.parse_args()

    print("Loading filter...")
    taps = load_filter(args.filter)

    # --------------------------------------------------------
    # Non-streaming (batch) path
    # --------------------------------------------------------
    if not args.stream:
        if args.mode == "mag":
            print("Loading magnitude file...")
            mag_in = load_magnitude(args.infile)
            mag = run_mag_pipeline(mag_in, taps)

        else:
            if args.fs == 2_400_000:
                print("Loading uint8 IQ (rtl_sdr)...")
                I, Q = load_raw_u8(args.infile)
            else:
                print("Loading uint16 IQ (HackRF)...")
                I, Q = load_raw_u16(args.infile)

            if args.mode == "raw-mag":
                print("Mode: raw-mag (raw IQ → magnitude → filter)")
                mag = run_raw_mag_pipeline(I, Q, taps, args.dc, 0.001, args.hbc)
            elif args.mode == "raw-iq":
                print("Mode: raw-iq (raw IQ → filter → magnitude)")
                mag = run_raw_iq_pipeline(I, Q, taps, args.dc, 0.005, args.hbc)

        if args.save_mag:
            mag.tofile(args.save_mag)
            print(f"Saved magnitude to {args.save_mag}")

        if not args.no1090:
            total, _ = run_stream1090(
                mag,
                input_rate=args.fs,
                upsample_rate=args.upsample_rate,
                stream1090_dir=args.stream1090_dir,
            )
            print(f"Total messages: {total}")
        else:
            print("Skipping stream1090 (user requested --no1090)")
        return

    # --------------------------------------------------------
    # Streaming path
    # --------------------------------------------------------
    print("Streaming mode enabled")

    if args.mode == "mag":
        print("Streaming magnitude → filter")
        proc = StreamMagProcessor(taps)
        all_mag = []

        for mag_chunk in chunk_reader_mag(args.infile, args.chunk_samples):
            y = proc.process_chunk(mag_chunk)
            all_mag.append(y)

        mag = np.concatenate(all_mag, dtype=np.float32)

    else:
        if args.fs == 2_400_000:
            reader = chunk_reader_iq_u8
            print("Streaming uint8 IQ (rtl_sdr)...")
        else:
            reader = chunk_reader_iq_u16
            print("Streaming uint16 IQ (HackRF)...")

        if args.mode == "raw-mag":
            print("Mode: raw-mag (raw IQ → magnitude → filter)")
            iq_proc = StreamIQProcessor(taps=None, dc=args.dc, dc_alpha=0.001, hbc=args.hbc)
            mag_proc = StreamMagProcessor(taps)
            all_mag = []

            for I_chunk, Q_chunk in reader(args.infile, args.chunk_samples):
                mag_chunk = iq_proc.process_chunk(I_chunk, Q_chunk)
                y = mag_proc.process_chunk(mag_chunk)
                all_mag.append(y)

            mag = np.concatenate(all_mag, dtype=np.float32)

        elif args.mode == "raw-iq":
            print("Mode: raw-iq (raw IQ → filter → magnitude)")
            iq_proc = StreamIQProcessor(taps, dc=args.dc, dc_alpha=0.005, hbc=args.hbc)
            all_mag = []

            for I_chunk, Q_chunk in reader(args.infile, args.chunk_samples):
                mag_chunk = iq_proc.process_chunk(I_chunk, Q_chunk)
                all_mag.append(mag_chunk)

            mag = np.concatenate(all_mag, dtype=np.float32)

    if args.save_mag:
        mag.tofile(args.save_mag)
        print(f"Saved magnitude to {args.save_mag}")

    if not args.no1090:
        total, _ = run_stream1090(
            mag,
            input_rate=args.fs,
            upsample_rate=args.upsample_rate,
            stream1090_dir=args.stream1090_dir,
        )
        print(f"Total messages: {total}")
    else:
        print("Skipping stream1090 (user requested --no1090)")


if __name__ == "__main__":
    main()
