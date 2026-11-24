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
# sd-card open-file frames samples two-controllers psram lua vdp usb font
# timers leave. Pictures and logs: build-sim/test/.
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

# run NAME DIR [simulator options] -- the plugin in DIR, headless; o= its
# output, $out/NAME. A run clears that folder only if a run made it (it holds
# .sim-test, or run.txt from an older sim/test.sh), so a card folder with the
# test's name is left alone and the test fails.
run() {
    name=$1; dir=$2; shift 2
    o=$out/$name
    echo "$name"
    skip=
    if [ -e "$o" ] && [ ! -f "$o/.sim-test" ] && [ ! -f "$o/run.txt" ] && [ -n "$(ls -A "$o" 2>/dev/null)" ]; then
        fail "$o isn't a test's output, so it stays: give the test or that folder another name"
        skip=1; return 0
    fi
    rm -rf "$o"; mkdir -p "$o"; : > "$o/.sim-test"
    [ -d "$dir" ] || { echo "  (not here: $dir)"; skip=1; return 0; }
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

# midi_log FILE LINE... -- what --midi-log wrote: these lines, no others
midi_log() {
    [ -z "$skip" ] || return 0
    f=$1; shift
    printf '%s\n' "$@" > "$f.want"
    cmp -s "$f" "$f.want" || { fail "$f isn't what the plugin should have sent:"; diff "$f.want" "$f" | head -8 | sed 's/^/    /'; }
}

# same_as NAME [picture] -- the last run's picture and log against test/run.sh's
# NAME, if it has run (the picture only: a card folder isn't macOS's image, so
# its sector reads, and the log's times, differ)
same_as() {
    [ -z "$skip" ] && [ -f "$host/$1/screen.png" ] || return 0
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
    [ -z "$skip" ] || return 0
    for want in "$@"; do
        grep -Eq -- "$want" "$o/log.txt" || fail "nothing in $o/log.txt matches: $want"
    done
}

# save_check FILE MAGIC RUNS -- a save file a plugin wrote in a card folder:
# 512 bytes, starting with MAGIC, a run count at byte 8 (as test/run.sh's
# card_check reads it on a card image)
save_check() {
    got=$("$py" -I -c 'import struct,sys; d=open(sys.argv[1],"rb").read(); print(len(d), d[:len(sys.argv[2])].decode(), struct.unpack_from("<I", d, 8)[0])' "$1" "$2" 2>&1) || true
    if [ "$got" = "512 $2 $3" ]; then echo "  the folder: $(basename "$1") is 512 bytes, run $3"
    else fail "$1: $got (wanted run $3)"; fi
}

# a card, as a folder: test/run.sh's tree
tree() {
    rm -rf "$1"
    mkdir -p "$1/NOTES" "$1/PLUGINS"
    printf 'Hello from the card.\nThis is the first file.\n' > "$1/A first file.txt"
    i=1; : > "$1/NOTES/Long story.txt"
    while [ $i -le 60 ]; do echo "Line $i of a story long enough to scroll, and to wrap around the screen's 30 columns." >> "$1/NOTES/Long story.txt"; i=$((i + 1)); done
}

# ---- the plugin in src/ (tools/new-plugin.py rewrites this part) ----
if wanted hello; then
    run hello "$kit/src" --ms 3000 --script "500:joy=up+fire 900:joy= 1200:key=5 1500:key= 2000:joy=fire2 2300:joy="
    expect 'note "Hello: 3 presses"'
fi
# ---- the examples ----
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
    save_check "$out/card/SDCARD.SAV" PCSAVE1 3
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
    expect 'psg 8E 0F 92 A0 00 BF C0 00 DF FF' 'psg 8E 0F 9F A0 00 BF C0 00 DF FF'
    # the beep at each bounce (440 Hz for four frames), where the log has its PSG bytes
    "$py" -I "$kit/sim/wavcheck.py" "$o/frames.wav" 0-1200:0 1222-1280:440 1300-2000:0 2023-2080:440 3141-3198:440 \
        3908-3965:440 4000-4990:0 || fail "the beeps aren't where the frame lists put them"
fi
if wanted samples; then
    run samples "$kit/examples/samples" --ms 3000 --script "300:key=6 500:key= 1500:joy=fire 1600:joy=" --wav "$out/samples/samples.wav"
    expect 'psg 90 BB D8 81 00 A1 00 C1 00 FF' 'lists back to back, .* 0 run again'
    # the note: 440 Hz as samples (442 at MAME's costs), then the drum
    "$py" -I "$kit/sim/wavcheck.py" "$o/samples.wav" 0-280:0 350-1400:442 || fail "the note isn't in tune in the WAV"
fi
if wanted two-controllers; then
    run two-controllers "$kit/examples/two-controllers" --ms 3000 --script "$(cat "$kit/examples/two-controllers/test.script")"
    expect 'note "C1 JE F2 K3, C2 J3 F1 K6"' 'note "C1 JF F3 KF, C2 JF F3 KF"'
    # two game pads on the desktop: the first is controller 1 too, the second controller 2
    run two-controllers-pads "$kit/examples/two-controllers" --ms 1500 --script "500:simpad=switch 600:simpad=hid 800:simbuttons=2:down+east+plus 900:simbuttons=1:up+south"
    expect 'note "C1 JF F3 KF, C2 JB F1 K6"' 'note "C1 JE F2 KF, C2 JB F1 K6"'
fi
if wanted psram; then
    run psram "$kit/examples/psram" --ms 60000
    expect 'note "PSRAM example: 8 MB right"'
    run psram-missing "$kit/examples/psram" --no-psram --ms 1000
    expect 'note "PSRAM example: no PSRAM"'
fi
# ---- the Lua plugin (lua/, docs/lua.md): a folder as the card, with scripts in its /LUA ----
if wanted lua; then
    lt=$kit/lua/test/scripts
    rm -rf "$out/luacard"; mkdir -p "$out/luacard/LUA/SUB"
    cp "$kit"/lua/examples/*.lua "$lt/files.lua" "$lt/error.lua" "$out/luacard/LUA/"
    printf 'return {answer = 42}\n' > "$out/luacard/LUA/lib.lua"
    printf 'one\ntwo\nthree\n' > "$out/luacard/LUA/notes.txt"
    printf 'deep\n' > "$out/luacard/LUA/SUB/deep.txt"
    export LUA_SEED=1
    # started by itself: the editor's list of /LUA (colours, error, files, hello, lib, snake,
    # sprites). Down six times to sprites.lua, fire opens it, keypad 4 runs it, and Esc on the
    # USB keyboard stops it
    run luasim-editor "$kit/lua" --card "$out/luacard" --usb host --ms 8000 --script "1500:snap=list 1600:joy=down 1700:joy= 1800:joy=down 1900:joy= 2000:joy=down 2100:joy= 2200:joy=down 2300:joy= 2400:joy=down 2500:joy= 2600:joy=down 2700:joy= 2900:joy=fire 3000:joy= 3600:snap=editor 3700:key=4 3800:key= 5500:snap=sprites 5600:simrole=keyboard 5620:simkey=29 5640:simkeyup=29 7000:snap=stopped"
    expect 'sound [0-9]+Hz@7 - - noise@15'
    if [ -z "$skip" ]; then
        cmp -s "$o/sprites.png" "$o/editor.png" && fail "keypad 4 didn't run the script"
        cmp -s "$o/sprites.png" "$o/stopped.png" && fail "Esc didn't stop the script"
    fi
    # opened as from the menu and run with keypad 4, three times: the save counts the runs
    for n in 1 2 3; do
        run "luasim-files-$n" "$kit/lua" --card "$out/luacard" --open /LUA/files.lua --ms 4000 --script "1500:key=4 1600:key="
        expect "note \"files run $n\""
    done
    # a script with a mistake: back in the editor on its line. A line typed at the top, Ctrl+S
    run luasim-error "$kit/lua" --card "$out/luacard" --open /LUA/error.lua --usb host --ms 6000 --script "1500:key=4 1600:key= 3500:snap=mistake 3600:simrole=keyboard 3620:simkey=E0 3640:simkey=4A 3660:simkeyup=4A 3680:simkeyup=E0 3700:simkey=1B 3720:simkeyup=1B 3740:simkey=28 3760:simkeyup=28 3800:simkey=E0 3820:simkey=16 3840:simkeyup=16 3860:simkeyup=E0"
    expect 'note "error.lua:2: attempt to index a "'
    if [ -z "$skip" ]; then
        got=$("$py" -I "$kit/lua/test/cardcheck.py" "$out/luacard/LUA" "$lt/error.lua" 2>&1) || true
        if [ "$got" = ok ]; then echo "  the folder: the files are as the scripts and the editor wrote them"; else fail "the card folder: $got"; fi
    fi
    # a script run at once, as sim/lua.sh --run does
    LUA_RUN=1 run luasim-snake "$kit/lua" --open "$kit/lua/examples/snake.lua" --ms 9000 --script "1000:joy=fire 1100:joy= 1500:joy=down 1600:joy= 2500:joy=left 2600:joy="
    expect 'note "Snake: [0-9]+ points"'
    unset LUA_SEED LUA_RUN
fi
if wanted vdp; then
    for k in 1 2 3 4 5 6; do
        run "vdp-$k" "$kit/test/vdp" --ms 1500 --script "100:key=$k"
        expect 'note "VDP test"'
    done
fi
# the SDK's USB features (test/usb/main.c writes each input to its note).
# First the models' own script words, as test/run.sh; then the desktop's
# devices, made up (the script's sim* words: sim/input.c), through all of
# what reads a real keyboard, mouse and game pad.
if wanted usb; then
    run usb-host "$kit/test/usb" --ms 3000 --script "$(cat "$kit/test/usb/host.script")" --midi-log "$out/usb-host.midi"
    expect 'note "key 04 dn 41 m02"' 'note "mouse -300,5 w-2 b0"' 'note "pad2 HID PAD 00000020"' 'note "joy 11 key 5"'
    # (the script's USB and MIDI words land at their own millisecond, not on the next TV frame)
    midi_log "$out/usb-host.midi" '  2500 90 3C 64 (to the MIDI device)' '  2600 F8 (to the MIDI device)'
    run usb-device "$kit/test/usb" --ms 2000 --script "$(cat "$kit/test/usb/device.script")" --midi-log "$out/usb-device.midi"
    expect 'note "usb ON A COMPUTER"' ' midi C0 05'
    grep -q 'to the MIDI device' "$o/log.txt" && fail "MIDI went to a MIDI device, not to the computer"
    midi_log "$out/usb-device.midi" '   500 90 3C 64' '   520 80 3C 00' '   700 C0 05'
    # --usb: as the plugin asked (a computer listening: MIDI_OUT), a hub, a computer that asks
    run usb-flag-host "$kit/test/usb" --usb host --ms 2000
    expect '^  1501 usb: the port is a host'
    run usb-flag-ask "$kit/test/usb" --usb ask --ms 2000
    expect 'note "usb ALLOW IT ON THE COMPUTER"'
    # the keyboard: through cvusb_keys.c, as a USB keyboard's reports; only in its F2 role
    run usb-keyboard "$kit/test/usb" --usb host --ms 2500 --script "1600:simkey=05 1650:simkeyup=05 1700:simrole=keyboard 1720:simkey=E1 1740:simkey=04 1760:simkeyup=04 1780:simkeyup=E1 1800:simkey=1E 1820:simkeyup=1E 1840:simkey=28 1860:simkey=E0 1880:simrole=controller"
    expect 'note "key E1 dn 00 m02"' 'note "key 04 dn 41 m02"' 'note "key 04 up 41 m02"' 'note "key E1 up 00 m00"' 'note "key 1E dn 31 m00"' \
        'note "key 28 dn 0D m00"' 'note "key E0 dn 00 m01"' 'note "key 28 up 0D m01"' 'note "key E0 up 00 m00"'
    grep -q 'note "key 05' "$o/log.txt" && fail "a key reached the USB keyboard in the controller's role"
    # the mouse: moves, buttons, the wheel; its buttons let go when F2 takes the keyboard away
    run usb-mouse "$kit/test/usb" --usb host --ms 2500 --script "1600:simmouse=5,5,1 1650:simmouse=0,0,0 1700:simrole=keyboard 1720:simmouse=40,-20,1,1 1760:simmouse=-300,0,3,-2 1800:simmouse=0,0,2 1900:simrole=pad"
    expect 'note "mouse 40,-20 w1 b1"' 'note "mouse -300,0 w-2 b3"' 'note "mouse 0,0 w0 b2"' 'note "mouse 0,0 w0 b0"'
    grep -Eq '^  1(6[0-9][0-9]|70[0-9]) note "mouse' "$o/log.txt" && fail "the mouse reached the USB mouse outside its F2 role"
    # two game pads: their slots, kinds, buttons and sticks; pad 1 the console's controller too
    run usb-pads "$kit/test/usb" --usb host --ms 3000 --script "1600:simpad=switch 1650:simbuttons=1:south+l+minus 1700:simsticks=1:16000,-30000,0,0 1750:simpad=hid 1800:simbuttons=2:east+zr 1850:simsticks=2:0,0,-32768,32767 1900:simunplug=1 1950:simrole=pad 2000:simrole=controller 2050:simpad=switch 2100:simbuttons=3:north 2150:simbuttons=2:"
    expect 'note "pad1 SWITCH PRO 00001110"' 'note "pad1 at 1000,1875 0,0"' 'note "joy 10 key 10"' 'note "joy 11 key 10"' \
        'note "pad2 HID PAD 00000820"' 'note "pad2 at 0,0 -2048,-2047"' 'note "pad1 OFF' 'note "pad1 SWITCH PRO 00000080"' 'note "pad2 HID PAD 00000000"'
    expect '^  1935 note "joy 20 key -1"' '^  1952 note "pad1 SWITCH PRO 00000000"' '^  2002 note "pad1 OFF' '^  2052 note "pad1 SWITCH PRO 00000000"'
    grep -q 'note "pad2 OFF' "$o/log.txt" && fail "pad 2 lost its slot when pad 1 was unplugged"
fi
# the SDK's font and text editor (cvfont.c, cvedit.c), linked when a
# plugin calls them, as test/run.sh's font
if wanted font; then
    run font "$kit/test/font" --ms 1500 --script "$(cat "$kit/test/font/test.script")"
    expect 'note "font: Hi\|there"'
fi
# the pico-sdk's alarms and repeating timers, as test/run.sh's timers
if wanted timers; then
    run timers "$kit/test/timers" --ms 1000
    expect 'note "timers 4[89][0-9] 9[0-9] 1 0 50"'
fi
# the screen kernel's exit function, as test/run.sh's leave
if wanted leave; then
    run leave "$kit/test/leave" --ms 1500 --script "500:key=1 700:key="
    expect "cvk_on_exit's function: 50[0-9] ms of its 3000" 'bottom line "left after 500 ms"'
fi

[ $compared = 0 ] && echo "(no test/host pictures to compare with: test/run.sh hasn't run here)"
[ $status = 0 ] && echo "all simulator checks passed (pictures: $out/*/screen.png)"
exit $status
