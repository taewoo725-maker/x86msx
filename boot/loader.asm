; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; =====================================================================
; loader.asm - MSX1 에뮬레이터 부트 로더 (레거시 BIOS / CSM)
;
; 섹터 0  : MBR (mbr.inc, KBTEST/VBETEST와 동일)
; 섹터 1~ : 이 2단계 로더
; (MBR과 로더는 파티션 앞 빈 공간, LBA 2048 미만)
; LBA 2048~: FAT32 파티션. \X86MSX.BIN(없으면 \MSXEMU.BIN)을 읽는다.
;
; 하는 일 (3.0)
;   1. A20 활성화, 언리얼 모드(FS 4 GiB)
;   2. FAT32 파티션에서 \X86MSX.BIN(없으면 \MSXEMU.BIN)을 0x100000에 (본체는 8 MiB까지).
;      기종 ROM·카트리지·매체는 본체가 실행 중에 읽는다.
;   3. INT 15h E801로 1 MiB 위 연속 메모리 끝을 재어 부팅 정보에 남김 (본체가 메모리 배치 확인)
;   4. VBE 모드 선택: 640x480 ~ 1920x1200의 선형 8비트·32비트 모드 중 MSX 화면이 가장 크게 나오는 것
;   5. 부팅 정보(0x0500)와 적재 목록(0x0600)을 남기고 32비트 보호 모드로 본체 실행
; =====================================================================

bits 16
org 0x7C00

BOOTINFO    equ 0x0500
HDRCOPY     equ 0x0600
VBEINFO     equ 0x1000
MODEINFO    equ 0x1200
BUFSEG      equ 0x1000          ; 디스크 읽기 버퍼 0x10000 (32 KiB)
BUFADDR     equ 0x10000
CHUNK       equ 64              ; 한 번에 읽을 섹터 수
FATSEG      equ 0x1800          ; FAT 섹터 캐시 0x18000
KERNEL_ADDR equ 0x100000
KERNEL_MAX  equ 0x800000        ; 본체 파일 한도 8 MiB (3.0, 2.x는 1 MiB)

; 부팅 정보 (kernel/bootinfo.h와 일치)
BI_MAGIC    equ BOOTINFO+0      ; 'BOOT'
BI_LFB      equ BOOTINFO+4
BI_PITCH    equ BOOTINFO+8
BI_WIDTH    equ BOOTINFO+12
BI_HEIGHT   equ BOOTINFO+16
BI_MODE     equ BOOTINFO+20
BI_DRIVE    equ BOOTINFO+24
BI_MEMEND   equ BOOTINFO+28     ; 3.0: 1 MiB 위 연속 메모리 끝 주소 (0 = 모름)
BI_BPP      equ BOOTINFO+32     ; 8 또는 32
BI_RPOS     equ BOOTINFO+36     ; 32비트일 때 빨강 비트 위치

%include "mbr.inc"

; ---------------------------------------------------------------------
stage2:
stage2_entry:
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov si, msg_hello
    call print

    ; A20
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    call enter_unreal

    ; FAT32 파티션에서 본체와 ROM 읽기 (설계서 §8)
    call fat_init
    jc err_fat
    mov dword [HDRCOPY], 'MSXP'
    mov si, path_kernel                 ; \X86MSX.BIN → 0x100000
    mov edi, KERNEL_ADDR
    call load_path
    jnc .kernel_ok
    mov si, path_kernel_old             ; 예전 이름 \MSXEMU.BIN (기존 USB 호환)
    mov edi, KERNEL_ADDR
    call load_path
    jc err_file
.kernel_ok:
    mov [HDRCOPY+16+4], edx             ; 항목 0: 본체
    mov dword [HDRCOPY+16+8], KERNEL_ADDR
    mov dword [HDRCOPY+4], 1            ; 본체만 (기종 ROM과 매체는 본체가 실행 중 읽음)

    mov si, msg_loaded
    call print

    ; 메모리 크기 (3.0)
    call mem_top
    mov [BI_MEMEND], eax

    ; VBE 모드 선택
    call choose_mode
    jc err_vbe

    mov dword [BI_MAGIC], 'BOOT'
    movzx eax, byte [boot_drive]
    mov [BI_DRIVE], eax

    ; 모드 설정 (선형 프레임 버퍼)
    mov bx, [BI_MODE]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne err_vbe

    ; 32비트 보호 모드
    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:pm_entry

bits 32
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov eax, 0x100000
    jmp eax

