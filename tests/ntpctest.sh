#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 호스트 SNTP 클라이언트(src/ntpc.inc) 시험
cd "$(dirname "$0")/.."; mkdir -p build
gcc -O1 -Wall -Wextra -o build/ntpctest tests/ntpctest.c && build/ntpctest
