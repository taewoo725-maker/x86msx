; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; mpwake_tramp.asm - 8.0.6: 깨운 코어(AP)가 처음 실행하는 코드 (1 MiB 아래 4 KiB 한 쪽, SIPI 가 이 쪽의 번호를 알려 줌).
;   16비트 실제 모드 → 32비트 보호 모드 → 64비트 모드로 올라가서, 자기 APIC ID 를 적고 "깨어남" 표시를 쓴 뒤 HLT 로 멈춘다.
;   펌웨어·메인 프로그램 메모리는 건드리지 않는다 (이 쪽 + 같이 잡은 쪽 3개[페이지 표 PML4/PDPT/PD]만 씀).
;   만드는 법: nasm -f bin uefi/mpwake_tramp.asm -o build/mpwake_tramp.bin  (Makefile 이 uefi/mpwake_tramp.inc 로 바꿈)
; 쪽 배치 (base = 이 쪽의 실제 주소):
;   +0x000 코드   +0x800 GDT(32바이트)   +0x820 GDTR   +0x828 FP32   +0x830 FP64   +0x840 FLAG   +0x844 APICID
;   +0x1000 PML4  +0x2000 PDPT  +0x3000 PD (0~1 GiB 를 2 MiB 쪽으로 항등 사상)  ← 부르는 쪽(mpwake.c)이 채움
;   스택 = base+0xF00 아래로
%define GDT     0x800
%define GDTR    0x820
%define FP32    0x828
%define FP64    0x830
%define FLAG    0x840
%define APICID  0x844
%define P_CR3   0x850                   ; 8.0.9: 일꾼 모드 입력 (부르는 쪽이 채움)
%define P_PAT   0x858
%define P_CTX   0x860                   ; 0 이면 예전 시험 모드(표시만 쓰고 HLT), 아니면 MpWCtx 주소
%define P_IDTR  0x870                   ; dw 한계, dq 주소 (10바이트)
%define P_MXCSR 0x880                   ; dd 0x1F80
%define DEAD    0x700                   ; 예외가 나면 여기서 영원히 멈춤 (cli; hlt)
%define MAGIC   0xA5A5A5A5

bits 16
org 0
start:
    cli
    cld
    mov ax, cs
    mov ds, ax
    xor ebx, ebx
    mov bx, ax
    shl ebx, 4                      ; ebx = base
    mov eax, ebx
    add eax, GDT
    mov [GDTR + 2], eax             ; GDTR 의 시작 주소
    mov eax, ebx
    add eax, pm32
    mov [FP32], eax                 ; 32비트로 건너뛸 주소
    lgdt [GDTR]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword far [FP32]

bits 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr4
    or eax, 0x20                    ; PAE
    mov cr4, eax
    lea eax, [ebx + 0x1000]
    mov cr3, eax                    ; PML4
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100                   ; EFER.LME
    wrmsr
    mov eax, cr0
    or eax, 0x80000000              ; 쪽 나누기 켜기 → 호환 모드
    mov cr0, eax
    lea eax, [ebx + lm64]
    mov [ebx + FP64], eax
    jmp dword far [ebx + FP64]

bits 64
lm64:
    mov r9d, ebx                    ; base (cpuid 가 ebx 를 지우므로 옮겨 둠)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    lea rsp, [r9 + 0xF00]
    xor eax, eax
    cpuid
    cmp eax, 0xB
    jb .leaf1
    mov eax, 0xB
    xor ecx, ecx
    cpuid
    mov r8d, edx                    ; x2APIC ID (전체 32비트)
    jmp .got
.leaf1:
    mov eax, 1
    cpuid
    shr ebx, 24
    mov r8d, ebx
.got:
    mov [r9 + APICID], r8d
    mov r13, [r9 + P_CTX]
    test r13, r13
    jnz .worker
    mov dword [r9 + FLAG], MAGIC    ; "깨어남" (시험 모드)
.halt:
    cli
    hlt
    jmp .halt

.worker:                            ; 8.0.9 일꾼 모드: 메인 프로그램의 페이지 표·PAT 로 바꾸고 C 함수를 부른다
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x800                   ; EFER.NXE — 메인 코어의 페이지 표가 NX(실행 금지) 비트를 쓰면 이 비트가 꺼져 있을 때 "예약 비트" 오류가 남
    wrmsr
    mov rax, [r9 + P_CR3]
    mov cr3, rax                    ; 메인 코어와 같은 사상 (전 메모리 항등) — 이 쪽 코드도 계속 보임
    mov rax, [r9 + P_PAT]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0x277
    wrmsr                           ; 메모리 종류 표를 메인 코어와 같게
    mov rax, cr0
    or eax, 0x22                    ; MP, NE
    and eax, 0xFFFFFFFB             ; EM 끔
    mov cr0, rax
    mov rax, cr4
    or eax, 0x600                   ; OSFXSR, OSXMMEXCPT (SSE 사용)
    mov cr4, rax
    fninit
    ldmxcsr [r9 + P_MXCSR]
    lidt [r9 + P_IDTR]
    mov [r13 + 4], r8d              ; 자기 APIC ID
    mov rsp, [r13 + 8]              ; 자기 스택
    mov rcx, [r13 + 24]             ; 인수 (UEFI 호출 규약: 첫 인수 = rcx)
    mfence
    mov dword [r13], 1              ; 시작함 — 이 뒤로 부르는 쪽이 이 쪽의 입력 칸을 다른 코어용으로 바꿈
    sub rsp, 32
    call qword [r13 + 16]
    mov dword [r13], 2              ; 끝남 (r13 은 부른 함수가 보존함)
.halt2:
    cli
    hlt
    jmp .halt2

times DEAD - ($ - $$) db 0
dead:                               ; 예외가 나면 여기로: 일꾼 칸에 0xDEAD 와 원인을 적고 멈춤 (r13 = 칸, 0 이면 그냥 멈춤)
    cli
    test r13, r13
    jz .d
    mov rax, cr2
    mov [r13 + 32], rax
    mov rax, [rsp]
    mov [r13 + 40], rax
    mov rax, [rsp + 8]
    mov [r13 + 48], rax
    mov dword [r13], 0xDEAD
.d:
    hlt
    jmp .d

times GDT - ($ - $$) db 0
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF           ; 0x08 코드 32
    dq 0x00CF92000000FFFF           ; 0x10 데이터
    dq 0x00AF9A000000FFFF           ; 0x18 코드 64
times GDTR - ($ - $$) db 0
    dw 31
    dd 0
times FP32 - ($ - $$) db 0
    dd 0
    dw 0x08
times FP64 - ($ - $$) db 0
    dd 0
    dw 0x18
times FLAG - ($ - $$) db 0
    dd 0
    dd 0
times P_MXCSR - ($ - $$) db 0
    dd 0x1F80
times 0x1000 - ($ - $$) db 0
