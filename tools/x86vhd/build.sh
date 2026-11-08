#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86VHD 빌드: mingw-w64 가 있는 리눅스에서 `sh tools/x86vhd/build.sh` (저장소 맨 위에서). 결과: build/X86VHD.exe (64비트), build/X86VHD_32bit.exe
set -e
cd "$(dirname "$0")/../.."
mkdir -p build
FLAGS="-Wall -Wextra -Wno-format-truncation -Wno-unused-parameter -Wno-cast-function-type -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -municode -O2"
SRC="tools/x86vhd/x86vhd_ui.c tools/x86vhd/winhost.c tools/x86vhd/jobs.c tools/x86vhd/vhdplan.c kernel/vfat.c kernel/dskimg.c kernel/msxfat.c"
LIBS="-mwindows -static -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid"
x86_64-w64-mingw32-windres -I tools/x86vhd tools/x86vhd/x86vhd.rc -o build/x86vhd_res64.o
x86_64-w64-mingw32-gcc $FLAGS -o build/X86VHD.exe $SRC build/x86vhd_res64.o $LIBS
i686-w64-mingw32-windres -I tools/x86vhd tools/x86vhd/x86vhd.rc -o build/x86vhd_res32.o
i686-w64-mingw32-gcc $FLAGS -o build/X86VHD_32bit.exe $SRC build/x86vhd_res32.o $LIBS
echo "built build/X86VHD.exe build/X86VHD_32bit.exe"
