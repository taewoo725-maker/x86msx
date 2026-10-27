; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; bootsec.asm - 회귀 시험용 부트 섹터 (5.2.151): 디스크 ROM 이 읽어 C000 에서 실행 → 글자를 찍고 멈춤
; pasmo bootsec.asm bootsec.bin  (make_dsk.py 가 BPB 뒤 0x1E 에 붙임)
        org 0xC01E
        ld hl,msg
lp:     ld a,(hl)
        and a
        jr z,done
        call 0x00A2             ; CHPUT
        inc hl
        jr lp
done:   jr done
msg:    defm "X86MSX BOOT SECTOR OK",13,10,0
