; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; soundtest.asm - 회귀 시험용 소리 카트 (5.0.185): PSG 톤 A + SCC 채널 1 + OPLL·Y8950·OPL4 FM(있으면) 한 음씩
; pasmo soundtest.asm soundtest.rom  (Konami SCC 매퍼로 넣음: CARTMAP=2)
        org 0x4000
        defb 'A','B'
        defw init
        defw 0,0,0,0,0,0
init:   call 0x0138             ; RSLREG: 페이지 1 슬롯을 페이지 2에도 (ENASLT)
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
        ld c,a
        inc hl
        inc hl
        inc hl
        inc hl
        ld a,(hl)
        and 0x0c
        or c
        ld h,0x80
        call 0x0024
        di
        ; PSG: 톤 A 주기 0x0FE (약 440 Hz), 음량 12
        ld a,0
        out (0xa0),a
        ld a,0xfe
        out (0xa1),a
        ld a,1
        out (0xa0),a
        xor a
        out (0xa1),a
        ld a,7
        out (0xa0),a
        ld a,0xbe
        out (0xa1),a
        ld a,8
        out (0xa0),a
        ld a,12
        out (0xa1),a
        ; SCC: 0x9000 = 3Fh 로 SCC 켜고 채널 1 파형(네모)·주파수·음량
        ld a,0x3f
        ld (0x9000),a
        ld hl,0x9800
        ld b,16
w1:     ld (hl),0x70
        inc hl
        djnz w1
        ld b,16
w2:     ld (hl),0x90
        inc hl
        djnz w2
        ld a,0x7f
        ld (0x9880),a
        ld a,0x01
        ld (0x9881),a
        ld a,15
        ld (0x988a),a
        ld a,1
        ld (0x988f),a
        ; OPLL (MSX-MUSIC, 포트 7Ch/7Dh): 채널 0, 악기 3, 음량 최대, 키 켬
        ld a,0x30
        out (0x7c),a
        ld a,0x30
        out (0x7d),a
        ld a,0x10
        out (0x7c),a
        ld a,0xac
        out (0x7d),a
        ld a,0x20
        out (0x7c),a
        ld a,0x18
        out (0x7d),a
        ; MSX-AUDIO (Y8950, 포트 C0h/C1h) 한 음
        ld a,0x20
        out (0xc0),a
        ld a,0x01
        out (0xc1),a
        ld a,0x23
        out (0xc0),a
        ld a,0x01
        out (0xc1),a
        ld a,0x40
        out (0xc0),a
        ld a,0x10
        out (0xc1),a
        ld a,0x43
        out (0xc0),a
        ld a,0x00
        out (0xc1),a
        ld a,0x60
        out (0xc0),a
        ld a,0xf0
        out (0xc1),a
        ld a,0x63
        out (0xc0),a
        ld a,0xf0
        out (0xc1),a
        ld a,0x80
        out (0xc0),a
        ld a,0x0f
        out (0xc1),a
        ld a,0x83
        out (0xc0),a
        ld a,0x0f
        out (0xc1),a
        ld a,0xc0
        out (0xc0),a
        ld a,0x01
        out (0xc1),a
        ld a,0xa0
        out (0xc0),a
        ld a,0xac
        out (0xc1),a
        ld a,0xb0
        out (0xc0),a
        ld a,0x2a
        out (0xc1),a
        ; MoonSound FM (OPL4, 포트 C4h/C5h) 한 음
        ld a,0x20
        out (0xc4),a
        ld a,0x02
        out (0xc5),a
        ld a,0x23
        out (0xc4),a
        ld a,0x02
        out (0xc5),a
        ld a,0x40
        out (0xc4),a
        ld a,0x10
        out (0xc5),a
        ld a,0x43
        out (0xc4),a
        ld a,0x00
        out (0xc5),a
        ld a,0x60
        out (0xc4),a
        ld a,0xf0
        out (0xc5),a
        ld a,0x63
        out (0xc4),a
        ld a,0xf0
        out (0xc5),a
        ld a,0x80
        out (0xc4),a
        ld a,0x0f
        out (0xc5),a
        ld a,0x83
        out (0xc4),a
        ld a,0x0f
        out (0xc5),a
        ld a,0xc0
        out (0xc4),a
        ld a,0x01
        out (0xc5),a
        ld a,0xa0
        out (0xc4),a
        ld a,0xac
        out (0xc5),a
        ld a,0xb0
        out (0xc4),a
        ld a,0x2a
        out (0xc5),a
        ei
loop:   halt
        jr loop
        ds 0x6000-$,0xff
