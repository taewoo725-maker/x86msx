#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - 5.2.120: 선택형 설정 시험 — 속성 창 6탭 + 조이스틱·마우스 창의 모든 줄을 키로 돌리고(좌우 바꾸기·풀다운 열기·고르기)
#   키마다 화면 해시와 마지막 ini 를 기준과 비교. 3개 언어(ko·en·ja). 선택형 설정(모니터·가로 비율·MIDI 출력 …)을 표로 옮기기 전후가 같은지 보는 용도.
#   기준 tests/data/seltest_expected.txt = 5.2.120 (팝업 크기 조정 뒤).   usage: bash tests/seltest.sh [update]
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d); OUT=$T/out.txt; : > $OUT
# 줄마다: 좌우로 바꾸기 / 풀다운 열고 그대로 고르기 / 풀다운 열고 한 칸 아래 고르기 (두 번째는 한 줄 어긋나게 시작해 토글 줄이 건너뛰는 것을 메움)
selkeys() { case $1 in A) p="right,left,right,down,";; B) p="enter,enter,down,";; C) p="enter,down,enter,down,";; D) p="down,enter,down,enter,";; E) p="enter,down,down,enter,down,";; esac; for r in $(seq 1 26); do echo -n "$p"; done; }
for lang in ko en ja; do
  for tab in 0 1 2 3 4 6 8 9 g; do
    for pass in A B C D E; do
      R=$T/root; rm -rf $R; mkdir -p $R/Machines; ln -s "$PWD/machines/C-BIOS" $R/Machines/C-BIOS
      printf '[State]\nMachine=MSX - C-BIOS\nLanguage=%s\n' $lang > $R/x86msx.ini
      KEYS="$(selkeys $pass)"; KEYS="${KEYS%,}"
      if [ $tab = g ]; then A1="gfx9000"; A2="props:1"; else A1="props:$tab"; A2="exit"; fi    # g = GFX9000 을 꽂은 비디오 탭 (화면 소스 줄)
      echo "== lang $lang tab $tab pass $pass" >> $OUT
      env ROOT=$R MACHINE=C-BIOS W=1280 H=800 FEACT=$A1 FEACT_F=5 FEACT2=$A2 FEACT2_F=$([ $tab = g ] && echo 30 || echo 100000) KEYS="$KEYS" GAP=10 HASHALL=1 START=60 TAIL=5 timeout 300 ${FEHOST_BIN:-./build/fehost} 2>&1 | grep "^key" | awk '{print $2, $4}' >> $OUT || true
      tr -d '\r' < $R/x86msx.ini 2>/dev/null | tr -d '\0' | grep "=" | grep -v "^;" | sort >> $OUT || true
    done
  done
done
if [ "$1" = update ]; then cp $OUT tests/data/seltest_expected.txt; echo "seltest: 기준 갱신 ($(grep -c '^key\|^[0-9]' $OUT) 줄)"; rm -rf $T; exit 0; fi
[ -n "$SELKEEP" ] && cp $OUT $SELKEEP
if cmp -s $OUT tests/data/seltest_expected.txt; then echo "seltest: OK ($(grep -c '^== ' $OUT) 창)"; rm -rf $T
else diff $OUT tests/data/seltest_expected.txt | head -20; echo "seltest: FAIL"; rm -rf $T; exit 1; fi
