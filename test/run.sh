#!/bin/sh
# test/run.sh -- everything about the kit that a Mac can check.
#
#   test/run.sh [example ...]      default: the template's plugin and every example
#
# For each plugin:
#   1. its own main.c is built for the Mac and run against models of the
#      console, the card, the pads and the PSRAM (test/host/host.c), with a
#      script of inputs; what it showed, played, sent and wrote is checked
#   2. the lists it sent the console are played to the SDK's real Z80
#      kernel in headless MAME (tools/mame-replay.sh), and MAME's picture
#      must be the model's, pixel for pixel
# and for the card examples, on FAT16, FAT32 and exFAT images made by
# tools/mkcard.sh (hdiutil on a Mac), macOS's own fsck and a mount afterwards.
#
# Output in $TMPDIR/parrotcart-test (pictures: */screen.png).
# NO_MAME=1 skips step 2. Step 2 needs mame, sjasmplus and the ColecoVision
# BIOS, coleco.zip (MAME's name for it), which the kit doesn't have. Put
# coleco.zip in a folder of your own and name that folder in MAME_ROMPATH:
#   MAME_ROMPATH=$HOME/mame/roms test/run.sh
# or put it where MAME already looks: the rompath in its mame.ini, or with
# no mame.ini, a roms folder in the folder you run this from. Without it,
# step 2 is skipped and the script says why.
# What this can't check is the cart itself: USB, the real card, the PSRAM
# chip, timing on the bus. docs/testing.md has the console test plan.
set -e
kit=$(cd "$(dirname "$0")/.." && pwd)
out=${TMPDIR:-/tmp}/parrotcart-test
mkdir -p "$out"
status=0
have_mame=1
[ -n "$NO_MAME" ] && have_mame=0
command -v mame >/dev/null 2>&1 && command -v sjasmplus >/dev/null 2>&1 || have_mame=0
[ $have_mame = 1 ] || echo "(no MAME step: NO_MAME is set, or mame or sjasmplus isn't installed)"
# is the BIOS where MAME looks? (asked headless, as tools/mame-replay.sh runs MAME)
if [ $have_mame = 1 ] && ! SDL_VIDEODRIVER=dummy mame -verifyroms coleco ${MAME_ROMPATH:+-rompath "$MAME_ROMPATH"} \
        -video none -videodriver dummy -window -nomaximize -sound none \
        -nothrottle -skip_gameinfo -seconds_to_run 10 >/dev/null 2>&1; then
    have_mame=0
    echo "(no MAME step: MAME can't find the ColecoVision BIOS, coleco.zip, which isn't in the kit."
    echo " Put it in a folder and run MAME_ROMPATH=/path/to/that/folder test/run.sh, or put it in"
    echo " MAME's own rompath. docs/testing.md has more.)"
fi

