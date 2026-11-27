#!/bin/sh
# test/host/build.sh PLUGIN_DIR OUT_BINARY [options] -- a plugin's own sources
# built for the Mac against test/host/host.c (which see). Every .c in
# PLUGIN_DIR is compiled as it is, its main() renamed, except files that
# talk to TinyUSB (#include "tusb.h"): host.c answers for those. The USB
# features its cvcart_features() turns on (in PLUGIN_DIR/CMakeLists.txt, or
# the one above it for src/) are the CVUSB_* switches of everything built
# here: test/host/plugin.cmake reads them (so needs CMake), for the
# simulator too. The SDK's optional parts (swzip.c, which a plugin adds to
# its CMake sources, and cvfont.c and cvedit.c, which cvfont_use() and
# cvedit_use() add) are linked in when the plugin calls them.
#
# For a plugin that needs more to build, the simulator's options
# (sim/run.sh), each as many times as needed:
#   --src PATTERN     more .c files, relative to PLUGIN_DIR (--src '../lib/*.c')
#   --vendor PATTERN  someone else's .c files (an emulator's core): like
#                     --src, but built without UBSan and without warnings,
#                     as their own build would. ASan stays
#   --include DIR     another include folder, relative to PLUGIN_DIR (or, if
#                     it isn't there, to the current folder)
#   --define NAME[=V] a define
# A pattern is the shell's, with no spaces in it. test/run.sh's run passes
# these on (run NAME DIR [options] [patterns]).
set -e
kit=$(cd "$(dirname "$0")/../.." && pwd)
usage="usage: test/host/build.sh PLUGIN_DIR OUT_BINARY [--src P] [--vendor P] [--include DIR] [--define NAME]"
dir=$(cd "${1:?$usage}" && pwd)
out=${2:?$usage}
shift 2
sdk=$kit/sdk/firmware/revc
obj=$out.o.d
rm -rf "$obj"
mkdir -p "$obj"
nl='
'
# the files a pattern names, each a full path on a line of its own
files_of() {
    # shellcheck disable=SC2086
    case "$1" in /*) set -- $1 ;; *) set -- "$dir"/$1 ;; esac
    [ -e "$1" ] || { echo "test/host/build.sh: nothing is $1" >&2; return 1; }
    for f in "$@"; do printf '%s/%s\n' "$(cd "$(dirname "$f")" && pwd)" "$(basename "$f")"; done
}
srcs= vendor= extra=
while [ $# -gt 0 ]; do
    [ $# -gt 1 ] || { echo "test/host/build.sh: $1 needs a value" >&2; exit 2; }
    case "$1" in
        --src) srcs="$srcs$(files_of "$2")$nl" ;;
        --vendor) vendor="$vendor$(files_of "$2")$nl" ;;
        --include)
            case "$2" in /*) d=$2 ;; *) if [ -d "$dir/$2" ]; then d=$dir/$2; else d=$2; fi ;; esac
            [ -d "$d" ] || { echo "test/host/build.sh: --include $2: no such folder, in $dir or here" >&2; exit 2; }
            extra="$extra -I$(cd "$d" && pwd)" ;;
        --define) extra="$extra -D$2" ;;
        *) echo "test/host/build.sh: $1? (--src, --vendor, --include, --define)" >&2; exit 2 ;;
    esac
    shift 2
done

# the USB features, as -DCVUSB_PADS=1 ...: read by plugin.cmake, as the
# simulator reads them
features=$(cmake -DPLUGIN_DIR="$dir" -P "$kit/test/host/plugin.cmake")
[ -z "$features" ] || features=" $features"
flags="-std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wno-missing-field-initializers
  -I$kit/test/host/include -I$dir$extra -I$kit/lib -I$sdk/sdk -I$sdk/midihello$features"

# PLUGIN_DIR's .c files and --src's, then --vendor's, each once (a file of
# PLUGIN_DIR named by --vendor too is built as someone else's)
list=$( { for c in "$dir"/*.c; do [ -e "$c" ] && printf '%s\n' "$c"; done; printf '%s' "$srcs"; } | awk 'NF && !seen[$0]++')
vendor=$(printf '%s' "$vendor" | awk 'NF && !seen[$0]++')
n=0
build() {       # build FILE [more flags]: one of the plugin's files
    case "$1" in *.c) ;; *) echo "test/host/build.sh: $1: C only here (the simulator builds C++ too)" >&2; exit 2 ;; esac
    c=$1; shift
    n=$((n + 1))
    # shellcheck disable=SC2086
    cc $flags "$@" -Dmain=plugin_main -c "$c" -o "$obj/plugin$n.o"
}
IFS=$nl
for c in $list; do
    IFS=' 	'"$nl"
    if ! printf '%s\n' "$vendor" | grep -q -x -F -- "$c" && ! grep -q '#include "tusb.h"' "$c"; then build "$c"; fi
done
IFS=$nl
for c in $vendor; do
    IFS=' 	'"$nl"
    build "$c" -w -fno-sanitize=undefined
done
IFS=' 	'"$nl"
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
