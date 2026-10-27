#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# mkprnfont.py : 인쇄 미리보기용 6x11 점 글꼴 (ASCII 0x20~0x7E) → kernel/prnfont.h
from PIL import Image, ImageDraw, ImageFont
f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 10)
rows = []
for c in range(0x20, 0x7F):
    im = Image.new('L', (6, 11), 0)
    d = ImageDraw.Draw(im)
    d.text((0, 0), chr(c), font=f, fill=255)
    g = []
    for y in range(11):
        v = 0
        for x in range(6):
            if im.getpixel((x, y)) >= 110: v |= 0x20 >> x
        g.append(v)
    rows.append(g)
out = ['/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */',
       '/* prnfont.h - 인쇄 미리보기용 6x11 점 글꼴 (DejaVu Sans Mono, tools/mkprnfont.py가 만듦). 한 줄 6비트, 왼쪽이 0x20 */',
       'static const uint8_t prn_font[95][11] = {']
for i, g in enumerate(rows):
    out.append('    {%s},  /* %s */' % (','.join('0x%02X' % v for v in g), repr(chr(0x20 + i))[1:-1].replace('*/', '* /')))
out.append('};')
open('/home/claude/msxemu/kernel/prnfont.h', 'w').write('\n'.join(out) + '\n')
print('ok')
