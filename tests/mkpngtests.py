#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkpngtests.py - PNG 해독 시험용 그림을 build/pngt에 만든다 (RGBA, RGB, 흑백, 1비트, 팔레트, 64x64)."""
import os
from PIL import Image
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(ROOT, 'build', 'pngt'); os.makedirs(out, exist_ok=True)
src = Image.open(os.path.join(ROOT, 'assets', 'icons', 'SCC.png')).convert('RGBA').resize((16, 16), Image.NEAREST)   # 3.0: 48x48 그림을 16x16 모양으로
on = [(x, y) for y in range(16) for x in range(16) if src.getpixel((x, y))[3] >= 128]
open(os.path.join(out, 'SCC.txt'), 'w').write('\n'.join(''.join('#' if (x, y) in on else '.' for x in range(16)) for y in range(16)) + '\n')
def save(mode, name, big=1):
    bg = {'RGBA': (0, 0, 0, 0), 'RGB': (255, 255, 255), 'L': 255, '1': 1}[mode]
    fg = {'RGBA': (0, 0, 0, 255), 'RGB': (0, 0, 0), 'L': 0, '1': 0}[mode]
    im = Image.new(mode, (16 * big, 16 * big), bg)
    for x, y in on:
        for dx in range(big):
            for dy in range(big): im.putpixel((x * big + dx, y * big + dy), fg)
    if name == 'pal': im = im.convert('P'); im.info['transparency'] = 0
    im.save(os.path.join(out, name + '.png'))
for m, n, b in (('RGBA', 'rgba', 1), ('RGB', 'rgb', 1), ('L', 'gray', 1), ('1', 'bit1', 1), ('RGBA', 'big', 4), ('RGBA', 'pal', 1)):
    save(m, n, b)
t = os.path.join(out, 'SCC.txt')
open(os.path.join(out, 'args.txt'), 'w').write(' '.join('%s %s' % (os.path.join(out, n + '.png'), t) for n in ('rgba', 'rgb', 'gray', 'bit1', 'big', 'pal')))