# run NAME DIR -- with PC_* in the environment; then the log must match each
# remaining argument (a grep -E pattern). (sh keeps a function's PC_*=...
# after it returns: they are cleared, so one run's script isn't the next's.)
# A frame plugin whose lists run back to back says so in its CMakeLists.txt
# (parrotcart_frames(TARGET BACK_TO_BACK)), or PC_BACK_TO_BACK=1 here: the
# model and the MAME step then check that the console goes straight from
# one list into the next, instead of failing lists longer than a frame.
run() {
    run_one "$@"
    unset PC_SCRIPT PC_END PC_CARD PC_OPEN PC_ID PC_NO_PSRAM PC_NO_LAUNCH PC_BACK_TO_BACK
}
run_one() {
    name=$1; dir=$2; shift 2
    o=$out/$name
    rm -rf "$o"; mkdir -p "$o"
    printf '%s\n' "$name"
    [ -d "$dir" ] || { echo "  (not here: $dir)"; return 0; }
    if ! "$kit/test/host/build.sh" "$dir" "$o/run" 2>"$o/build.log"; then
        echo "  the Mac build FAILED:"; sed 's/^/    /' "$o/build.log" | head -20; status=1
        unset PC_SCRIPT PC_OPEN PC_CARD PC_NO_PSRAM PC_END PC_BACK_TO_BACK; return 0
    fi
    if grep -v '^ld: warning' "$o/build.log" | grep -q 'warning:'; then echo "  FAIL: warnings:"; grep 'warning:' "$o/build.log" | sed 's/^/    /' | head; status=1; fi
    PC_OUT=$o "$o/run" || status=1
    # (sh keeps a function's VAR=value prefixes set after it: not into the next run)
    unset PC_SCRIPT PC_OPEN PC_CARD PC_NO_PSRAM PC_END PC_BACK_TO_BACK
    for want in "$@"; do
        grep -Eq -- "$want" "$o/log.txt" || { echo "  FAIL: nothing in $o/log.txt matches: $want"; status=1; }
    done
    python3 -I "$kit/tools/png256.py" check "$o/screen.png" >/dev/null || { echo "  FAIL: screen.png isn't a 256x192 screenshot"; status=1; }
    if [ $have_mame = 1 ]; then
        if "$kit/tools/mame-replay.sh" "$o/lists.bin" "$o" 120 >"$o/mame.log" 2>&1 &&
           python3 -I "$kit/tools/png256.py" same "$o/screen.png" "$o/mame-256x192.png" >>"$o/mame.log" 2>&1; then
            echo "  MAME: the real kernel shows the same picture ($(grep -o '[0-9]* of [0-9]* lists run' "$o/mame.log"))"
            if grep -q 'longer than their TV frame' "$o/mame.log" && ! grep -q 'replay: 0 lists took longer' "$o/mame.log"; then
                echo "  FAIL in MAME: $(grep 'longer than' "$o/mame.log")"; status=1
            fi
            if grep -q 'lists back to back' "$o/mame.log"; then
                if grep -q ' 0 of them followed by a wait' "$o/mame.log"; then
                    echo "  MAME: $(grep -o '[0-9]* lists back to back' "$o/mame.log"), none followed by a wait"
                else echo "  FAIL in MAME: $(grep 'back to back' "$o/mame.log" | sed 's/^replay: //')"; status=1; fi
            fi
        else
            echo "  FAIL in MAME:"; tail -3 "$o/mame.log" | sed 's/^/    /'; status=1
        fi
    fi
}

wanted() { [ -z "$ONLY" ] && return 0; case " $ONLY " in *" $1 "*) return 0 ;; esac; return 1; }
ONLY="$*"

# The card helpers leave the caller's variables alone: each runs in a
# subshell, ( ... ), so the names it uses are its own, and a check that
# fails sets status=1 through its exit code.

# a card image: FILESYSTEM:LAYOUT:NAME -> $out/card-NAME.cdr, from a small
# tree, with more folders if asked (card SPEC /FOLDER ...). FILESYSTEM and
# LAYOUT are hdiutil's names; tools/mkcard.sh makes it (a plugin's own
# cards too: it gives the volume a name, which hdiutil needs when the
# folder's name is longer than 11 characters)
card() (
    fsname=${1%%:*}; rest=${1#*:}; layout=${rest%%:*}; cname=${rest#*:}
    shift
    tree=$out/tree
    rm -rf "$tree" "$out/card-$cname.cdr"
    mkdir -p "$tree/NOTES" "$tree/PLUGINS"
    for d in "$@"; do mkdir -p "$tree$d"; done
    printf 'Hello from the card.\nThis is the first file.\n' > "$tree/A first file.txt"
    i=1; : > "$tree/NOTES/Long story.txt"
    while [ $i -le 60 ]; do echo "Line $i of a story long enough to scroll, and to wrap around the screen's 30 columns." >> "$tree/NOTES/Long story.txt"; i=$((i + 1)); done
    case $fsname in "MS-DOS FAT16") fs=fat16 ;; "MS-DOS FAT32") fs=fat32 ;; *) fs=exfat ;; esac
    case $layout in GPT*) lay=gpt ;; *) lay=mbr ;; esac
    "$kit/tools/mkcard.sh" "$tree" "$out/card-$cname.cdr" $fs $lay
)

# hdiutil detach, again with -force if the disk is busy (Spotlight, or
# another test's attach at the same moment)
detach() { hdiutil detach -quiet "$1" 2>/dev/null || { sleep 1; hdiutil detach -quiet -force "$1"; }; }

# macOS's opinion of an image the plugin wrote to: fsck (card_fsck NAME FS,
# FS as hdiutil names it: "MS-DOS FAT32", ExFAT)
card_fsck() { fsck_card "$@" || status=1; }
fsck_card() (
    cname=$1; fs=$2
    img=$out/card-$cname.cdr
    dev=$(hdiutil attach -nomount -readonly "$img" | awk '$1 ~ /s[0-9]+$/ {d = $1} END {print d}')
    whole=${dev%s*}
    fsck_ok=1
    case $fs in ExFAT) fsck_exfat -n "$dev" >"$out/fsck-$cname.log" 2>&1 || { echo "  FAIL: fsck_exfat"; fsck_ok=0; } ;;
                *) fsck_msdos -n "$dev" >"$out/fsck-$cname.log" 2>&1 || { echo "  FAIL: fsck_msdos"; fsck_ok=0; } ;; esac
    detach "$whole"
    [ $fsck_ok = 1 ] && [ $# = 2 ] && echo "  macOS: fsck clean"
    [ $fsck_ok = 1 ]
)

