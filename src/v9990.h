/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * v9990.h - Yamaha V9990 (E-VDP-III), Gfx9000 카트리지 (포트 60h~6Fh)
 *
 * 동작은 openMSX(GPL-2.0-or-later)의 V9990 구현을 참고해 새로 쓴 것이다 (THIRD-PARTY-NOTICES 참조).
 * 시간은 MSX의 줄 단위 루프에 맞춘다: v9990_line_start()는 줄 처음(인터럽트), v9990_line_end()는 줄 끝(그리기).
 * 명령(블리터)은 시작하는 즉시 끝까지 실행한다 (CPU가 자료를 주고받는 LMMC·LMCM·CMMC·POINT만 포트 2로 한 단계씩).
 */
#ifndef V9990_H
#define V9990_H

#include <stdint.h>

#define V9_FBW 640              /* 출력 버퍼 가로 (4 UC 클럭 = 1점, 2560클럭 창) */
#define V9_FBH 290              /* 출력 버퍼 세로 (PAL 최대) */

enum { V9_P1, V9_P2, V9_B0, V9_B1, V9_B2, V9_B3, V9_B4, V9_B5, V9_B6, V9_B7 };

typedef struct V9990 {
    uint8_t vram[0x80000];      /* 512 KiB, openMSX와 같은 물리 배치 (Bx는 홀짝 주소를 위·아래 256 KiB로) */
    uint8_t regs[64];
    uint8_t pal[256];           /* 64색 x (R, G, B, 0) */
    uint16_t pal15[64];         /* 15비트 (G<<10 | R<<5 | B) */
    uint8_t pending;            /* 인터럽트 깃발: 1 수직, 2 수평, 4 명령 끝 */
    uint8_t status;             /* 비트 1 = 홀짝 필드, 비트 2 = 시스템 제어 비트 0 (MCLK 방식) */
    uint8_t regsel, sysreset, rdbuf;
    int mode;                   /* V9_P1 ... V9_B7 */
    int cur_line, total;        /* 지금 줄, 프레임 줄 수 */
    int disp_en, pal_t, interlaced;
    uint8_t ay_hi, by_hi;       /* 프레임 시작에 잡는 세로 스크롤 상위 */
    int voff_a, voff_b;         /* 스크롤 세로 시작 줄 (AY0·BY0을 쓰면 그 줄부터) */
    int rows;                   /* 출력 줄 수 (NTSC 240, PAL 290) */
    int shown;                  /* 한 번이라도 화면을 켰으면 1 (MSX 리셋·시스템 리셋 전까지 V9990 화면을 계속 보여 줌) */
    /* 명령 엔진 */
    uint16_t SX, SY, DX, DY, NX, NY, WM, fg, bg;
    uint8_t ARG, LOG, CMD, cst;         /* cst: 0x80 TR, 0x10 BD, 0x01 CE */
    uint16_t ASX, ADX, ANX, ANY, borderX;
    uint32_t srcA, dstA, nbytes;
    uint8_t cdata, cpart, cbits, cend;
    int cmode;
    uint16_t lbuf[1300];                /* 줄 하나 그리기 임시 */
    uint32_t fb[V9_FBH][V9_FBW];        /* 0x00RRGGBB */
} V9990;

void v9990_init(V9990 *v);
void v9990_reset(V9990 *v);
uint8_t v9990_read(V9990 *v, int port, int tick);     /* tick: 줄 안 위치 0~2735 (HR 상태용) */
void v9990_write(V9990 *v, int port, uint8_t val);
void v9990_line_start(V9990 *v, int line, int total);
void v9990_line_end(V9990 *v, int line);
int v9990_frame_lines(const V9990 *v);                /* V9990 자기 프레임의 줄 수 (PAL 313 / NTSC 262) */
int v9990_irq(const V9990 *v);
int v9990_visible(const V9990 *v);                    /* 화면을 V9990으로 보여야 하나 */

#endif
