#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""make_carts.py - 매퍼 시험용 카트리지 생성 (z80asm 필요)

각 ROM은 0x4000에서 시작해 뱅크를 바꾸며 창 주소의 표지를 BIOS CHPUT으로 출력한다.
기대 출력은 expected.txt에 기록한다.
"""
import os, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))

def asm(src):
    path = os.path.join(HERE, 'tmp.asm')
    out = os.path.join(HERE, 'tmp.bin')
    open(path, 'w').write(src)
    subprocess.run(['z80asm', '-o', out, path], check=True)
    data = open(out, 'rb').read()
    os.remove(path); os.remove(out)
    return data

def program(title, steps, extra=''):
    """steps: [(레지스터 주소, 창 주소, 뱅크 번호)]"""
    body = ''
    for reg, win, bank in steps:
        body += f'    ld a,{bank}\n    ld ({reg:#06x}),a\n    ld hl,{win:#06x}\n    call print\n'
    return asm(f'''CHPUT: equ 0x00a2
    org 0x4000
    defb 'A','B'
    defw init
    defw 0,0,0,0,0,0
init:
    ; 실제 게임처럼 0x8000-0xBFFF 페이지도 이 카트리지 슬롯으로 (RSLREG, ENASLT)
    call 0x0138
    rrca
    rrca
    and 3
    ld c,a
    ld b,0
    ld hl,0xfcc1
    add hl,bc
    ld a,(hl)
    and 0x80
    or c
    ld h,0x80
    call 0x0024
    ld hl,title
    call print
{body}{extra}    ld hl,done
    call print
forever:
    jr forever
print:
    ld a,(hl)
    or a
    ret z
    call CHPUT
    inc hl
    jr print
title: defb '{title}',0
done:  defb ' END',0
''')

def mega(code, bank_size, total=128 * 1024):
    rom = bytearray(b'\xff' * total)
    rom[:len(code)] = code
    for b in range(1, total // bank_size):
        mark = b'[%02d]' % b + b'\0'
        rom[b * bank_size:b * bank_size + len(mark)] = mark
    return bytes(rom)

def write(name, data):
    open(os.path.join(HERE, name), 'wb').write(data)

expected = {}

# Konami: 0x6000/0x8000/0xA000 쓰기 → 같은 주소 창. 17 = 16뱅크 넘어 1로 되감김
steps = [(0x6000, 0x6000, 3), (0x8000, 0x8000, 7), (0xA000, 0xA000, 12), (0x8000, 0x8000, 15), (0x6000, 0x6000, 17)]
write('konami.rom', mega(program('KON:', steps), 8192))
expected['konami.rom'] = ('Konami', 'KON:[03][07][12][15][01] END')

# Konami SCC: 0x7000/0x9000/0xB000 → 0x6000/0x8000/0xA000 창
steps = [(0x7000, 0x6000, 4), (0x9000, 0x8000, 9), (0xB000, 0xA000, 14), (0x9000, 0x8000, 2)]
write('konamiscc.rom', mega(program('SCC:', steps), 8192))
expected['konamiscc.rom'] = ('KonamiSCC', 'SCC:[04][09][14][02] END')

# ASCII8: 0x6800/0x7000/0x7800 → 0x6000/0x8000/0xA000 창
steps = [(0x6800, 0x6000, 2), (0x7000, 0x8000, 6), (0x7800, 0xA000, 11), (0x7000, 0x8000, 13)]
write('ascii8.rom', mega(program('ASCII8:', steps), 8192))
expected['ascii8.rom'] = ('ASCII8', 'ASCII8:[02][06][11][13] END')

# ASCII16: 0x7000(0x77FF 포함) → 0x8000-0xBFFF 16 KiB 창
steps = [(0x7000, 0x8000, 3), (0x77FF, 0x8000, 5), (0x7000, 0x8000, 7)]
write('ascii16.rom', mega(program('ASCII16:', steps), 16384))
expected['ascii16.rom'] = ('ASCII16', 'ASCII16:[03][05][07] END')

# 일반 16 KiB
write('plain16.rom', program('PLAIN16 OK', []) + b'\xff' * 0)
d = program('PLAIN16 OK', [])
write('plain16.rom', d + b'\xff' * (16384 - len(d)))
expected['plain16.rom'] = ('Plain', 'PLAIN16 OK END')

# 일반 32 KiB: 0x8000 쪽 데이터를 출력해 위쪽 16 KiB 배치를 확인
d = program('PLAIN32:', [], extra='    ld hl,0x8000\n    call print\n')
rom = bytearray(b'\xff' * 32768)
rom[:len(d)] = d
rom[0x4000:0x4000 + 8] = b'HIGH OK\0'
write('plain32.rom', bytes(rom))
expected['plain32.rom'] = ('Plain', 'PLAIN32:HIGH OK END')

with open(os.path.join(HERE, 'expected.txt'), 'w') as f:
    for k, (mapper, text) in expected.items():
        f.write(f'{k}\t{mapper}\t{text}\n')
print('\n'.join(f'{k}: {v[0]:9s} {v[1]}' for k, v in expected.items()))
