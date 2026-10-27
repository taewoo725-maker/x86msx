#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 3.0.2 디스크 이미지 만들기·파일 복사 시험: 만든 이미지를 mtools·fsck.fat으로 확인
set -e
fsck() { o=$(fsck.fat -n "$1" 2>&1 | grep -v -e '^fsck' -e 'Volume label' -e 'Auto-copying' -e '^ *$' -e '^Leaving' -e 'clusters$' || true); [ -z "$o" ] || { echo "fsck $1: $o"; return 1; }; }
B=build/mft; rm -rf $B build/dskt; mkdir -p $B build/dskt
export MTOOLS_SKIP_CHECK=1
./build/dskimgtest build/dskt > /dev/null
for f in build/dskt/fmt*.dsk; do mdir -i $f :: > /dev/null; fsck $f; done
for h in 0 1 2; do mdir -i build/dskt/hd$h.img@@512 :: > /dev/null; done
./build/msxfattest $B > $B/out.txt || { cat $B/out.txt; exit 1; }
for f in $B/w*.dsk; do
  fsck $f
  ./build/msxfattest read $f > $B/r.txt
  for n in SMALL.BIN BIG.BIN EMPTY; do
    s1=$(mtype -i $f ::$n | python3 -c "import sys;d=sys.stdin.buffer.read();h=0
for b in d: h=(h*31+b)&0xffffffff
print('%u %08x'%(len(d),h))")
    s2=$(grep "^$n " $B/r.txt | cut -d' ' -f3,4)
    [ "$s1" = "$s2" ] || { echo "mismatch $f $n: $s1 / $s2"; exit 1; }
  done
done
# mtools가 만든 이미지 (폴더, 긴 이름 포함) 읽기 + 폴더 늘리기
dd if=/dev/zero of=$B/m.dsk bs=1024 count=720 2>/dev/null
mformat -i $B/m.dsk -f 720 ::
head -c 30000 /dev/urandom > $B/a.bin; mcopy -i $B/m.dsk $B/a.bin ::A.BIN; mmd -i $B/m.dsk ::SUB
./build/msxfattest read $B/m.dsk | grep -q "^A.BIN FILE 30000" || { echo "read mtools image"; exit 1; }
./build/msxfattest sub $B/m.dsk > /dev/null
[ "$(mdir -b -i $B/m.dsk ::SUB | wc -l)" = 40 ] || { echo "sub count"; exit 1; }
fsck $B/m.dsk
mtype -i $B/m.dsk ::SUB/F39.TXT | wc -c | grep -q "^139$" || { echo "sub file"; exit 1; }
tail -1 $B/out.txt
echo "dskimg: 9 disk formats + 3 hard disk images OK (mtools, fsck.fat)"
