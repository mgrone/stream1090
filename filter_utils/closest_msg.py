import sys

def extract_ts(line):
    """
    Extract the first 12 hex digits of the timestamp.
    If the line starts with '@', skip it.
    """
    if line.startswith("@"):
        line = line[1:]

    ts_hex = line[:12]
    return int(ts_hex, 16)


def process_stdin():
    prev_ts = None
    prev_line = None

    best_diff = None
    best_pair = (None, None)

    sorted_ok = True
    first_unsorted = None

    for raw in sys.stdin:
        line = raw.rstrip("\n")

        # Skip empty or too-short lines
        if len(line.lstrip("@")) < 12:
            continue

        ts = extract_ts(line)

        if prev_ts is not None:
            # Check sortedness
            if ts < prev_ts and sorted_ok:
                sorted_ok = False
                first_unsorted = (prev_ts, ts, prev_line, line)

            # Track minimum difference (only meaningful if sorted)
            diff = ts - prev_ts
            if diff > 0:
                if best_diff is None or diff < best_diff:
                    best_diff = diff
                    best_pair = (prev_line, line)

        prev_ts = ts
        prev_line = line

    return sorted_ok, first_unsorted, best_diff, best_pair


if __name__ == "__main__":
    sorted_ok, first_unsorted, diff, (msg1, msg2) = process_stdin()

    if sorted_ok:
        print("Timestamps ARE sorted.")
    else:
        print("Timestamps are NOT sorted.")
        prev_ts, ts, prev_line, line = first_unsorted
        print("First out-of-order pair:")
        print(f"  Previous: {prev_ts:012X}  ({prev_line})")
        print(f"  Current:  {ts:012X}  ({line})")

    print()
    print("Minimum timestamp difference:", diff)
    print("Message 1:", msg1)
    print("Message 2:", msg2)
