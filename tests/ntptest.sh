#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86NTP.COM 시험: 실제 unapi.rom + netcard + 가짜 호스트. 사용법: tests/ntptest.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build
gcc -O1 -o build/ntptest src/z80.c src/netcard.c tests/ntptest.c
pasmo tools/dos/x86ntp.asm build/X86NTP.COM
T=build/ntptest; C=build/X86NTP.COM; R=assets/net/unapi.rom
fail=0
chk() { # 명령줄 unix초 mode 기대글
    out=$($T $C $R "$1" $2 $3 | tr -d '\r'); case "$out" in *"$4"*) ;; *) echo "FAIL [$1] $2 mode$3: want '$4'"; echo "$out"; fail=1;; esac; }
chk " "            1793450096 0 "SET 2026-10-31 21:34:56"
chk " /Z-5"        1793450096 0 "SET 2026-10-31 07:34:56"
chk " /Z+5:30"     1793450096 0 "SET 2026-10-31 18:04:56"
chk " /Z+0"        1709164799 0 "SET 2024-02-28 23:59:59"
chk " /Z+0"        1709164800 0 "SET 2024-02-29 00:00:00"
chk " /Z+0"        951782400  0 "SET 2000-02-29 00:00:00"
chk " /Z+0"        978307199  0 "SET 2000-12-31 23:59:59"
chk " /T"          1793450096 0 "date=0 time=0"
chk " time.google.com" 1793450096 0 "dns=time.google.com"
chk " /Z+99"       1793450096 0 "Usage:"
chk " "            1793450096 1 "No reply from the time server."
chk " "            1793450096 2 "Network is not connected."
chk " "            1793450096 3 "DNS failed."
chk " "            1793450096 4 "No reply from the time server."
[ $fail = 0 ] && echo "ntptest: ok"
exit $fail
