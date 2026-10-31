#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
# 8.0.23 partest: 화면 줄 그리기를 여러 스레드로 나눠 돌린 화면(FEPAR=1)이 메인 혼자 그린 화면과 점 하나까지 같은지 본다.
#   기종 C-BIOS(MSX1)·cbios2p(MSX2+), 카트 여러 개, 키 몇 개 누르며 HASHALL(키마다 해시)과 DIRTY(그린 영역 빠짐) 를 비교.
set -e
cd "$(dirname "$0")/.."
BIN=${FEHOST_BIN:-./build/fehost}
T=$(mktemp -d); mkdir -p $T/Machines
ln -s "$PWD/machines/C-BIOS" $T/Machines/C-BIOS
ln -s "$PWD/tests/machines/cbios2p" $T/Machines/cbios2p
[ -d "$PWD/tests/machines/Shared Roms" ] && ln -s "$PWD/tests/machines/Shared Roms" "$T/Machines/Shared Roms"
fail=0; n=0
run() {   # $1 기종 $2 카트 $3 FEPAR
    printf '[State]\nMachine=%s\nLanguage=ko\n' "$1" > $T/x86msx.ini
    env ROOT=$T MACHINE=$1 W=1280 H=800 FECART=$2 FECART_F=3 FECARTRESET=1 KEYS="space,wait,right,wait,space,down,wait,wait" GAP=40 START=200 TAIL=5 HASHALL=1 DIRTY=1 ${3:+FEPAR=1} \
        timeout 200 $BIN 2>&1 | grep -E "^key|DIRTYMISS" || true
}
for mc in C-BIOS cbios2p; do
    for c in tests/carts/konami.rom tests/carts/konamiscc.rom tests/carts/ascii8.rom tests/regress/midline.rom; do
        [ -f "$c" ] || continue
        a=$(run $mc $c ""); b=$(run $mc $c 1); n=$((n + 1))
        if [ -z "$a" ]; then echo "partest: $mc $(basename $c) 결과 없음"; fail=1; continue; fi
        if [ "$a" != "$b" ]; then echo "partest FAIL: $mc $(basename $c)"; diff <(echo "$a") <(echo "$b") | head -6; fail=1
        else echo "partest ok: $mc $(basename $c) ($(echo "$a" | grep -c '^key') 화면 같음)"; fi
    done
done
rm -rf $T
[ $fail = 0 ] && echo "partest OK ($n)" || { echo "partest FAILED"; exit 1; }
