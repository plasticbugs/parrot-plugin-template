#!/bin/sh
# sim/test.sh [NAME...] -- the simulator, checked headless: the kit's
# examples and the template's plugin, each built with sim/run.sh and run
# with --headless and a script of inputs, as test/run.sh runs them on the
# models alone. No window opens and nothing plays.
#
# For each: the log must say what test/run.sh's does; the picture must be a
# 256x192 screenshot; and where test/run.sh has run first (its pictures in
# $TMPDIR/parrotcart-test), the simulator's picture and log must be the
# same as its. Beyond that, what only the simulator does: the Sound
# example's scale, in tune in the WAV; the SD card as a folder, the save
# landing in it as a file; a file opened with --open.
#
#   sim/test.sh                 everything
#   sim/test.sh sound sd-card   just those
#
# The names: hello screen sound usb-gamepad usb-keyboard-mouse usb-midi-out
# sd-card open-file frames psram vdp. Pictures and logs: build-sim/test/.
set -e
# (on Windows, in Git Bash or MSYS2: C:/... paths throughout, and the shell
# told not to turn the card's "/NOTES/..." into a path on this computer)
kit=$(cd "$(dirname "$0")/.." && { pwd -W 2>/dev/null || pwd; })
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*'
out=$kit/build-sim/test
host=${TMPDIR:-/tmp}/parrotcart-test
mkdir -p "$out"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy  # (the simulator insists, headless, anyway)
py=
for p in python3 python; do "$p" -c 'import sys' >/dev/null 2>&1 && { py=$p; break; }; done
[ -n "$py" ] || { echo "sim/test.sh: needs Python 3"; exit 2; }
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) windows=1 ;; *) windows=0 ;; esac
status=0
ONLY="$*"
wanted() { [ -z "$ONLY" ] && return 0; case " $ONLY " in *" $1 "*) return 0 ;; esac; return 1; }
fail() { echo "  FAIL: $*"; status=1; }
compared=0

# run NAME DIR [simulator options] -- the plugin in DIR, headless; o= its output
run() {
    name=$1; dir=$2; shift 2
    o=$out/$name
    rm -rf "$o"; mkdir -p "$o"
    echo "$name"
    if "$kit/sim/run.sh" "$dir" --headless --out "$o" "$@" >"$o/run.txt" 2>&1; then :; else
        fail "the simulator stopped with an error:"; sed 's/^/    /' "$o/run.txt" | tail -15
    fi
    # (warnings fail the check, as in test/run.sh; not MSVC's, about code written for GCC)
    if grep -q 'compiler warnings' "$o/run.txt"; then
        if [ $windows = 1 ]; then grep 'compiler warnings' "$o/run.txt"; else fail "$(grep 'compiler warnings' "$o/run.txt")"; fi
    fi
    grep '^  [0-9]* ms' "$o/run.txt" || true
    [ -f "$o/screen.png" ] && { "$py" -I "$kit/tools/png256.py" check "$o/screen.png" >/dev/null || fail "screen.png isn't a 256x192 screenshot"; }
    same_as "$name"
}

# same_as NAME [picture] -- the last run's picture and log against test/run.sh's
# NAME, if it has run (the picture only: a card folder isn't macOS's image, so
# its sector reads, and the log's times, differ)
same_as() {
    [ -f "$host/$1/screen.png" ] || return 0
    compared=$((compared + 1))
    if "$py" -I "$kit/tools/png256.py" same "$o/screen.png" "$host/$1/screen.png" >"$o/same.txt" 2>&1; then
        echo "  test/host: the same picture"
    else fail "the picture isn't test/host's $1: $(cat "$o/same.txt")"; fi
    if [ "$2" != picture ] && [ -f "$host/$1/log.txt" ] && ! cmp -s "$o/log.txt" "$host/$1/log.txt"; then
        fail "the log isn't test/host's $1:"; diff "$host/$1/log.txt" "$o/log.txt" | head -8 | sed 's/^/    /'
    fi
}

# expect PATTERN... -- in the last run's log (grep -E)
expect() {
    for want in "$@"; do
        grep -Eq -- "$want" "$o/log.txt" || fail "nothing in $o/log.txt matches: $want"
    done
}

# a card, as a folder: test/run.sh's tree
tree() {
    rm -rf "$1"
    mkdir -p "$1/NOTES" "$1/PLUGINS"
    printf 'Hello from the card.\nThis is the first file.\n' > "$1/A first file.txt"
    i=1; : > "$1/NOTES/Long story.txt"
    while [ $i -le 60 ]; do echo "Line $i of a story long enough to scroll, and to wrap around the screen's 30 columns." >> "$1/NOTES/Long story.txt"; i=$((i + 1)); done
}

if wanted hello; then
    run hello "$kit/src" --ms 3000 --script "500:joy=up+fire 900:joy= 1200:key=5 1500:key= 2000:joy=fire2 2300:joy="
    expect 'note "Hello: 3 presses"'
fi
if wanted screen; then
    run screen "$kit/examples/screen" --ms 4000 --script "1000:joy=fire 1200:joy= 2000:snap=moving"
    expect 'back to the menu'
fi
if wanted sound; then
    run sound "$kit/examples/sound" --ms 3000 --script "300:key=1 600:key= 800:joy=up 900:joy= 1000:key=6 1400:key= 1600:joy=fire 1700:joy="
    expect 'sound 26[123]Hz@2 - - noise@15' 'sound 88[0-9]Hz@2 - - noise@15' 'noise control E5 \(white noise, rate 1\)' 'sound - - - noise@0' 'sound - - - noise@14'
    # the scale, keys 1-8 held in turn, in the WAV: each note the chip's pitch for
    # it, within 1% (17 cents) of the true note, and silence between
    s= t=500
    for k in 1 2 3 4 5 6 7 8; do s="$s $t:key=$k $((t + 400)):key="; t=$((t + 500)); done
    run sound-scale "$kit/examples/sound" --ms 5000 --script "$s" --wav "$out/sound-scale/scale.wav"
    "$py" -I "$kit/sim/wavcheck.py" "$o/scale.wav" 0-450:0 600-880:261.63 1100-1380:293.66 1600-1880:329.63 2100-2380:349.23 \
        2600-2880:392 3100-3380:440 3600-3880:493.88 4100-4380:523.25 4500-4950:0 || fail "the scale isn't in tune"
