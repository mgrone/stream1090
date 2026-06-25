# Building a .deb Package for stream1090

This guide covers building and installing stream1090 as a Debian package on
Debian 12 (Bookworm), Ubuntu 22.04 LTS, Ubuntu 24.04 LTS, and compatible
derivatives such as Raspberry Pi OS.

## Prerequisites

Install the build tools and device library headers:

```bash
sudo apt install devscripts debhelper cmake pkg-config g++
```

Add whichever device library headers match your hardware:

```bash
# For RTL-SDR dongles
sudo apt install librtlsdr-dev

# For Airspy
sudo apt install libairspy-dev
```

Both can be installed at the same time; the build system detects what is
available and enables support accordingly.

## Building the Package

Clone the repository and run `dpkg-buildpackage` from the project root:

```bash
git clone git clone -b deb https://github.com/jprochazka/stream1090.git
cd stream1090
dpkg-buildpackage -us -uc -b
```

`-b` builds a binary-only package (no source package).  
`-us -uc` skips GPG signing, which is fine for local use.

The resulting `.deb` file is placed one directory above the project root:

```bash
ls ../stream1090_*.deb
```

### What the package build does differently from a manual build

The Debian build passes two flags that differ from the defaults:

| Flag | Package value | Reason |
|------|--------------|--------|
| `ENABLE_STATS` | `OFF` | Statistics are printed to stderr, which produces noise in journal logs when running as a service |
| `ENABLE_RTLSDR_BLOG` | `OFF` | The vendored RTL-SDR Blog fork conflicts with the system `librtlsdr0`; use the system library instead |

If you need the RTL-SDR Blog fork or statistics output, build from source
using the [standard instructions](./README.md#compiling-stream1090).

## Installing the Package

```bash
sudo dpkg -i ../stream1090_*.deb
sudo apt-get install -f   # resolves any missing runtime dependencies
```

Verify the installation:

```bash
stream1090 -h
```

The second line of the help output (`Native device support: ...`) lists which
device backends were compiled in.

## Configuration

The service reads its options from `/etc/stream1090/stream1090.conf`.
Edit this file to match your hardware before starting the service:

```bash
sudo nano /etc/stream1090/stream1090.conf
```

The default contents are:

```bash
STREAM1090_OPTS="-s 2.4 -d /etc/stream1090/rtlsdr.ini"
READSB_PORT=30001
```

Adjust `STREAM1090_OPTS` for your setup:

| Hardware | Minimum options |
|----------|----------------|
| RTL-SDR  | `-s 2.4 -d /etc/stream1090/rtlsdr.ini` |
| RTL-SDR (higher rate) | `-s 2.56 -d /etc/stream1090/rtlsdr.ini` |
| Airspy   | `-s 6 -d /etc/stream1090/airspy.ini` |
| Airspy (10 Msps) | `-s 10 -d /etc/stream1090/airspy.ini` |

Add `-u <rate>` to enable upsampling, or `-q` to enable the IQ FIR filter.
See [README.md](./README.md) for the full list of supported rate combinations.

Device-specific settings (gain, bias-tee, serial number, etc.) are in the
corresponding `.ini` file under `/etc/stream1090/`.

## Running as a Service

The package installs a systemd unit that pipes stream1090's output to
`socat`, which forwards it to readsb or dump1090-fa over TCP.

Install `socat` if you haven't already:

```bash
sudo apt install socat
```

Configure your decoder to accept raw input on port 30001 — see
[README.md](./README.md#stack-integration) for readsb and dump1090-fa
instructions.

Enable and start the service:

```bash
sudo systemctl enable stream1090
sudo systemctl start stream1090
```

Check that it is running:

```bash
sudo systemctl status stream1090
```

View live log output:

```bash
sudo journalctl -u stream1090 -f
```

## Uninstalling

```bash
sudo apt remove stream1090
```

Configuration files under `/etc/stream1090/` are preserved. To remove them
as well:

```bash
sudo apt purge stream1090
```

## Alternative: CPack

If you want to produce a `.deb` without the full Debian toolchain, CMake's
built-in CPack support can generate one directly from the build directory.
This skips policy checks and conffile handling but is useful for quick
distribution.

```bash
mkdir build && cd build
cmake ../ -DCMAKE_BUILD_TYPE=Release -DENABLE_STATS=OFF
make
cpack
```

The resulting `.deb` appears in the `build/` directory.
