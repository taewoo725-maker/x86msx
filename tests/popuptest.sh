#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - 5.2.120: 팝업 보이는 방식 시험 (키 순서를 돌려 키마다 화면 해시를 얻고, 같아야 하는 화면끼리 비교)
#   메뉴에서 연 팝업: Esc 로 메뉴(펼친 단계 그대로)로 돌아가고 한 번 더 Esc 로 메뉴가 닫힘
#   단축키로 연 팝업: 같은 단축키를 다시 누르면 닫힘, Esc 는 그냥 닫힘 (메뉴가 뜨지 않음)
#   믹서(F12): 메뉴·팝업이 떠 있어도 먼저 열리고, 닫으면 하던 곳으로 돌아감
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d); R=$T/root; mkdir -p $R/Machines; ln -s "$PWD/machines/C-BIOS" $R/Machines/C-BIOS 2>/dev/null || ln -s /tmp/seltest/Machines/C-BIOS $R/Machines/C-BIOS
printf '[State]\nMachine=MSX - C-BIOS\nLanguage=ko\n' > $R/x86msx.ini
run() { env ROOT=$R MACHINE=C-BIOS W=1280 H=800 KEYS="$1" GAP=14 HASHALL=1 START=60 TAIL=10 timeout 120 ./build/fehost 2>&1 | grep "^key" | awk '{print $4}' | tr '\n' ' '; }
fail=0
chk() { if [ "$2" != "$3" ]; then echo "FAIL: $1 ($2 != $3)"; fail=1; fi; }
ne() { if [ "$2" = "$3" ]; then echo "FAIL: $1 (같으면 안 됨)"; fail=1; fi; }
# 인수: 키 순서 → $1.. 키마다 해시 (0번 = wait)
# 단축키 팝업 열기·닫기 (F11 파일 관리자)
set -- $(run "wait,f11,f11,f11,esc"); ne "F11 열기" $1 $2; chk "F11 다시 = 닫힘" $1 $3; chk "F11 세 번째 = 다시 열림" $2 $4; chk "Esc = 그냥 닫힘" $1 $5
# 믹서 F12 열기·닫기
set -- $(run "wait,f12,f12,esc"); ne "F12 열기" $1 $2; chk "F12 다시 = 닫힘" $1 $3
# 메뉴 → 팝업 → Esc 는 메뉴(펼친 채)로 → Esc → 한 단계 위 → … → 닫힘 (닫힌 화면끼리는 같음)
set -- $(run "wait,f10,right,right,down,enter,esc,esc,esc,esc"); ne "메뉴에서 팝업" $5 $6; chk "Esc = 메뉴(펼친 채)로" $5 $7; chk "다음 Esc = 한 단계 위" $4 $8; chk "끝까지 Esc = 닫힘" $9 ${10}; ne "닫힘 != 메뉴" $8 $9
# 메뉴 → 팝업 → F12 믹서 → F12 → 팝업으로 복귀 → Esc → 메뉴
set -- $(run "wait,f10,right,right,down,enter,f12,f12,esc"); ne "믹서가 열림" $6 $7; chk "믹서 닫으면 팝업으로" $6 $8; chk "Esc = 메뉴로" $5 $9
# 메뉴만 열고 F12 → 믹서 → Esc → 메뉴로
set -- $(run "wait,f10,right,right,f12,esc,esc"); ne "믹서가 열림" $4 $5; chk "믹서 Esc = 메뉴로" $4 $6; ne "그다음 Esc = 한 단계 위" $4 $7
# 팝업(F11) 위에 믹서 → Esc → 팝업으로
set -- $(run "wait,f11,f12,esc,esc"); chk "믹서 Esc = F11 팝업으로" $2 $4; ne "그다음 Esc = 닫힘" $2 $5
rm -rf $T
if [ $fail = 0 ]; then echo "popuptest: OK"; else echo "popuptest: FAIL"; exit 1; fi