fi
if wanted usb-gamepad; then
    run usb-gamepad-none "$kit/examples/usb-gamepad" --ms 1000
    expect 'usb: the port is a host'
    run usb-gamepad "$kit/examples/usb-gamepad" --ms 2500 --script "500:pad=south+l+up 800:lstick=1500,-900 900:rstick=-2048,2047 1500:pad=east+plus"
    expect 'usb: the port is a host'
fi
if wanted usb-keyboard-mouse; then
    run usb-keyboard-mouse "$kit/examples/usb-keyboard-mouse" --ms 2500 --script "300:keydown=0B 320:keyup=0B 340:keydown=08 360:keyup=08 380:keydown=0F 400:keyup=0F 420:keydown=0F 440:keyup=0F 460:keydown=12 480:keyup=12 500:keydown=28 520:keyup=28 540:keydown=12 560:keydown=0E 580:keydown=1B 600:keydown=2A 1000:mouse=60,30,1"
    expect 'usb: the port is a host'
fi
if wanted usb-midi-out; then
    run usb-midi-out "$kit/examples/usb-midi-out" --ms 2500 --script "300:key=1 600:key=3 900:key= 1200:key=8 1500:key="
    expect 'midi 90 3C 64' 'midi 80 3C 00' 'midi 90 40 64' 'midi 80 40 00' 'midi 90 48 64' 'midi 80 48 00'
fi
if wanted sd-card; then
    run sd-card-nocard "$kit/examples/sd-card" --ms 1500
    expect 'back to the menu'
    # a folder as the card, three runs: the save is made, then counted up, in the folder
    tree "$out/card"; tree "$out/card-before"
    for n in 1 2 3; do
        run "sd-card-folder-$n" "$kit/examples/sd-card" --card "$out/card" --ms 2500
        expect "note \"SD card example: run $n\""
    done
    got=$("$py" -I -c 'import struct,sys; d=open(sys.argv[1],"rb").read(); print(len(d), d[:7].decode(), struct.unpack_from("<I", d, 8)[0])' "$out/card/SDCARD.SAV" 2>&1) || true
    if [ "$got" = "512 PCSAVE1 3" ]; then echo "  the folder: SDCARD.SAV is 512 bytes, run 3"
    else fail "SDCARD.SAV in the folder: $got (wanted run 3)"; fi
    rm -f "$out/card/SDCARD.SAV"
    diff -r "$out/card" "$out/card-before" >/dev/null || fail "the card folder's other files changed"
fi
if wanted open-file; then
    run open-file-none "$kit/examples/open-file" --ms 1000
    expect 'back to the menu'
    tree "$out/card"
    run open-file-folder-short "$kit/examples/open-file" --card "$out/card" --open "/A first file.txt" --ms 2000
    expect 'note "Read A first file.txt"'
    same_as open-file-fat32-short picture
    run open-file-folder-long "$kit/examples/open-file" --card "$out/card" --open "/NOTES/Long story.txt" --ms 3000 \
        --script "1500:joy=down 1600:joy= 1700:joy=down+fire 1800:joy="
    expect 'note "Read Long story.txt"'
    same_as open-file-fat32-long picture
    # a file on this computer: its folder is the card
    run open-file-path "$kit/examples/open-file" --open "$out/card/NOTES/Long story.txt" --ms 2000
    expect 'note "Read Long story.txt"'
    # test/run.sh's own disk image, if it made one (macOS): an image as the card
    if [ -f "$host/card-fat32.cdr" ]; then
        cp "$host/card-fat32.cdr" "$out/card.img"
        run open-file-image "$kit/examples/open-file" --card "$out/card.img" --open "/A first file.txt" --ms 2000
        expect 'note "Read A first file.txt"'
        same_as open-file-fat32-short picture
    fi
fi
if wanted frames; then
    run frames "$kit/examples/frames" --ms 5000 --script "1000:snap=early 2000:joy=left 2100:joy= 3000:joy=up 3100:joy=" --wav "$out/frames/frames.wav"
    expect 'psg 8E 0F 92' 'psg 9F'
    # the beep at each bounce (440 Hz for four frames), where the log has its PSG bytes
    "$py" -I "$kit/sim/wavcheck.py" "$o/frames.wav" 0-1200:0 1222-1280:440 1300-2000:0 2023-2080:440 3141-3198:440 \
        3908-3965:440 4000-4990:0 || fail "the beeps aren't where the frame lists put them"
fi
if wanted psram; then
    run psram "$kit/examples/psram" --ms 60000
    expect 'note "PSRAM example: 8 MB right"'
    run psram-missing "$kit/examples/psram" --no-psram --ms 1000
    expect 'note "PSRAM example: no PSRAM"'
fi
if wanted vdp; then
    for k in 1 2 3 4 5 6; do
        run "vdp-$k" "$kit/test/vdp" --ms 1500 --script "100:key=$k"
        expect 'note "VDP test"'
    done
fi

[ $compared = 0 ] && echo "(no test/host pictures to compare with: test/run.sh hasn't run here)"
[ $status = 0 ] && echo "all simulator checks passed (pictures: $out/*/screen.png)"
exit $status
