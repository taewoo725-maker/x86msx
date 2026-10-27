#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkromdb.py - 3.2.15: openMSX softwaredb.xml(xml-msxromsdb.zip 안) → X86MSX 롬 DB 이진 파일 (Databases\\x86msx_romdb.bin)

머리 "X86ROMDB" + 항목 수(4바이트, 리틀엔디언) + 항목(22바이트: SHA-1 20 + 종류 1 + 시작 페이지 1), SHA-1 순서로 정렬.
종류: 1 일반(시작 페이지 0~3, 0xFF = 자동), 2 Konami, 3 Konami SCC, 4 ASCII8, 5 ASCII16, 6 ASCII8+SRAM(둘째 바이트 = SRAM KB 2·8·32), 7 ASCII16+SRAM,
      8 R-Type, 9 Harry Fox, 10 Zemina 80 in 1, 11 Zemina 90 in 1, 12 Zemina 126 in 1, 13 Cross Blaim, 14 Super Lode Runner, 15 Al Quran(풀린 덤프), 16 Yamanooto(5.0.56). 그 밖(멀티카트 등)은 넣지 않음. MSX 계열(system="MSX…")만.
쓰기: mkromdb.py <xml-msxromsdb.zip 또는 softwaredb.xml> <출력 .bin>
"""
import re, struct, sys, zipfile

TYPES = {
    'Mirrored': (1, 0xFF), 'Mirrored4000': (1, 0xFF), 'Normal': (1, 0xFF), '8kB': (1, 0xFF), '16kb': (1, 0xFF),
    'Page12': (1, 1), 'Page2': (1, 2), 'Page23': (1, 2), '0x0000': (1, 0), '0x4000': (1, 1), '0x8000': (1, 2),
    'Konami': (2, 0), 'KonamiSCC': (3, 0), 'ASCII8': (4, 0), 'ASCII16': (5, 0), 'ASCII16-X': (5, 0),
    'ASCII8SRAM2': (6, 2), 'ASCII8SRAM8': (6, 8), 'ASCII8SRAM32': (6, 32), 'KoeiSRAM8': (6, 8), 'KoeiSRAM32': (6, 32),   # 3.8.0: 종류 6의 둘째 바이트 = SRAM KB
    'ASCII16SRAM2': (7, 0), 'ASCII16SRAM8': (7, 0), 'R-Type': (8, 0), 'HarryFox': (9, 0),
    'Zemina80in1': (10, 0), 'Zemina90in1': (11, 0), 'Zemina126in1': (12, 0),
    'CrossBlaim': (13, 0), 'SuperLodeRunner': (14, 0), 'AlQuranDecoded': (15, 0), 'YAMANOOTO': (16, 0),   # Yamanooto 플래시용   # 3.8.0
}

def main():
    src, out = sys.argv[1], sys.argv[2]
    if src.endswith('.zip'):
        with zipfile.ZipFile(src) as z:
            text = z.read('softwaredb.xml').decode('utf-8', 'replace')
    else:
        text = open(src, encoding='utf-8', errors='replace').read()
    ents = {}
    for sw in re.finditer(r'<software\b([^>]*)>(.*?)</software>', text, re.S):
        sysm = re.search(r'system="([^"]*)"', sw.group(1))
        if not sysm or not sysm.group(1).startswith('MSX'):
            continue
        for rom in re.finditer(r'<rom\b([^>]*)/?>', sw.group(2)):
            a = rom.group(1)
            h = re.search(r'sha1="([0-9a-fA-F]{40})"', a)
            t = re.search(r'type="([^"]*)"', a)
            if not h or not t or t.group(1) not in TYPES:
                continue
            ents[bytes.fromhex(h.group(1).lower())] = TYPES[t.group(1)]
    data = bytearray(b'X86ROMDB' + struct.pack('<I', len(ents)))
    for k in sorted(ents):
        data += k + bytes(ents[k])
    open(out, 'wb').write(data)
    print('mkromdb: %d entries -> %s' % (len(ents), out))

if __name__ == '__main__':
    main()
