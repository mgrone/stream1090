#!/usr/bin/env bash
# Clone (or update) the latest libiio into thirdparty/libiio and build the CLI tools.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../"        # run from the repo root

SRC=thirdparty/libiio
if [ -d "$SRC/.git" ]; then
    git -C "$SRC" pull --ff-only
elif [ -e "$SRC" ]; then
    echo "$SRC exists but is not a git checkout, move it away first" >&2; exit 1
else
    git clone --depth 1 https://github.com/analogdevicesinc/libiio.git "$SRC"
fi

cmake -S "$SRC" -B "$SRC/build" -DCMAKE_BUILD_TYPE=Release \
      -DWITH_TESTS=OFF -DWITH_DOC=OFF -DPYTHON_BINDINGS=OFF -DCSHARP_BINDINGS=OFF
cmake --build "$SRC/build" -j"$(nproc)"

LD_LIBRARY_PATH="$SRC/build" "$SRC/build/utils/iio_info" -V
