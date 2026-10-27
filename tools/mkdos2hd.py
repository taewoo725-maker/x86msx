#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkdos2hd.py out.hdd total_mb spc_max label... : MSX-DOS2용 하드 디스크 이미지 (MBR + FAT12 파티션 여러 개, 각 32 MB 이하)
부트 섹터는 X86MSX 3.4.4 이미지 도구와 같은 MSX-DOS2 모양 (EB FE 90, 1Eh JR 3Eh, 20h VOL_ID)"""
import sys, struct
out, mb = sys.argv[1], int(sys.argv[2])
rev = len(sys.argv) > 3 and sys.argv[3] == 'rev'   # Sunrise IDE: 표의 뒤 칸부터 A:
fatmax = 12
total = mb * 2048
parts, start = [], 1
while start < total:
    n = min(65535, total - start)
    parts.append((start, n)); start += n
assert len(parts) <= 4
img = bytearray(total * 512)
for i, (s, n) in enumerate(parts):
    e = 446 + 16 * ((len(parts) - 1 - i) if rev else i)
    img[e:e+16] = bytes([0, 0xFE, 0xFF, 0xFF, 0x01, 0xFE, 0xFF, 0xFF]) + struct.pack('<II', s, n)
img[510:512] = b'\x55\xaa'
for i, (s, n) in enumerate(parts):
    root, rootsec = 512, 32
    for spc in (1, 2, 4, 8, 16, 32, 64):
        c = n // spc
        fs = ((c * 3 + 1) // 2 + 3 + 511) // 512
        cl = (n - 1 - 2 * fs - rootsec) // spc
        if cl < 4085 and fs <= fatmax: break
    b = bytearray(512)
    b[0:3] = b'\xEB\xFE\x90'; b[3:11] = b'MSXDOS20'
    struct.pack_into('<HBHBHHBHHHH', b, 0x0B, 512, spc, 1, 2, root, n, 0xF8, fs, 32, 16, s & 0xFFFF)
    b[0x1E:0x20] = b'\x18\x1E'; b[0x20:0x26] = b'VOL_ID'; b[0x26] = 0
    struct.pack_into('<I', b, 0x27, 0x58384D53 + i); b[0x3E] = 0xC9; b[510:512] = b'\x55\xaa'
    o = s * 512
    img[o:o+512] = b
    for k in range(2):
        f = o + (1 + k * fs) * 512
        img[f:f+3] = b'\xF8\xFF\xFF'
    lab = (['MSXDOS2 HD', 'DATA'][min(i, 1)] + ' ' * 11)[:11].encode()
    r = o + (1 + 2 * fs) * 512
    img[r:r+11] = lab; img[r+11] = 0x08
    print('part %d: start %d, %d sectors (%.1f MB), spc %d, fat %d' % (i, s, n, n / 2048, spc, fs))
open(out, 'wb').write(img)
