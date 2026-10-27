#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mksf2.py out.sf2 - 3.4.5 시험용 작은 SoundFont: 프리셋 0(뱅크 0) = 사인 루프 샘플(1주기 = 440 Hz 기준 음 69),
프리셋 0(뱅크 128) = 짧은 잡음 (루프 없음). 발생기: 키 범위, 어택·릴리스, 루프 모드, 샘플 번호"""
import struct, sys, math
def ck(t, d): return t + struct.pack('<I', len(d)) + d + (b'\0' if len(d) & 1 else b'')
def lst(t, d): return ck(b'LIST', t + d)
sr = 44000
per = sr // 440                                   # 100 표본 = 440 Hz
sine = [int(12000 * math.sin(2 * math.pi * i / per)) for i in range(per * 20)]
noise = [((i * 7919 * 104729) % 20000) - 10000 for i in range(4000)]
smp = sine + [0] * 46 + noise + [0] * 46
smpl = struct.pack('<%dh' % len(smp), *smp)
ns = len(sine); no = ns + 46
def shdr(name, s, e, ls, le, root):
    return name.ljust(20, b'\0')[:20] + struct.pack('<IIIIIBbHH', s, e, ls, le, sr, root, 0, 0, 1)
shd = shdr(b'sine', 0, ns, per * 2, per * 18, 69) + shdr(b'noise', no, no + 4000, 0, 0, 60) + b'EOS'.ljust(46, b'\0')
def gen(op, val): return struct.pack('<Hh', op, val)
def rng(op, lo, hi): return struct.pack('<HBB', op, lo, hi)
# 악기 0: 사인 루프 (전역 존: 릴리스), 악기 1: 잡음
igen = rng(43, 0, 127) + gen(34, -7000) + gen(38, -3000) + gen(54, 1) + gen(53, 0) \
     + gen(53, 1) + gen(0, 0)
igen = (gen(38, -3000)) + rng(43, 0, 127) + gen(34, -7000) + gen(54, 1) + gen(53, 0) + gen(53, 1)
# 존: [전역: 릴리스] [사인] / [잡음]
ibag = struct.pack('<HH', 0, 0) + struct.pack('<HH', 1, 0) + struct.pack('<HH', 5, 0) + struct.pack('<HH', 6, 0)
inst = b'sine'.ljust(20, b'\0') + struct.pack('<H', 0) + b'noise'.ljust(20, b'\0') + struct.pack('<H', 2) + b'EOI'.ljust(20, b'\0') + struct.pack('<H', 3)
pgen = gen(41, 0) + gen(41, 1)
pbag = struct.pack('<HH', 0, 0) + struct.pack('<HH', 1, 0) + struct.pack('<HH', 2, 0)
def ph(name, prog, bank, bag): return name.ljust(20, b'\0') + struct.pack('<HHHIII', prog, bank, bag, 0, 0, 0)
phdr = ph(b'Sine', 0, 0, 0) + ph(b'Drums', 0, 128, 1) + ph(b'EOP', 0, 0, 2)
imod = b'\0' * 10; pmod = b'\0' * 10
pdta = ck(b'phdr', phdr) + ck(b'pbag', pbag) + ck(b'pmod', pmod) + ck(b'pgen', pgen + b'\0' * 4) + ck(b'inst', inst) + ck(b'ibag', ibag) + ck(b'imod', imod) + ck(b'igen', igen + b'\0' * 4) + ck(b'shdr', shd)
body = b'sfbk' + lst(b'INFO', ck(b'ifil', struct.pack('<HH', 2, 1)) + ck(b'INAM', b'X86MSX test\0')) + lst(b'sdta', ck(b'smpl', smpl)) + lst(b'pdta', pdta)
open(sys.argv[1], 'wb').write(ck(b'RIFF', body)[:8] + body if False else b'RIFF' + struct.pack('<I', len(body)) + body)
print('ok', len(smp))
