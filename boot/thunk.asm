; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; =====================================================================
; thunk.asm - 32비트 보호 모드(페이징 가능)에서 실모드 BIOS 인터럽트 호출
;   0x9000에 복사해 쓰는 평면 바이너리. 본체(kernel/bios.c)가 call 0x9000.
;   인자/결과: 0x8E00의 BiosRegs (bios.c와 일치)
;   BIOS를 부르는 동안: PIC는 BIOS 설정(벡터 0x08/0x70, 원래 마스크 + 키보드·마우스 막음),
;   PIT 채널 0은 BIOS 기본(18.2 Hz). 돌아오면 본체 설정(0x20/0x28, 1 kHz)으로 복구.
; =====================================================================
bits 32
org 0x9000

REGS        equ 0x8E00
R_EAX       equ REGS+0
R_EBX       equ REGS+4
R_ECX       equ REGS+8
R_EDX       equ REGS+12
R_ESI       equ REGS+16
R_EDI       equ REGS+20
R_DS        equ REGS+24
R_ES        equ REGS+26
R_FLAGS     equ REGS+28
R_INT       equ REGS+32
R_PICM      equ REGS+33     ; BIOS 때의 PIC 마스크
R_PICS      equ REGS+34
RM_STACK    equ 0x8C00

entry:
    pushfd
    cli
    pushad
    mov [saved_esp], esp
    sgdt [saved_gdt]
    sidt [saved_idt]
    mov eax, cr0
    mov [saved_cr0], eax
    mov eax, cr3
    mov [saved_cr3], eax
    and eax, eax
    mov eax, cr0
    and eax, 0x7FFFFFFF         ; 페이징 끄기 (1:1 매핑이라 안전)
    mov cr0, eax
    lgdt [tgdt_ptr]
    jmp 0x18:pm16

bits 16
pm16:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp 0x0000:rm

rm:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, RM_STACK
    lidt [rm_idt]

    ; PIC → BIOS 설정
    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    mov al, 0x08
    out 0x21, al
    mov al, 0x70
    out 0xA1, al
    mov al, 4
    out 0x21, al
    mov al, 2
    out 0xA1, al
    mov al, 1
    out 0x21, al
    out 0xA1, al
    mov al, [R_PICM]
    or al, 0x02                 ; 키보드 IRQ1 막음 (BIOS가 키를 가져가지 않게)
    out 0x21, al
    mov al, [R_PICS]
    or al, 0x10                 ; 마우스 IRQ12 막음
    out 0xA1, al
    ; PIT → BIOS 기본 (모드 3, 분주 65536)
    mov al, 0x36
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al

    ; 인터럽트 번호 넣기 (자기 수정 코드)
    mov al, [R_INT]
    mov [intno + 1], al
    mov eax, [R_EAX]
    mov ebx, [R_EBX]
    mov ecx, [R_ECX]
    mov edx, [R_EDX]
    mov esi, [R_ESI]
    mov edi, [R_EDI]
    push word [R_DS]
    push word [R_ES]
    pop es
    pop ds
    sti
    jmp short intno             ; 수정한 명령을 다시 읽도록
intno:
    int 0x13
    cli
    pushfd
    push ds
    push es
    push eax
    xor ax, ax
    mov ds, ax
    pop eax
    mov [R_EAX], eax
    mov [R_EBX], ebx
    mov [R_ECX], ecx
    mov [R_EDX], edx
    mov [R_ESI], esi
    mov [R_EDI], edi
    pop word [R_ES]
    pop word [R_DS]
    pop dword [R_FLAGS]
    xor ax, ax
    mov es, ax

    ; PIC → 본체 설정 (IRQ0-7 → 0x20, IRQ8-15 → 0x28, 타이머·키보드만)
    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xA1, al
    mov al, 4
    out 0x21, al
    mov al, 2
    out 0xA1, al
    mov al, 1
    out 0x21, al
    out 0xA1, al
    mov al, 0xFC
    out 0x21, al
    mov al, 0xFF
    out 0xA1, al
    ; PIT → 본체 (모드 2, 분주 1193 = 약 1 kHz)
    mov al, 0x34
    out 0x43, al
    mov al, 1193 & 0xFF
    out 0x40, al
    mov al, 1193 >> 8
    out 0x40, al

    ; 보호 모드로
    lgdt [tgdt_ptr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp dword 0x08:pm32

bits 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    lgdt [saved_gdt]            ; 본체 GDT (같은 0x08/0x10 구성)
    lidt [saved_idt]
    mov eax, [saved_cr3]
    mov cr3, eax
    mov eax, [saved_cr0]        ; 페이징 원래대로
    mov cr0, eax
    mov esp, [saved_esp]
    popad
    popfd
    ret

align 8
tgdt:
    dq 0
    dw 0xFFFF, 0x0000           ; 0x08: 32비트 코드
    db 0x00, 0x9A, 0xCF, 0x00
    dw 0xFFFF, 0x0000           ; 0x10: 32비트 데이터
    db 0x00, 0x92, 0xCF, 0x00
    dw 0xFFFF, 0x0000           ; 0x18: 16비트 코드 (64 KiB)
    db 0x00, 0x9A, 0x00, 0x00
    dw 0xFFFF, 0x0000           ; 0x20: 16비트 데이터 (64 KiB)
    db 0x00, 0x92, 0x00, 0x00
tgdt_end:
tgdt_ptr:
    dw tgdt_end - tgdt - 1
    dd tgdt
rm_idt:
    dw 0x3FF
    dd 0
align 4
saved_esp   dd 0
saved_cr0   dd 0
saved_cr3   dd 0
saved_gdt   dw 0
            dd 0
saved_idt   dw 0
            dd 0
