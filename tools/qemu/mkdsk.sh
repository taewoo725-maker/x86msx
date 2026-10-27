#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# MSX용 720 KB 시험 디스크: FAT12, 부트 코드 자리(0x1E)에 RET
rm -f "$1"; mkfs.fat -C "$1" 720 -F 12 -f 2 -r 112 -s 2 -S 512 -g 2/9 -M 0xF9 -n MSXDISK >/dev/null
printf '\311' | dd of="$1" bs=1 seek=30 conv=notrunc status=none
printf '10 PRINT "HELLO FROM DISK"\r\n' > /tmp/HELLO.BAS
MTOOLS_SKIP_CHECK=1 mcopy -i "$1" /tmp/HELLO.BAS ::/HELLO.BAS
