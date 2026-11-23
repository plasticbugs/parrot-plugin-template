#!/bin/sh
# tools/mame-controllers.sh OUT_DIR [plain]
#
# The frame kernel's controller reports on MAME's ColecoVision: a Lua
# script (tools/mame/controllers.lua) presses each direction, both fire
# buttons and every keypad key of controller 2, then of controller 1, and
# reads the kernel's reports as the cart's bus loop does. It prints
# "controllers: ok" when every one came out as cvf_input2() and cvf_input()
# give it.
#
# The kernel is gbkernel.asm built with CONTROLLER2, or as plugins without
# it have it with "plain" (then controller 1 only, and nothing may be
# reported where controller 2's go).
#
# Needs mame, sjasmplus and the ColecoVision BIOS (MAME_ROMPATH=dir, holding
# coleco.zip). MAME runs headless: no window, no sound, a time limit.
set -e
kit=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?usage: tools/mame-controllers.sh OUT_DIR [plain]}
mkdir -p "$out/mame"
out=$(cd "$out" && pwd)
if [ "$2" = plain ]; then define= ctrl2=0; else define=-DCONTROLLER2 ctrl2=1; fi
(cd "$out/mame" && sjasmplus --nologo $define "$kit/sdk/gbcv/kernel/gbkernel.asm" >/dev/null 2>&1)
PC_CTRL2=$ctrl2 SDL_VIDEODRIVER=dummy mame coleco ${MAME_ROMPATH:+-rompath "$MAME_ROMPATH"} -cart "$out/mame/gbkernel.bin" \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -skip_gameinfo -seconds_to_run 60 \
    -cfg_directory "$out/mame/cfg" -nvram_directory "$out/mame/nvram" \
    -autoboot_script "$kit/tools/mame/controllers.lua" | grep '^controllers:' || true
