#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - 5.2.127: 화면 겹치기·나란히 시험 — GFX9000 이 초록 화면을 내보낼 때 화면 소스를 바꿔 가며 찍은 그림의 점 색을 본다.
#   소스 VDP: 초록이 없음 / 겹치기: 초록과 MSX 가 반반 (순수 초록 아님·순수 MSX 색도 아님) / 나란히: 오른쪽 반 가운데가 순수 초록, 왼쪽 반 가운데는 MSX
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d); mkdir -p $T/Machines; ln -s "$PWD/machines/C-BIOS" $T/Machines/C-BIOS
printf '[State]\nMachine=MSX - C-BIOS\nLanguage=ko\n' > $T/x86msx.ini
shot() { rm -f $T/s_*; env ROOT=$T MACHINE=C-BIOS W=1280 H=800 FEACT=gfx9000 FEACT_F=5 FEACT2=v9show:$1 FEACT2_F=20 GAP=10 START=120 TAIL=5 SHOT=$T/s timeout 100 ${FEHOST_BIN:-./build/fehost} >/dev/null 2>&1 || true; ls $T/s_*.ppm | head -1; }
px() { python3 - "$1" "$2" "$3" <<'PY'
import sys
d=open(sys.argv[1],'rb').read(); i=0; tok=[]
while len(tok)<4:
    while d[i:i+1].isspace(): i+=1
    j=i
    while not d[j:j+1].isspace(): j+=1
    tok.append(d[i:j]); i=j
i+=1; w=int(tok[1]); x=int(sys.argv[2]); y=int(sys.argv[3]); o=i+(y*w+x)*3
print(d[o],d[o+1],d[o+2])
PY
}
fail=0
chk() { if [ "$2" != "$3" ]; then echo "FAIL $1: $2 (기대 $3)"; fail=1; fi; }
G="0 255 0"
f=$(shot 1)
chk "VDP 고정: 초록 없음" "$([ "$(px $f 900 400)" = "$G" ] && echo green || echo no)" no
f=$(shot 2); chk "V9990 고정: 가운데 = 초록" "$(px $f 640 400)" "$G"
f=$(shot 3); c=$(px $f 700 700); chk "겹치기: 반반 (순수 초록 아님)" "$([ "$c" = "$G" ] && echo green || echo mixed)" mixed
chk "겹치기: 초록 성분 반" "$(set -- $c; [ $2 -gt 100 ] && [ $2 -lt 200 ] && echo ok || echo bad)" ok
f=$(shot 4); chk "나란히: 오른쪽 반 가운데 = 초록" "$(px $f 960 400)" "$G"
chk "나란히: 왼쪽 반 가운데는 초록 아님" "$([ "$(px $f 320 400)" = "$G" ] && echo green || echo no)" no
rm -rf $T
[ $fail = 0 ] && echo "comptest: OK" || { echo "comptest: FAIL"; exit 1; }
