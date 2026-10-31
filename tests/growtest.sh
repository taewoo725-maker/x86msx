#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# USB 전체 용량 사용 (usbgrow.c) 시험: 새 파티션 만들기 → fsck.fat → 파일 비교 → 옛 구역이 그대로인지
# 그리고 실패해야 하는 경우(다른 파티션이 있음, 이미 큼)에 MBR 이 안 바뀌는지
cd "$(dirname "$0")/.."
export MTOOLS_SKIP_CHECK=1
IMG=build/msx1.img
[ -f $IMG ] || { echo "growtest: $IMG 없음 (make build/msx1.img)"; exit 1; }
T=$(mktemp -d); bad=0
fail() { echo "FAIL growtest: $1"; bad=1; }
G=./build/growtest
# 큰 USB 흉내: 64 MiB 뒤를 쓰레기(0xA5)로 채움 (실제 USB 는 옛 내용이 남아 있음 - 안 쓴 자리를 잡아내려면 필요)
junk() { python3 -I -c "
import sys
f=open(sys.argv[1],'r+b'); f.seek(64<<20)
n=(int(sys.argv[2])<<20)-(64<<20)
while n>0:
    k=min(n,1<<24); f.write(b'\\xA5'*k); n-=k" $1 $2; }
mbr() { python3 -I -c "
import struct,sys
d=open(sys.argv[1],'rb').read(512)
print('|'.join('%d,%d,%d' % (d[446+16*i+4], *struct.unpack('<II', d[446+16*i+8:446+16*i+16])) for i in range(2)))" $1; }

# 1) 정상: 64 MiB -> 512 MiB
cp $IMG $T/a.img; truncate -s 512M $T/a.img; junk $T/a.img 512
$G $T/a.img > $T/a.log 2>&1; r=$?
[ $r = 0 ] || fail "정상 확장 실패 ($(tail -1 $T/a.log))"
m=$(mbr $T/a.img)
[ "${m%%|*}" = "12,131072,917504" ] || fail "MBR 첫 항목: $m"
[ "${m##*|}" = "0,0,0" ] || fail "둘째 항목이 비어 있지 않음: $m"
dd if=$T/a.img of=$T/part.img bs=512 skip=131072 count=917504 2>/dev/null
fsck.fat -n $T/part.img > $T/fsck.log 2>&1 || fail "fsck.fat: $(tail -3 $T/fsck.log | tr '\n' ' ')"
mkdir $T/o $T/n
mcopy -s -n -i $IMG@@$((2048*512)) :: $T/o/ 2>/dev/null
mcopy -s -n -i $T/a.img@@$((131072*512)) :: $T/n/ 2>/dev/null
diff -r $T/o $T/n > /dev/null || fail "새 구역 파일이 옛것과 다름"
[ "$(find $T/n -type f | wc -l)" -gt 20 ] || fail "파일 수가 너무 적음"
# 옛 구역(LBA 2048~131071)은 한 바이트도 안 바뀜
cmp -n $((129024*512)) -i $((2048*512)) $IMG $T/a.img || fail "옛 구역이 바뀜"
# LBA 0~2047 (MBR 부트 코드·로더)도 MBR 파티션 표 말고는 그대로
cmp -n 446 $IMG $T/a.img || fail "MBR 부트 코드가 바뀜"
cmp -n $((2047*512)) -i 512 $IMG $T/a.img || fail "로더 영역이 바뀜"

# 2) 이미 전체 크기 (64 MiB 그대로): 아무것도 안 함
cp $IMG $T/b.img
$G $T/b.img > $T/b.log 2>&1; [ $? = 0 ] && grep -q "result 1" $T/b.log || fail "이미 전체 크기인데 1이 아님: $(tail -1 $T/b.log)"
cmp $IMG $T/b.img || fail "이미 전체 크기인데 디스크가 바뀜"

# 3) 늘어나는 양이 128 MiB 미만이면 안 함 (192 MiB 디스크)
cp $IMG $T/c.img; truncate -s 160M $T/c.img
$G $T/c.img > $T/c.log 2>&1; grep -q "result 1" $T/c.log || fail "조금만 늘어나는 디스크를 건드림: $(tail -1 $T/c.log)"

# 4) 다른 파티션이 있으면 거절하고 MBR 그대로
cp $IMG $T/d.img; truncate -s 512M $T/d.img
python3 -I -c "
import sys
f=open(sys.argv[1],'r+b'); d=bytearray(f.read(512)); d[446+16+4]=0x83; d[446+16+8:446+16+16]=(900000).to_bytes(4,'little')+(1000).to_bytes(4,'little'); f.seek(0); f.write(d)" $T/d.img
cp $T/d.img $T/d0.img
$G $T/d.img > $T/d.log 2>&1; grep -q "other partitions" $T/d.log || fail "다른 파티션을 막지 않음: $(tail -1 $T/d.log)"
cmp $T/d0.img $T/d.img || fail "거절했는데 디스크가 바뀜"

# 5) 한 번 늘린 USB 를 다시 늘리면 할 일 없음 (1)
$G $T/a.img > $T/e.log 2>&1; grep -q "result 1" $T/e.log || fail "두 번째 확장이 1이 아님: $(tail -1 $T/e.log)"

# 8) 폴더 하나에 파일이 아주 많을 때 (64개씩 나눠 읽는 경계, 긴 이름·한글 이름·빈 파일 포함)
cp $IMG $T/m.img; truncate -s 512M $T/m.img; junk $T/m.img 512
mkdir -p $T/many; for i in $(seq 1 321); do printf "data $i %0*d" $((i % 700)) 0 > "$T/many/긴 파일 이름 번호 $i.rom"; done; : > "$T/many/빈 파일.txt"
mmd -i $T/m.img@@$((2048*512)) ::/MANY 2>/dev/null; mcopy -i $T/m.img@@$((2048*512)) $T/many/* ::/MANY/ 2>/dev/null
cp $T/m.img $T/m0.img
$G $T/m.img > $T/m.log 2>&1; grep -q "result 0" $T/m.log || fail "파일 많은 폴더 확장 실패: $(tail -1 $T/m.log)"
mkdir $T/mo $T/mn
mcopy -s -n -i $T/m0.img@@$((2048*512)) :: $T/mo/ 2>/dev/null; mcopy -s -n -i $T/m.img@@$((131072*512)) :: $T/mn/ 2>/dev/null
diff -r $T/mo $T/mn > /dev/null || fail "파일 많은 폴더 내용이 다름"
[ "$(ls $T/mn/MANY | wc -l)" -ge 322 ] || fail "파일 많은 폴더의 파일 수: $(ls $T/mn/MANY | wc -l)"
dd if=$T/m.img of=$T/mp.img bs=512 skip=131072 count=917504 2>/dev/null; fsck.fat -n $T/mp.img > /dev/null 2>&1 || fail "파일 많은 폴더 fsck"

# 6) 쓰다가 실패하면 (여러 지점) 오류로 끝나고 MBR 은 그대로 = 옛 USB 그대로 부팅
for n in 1 3 10 40 100; do
  cp $IMG $T/f.img; truncate -s 512M $T/f.img; junk $T/f.img 512; cp $T/f.img $T/f0.img
  FAILAT=$n $G $T/f.img > $T/f.log 2>&1; r=$?
  [ $r != 0 ] && grep -q "result -1" $T/f.log || fail "FAILAT=$n 오류로 안 끝남: $(tail -1 $T/f.log)"
  cmp -n 512 $T/f0.img $T/f.img || fail "FAILAT=$n 인데 MBR 이 바뀜"
  cmp -n $((129024*512)) -i $((2048*512)) $T/f0.img $T/f.img || fail "FAILAT=$n 인데 옛 구역이 바뀜"
done

# 7) 쓴 줄 알았는데 실제로는 안 써진 경우 (조용히 버려짐): 확인 단계가 잡아야 함
for n in 1 2 3 4 5 6 8 12 20 30 50 80 200 400; do
  cp $IMG $T/f.img; truncate -s 512M $T/f.img; junk $T/f.img 512; cp $T/f.img $T/f0.img
  DROPAT=$n $G $T/f.img > $T/f.log 2>&1
  if grep -q "result 0" $T/f.log; then
    # 버려진 쓰기가 하필 중요하지 않은 곳(0으로 채우는 자리 등)이면 성공할 수 있음 - 그때는 결과가 온전해야 함
    dd if=$T/f.img of=$T/p.img bs=512 skip=131072 count=917504 2>/dev/null
    fsck.fat -n $T/p.img > /dev/null 2>&1 || fail "DROPAT=$n 성공이라는데 새 구역이 손상"
  else
    cmp -n 512 $T/f0.img $T/f.img || fail "DROPAT=$n 실패했는데 MBR 이 바뀜"
  fi
done

rm -rf $T
[ $bad = 0 ] && echo "growtest: OK" || exit 1
