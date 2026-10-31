#!/bin/bash
# 만든이: KimTaeWoo
# 8.0.35 영상 녹화: 일꾼이 JPEG 을 압축한 AVI 가 메인이 혼자 압축한 AVI 와 바이트 단위로 같은지 (전체 화면·MSX 원본 크기 둘 다)
cd "$(dirname "$0")/.." || exit 1
ROM=${ROM:-/tmp/9f/9Fingers (NOP)(2024)[ASCII16].rom}
[ -f "$ROM" ] || { echo "vrectest: 시험 롬 없음 ($ROM) — 건너뜀"; exit 0; }
rm -rf build/vrec_root && mkdir -p build/vrec_root/Machines && cp -r machines/C-BIOS build/vrec_root/Machines/ && mkdir -p build/vrec_root/Video
fail=0
for mode in vrec vrecn; do
  for par in 0 1; do
    rm -f build/vrec_root/Video/*.avi
    unset FEPAR; [ $par = 1 ] && export FEPAR=1
    ROOT=build/vrec_root MACHINE=C-BIOS W=1280 H=800 KEYS=wait GAP=5 TAIL=900 FECART="$ROM" FECARTMAP=4 FECART_F=3 FEACT=$mode FEACT_F=60 FEACT2=$mode FEACT2_F=500 ./build/fehost >/dev/null 2>&1
    f=$(ls build/vrec_root/Video/*.avi 2>/dev/null | head -1)
    [ -n "$f" ] || { echo "vrectest: $mode par=$par AVI 가 안 만들어짐"; fail=1; continue; }
    cp "$f" build/vrec_$mode$par.avi
  done
  if cmp -s build/vrec_${mode}0.avi build/vrec_${mode}1.avi; then echo "vrectest ok: $mode (혼자 = 일꾼, $(stat -c %s build/vrec_${mode}0.avi) 바이트)"; else echo "vrectest FAIL: $mode 혼자 압축과 일꾼 압축이 다름"; fail=1; fi
done
[ $fail = 0 ] && echo "vrectest OK"
exit $fail
