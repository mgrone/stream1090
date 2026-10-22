#!/usr/bin/env bash
#
# ant.sh - drive an AntSDR (AD9361) with the local libiio 1.0 build and feed stream1090_ad936x
#
#   ant.sh start  -s <MHz> -g <dB> [-u <factor>] [options] [-- <stream1090 args>]
#   ant.sh filter <tapfile> [-y] [options]
#   ant.sh status [options]
#
# Put this script in the stream1090 repo root, or set STREAM1090_DIR.
# The libiio tools come from thirdparty/libiio/build and NOT from the system:
# older libiio 1.0 builds have a bug when uploading FIR filters with > 16 taps.

set -euo pipefail

# ------------------------------------------------------------
# Defaults (override with environment variables or options)
# ------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${STREAM1090_DIR:-$SCRIPT_DIR}/../"
IIO_PREFIX="${IIO_PREFIX:-$REPO_DIR/thirdparty/libiio/build}"
STREAM_BIN="${STREAM1090_BIN:-$REPO_DIR/build/stream1090_ad936x}"

ANT="${ANT_HOST:-ant}"                 # AntSDR host name or IP
OUT="${ANT_OUT:-raspberrypi:30001}"    # host:port for socat, or "-" for stdout
FREQ_HZ=1090000000
BW_HZ=4000000                          # 4 MHz is crucial for Mode-S
BUFFER=262144                          # samples per iio_rwdev buffer

export PATH="$IIO_PREFIX/utils:$PATH"
export LD_LIBRARY_PATH="$IIO_PREFIX${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# ------------------------------------------------------------
# Helpers
# ------------------------------------------------------------
die()  { echo "ant.sh: $*" >&2; exit 1; }
log()  { echo "[ant] $*" >&2; }

need_val() { [[ $# -ge 2 ]] || die "option $1 needs a value"; }

usage() {
    cat <<'EOF'
Usage:
  ant.sh start  -s <MHz> -g <dB> [-u <factor>] [options] [-- <stream1090 args>]
  ant.sh filter <tapfile> [-y] [options]
  ant.sh status [options]

start options:
  -s, --rate MHz        sampling rate in MHz, decimals allowed (e.g. 2.4, 12)
  -g, --gain dB         hardware gain
  -u, --upsample N      stream1090 upsampling factor (default 1)
      --taps FILE       upload this FIR to the AntSDR before streaming
      --record FILE     also write the raw samples to FILE (for replays)
  -o, --out HOST:PORT   send output via socat (default raspberrypi:30001), "-" = stdout
      --buffer N        iio_rwdev buffer size in samples (default 262144)
      --bin PATH        stream1090 binary (default build/stream1090_ad936x)
  Everything after "--" is passed to stream1090, e.g.:  ant.sh start -s 12 -g 60 -- -q

common options:
      --ant HOST        AntSDR host (default: $ANT_HOST or "ant")
      --bw HZ           RF bandwidth in Hz (default 4000000)

Environment: ANT_HOST, ANT_OUT, STREAM1090_DIR, STREAM1090_BIN, IIO_PREFIX
EOF
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
    echo "FIR enabled:";        rx_attr filter_fir_en
    echo "FIR config:";         dev_attr filter_fir_config
    echo "RX path rates:";      dev_attr rx_path_rates
    echo "Sampling frequency:"; rx_attr sampling_frequency
    echo "RF bandwidth:";       rx_attr rf_bandwidth
    echo "Hardware gain:";      rx_attr hardwaregain
}

# ------------------------------------------------------------
# FIR: tap file (one float per line, '#' = comment) -> AD9361 filter_fir_config text
# ------------------------------------------------------------
fir_config() {
    python3 - "$1" <<'PYEOF'
import sys

taps = []
with open(sys.argv[1]) as f:
    for line in f:
        line = line.strip()
        if line and not line.startswith("#"):
            taps.append(float(line))

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

print(f"{n} taps -> {size} uploaded", file=sys.stderr)
print("RX 3 GAIN 0 DEC 1")
print("TX 3 GAIN 0 INT 1")
for c in q:
    print(f"{c},{c}")
PYEOF
}

# Convert the tap file once into FIR_CFG, then load it into the device.
# (No prompting in here: functions called from an "||" list would run without "set -e".)
build_fir() {
    [[ -f $1 ]] || die "tap file not found: $1"
    FIR_CFG=$(fir_config "$1") || die "tap conversion failed"
}

load_fir() {
    log "setting bandwidth, loading FIR"
    rx_attr rf_bandwidth "$BW_HZ"
    rx_attr filter_fir_en 0
    dev_attr filter_fir_config "$FIR_CFG"
    rx_attr filter_fir_en 1
}

# ------------------------------------------------------------
# start: configure the AntSDR, then run  iio_rwdev | stream1090 | socat
# ------------------------------------------------------------
cmd_start() {
    local rate="" up=1 gain="" taps="" record="" hz
    local extra=()

    while (($#)); do
        case "$1" in
            -s|--rate)     need_val "$@"; rate=$2;      shift 2 ;;
            -u|--upsample) need_val "$@"; up=$2;        shift 2 ;;
            -g|--gain)     need_val "$@"; gain=$2;      shift 2 ;;
            --taps)        need_val "$@"; taps=$2;      shift 2 ;;
            --record)      need_val "$@"; record=$2;    shift 2 ;;
            -o|--out)      need_val "$@"; OUT=$2;       shift 2 ;;
            --buffer)      need_val "$@"; BUFFER=$2;    shift 2 ;;
            --bin)         need_val "$@"; STREAM_BIN=$2; shift 2 ;;
            --ant)         need_val "$@"; ANT=$2;       shift 2 ;;
            --bw)          need_val "$@"; BW_HZ=$2;     shift 2 ;;
            -h|--help)     usage; exit 0 ;;
            --)            shift; extra=("$@"); break ;;
            *)             die "unknown option: $1 (see --help)" ;;
        esac
    done

    [[ $rate =~ ^[0-9]+(\.[0-9]+)?$ ]]  || die "-s <MHz> is required (e.g. 4 or 2.4)"
    [[ $gain =~ ^-?[0-9]+(\.[0-9]+)?$ ]] || die "-g <dB> is required"
    [[ $up   =~ ^[0-9]+$ ]]             || die "-u must be an integer"
    [[ $BW_HZ =~ ^[0-9]+$ ]]            || die "--bw must be an integer in Hz"
    [[ -x $STREAM_BIN ]]                || die "stream1090 binary not found: $STREAM_BIN"
    if [[ $OUT != - ]]; then
        command -v socat >/dev/null || die "socat is not installed"
    fi

    hz=$(awk -v m="$rate" 'BEGIN { printf "%.0f", m * 1000000 }')

    check_tools
    device_rate >/dev/null
    log "configuring $ANT: $rate MHz, gain $gain dB, bandwidth $BW_HZ Hz"

    rx_attr sampling_frequency "$hz"
    lo_attr frequency "$FREQ_HZ"
    rx_attr gain_control_mode manual
    rx_attr hardwaregain "$gain"
    rx_attr rf_bandwidth "$BW_HZ"

    if [[ -n $taps ]]; then
        build_fir "$taps"
        load_fir
    fi

    echo >&2
    status >&2
    echo >&2

    log "streaming: $ANT -> $(basename "$STREAM_BIN") -> $OUT"

    local src=(iio_rwdev -b "$BUFFER" -u "ip:$ANT" cf-ad9361-lpc voltage0 voltage1)
    local dec=("$STREAM_BIN" -v -s "$rate" -u "$up" ${extra[@]+"${extra[@]}"})

    sink() { if [[ $OUT == - ]]; then cat; else socat -u - "TCP4:$OUT"; fi; }

    if [[ -n $record ]]; then
        "${src[@]}" | tee -- "$record" | "${dec[@]}" | sink
    else
        "${src[@]}" | "${dec[@]}" | sink
    fi
}