# card_fsck, then the save file's bytes: 512 of them, starting with MAGIC,
# a run count at byte 8 (card_check NAME FS RUNS [/PATH/OF.SAV MAGIC]:
# /SDCARD.SAV and PCSAVE1)
card_check() { check_card "$@" || status=1; }
check_card() (
    cname=$1; fs=$2; runs=$3; save=${4:-/SDCARD.SAV}; magic=${5:-PCSAVE1}
    ok=1
    fsck_card "$cname" "$fs" quiet || ok=0
    img=$out/card-$cname.cdr
    mnt=$out/mnt-$cname
    mkdir -p "$mnt"
    hdiutil attach -quiet -readonly -nobrowse -mountpoint "$mnt" "$img"
    got=$(python3 -I -c 'import struct,sys; d=open(sys.argv[1],"rb").read(); print(len(d), d[:len(sys.argv[2])].decode(), struct.unpack_from("<I", d, 8)[0])' "$mnt$save" "$magic" 2>&1) || true
    detach "$mnt"
    if [ "$got" = "512 $magic $runs" ]; then echo "  macOS: fsck clean, $save is 512 bytes, run $runs"
    else echo "  FAIL: $save on the Mac: $got (wanted run $runs)"; ok=0; fi
    [ $ok = 1 ]
)

# test/b2b, made wrong on purpose: the model must fail it with PATTERN
# (b2b_fails NAME LIST_T BLANK_WRITES PATTERN), and MAME see the gaps
b2b_fails() {
    o=$out/$1
    rm -rf "$o"; mkdir -p "$o"
    printf '%s\n' "$1"
    "$kit/test/host/build.sh" "$kit/test/b2b" "$o/run" 2>"$o/build.log" || { echo "  the Mac build FAILED"; status=1; return 0; }
    if PC_OUT=$o PC_END=500 PC_BACK_TO_BACK=1 PC_B2B_T=$2 PC_B2B_VB=$3 "$o/run" >"$o/out.txt"; then
        echo "  FAIL: the model passed it"; status=1
    elif grep -q "$4" "$o/out.txt"; then
        echo "  the model fails it, as it should: $(grep -o "$4" "$o/out.txt" | head -1)"
    else
        echo "  FAIL: the model failed it for something else:"; head -3 "$o/out.txt" | sed 's/^/    /'; status=1
    fi
    if [ $have_mame = 1 ] && [ "$3" = 0 ]; then
        "$kit/tools/mame-replay.sh" "$o/lists.bin" "$o" 60 >"$o/mame.log" 2>&1 || true
        if grep -q ' [1-9][0-9]* of them followed by a wait' "$o/mame.log"; then
            echo "  MAME: $(grep -o '[0-9]* of them followed by a wait' "$o/mame.log")"
        else echo "  FAIL in MAME: no wait seen"; tail -3 "$o/mame.log" | sed 's/^/    /'; status=1; fi
    fi
}

# ---- the plugin in src/ (tools/new-plugin.py rewrites this part) ----
if wanted hello; then
    PC_END=3000 PC_SCRIPT="500:joy=up+fire 900:joy= 1200:key=5 1500:key= 2000:joy=fire2 2300:joy=" \
        run hello "$kit/src" 'note "Hello: 3 presses"'
fi
# ---- the examples ----
if wanted screen; then
    PC_END=4000 PC_SCRIPT="1000:joy=fire 1200:joy= 2000:snap=moving" run screen "$kit/examples/screen" 'back to the menu'
