/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * opl4.h - YMF278B (OPL4) — MoonSound (3.1.0, 요구사항 DEV-31)
 *
 * FM 부분 = YMF262(OPL3): 포트 C4h/C5h(뱅크 0), C6h/C7h(뱅크 1), C4h 읽기 = 상태.
 *   18채널 2연산자, 4연산자 연결(레지스터 104h), 파형 8가지, 리듬, 타이머 2개. 49,716 Hz로 만든다.
 * 웨이브 부분: 포트 7Eh(주소)/7Fh(데이터), FM의 NEW2(105h 비트 1)가 켜져야 쓸 수 있다.
 *   24채널, 8·12·16비트 샘플, 엔벨로프·LFO(비브라토·트레몰로)·팬. 44,100 Hz로 만든다.
 *   메모리 22비트(4 MiB): 0x000000~0x1FFFFF 롬(yrw801, MOONSOUND.rom), 0x200000~0x3FFFFF 램(2 MiB).
 * 두 부분을 48 kHz 한 소리(모노)로 섞어 낸다. 계산은 정수만(표는 처음에 한 번 만든다).
 */
#ifndef MSX_OPL4_H
#define MSX_OPL4_H
#include <stdint.h>

#define OPL4_MEM_SIZE   (4u << 20)
#define OPL4_RAM_OFF    (2u << 20)      /* 3.4.4: 램 시작 (상태 저장용) */
#define OPL4_RAM_SIZE   (2u << 20)

typedef struct {
    uint32_t phase;                 /* 19비트 위상 */
    uint16_t eg_rout;               /* 엔벨로프 감쇠 0~511 */
    uint8_t eg_state;               /* 0 어택, 1 디케이, 2 서스테인, 3 릴리스 */
    uint8_t key;                    /* 비트 0 보통 키, 비트 1 리듬 키 */
    int16_t out, prout;
    uint8_t am, vib, egt, ksr, mult, ksl, tl, ar, dr, sl, rr, ws;
} Opl3Op;

typedef struct {
    uint16_t fnum;
    uint8_t block, kon, fb, cnt, outl, outr;
} Opl3Ch;

typedef struct {                    /* openMSX YMF278.cc 와 같은 구조 */
    uint16_t wave;                  /* 음색 번호 9비트 */
    uint16_t fn;                    /* F-number 10비트 */
    int8_t oct;                     /* -8~7 */
    uint8_t prvb, damp, keyon, pan, lfo_active, lfo, vib, am, bits;
    uint8_t ar, d1r, d2r, rc, rr, tl, tldest;
    int16_t dl, env;                /* dl: 감쇠 레벨(0.09375 dB 단위), env: 0 최대 ~ 0x280 무음 */
    uint8_t state;                  /* 0 꺼짐, 1 릴리스, 2 서스테인, 3 디케이, 4 어택 */
    uint32_t start;                 /* 샘플 시작 바이트 주소 */
    uint16_t loop, end;             /* 헤더 값 그대로 (end 는 음수 표기) */
    uint16_t pos;                   /* 지금 샘플 번호 */
    uint32_t step_ptr, step, lfo_cnt;
} Opl4Slot;

typedef struct {
    /* FM */
    uint8_t fmreg[512];
    Opl3Op op[36];
    Opl3Ch ch[18];
    uint8_t addr[2];
    uint8_t lbank;                      /* 마지막으로 주소를 쓴 뱅크 — 데이터 포트 둘(C5·C7)은 이 뱅크의 주소를 씀 (실제 YMF262/278 와 같음) */
    uint32_t eg_cnt, timer;
    uint32_t noise;
    uint8_t trem_pos, vib_pos;
    uint8_t status;                 /* 비트 7 IRQ, 6 타이머1, 5 타이머2 */
    uint8_t status2, new2_sig;      /* NEW2를 처음 켠 뒤 상태를 한 번 읽으면 비트 1 (실제 YMF278, openMSX 와 같게) */
    uint32_t t1_cnt, t2_cnt;        /* 타이머 남은 샘플 (49,716 Hz 단위, 소수 없이 근사) */
    uint8_t rm_hh_bit2, rm_hh_bit3, rm_hh_bit7, rm_hh_bit8, rm_tc_bit3, rm_tc_bit5;
    /* 웨이브 */
    uint8_t wreg[256];
    uint8_t waddr;
    uint32_t memaddr;
    Opl4Slot sl[24];
    /* 48 kHz로 바꾸기 */
    uint32_t fm_acc, pcm_acc;
    int32_t fm_last, pcm_last;      /* 가장 최근 칩 샘플 */
    int32_t fm_prev, pcm_prev;      /* 그 앞 샘플 (48 kHz 로 바꿀 때 직선 보간) */
    uint32_t weg_cnt;
} Opl4;

/* 웨이브 메모리 (롬 + 램, OPL4_MEM_SIZE). 플랫폼이 한 번 정함. rom_size = 롬 바이트 */
void opl4_set_memory(uint8_t *mem, uint32_t rom_size);
int opl4_has_memory(void);
uint8_t *opl4_memory(void);                            /* 롬을 읽어 넣을 자리 (없으면 NULL) */
void opl4_reset(Opl4 *o);
void opl4_write_fm(Opl4 *o, int port, uint8_t v);      /* port 0~3 = C4h~C7h */
uint8_t opl4_read_fm(Opl4 *o, int port);
void opl4_write_wave(Opl4 *o, int port, uint8_t v);    /* port 0 = 7Eh, 1 = 7Fh */
uint8_t opl4_read_wave(Opl4 *o, int port);
/* 48 kHz 한 샘플 (모노, 약 ±32767) */
int opl4_calc48(Opl4 *o);
int opl4_active(const Opl4 *o);                        /* 소리를 내는 중이면 1 (상태 칩) */
#endif
