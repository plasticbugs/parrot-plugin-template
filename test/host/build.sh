#!/bin/sh
# test/host/build.sh PLUGIN_DIR OUT_BINARY -- a plugin's own sources built
# for the Mac against test/host/host.c (which see). Every .c in PLUGIN_DIR is
# compiled as it is, its main() renamed, except files that talk to TinyUSB
# (#include "tusb.h"): host.c answers for those. The USB features its
# cvcart_features() turns on (in PLUGIN_DIR/CMakeLists.txt, or the one
# above it for src/) are the CVUSB_* switches of everything built here:
# test/host/plugin.cmake reads them (so needs CMake), for the simulator too.
# The SDK's optional parts (swzip.c, which a plugin adds to its CMake
# sources, and cvfont.c and cvedit.c, which cvfont_use() and cvedit_use()
# add) are linked in when the plugin calls them.
set -e
kit=$(cd "$(dirname "$0")/../.." && pwd)
dir=$1
out=$2
sdk=$kit/sdk/firmware/revc
obj=$out.o.d
rm -rf "$obj"
mkdir -p "$obj"
# the USB features, as -DCVUSB_PADS=1 ...: read by plugin.cmake, as the
# simulator reads them
features=$(cmake -DPLUGIN_DIR="$dir" -P "$kit/test/host/plugin.cmake")
[ -z "$features" ] || features=" $features"
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
# the SDK's parts a plugin adds to its sources when it uses them (swzip.c;
# cvfont.c and cvedit.c, by cvfont_use() and cvedit_use()), as a library:
# they fill in only what the plugin's own files don't have, so a plugin
# with its own copy still links
rm -f "$obj/libsdkextra.a"
for part in swzip cvfont cvedit; do
    # shellcheck disable=SC2086
    cc $flags -c "$sdk/sdk/$part.c" -o "$obj/$part.lib.o"
done
ar rcs "$obj/libsdkextra.a" "$obj"/*.lib.o
rm -f "$obj"/*.lib.o
# shellcheck disable=SC2086
cc $flags "$kit/test/host/host.c" "$kit/test/host/tms9918.c" "$sdk/midihello/vdp.c" "$sdk/sdplayer/fsro.c" \
    "$sdk/sdk/cvusb_keys.c" "$sdk/sdk/cvusb_midi.c" "$sdk/sdk/cvscale.c" "$obj"/*.o "$obj/libsdkextra.a" -lz -lm -o "$out"
