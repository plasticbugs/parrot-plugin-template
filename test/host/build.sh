#!/bin/sh
# test/host/build.sh PLUGIN_DIR OUT_BINARY -- a plugin's own sources built
# for the Mac against test/host/host.c (which see). Every .c in PLUGIN_DIR is
# compiled as it is, its main() renamed, except files that talk to TinyUSB
# (#include "tusb.h"): host.c answers for those. The USB features its
# cvcart_features() turns on (in PLUGIN_DIR/CMakeLists.txt, or the one
# above it for src/) are the CVUSB_* switches of everything built here.
set -e
kit=$(cd "$(dirname "$0")/../.." && pwd)
dir=$1
out=$2
sdk=$kit/sdk/firmware/revc
obj=$out.o.d
rm -rf "$obj"
mkdir -p "$obj"
cm=$dir/CMakeLists.txt
[ -f "$cm" ] || cm=$dir/../CMakeLists.txt
features=""
if [ -f "$cm" ]; then
    # the words inside cvcart_features( ... ), comments left out
    for f in $(sed 's/#.*//' "$cm" | tr '\n' ' ' | sed -n 's/.*cvcart_features([^ )]*\([^)]*\)).*/\1/p'); do
        case $f in PADS|KEYBOARD|MOUSE|MIDI_IN|MIDI_OUT) features="$features -DCVUSB_$f=1" ;; esac
    done
fi
flags="-std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers
  -I$kit/test/host/include -I$dir -I$kit/lib -I$sdk/sdk -I$sdk/midihello$features"
n=0
for c in "$dir"/*.c; do
    grep -q '#include "tusb.h"' "$c" && continue
    n=$((n + 1))
    # shellcheck disable=SC2086
    cc $flags -Dmain=plugin_main -c "$c" -o "$obj/plugin$n.o"
done
# shellcheck disable=SC2086
cc $flags -Dcvs_update=sdk_cvs_update -c "$sdk/sdk/cvscreen.c" -o "$obj/cvscreen.o"
# shellcheck disable=SC2086
cc $flags "$kit/test/host/host.c" "$kit/test/host/tms9918.c" "$sdk/midihello/vdp.c" "$sdk/sdplayer/fsro.c" \
    "$sdk/sdk/cvusb_keys.c" "$sdk/sdk/cvusb_midi.c" "$obj"/*.o -lz -o "$out"
