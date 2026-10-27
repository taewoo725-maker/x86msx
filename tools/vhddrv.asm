; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; vhddrv.asm - X86MSX 가상 디스크 Nextor 드라이버 (3.7.1)
; 장치 기반 드라이버. 실제 일은 에뮬레이터가 한다: 루틴마다 레지스터를 그대로 두고 OUT (포트),A 한 번,
; 에뮬레이터가 그 자리에서 메모리(HL, DE가 가리키는 곳)를 읽고 쓰고 A·B·C를 결과로 바꿔 둔다.
;   3Ah DRV_INIT   3Bh DRV_CONFIG   3Ch DEV_RW   3Dh DEV_INFO   3Eh DEV_STATUS   3Fh LUN_INFO
; 장치 1 = 플로피 (LUN 1 = A, LUN 2 = B), 장치 2~7 = 가상 하드 C~H.
; 만들기: z80asm -o vhddrv.bin vhddrv.asm → tools/mkvhddrv.py가 kernel/vhd_drv.h로 (4100h부터의 바이트)

        org 4100h

        db "NEXTOR_DRIVER",0            ; 4100h 서명
        db 1 + 4                        ; 410Eh 장치 기반 + DRV_CONFIG
        db 0                            ; 410Fh
        db "X86MSX virtual disk           " ; 4110h 이름 32자
        db "  "

        jp DRV_TIMI                     ; 4130h
        jp DRV_VERSION                  ; 4133h
        jp DRV_INIT                     ; 4136h
        jp DRV_BASSTAT                  ; 4139h
        jp DRV_BASDEV                   ; 413Ch
        jp DRV_EXTBIO                   ; 413Fh
        jp DRV_DIRECT                   ; 4142h DIRECT0
        jp DRV_DIRECT                   ; 4145h
        jp DRV_DIRECT                   ; 4148h
        jp DRV_DIRECT                   ; 414Bh
        jp DRV_DIRECT                   ; 414Eh DIRECT4
        jp DRV_CONFIG                   ; 4151h
        db 0,0,0,0,0,0,0,0,0,0,0,0      ; 4154h~415Fh 비워 둠

        jp DEV_RW                       ; 4160h
        jp DEV_INFO                     ; 4163h
        jp DEV_STATUS                   ; 4166h
        jp LUN_INFO                     ; 4169h

DRV_TIMI:
DRV_EXTBIO:
DRV_DIRECT:
        ret

DRV_VERSION:
        ld a,1
        ld b,0
        ld c,0
        ret

DRV_INIT:                               ; 첫 번째(A=0): 작업 자리 필요 없음 (HL=0), 타이머 안 씀 (Cy=0)
        out (3Ah),a
        or a
        jr nz,INIT2
        ld hl,0
        or a
        ret
INIT2:
        ret

DRV_BASSTAT:
DRV_BASDEV:
        scf
        ret

DRV_CONFIG:                             ; A=1 부팅 때 드라이브 수 (→B), A=2 드라이브 C번째 → 장치 B·LUN C
        out (3Bh),a
        ret

DEV_RW:                                 ; Cy 0 읽기 / 1 쓰기, A 장치, B 섹터 수, C LUN, HL 주소, DE → 섹터 번호 4바이트
        out (3Ch),a
        ret

DEV_INFO:                               ; A 장치, B 종류, HL 버퍼
        out (3Dh),a
        ret

DEV_STATUS:                             ; A 장치, B LUN
        out (3Eh),a
        ret

LUN_INFO:                               ; A 장치, B LUN, HL 버퍼 12바이트
        out (3Fh),a
        ret

        end
