#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""wavfreq.py - WAV 구간의 기본 주파수(평균 교차 횟수)와 크기 측정
사용법: wavfreq.py file.wav start_s end_s"""
import struct, sys, wave
w = wave.open(sys.argv[1]); rate = w.getframerate()
a, b = float(sys.argv[2]), float(sys.argv[3])
w.setpos(int(a * rate)); n = int((b - a) * rate)
x = struct.unpack('<%dh' % n, w.readframes(n))
mean = sum(x) / n
cross = sum(1 for i in range(1, n) if (x[i - 1] - mean) < 0 <= (x[i] - mean))
print('%.1f Hz  (min %d, max %d, mean %.0f)' % (cross / (b - a), min(x), max(x), mean))
