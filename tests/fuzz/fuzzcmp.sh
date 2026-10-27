#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - 구조 개편 동등성 시험 (5.2.5): 이전 판 소스와 지금 소스를 같은 무작위 입력으로 돌려 결과 해시를 비교
#   usage: tests/fuzz/fuzzcmp.sh <이전 판 msxemu 소스 폴더>
#   memfuzz: 매퍼 41종 × 롬 3크기, 메모리 읽기·쓰기 + 슬롯(A8)·서브슬롯(FFFF) 바꿈 + FDC·DOS2·펌웨어 레지스터 주소
#   iofuzz : I/O IN/OUT 30만 번 × 시드 3
#   profcmp: config.ini 해석 (실제 기종 파일 tests/regress/local/configs + 형식 번호 합성 줄) + 장치 목록·머신 편집기 형식 표 (5.2.8)
#   setupcmp: 기종 만들기 msx_setup (같은 기종 파일·합성 줄 + 가짜 롬) — 슬롯 표·카트 자리·FDC·DOS2·펌웨어·한자·매퍼 RAM (5.2.89)
#   기종: tests/machines 의 cbios2·cbios2_dos2(가짜 DOS2 롬)·cbios2_dos2fdc(DOS2 + WD2793 디스크 같은 서브슬롯) + tests/regress/local/machines 에 있는 것
set -e
OLD=${1:?이전 판 소스 폴더}
NEW=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d)
CORE=$(sed -n 's/^CORE *= *//p' "$NEW/Makefile")
OCORE=$(sed -n 's/^CORE *= *//p' "$OLD/Makefile")
for f in memfuzz iofuzz setupcmp; do
  (cd "$NEW" && gcc -O2 -std=c11 -w -Isrc -Ihost -o $T/${f}_new tests/fuzz/$f.c $CORE -lm)
  (cd "$OLD" && gcc -O2 -std=c11 -w -Isrc -Ihost -o $T/${f}_old "$NEW/tests/fuzz/$f.c" $OCORE -lm)
done
pc() { (cd "$1" && gcc -O1 -std=c11 -w -Isrc -o "$2" "$NEW/tests/fuzz/profcmp.c" src/profile.c $( [ -f src/devcat.c ] && echo src/devcat.c ) kernel/medit.c); }
pc "$NEW" $T/pc_new; pc "$OLD" $T/pc_old
CFG="$NEW/tests/machines"; [ -d "$NEW/tests/regress/local/configs" ] && CFG="$NEW/tests/regress/local/configs $CFG"
$T/pc_new $CFG > $T/p_new; $T/pc_old $CFG > $T/p_old
MACH="$NEW/tests/machines/cbios2 $NEW/tests/machines/cbios2_dos2 $NEW/tests/machines/cbios2_dos2fdc"
for d in "$NEW"/tests/regress/local/machines/*/; do [ -f "$d/config.ini" ] && MACH="$MACH ${d%/}"; done
fail=0
for mc in $MACH; do
  n=$(basename $mc)
  ( $T/memfuzz_new $mc > $T/m_new_$n 2>/dev/null; $T/memfuzz_old $mc > $T/m_old_$n 2>/dev/null
    for s in 1 2 3; do $T/iofuzz_new $mc $s; done > $T/i_new_$n 2>/dev/null
    for s in 1 2 3; do $T/iofuzz_old $mc $s; done > $T/i_old_$n 2>/dev/null ) &
done
wait
for mc in $MACH; do
  n=$(basename $mc)
  for k in m i; do
    if cmp -s $T/${k}_new_$n $T/${k}_old_$n; then r=SAME; else r=DIFF; fail=1; fi
    printf "%-16s %s %4d줄 %s\n" $n $([ $k = m ] && echo mem || echo io ) $(wc -l < $T/${k}_new_$n) $r
  done
done
if cmp -s $T/p_new $T/p_old; then r=SAME; else r=DIFF; fail=1; fi
printf "%-16s %s %4d줄 %s\n" config.ini parse $(wc -l < $T/p_new) $r
$T/setupcmp_new $CFG > $T/s_new; $T/setupcmp_old $CFG > $T/s_old
if cmp -s $T/s_new $T/s_old; then r=SAME; else r=DIFF; fail=1; fi
printf "%-16s %s %4d줄 %s\n" config.ini setup $(wc -l < $T/s_new) $r
rm -rf $T
[ $fail = 0 ] && echo "fuzzcmp: 모두 같음" || { echo "fuzzcmp: 다름 있음"; exit 1; }
