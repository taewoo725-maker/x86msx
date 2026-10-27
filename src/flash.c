/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * flash.c - AMD 형 플래시 칩 공용 부품 (5.2.7 구조 개편 4단계). 설명은 flash.h
 * 예전 msx.c 의 flash_cmd(29F040)·flash_cmd_gl(S29GL064)·flash_mark·ID/CFI 블록을 칩 표 하나로 옮김 (동작 같음)
 */
#include "flash.h"
#include <string.h>

typedef struct {
    const char *name;
    uint32_t size;          /* 칩 크기 */
    uint32_t boot_end;      /* 이 주소 아래는 boot_sector 크기 섹터 (0 = 없음) */
    uint32_t boot_sector, sector;
    uint16_t addr_mask;     /* 명령 주소 비교에 쓰는 아래 비트 */
    uint16_t a1, a2;        /* 명령 주소 (AAh 받는 곳, 55h 받는 곳) */
    uint8_t word_ok;        /* 1 = 워드 모드 주소(아래 1비트 무시)도 받음 */
    uint8_t gl;             /* 1 = 자동 선택·CFI 중엔 F0h·CFI 질의만 받음, 프로그램 주소를 칩 크기로 감쌈, CFI 있음 */
    uint8_t cfi_ff_exit;    /* 1 = CFI 중 FFh 로도 읽기로 */
    uint8_t mark_map;       /* 1 = 8 KiB 조각 바뀜 표시 (.flash 저장) */
    uint8_t cfi_s;          /* CFI 표: 1 = S형, 0 = N형 */
    uint8_t id_mfr, id_dev; /* 자동 선택 ID (AMD 형 칩, 0 이면 01/A4) */
    uint8_t boot_kind;      /* 0 = boot_end 아래 boot_sector 균일, 1 = 16K/8K/8K/32K (M29W800DB 아래쪽 부트) */
    uint8_t a12_swap;       /* 명령 주소 비교 때 CPU A12 → 칩 A0, A0~A11 → A1~A12 (MFR SCC+). 자료 주소는 그대로 */
} FlashInfo;

