/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * profile.h - 기종 프로필 (설계서 §1.1, §1.3)
 * blueMSX 형식 config.ini를 읽는다. 블록 단위는 8 KiB.
 */
#ifndef MSX_PROFILE_H
#define MSX_PROFILE_H

#include <stdint.h>
#include <stddef.h>

#define MAX_REGIONS 16
#define MAX_UNSUPPORTED 16

typedef enum {
    REGION_RAM = 1,
    REGION_ROM_NORMAL,
    REGION_ROM_BASIC,
    REGION_RAM_MAPPER,          /* 메모리 매퍼 RAM (포트 0xFC~0xFF) */
    REGION_KANJI,               /* 한자/한글 글꼴 ROM (포트 0xD8~0xDB), 메모리 블록 없음 */
    REGION_DISK_ROM,            /* 디스크 ROM + FDC (종류 64 대우·내셔널, 65 필립스·소니) */
    REGION_ROM_DOS2,            /* MSX-DOS2 ROM (종류 2): 16 KiB 뱅크, 0x6000~0x7FFF에 쓰면 바뀜 */
    REGION_PAC,                 /* 3.1.0: 기종 내장 PAC SRAM (종류 43), 파일 없음 */
    REGION_FW,                  /* 3.4.0: 펌웨어 매퍼 (27·68·142 파나소닉, 31 내셔널, 40 Halnote) */
    REGION_BUNSETSU,            /* 3.4.0: 문절 변환 롬 (28) */
    REGION_JISYO,               /* 3.4.0: 사전 롬 (29, 메모리 블록 없음) */
    REGION_FORTE,               /* 3.4.0: Forte II (138, 주소·자료 선이 섞인 64 KB 롬) */
    REGION_SUNRISE,             /* 3.4.0: 기종 내장 Sunrise IDE (96) */
    REGION_SFG,                 /* 3.4.1: 기종 내장 SFG-01·05 (106·105) */
    REGION_CART                 /* 3.6.8: 기종 내장 카트리지 (매퍼 = cart_mapper, 파일 없으면 빈 버퍼) */
} RegionType;

typedef struct {
    uint8_t slot, subslot;
    uint8_t start_block;        /* 0..7 */
    uint8_t block_count;        /* 1..8 */
    RegionType type;
    int blue_type;              /* config.ini의 원래 형식 번호 */
    char file[256];             /* config.ini에 적힌 ROM 경로 그대로 (RAM이면 빈 문자열) */
    uint16_t size_blocks;       /* 매퍼 RAM 크기 (8 KiB 단위) */
    uint8_t cart_mapper;        /* 3.6.8: REGION_CART의 MapperType */
    uint8_t cart_fill;          /* 3.6.8: 파일 없는 카트리지 버퍼 채움 값 (0x00 / 0xFF) */
    uint32_t cart_size;         /* 3.6.8: 파일 없는 카트리지 버퍼 크기 (0 = 파일 크기) */
} ProfileRegion;

typedef struct {
    char id[64];
    char manufacturer[32];
    char model[32];
    char key_layout[16];
    char board[24];
    uint32_t z80_hz;
    char vdp[16];
    int vram_kib;
    int subslotted[4];
    int ext_slot[5];            /* 외부 슬롯 A/B가 연결된 기본 슬롯 (3.3.1: C/D, -1 = 없음) */
    int ext_ss[5];              /* 그 슬롯이 나뉘었을 때 서브슬롯 */
    int fdc_count;
    ProfileRegion region[MAX_REGIONS];
    int region_count;
    int cmos;                           /* [CMOS] Enable CMOS */
    int cmos_battery;                   /* [CMOS] Battery Backed: RTC RAM(블록 1~3)을 저장 */
    int opll;                           /* MSX-MUSIC (종류 78) 있음 */
    int y8950;                          /* 2.16: 내장 MSX-AUDIO (종류 80, 메모리 없는 장치) */
    int joyrex;                         /* 3.6.8: 내장 Joyrex PSG (종류 147) */
    int moonsound;                      /* 3.1.0: 내장 MoonSound (종류 79) */
    char ms_rom[128];                   /* 그 웨이브 롬 경로 (config.ini 그대로) */
    int s1985;                          /* MSX2 엔진 S1985 (종류 32) */
    int s1990, trtimer, pcm, trpause;   /* turbo R: S1990(58), 타이머(59), PCM(60), 일시 정지(90) */
    int f4;                             /* F4 장치: 1 보통(33), 2 반전(34) */
    int midi;                           /* 3.3.3: MSX-MIDI (종류 88, turbo R GT). 3.6.1: 프런트엔드가 midi_hw와 "내장 MIDI" 설정으로 정함 */
    int midi_hw;                        /* 3.6.1: 기종에 MSX-MIDI가 있음 (설정 파일 그대로) */
    int matsushita;                     /* 3.4.0: Matsushita 교환 I/O (종류 26) */
    int unsupported[MAX_UNSUPPORTED];   /* 아직 지원하지 않는 장치 종류 번호 */
    int unsupported_count;
    uint8_t unknown_at[4][4];           /* 아직 없는 장치가 차지한 [슬롯][서브슬롯] (슬롯 표에 ?) */
    char missing[4][64];                /* 없어서 뺀 ROM 파일 이름 (machine_load가 채움) */
    int missing_count;
} MachineProfile;

/* config.ini 텍스트 해석 (표준 라이브러리 없이 동작). 성공 0, 실패 -1 (err에 이유) */
int profile_parse(const char *text, size_t len, MachineProfile *p, char *err, size_t errlen);
int profile_cart_mapper(int type);                 /* 3.6.8 */
void profile_cart_buf(int type, int count, uint32_t *size, uint8_t *fill);
int profile_type_supported(int type);              /* 3.6.8: 머신 편집기 "미지원" 표시용 */

/* 호스트 전용: dir/config.ini를 읽고 ROM 경로를 dir 기준으로 바꾼다 */
int profile_load(const char *dir, MachineProfile *p, char *err, size_t errlen);

#endif
