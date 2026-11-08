; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; oh_wp.asm (8.0.126) - OpenH264 해독기의 가중 예측(WeightPrediction / BiWeightPrediction)을 SSE2 로.
;   C 판은 한 점마다 포인터를 다시 읽어 느렸음 (callgrind: 전체 명령의 약 45%). 결과는 C 판과 똑같아야 함.
;   8.0.159: Mp4Asm 스위치 삭제, 항상 이 어셈을 씀. 폭 16·8·4·2 만 받음(호출하는 쪽 src/h264oh.cpp 가 검사).
;   인자는 구조체 포인터 하나 (win64: rcx, SysV: rdi). 쓰는 레지스터: rax,rcx,rdx,r8-r11, xmm0-7 (win64 에서는 xmm6·7 을 저장·복원).
;   구조체 OhWp: p(8) q(8) stride(8) w(4) h(4) logd(4) w1(4) w2(4) off(4)  → 오프셋 0 8 16 24 28 32 36 40 44
default rel
%ifdef WIN64
%define A1 rcx
%else
%define A1 rdi
%endif

SECTION .text align=16

%macro SAVE67 0
%ifdef WIN64
    sub rsp, 40
    movdqa [rsp], xmm6
    movdqa [rsp + 16], xmm7
%endif
%endmacro
%macro REST67 0
%ifdef WIN64
    movdqa xmm6, [rsp]
    movdqa xmm7, [rsp + 16]
    add rsp, 40
%endif
%endmacro

; ---- 한 방향: p = clip(((p*w1 + round) >> logd) + off) ----
%macro UNI8 0                 ; xmm0 = 바이트 8개(앞쪽) → xmm0 앞쪽 8바이트
    punpcklbw xmm0, xmm7
    pmullw xmm0, xmm1
    paddw xmm0, xmm3
    psraw xmm0, xmm2
    paddsw xmm0, xmm5
    packuswb xmm0, xmm0
%endmacro

global oh_wp_uni
oh_wp_uni:
    SAVE67
    mov rax, [A1]                         ; (win64 는 A1 = rcx 라서 cl 을 쓰기 전에 구조체를 모두 읽음)
    mov rdx, [A1 + 16]
    mov r8d, dword [A1 + 24]              ; w
    mov r9d, dword [A1 + 28]              ; h
    mov r10d, dword [A1 + 32]             ; logd
    movd xmm1, dword [A1 + 36]            ; 가중치 → 워드 8개
    pshuflw xmm1, xmm1, 0
    punpcklqdq xmm1, xmm1
    movd xmm5, dword [A1 + 44]            ; 보정값 → 워드 8개
    pshuflw xmm5, xmm5, 0
    punpcklqdq xmm5, xmm5
    movd xmm2, r10d                       ; 오른쪽 이동 횟수
    xor r11d, r11d                        ; 반올림 = logd>=1 ? 1<<(logd-1) : 0
    test r10d, r10d
    jz .nr
    lea ecx, [r10 - 1]
    mov r11d, 1
    shl r11d, cl
.nr:
    movd xmm3, r11d
    pshuflw xmm3, xmm3, 0
    punpcklqdq xmm3, xmm3
    pxor xmm7, xmm7
    cmp r8d, 16
    je .w16
    cmp r8d, 8
    je .w8
    cmp r8d, 4
    je .w4
.w2:
    movzx r10d, word [rax]
    movd xmm0, r10d
    UNI8
    movd r10d, xmm0
    mov [rax], r10w
    add rax, rdx
    dec r9d
    jnz .w2
    jmp .done
.w4:
    movd xmm0, dword [rax]
    UNI8
    movd dword [rax], xmm0
    add rax, rdx
    dec r9d
    jnz .w4
    jmp .done
.w8:
    movq xmm0, qword [rax]
    UNI8
    movq qword [rax], xmm0
    add rax, rdx
    dec r9d
    jnz .w8
    jmp .done
.w16:
    movq xmm0, qword [rax]
    UNI8
    movq qword [rax], xmm0
    movq xmm0, qword [rax + 8]
    UNI8
    movq qword [rax + 8], xmm0
    add rax, rdx
    dec r9d
    jnz .w16
.done:
    REST67
    ret

; ---- 두 방향: p = clip(((p*w1 + q*w2 + (1<<logd)) >> (logd+1)) + off) ----
%macro BI8 0                  ; xmm0 = p 바이트 8개, xmm4 = q 바이트 8개 → xmm0 앞쪽 8바이트
    punpcklbw xmm0, xmm7
    punpcklbw xmm4, xmm7
    movdqa xmm6, xmm0
    punpcklwd xmm0, xmm4
    punpckhwd xmm6, xmm4
    pmaddwd xmm0, xmm1
    pmaddwd xmm6, xmm1
    paddd xmm0, xmm2
    paddd xmm6, xmm2
    psrad xmm0, xmm3
    psrad xmm6, xmm3
    paddd xmm0, xmm5
    paddd xmm6, xmm5
    packssdw xmm0, xmm6
    packuswb xmm0, xmm0
%endmacro

global oh_wp_bi
oh_wp_bi:
    SAVE67
    mov rax, [A1]
    mov r10, [A1 + 8]                     ; q
    mov rdx, [A1 + 16]
    mov r8d, dword [A1 + 24]
    mov r9d, dword [A1 + 28]
    movd xmm1, dword [A1 + 36]            ; w1 → 아래 16비트만
    pslld xmm1, 16
    psrld xmm1, 16
    movd xmm2, dword [A1 + 40]            ; w2 → 위 16비트
    pslld xmm2, 16
    por xmm1, xmm2
    pshufd xmm1, xmm1, 0                  ; (w1 | w2<<16) 4번
    movd xmm5, dword [A1 + 44]            ; 보정값 (32비트 4칸)
    pshufd xmm5, xmm5, 0
    movd xmm3, dword [A1 + 32]            ; logd
    movd r11d, xmm3
    mov ecx, r11d
    mov r11d, 1
    shl r11d, cl                          ; 반올림 = 1<<logd
    movd xmm2, r11d
    pshufd xmm2, xmm2, 0
    inc ecx
    movd xmm3, ecx                        ; 이동 횟수 logd+1
    pxor xmm7, xmm7
    cmp r8d, 16
    je .b16
    cmp r8d, 8
    je .b8
    cmp r8d, 4
    je .b4
.b2:
    movzx r11d, word [rax]
    movd xmm0, r11d
    movzx r11d, word [r10]
    movd xmm4, r11d
    BI8
    movd r11d, xmm0
    mov [rax], r11w
    add rax, rdx
    add r10, rdx
    dec r9d
    jnz .b2
    jmp .bdone
.b4:
    movd xmm0, dword [rax]
    movd xmm4, dword [r10]
    BI8
    movd dword [rax], xmm0
    add rax, rdx
    add r10, rdx
    dec r9d
    jnz .b4
    jmp .bdone
.b8:
    movq xmm0, qword [rax]
    movq xmm4, qword [r10]
    BI8
    movq qword [rax], xmm0
    add rax, rdx
    add r10, rdx
    dec r9d
    jnz .b8
    jmp .bdone
.b16:
    movq xmm0, qword [rax]
    movq xmm4, qword [r10]
    BI8
    movq qword [rax], xmm0
    movq xmm0, qword [rax + 8]
    movq xmm4, qword [r10 + 8]
    BI8
    movq qword [rax + 8], xmm0
    add rax, rdx
    add r10, rdx
    dec r9d
    jnz .b16
.bdone:
    REST67
    ret
