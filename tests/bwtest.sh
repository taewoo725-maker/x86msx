#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - 8.0.45(IP 기다리기): 네트워크 롬 INIT 의 "계속 기다릴까?" 묻기 + 프런트엔드의 기다림 규칙 시험.
#   1) tests/bwtest.c: 실제 unapi.rom + netcard + 콜백 — N번 "기다림" 뒤 "부팅", 콜백 없으면 바로 부팅, 스택 균형
#   2) fehost(가짜 MSX): 주소를 이미 받았으면 바로 지나감, 못 받으면 NetBootWait 초를 넘기지 않음, 카트리지가 없으면 안 기다림
#   usage: bash tests/bwtest.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build
gcc -O1 -o build/bwtest src/z80.c src/netcard.c tests/bwtest.c
for a in "-1" "0" "1" "5" "300" "3 0 1" "3 1 0" "3 1 1" "0 1 1" "5 1 0 1" "5 0 0 1" "1 1 0 1" "3 1 1 1"; do build/bwtest assets/net/unapi.rom $a >/dev/null || { build/bwtest assets/net/unapi.rom $a; exit 1; }; done   # N [무선] [F2] [시계글자]
T=$(mktemp -d); mkdir -p "$T/Machines/Shared Roms"; ln -s "$PWD/tests/machines/cbios2" "$T/Machines/cbios2"; cp assets/net/unapi.rom "$T/Machines/Shared Roms/UNAPI.ROM"
run() { env ROOT=$T MACHINE=cbios2 KEYS="" TAIL=1 FENET=1 "$@" timeout 300 ./build/fehost 2>&1 | grep '^NETBW'; }
fail() { echo "bwtest FAIL: $1"; rm -rf $T; exit 1; }
# 주소를 이미 받은 상태 → 기다림 끝
o=$(run START=500 FENETON=1 FESP=59 FESP_F=5 FENETST=400); case "$o" in "NETBW 1 "*) ;; *) fail "주소가 있는데 기다림: $o";; esac
# 못 받은 상태: NetBootWait=3 초를 넘겨서 붙잡지 않는다 (가짜 MSX 는 부팅을 되풀이하므로 매번 새 기다림이 시작돼도 경과는 3초를 못 넘는다)
printf 'NetBootWait=3\n' > $T/x86msx.ini
max=0; seen=0
for f in 300 340 380 420 460 500 540 580; do
  o=$(run START=$((f+10)) FESP=59 FESP_F=5 FENETST=$f); el=$(echo "$o" | awk '{print $4}')
  [ "$el" -gt "$max" ] && max=$el; [ "$el" -ge 1000 ] && seen=1
