#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkopmtab.py - 3.4.1 YM2151 표 (src/ym64_tab.h): 사인, 감쇠→진폭, 1/768 옥타브 배율, 위상 증가 기준"""
import math, sys
out = sys.argv[1] if len(sys.argv) > 1 else 'src/ym64_tab.h'
FS = 3579545 / 64
L = ['/* ym64_tab.h - tools/mkopmtab.py가 만듦 (고치지 말 것) */', '#include <stdint.h>']
L.append('static const int16_t opm_sin[1024] = {' + ','.join(str(int(round(math.sin((i + 0.5) * 2 * math.pi / 1024) * 8191))) for i in range(1024)) + '};')
L.append('static const uint16_t opm_exp[64] = {' + ','.join(str(int(round(65535 * 2 ** (-i / 64)))) for i in range(64)) + '};')
L.append('static const uint32_t opm_pow768[768] = {' + ','.join(str(int(round(65536 * 2 ** (i / 768)))) for i in range(768)) + '};')
L.append('#define OPM_INC_A4 %du        /* 440 Hz 위상 증가 (32비트 위상, fs = 3579545/64) */' % int(round(440 / FS * 2 ** 32)))
open(out, 'w').write('\n'.join(L) + '\n')