bits 16
; ---------------------------------------------------------------------
; 디스크
; ---------------------------------------------------------------------
; eax = LBA, cx = 섹터 수 (<= CHUNK) -> 0x10000. 실패 시 CF
read_lba:
    push di
    mov di, BUFSEG
    call read_lba_seg
    pop di
    ret

; eax = LBA, cx = 섹터 수, di = 버퍼 세그먼트 (오프셋 0). CF 외 레지스터 보존
read_lba_seg:
    push eax
    push edx
    push si
    mov [ld_dap+2], cx
    mov word [ld_dap+4], 0
    mov [ld_dap+6], di
    mov [ld_dap+8], eax
    mov dword [ld_dap+12], 0
    mov si, ld_dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    pop si
    pop edx
    pop eax
    ret

; eax = 시작 LBA, edx = 바이트 수, edi = 적재 주소
load_blob:
.next:
    test edx, edx
    jz .ok
    mov ecx, edx
    add ecx, 511
    shr ecx, 9                  ; 남은 섹터 수
    cmp ecx, CHUNK
    jbe .n
    mov ecx, CHUNK
.n:
    push eax
    push edx
    push edi
    push ecx
    call read_lba
    pop ecx
    pop edi
    pop edx
    pop eax
    jc .fail
    push ecx
    shl ecx, 9                  ; 바이트
    cmp ecx, edx
    jbe .c
    mov ecx, edx
.c:
    push eax
    push edx
    mov esi, BUFADDR
    push ecx
    call copy_high              ; edi는 복사한 만큼 증가
    pop ecx
    pop edx
    pop eax
    sub edx, ecx
    pop ecx
    add eax, ecx
    jmp .next
.ok:
    clc
    ret
.fail:
    stc
    ret

; esi -> edi, ecx 바이트 (언리얼 모드 FS). edi는 끝 위치로 갱신
copy_high:
    jecxz .d
.l: mov al, [fs:esi]
    mov [fs:edi], al
    inc esi
    inc edi
    dec ecx
    jnz .l
.d: ret

; ---------------------------------------------------------------------
; VBE: 1366x768x8 선형 우선, 없으면 1024x768x8 선형. 선택 결과를 부팅 정보에 기록
; ---------------------------------------------------------------------
; 화면 모드 고르기: 선형 프레임 버퍼, 8비트(팔레트) 또는 32비트(직접 색), 640x480 ~ 1920x1200.
; 3.3.2: 그 범위에 없으면 둘째 바퀴에서 640x480 이상(가로 4096까지) 중 가장 작은 모드
; 점수 = (MSX 화면 정수배 << 24) + (넓이 >> 4) + (8비트면 1)  → 크게, 넓게, 같으면 8비트
choose_mode:
    mov di, VBEINFO
    mov dword [di], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov byte [relax], 0
    mov byte [panel_pref], 0
    mov dx, 0xCF8                   ; 인텔 HD 4000 노트북(8086:0166, 00:02.0)은 내장 패널(1366x768)에 1:1로 나오는 모드를 우선
    mov eax, 0x80001000
    out dx, eax
    mov dx, 0xCFC
    in eax, dx
    cmp eax, 0x01668086
    jne .scan
    mov byte [panel_pref], 1
.scan:
    mov word [best_mode], 0xFFFF
    mov dword [best_score], 0
    mov si, [VBEINFO+0x0E]
    mov ax, [VBEINFO+0x10]
    mov gs, ax
    mov bp, 512
.next:
    dec bp
    jz .done
    mov cx, [gs:si]
    cmp cx, 0xFFFF
    je .done
    add si, 2
    push si
    push gs
    push bp
    push cx
    mov di, MODEINFO
    mov ax, 0x4F01
    int 0x10
    pop cx
    pop bp
    pop gs
    pop si
    cmp ax, 0x004F
    jne .next
    mov ax, [MODEINFO]
    and ax, 0x0091              ; 지원 + 그래픽 + 선형
    cmp ax, 0x0091
    jne .next
    mov al, [MODEINFO+0x19]     ; 픽셀당 비트
    cmp al, 8
    je .bpp_ok
    cmp al, 32
    jne .next
    cmp byte [MODEINFO+0x1B], 6 ; 직접 색
    jne .next
.bpp_ok:
    movzx ebx, word [MODEINFO+0x12]     ; 가로
    movzx edx, word [MODEINFO+0x14]     ; 세로
    cmp ebx, 640
    jb .next
    cmp edx, 480
    jb .next
    cmp byte [panel_pref], 0
    je .nopp
    cmp ebx, 1366
    jne .nopp
    cmp edx, 768
    jne .nopp
    mov eax, 0x70000000             ; 패널 크기 모드가 있으면 무조건 1등 (8비트면 +1)
    cmp byte [MODEINFO+0x19], 8
    jne .cmp
    inc eax
    jmp .cmp
