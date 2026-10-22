#!/usr/bin/env bash
#
# ant.sh - configure an AntSDR (AD9361) with the local libiio 1.0 build and feed stream1090_ad936x
#
#   ant.sh config [options]                         show the config; options change it
#   ant.sh start  [options] [-- <stream1090 args>]  apply options (if any), then stream
#
# Lives in a subfolder of the stream1090 repo (e.g. scripts/), otherwise set STREAM1090_DIR.
# The libiio tools come from thirdparty/libiio/build and NOT from the system:
# older libiio 1.0 builds have a bug when uploading FIR filters with > 16 taps.

set -euo pipefail

# ------------------------------------------------------------
# Defaults (override with environment variables or options)
# ------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${STREAM1090_DIR:-$(cd "$SCRIPT_DIR/.." && pwd)}"
IIO_PREFIX="${IIO_PREFIX:-$REPO_DIR/thirdparty/libiio/build}"
STREAM_BIN="${STREAM1090_BIN:-$REPO_DIR/build/stream1090_ad936x}"

ANT="${ANT_HOST:-ant}"                 # AntSDR host name or IP
OUT="${ANT_OUT:-raspberrypi:30001}"    # host:port for socat, or "-" for stdout
DEFAULT_FREQ_HZ=1090000000             # 'start' always tunes here unless --freq is given
DEFAULT_BW_HZ=4000000                  # 4 MHz is crucial for Mode-S
BUFFER=262144                          # samples per iio_rwdev buffer

export PATH="$IIO_PREFIX/utils:$PATH"
export LD_LIBRARY_PATH="$IIO_PREFIX${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# ------------------------------------------------------------
# Helpers
# ------------------------------------------------------------
die()  { echo "ant.sh: $*" >&2; exit 1; }
log()  { echo "[ant] $*" >&2; }

need_val()   { [[ $# -ge 2 ]] || die "option $1 needs a value"; }
only_start() { [[ $CMD == start ]] || die "option $1 is only valid for 'start'"; }

usage() {
    cat <<'EOF'
Usage:
  ant.sh config [options]                          show the current config; options change it
  ant.sh start  [options] [-- <stream1090 args>]   apply options (if any), then stream

config and start options (only what you give is changed):
  -s, --rate MHz        sampling rate in MHz, decimals allowed (e.g. 2.4, 12)
  -g, --gain dB         hardware gain (manual gain mode)
      --bw HZ           RF bandwidth in Hz
      --freq HZ         RX LO frequency ('start' defaults to 1090000000)
      --taps FILE       upload this FIR (also sets the bandwidth, default 4000000)
      --dec N           RX decimation and TX interpolation together: 1, 2 or 4
                        (part of the FIR config, so it needs --taps)
      --ant HOST        AntSDR host (default: $ANT_HOST or "ant")

start only:
  -u, --upsample N      stream1090 upsampling factor (default 1)
      --record FILE     also write the raw samples to FILE (for replays)
  -o, --out HOST:PORT   send output via socat (default raspberrypi:30001), "-" = stdout
      --buffer N        iio_rwdev buffer size in samples (default 262144)
      --bin PATH        stream1090 binary (default build/stream1090_ad936x)
  Without -s, stream1090 uses the sampling rate the AntSDR currently has.
  Everything after "--" is passed to stream1090, e.g.:  ant.sh start -- -q

Environment: ANT_HOST, ANT_OUT, STREAM1090_DIR, STREAM1090_BIN, IIO_PREFIX
EOF
}

# ------------------------------------------------------------
# Options (shared by config and start)
# ------------------------------------------------------------
CMD=""; RATE=""; RATE_HZ=""; GAIN=""; BW=""; FREQ=""; TAPS=""; DEC=""
UP=1; RECORD=""; EXTRA=()

parse_opts() {
    while (($#)); do
        case "$1" in
            -s|--rate)     need_val "$@"; RATE=$2;  shift 2 ;;
            -g|--gain)     need_val "$@"; GAIN=$2;  shift 2 ;;
            --bw)          need_val "$@"; BW=$2;    shift 2 ;;
            --freq)        need_val "$@"; FREQ=$2;  shift 2 ;;
            --taps)        need_val "$@"; TAPS=$2;  shift 2 ;;
            --dec)         need_val "$@"; DEC=$2;   shift 2 ;;
            --ant)         need_val "$@"; ANT=$2;   shift 2 ;;
            -u|--upsample) only_start "$@"; need_val "$@"; UP=$2;         shift 2 ;;
            --record)      only_start "$@"; need_val "$@"; RECORD=$2;     shift 2 ;;
            -o|--out)      only_start "$@"; need_val "$@"; OUT=$2;        shift 2 ;;
            --buffer)      only_start "$@"; need_val "$@"; BUFFER=$2;     shift 2 ;;
            --bin)         only_start "$@"; need_val "$@"; STREAM_BIN=$2; shift 2 ;;
            --)            only_start "$@"; shift; EXTRA=("$@"); break ;;
            -h|--help)     usage; exit 0 ;;
            *)             die "unknown option: $1 (see --help)" ;;
        esac
    done

    [[ -z $RATE || $RATE =~ ^[0-9]+(\.[0-9]+)?$ ]]    || die "-s needs MHz, e.g. 4 or 2.4"
    [[ -z $GAIN || $GAIN =~ ^-?[0-9]+(\.[0-9]+)?$ ]]  || die "-g needs dB"
    [[ -z $BW   || $BW   =~ ^[0-9]+$ ]]               || die "--bw needs Hz"
    [[ -z $FREQ || $FREQ =~ ^[0-9]+$ ]]               || die "--freq needs Hz"
    [[ -z $DEC  || $DEC  =~ ^[124]$ ]]                || die "--dec must be 1, 2 or 4"
    [[ $UP =~ ^[0-9]+$ ]]                             || die "-u must be an integer"
    if [[ -n $DEC && -z $TAPS ]]; then
        die "--dec is part of the FIR config and is uploaded together with --taps"
    fi
    if [[ -n $RATE ]]; then
        RATE_HZ=$(awk -v m="$RATE" 'BEGIN { printf "%.0f", m * 1000000 }')
    fi
}

