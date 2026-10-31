; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; vdpk_x64.asm - VDP 그리기 점 커널 어셈블러판 (x86-64, SSSE3). C판(기준)은 src/vdpk.c.
;   nasm -f elf64  : 호스트(리눅스, System V ABI)   nasm -f win64 : UEFI(BOOTX64.EFI, Microsoft x64 ABI)
; XMM0~5 와 rax·r10·r11 만 쓴다 (두 ABI 모두에서 호출한 쪽이 지켜 주는 칸이 아니라서 따로 저장할 것이 없음).
; n 은 16의 배수여야 한다 (G4 128, G5 128, G6 256 — 모두 만족).
bits 64
default rel

%ifidn __OUTPUT_FORMAT__, win64
  %define MSABI 1
%endif
%ifdef FORCE_MSABI            ; 시험용: elf64 로 빌드하되 Microsoft x64 호출 규칙의 레지스터 배치로 (tests/vdpktest_ms.c)
  %define MSABI 1
%endif
%ifdef MSABI
  %define A_O   rcx
  %define A_S   rdx
  %define A_N   r8d
  %define A_M   r9
  %define G_O   rcx                 ; glyph8(o, pat, fg, bg, n): n 은 다섯째 인자 = 스택 [rsp+40]
  %define G_P   rdx
  %define G_F   r8
  %define G_B   r9
%else
  %define A_O   rdi
  %define A_S   rsi
  %define A_N   edx
  %define A_M   rcx
  %define G_O   rdi
  %define G_P   rsi
  %define G_F   rdx
  %define G_B   rcx
%endif

section .rodata align=16
mask0f: times 16 db 0x0F
mask03: times 16 db 0x03
shuf2:  times 8 db 0
        times 8 db 1
bitm:   times 2 db 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

section .text
%ifdef FORCE_MSABI
%define vdpk_asm_put256 vdpk_ms_put256
%define vdpk_asm_nib16 vdpk_ms_nib16
%define vdpk_asm_crumb4 vdpk_ms_crumb4
%define vdpk_asm_glyph8 vdpk_ms_glyph8
%endif

global vdpk_asm_put256
; void put256(uint8_t *o, const uint8_t *l)   o[2x] = o[2x+1] = l[x], x < 256
vdpk_asm_put256:
    mov  eax, 16
.l: movdqu xmm0, [A_S]
    movdqa xmm1, xmm0
    punpcklbw xmm0, xmm0
    punpckhbw xmm1, xmm1
    movdqu [A_O], xmm0
    movdqu [A_O + 16], xmm1
    add  A_S, 16
    add  A_O, 32
    dec  eax
    jnz  .l
    ret

global vdpk_asm_nib16
; void nib16(uint8_t *o, const uint8_t *src, int n, const uint8_t *map16)
vdpk_asm_nib16:
    mov  eax, A_N
    shr  eax, 4
    jz   .done
    movdqu xmm2, [A_M]
    movdqa xmm5, [mask0f]
.l: movdqu xmm0, [A_S]
    movdqa xmm1, xmm0
    psrlw xmm1, 4
    pand xmm1, xmm5          ; 위 니블
    pand xmm0, xmm5          ; 아래 니블
    movdqa xmm3, xmm2
    movdqa xmm4, xmm2
    pshufb xmm3, xmm1
    pshufb xmm4, xmm0
    movdqa xmm0, xmm3
    punpcklbw xmm0, xmm4
    punpckhbw xmm3, xmm4
    movdqu [A_O], xmm0
    movdqu [A_O + 16], xmm3
    add  A_S, 16
    add  A_O, 32
    dec  eax
    jnz  .l
.done:
    ret

global vdpk_asm_crumb4
; void crumb4(uint8_t *o, const uint8_t *src, int n, const uint8_t *map4)   (map4 은 4바이트)
vdpk_asm_crumb4:
    mov  eax, A_N
    shr  eax, 4
    jz   .done
    sub  rsp, 24
    pxor xmm5, xmm5
    movd xmm5, [A_M]         ; map4 4바이트 (나머지 0)
    movdqu [rsp], xmm5       ; 16바이트로 늘린 표 (읽기가 4바이트 밖으로 나가지 않게)
.l: movdqu xmm0, [A_S]
    ; A = 점 0·1 (위 2비트, 다음 2비트)
    movdqu xmm1, [rsp]
    movdqa xmm5, xmm0
    psrlw xmm5, 6
    pand xmm5, [mask03]
    pshufb xmm1, xmm5        ; m0
    movdqu xmm2, [rsp]
    movdqa xmm5, xmm0
    psrlw xmm5, 4
    pand xmm5, [mask03]
    pshufb xmm2, xmm5        ; m1
    movdqa xmm3, xmm1
    punpcklbw xmm1, xmm2     ; A_lo
    punpckhbw xmm3, xmm2
    movdqa xmm2, xmm3        ; A_hi
    ; B = 점 2·3
    movdqu xmm3, [rsp]
    movdqa xmm4, xmm0
    psrlw xmm4, 2
    pand xmm4, [mask03]
    pshufb xmm3, xmm4        ; m2
    movdqa xmm4, xmm0
    pand xmm4, [mask03]
    movdqu xmm5, [rsp]
    pshufb xmm5, xmm4        ; m3
    movdqa xmm4, xmm3
    punpcklbw xmm3, xmm5     ; B_lo
    punpckhbw xmm4, xmm5     ; B_hi
    movdqa xmm5, xmm1
    punpcklwd xmm1, xmm3
    punpckhwd xmm5, xmm3
    movdqa xmm3, xmm2
    punpcklwd xmm2, xmm4
    punpckhwd xmm3, xmm4
    movdqu [A_O], xmm1
    movdqu [A_O + 16], xmm5
    movdqu [A_O + 32], xmm2
    movdqu [A_O + 48], xmm3
    add  A_S, 16
    add  A_O, 64
    dec  eax
    jnz  .l
    add  rsp, 24
.done:
    ret

global vdpk_asm_glyph8
; void glyph8(uint8_t *o, const uint8_t *pat, const uint8_t *fg, const uint8_t *bg, int n)   n 은 짝수 (두 칸씩 16점)
vdpk_asm_glyph8:
%ifdef MSABI
    mov  r10d, [rsp + 40]    ; 다섯째 인자 (상위 32비트는 쓰레기일 수 있어 32비트로만 읽음)
%else
    mov  r10d, r8d
%endif
    shr  r10d, 1
    jz   .done
    movdqa xmm4, [shuf2]
    movdqa xmm5, [bitm]
.l: movzx eax, word [G_P]
    movd xmm0, eax
    pshufb xmm0, xmm4        ; 무늬 바이트를 칸마다 8번 복사
    movzx eax, word [G_F]
    movd xmm1, eax
    pshufb xmm1, xmm4        ; 앞 색
    movzx eax, word [G_B]
    movd xmm2, eax
    pshufb xmm2, xmm4        ; 뒤 색
    pand xmm0, xmm5
    pcmpeqb xmm0, xmm5       ; 켜진 비트 = FF
    pand xmm1, xmm0
    pandn xmm0, xmm2
    por  xmm0, xmm1
    movdqu [G_O], xmm0
    add  G_P, 2
    add  G_F, 2
    add  G_B, 2
    add  G_O, 16
    dec  r10d
    jnz  .l
.done:
    ret