fi
if wanted sound; then
    PC_END=3000 PC_SCRIPT="300:key=1 600:key= 800:joy=up 900:joy= 1000:key=6 1400:key= 1600:joy=fire 1700:joy=" \
        run sound "$kit/examples/sound" 'sound 26[123]Hz@2 - - noise@15' 'sound 88[0-9]Hz@2 - - noise@15' 'noise control E5 \(white noise, rate 1\)' 'sound - - - noise@0' 'sound - - - noise@14'
    # how loud, as tools/loudness.sh measures a run: its sound.bin through the
    # real kernel in MAME, or worked out without MAME (docs/testing.md, "How loud")
    if NO_MAME=$([ $have_mame = 1 ] || echo 1) "$kit/tools/loudness.sh" "$out/sound" >"$out/sound/loudness.txt" 2>&1; then
        how=$(grep -q 'plugin.wav: rms' "$out/sound/loudness.txt" && echo "MAME's recording" || echo "worked out")
        echo "  how loud: $(grep -o 'rms [-0-9.]*' "$out/sound/loudness.txt" | tail -1) dBFS ($how), as loud as a cartridge"
    else
        echo "  FAIL: tools/loudness.sh:"; sed 's/^/    /' "$out/sound/loudness.txt"; status=1
    fi
fi
if wanted usb-gamepad; then
    PC_END=1000 run usb-gamepad-none "$kit/examples/usb-gamepad" 'usb: the port is a host'
    PC_END=2500 PC_SCRIPT="500:pad=south+l+up 800:lstick=1500,-900 900:rstick=-2048,2047 1500:pad=east+plus" \
        run usb-gamepad "$kit/examples/usb-gamepad" 'usb: the port is a host'
fi
if wanted usb-keyboard-mouse; then
    # H E L L O, Enter, shift-1 would need mods: O K, a backspace, and the mouse down and right with its left button
    PC_END=2500 PC_SCRIPT="300:keydown=0B 320:keyup=0B 340:keydown=08 360:keyup=08 380:keydown=0F 400:keyup=0F 420:keydown=0F 440:keyup=0F 460:keydown=12 480:keyup=12 500:keydown=28 520:keyup=28 540:keydown=12 560:keydown=0E 580:keydown=1B 600:keydown=2A 1000:mouse=60,30,1" \
        run usb-keyboard-mouse "$kit/examples/usb-keyboard-mouse" 'usb: the port is a host'
fi
if wanted usb-midi-out; then
    PC_END=2500 PC_SCRIPT="300:key=1 600:key=3 900:key= 1200:key=8 1500:key=" \
        run usb-midi-out "$kit/examples/usb-midi-out" 'midi 90 3C 64' 'midi 80 3C 00' 'midi 90 40 64' 'midi 80 40 00' 'midi 90 48 64' 'midi 80 48 00'
    PC_END=1000 PC_SCRIPT="0:computer=0" run usb-midi-out-alone "$kit/examples/usb-midi-out" 'the port is a MIDI device'
