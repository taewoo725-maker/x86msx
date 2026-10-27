#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""make_dsk.py - 회귀 시험용 부트 디스크 boot.dsk (720 KB, 5.2.151) — bootsec.asm 을 pasmo 로 만든 bootsec.bin 이 있어야 함"""
import os
H = os.path.dirname(os.path.abspath(__file__))
code = open(os.path.join(H, 'bootsec.bin'), 'rb').read()
s = bytearray(512)
s[0:3] = b'\xEB\x1C\x90'
s[3:11] = b'X86MSX  '
s[11:13] = (512).to_bytes(2, 'little'); s[13] = 2; s[14:16] = (1).to_bytes(2, 'little'); s[16] = 2
s[17:19] = (112).to_bytes(2, 'little'); s[19:21] = (1440).to_bytes(2, 'little'); s[21] = 0xF9
s[22:24] = (3).to_bytes(2, 'little'); s[24:26] = (9).to_bytes(2, 'little'); s[26:28] = (2).to_bytes(2, 'little')
s[0x1E:0x1E + len(code)] = code
img = bytearray(737280)
img[0:512] = s
for off in (512, 512 + 3 * 512):
    img[off:off + 3] = b'\xF9\xFF\xFF'
open(os.path.join(H, 'boot.dsk'), 'wb').write(img)