# ------------------------------------------------------------
# libiio access (always through the local build)
# ------------------------------------------------------------
rx_attr()  { iio_attr -u "ip:$ANT" -i -c ad9361-phy voltage0 "$@"; }      # RX channel attributes
lo_attr()  { iio_attr -u "ip:$ANT" -o -c ad9361-phy altvoltage0 "$@"; }   # RX LO
dev_attr() { iio_attr -u "ip:$ANT" -d ad9361-phy "$@"; }                  # device attributes

check_tools() {
    [[ -x "$IIO_PREFIX/utils/iio_attr" ]] \
        || die "libiio tools not found in $IIO_PREFIX/utils (build thirdparty/libiio first)"
    iio_info -V >&2
}

device_rate() {
    local fs
    fs=$(rx_attr sampling_frequency | awk '{print $NF}') || die "cannot reach AntSDR at '$ANT'"
    [[ $fs =~ ^[0-9]+$ ]] || die "could not determine the sample rate of '$ANT'"
    echo "$fs"
}

status() {
    echo "RX LO frequency:";    lo_attr frequency
    echo "Sampling frequency:"; rx_attr sampling_frequency
    echo "RF bandwidth:";       rx_attr rf_bandwidth
    echo "Hardware gain:";      rx_attr hardwaregain
    echo "FIR enabled:";        rx_attr filter_fir_en
    echo "FIR config:";         dev_attr filter_fir_config
    echo "RX path rates:";      dev_attr rx_path_rates
}

# ------------------------------------------------------------
# FIR: tap file (one float per line, '#' = comment) -> AD9361 filter_fir_config text
#   $1 = tap file, $2 = decimation (RX) / interpolation (TX)
# ------------------------------------------------------------
fir_config() {
    python3 - "$1" "$2" <<'PYEOF'
import sys

taps = []
with open(sys.argv[1]) as f:
    for line in f:
        line = line.strip()
        if line and not line.startswith("#"):
            taps.append(float(line))
ratio = int(sys.argv[2])

if not taps:
    sys.exit("no taps found in " + sys.argv[1])
total = sum(taps)
if total == 0:
    sys.exit("taps sum to zero, cannot normalise")
if len(taps) > 128:
    sys.exit(f"{len(taps)} taps, the AD9361 FIR holds at most 128")

n = len(taps)
size = max(16, 1 << (n - 1).bit_length())      # 16, 32, 64 or 128
q = [max(-32768, min(32767, round(t / total * 32767))) for t in taps]
q += [0] * (size - n)                          # zero padding

print(f"{n} taps -> {size} uploaded, decimation/interpolation {ratio}", file=sys.stderr)
print(f"RX 3 GAIN 0 DEC {ratio}")
print(f"TX 3 GAIN 0 INT {ratio}")
for c in q:
    print(f"{c},{c}")
PYEOF
}

