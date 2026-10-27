#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 3.4.5: FAT32 쓰기 반복 시험 + fsck.fat (가득 찬 경우 포함)
set -e
rm -f build/fs_big.img build/fs_small.img
mkfs.fat -F 32 -n X86MSX -C build/fs_big.img 524288 >/dev/null
./build/fatstress build/fs_big.img 1500 | tail -1
fsck.fat -n build/fs_big.img 2>&1 | grep -q "Truncating\|Reclaimed\|bad\|Bad" && { echo "FAIL fsck big"; exit 1; } || echo "fatstress: fsck clean (512 MB)"
mkfs.fat -F 32 -n X86MSX -C build/fs_small.img 66000 >/dev/null
./build/fatstress build/fs_small.img 800 >/dev/null || true
fsck.fat -n build/fs_small.img 2>&1 | grep -q "Truncating\|Reclaimed\|bad\|Bad" && { echo "FAIL fsck full disk"; exit 1; } || echo "fatstress: fsck clean after disk full (64 MB)"
rm -f build/fs_big.img build/fs_small.img
