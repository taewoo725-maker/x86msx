#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - 8.0.45: 네트워크 카트리지 분리(X86Ethernet 유선 / x86wifi 무선) 시험.
#   NETST = 켜짐(유선 1·무선 2) + 카트리지(유선 4·무선 8). 둘 다 꽂으면 무선이 기본, 한쪽을 켜면 다른 쪽은 꺼진다.
#   usage: bash tests/nettest.sh
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d)
mkdir -p "$T/Machines/Shared Roms"; ln -s "$PWD/tests/machines/cbios2" "$T/Machines/cbios2"
cp assets/net/unapi.rom "$T/Machines/Shared Roms/UNAPI.ROM"
run() { env ROOT=$T MACHINE=cbios2 START=60 KEYS="" TAIL=1 FENETST=50 "$@" timeout 60 ./build/fehost 2>&1 | grep '^NETST' | tail -1 | cut -d' ' -f2; }
chk() { [ "$2" = "$3" ] || { echo "nettest FAIL: $1 기대 $3 실제 $2"; rm -rf $T; exit 1; }; }
chk "유선만(어댑터 둘)"        "$(run FENET=3 FESP=59 FESP_F=5)" 5
chk "무선만(어댑터 둘)"        "$(run FENET=3 FESP=68 FESP_F=5)" 10
chk "유선→무선 순서"           "$(run FENET=3 FESP=59 FESP_F=5 FESP2=68)" 14
chk "무선→유선 순서"           "$(run FENET=3 FESP=68 FESP_F=5 FESP2=59)" 14
chk "유선 어댑터만 + 무선카트리지" "$(run FENET=1 FESP=68 FESP_F=5)" 8
chk "무선 어댑터만 + 유선카트리지" "$(run FENET=2 FESP=59 FESP_F=5)" 4
chk "유선 어댑터만 + 유선카트리지" "$(run FENET=1 FESP=59 FESP_F=5)" 5
chk "카트리지 없음"            "$(run FENET=3)" 0
echo "nettest: OK"
rm -rf $T
