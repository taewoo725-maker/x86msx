#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
# addlfn.py img SHORTNAME.EXT "긴 이름" : FAT16 최상위의 8.3 항목 앞에 VFAT 긴 이름 조각을 넣음 (3.4.7 시험용).
# mtools는 여러 바이트 글자로 된 아주 긴 이름(255자)을 제대로 쓰지 못해서, Windows와 같은 배치를 직접 만든다.
import struct, sys
img, short, lname = sys.argv[1], sys.argv[2], sys.argv[3]
b = bytearray(open(img, 'rb').read())
bps, spc, rsv, nf, nroot, _, _, fsz = struct.unpack_from('<HBHBHHBH', b, 11)
root = (rsv + nf * fsz) * bps
ents = [b[root + i * 32: root + i * 32 + 32] for i in range(nroot)]
n, e = short.split('.') if '.' in short else (short, '')
sn = (n.ljust(8) + e.ljust(3)).encode()
idx = next(i for i, x in enumerate(ents) if x[:11] == sn)
s = 0
for c in sn: s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
u = list(lname.encode('utf-16-le'))
u = [u[i] | u[i + 1] << 8 for i in range(0, len(u), 2)]
parts = (len(u) + 12) // 13
u = u + [0] + [0xFFFF] * (parts * 13 - len(u) - 1) if len(u) % 13 else u
pos = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
lfn = []
for p in range(parts, 0, -1):
    x = bytearray(32); x[0] = p | (0x40 if p == parts else 0); x[11] = 0x0F; x[13] = s
    for j in range(13): struct.pack_into('<H', x, pos[j], u[(p - 1) * 13 + j])
    lfn.append(x)
ents = ents[:idx] + lfn + ents[idx:]
assert all(x[0] == 0 for x in ents[nroot:]), 'root directory full'
for i in range(nroot): b[root + i * 32: root + i * 32 + 32] = ents[i]
open(img, 'wb').write(b)