.nopp:
    cmp byte [relax], 0             ; 3.3.2 PLAT-02: 둘째 바퀴는 크기 제한 없이 가장 작은 모드
    jne .relaxed
    cmp ebx, 1920
    ja .next
    cmp edx, 1200
    ja .next
    ; 배율 = min(가로/256, 세로/192)
    push edx
    mov eax, ebx
    shr eax, 8
    mov edi, eax
    mov eax, edx
    xor edx, edx
    push ecx
    mov ecx, 192
    div ecx
    pop ecx
    pop edx
    cmp eax, edi
    jb .s_ok
    mov eax, edi
.s_ok:
    shl eax, 24
    push eax
    mov eax, ebx
    imul eax, edx
    shr eax, 4
    pop edi
    add eax, edi
    cmp byte [MODEINFO+0x19], 8
    jne .cmp
    inc eax
.cmp:
    cmp eax, [best_score]
    jbe .next
    mov [best_score], eax
    mov [best_mode], cx
    jmp .next
.relaxed:
    cmp ebx, 4096
    ja .next
    mov eax, ebx
    imul eax, edx
    shr eax, 4
    neg eax
    add eax, 0x7FFFFFFF
    jmp .cmp
.done:
    mov cx, [best_mode]
    cmp cx, 0xFFFF
    jne .found
    cmp byte [relax], 0
    jne .fail
    mov byte [relax], 1
    jmp .scan
.found:
    ; 선택한 모드 정보를 다시 읽어 기록
    push cx
    mov di, MODEINFO
    mov ax, 0x4F01
    int 0x10
    pop cx
    cmp ax, 0x004F
    jne .fail
    mov [BI_MODE], cx
    mov word [BI_MODE+2], 0
    mov eax, [MODEINFO+0x28]
    mov [BI_LFB], eax
    movzx eax, word [MODEINFO+0x12]
    mov [BI_WIDTH], eax
    movzx eax, word [MODEINFO+0x14]
    mov [BI_HEIGHT], eax
    movzx eax, byte [MODEINFO+0x19]
    mov [BI_BPP], eax
    movzx eax, byte [MODEINFO+0x20]          ; 빨강 위치 (16이면 B,G,R,x 순서)
    mov [BI_RPOS], eax
    movzx eax, word [MODEINFO+0x10]          ; BytesPerScanLine
    cmp word [VBEINFO+4], 0x0300
    jb .p
    cmp word [MODEINFO+0x32], 0              ; LinBytesPerScanLine (VBE 3.0)
    je .p
    movzx eax, word [MODEINFO+0x32]
.p: mov [BI_PITCH], eax
    clc
    ret
.fail:
    stc
    ret

; ---------------------------------------------------------------------
enter_unreal:
    cli
    push eax
    push bx
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp short .pm
.pm:
    mov bx, 0x10
    mov fs, bx
    and al, 0xFE
    mov cr0, eax
    jmp short .rm
.rm:
    xor bx, bx
    mov fs, bx
    pop bx
    pop eax
    sti
    ret

print:
    lodsb
    test al, al
    jz .d
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    jmp print
.d: ret

; ---------------------------------------------------------------------
; FAT32 (읽기 전용). 섹터 크기 512만 지원.
; ---------------------------------------------------------------------
; MBR에서 FAT32 파티션(형식 0x0B/0x0C)을 찾고 BPB를 읽는다. 실패 시 CF
fat_init:
    xor eax, eax
    mov cx, 1
    call read_lba
    jc .fail
    push es
    mov ax, BUFSEG
    mov es, ax
    mov si, 0x1BE
    mov cx, 4
.p: mov al, [es:si+4]
    cmp al, 0x0B
    je .found
    cmp al, 0x0C
    je .found
    add si, 16
    loop .p
    pop es
    stc
    ret
.found:
    mov eax, [es:si+8]
    mov [part_lba], eax
    pop es
    mov eax, [part_lba]
    mov cx, 1
    call read_lba
    jc .fail
    push es
    mov ax, BUFSEG
    mov es, ax
    cmp word [es:0x0B], 512
    jne .bad
    movzx eax, byte [es:0x0D]
    test eax, eax
    jz .bad
    cmp eax, CHUNK
    ja .bad
    mov [spc], eax
    movzx eax, word [es:0x0E]
    add eax, [part_lba]
    mov [fat_lba], eax
    mov eax, [es:0x24]
    movzx ecx, byte [es:0x10]
    imul eax, ecx
    add eax, [fat_lba]
    mov [data_lba], eax
    mov eax, [es:0x2C]
    mov [root_clus], eax
    mov dword [fat_cached], 0xFFFFFFFF
    pop es
    clc
    ret
