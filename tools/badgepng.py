#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""badgepng.py - kernel/badges.c에 들어간 배지를 화면과 같은 색의 PNG로 내보낸다 (assets/badges)."""
import os, re
from PIL import Image
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COL = {'.': (0, 0, 0, 0), 'k': (0, 0, 0, 255), 'r': (255, 36, 36, 255), 'g': (182, 182, 182, 255), 'w': (255, 255, 255, 255)}
NAMES = ['MSX', 'MSX2', 'MSX2+', 'turboR']
src = open(os.path.join(ROOT, 'kernel', 'badges.c'), encoding='utf-8').read()
out = os.path.join(ROOT, 'assets', 'badges'); os.makedirs(out, exist_ok=True)
for m in re.finditer(r'b(\d+)_(\d)\[\d+\] = \{(.*?)\};', src, re.S):
    H, i = int(m.group(1)), int(m.group(2))
    rows = re.findall(r'"([.krgw]+)"', m.group(3))
    img = Image.new('RGBA', (len(rows[0]), H))
    for y, r in enumerate(rows):
        for x, c in enumerate(r): img.putpixel((x, y), COL[c])
    img.save(os.path.join(out, f'BADGE_{NAMES[i]}_{H}.png'))
print('wrote', out)
