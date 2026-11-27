#!/bin/sh
# lib/test.sh -- the smoke tests of lib/'s shared code, on this computer,
# with the address and undefined-behaviour checkers. Each core's proof
# against MAME is in its README; this checks the code still builds and
# behaves. CC=gcc lib/test.sh to try another compiler.
set -e
kit=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
out=$(mktemp -d "${TMPDIR:-/tmp}/parrotcart-lib.XXXXXX")
trap 'rm -rf "$out"' EXIT
flags="-std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-missing-field-initializers"
sdk=$kit/sdk/firmware/revc/sdk
for t in z80 sched mcs48 sp0250 m6809 m6800 scalen dial nescv nescv-fast; do
    extra= src=$t
    case $t in
        scalen) extra="$kit/lib/scalen/scalen.c $sdk/cvscale.c" ;;
        nescv) extra="$kit/lib/nescv/nescv.c" ;;
        nescv-fast) src=nescv extra="-DNESCV_FAST_KEYS $kit/lib/nescv/nescv.c" ;;
    esac
    # shellcheck disable=SC2086
    $cc $flags -I"$kit/lib" -I"$sdk" "$kit/lib/$src/test.c" $extra -o "$out/$t"
    "$out/$t"
done
