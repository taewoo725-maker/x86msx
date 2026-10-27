#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# ZEXDOC/ZEXALL (Frank Cringle, GPL) 내려받기
set -e
cd "$(dirname "$0")"
base=https://raw.githubusercontent.com/anotherlin/z80emu/master/testfiles
for f in zexdoc.com zexall.com; do curl -sfL -o "$f" "$base/$f"; done
sha256sum zexdoc.com zexall.com
