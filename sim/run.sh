#!/bin/sh
# sim/run.sh PLUGIN_DIR [options] -- a plugin in the simulator: built for
# this computer against the console's models, then run in a window with
# sound and controls (docs/simulator.md).
#
#   sim/run.sh src                      the template's plugin
#   sim/run.sh examples/sound           an example
#   sim/run.sh src --card ~/sdcard      with a folder as the SD card
#   sim/run.sh src --help               the simulator's options and keys
#
# PLUGIN_DIR is the folder with the plugin's .c (and .cpp) files. For a
# plugin that needs more to build, anywhere among the simulator's options:
#   --src PATTERN     more sources, relative to PLUGIN_DIR (--src '../lib/*.c')
#   --vendor PATTERN  someone else's sources (an emulator's core), relative
#                     to PLUGIN_DIR: like --src, but built without UBSan and
#                     without warnings, as their own build would. ASan stays
#   --include DIR     another include folder, relative to PLUGIN_DIR (or, if
#                     it isn't there, to the current folder)
#   --define NAME[=V] a define
#   --no-sanitize     without ASan and UBSan (faster)
# test/host/build.sh takes the same --src, --vendor, --include and
# --define. Everything else goes to the simulator, with --out, --shot,
# --shots and --wav made full paths first, so a wrapper script that calls
# this from another folder gets its files where it asked. It is built in
# build-sim/NAME; building again takes seconds. On Windows it runs in Git
# Bash (with Visual Studio) and in MSYS2's shells (with their compilers).
set -e
case "$1" in
    ""|-h|--help) sed -n '2,29p' "$0" | grep '^#' | sed 's/^# \{0,1\}//'; exit 0 ;;
esac
# a folder's full path, as the compiler wants it (C:/... on Windows)
full() { (cd "$1" && { pwd -W 2>/dev/null || pwd; }); }
kit=$(full "$(dirname "$0")/..")
dir=$(full "$1") || exit 2
shift
name=$(basename "$dir")
[ "$name" = src ] && name=$(basename "$(dirname "$dir")")
build=$kit/build-sim/$name

here=$( { pwd -W 2>/dev/null || pwd; } )
# a path the simulator writes, as a full path from the folder this was run in
abs() { case "$1" in /*|[A-Za-z]:[/\\]*) printf '%s\n' "$1" ;; *) printf '%s/%s\n' "$here" "$1" ;; esac; }
# an include folder: in PLUGIN_DIR, as --src is, or else in the current folder
inc() {
    case "$1" in /*|[A-Za-z]:[/\\]*) d=$1 ;; *) if [ -d "$dir/$1" ]; then d=$dir/$1; else d=$1; fi ;; esac
    [ -d "$d" ] || { echo "sim/run.sh: --include $1: no such folder, in $dir or here" >&2; exit 2; }
    full "$d"
}

# the build's options, from anywhere among the simulator's (each argument is
# taken off the front and the simulator's put back at the end)
sources= vendor= includes= defines= sanitize=-USIM_SANITIZE
n=$#
while [ $n -gt 0 ]; do
    a=$1; shift; n=$((n - 1))
    case "$a" in
        --src|--vendor|--include|--define|--out|--shot|--shots|--wav)
            [ $n -gt 0 ] || { echo "sim/run.sh: $a needs a value" >&2; exit 2; }
            v=$1; shift; n=$((n - 1)) ;;
    esac
    case "$a" in
        --src) sources="$sources${sources:+;}$v" ;;
        --vendor) vendor="$vendor${vendor:+;}$v" ;;
        --include) i=$(inc "$v") || exit 2; includes="$includes${includes:+;}$i" ;;
        --define) defines="$defines${defines:+;}$v" ;;
        --no-sanitize) sanitize=-DSIM_SANITIZE=OFF ;;
        --out|--shot|--shots|--wav) set -- "$@" "$a" "$(abs "$v")" ;;
        *) set -- "$@" "$a" ;;
    esac
done

# the build tool: CMAKE_GENERATOR if set; else Ninja, but on Windows only in
# an MSYS2 shell with its own compiler (Git Bash: CMake's choice, Visual Studio)
mkdir -p "$build"
gen=
if [ -z "$CMAKE_GENERATOR" ] && [ ! -f "$build/CMakeCache.txt" ]; then
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*)
            if [ -n "$MSYSTEM_PREFIX" ] && { [ -x "$MSYSTEM_PREFIX/bin/gcc.exe" ] || [ -x "$MSYSTEM_PREFIX/bin/clang.exe" ]; }; then
                if command -v ninja >/dev/null 2>&1; then gen=Ninja; else gen="MSYS Makefiles"; fi
            fi ;;
        *) command -v ninja >/dev/null 2>&1 && gen=Ninja ;;
    esac
fi
if ! cmake -S "$kit/sim" -B "$build" ${gen:+-G "$gen"} -DPLUGIN_DIR="$dir" -DPLUGIN_SOURCES="$sources" \
        -DPLUGIN_VENDOR="$vendor" -DPLUGIN_INCLUDES="$includes" -DPLUGIN_DEFINES="$defines" $sanitize \
        >"$build/configure.log" 2>&1; then
    cat "$build/configure.log" >&2
    exit 1
fi
if ! cmake --build "$build" --config Release >"$build/build.log" 2>&1; then
    grep -E 'error|Error' "$build/build.log" | head -40 >&2
    echo "sim/run.sh: the build failed; all of it is in $build/build.log" >&2
    exit 1
fi
warnings=$(grep -v '^ld: ' "$build/build.log" | grep -Ec 'warning( C[0-9]+)?:' || true)
[ "$warnings" -gt 0 ] && echo "($warnings compiler warnings: $build/build.log)"
exe=$build/parrotsim
[ -x "$exe.exe" ] && exe=$exe.exe
exec "$exe" --out "$build/out" "$@"