fi
if wanted sd-card; then
    PC_END=1500 run sd-card-nocard "$kit/examples/sd-card" 'back to the menu'
    for spec in "MS-DOS FAT16:MBRSPUD:fat16" "MS-DOS FAT32:GPTSPUD:fat32" "ExFAT:MBRSPUD:exfat"; do
        cname=${spec##*:}
        card "$spec"
        for n in 1 2 3; do
            PC_CARD=$out/card-$cname.cdr PC_END=2500 run "sd-card-$cname-$n" "$kit/examples/sd-card" "note \"SD card example: run $n\""
        done
        card_check "$cname" "${spec%%:*}" 3
    done
fi
if wanted open-file; then
    PC_END=1000 run open-file-none "$kit/examples/open-file" 'back to the menu'
    for spec in "MS-DOS FAT32:MBRSPUD:fat32" "ExFAT:GPTSPUD:exfat"; do
        cname=${spec##*:}
        card "$spec"
        PC_CARD=$out/card-$cname.cdr PC_OPEN="/A first file.txt" PC_END=2000 \
            run "open-file-$cname-short" "$kit/examples/open-file" 'note "Read A first file.txt"'
        PC_CARD=$out/card-$cname.cdr PC_OPEN="/NOTES/Long story.txt" PC_END=3000 PC_SCRIPT="1500:joy=down 1600:joy= 1700:joy=down+fire 1800:joy=" \
            run "open-file-$cname-long" "$kit/examples/open-file" 'note "Read Long story.txt"'
    done
fi
if wanted frames; then
    PC_END=5000 PC_SCRIPT="1000:snap=early 2000:joy=left 2100:joy= 3000:joy=up 3100:joy=" \
        run frames "$kit/examples/frames" 'psg 8E 0F 92 A0 00 BF C0 00 DF FF' 'psg 8E 0F 9F A0 00 BF C0 00 DF FF'
fi
# back to the menu from the controllers (cvhome.h): both fire buttons and *
# held 2 s with either kernel, and a USB pad's Select and Start; held 1.5 s
# and let go, it doesn't
if wanted home; then
    PC_END=6000 PC_SCRIPT="1000:joy=fire+fire2 1000:key=*" \
        run home-screen "$kit/examples/screen" 'both fire buttons and \* held for 2 s' 'the controllers: back to the menu'
    PC_END=6000 PC_SCRIPT="1000:joy=fire+fire2 1000:key=*" \
        run home-frames "$kit/examples/frames" 'both fire buttons and \* held for 2 s' 'the controllers: back to the menu'
    PC_END=6000 PC_SCRIPT="500:pad=minus+plus" \
        run home-pad "$kit/examples/usb-gamepad" "a USB pad's Select and Start held for 2 s" 'the controllers: back to the menu'
    PC_END=5000 PC_SCRIPT="1000:joy=fire+fire2 1000:key=* 2500:key= 3000:key=* 4500:key=" \
        run home-short "$kit/examples/frames" '^ *5000 RESET: back to the menu'
fi
# controller 2 (CONTROLLER2, cvf_input2()) and USB pad 1's two sticks; then in
# MAME, every switch of both controllers through the frame kernel's reports,
# with CONTROLLER2 and without it (tools/mame-controllers.sh)
if wanted samples; then
    # keypad 6 (440 Hz) and fire (the drum) as samples, the lists back to back
    PC_END=3000 PC_SCRIPT="300:key=6 500:key= 1500:joy=fire 1600:joy=" \
        run samples "$kit/examples/samples" 'psg 90 BB D8 81 00 A1 00 C1 00 FF' 'lists back to back, .* 0 run again'
fi
if wanted back-to-back; then
    # the models' checks on lists that run back to back (test/b2b/main.c):
    # long enough passes; short of a frame, or with video writes in the
    # blank part, fails, and MAME sees the short one's gaps too
    PC_BACK_TO_BACK=1 PC_END=500 run b2b-long "$kit/test/b2b" '30 lists back to back'
    b2b_fails b2b-short 59200 0 'less than a frame: the console waits'
    b2b_fails b2b-blank 59700 1 'video RAM in its blank part'
fi
if wanted two-controllers; then
    PC_END=3000 PC_SCRIPT="$(cat "$kit/examples/two-controllers/test.script")" \
        run two-controllers "$kit/examples/two-controllers" 'note "C1 JE F2 K3, C2 J3 F1 K6"' 'note "C1 JF F3 KF, C2 JF F3 KF"'
    if [ $have_mame = 1 ]; then
        for k in controller2 plain; do
            r=$("$kit/tools/mame-controllers.sh" "$out/two-controllers/$k" $k 2>&1 | tail -2)
            case $r in *"controllers: ok"*) echo "  MAME, the kernel's reports ($k): $(echo "$r" | head -1 | sed 's/^controllers: //')" ;;
                *) echo "  FAIL in MAME, the kernel's reports ($k):"; echo "$r" | sed 's/^/    /'; status=1 ;; esac
        done
    fi
fi
if wanted psram; then
    PC_END=60000 run psram "$kit/examples/psram" 'note "PSRAM example: 8 MB right"'
    PC_NO_PSRAM=1 PC_END=1000 run psram-missing "$kit/examples/psram" 'note "PSRAM example: no PSRAM"'
fi
# the picture itself (test/host/tms9918.c): every VDP mode and the sprite
# rules, against MAME's own TMS9928A (test/vdp/main.c has the cases)
if wanted vdp; then
    for k in 1 2 3 4 5 6; do
        PC_END=1500 PC_SCRIPT="100:key=$k" run "vdp-$k" "$kit/test/vdp" 'note "VDP test"'
    done
fi
# the SDK's USB features, all on (test/usb/main.c writes each input to its
# note): a keyboard, a mouse and two pads on a hub, a MIDI keyboard's notes
# sent back to it; then as a MIDI device on a computer
if wanted usb; then
    PC_END=3000 PC_SCRIPT="$(cat "$kit/test/usb/host.script")" run usb-host "$kit/test/usb" \
        'note "usb HOST"' 'note "key E1 dn 00 m02"' 'note "key 04 dn 41 m02"' 'note "key 04 up 41 m02"' 'note "key 28 dn 0D m00"' \
        'note "mouse 40,20 w1 b1"' 'note "mouse -300,5 w-2 b0"' 'note "pad1 SWITCH PRO 00000110"' 'note "pad1 at 1500,-900 0,0"' \
        'note "pad2 HID PAD 00000020"' 'note "pad2 at 0,0 -2048,2047"' 'note "pad2 OFF' 'midi 90 3C 64 \(to the MIDI device\)' \
        'note "joy 11 key 5"'
    PC_END=2000 PC_SCRIPT="$(cat "$kit/test/usb/device.script")" run usb-device "$kit/test/usb" \
        'note "usb ON A COMPUTER"' ' midi 90 3C 64$' ' midi 80 3C 00$' ' midi C0 05$'
    grep -q 'note "midi F0' "$out/usb-device/log.txt" && { echo "  FAIL: a SysEx came in (the cart passes none in)"; status=1; }
