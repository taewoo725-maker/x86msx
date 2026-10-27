#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 설정 표 회귀 시험 — 일부러 이상한 값을 넣은 ini 로 켜고 끝냈을 때 ini·기종별 옵션 파일이 기준(5.2.34 판 결과)과 같은지
set -e
R=build/initest_root; rm -rf $R; mkdir -p $R/Machines; cp -r machines/C-BIOS $R/Machines/; cp tests/data/ini_seed.ini $R/x86msx.ini
ROOT=$R MACHINE=C-BIOS W=1280 H=800 START=20 KEYS=enter,y,enter TAIL=60 FEACT=exit FEACT_F=10 ./build/fehost > /dev/null 2>&1 || true   # A_EXIT 로 끝나면 0 이 아닐 수 있음
tr -d '\r' < $R/x86msx.ini | tr -d '\0' | grep "=" | grep -v "^;" | sort > build/initest_ini.txt
tr -d '\r' < "$R/States/Resume/MSX - C-BIOS.opt" | sort > build/initest_opt.txt
diff tests/data/ini_seed_expected.txt build/initest_ini.txt > build/initest.diff && diff tests/data/opt_seed_expected.txt build/initest_opt.txt >> build/initest.diff && echo "initest: OK" || { echo "initest: FAIL"; head -20 build/initest.diff; exit 1; }
