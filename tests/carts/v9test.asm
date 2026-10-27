; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; v9test.asm - 회귀 시험용 V9990 카트 (5.2.151): B1 모드 8비트(BD8) 256 x 212 에 (x+y) 띠 그림
; pasmo v9test.asm v9test.rom  (ROM 플레인 16K 로 넣음)
        org 0x4000
        defb 'A','B'
        defw init
        defw 0,0,0,0,0,0
init:   di
        xor a
        out (0x67),a            ; 시스템 제어: 리셋 풀기, B1 계열
        ld a,6                  ; 레지스터 6부터 자동 증가 쓰기
        out (0x64),a
        ld a,0x82               ; R6 = 비트맵·256폭·8비트
        out (0x63),a
        xor a
        out (0x63),a            ; R7
        out (0x63),a            ; R8 = 표시 끔
        ld a,13
        out (0x64),a
        ld a,0x40               ; R13 = BD8
        out (0x63),a
        xor a
        out (0x64),a            ; 쓰기 주소 R0~R2 = 0
        out (0x63),a
        out (0x63),a
        out (0x63),a
        ld d,0
yl:     ld b,0
        ld a,d
xl:     out (0x60),a
        inc a
        djnz xl
        inc d
        ld a,d
        cp 212
        jr nz,yl
        ld a,8
        out (0x64),a
        ld a,0x80               ; R8 = 표시 켬
        out (0x63),a
hang:   jr hang
        ds 0x8000-$,0
