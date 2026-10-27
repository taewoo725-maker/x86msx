#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkbadges.py - 기종 세대 배지(MSX / MSX 2 / MSX 2+ / MSX turbo R) → kernel/badges.c  (2.8.1)
의뢰인이 준 MSX2 로고(둥근 알약 모양, 왼쪽 검정 바탕에 흰 기울임 MSX, 오른쪽 회색 칸에 검정 숫자)를 본떠 그린다.
화면 색: . 투명, k 검정(1), r 빨강(8, 2.9), g 회색(14), w 흰색(15). 크기 두 가지(높이 20·32줄)."""
import os
from PIL import Image, ImageDraw, ImageFont
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
F_MSX = '/usr/share/fonts/opentype/inter/Inter-BlackItalic.otf'
F_NUM = '/usr/share/fonts/opentype/inter/Inter-BlackItalic.otf'
SIZES = (48,)                                      # 2.17: 48줄 하나만 두고 화면에서 줄여 그림
CARD = (22, 28, 41)                                # 2.17: 배지를 올리는 카드 색 (fe_ui C_CARD)                               # 2.9: 1.5배 (30·48), 좁은 화면용 20
GENS = [('MSX', None), ('MSX2', '2'), ('MSX2+', '2+'), ('turboR', 'turbo R')]

def text_box(d, txt, font):
    b = d.textbbox((0, 0), txt, font=font)
    return b, b[2] - b[0], b[3] - b[1]

def render(right, H, png_only=False):
    SS = 8
    h = H * SS
    rim = max(SS, int(h * 0.09))
    inner_h = h - 2 * rim
    fm = ImageFont.truetype(F_MSX, int(inner_h * 0.78))
    tmp = ImageDraw.Draw(Image.new('L', (10, 10)))
    _, mw, mh = text_box(tmp, 'MSX', fm)
    left_w = int(mw + inner_h * 0.75)                   # 검정 칸 (둥근 왼쪽 끝 포함)
    rw = 0
    if right:
        fn = ImageFont.truetype(F_NUM, int(inner_h * (0.80 if len(right) <= 2 else 0.56)))
        _, nw, nh = text_box(tmp, right, fn)
        rw = int(nw + inner_h * (0.55 if len(right) <= 2 else 0.6))
    w = rim * 2 + left_w + rw
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius=h // 2, fill=(170, 170, 170, 255))      # 은색 테
    d.rounded_rectangle((rim, rim, w - 1 - rim, h - 1 - rim), radius=inner_h // 2, fill=(255, 36, 36, 255))   # 2.9: 빨강 (의뢰인 선택)
    if right:                                           # 오른쪽 회색 칸 (둥근 오른쪽 끝)
        mask = Image.new('L', (w, h), 0)
        ImageDraw.Draw(mask).rounded_rectangle((rim, rim, w - 1 - rim, h - 1 - rim), radius=inner_h // 2, fill=255)
        part = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(part).rectangle((rim + left_w, rim, w, h), fill=(185, 185, 185, 255))
        img.paste(part, (0, 0), Image.composite(mask, Image.new('L', (w, h), 0), part.split()[3]))
    b, mw, mh = text_box(d, 'MSX', fm)
    d.text((rim + (left_w - mw) // 2 - b[0] + inner_h // 12, rim + (inner_h - mh) // 2 - b[1]), 'MSX', font=fm, fill=(255, 255, 255, 255))
    if right:
        b, nw, nh = text_box(d, right, fn)
        d.text((rim + left_w + (rw - nw) // 2 - b[0], rim + (inner_h - nh) // 2 - b[1]), right, font=fn, fill=(0, 0, 0, 255))
    small = img.resize((w // SS, H), Image.LANCZOS)
    if png_only: return small
    bg = Image.new('RGBA', small.size, CARD + (255,))         # 2.17: 카드 색 위에 부드럽게 섞음
    return Image.alpha_composite(bg, small).convert('RGB')

def rle(im):
    out = []
    for y in range(im.height):
        x = 0
        while x < im.width:
            c = im.getpixel((x, y)); n = 1
            while x + n < im.width and n < 255 and im.getpixel((x + n, y)) == c: n += 1
            out += [n, *c]; x += n
    return out

def main():
    out = os.path.join(ROOT, 'kernel', 'badges.c')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('/* badges.c - tools/mkbadges.py가 만든 기종 세대 배지 (2.17: 카드 색 위에 섞은 RGB, 줄마다 [개수 R G B] 묶음) */\n#include "badges.h"\n\n')
        dims = {}
        for H in SIZES:
            for i, (name, right) in enumerate(GENS):
                im = render(right, H); data = rle(im); dims[(H, i)] = im.width
                f.write(f'static const uint8_t b{H}_{i}[{len(data)}] = {{' + ','.join(map(str, data)) + '};\n')
        f.write('\nconst Badge badges[BADGE_SIZES][BADGE_GENS] = {\n')
        for H in SIZES:
            f.write('    { ' + ' '.join(f'{{ {H}, {dims[(H, i)]}, b{H}_{i} }},' for i in range(4)) + ' },\n')
        f.write('};\n')
    bdir = os.path.join(ROOT, 'assets', 'badges')                # 3.0: USB Icons 폴더용 PNG (높이 64, 투명 배경)
    os.makedirs(bdir, exist_ok=True)
    for f_ in os.listdir(bdir): os.remove(os.path.join(bdir, f_))
    for name, right in GENS:
        render(right, 64, True).save(os.path.join(bdir, f'BADGE_{name}.png'))
    print('wrote', out)
main()
