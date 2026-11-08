#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mktextfont.py - X86 Multimedia 글자 그리기(30h TEXT)용 자료 만들기

  kernel/font_t9.c     Galmuri9 (SIL OFL, third_party/galmuri/Galmuri9.bdf) 의 BMP 전체 글자를 1비트 비트맵으로.
                       한 글자 = 10줄 (글자 윗선 위 9줄 + 기준선 아래 1줄: 영문 g·p·y 꼬리).
                       한글 11,172자 + 자모 + 한자 + 가나 + 영문·기호 모두.
  kernel/cp949_tab.c   CP949(UHC) 두 바이트 → 유니코드 표 (앞바이트 81..FE × 뒷바이트 41..FE)

Galmuri9 가 없는 글자는 표에 안 넣는다 (그리기 쪽이 ? 로 대신).
"""
import re, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASC, H = 9, 10                                    # 기준선 위 9줄 + 아래 1줄

def load_bdf(path):
    t = open(path, encoding='latin-1').read()
    glyphs = {}
    for m in re.finditer(r'STARTCHAR.*?ENCODING (-?\d+).*?DWIDTH (\d+) \d+.*?BBX (-?\d+) (-?\d+) (-?\d+) (-?\d+).*?BITMAP\n(.*?)ENDCHAR', t, re.S):
        cp, dx, w, h, xo, yo = (int(m.group(i)) for i in range(1, 7))
        if cp < 0x20 or cp > 0xFFFF or 0xD800 <= cp < 0xF900 or 0xE000 <= cp < 0xF900:
            continue
        rows = m.group(7).split()
        glyphs[cp] = (dx, w, h, xo, yo, [int(r, 16) for r in rows], len(rows[0]) * 4 if rows else 0)
    return glyphs

def render(g):
    dx, w, h, xo, yo, rows, bits = g
    width = max(dx, xo + w, 1)
    bpr = (width + 7) // 8
    out = [0] * (H * bpr)
    top = ASC - (yo + h)
    for r in range(h):
        y = top + r
        if y < 0 or y >= H:
            continue
        for x in range(w):
            if rows[r] >> (bits - 1 - x) & 1:
                px = xo + x
                if 0 <= px < width:
                    out[y * bpr + px // 8] |= 0x80 >> (px % 8)
    return dx, bpr, out

def main():
    glyphs = load_bdf(os.path.join(ROOT, 'third_party', 'galmuri', 'Galmuri9.bdf'))
    bits, table = [], []
    for cp in sorted(glyphs):
        dx, bpr, data = render(glyphs[cp])
        table.append((cp, len(bits), dx, bpr))
        bits += data
    with open(os.path.join(ROOT, 'kernel', 'font_t9.c'), 'w') as f:
        f.write('/* font_t9.c - tools/mktextfont.py가 만든 파일. 고치지 말 것.\n'
                ' * Galmuri9 (c) Lee Minseo, SIL Open Font License 1.1 (third_party/galmuri/LICENSE.txt) */\n'
                '#include "font.h"\n\n')
        f.write(f'static const uint8_t font_t9_bits[{len(bits)}] = {{\n')
        for i in range(0, len(bits), 32):
            f.write('    ' + ','.join(str(b) for b in bits[i:i + 32]) + ',\n')
        f.write('};\n')
        f.write(f'static const FontGlyph font_t9_glyphs[{len(table)}] = {{\n')
        for cp, off, dx, bpr in table:
            f.write(f'    {{0x{cp:05X}, {off}, {dx}, {bpr}}},\n')
        f.write('};\n')
        f.write(f'UiFont font_t9 = {{ {H}, {ASC}, {len(table)}, font_t9_glyphs, font_t9_bits, 0, 0 }};\n')
    print(f'font_t9.c: {len(table)} glyphs, {len(bits)} bytes')
    tab = [0] * (126 * 190)
    n = 0
    for lead in range(0x81, 0xFF):
        for trail in range(0x41, 0xFF):
            try:
                u = bytes([lead, trail]).decode('cp949')
            except UnicodeDecodeError:
                continue
            if len(u) == 1 and ord(u) <= 0xFFFF:
                tab[(lead - 0x81) * 190 + (trail - 0x41)] = ord(u); n += 1
    with open(os.path.join(ROOT, 'kernel', 'cp949_tab.c'), 'w') as f:
        f.write('/* cp949_tab.c - tools/mktextfont.py가 만든 파일. 고치지 말 것. CP949(UHC) 두 바이트 -> 유니코드 (0 = 없음). 칸 = (앞-0x81)*190 + (뒤-0x41) */\n'
                '#include <stdint.h>\n\n')
        f.write('const uint16_t cp949_tab[126 * 190] = {\n')
        for i in range(0, len(tab), 16):
            f.write('    ' + ','.join(str(v) for v in tab[i:i + 16]) + ',\n')
        f.write('};\n')
    print(f'cp949_tab.c: {n} entries')

main()
