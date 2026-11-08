#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
ck() { fsck.fat -n "$1" 2>&1 | grep -v -i "label\|leaving\|fsck.fat\|files, \|dirty bit\|^\s*$" | grep -q . && return 1; return 0; }   # FAT12 MSX 부트 섹터는 볼륨 이름 칸이 없음 (경고만)
# vfattest.sh - 3.7.1 가상 하드 시험 (mtools·fsck.fat 필요): 폴더 → 볼륨 → 고치기 → USB 반영 → 다시 마운트
T=$(mktemp -d); B=./build/vfattest; fail=0
mkdir -p $T/A/SUB/DEEP
head -c 100000 /dev/urandom > $T/A/big.bin; echo hello > $T/A/readme.txt; head -c 3000 /dev/urandom > "$T/A/Long File Name.dat"
echo x > $T/A/SUB/a.txt; head -c 70000 /dev/urandom > $T/A/SUB/DEEP/b.bin
echo "new file" > $T/new.txt; head -c 50000 /dev/urandom > $T/b2
for k in 3 2 1; do
  rm -rf $T/B; cp -r $T/A $T/B
  $B $T/B 8 $k dump $T/img.bin >/dev/null || fail=1
  dd if=$T/img.bin of=$T/p.bin bs=512 skip=1 2>/dev/null
  ck $T/p.bin || { echo "kind $k: fsck"; fail=1; }
  mcopy -n -i $T/p.bin ::BIG.BIN $T/o 2>/dev/null && cmp -s $T/o $T/A/big.bin || { echo "kind $k: read"; fail=1; }
  cp $T/p.bin $T/q.bin
  mcopy -i $T/q.bin $T/new.txt ::NEW.TXT; mdel -i $T/q.bin ::README.TXT; mren -i $T/q.bin ::LONGFI~1.DAT ::X.DAT; mcopy -o -i $T/q.bin $T/b2 ::BIG.BIN
  [ $k != 1 ] && { mmd -i $T/q.bin ::NEWDIR; mcopy -i $T/q.bin $T/new.txt ::NEWDIR/IN.TXT; }
  (dd if=$T/img.bin bs=512 count=1 2>/dev/null; cat $T/q.bin) > $T/qi.bin
  $B $T/B 8 $k apply $T/qi.bin >/dev/null || fail=1
  [ -f $T/B/readme.txt ] && { echo "kind $k: delete"; fail=1; }
  cmp -s $T/B/big.bin $T/b2 && cmp -s $T/B/NEW.TXT $T/new.txt && [ -f $T/B/X.DAT ] || { echo "kind $k: write back"; fail=1; }
  # 8.0.42: 시간 제한(조각내기)으로 반영해도 결과가 똑같아야 함
  rm -rf $T/C; cp -r $T/A $T/C
  VF_SLICE=1 $B $T/C 8 $k apply $T/qi.bin > $T/slice.out || { echo "kind $k: slice run"; cat $T/slice.out; fail=1; }
  diff -r $T/B $T/C >/dev/null || { echo "kind $k: slice result differs"; fail=1; }
  $B $T/B 8 $k dump $T/re.bin >/dev/null; dd if=$T/re.bin of=$T/rp.bin bs=512 skip=1 2>/dev/null
  ck $T/rp.bin || { echo "kind $k: remount fsck"; fail=1; }
done
# 3.8.0 (A-1): MSX-DOS1 볼륨에서 MSXDOS.SYS의 LD SP,9000h → C800h (MSX가 읽는 내용만; 다른 형식·다른 내용은 그대로)
python3 - $T <<'PY'
import sys; t=sys.argv[1]
import os; os.makedirs(t+'/D1',exist_ok=True); os.makedirs(t+'/D2',exist_ok=True)
b=bytearray(range(256))*10; b=b[:2432]; b[0]=0xC3; b[0x138:0x13B]=bytes([0x31,0,0x90])
open(t+'/D1/MSXDOS.SYS','wb').write(b); open(t+'/D1/COMMAND.COM','wb').write(b'x'*100)
c=bytearray(b); c[0x138]=0x21; open(t+'/D2/MSXDOS.SYS','wb').write(c); open(t+'/D2/COMMAND.COM','wb').write(b'x'*100)
PY
for d in D1 D2; do
  $B $T/$d 8 1 dump $T/d.bin >/dev/null || fail=1; dd if=$T/d.bin of=$T/dp.bin bs=512 skip=1 2>/dev/null
  mcopy -n -i $T/dp.bin ::MSXDOS.SYS $T/ds.$d 2>/dev/null || { echo "dos1 $d: read"; fail=1; }