done
[ "$max" -lt 3500 ] || fail "제한 시간 3초를 넘김 (최대 ${max} ms)"
[ "$seen" = 1 ] || fail "기다리는 모습이 없음 (최대 ${max} ms)"
# 0 이면 안 기다림
printf 'NetBootWait=0\n' > $T/x86msx.ini
o=$(run START=500 FESP=59 FESP_F=5 FENETST=400); case "$o" in "NETBW 1 "*) ;; *) fail "NetBootWait=0 인데 기다림: $o";; esac
# 무선 카트리지의 안내 화면·F2 대기는 MSX 가 부팅할 때마다 (8.0.51)
printf 'NetWifiOn=1\n' > $T/x86msx.ini
o=$(run START=800 FENET=2 FESP=68 FESP_F=5 FENETST=790)
st=$(echo "$o" | sed 's/.*STARTS \([0-9]*\).*/\1/'); sh=$(echo "$o" | sed 's/.*SHOWS \([0-9]*\).*/\1/')
[ "$st" -ge 2 ] || fail "가짜 MSX 가 부팅을 되풀이하지 않아 시험이 되지 않음: $o"
[ "$sh" = "$st" ] || fail "안내 화면이 부팅마다 나와야 함 (부팅 $st 번 중 $sh 번): $o"
# 8.0.49 인터넷 시간: IP 를 받으면 시계를 맞춘다 (한국 +9 기본), 시간대·끄기·서버 무응답
ntp() { env ROOT=$T MACHINE=cbios2 KEYS="" TAIL=1 FENET=2 FENETHOST=1 FERTC=1 FENETON=1 FESP=68 FESP_F=5 START=500 FENETST=490 "$@" timeout 300 ./build/fehost 2>&1 | grep -E '^(SETTIME|NTPST)'; }
printf 'NetWifiOn=1\nNetSsid=x\n' > $T/x86msx.ini
o=$(ntp FENTP=1793491200); echo "$o" | grep -q '^SETTIME 2026-11-01 09:00:0' || fail "NTP: 한국 시간 09:00 이 아님: $o"; echo "$o" | grep -q '^NTPST 1' || fail "NTP: 맞춤 상태 아님: $o"
printf 'NetWifiOn=1\nNetSsid=x\nNtpTz=-330\n' > $T/x86msx.ini
o=$(ntp FENTP=1793491200); echo "$o" | grep -q '^SETTIME 2026-10-31 18:30:0' || fail "NTP: 시간대 -5:30 이 안 먹음: $o"
printf 'NetWifiOn=1\nNetSsid=x\nNtpTz=840\n' > $T/x86msx.ini
o=$(ntp FENTP=1793491200); echo "$o" | grep -q '^SETTIME 2026-11-01 14:00:0' || fail "NTP: 시간대 +14 이 안 먹음: $o"
printf 'NetWifiOn=1\nNetSsid=x\nNtpOn=0\n' > $T/x86msx.ini
o=$(ntp FENTP=1793491200); echo "$o" | grep -q '^NTPST 0' || fail "NTP: 끔인데 맞춤: $o"; echo "$o" | grep -q 'SETTIME 2026-11-01 09:00:00' && fail "NTP: 끔인데 시계를 바꿈: $o"
printf 'NetWifiOn=1\nNetSsid=x\n' > $T/x86msx.ini
o=$(ntp FENTP=1793491200 FENTP_MODE=1 START=2000 FENETST=1990); echo "$o" | grep -q '^NTPST 2' || fail "NTP: 서버 무응답인데 실패로 안 잡힘: $o"; echo "$o" | grep -q 'SETTIME 2026-11-01 09:00:00' && fail "NTP: 무응답인데 시계를 바꿈: $o"
# 메뉴: 시간 자동 맞춤 끄기(스페이스) + 시간대 →→→ (30분씩) 이 ini 에 저장됨
printf 'NetWifiOn=1\nNetSsid=x\n' > $T/x86msx.ini
env ROOT=$T MACHINE=cbios2 KEYS="down,down,down,down,space,down,right,right,right,down,right,right,right" GAP=6 START=60 TAIL=200 FENET=2 FENETHOST=1 FERTC=1 FENETON=1 FESP=68 FESP_F=5 FEACT=net FEACT_F=40 FEACT2=inisave FEACT2_F=190 timeout 300 ./build/fehost >/dev/null 2>&1
grep -q '^NtpOn=0' $T/x86msx.ini && grep -q '^NtpServer=3' $T/x86msx.ini && grep -q '^NtpTz=630' $T/x86msx.ini || fail "메뉴에서 시간 줄을 바꿔도 ini 에 안 남음: $(grep Ntp $T/x86msx.ini | tr '\n' ' ')"
# 8.0.50: x86wifi 카트리지가 꽂힌 채 끝냈다 켜면(이어하기) MSX 는 새로 부팅해야 롬 INIT(안내 화면)이 돌고, 유선은 그대로 이어서
printf 'Machine=cbios2\nNetWifiOn=1\nNetSsid=x\n' > $T/x86msx.ini
rs() { rm -rf $T/States; env ROOT=$T MACHINE=cbios2 TAIL=1 FENET=2 FENETHOST=1 FENETON=1 KEYS=enter,y,enter START=300 FESP=$1 FESP_F=5 FEACT=exit FEACT_F=280 timeout 300 ./build/fehost >/dev/null 2>&1
      env ROOT=$T MACHINE=cbios2 TAIL=1 FENET=2 FENETHOST=1 FENETON=1 KEYS="" START=800 FENETST=790 timeout 300 ./build/fehost 2>&1 | grep '^NETBW'; }
o=$(rs 68); echo "$o" | grep -Eq 'SHOWS [1-9]' || fail "무선 카트리지로 이어하기 했는데 새로 부팅하지 않음(안내 화면 없음): $o"
# 8.0.52: 진단 > 스위치 탭에서 바꾸면 창을 닫기 전에도 바로 ini 에 저장된다
printf 'Machine=cbios2\n' > $T/x86msx.ini
env ROOT=$T MACHINE=cbios2 KEYS="enter" GAP=8 START=60 TAIL=300 FEACT=diagsw FEACT_F=40 timeout 120 ./build/fehost >/dev/null 2>&1
grep -q '^LogDiag=0' $T/x86msx.ini || fail "진단 스위치를 바꿨는데 바로 저장되지 않음"
# 8.0.58: 진단 스위치는 별도 파일(X86MSX.SW)에도 저장되고, 설정 파일이 되돌려져도 그 값이 이긴다
grep -q '^LogDiag=0' $T/X86MSX.SW || fail "X86MSX.SW 에 스위치가 저장되지 않음: $(cat $T/X86MSX.SW 2>&1)"
printf 'Machine=cbios2\nLogDiag=1\nLogKey=0\n' > $T/x86msx.ini
env ROOT=$T MACHINE=cbios2 KEYS="" START=60 TAIL=100 FEACT=inisave FEACT_F=50 timeout 120 ./build/fehost >/dev/null 2>&1
grep -q '^LogDiag=0' $T/x86msx.ini || fail "설정 파일이 되돌려졌는데 X86MSX.SW 의 값이 이기지 않음: $(grep Log $T/x86msx.ini | tr '\n' ' ')"
printf 'LogKey=1\nLogSnd=1\n' > $T/X86MSX.SW
env ROOT=$T MACHINE=cbios2 KEYS="" START=60 TAIL=100 FEACT=inisave FEACT_F=50 timeout 120 ./build/fehost >/dev/null 2>&1
grep -q '^LogKey=1' $T/x86msx.ini && grep -q '^LogSnd=1' $T/x86msx.ini || fail "X86MSX.SW 에서 켠 스위치가 부팅 때 적용되지 않음: $(grep Log $T/x86msx.ini | tr '\n' ' ')"
echo "bwtest: ok"
rm -rf $T
