import sys
import rs1090

# ICAO → list of (mlat, df, is_long, frame_hex)
planes = {}

def parse_mlat_line(line):
    line = line.strip()
    if not line or line[0] != '<' or not line.endswith(';'):
        return None

    payload = line[1:-1]  # strip < and ;

    # MLAT timestamp (12 hex)
    mlat_hex = payload[:12]
    mlat = int(mlat_hex, 16)

    # Skip the RSSI value (2 hex) the rest is the Mode-S frame
    frame_hex = payload[14:]

    return mlat, frame_hex


def is_long_frame(frame_hex):
    return len(frame_hex) == 28   # 28 hex chars = 112 bits


for raw in sys.stdin:
    parsed = parse_mlat_line(raw)
    if not parsed:
        continue

    mlat, frame_hex = parsed

    # try to decode using rs1090
    try:
        res = rs1090.decode(frame_hex)
    except Exception:
        continue

    if res is None:
        continue

    icao = res['icao24']
    df = res['df']
    long_flag = is_long_frame(frame_hex)

    if icao not in planes:
        planes[icao] = []

    planes[icao].append((mlat, df, long_flag, frame_hex))


# ============================================================
#  Δt analysis per ICAO
# ============================================================

for icao, msgs in planes.items():
    msgs.sort(key=lambda x: x[0])  # sort by MLAT

    prev = None
    for m in msgs:
        mlat, df, long_flag, frame_hex = m

        if prev is not None:
            prev_mlat, prev_df, prev_long, _ = prev
            dt = mlat - prev_mlat

            if prev_long:
                if dt < 1344:  # 112 us * 12 MHz
                    print("Duplicate found.", icao, mlat, dt, prev_df, df)
            else:
                if dt < 672:  # 56 us * 12 MHz
                    print("Duplicate found.", icao, mlat, dt, prev_df, df)

        prev = m
