#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkgmtab.py - 3.4.5 소프트웨어 MIDI 음원 표 → src/gmsynth_tab.h (본체는 실수를 쓰지 않음)
  gm_sin[1024]   사인 Q14
  gm_pow2[1200]  2^(i/1200) Q16 (센트 → 배율)
  gm_cb[1441]    10^(-i/200) Q15 (센티벨 감쇠 → 배율)"""
import math, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
L = ['/* gmsynth_tab.h - tools/mkgmtab.py가 만듦 (고치지 말 것) */', '#ifndef MSX_GMSYNTH_TAB_H', '#define MSX_GMSYNTH_TAB_H', '#include <stdint.h>']
def arr(name, ctype, vals, per=16):
    L.append('static const %s %s[%d] = {' % (ctype, name, len(vals)))
    for i in range(0, len(vals), per):
        L.append('    ' + ', '.join(str(v) for v in vals[i:i+per]) + ',')
    L.append('};')
arr('gm_sin', 'int16_t', [round(16383 * math.sin(2 * math.pi * i / 1024)) for i in range(1024)])
arr('gm_pow2', 'uint32_t', [round(65536 * 2 ** (i / 1200)) for i in range(1200)])
arr('gm_cb', 'uint16_t', [round(32767 * 10 ** (-i / 200)) for i in range(1441)])
L += ['#endif', '']
open(os.path.join(ROOT, 'src', 'gmsynth_tab.h'), 'w').write('\n'.join(L))
print('ok')