# ------------------------------------------------------------
# filter: upload a FIR to the AntSDR
# ------------------------------------------------------------
cmd_filter() {
    local tapfile="" confirm=1 fs answer

    while (($#)); do
        case "$1" in
            --ant)     need_val "$@"; ANT=$2;   shift 2 ;;
            --bw)      need_val "$@"; BW_HZ=$2; shift 2 ;;
            -h|--help) usage; exit 0 ;;
            -*)        die "unknown option: $1 (see --help)" ;;
            *)         [[ -z $tapfile ]] || die "only one tap file allowed"; tapfile=$1; shift ;;
        esac
    done

    [[ -n $tapfile ]] || die "usage: ant.sh filter <tapfile> [-y] [--ant HOST]"
    [[ $BW_HZ =~ ^[0-9]+$ ]] || die "--bw must be an integer in Hz"
    check_tools
    fs=$(device_rate)
    log "device sample rate: $fs Hz"
    build_fir "$tapfile"

    echo
    echo "--- FIR CONFIG ---"
    echo "$FIR_CFG"
    echo "------------------"
    
    load_fir
    echo
    status
    echo "Done."
}

cmd_status() {
    while (($#)); do
        case "$1" in
            --ant)     need_val "$@"; ANT=$2; shift 2 ;;
            -h|--help) usage; exit 0 ;;
            *)         die "unknown option: $1 (see --help)" ;;
        esac
    done
    check_tools
    device_rate >/dev/null
    status
}

# ------------------------------------------------------------
# main
# ------------------------------------------------------------
cmd=${1:-}
[[ $# -gt 0 ]] && shift
case "$cmd" in
    start)   cmd_start "$@" ;;
    filter)  cmd_filter "$@" ;;
    status)  cmd_status "$@" ;;
    -h|--help|help|"") usage ;;
    *)       die "unknown command: $cmd (try --help)" ;;
esac
