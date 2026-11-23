#!/bin/sh
# tools/armcount/build.sh OUT.elf BENCH.c [MORE.c ...] [-- CFLAGS ...]
#
# A bench for tools/armcount/armcount.py: BENCH.c (start from
# tools/armcount/bench.c) and the plugin's files it calls, built for the
# cart's Cortex-M33 with the pico-sdk's flags, everything in SRAM. The
# plugin's own optimisation goes after --, the same as in its
# CMakeLists.txt (-O2 by default; -O3 -fno-data-sections for an
# emulator's files means building those apart: see porting.md). The SDK's
# folder and the kit's lib/ are on the include path.
set -e
here=$(cd "$(dirname "$0")" && pwd)
kit=$(cd "$here/../.." && pwd)
sdk=$kit/sdk/firmware/revc/sdk
out=${1:?usage: tools/armcount/build.sh OUT.elf BENCH.c [MORE.c ...] [-- CFLAGS ...]}
shift
files=
while [ $# -gt 0 ] && [ "$1" != -- ]; do files="$files $1"; shift; done
[ "$1" = -- ] && shift
[ -n "$files" ] || { echo "build.sh: which bench? (tools/armcount/bench.c is one)" >&2; exit 2; }
# shellcheck disable=SC2086
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp \
    -O2 -std=c11 -ffunction-sections -fdata-sections -I"$sdk" -I"$kit/lib" "$@" \
    $files -nostartfiles -T "$here/bench.ld" --specs=nano.specs -Wl,--gc-sections -lm -o "$out"