done
[ "$(od -An -tx1 -j 312 -N3 $T/ds.D1 | tr -d ' ')" = "3100c2" ] || { echo "dos1: SP patch not applied"; fail=1; }
head -c 314 $T/ds.D1 > $T/h1; head -c 314 $T/D1/MSXDOS.SYS > $T/h2; tail -c +316 $T/ds.D1 > $T/t1; tail -c +316 $T/D1/MSXDOS.SYS > $T/t2
cmp -s $T/h1 $T/h2 && cmp -s $T/t1 $T/t2 || { echo "dos1: other bytes changed"; fail=1; }
cmp -s $T/ds.D2 $T/D2/MSXDOS.SYS || { echo "dos1: unrelated MSXDOS.SYS changed"; fail=1; }
$B $T/D1 8 3 dump $T/d3.bin >/dev/null; dd if=$T/d3.bin of=$T/d3p.bin bs=512 skip=1 2>/dev/null
mcopy -n -i $T/d3p.bin ::MSXDOS.SYS $T/ds.f16 2>/dev/null; cmp -s $T/ds.f16 $T/D1/MSXDOS.SYS || { echo "FAT16: MSXDOS.SYS changed"; fail=1; }
# 3.9.12: 파일 4096개 넘는 폴더(5701개)와 한 폴더 600개 — 전부 보이고, 새 파일이 USB에 반영되는지 ("files" 실패였음)
python3 - $T <<'PY'
import os,sys
t=sys.argv[1]+'/L'
for d in range(30):
    os.makedirs('%s/D%02d'%(t,d))
    for f in range(170): open('%s/D%02d/F%03d.TXT'%(t,d,f),'wb').write(b'x')
os.makedirs(t+'/BIG')
for f in range(600): open('%s/BIG/B%04d.DAT'%(t,f),'wb').write(b'y')
open(t+'/NEXTOR.SYS','wb').write(b'z'*4467)
PY
$B $T/L 200 3 dump $T/l.bin >/dev/null || fail=1; dd if=$T/l.bin of=$T/lp.bin bs=512 skip=1 2>/dev/null
[ "$(mdir -/ -b -i $T/lp.bin :: 2>/dev/null | grep -c '\.')" = 5701 ] || { echo "big folder: not all files visible"; fail=1; }
[ "$(mdir -b -i $T/lp.bin ::BIG | wc -l)" = 600 ] || { echo "big folder: 600 entries not all visible"; fail=1; }
echo hi > $T/n.txt; mcopy -i $T/lp.bin $T/n.txt ::G.BAT; (dd if=$T/l.bin bs=512 count=1 2>/dev/null; cat $T/lp.bin) > $T/lq.bin
$B $T/L 200 3 apply $T/lq.bin >/dev/null || fail=1
[ -f $T/L/G.BAT ] || { echo "big folder: new file not written back"; fail=1; }
# 8.0.66: 소리 카트리지 파일 재생 — 짧은 이름 경로 → USB 파일 전체 경로·크기 (폴더는 안 됨)
mkdir -p "$T/FM/My Music"; echo 12345 > "$T/FM/My Music/Song Two Long.wav"; echo abc > "$T/FM/Another Track.flac"
o=$($B "$T/FM" 32 0 find '\MYMUSI~1\SONGTW~1.WAV' 'a:\anothe~1.fla' '\MYMUSI~1' '\NOPE.WAV' 2>&1)
printf "%s\n" "$o" | grep -q 'find .MYMUSI~1.SONGTW~1.WAV -> .*My Music.Song Two Long.wav 6$' || { echo "find: nested file not resolved"; fail=1; }
printf "%s\n" "$o" | grep -q 'find a:.anothe~1.fla -> .*Another Track.flac 4$' || { echo "find: root file (drive letter, lowercase) not resolved"; fail=1; }
printf "%s\n" "$o" | grep -q 'find .MYMUSI~1 -> (none)' || { echo "find: folder must not resolve"; fail=1; }
printf "%s\n" "$o" | grep -q 'find .NOPE.WAV -> (none)' || { echo "find: missing file must not resolve"; fail=1; }
# 8.0.169: 설정 용량보다 큰 폴더 — 마운트는 "full" 로 실패하지만 필요한 MB(올림)를 알려 줌. 그 값 이상 + 여유 용량이면 마운트됨
mkdir -p $T/OV/D1; for n in 1 2 3 4 5; do head -c 700000 /dev/urandom > $T/OV/F$n.bin; done; head -c 20000 /dev/urandom > $T/OV/D1/x.bin
o=$($B $T/OV 2 3 dump $T/ov.img 2>&1)
need=$(printf "%s\n" "$o" | sed -n 's/.*mount fail full need \([0-9]*\) MB.*/\1/p')
{ [ -n "$need" ] && [ "$need" -ge 4 ] && [ "$need" -le 5 ]; } || { echo "overflow: need MB missing or wrong ($o)"; fail=1; }
$B $T/OV 8 3 dump $T/ov.img >/dev/null 2>&1 || { echo "overflow: fits at 8 MB but mount failed"; fail=1; }
rm -rf $T
[ $fail = 0 ] && echo "vfattest: ok (FAT16, FAT12, DOS1)" || { echo "vfattest: FAIL"; exit 1; }