# Convert the tap file once into FIR_CFG, then load it into the device.
build_fir() {
    [[ -f $1 ]] || die "tap file not found: $1"
    FIR_CFG=$(fir_config "$1" "${DEC:-1}") || die "tap conversion failed"
}

load_fir() {
    log "loading FIR"
    if [[ -z $BW ]]; then
        rx_attr rf_bandwidth "$DEFAULT_BW_HZ"
    fi
    rx_attr filter_fir_en 0
    dev_attr filter_fir_config "$FIR_CFG"
    rx_attr filter_fir_en 1
}

# Apply only what was given. Order: rate, LO, gain, bandwidth, FIR.
apply_config() {
    if [[ -n $RATE ]]; then
        log "sampling rate: $RATE MHz"
        rx_attr sampling_frequency "$RATE_HZ"
    fi
    if [[ -n $FREQ ]]; then
        log "LO frequency: $FREQ Hz"
        lo_attr frequency "$FREQ"
    fi
    if [[ -n $GAIN ]]; then
        log "hardware gain: $GAIN dB"
        rx_attr gain_control_mode manual
        rx_attr hardwaregain "$GAIN"
    fi
    if [[ -n $BW ]]; then
        log "bandwidth: $BW Hz"
        rx_attr rf_bandwidth "$BW"
    fi
    if [[ -n $TAPS ]]; then
        build_fir "$TAPS"
        load_fir
    fi
}

# ------------------------------------------------------------
# config: show the current config, change what was given
# ------------------------------------------------------------
cmd_config() {
    parse_opts "$@"
    check_tools
    device_rate >/dev/null
    apply_config
    status
}

# ------------------------------------------------------------
# start: apply options, then run  iio_rwdev | stream1090 | socat
# ------------------------------------------------------------
cmd_start() {
    local actual mhz
    parse_opts "$@"

    [[ -x $STREAM_BIN ]] || die "stream1090 binary not found: $STREAM_BIN"
    if [[ $OUT != - ]]; then
        command -v socat >/dev/null || die "socat is not installed"
    fi

    check_tools
    device_rate >/dev/null
    FREQ=${FREQ:-$DEFAULT_FREQ_HZ}
    apply_config

    actual=$(device_rate)
    if [[ -n $RATE && $actual != "$RATE_HZ" ]]; then
        log "WARNING: requested $RATE_HZ Hz, the device reports $actual Hz"
    fi
    mhz=${RATE:-$(awk -v h="$actual" 'BEGIN { printf "%g", h / 1000000 }')}

    echo >&2
    status >&2
    echo >&2
    log "streaming: $ANT ($mhz MHz) -> $(basename "$STREAM_BIN") -> $OUT"

    local src=(iio_rwdev -b "$BUFFER" -u "ip:$ANT" cf-ad9361-lpc voltage0 voltage1)
    local dec=("$STREAM_BIN" -v -s "$mhz" -u "$UP" ${EXTRA[@]+"${EXTRA[@]}"})

    sink() { if [[ $OUT == - ]]; then cat; else socat -u - "TCP4:$OUT"; fi; }

    if [[ -n $RECORD ]]; then
        "${src[@]}" | tee -- "$RECORD" | "${dec[@]}" | sink
    else
        "${src[@]}" | "${dec[@]}" | sink
    fi
}

# ------------------------------------------------------------
# main
# ------------------------------------------------------------
CMD=${1:-}
[[ $# -gt 0 ]] && shift
case "$CMD" in
    config)            cmd_config "$@" ;;
    start)             cmd_start "$@" ;;
    -h|--help|help|"") usage ;;
    *)                 die "unknown command: $CMD (try --help)" ;;
esac
