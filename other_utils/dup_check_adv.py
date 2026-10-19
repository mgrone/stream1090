#!/usr/bin/env python3

import sys
from collections import defaultdict

import pyModeS as pms

# ----------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------

LONG_FRAME_THRESHOLD = 1344   # 112 us @ 12 MHz
SHORT_FRAME_THRESHOLD = 672   # 56 us @ 12 MHz

# ----------------------------------------------------------------------
# State
# ----------------------------------------------------------------------

# frame_hex -> (mlat, df)
last_seen = {}

total_messages = 0
duplicate_messages = 0
undecodable = 0

messages_by_df = defaultdict(int)
duplicates_by_df = defaultdict(int)

first_mlat = None
last_mlat = 0

# Input ordering check
out_of_order = 0
largest_backward_jump = 0
previous_input_mlat = None

# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------

def parse_stream1090_line(line: str):
    """Parse MLAT ASCII format from stream1090."""

    line = line.strip()

    if not line.startswith("<") or not line.endswith(";"):
        return None

    payload = line[1:-1]

    if len(payload) < (12 + 2 + 14):
        return None

    mlat_hex = payload[:12]
    rssi_hex = payload[12:14]
    frame_hex = payload[14:].upper()

    if len(frame_hex) not in (14, 28):
        return None

    try:
        mlat_int = int(mlat_hex, 16)
        rssi_int = int(rssi_hex, 16)
    except ValueError:
        return None

    return mlat_int, rssi_int, frame_hex


def duplicate_threshold(frame_hex: str):
    if len(frame_hex) == 28:
        return LONG_FRAME_THRESHOLD
    else:
        return SHORT_FRAME_THRESHOLD


# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------

for raw in sys.stdin:

    parsed = parse_stream1090_line(raw)

    if parsed is None:
        continue

    mlat_int, rssi_int, frame_hex = parsed

    # ------------------------------------------------------------------
    # Verify input stream ordering
    # ------------------------------------------------------------------

    if previous_input_mlat is not None:
        if mlat_int < previous_input_mlat:
            out_of_order += 1
            backward_jump = previous_input_mlat - mlat_int
            if backward_jump > largest_backward_jump:
                largest_backward_jump = backward_jump

            backward_ms = backward_jump / 12000.0

            print(
                f"TIME_ORDER_ERROR "
                f"PREV={previous_input_mlat} "
                f"CUR={mlat_int} "
                f"DELTA=-{backward_jump} "
                f"({backward_ms:.1f} ms)"
            )

    previous_input_mlat = mlat_int

    # ------------------------------------------------------------------
    # Decode
    # ------------------------------------------------------------------

    try:
        decoded = pms.decode(frame_hex)
    except Exception:
        undecodable += 1
        continue

    if decoded is None:
        undecodable += 1
        continue

    try:
        df = decoded["df"]
    except Exception:
        undecodable += 1
        continue

    total_messages += 1

    if first_mlat is None:
        first_mlat = mlat_int

    if mlat_int > last_mlat:
        last_mlat = mlat_int

    messages_by_df[df] += 1

    # ------------------------------------------------------------------
    # Duplicate detection
    # ------------------------------------------------------------------

    previous = last_seen.get(frame_hex)

    if previous is not None:
        prev_mlat, prev_df = previous
        dt = mlat_int - prev_mlat
        if 0 <= dt < duplicate_threshold(frame_hex):
            duplicate_messages += 1
            duplicates_by_df[df] += 1
            frame_bits = 112 if len(frame_hex) == 28 else 56
            print(
                f"DUP "
                f"DF={df:2d} "
                f"LEN={frame_bits:3d} "
                f"DT={dt:5d} "
                f"RSSI={rssi_int:3d} "
                f"FRAME={frame_hex}"
            )
    last_seen[frame_hex] = (mlat_int, df)

# ----------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------

print()
print("==================================================")
print("Duplicate Analysis Summary")
print("==================================================")

print(f"Messages            : {total_messages:,}")
print(f"Undecodable         : {undecodable:,}")
print(f"Duplicates          : {duplicate_messages:,}")

if total_messages > 0:
    duplicate_rate = 100.0 * duplicate_messages / total_messages
    print(f"Duplicate Rate      : {duplicate_rate:.3f}%")

print()
print("Input Timestamp Ordering")
print("------------------------")
print(f"Out-of-order msgs   : {out_of_order:,}")

if out_of_order > 0:
    print(f"Largest back jump   : {largest_backward_jump:,} ticks")

if first_mlat is not None and last_mlat > first_mlat:

    duration_sec = (last_mlat - first_mlat) / 12000000.0

    print()
    print("Capture Statistics")
    print("------------------")
    print(f"Duration            : {duration_sec:.1f} s")

    if duration_sec > 0:
        print(f"Message Rate        : {total_messages / duration_sec:.1f} msg/s")

print()
print("Per DF Statistics")
print("-----------------")

for df in sorted(messages_by_df.keys()):

    total = messages_by_df[df]
    dupes = duplicates_by_df[df]

    pct = 0.0
    if total > 0:
        pct = 100.0 * dupes / total

    print(
        f"DF {df:2d}: "
        f"{total:10,d} msgs   "
        f"{dupes:10,d} dupes   "
        f"{pct:7.3f}%"
    )