fi
# the SDK's font and text editor (cvfont.c, cvedit.c), which the models
# link when a plugin calls them (test/font/main.c): every character, and an
# editor that a USB keyboard types "Hi", Enter, "there" into
if wanted font; then
    PC_END=1500 PC_SCRIPT="$(cat "$kit/test/font/test.script")" run font "$kit/test/font" 'note "font: Hi\|there"'
fi
# the pico-sdk's alarms and repeating timers on the models' clock
# (test/host/include/pico/time.h; test/timers/main.c says what its note holds)
if wanted timers; then
    PC_END=1000 run timers "$kit/test/timers" 'note "timers 4[89][0-9] 9[0-9] 1 0 50"'
    at=$(awk '$2 == "note" && $6 == 1 {print $1; exit}' "$out/timers/log.txt")
    if [ -n "$at" ] && [ "$at" -ge 500 ] && [ "$at" -le 520 ]; then echo "  the 500 ms alarm: seen at $at ms"
    else echo "  FAIL: the 500 ms alarm was first seen at ${at:-no time}"; status=1; fi
fi
# the screen kernel's exit function (cvk_on_exit; test/leave/main.c): half
# a second passes, and 5 seconds, past its CVK_EXIT_MS, fails, as the
# watchdog would cut it off. Then the console's side in MAME
# (test/leave/leave.lua): parked, it waits out 4 s of $00 for the menu,
# after RESET and after the controllers' way back
if wanted leave; then
    PC_END=1500 PC_SCRIPT="500:key=1 700:key=" run leave "$kit/test/leave" \
        "cvk_on_exit's function: 50[0-9] ms of its 3000" 'bottom line "left after 500 ms"'
    o=$out/leave-late
    rm -rf "$o"; mkdir -p "$o"
    echo leave-late
    if PC_OUT=$o PC_END=1500 PC_SCRIPT="500:key=2 700:key=" "$out/leave/run" >"$o/out.txt"; then
        echo "  FAIL: the model passed an exit function that runs 2 s past its time"; status=1
    elif grep -q "didn't return in its 3000 ms" "$o/out.txt" && grep -q 'with no note (the watchdog)' "$o/log.txt"; then
        echo "  the model fails it, as it should: the watchdog cuts it off at 3000 ms"
    else
        echo "  FAIL: the model failed it for something else:"; head -3 "$o/out.txt" | sed 's/^/    /'; status=1
    fi
    if [ $have_mame = 1 ]; then
        m=$out/leave-mame
        rm -rf "$m"; mkdir -p "$m"
        (cd "$m" && sjasmplus --nologo "$kit/sdk/firmware/revc/midihello/midihello.asm" >/dev/null 2>&1 &&
            sjasmplus --nologo "$kit/sdk/firmware/revc/midihello/handoff.asm" >/dev/null 2>&1)
        for how in reset combo; do
            LEAVE_KERNEL=$m/mhkernel.bin LEAVE_HANDOFF=$m/handoff.bin LEAVE_HOW=$how LEAVE_GAP=4 \
                SDL_VIDEODRIVER=dummy mame coleco ${MAME_ROMPATH:+-rompath "$MAME_ROMPATH"} -cart "$m/mhkernel.bin" \
                -video none -videodriver dummy -window -nomaximize -sound none \
                -nothrottle -skip_gameinfo -seconds_to_run 30 \
                -cfg_directory "$m/cfg" -nvram_directory "$m/nvram" \
                -autoboot_script "$kit/test/leave/leave.lua" 2>&1 | grep '^leave:' >"$m/$how.txt" || true
            if grep -q '| ok$' "$m/$how.txt"; then echo "  MAME, $how: the parked console waits out 4 s of \$00, then boots the menu"
            else echo "  FAIL in MAME ($how): $(cat "$m/$how.txt")"; status=1; fi
        done
    fi
fi
[ $status = 0 ] && echo "all checks passed (pictures: $out/*/screen.png)"
exit $status