static const FlashInfo flash_tab[FLASH_TYPES] = {
    [FLASH_NONE]      = { "none", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    [FLASH_AM29F040]  = { "AM29F040", 512u << 10, 0, 0, 0x10000, 0x7FF, 0x555, 0x2AA, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    [FLASH_S29GL064N] = { "S29GL064N", 8u << 20, 0x10000, 0x2000, 0x10000, 0xFFF, 0xAAA, 0x555, 1, 1, 0, 1, 0, 0, 0, 0, 0 },
    [FLASH_S29GL064S] = { "S29GL064S", 8u << 20, 0x10000, 0x2000, 0x10000, 0xFFF, 0xAAA, 0x555, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
    [FLASH_M29W128]   = { "M29W128", 16u << 20, 0, 0, 0x20000, 0xFFF, 0xAAA, 0x555, 1, 1, 0, 1, 0, 0x20, 0x7E, 0, 0 },
    [FLASH_M29W800DB] = { "M29W800DB", 1u << 20, 0x10000, 0x2000, 0x10000, 0xFFF, 0xAAA, 0x555, 1, 0, 0, 0, 0, 0x20, 0x5B, 1, 1 },
};

static uint8_t am_id_block[8192];                   /* 29F040 자동 선택 읽기 (제조사 01, 장치 A4) */
static uint8_t st_id_block[8192];
static uint8_t m128_id_block[8192];                 /* M29W128 자동 선택 (popolon 표: 4000 20 20 7E 7E 00 00 19 19 … 401C 21 21 01 01) */                   /* M29W800DB 자동 선택 (20/5B) — CPU 주소 순서 (A12 바꿔 꽂음 때문에 0 = 제조사, 1 = 장치) */
static uint8_t gl_id_block[3][8192];                /* S29GL064 자동 선택 / CFI(N형) / CFI(S형) */
static int id_ready;

/* 4.0.131: S29GL064 CFI 표 (openMSX AmdFlash.cc와 같음). n = 칩 안 16비트 주소, sm = 1이면 S29GL064S (ASCII16-X), 0이면 N (Yamanooto) */
static uint16_t gl_cfi16(uint32_t n, int sm)
{
    static const uint8_t tN[8] = { 7, 7, 10, 0, 3, 5, 4, 0 }, tS[8] = { 8, 8, 9, 16, 3, 3, 1, 0 };
    const uint8_t *t = sm ? tS : tN;
    n &= 0x7F;
    if (n < 0x10) {
        if (!sm) return 0xFFFF;
        switch (n) { case 0: return 0x0001; case 1: return 0x227E; case 3: return 0xFF0A; case 0xE: return 0x2210; case 0xF: return 0x2200; default: return 0xFFFF; }
    }
    switch (n) {
    case 0x10: return 'Q'; case 0x11: return 'R'; case 0x12: return 'Y';
    case 0x13: return 0x02; case 0x14: return 0x00; case 0x15: return 0x40;
    case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: return 0x00;
    case 0x1B: return 0x27; case 0x1C: return 0x36; case 0x1D: case 0x1E: return 0x00;
    case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: return t[n - 0x1F];
    case 0x27: return 0x17; case 0x28: return 0x02; case 0x29: return 0x00;
    case 0x2A: return sm ? 8 : 5; case 0x2B: return 0x00; case 0x2C: return 2;
    case 0x2D: return 7; case 0x2E: return 0; case 0x2F: return 0x20; case 0x30: return 0;      /* 8 KiB x 8 */
    case 0x31: return 0x7E; case 0x32: return 0; case 0x33: return 0; case 0x34: return 1;      /* 64 KiB x 127 */
    case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: return 0;
    case 0x40: return 'P'; case 0x41: return 'R'; case 0x42: return 'I'; case 0x43: return '1'; case 0x44: return '3';
    case 0x45: return 0x20; case 0x46: return 2; case 0x47: return 1; case 0x48: return 0; case 0x49: return 8;
    case 0x4A: case 0x4B: return 0; case 0x4C: return 2; case 0x4D: return 0xB5; case 0x4E: return 0xC5; case 0x4F: return 0x02; case 0x50: return 1;
    default: return 0xFFFF;
    }
}
static void gl_id_init(void)
{
    for (int i = 0; i < 8192; i++) {
        uint32_t n = ((uint32_t)i >> 1) & 0x0F;                    /* 자동 선택: 16비트 값의 아래 바이트가 짝·홀 모두 */
        gl_id_block[0][i] = n == 0 ? 0x01 : n == 1 ? 0x7E : n == 2 ? 0x00 : n == 3 ? 0x0A : n == 0xE ? 0x10 : n == 0xF ? 0x00 : 0xFF;
        for (int sm = 0; sm < 2; sm++) {
            uint16_t r = gl_cfi16(((uint32_t)i >> 1) & 0x7F, sm);
            gl_id_block[1 + sm][i] = (i & 1) ? (uint8_t)(r >> 8) : (uint8_t)r;
        }
    }
}

static void id_init(void)
{
    if (id_ready) return;
    gl_id_init();
    for (int i = 0; i < 8192; i++) am_id_block[i] = (i & 3) == 0 ? 0x01 : (i & 3) == 1 ? 0xA4 : 0x00;
    for (int i = 0; i < 8192; i++) st_id_block[i] = (i & 3) == 0 ? 0x20 : (i & 3) == 1 ? 0x5B : 0x00;
    for (int i = 0; i < 8192; i++) { int n = (i >> 1) & 0x0F; m128_id_block[i] = n == 0 ? 0x20 : n == 1 ? 0x7E : n == 3 ? 0x19 : n == 0xE ? 0x21 : n == 0xF ? 0x01 : 0x00; }
    id_ready = 1;
}

static const FlashInfo *info(const FlashChip *f) { return &flash_tab[f->type < FLASH_TYPES ? f->type : FLASH_NONE]; }

void flash_init(FlashChip *f, FlashType type) { id_init(); f->type = (uint8_t)type; f->step = 0; f->mode = 0; f->rsv = 0; }
void flash_reset(FlashChip *f) { f->step = 0; f->mode = 0; }
uint32_t flash_chip_size(FlashType type) { return type < FLASH_TYPES ? flash_tab[type].size : 0; }
const char *flash_chip_name(FlashType type) { return type < FLASH_TYPES ? flash_tab[type].name : "?"; }

const uint8_t *flash_id_page(const FlashChip *f)
{
    if (!f->mode) return 0;
    id_init();
    const FlashInfo *t = info(f);
    if (!t->gl) return t->id_mfr ? st_id_block : am_id_block;
    if (f->mode == 1 && t->id_mfr) return m128_id_block;
    return gl_id_block[f->mode == 1 ? 0 : t->cfi_s ? 2 : 1];
}

/* 4.0.131: 바뀐 8 KiB 조각 표시 → 저장 파일(.flash)에 이 조각만 */
static void mark(const FlashInfo *t, const FlashMem *mem, uint32_t off, uint32_t len)
{
    if (mem->dirty) *mem->dirty = 1;
    if (!t->mark_map || !mem->dirty_map || !len) return;
    for (uint32_t k = off >> 13, e = (off + len - 1) >> 13; k <= e && (k >> 3) < mem->dirty_map_bytes; k++) mem->dirty_map[k >> 3] |= (uint8_t)(1u << (k & 7));
}

static int at(const FlashInfo *t, uint32_t lo, uint32_t want)  /* 명령 주소 맞음 (워드 모드면 아래 1비트 무시: 555h 대신 554h 등, 5.0.8) */
{
    return t->word_ok ? (lo | 1) == (want | 1) : lo == want;
}

int flash_write(FlashChip *f, const FlashMem *mem, uint32_t fa, uint8_t v)
{
    const FlashInfo *t = info(f);
    if (!t->size || !mem->data || !mem->size) return 0;
    uint8_t *fl = mem->data;
    uint32_t lo = fa & t->addr_mask, st = f->step;
    if (t->a12_swap) lo = (((fa & 0xFFF) << 1) | ((fa >> 12) & 1)) & t->addr_mask;   /* CPU A12 → 칩 A0 (OPFX 의 555h/AAAh 순서가 칩에는 AAAh/555h) */
    uint8_t was = f->mode;
    if (t->gl) {
        fa %= mem->size;
        if (f->mode) {                                           /* 자동 선택·CFI 중: 되돌리기 / CFI 질의(칩 주소 0x55 에 98h)만 받음 */
            int cfi_cmd = v == 0x98 && ((fa >> 1) & 0xFF) == 0x55;
            if (v == 0xF0 || (f->mode == 2 && v == 0xFF && t->cfi_ff_exit)) f->mode = 0;
            else if (cfi_cmd) f->mode = 2;
            st = 0;
            goto done;
        }
    } else if (v == 0xF0) { st = 0; f->mode = 0; goto done; }
    if (v == 0xF0) st = 0;
    else if (st == 3) { fl[fa % mem->size] &= v; mark(t, mem, fa % mem->size, 1); st = 0; }          /* 바이트 쓰기 (1 → 0만) */
    else if (st == 0 && at(t, lo, t->a1) && v == 0xAA) st = 1;
    else if (st == 0 && t->gl && v == 0x98 && ((fa >> 1) & 0xFF) == 0x55) { f->mode = 2; st = 0; }
    else if (st == 1 && at(t, lo, t->a2) && v == 0x55) st = 2;
    else if (st == 2 && at(t, lo, t->a1) && v == 0x90) { f->mode = 1; st = 0; }
    else if (st == 2 && at(t, lo, t->a1) && v == 0xA0) st = 3;
    else if (st == 2 && at(t, lo, t->a1) && v == 0x80) st = 4;
    else if (st == 4 && at(t, lo, t->a1) && v == 0xAA) st = 5;
    else if (st == 5 && at(t, lo, t->a2) && v == 0x55) st = 6;
    else if (st == 6 && v == 0x30) {                                             /* 섹터 지우기 */
        uint32_t base, len;
        if (t->boot_kind == 1 && fa < 0x10000) {                                /* 16K / 8K / 8K / 32K */
            if (fa < 0x4000) { base = 0; len = 0x4000; } else if (fa < 0x8000) { base = fa & ~0x1FFFu; len = 0x2000; } else { base = 0x8000; len = 0x8000; }
        }
        else if (t->boot_end && fa < t->boot_end) { base = fa & ~(t->boot_sector - 1); len = t->boot_sector; }
        else { base = fa & ~(t->sector - 1); len = t->sector; }
        if (!t->gl) base %= mem->size;
        if (base >= mem->size) base %= mem->size;
        if (base + len > mem->size) len = mem->size - base;                     /* 버퍼(롬 파일)가 섹터보다 작으면 버퍼까지만 */
        memset(fl + base, 0xFF, len); mark(t, mem, base, len); st = 0;
    }
    else if (st == 6 && at(t, lo, t->a1) && v == 0x10) { memset(fl, 0xFF, mem->size); mark(t, mem, 0, mem->size); st = 0; }   /* 칩 전체 지우기 */
    else st = 0;
done:
    f->step = (uint8_t)st;
    return was != f->mode;
}
