; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; v9_conv.asm (8.0.136) - X86 Video 가 V9990 으로 영상을 내보낼 때 쓰는 색 변환 어셈블러 (SSE2). C 판(기준)은 src/mp4play.c(mp4p_rgb32) 와 src/mmv9.c(mmv9_frame).
;   8.0.159: 비교용 스위치(V9Asm)와 C 판 삭제, 항상 이 어셈을 씀.(기본) 일 때만 부름. 0 이면 예전 C 코드.
;   인자는 구조체 포인터 하나 (win64: rcx, SysV: rdi). 쓰는 레지스터: rax,rcx,rdx,r8-r11, xmm0-xmm9 (win64 에서는 xmm6~9 를 저장·복원). n 은 8의 배수.
;   v9_yuv2rgb  : 줄 하나의 Y·U·V 바이트(점마다 한 칸) → 0x00RRGGBB 32비트 (식: r=(298(y-16)+128+rv(v-128))>>8 … 0~255 로 자름)
;   v9_pack15   : 0x00RRGGBB → V9990 B2 의 16비트 색 (GGGGGRRRRRBBBBB, 낮은 바이트 먼저)
default rel
%ifdef WIN64
%define A1 rcx
%else
%define A1 rdi
%endif

SECTION .text align=16

%macro SAVE6789 0
%ifdef WIN64
    sub rsp, 88
    movdqa [rsp], xmm6
    movdqa [rsp + 16], xmm7
    movdqa [rsp + 32], xmm8
    movdqa [rsp + 48], xmm9
%endif
%endmacro
%macro REST6789 0
%ifdef WIN64
    movdqa xmm6, [rsp]
    movdqa xmm7, [rsp + 16]
    movdqa xmm8, [rsp + 32]
    movdqa xmm9, [rsp + 48]
    add rsp, 88
%endif
%endmacro

; ---- 상수 (16바이트씩). 묶음 하나 = kR, kB, kG1, kG2 (워드 쌍 → pmaddwd) ----
align 16
k_sd:   dw 298, 409, 298, 409, 298, 409, 298, 409          ; R = y*298 + v*409
        dw 298, 516, 298, 516, 298, 516, 298, 516          ; B = y*298 + u*516
        dw 298, -100, 298, -100, 298, -100, 298, -100      ; G = y*298 + u*(-100)
        dw -208, 0, -208, 0, -208, 0, -208, 0              ;   + v*(-208)
k_hd:   dw 298, 459, 298, 459, 298, 459, 298, 459
        dw 298, 541, 298, 541, 298, 541, 298, 541
        dw 298, -55, 298, -55, 298, -55, 298, -55
        dw -136, 0, -136, 0, -136, 0, -136, 0
c_16:   times 8 dw 16
c_128:  times 8 dw 128
c_128d: times 4 dd 128
m_b5:   times 4 dd 0x1F                                     ; 15비트: 파랑 5비트
m_r5:   times 4 dd 0x3E0
m_g5:   times 4 dd 0x7C00
m_g8:   times 4 dd 0xE0                                     ; 8비트: 초록 위 3비트
m_r8:   times 4 dd 0x1C
m_b8:   times 4 dd 0x03

; ---- 구조체 V9Cv { const u8 *y, *u, *v; u32 *out; int n, hd; }  오프셋 0 8 16 24 32 36 ----
global v9_yuv2rgb
v9_yuv2rgb:
    SAVE6789
    mov r8, [A1]                          ; y
    mov r9, [A1 + 8]                      ; u
    mov r10, [A1 + 16]                    ; v
    mov r11, [A1 + 24]                    ; out
    mov eax, dword [A1 + 32]              ; n
    mov edx, dword [A1 + 36]              ; hd
    lea rcx, [k_sd]                       ; (win64: A1 = rcx 였지만 위에서 다 읽었음)
    test edx, edx
    jz .sd
    lea rcx, [k_hd]
.sd:
    shr eax, 3                            ; 8점씩
    jz .done
    pxor xmm7, xmm7
    movdqa xmm8, [c_16]
    movdqa xmm9, [c_128]
