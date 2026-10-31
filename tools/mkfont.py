#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkfont.py - Galmuri BDF(SIL OFL)에서 화면 글꼴 C 배열 만들기 → kernel/font_data.c

글자 집합: ASCII, Latin-1, 한글 자모, KS X 1001 한글 2,350자, 가나, CJK 기호, 화살표·도형,
          그리고 kernel/lang.h에 쓰인 모든 글자(메뉴의 일본어 한자 등).
글자 하나 = 셀 높이(ascent+descent)만큼의 줄, 줄마다 (폭+7)/8 바이트 (최상위 비트가 왼쪽).
"""
import re, sys, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def charset():
    cs = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
    cs |= set(range(0x3131, 0x318F))                       # 한글 호환 자모
    for cp in range(0xAC00, 0xD7A4):                        # KS X 1001 한글 2,350자
        try:
            if len(chr(cp).encode('euc_kr')) == 2:      # 2,350자 밖은 8바이트 조합형으로 바뀜
                cs.add(cp)
        except UnicodeEncodeError:
            pass
    cs |= set(range(0x3000, 0x3040)) | set(range(0x3040, 0x3100))   # CJK 기호, 가나
    cs |= set(map(ord, '←↑→↓▶◀▲▼●○■□◆◇★☆✓×÷·…‥“”‘’〜～！？（）：，．‹›»«↔₩'))
    lang = open(os.path.join(ROOT, 'kernel', 'lang.h'), encoding='utf-8').read() + open(os.path.join(ROOT, 'kernel', 'lang2.h'), encoding='utf-8').read()
    ar = os.path.join(ROOT, 'kernel', 'lang_ar.h')               # 3.4.3 LANG3-01 아랍어 (표현형)
    if os.path.exists(ar):
        lang += open(ar, encoding='utf-8').read()
    for s in re.findall(r'"((?:[^"\\]|\\.)*)"', lang):
        cs |= set(map(ord, s))
    return sorted(c for c in cs if c >= 0x20)

def load_bdf(path):
    t = open(path, encoding='latin-1').read()
    asc = int(re.search(r'FONT_ASCENT (\d+)', t).group(1))
    desc = int(re.search(r'FONT_DESCENT (\d+)', t).group(1))
    glyphs = {}
    for m in re.finditer(r'STARTCHAR.*?ENCODING (-?\d+).*?DWIDTH (\d+) \d+.*?BBX (-?\d+) (-?\d+) (-?\d+) (-?\d+).*?BITMAP\n(.*?)ENDCHAR', t, re.S):
        cp, dx, w, h, xo, yo = (int(m.group(i)) for i in range(1, 7))
        rows = m.group(7).split()
        glyphs[cp] = (dx, w, h, xo, yo, [int(r, 16) for r in rows], len(rows[0]) * 4 if rows else 0)
    return asc, desc, glyphs

def render(asc, desc, g):
    dx, w, h, xo, yo, rows, bits = g
    H = asc + desc
    width = max(dx, xo + w, 1)
    bpr = (width + 7) // 8
    out = [0] * (H * bpr)
    top = asc - (yo + h)                                    # 셀에서 글자 윗줄 위치
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

def is_arabic(cp):
    return 0x0600 <= cp <= 0x06FF or 0xFB50 <= cp <= 0xFDFF or 0xFE70 <= cp <= 0xFEFF

def render_ttf(asc, desc, cp, size):
    """3.4.3: Galmuri에 없는 아랍어 글자는 DejaVu Sans(third_party/dejavu)에서 셀 높이에 맞춰 그려 1비트로"""
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype(os.path.join(ROOT, 'third_party', 'dejavu', 'DejaVuSans.ttf'), size)
    ch = chr(cp)
    dx = max(1, int(round(font.getlength(ch))))
    H = asc + desc
    img = Image.new('L', (dx + 4, H), 0)
    ImageDraw.Draw(img).text((0, asc), ch, font=font, fill=255, anchor='ls')
    width = dx
    bpr = (width + 7) // 8
    out = [0] * (H * bpr)
    for y in range(H):
        for x in range(width):
            if img.getpixel((x, y)) >= 110:
                out[y * bpr + x // 8] |= 0x80 >> (x % 8)
    return dx, bpr, out

def emit(name, path, cs, f):
    asc, desc, glyphs = load_bdf(path)
    H = asc + desc
    bits, table, missing = [], [], 0
    for cp in cs:
        g = glyphs.get(cp)
        if not g and is_arabic(cp):
            dx, bpr, data = render_ttf(asc, desc, cp, asc - 1)
            table.append((cp, len(bits), dx, bpr))
            bits += data
            continue
        if not g:
            missing += 1
            if os.environ.get("MISS"): print("missing", hex(cp), chr(cp))
            continue
        dx, bpr, data = render(asc, desc, g)
        table.append((cp, len(bits), dx, bpr))
        bits += data
    f.write(f'static const uint8_t {name}_bits[{len(bits)}] = {{\n')
    for i in range(0, len(bits), 24):
        f.write('    ' + ','.join(str(b) for b in bits[i:i + 24]) + ',\n')
    f.write('};\n')
    f.write(f'static const FontGlyph {name}_glyphs[{len(table)}] = {{\n')
    for cp, off, dx, bpr in table:
        f.write(f'    {{0x{cp:05X}, {off}, {dx}, {bpr}}},\n')
    f.write('};\n')
    f.write(f'UiFont {name} = {{ {H}, {asc}, {len(table)}, {name}_glyphs, {name}_bits, 0 }};\n\n')
    return len(table), len(bits), missing

NOTO = '/usr/share/fonts/opentype/noto/NotoSansCJK-%s.ttc'
WS = 1.10                                                  # 가로 배율 (한글 칸 폭이 예전 점글꼴과 같아지게)
HS = 0.93                                                   # 세로 배율 (너무 네모나 보이지 않게 조금 낮춤)
GAMMA = 0.60                                               # 어두운 바탕에 밝은 글씨: 획이 가늘어 보이지 않게 알파를 조금 올림

# 글꼴 얼굴: (파일, 얼굴 번호, 가로 배율)
FACES = {'noto': (NOTO, 1, WS), 'gothic': (os.path.join(ROOT, 'third_party', 'gothic', '%s.ttf'), 0, 1.0), 'happy': (os.path.join(ROOT, 'third_party', 'happy', '%s.ttf'), 0, WS)}

def emit_aa(name, weight, size, bdf, cs, f, face='noto', scale=1):
    """부드러운 글꼴: Noto Sans CJK KR(SIL OFL)을 줄 높이·기준선은 Galmuri와 같게 그려 4비트 알파로. 없는 글자는 Galmuri를 알파 15로"""
    from PIL import Image, ImageDraw, ImageFont
    from fontTools.ttLib import TTCollection
    fpath, fidx, WS = FACES[face]
    path = fpath % weight
    if path.endswith('.ttc'): cmap = TTCollection(path).fonts[fidx].getBestCmap()
    else:
        from fontTools.ttLib import TTFont
        cmap = TTFont(path).getBestCmap()
    size *= scale                                             # 5.3.34: scale 2 = 1080p 용 두 배 크기 글꼴 (줄 높이·기준선도 두 배)
    font = ImageFont.truetype(path, size, index=fidx)
    font4 = ImageFont.truetype(path, size * 4, index=fidx)
    asc, desc, glyphs = load_bdf(bdf)
    asc *= scale; desc *= scale
    H = asc + desc
    bits, table, missing = [], [], 0
    for cp in cs:
        data = None
        if (cp in cmap and cp != 0xB7) or cp in (0x20, 0xA0):
            ch = chr(cp)
            adv = max(1, int(round(font.getlength(ch) * (WS if cp >= 0x2E80 else 1.0))))
            nat = font.getlength(ch)                          # 실제 크기로 바로 그림 (4배 줄이기·가로세로 배율 없음 → 번지지 않음). 칸 폭만 넓혀 글자를 가운데에
            off = int(round((adv - nat) / 2.0)) if adv > nat else 0
            img = Image.new('L', (adv + 8, H), 0)
            ImageDraw.Draw(img).text((off, asc), ch, font=font, fill=255, anchor='ls')
            img = img.crop((0, 0, adv, H))
            px = img.load()
            bpr = (adv + 1) // 2
            data = [0] * (H * bpr)
            for y in range(H):
                for x in range(adv):
                    a = int(round(((px[x, y] / 255.0) ** GAMMA) * 15))
                    data[y * bpr + x // 2] |= a << (4 if x % 2 == 0 else 0)
        elif scale == 1 and cp in glyphs:                    # Noto에 없는 글자: Galmuri 점글꼴을 그대로
            dx, bpr1, d1 = render(asc, desc, glyphs[cp])
            adv = dx
            bpr = (adv + 1) // 2
            data = [0] * (H * bpr)
            for y in range(H):
                for x in range(adv):
                    if d1[y * bpr1 + x // 8] & (0x80 >> (x % 8)):
                        data[y * bpr + x // 2] |= 15 << (4 if x % 2 == 0 else 0)
        elif scale == 1 and is_arabic(cp):
            dx, bpr1, d1 = render_ttf(asc, desc, cp, asc - 1)
            adv = dx
            bpr = (adv + 1) // 2
            data = [0] * (H * bpr)
            for y in range(H):
                for x in range(adv):
                    if d1[y * bpr1 + x // 8] & (0x80 >> (x % 8)):
                        data[y * bpr + x // 2] |= 15 << (4 if x % 2 == 0 else 0)
        if data is None:
            missing += 1
            continue
        table.append((cp, len(bits), adv, bpr))
        bits += data
    f.write(f'static const uint8_t {name}_bits[{len(bits)}] = {{\n')
    for i in range(0, len(bits), 24):
        f.write('    ' + ','.join(str(b) for b in bits[i:i + 24]) + ',\n')
    f.write('};\n')
    f.write(f'static const FontGlyph {name}_glyphs[{len(table)}] = {{\n')
    for cp, off, dx, bpr in table:
        f.write(f'    {{0x{cp:05X}, {off}, {dx}, {bpr}}},\n')
    f.write('};\n')
    f.write(f'UiFont {name} = {{ {H}, {asc}, {len(table)}, {name}_glyphs, {name}_bits, 1 }};\n\n')
    return len(table), len(bits), missing

X8F_FONTS = (('font_small', 'Regular', 12, 'Galmuri11.bdf'), ('font_large', 'Regular', 14, 'Galmuri14.bdf'), ('font_bold', 'Bold', 17, 'Galmuri14.bdf'), ('font_tiny', 'Regular', 11, 'Galmuri11.bdf'), ('font_btn', 'Regular', 13, 'Galmuri11.bdf'), ('font_head', 'Bold', 16, 'Galmuri14.bdf'), ('font_title', 'Bold', 20, 'Galmuri14.bdf'), ('font_mini', 'Regular', 9, 'Galmuri11.bdf'))   # 5.3.13: 9px (USB 확장 화면 작은 글)

def collect_aa(weight, size, bdf, cs, face='noto', scale=1):
    """emit_aa 와 같은 그림을 (표, 비트) 로 돌려줌"""
    import io
    buf = io.StringIO()
    emit_aa('tmp', weight, size, bdf, cs, buf, face, scale)
    return buf.getvalue()

def hd_charset(cs):
    """5.3.34 1080p 두 배 글꼴에 넣을 글자: 메뉴 글(lang.h)에 쓰인 글자 + 영문·라틴·자모. 한자·아랍 글자는 뺌(옛 방식으로 그려짐)"""
    lang = open(os.path.join(ROOT, 'kernel', 'lang.h'), encoding='utf-8').read() + open(os.path.join(ROOT, 'kernel', 'lang2.h'), encoding='utf-8').read()
    used = set()
    for t in re.findall(r'"((?:[^"\\]|\\.)*)"', lang): used |= set(map(ord, t))
    keep = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100)) | set(range(0x3131, 0x318F)) | set(range(0x3040, 0x3100))
    keep |= {c for c in used if 0xAC00 <= c <= 0xD7A3 or 0x2190 <= c <= 0x25FF or 0x3000 <= c < 0x3040 or c in (0x2026, 0x201C, 0x201D, 0x2018, 0x2019, 0x2713, 0xD7)}
    return sorted(c for c in keep if c in cs)

def write_x8f(cs, path, face='noto'):
    """fonts/NotoSansKR.x8f - 부팅 때 USB 의 fonts 폴더에서 읽는 부드러운 글꼴 (한 파일에 작은·큰·굵은 세 가지)
    머리: "X8F1", 글꼴 수(u32), 글꼴마다 [높이, 기준선, 글자 수, 알파 여부, 표 위치, 비트 위치, 비트 길이] (u32 7개)
    표: 글자마다 12바이트 {cp u32, off u32, adv u8, bpr u8, pad 2}. 모든 위치는 4의 배수"""
    import struct, re
    blobs = []
    for name, weight, size, bdf in X8F_FONTS:
        txt = collect_aa(weight, size, os.path.join(ROOT, 'third_party', 'galmuri', bdf), cs, face)
        bits = bytes(int(x) for x in re.search(r'_bits\[\d+\] = \{(.*?)\};', txt, re.S).group(1).replace('\n', '').split(',') if x.strip())
        tab = [tuple(int(v, 16) if v.startswith('0x') else int(v) for v in m) for m in re.findall(r'\{(0x[0-9A-F]+), (\d+), (\d+), (\d+)\}', txt)]
        h = re.search(r'UiFont tmp = \{ (\d+), (\d+), (\d+),', txt)
        blobs.append((int(h.group(1)), int(h.group(2)), tab, bits))
    txt = collect_aa('Regular', 12, os.path.join(ROOT, 'third_party', 'galmuri', 'Galmuri11.bdf'), hd_charset(cs), face, 2)   # 아홉째 = 작은 글꼴의 두 배 크기 (1080p)
    bits = bytes(int(x) for x in re.search(r'_bits\[\d+\] = \{(.*?)\};', txt, re.S).group(1).replace('\n', '').split(',') if x.strip())
    tab = [tuple(int(v, 16) if v.startswith('0x') else int(v) for v in m) for m in re.findall(r'\{(0x[0-9A-F]+), (\d+), (\d+), (\d+)\}', txt)]
    h = re.search(r'UiFont tmp = \{ (\d+), (\d+), (\d+),', txt)
    blobs.append((int(h.group(1)), int(h.group(2)), tab, bits))
    head = 8 + 28 * len(blobs)
    off = (head + 3) & ~3
    parts, metas = [], []
    for H, asc, tab, bits in blobs:
        tb = b''.join(struct.pack('<IIBBH', cp, o, adv, bpr, 0) for cp, o, adv, bpr in tab)
        t_off = off; off += len(tb); b_off = off; off += (len(bits) + 3) & ~3
        metas.append((H, asc, len(tab), 1, t_off, b_off, len(bits)))
        parts.append((tb, bits))
    out = bytearray(b'X8F1' + struct.pack('<I', len(blobs)))
    for m in metas: out += struct.pack('<7I', *m)
    out += b'\0' * (((len(out) + 3) & ~3) - len(out))
    for tb, bits in parts:
        out += tb + bits + b'\0' * (((len(bits) + 3) & ~3) - len(bits))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(out)
    print(f'{path}: {len(out)} bytes')

def main():
    cs = charset()
    out = os.path.join(ROOT, 'kernel', 'font_data.c')
    with open(out, 'w') as f:
        f.write('/* font_data.c - tools/mkfont.py가 만든 파일. 고치지 말 것.\n'
                ' * 내장 글꼴(파일 글꼴이 없을 때): Galmuri (c) Lee Minseo, SIL Open Font License 1.1 (third_party/galmuri/LICENSE.txt)\n'
                ' * 아랍어 글자: DejaVu Sans (Bitstream Vera·Arev 라이선스, third_party/dejavu/LICENSE.txt) */\n'
                '#include "font.h"\n\n')
        for name, file in (('font_small', 'Galmuri11.bdf'), ('font_large', 'Galmuri14.bdf')):
            n, nb, miss = emit(name, os.path.join(ROOT, 'third_party', 'galmuri', file), cs, f)
            print(f'{name}: {n} glyphs, {nb} bytes, {miss} missing of {len(cs)}')
        f.write('UiFont font_btn = { 16, 14, 0, 0, 0, 0 };\n        UiFont font_head = { 21, 18, 0, 0, 0, 0 };\n        UiFont font_title = { 21, 18, 0, 0, 0, 0 };\n        UiFont font_tiny = { 16, 14, 0, 0, 0, 0 };      /* 줄 높이는 작은 글꼴과 같고 글자만 더 작음: 내장은 fe.c 가 작은 글꼴로 채움 */\n        UiFont font_bold = { 21, 18, 0, 0, 0, 0 };     /* 내장 글꼴에는 굵은 글꼴이 없음: fe.c 가 큰 글꼴로 채움 */\n        UiFont font_mini = { 16, 14, 0, 0, 0, 0 };     /* 9px: 내장은 작은 글꼴로 채움 */\n')
    if '--x8f' in sys.argv or True:
        write_x8f(cs, os.path.join(ROOT, 'third_party', 'noto', 'NotoSansKR.x8f'))
        write_x8f(cs, os.path.join(ROOT, 'third_party', 'gothic', 'GothicKR.x8f'), 'gothic')
        write_x8f(cs, os.path.join(ROOT, 'third_party', 'happy', 'HappyKR.x8f'), 'happy')

main()
