#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - 5.2.90: 특수 카트리지 전부(spcat.c kind 0~67)를 하나씩 꽂고, 꽂은 뒤 SCC 로 바꿔 꽂아(앞 장치 빼기) 카트 자리·장치 상태를 기준과 비교.
#   5.2.86~5.2.89 에 17종(디스크 인터페이스·SCC·FM-PAC·DOS2·SCC-I·IDE·VHD·X86Network)이 꽂히지 않던 회귀를 잡으려고 만듦.
#   기준 tests/data/spcart_expected.txt = 5.2.85 와 같은 결과 (가짜 롬은 고정 시드로 이 스크립트가 만듦)
#   usage: bash tests/spcarttest.sh [update]
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
mkdir -p "$T/Machines/Shared Roms"; ln -s "$PWD/tests/machines/cbios2" "$T/Machines/cbios2"
python3 - "$T/Machines/Shared Roms" <<'P'
import re,random,os,sys
d=sys.argv[1]; src=open('src/spcat.c',encoding='utf-8').read()
def size(n):
    l=n.lower()
    if 'moonsound' in l or 'yrw801' in l: return 2*1024*1024
    if 'fmpac' in l or 'pac.rom' in l or 'dos2' in l or 'msxdos' in l: return 65536
    if 'sunrise' in l or 'nextor' in l or 'ide240' in l or 'ca1' in l: return 131072
    if 'nms1205' in l or 'musicmodule' in l or 'hx-mu900' in l or 'sfg' in l: return 32768
    return 16384
random.seed(11)
for n in sorted(set(re.findall(r'SR "([^"|]+)',src)) | {'MOONSOUND.rom'}):   # MoonSound 웨이브 롬은 꽂기 코드(fe_extdev.inc)가 직접 찾음
    b=bytearray(random.getrandbits(8) for _ in range(size(n))); b[0:2]=b'AB'
    open(os.path.join(d,n),'wb').write(b)
P
OUT=$T/out.txt; : > $OUT
for k in $(seq 0 67); do
  for mode in a b; do
    X=""; [ $mode = b ] && X="FEACT2=special:2 FEACT2_F=40"
    echo "== kind $k $mode" >> $OUT
    env ROOT=$T MACHINE=cbios2 FEACT=special:$k FEACT_F=5 $X CARTDUMP=1 START=60 KEYS="" TAIL=1 timeout 60 ./build/fehost 2>&1 | grep -v "^fehost:\|^write " >> $OUT || true
  done
done
if [ "$1" = update ]; then cp $OUT tests/data/spcart_expected.txt; echo "spcarttest: 기준 갱신"; rm -rf $T; exit 0; fi
if cmp -s $OUT tests/data/spcart_expected.txt; then
  echo "spcarttest: OK ($(grep -c '^== kind' $OUT) 경우, 꽂힌 종류 $(awk '/^== kind/{k=$3" "$4} /present=1/{print k}' $OUT | sort -u | grep -c ' a$'))"
  rm -rf $T
else
  diff $OUT tests/data/spcart_expected.txt | head -20; echo "spcarttest: FAIL"; rm -rf $T; exit 1
fi
