#!/bin/sh
# tools/mkcard.sh FOLDER IMAGE [fat16|fat32|exfat] [mbr|gpt] [SIZE_MB]
#
# A card image for the models (PC_CARD=IMAGE test/run.sh ...; docs/testing.md):
# FOLDER's files on a FAT16, FAT32 (the default) or exFAT volume, in an MBR
# (the default) or GPT partition table, SIZE_MB big (default 64). IMAGE is
# written as given (no .cdr added).
#
#   macOS   hdiutil, every file system. The volume is always named PARROTCART:
#           without -volname, hdiutil names it after FOLDER, and a folder
#           name longer than 11 characters (exFAT's and FAT's limit) makes
#           it fail with "Operation not permitted".
#   Linux   mkfs.fat and mcopy (the dosfstools and mtools packages), FAT16
#           and FAT32 in an MBR only. exFAT needs macOS; the simulator reads
#           a folder as the card on any system (sim/run.sh --card FOLDER).
set -e
src=${1:?usage: tools/mkcard.sh FOLDER IMAGE [fat16|fat32|exfat] [mbr|gpt] [SIZE_MB]}
img=${2:?usage: tools/mkcard.sh FOLDER IMAGE [fat16|fat32|exfat] [mbr|gpt] [SIZE_MB]}
fs=${3:-fat32}
layout=${4:-mbr}
mb=${5:-64}
[ -d "$src" ] || { echo "mkcard.sh: $src isn't a folder" >&2; exit 2; }
case $fs in fat16|fat32|exfat) ;; *) echo "mkcard.sh: fat16, fat32 or exfat, not $fs" >&2; exit 2 ;; esac
case $layout in mbr|gpt) ;; *) echo "mkcard.sh: mbr or gpt, not $layout" >&2; exit 2 ;; esac
rm -f "$img"

if command -v hdiutil >/dev/null 2>&1; then
    case $fs in fat16) hfs="MS-DOS FAT16" ;; fat32) hfs="MS-DOS FAT32" ;; exfat) hfs=ExFAT ;; esac
    case $layout in mbr) hl=MBRSPUD ;; gpt) hl=GPTSPUD ;; esac
    tmp=$img.mkcard
    rm -f "$tmp.cdr"
    hdiutil create -quiet -srcfolder "$src" -volname PARROTCART -fs "$hfs" -layout "$hl" \
        -size "${mb}m" -format UDTO -o "$tmp"
    mv "$tmp.cdr" "$img"
    exit 0
fi

# Linux (or any system with dosfstools and mtools)
[ "$fs" != exfat ] || { echo "mkcard.sh: an exFAT image needs macOS's hdiutil. Use fat32, or the simulator's --card FOLDER" >&2; exit 2; }
[ "$layout" = mbr ] || { echo "mkcard.sh: a GPT image needs macOS's hdiutil. Use mbr" >&2; exit 2; }
for t in mkfs.fat mcopy python3; do
    command -v $t >/dev/null 2>&1 || { echo "mkcard.sh: needs $t (dosfstools, mtools, python3), or macOS's hdiutil" >&2; exit 2; }
done
start=2048                              # the partition's first sector, as a card's often is
sectors=$((mb * 2048))
dd if=/dev/zero of="$img" bs=1048576 count=0 seek="$mb" 2>/dev/null
# the MBR: one partition from $start to the end (0x0E FAT16 LBA, 0x0C FAT32 LBA)
python3 -I -c '
import struct, sys
img, start, total, kind = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4], 16)
mbr = bytearray(512)
mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", kind, b"\xfe\xff\xff", start, total - start)
mbr[510:512] = b"\x55\xaa"
with open(img, "r+b") as f:
    f.write(mbr)
' "$img" "$start" "$sectors" "$([ "$fs" = fat16 ] && echo 0E || echo 0C)"
mkfs.fat -F "${fs#fat}" --offset="$start" -n PARROTCART "$img" $(((sectors - start) / 2)) >/dev/null
# (mtools: the partition at its byte offset; the folder's contents to the root)
export MTOOLS_SKIP_CHECK=1
if [ -n "$(ls -A "$src")" ]; then
    mcopy -s -Q -i "$img@@$((start * 512))" "$src"/* ::/
fi
