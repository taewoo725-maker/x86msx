; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; entry.asm - 32비트 본체 진입점과 인터럽트 진입부
; 로더가 보호 모드(CS=0x08, DS=0x10)로 0x100000에 점프한다.
bits 32

section .text.start
global _start
extern kmain
extern __bss_start, __bss_end
_start:
    mov esp, 0x90000
    cld
    ; 본체 자체 GDT (로더 메모리는 이후 BIOS 썽크 영역으로 쓰임)
    lgdt [kgdt_ptr]
    jmp 0x08:.reload
.reload:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .text
extern irq0_handler, irq1_handler, exc_handler

global isr_irq0, isr_irq1, isr_spurious
isr_irq0:
    pushad
    cld
    call irq0_handler
    popad
    iretd
isr_irq1:
    pushad
    cld
    call irq1_handler
    popad
    iretd
isr_spurious:                   ; 마스크된 IRQ가 들어와도 EOI만
    push eax
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    pop eax
    iretd

; CPU 예외 0~31: 번호를 넘겨 오류 화면을 띄우고 멈춤 (돌아가지 않음)
%macro EXC 1
global isr_exc%1
isr_exc%1:
    cli
    push dword %1
    call exc_handler
.h%1:
    hlt
    jmp .h%1
%endmacro
%assign i 0
%rep 32
EXC i
%assign i i+1
%endrep

section .data
align 8
kgdt:
    dq 0
    dw 0xFFFF, 0x0000
    db 0x00, 0x9A, 0xCF, 0x00
    dw 0xFFFF, 0x0000
    db 0x00, 0x92, 0xCF, 0x00
kgdt_end:
kgdt_ptr:
    dw kgdt_end - kgdt - 1
    dd kgdt

; BIOS 호출 썽크 (boot/thunk.asm, 0x9000에 복사해 실행)
global thunk_bin, thunk_bin_end
thunk_bin:
    incbin "build/thunk.bin"
thunk_bin_end:

section .rodata
global isr_exc_table
isr_exc_table:
%assign i 0
%rep 32
    dd isr_exc %+ i
%assign i i+1
%endrep
