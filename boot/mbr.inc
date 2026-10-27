; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
; MBR: kbtest.asm과 동일. 2단계를 0x7E00에 적재하고 stage2_entry로 이동
; 사용하는 쪽에서 org 0x7C00, bits 16, stage2/stage2_entry/image_end 정의 필요
; ---------------------------------------------------------------------
; 섹터 0: MBR
; ---------------------------------------------------------------------
mbr:
    jmp short mbr_start
    nop
    ; BPB 자리. 일부 펌웨어는 USB 메모리를 플로피로 에뮬레이션할 때
    ; 이 영역(드라이브 번호 등)에 값을 써넣으므로 코드를 두지 않는다.
    ; (값은 0으로 둠: Windows가 파티션 없는 디스크로 오해하지 않도록)
    times 0x5A-($-$$) db 0  ; FAT32 BPB 크기만큼 비워둠

mbr_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl

    ; INT 13h 확장(LBA) 지원 확인
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .chs
    cmp bx, 0xAA55
    jne .chs
    test cl, 1
    jz .chs
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .chs
    mov byte [load_method], 1
    jmp stage2_entry

.chs:
    ; 확장이 없으면 CHS로 한 섹터씩 읽는다
    mov dl, [boot_drive]
    mov ah, 0x08
    xor di, di
    int 0x13
    jc boot_fail
    xor ax, ax
    mov es, ax              ; AH=08h가 ES를 바꿀 수 있음
    and cx, 0x3F
    mov [spt], cx
    movzx dx, dh
    inc dx
    mov [heads], dx
    mov word [cur_lba], 1
    mov bx, 0x7E00
.loop:
    mov ax, [cur_lba]
    xor dx, dx
    div word [spt]
    mov cl, dl
    inc cl                  ; 섹터 (1부터)
    xor dx, dx
    div word [heads]        ; ax = 실린더, dx = 헤드
    mov dh, dl
    mov ch, al
    shl ah, 6
    or cl, ah
    mov dl, [boot_drive]
    mov ax, 0x0201
    int 0x13
    jc boot_fail
    add bx, 512
    inc word [cur_lba]
    cmp word [cur_lba], STAGE2_SECTORS
    jbe .loop
    mov byte [load_method], 2
    jmp stage2_entry

boot_fail:
    mov si, msg_boot_fail
.p: lodsb
    test al, al
    jz .h
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    jmp .p
.h: hlt
    jmp .h

msg_boot_fail db 'KBTEST: disk read error', 0

dap:
    db 0x10, 0
    dw STAGE2_SECTORS
    dw 0x7E00, 0
    dq 1

boot_drive  db 0
load_method db 0            ; 1 = LBA 확장, 2 = CHS
spt         dw 0
heads       dw 0
cur_lba     dw 0

    times 0x1BE-($-$$) db 0 ; 여기서 음수가 되면 MBR 코드가 너무 큰 것

    ; 파티션 표: tools/mkimage.py가 FAT32 파티션(LBA 2048~)으로 채운다
    times 64 db 0
    dw 0xAA55

