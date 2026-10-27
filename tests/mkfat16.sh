#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# fattest용 FAT16 슈퍼플로피 이미지 (파티션 없음, 클러스터 4섹터, 하위 폴더, 한글 이름)
set -e
OUT=${1:-build/fat16.img}
export MTOOLS_SKIP_CHECK=1 LC_ALL=C.UTF-8
rm -f "$OUT"
mkfs.fat -C -F 16 -s 4 -n FAT16TEST "$OUT" 16384 >/dev/null
mmd -i "$OUT" ::/Games ::/Games/Konami
mcopy -i "$OUT" tests/carts/konami.rom "::/Games/Konami/Konami Test Game.rom"
mcopy -i "$OUT" tests/carts/konamiscc.rom "::/Games/Konami/Konami SCC.rom"
printf 'readme\r\n' > build/readme.txt
mcopy -i "$OUT" build/readme.txt ::/readme.txt
mcopy -i "$OUT" tests/carts/plain16.rom "::/한글이름.rom"
# 3.4.7: Windows 최대 길이 이름 (255자) — 한글 251자 + ".rom", 긴 영문 폴더 안 영문 255자
LK=$(python3 -c "print(('아주긴파일이름시험'*40)[:251]+'.rom')")
LD=$(python3 -c "print(('Very Long Folder Name '*12)[:200])")
LA=$(python3 -c "print(('A very long English file name for X86MSX test '*6)[:251]+'.rom')")
mcopy -i "$OUT" tests/carts/plain16.rom "::/LONGKO.ROM"
python3 tests/addlfn.py "$OUT" LONGKO.ROM "$LK"      # mtools는 여러 바이트 255자를 못 써서 직접
mmd -i "$OUT" "::/$LD"
mcopy -i "$OUT" tests/carts/konami.rom "::/$LD/$LA"
