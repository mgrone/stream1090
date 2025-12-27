import argparse
from vis_util_module import (
    SampleFormat,
    load_sample_stream,
    parse_avr_lines,
    compute_global_time_window_us,
    extract_stream_window_for_time_us,
    plot_stream_single,
    plot_streams_overlay,
)


def main():
    parser = argparse.ArgumentParser(description="ADS-B Multi-Stream Visualizer")

    # ------------------------------------------------------------
    # Multiple sample files
    # ------------------------------------------------------------
    parser.add_argument("--file", action="append", required=True,
                        help="Path to IQ sample file (can be repeated)")

    parser.add_argument("--fs", action="append", required=True, type=float,
                        help="Sample rate in Hz for each --file (same order)")

    parser.add_argument("--format", action="append", required=True,
                        choices=["iq_uint8", "iq_int16", "iq_uint16", "mag_float32"],
                        help="IQ sample format for each --file (same order)")

    # ------------------------------------------------------------
    # Messages (can be repeated, each may contain multiple messages)
    # ------------------------------------------------------------
    parser.add_argument("--message", action="append", required=True,
                        help="AVR message(s). Can be repeated. "
                             "Each argument may contain multiple messages separated by ';'.")

    # ------------------------------------------------------------
    # Margins
    # ------------------------------------------------------------
    parser.add_argument("--pre-margin", type=float, default=20.0,
                        help="Margin before earliest message (µs)")

    parser.add_argument("--post-margin", type=float, default=20.0,
                        help="Margin after latest message (µs)")
    
    # Save to png
    parser.add_argument("--save", help="Save the plot to this PNG file instead of showing it")


    args = parser.parse_args()

    # ------------------------------------------------------------
    # Validate counts
    # ------------------------------------------------------------
    if not (len(args.file) == len(args.fs) == len(args.format)):
        raise RuntimeError("Each --file must have a matching --fs and --format")

    # ------------------------------------------------------------
    # Map formats
    # ------------------------------------------------------------
    def make_format(fmt_str):
        if fmt_str == "iq_uint8":
            return SampleFormat("RAW_U8_IQ", "uint8", True, 8)
        elif fmt_str == "iq_int16":
            return SampleFormat("RAW_S16_IQ", "int16", True, 16)
        elif fmt_str == "iq_uint16":
            return SampleFormat("RAW_U16_IQ", "uint16", True, 12)
        elif fmt_str == "mag_float32":
            return SampleFormat("FLOAT_MAG", "float32", False, 32)
        else:
            raise RuntimeError(f"Unsupported format: {fmt_str}")

    # ------------------------------------------------------------
    # Load streams
    # ------------------------------------------------------------
    streams = []
    for filename, fs, fmt_str in zip(args.file, args.fs, args.format):
        fmt = make_format(fmt_str)
        stream = load_sample_stream(filename, fs, fmt)
        streams.append(stream)

    # ------------------------------------------------------------
    # Parse messages (supporting multiple per argument)
    # ------------------------------------------------------------
    raw_msgs = []
    for m_arg in args.message:
        # Split on '@' but keep structure clean
        parts = [p.strip() for p in m_arg.split("@") if p.strip()]
        for p in parts:
            msg = "@" + p
            if not msg.endswith(";"):
                msg += ";"
            raw_msgs.append(msg)

    msgs = parse_avr_lines(raw_msgs)
    if not msgs:
        raise RuntimeError("No valid AVR messages parsed")

    # ------------------------------------------------------------
    # Compute global time window (µs)
    # ------------------------------------------------------------
    t_win_start_us, t_win_end_us = compute_global_time_window_us(
        msgs,
        pre_margin_us=args.pre_margin,
        post_margin_us=args.post_margin,
    )

    # ------------------------------------------------------------
    # Extract per-stream windows
    # ------------------------------------------------------------
    streams_mag_and_time = []

    for stream in streams:
        mag1, t_rel_us, n_start, n_end = extract_stream_window_for_time_us(
            stream,
            t_win_start_us=t_win_start_us,
            t_win_end_us=t_win_end_us
        )

        label = f"{stream.filename} (fs={stream.fs/1e6:.1f} MHz)"

        # IMPORTANT: include fs so overlay can compute bar widths
        streams_mag_and_time.append((label, mag1, t_rel_us, stream.fs))

    # ------------------------------------------------------------
    # Plot logic:
    #   - If only one stream → show single-stream plot
    #   - If multiple streams → show only the overlay plot
    # ------------------------------------------------------------
    if len(streams) == 1:
        label, mag1, t_rel_us, fs = streams_mag_and_time[0]
        plot_stream_single(
            t_rel_us=t_rel_us,
            mag1=mag1,
            msgs=msgs,
            t_win_start_us=t_win_start_us,
            fs=fs,
            title=f"Single Stream: {label}",
            save_path=args.save
        )
    else:
        plot_streams_overlay(
            streams_mag_and_time=streams_mag_and_time,
            msgs=msgs,
            t_win_start_us=t_win_start_us,
            title="ADS-B Multi-Stream Overlay (µs axis)",
            save_path=args.save
        )

if __name__ == "__main__":
    main()
