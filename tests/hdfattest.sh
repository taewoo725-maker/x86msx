#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 8.0.110 hdfattest: mkfs.fat 으로 FAT16 이미지(파티션 있음/없음) + FAT12 플로피·FAT32 도 읽음
set -e
T=/tmp/hdfat_test; rm -rf $T; mkdir -p $T/src/SCREEN; cd $T
python3 - <<'PY'
n=3000000
open('src/BIG.BIN','wb').write(bytes(((i*7+(i>>8))&255) for i in range(n)))
open('src/SCREEN/SHORT.MP4','wb').write(b'x'*1234)
open('src/SCREEN/14035-254146872_medium.mp4','wb').write(b'y'*5000)
PY
mk() { # $1 이미지 $2 mkfs 옵션
  dd if=/dev/zero of=$1 bs=1M count=$3 2>/dev/null; mkfs.fat $2 $1 >/dev/null
  mcopy -i $1 src/BIG.BIN ::BIG.BIN; mmd -i $1 ::SCREEN; mcopy -i $1 src/SCREEN/SHORT.MP4 ::SCREEN/SHORT.MP4; mcopy -i $1 src/SCREEN/14035-254146872_medium.mp4 ::SCREEN/
}
mk f16.img "-F 16" 16
# 파티션 표가 있는 이미지: 1MiB 앞에 MBR, 파티션 1 = f16.img
python3 - <<'PY'
import struct
v=open('f16.img','rb').read()
start=2048; ns=len(v)//512
mbr=bytearray(512); p=0x1BE
mbr[p+4]=0x06; mbr[p+8:p+12]=struct.pack('<I',start); mbr[p+12:p+16]=struct.pack('<I',ns); mbr[510]=0x55; mbr[511]=0xAA
open('p16.img','wb').write(bytes(mbr)+b'\0'*(start*512-512)+v)
PY
dd if=/dev/zero of=f12.img bs=1k count=1440 2>/dev/null; mkfs.fat -F 12 f12.img >/dev/null
cp f12.img g12.img; python3 - <<'PY2'
open("src/BIG.BIN","wb").write(bytes(((i*7+(i>>8))&255) for i in range(300000)))
PY2
mcopy -i g12.img src/BIG.BIN ::BIG.BIN; mmd -i g12.img ::SCREEN; mcopy -i g12.img src/SCREEN/SHORT.MP4 ::SCREEN/SHORT.MP4; mcopy -i g12.img src/SCREEN/14035-254146872_medium.mp4 ::SCREEN/

mk f32.img "-F 32" 40
cd - >/dev/null
./build/hdfattest $T/f16.img -1 16 | tail -1
./build/hdfattest $T/p16.img 0 | tail -1
./build/hdfattest $T/g12.img -1 12 | tail -1
./build/hdfattest $T/f32.img -1 32 | tail -1
