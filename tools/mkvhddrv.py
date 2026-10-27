#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
# mkvhddrv.py : tools/vhddrv.asm → kernel/vhd_drv.h (z80asm 필요). 3.7.1
import subprocess, os
h = os.path.dirname(os.path.abspath(__file__))
subprocess.check_call(['z80asm', '-o', '/tmp/vhddrv.bin', os.path.join(h, 'vhddrv.asm')])
d = open('/tmp/vhddrv.bin', 'rb').read()
assert d[0x30] == 0xC3 and d[0x51] == 0xC3 and d[0x60] == 0xC3 and d[0x69] == 0xC3
out = ['/* vhd_drv.h - tools/mkvhddrv.py가 tools/vhddrv.asm에서 만듦 (손대지 마세요). Nextor 드라이버 뱅크 4100h부터 */',
       'static const unsigned char vhd_drv[%d] = {' % len(d)]
for i in range(0, len(d), 16): out.append('    ' + ', '.join('0x%02X' % b for b in d[i:i + 16]) + ',')
out.append('};')
open(os.path.join(h, '..', 'kernel', 'vhd_drv.h'), 'w').write('\n'.join(out) + '\n')
print('vhd_drv.h', len(d))
