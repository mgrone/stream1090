import argparse
from vis_util_module import (
    SampleFormat,
    load_sample_stream,
    parse_avr_line,
    inspect_message,
    plot_mag_with_bits_single_panel,
)

def main():
    parser = argparse.ArgumentParser(description="Sample Visualizer")

    parser.add_argument("--file", required=True,
                        help="Path to IQ sample file")

    parser.add_argument("--format", required=True, choices=["uint8", "int16", "uint16"],
                        help="IQ sample format (interleaved I/Q)")

    parser.add_argument("--fs", required=True, type=float,
                        help="Sample rate in Hz")

    parser.add_argument("--message", required=True,
                        help="AVR message line, e.g. @000001bb5843a8...;")

    parser.add_argument("--pre-margin", type=float, default=20.0,
                        help="Margin before preamble in microseconds")

    parser.add_argument("--post-margin", type=float, default=20.0,
                        help="Margin after message in microseconds")

    args = parser.parse_args()

    # ------------------------------------------------------------
    # Map CLI format → SampleFormat
    # ------------------------------------------------------------
    if args.format == "uint8":
        fmt = SampleFormat(
            name="RAW_U8_IQ",
            dtype="uint8",
            is_interleaved_iq=True,
            bits=8,
        )
    elif args.format == "int16":
        fmt = SampleFormat(
            name="RAW_S16_IQ",
            dtype="int16",
            is_interleaved_iq=True,
            bits=16,
        )
    elif args.format == "uint16":
        fmt = SampleFormat(
            name="RAW_U16_IQ",
            dtype="uint16",
            is_interleaved_iq=True,
            bits=12,
        )
    else:
        raise RuntimeError("Unsupported format")

    # ------------------------------------------------------------
    # Load sample stream (ts12_start fixed to 0)
    # ------------------------------------------------------------
    stream = load_sample_stream(
        filename=args.file,
        fs=args.fs,
        ts12_start=0,
        fmt=fmt,
    )

    # ------------------------------------------------------------
    # Parse message
    # ------------------------------------------------------------
    msg = parse_avr_line(args.message)
    if msg is None:
        raise RuntimeError("Invalid AVR message format")

    # ------------------------------------------------------------
    # Extract window + compute magnitude
    # ------------------------------------------------------------
    I, Q, mag1, bits, bit_centers, pre_margin_us = inspect_message(
        stream,
        msg,
        pre_margin_us=args.pre_margin,
        post_margin_us=args.post_margin,
    )

    # ------------------------------------------------------------
    # Plot
    # ------------------------------------------------------------
    plot_mag_with_bits_single_panel(
        mag1=mag1,
        bits=bits,
        fs=stream.fs,
        pre_margin_us=pre_margin_us,
        title="Sample Diagnostic View",
    )


if __name__ == "__main__":
    main()
