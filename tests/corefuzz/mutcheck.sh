#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
# mutcheck.sh - corefuzz 가 정말 고장을 잡는지: 현재 코어(src/z80.c)를 일부러 망가뜨린 사본을 만들어 돌려 봄 (모두 "다름!" 이어야 통과)
#   usage: tests/corefuzz/mutcheck.sh [시드 수 (기본 60)]
cd "$(dirname "$0")/../.."
SEEDS=${1:-60}
T=$(mktemp -d); trap 'rm -rf $T' EXIT
declare -a MUT=(
 "daa 의 H 플래그|s/h = ((a \& 0x0F) > 9) ? FH : 0;/h = ((a \& 0x0F) > 8) ? FH : 0;/"
 "BIT n,(HL) 의 X/Y 출처(WZ)|s/bit(z, y, v, z->wz.b.h); return 12;/bit(z, y, v, z->hl.b.h); return 12;/"
 "인터럽트 수락 조건 (EI 지연 무시)|s/if (z->int_line \&\& z->iff1 \&\& !z->ei_delay)/if (z->int_line \&\& z->iff1)/"
 "R800 MULUW 시간|s/z->r8c += 34;/z->r8c += 33;/"
 "R800 MULUB 시간|s/z->r8c += 12;/z->r8c += 13;/"
 "EI 가 EI 지연을 안 켬|s/z->iff1 = z->iff2 = 1; z->ei_delay = 1; break;/z->iff1 = z->iff2 = 1; break;/"
)
ok=0; bad=0
for m in "${MUT[@]}"; do
  name=${m%%|*}; sedx=${m#*|}
  mkdir -p $T/s; cp src/z80.c src/z80.h $T/s/
  sed -i "$sedx" $T/s/z80.c
  if cmp -s src/z80.c $T/s/z80.c; then echo "건너뜀(코어가 바뀌어 줄이 없음): $name"; bad=$((bad+1)); continue; fi
  R="-Dz80_init=z80r_init -Dz80_reset=z80r_reset -Dz80_step=z80r_step"
  cc -O2 -std=c11 -w -I$T/s -DCF_PFX=cur_ -DCF_HDR='"z80.h"' -c tests/corefuzz/cf_wrap.c -o $T/c1.o &&
  cc -O2 -std=c11 -w -Itests/ref $R -DCF_REF -DCF_PFX=ref_ -DCF_HDR='"z80_ref.h"' -c tests/corefuzz/cf_wrap.c -o $T/c2.o &&
  cc -O2 -std=c11 -w -I$T/s -c $T/s/z80.c -o $T/c3.o &&
  cc -O2 -std=c11 -w -Itests/ref $R -c tests/ref/z80_ref.c -o $T/c4.o &&
  cc -O2 -std=c11 -w -Itests/corefuzz -o $T/cf tests/corefuzz/corefuzz.c $T/c1.o $T/c2.o $T/c3.o $T/c4.o || { echo "빌드 실패: $name"; bad=$((bad+1)); continue; }
  if $T/cf $SEEDS 20000 >$T/out 2>&1; then echo "못 잡음!  $name"; bad=$((bad+1)); else echo "잡음      $name  ($(grep -m1 '다름' $T/out | cut -c1-60))"; ok=$((ok+1)); fi
done
echo "mutcheck: 잡음 $ok / ${#MUT[@]}"
[ $bad -eq 0 ]