.bad:
    pop es
.fail:
    stc
    ret

; eax = 클러스터 -> eax = LBA
clus_lba:
    sub eax, 2
    imul eax, [spc]
    add eax, [data_lba]
    ret

; eax = 클러스터 -> eax = 다음 클러스터 (0x0FFFFFF8 이상이면 끝). 실패 시 CF
next_clus:
    push ebx
    push ecx
    push di
    mov ebx, eax
    shr eax, 7
    add eax, [fat_lba]
    cmp eax, [fat_cached]
    je .have
    mov [fat_cached], eax
    mov cx, 1
    mov di, FATSEG
    push ebx
    call read_lba_seg
    pop ebx
    jc .err
.have:
    and ebx, 0x7F
    shl ebx, 2
    push es
    mov ax, FATSEG
    mov es, ax
    mov eax, [es:bx]
    pop es
    and eax, 0x0FFFFFFF
    pop di
    pop ecx
    pop ebx
    clc
    ret
.err:
    mov dword [fat_cached], 0xFFFFFFFF
    pop di
    pop ecx
    pop ebx
    stc
    ret

; eax = 디렉터리 클러스터, si = 11바이트 8.3 이름 -> eax = 첫 클러스터, edx = 크기. 없으면 CF
find_entry:
    mov [fe_name], si
.clus:
    cmp eax, 2
    jb .nf
    cmp eax, 0x0FFFFFF8
    jae .nf
    mov [fe_clus], eax
    call clus_lba
    mov ecx, [spc]
    call read_lba
    jc .nf
    mov ecx, [spc]
    shl ecx, 4                  ; 클러스터당 항목 수
    push es
    mov ax, BUFSEG
    mov es, ax
    xor di, di
.ent:
    mov al, [es:di]
    test al, al
    jz .end                     ; 디렉터리 끝
    cmp al, 0xE5
    je .next                    ; 지운 항목
    mov al, [es:di+11]
    cmp al, 0x0F
    je .next                    ; 긴 이름 항목
    test al, 0x08
    jnz .next                   ; 볼륨 이름
    push di
    push ecx
    mov si, [fe_name]
    mov cx, 11
    repe cmpsb
    pop ecx
    pop di
    je .found
.next:
    add di, 32
    dec ecx
    jnz .ent
    pop es
    mov eax, [fe_clus]
    call next_clus
    jc .nf
    jmp .clus
.found:
    movzx eax, word [es:di+0x14]
    shl eax, 16
    mov ax, [es:di+0x1A]
    mov edx, [es:di+0x1C]
    pop es
    clc
    ret
.end:
    pop es
.nf:
    stc
    ret

; eax = 첫 클러스터, edx = 크기, edi = 적재 주소. 실패 시 CF
load_file:
.c: test edx, edx
    jz .ok
    mov byte [lp_stage], 6          ; 클러스터 범위 밖
    cmp eax, 2
    jb .bad
    cmp eax, 0x0FFFFFF8
    jae .bad
    mov [lf_clus], eax
    push edx
    push edi
    call clus_lba
    mov ecx, [spc]
    call read_lba
    pop edi
    pop edx
    mov byte [lp_stage], 7          ; 데이터 읽기 실패
    jc .bad
    mov ecx, [spc]
    shl ecx, 9
    cmp ecx, edx
    jbe .n
    mov ecx, edx
.n: sub edx, ecx
    mov esi, BUFADDR
    push edx
    call copy_high
    pop edx
    mov eax, [lf_clus]
    mov byte [lp_stage], 8          ; FAT 읽기 실패
    call next_clus
    jc .bad
    jmp .c
.ok:
    clc
    ret
.bad:
    mov [lf_left], edx
    stc
    ret

; si = 경로 설명 {단계 수 1바이트, 이름11 x 단계 수, 표시용 문자열}
; -> edi에 적재, edx = 크기. 실패 시 CF (lp_disp에 표시용 문자열)
load_path:
    mov [lp_dest], edi
    mov byte [lp_stage], 1
    movzx cx, byte [si]
    inc si
    mov [lp_desc], si
    mov eax, [root_clus]
