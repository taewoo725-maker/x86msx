# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
# usage: bin2c.py file nbytes name  — 앞 nbytes 바이트를 C 배열로 (8.0.6 mpwake_tramp.inc 용)
import sys
d = open(sys.argv[1], 'rb').read()[:int(sys.argv[2])]
print('/* 자동 생성 (tools/bin2c.py, uefi/mpwake_tramp.asm) — 직접 고치지 말 것 */')
print('static const uint8_t %s[%d] = {' % (sys.argv[3], len(d)))
for i in range(0, len(d), 16):
    print('    ' + ', '.join('0x%02X' % x for x in d[i:i+16]) + ',')
print('};')
