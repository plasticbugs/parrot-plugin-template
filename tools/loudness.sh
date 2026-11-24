#!/bin/sh
# tools/loudness.sh PLUGIN_DIR [SECONDS] [SCRIPT]
# tools/loudness.sh RUN_DIR
#
# How loud a plugin is next to ordinary cartridges (docs/testing.md, "How
# loud"). For screen plugins and frame plugins alike.
#
# Given a plugin's sources (src/, or a folder like examples/sound), it
# builds them for this computer (test/host/build.sh) and runs them for
# SECONDS (30 if not given). SCRIPT is the run's PC_SCRIPT, the controls
# that make the plugin play (test/host/host.c says what it can hold).
# Other PC_* settings come from the environment (PC_CARD, PC_OPEN ...).
# Given the folder of a run already made (one of test/run.sh's, with
# lists.bin in it), it measures that run.
#
# Headless MAME then plays what the plugin sent to the SDK's real kernel
# and records it (tools/mame-replay.sh with MAME_WAV: a screen plugin's
# sound is in sound.bin, a frame plugin's in its lists), and
# tools/wavlevel.py --check measures the recording. Without MAME, or the
# ColecoVision BIOS (MAME_ROMPATH=dir holding coleco.zip), or with
# NO_MAME=1, it works the level out from the run instead
# (tools/listlevel.py --check), within about half a dB of MAME. Either way
# it says OK, or FAIL and exits 1: louder than the loudest cartridge
# measured, more than 4 dB under their average, or clipped.
#
#   tools/loudness.sh src 30 "300:key=1 400:key="
#
# The run and the recording (plugin.wav) are left in the run's folder.
set -e
kit=$(cd "$(dirname "$0")/.." && pwd)
usage="usage: tools/loudness.sh PLUGIN_DIR [SECONDS] [SCRIPT], or tools/loudness.sh RUN_DIR"
dir=${1:?$usage}
[ -d "$dir" ] || { echo "loudness: no folder $dir" >&2; exit 2; }
dir=$(cd "$dir" && pwd)
if [ -f "$dir/lists.bin" ]; then
    run=$dir
    secs=${2:-600}                      # (MAME's backstop: the replay stops when the run's sound ends)
else
    secs=${2:-30}
    name=$(basename "$dir")
    [ "$name" = src ] && name=$(basename "$(dirname "$dir")")
    run=${TMPDIR:-/tmp}/parrotcart-test/$name-loudness
    rm -rf "$run"; mkdir -p "$run"
    if ! "$kit/test/host/build.sh" "$dir" "$run/run" 2>"$run/build.log"; then
        echo "loudness: the Mac build failed:"; sed 's/^/  /' "$run/build.log" | head -20; exit 1
    fi
    PC_OUT=$run PC_END=$((secs * 1000)) PC_SCRIPT=${3:-$PC_SCRIPT} "$run/run" ||
        { echo "loudness: the run failed ($run/log.txt says why)"; exit 1; }
fi

have_mame=1
[ -n "$NO_MAME" ] && have_mame=0
command -v mame >/dev/null 2>&1 && command -v sjasmplus >/dev/null 2>&1 || have_mame=0
# (MAME is headless, always, as tools/mame-replay.sh runs it)
if [ $have_mame = 1 ] && ! SDL_VIDEODRIVER=dummy mame -verifyroms coleco ${MAME_ROMPATH:+-rompath "$MAME_ROMPATH"} \
        -video none -videodriver dummy -window -nomaximize -sound none \
        -nothrottle -skip_gameinfo -seconds_to_run 10 >/dev/null 2>&1; then
    have_mame=0
fi

rm -f "$run/plugin.wav"
if [ $have_mame = 1 ] &&
   MAME_WAV=$run/plugin.wav "$kit/tools/mame-replay.sh" "$run/lists.bin" "$run/loudness-mame" $((secs + 30)) \
       >"$run/loudness-mame.log" 2>&1 && [ -s "$run/plugin.wav" ]; then
    python3 -I "$kit/tools/listlevel.py" "$run/lists.bin" | sed 's/^/(worked out) /'
    exec python3 -I "$kit/tools/wavlevel.py" --check "$run/plugin.wav"
fi
[ $have_mame = 1 ] && { echo "(MAME's recording failed: $run/loudness-mame.log. Working it out instead.)"; }
[ $have_mame = 1 ] || echo "(no MAME, or no BIOS where it looks: worked out from the run, not recorded)"
exec python3 -I "$kit/tools/listlevel.py" --check "$run/lists.bin"