.lp:
    movq xmm0, qword [r8]
    movq xmm1, qword [r9]
    movq xmm2, qword [r10]
    punpcklbw xmm0, xmm7
    punpcklbw xmm1, xmm7
    punpcklbw xmm2, xmm7
    psubw xmm0, xmm8                      ; y-16
    psubw xmm1, xmm9                      ; u-128
    psubw xmm2, xmm9                      ; v-128
    ; --- R ---
    movdqa xmm3, xmm0
    punpcklwd xmm3, xmm2                  ; (y,v) 점 0~3
    movdqa xmm4, xmm0
    punpckhwd xmm4, xmm2                  ; (y,v) 점 4~7
    movdqa xmm5, [rcx]
    pmaddwd xmm3, xmm5
    pmaddwd xmm4, xmm5
    paddd xmm3, [c_128d]
    paddd xmm4, [c_128d]
    psrad xmm3, 8
    psrad xmm4, 8
    packssdw xmm3, xmm4
    packuswb xmm3, xmm3                   ; xmm3 = R 바이트 8개
    ; --- B ---
    movdqa xmm4, xmm0
    punpcklwd xmm4, xmm1                  ; (y,u) 점 0~3
    movdqa xmm5, xmm0
    punpckhwd xmm5, xmm1                  ; (y,u) 점 4~7
    movdqa xmm6, [rcx + 16]
    pmaddwd xmm4, xmm6
    pmaddwd xmm5, xmm6
    paddd xmm4, [c_128d]
    paddd xmm5, [c_128d]
    psrad xmm4, 8
    psrad xmm5, 8
    packssdw xmm4, xmm5
    packuswb xmm4, xmm4                   ; xmm4 = B 바이트 8개
    ; --- G (y,u 쌍 + v 단독) ---
    movdqa xmm5, xmm0
    punpcklwd xmm5, xmm1                  ; (y,u) 0~3
    punpckhwd xmm0, xmm1                  ; (y,u) 4~7   (y 는 더 안 씀)
    movdqa xmm6, [rcx + 32]
    pmaddwd xmm5, xmm6
    pmaddwd xmm0, xmm6
    movdqa xmm6, xmm2
    punpcklwd xmm6, xmm7                  ; (v,0) 0~3
    punpckhwd xmm2, xmm7                  ; (v,0) 4~7
    movdqa xmm1, [rcx + 48]
    pmaddwd xmm6, xmm1
    pmaddwd xmm2, xmm1
    paddd xmm5, xmm6
    paddd xmm0, xmm2
    paddd xmm5, [c_128d]
    paddd xmm0, [c_128d]
    psrad xmm5, 8
    psrad xmm0, 8
    packssdw xmm5, xmm0
    packuswb xmm5, xmm5                   ; xmm5 = G 바이트 8개
    ; --- 0x00RRGGBB 로 합치기 (바이트 순서 B G R 0) ---
    punpcklbw xmm4, xmm5                  ; B0 G0 B1 G1 …
    punpcklbw xmm3, xmm7                  ; R0 00 R1 00 …
    movdqa xmm0, xmm4
    punpcklwd xmm0, xmm3                  ; 점 0~3
    punpckhwd xmm4, xmm3                  ; 점 4~7
    movdqu [r11], xmm0
    movdqu [r11 + 16], xmm4
    add r8, 8
    add r9, 8
    add r10, 8
    add r11, 32
    dec eax
    jnz .lp
.done:
    REST6789
    ret

; ---- 구조체 V9P15 { const u32 *rgb; u8 *out; int n; }  오프셋 0 8 16 ----
global v9_pack15
v9_pack15:
    mov r8, [A1]
    mov r9, [A1 + 8]
    mov eax, dword [A1 + 16]
    shr eax, 3
    jz .done
    movdqa xmm5, [m_b5]
.lp:
    movdqu xmm0, [r8]
    movdqu xmm1, [r8 + 16]
    ; 점 0~3
    movdqa xmm2, xmm0
    psrld xmm2, 3
    pand xmm2, xmm5                       ; b = (c>>3)&31
    movdqa xmm3, xmm0
    psrld xmm3, 14
    pand xmm3, [m_r5]                       ; r<<5 = (c>>14)&0x3E0
    por xmm2, xmm3
    psrld xmm0, 1
    pand xmm0, [m_g5]                       ; g<<10 = (c>>1)&0x7C00
    por xmm0, xmm2
    ; 점 4~7
    movdqa xmm2, xmm1
    psrld xmm2, 3
    pand xmm2, xmm5
    movdqa xmm3, xmm1
    psrld xmm3, 14
    pand xmm3, [m_r5]
    por xmm2, xmm3
    psrld xmm1, 1
    pand xmm1, [m_g5]
    por xmm1, xmm2
    packssdw xmm0, xmm1                   ; 값이 0x7FFF 이하라 그대로 워드로
    movdqu [r9], xmm0
    add r8, 32
    add r9, 16
    dec eax
    jnz .lp
.done:
    ret