.lp:
    push cx
    push si
    call find_entry
    pop si
    pop cx
    jc .nf
    add si, 11
    inc byte [lp_stage]
    loop .lp
    mov [lp_disp], si
    test edx, edx
    jz .f
    cmp edx, KERNEL_MAX             ; 본체 한도 (3.0: 8 MiB)
    ja .f
    mov edi, [lp_dest]
    push edx
    call load_file
    pop edx
    ret
.nf:
    movzx cx, cl                    ; 남은 단계를 건너뛰어 표시용 문자열 위치 계산
    dec cx
    imul cx, cx, 11
    add si, 11
    add si, cx
    mov [lp_disp], si
.f: stc
    ret

; ---------------------------------------------------------------------
; BIOS에 1 MiB 위 메모리 크기를 물어 연속 메모리 끝 주소 (eax, 모르면 0)
mem_top:
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jc .unknown
    test ax, ax                     ; 일부 BIOS는 CX/DX에 돌려줌
    jnz .ab
    mov ax, cx
    mov bx, dx
.ab:
    test bx, bx
    jz .low
    movzx eax, bx                   ; 16 MiB 위 64 KiB 블록 수
    shl eax, 16
    add eax, 0x1000000
    ret
.low:
    movzx eax, ax                   ; 1~16 MiB 사이 KiB 수
    shl eax, 10
    add eax, 0x100000
    ret
.unknown:
    xor eax, eax
    ret

err_fat:    mov si, msg_err_fat
            jmp die
err_file:   mov si, msg_err_file
            call print
            mov si, [lp_disp]
            call print
            ; 진단 값: 파티션, 데이터 시작, 루트, 마지막 디렉터리 클러스터, 단계
            mov eax, [part_lba]
            call phex
            mov eax, [data_lba]
            call phex
            mov eax, [root_clus]
            call phex
            mov eax, [fe_clus]
            call phex
            movzx eax, byte [lp_stage]
            call phex
            mov eax, [lf_clus]
            call phex
            mov eax, [lf_left]
            call phex
            mov si, msg_crlf
            jmp die

; eax를 16진수 8자리로 출력
phex:
    push eax
    mov al, ' '
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop eax
    mov cx, 8
.l: rol eax, 4
    push eax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .o
    add al, 7
.o: mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop eax
    loop .l
    ret
err_disk:   mov si, msg_err_disk
            jmp die
err_header: mov si, msg_err_header
            jmp die
err_vbe:    mov si, msg_err_vbe
die:
    call print
.h: hlt
    jmp .h

align 8
gdt:
    dq 0
    dw 0xFFFF, 0x0000           ; 0x08: 32비트 코드, 0~4 GiB
    db 0x00, 0x9A, 0xCF, 0x00
    dw 0xFFFF, 0x0000           ; 0x10: 32비트 데이터, 0~4 GiB
    db 0x00, 0x92, 0xCF, 0x00
gdt_end:
gdtr:
    dw gdt_end - gdt - 1
    dd gdt

align 4
ld_dap:
    db 0x10, 0
    dw 0, 0, 0
    dq 0
best_mode       dw 0
relax           db 0
panel_pref      db 0
align 4
best_score      dd 0
align 4
part_lba        dd 0
spc             dd 0
fat_lba         dd 0
data_lba        dd 0
root_clus       dd 0
fat_cached      dd 0
fe_clus         dd 0
lf_clus         dd 0
lp_dest         dd 0
lf_left         dd 0
fe_name         dw 0
lp_desc         dw 0
lp_disp         dw 0
lp_stage        db 0
msg_crlf        db 13, 10, 0
align 4
path_kernel     db 1, 'X86MSX  BIN', '\X86MSX.BIN', 13, 10, 0
path_kernel_old db 1, 'MSXEMU  BIN', '\MSXEMU.BIN', 13, 10, 0

msg_hello       db 'X86MSX loader', 13, 10, 0
msg_loaded      db 'X86MSX loaded.', 13, 10, 0
msg_err_disk    db 'Disk read error.', 13, 10, 0
msg_err_fat     db 'FAT32 partition not found or unsupported.', 13, 10, 0
msg_err_file    db 'File missing or too large: ', 0
msg_err_header  db 'Bad image: payload header not found.', 13, 10, 0
msg_err_vbe     db 'No usable VESA linear video mode (8-bit or 32-bit, 640x480 or larger).', 13, 10, 0

    times (512 - ($-$$) % 512) % 512 db 0
image_end:

STAGE2_SECTORS equ (image_end - stage2) / 512
