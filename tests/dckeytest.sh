#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 파일 관리자(F11) 창이 떠 있어도 F10(메뉴)·리셋 단축키가 먹는지 (창이 닫혀야 함)
cd "$(dirname "$0")/.."
R=$(mktemp -d); mkdir -p $R/Machines "$R/Storage Device/ROMS"; ln -s $PWD/machines/C-BIOS $R/Machines/C-BIOS
printf '[State]\nMachine=MSX - C-BIOS\nLanguage=ko\n' > $R/x86msx.ini
bad=0
for ks in f10 alt+f2 ctrl+f2; do
  d=$(env ROOT=$R MACHINE=C-BIOS W=1280 H=800 FEACT=diskcopy KEYS="$ks" GAP=6 START=260 TAIL=16 REDRAWCMP=1 SHOT=$R/s timeout 120 ./build/fehost 2>&1 | grep "^DLG" | head -1)
  [ "$d" = "DLG 0" ] || { echo "FAIL $ks: $d"; bad=1; }
done
# 음량 키(LAlt+↓)는 창을 닫지 않고 음량만 바꿈 (OSD 막대가 보여야 함)
d=$(env ROOT=$R MACHINE=C-BIOS W=1280 H=800 FEACT=diskcopy KEYS="alt+down" GAP=6 START=260 TAIL=3 REDRAWCMP=1 SHOT=$R/s timeout 120 ./build/fehost 2>&1 | grep "^DLG" | head -1)
[ "$d" = "DLG 13" ] || { echo "FAIL vol: $d"; bad=1; }
rm -rf $R
[ $bad = 0 ] && echo "dckeytest: OK" || exit 1
