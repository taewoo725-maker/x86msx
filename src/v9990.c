/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * v9990.c - Yamaha V9990 (E-VDP-III) / Gfx9000
 *
 * 레지스터 접근 표·VRAM 배치·명령 동작·화면 해석은 openMSX(GPL-2.0-or-later)의 V9990 구현을 참고해 새로 썼다.
 */
#include "v9990.h"
#include <string.h>

/* 레지스터 번호 */
enum {
    R_WA0 = 0, R_WA2 = 2, R_RA0 = 3, R_RA2 = 5, R_SM0 = 6, R_SM1 = 7, R_CTL = 8, R_INT0 = 9, R_INT1 = 10, R_INT2 = 11, R_INT3 = 12,
    R_PALCTL = 13, R_PALPTR = 14, R_BACK = 15, R_ADJ = 16, R_AY0 = 17, R_AY1 = 18, R_AX0 = 19, R_AX1 = 20, R_BY0 = 21, R_BY1 = 22, R_BX0 = 23, R_BX1 = 24,
    R_SPAT = 25, R_LCD = 26, R_PRIO = 27, R_SPAL = 28, R_CMD = 52, R_BX_LO = 53, R_BX_HI = 54
};
enum { CM_P1, CM_P2, CM_BP2, CM_BP4, CM_BP8, CM_BP16 };
enum { TR = 0x80, BD = 0x10, CE = 0x01 };
enum { DIY = 8, DIX = 4, NEQ = 2, MAJ = 1 };
enum { ACC_R = 1, ACC_W = 2 };

static const uint8_t reg_acc[64] = {
    2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 2, 2, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 0, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t reg_mask[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x87, 0xFF, 0x83, 0x0F, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xDF, 0x07, 0xFF, 0xFF, 0xC1, 0x07, 0x3F, 0xCF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* openMSX 와 같은 색 곡선 — 5비트 → 255 * (i/31)^(1/1.1)  (openMSX 감마 기본값 1.1, 소수점 버림). 커널엔 실수 계산이 없어 미리 구한 표 */
static const uint8_t gam5[32] = { 0, 11, 21, 30, 39, 48, 57, 65, 74, 82, 91, 99, 107, 115, 123, 131, 139, 147, 155, 163, 171, 178, 186, 194, 202, 209, 217, 224, 232, 239, 247, 255 };
static inline uint32_t rgb15(unsigned i)   /* 15비트 GRB → 0x00RRGGBB */
{
    unsigned g = (i >> 10) & 31, r = (i >> 5) & 31, b = i & 31;
    return ((uint32_t)gam5[r] << 16) | ((uint32_t)gam5[g] << 8) | gam5[b];
}

/* ---------- VRAM 주소 변환 ---------- */
static inline unsigned tr_bx(unsigned a) { return ((a & 1) << 18) | ((a & 0x7FFFE) >> 1); }
static inline unsigned tr_p2(unsigned a)
{
    if (a < 0x78000) return tr_bx(a);
    if (a < 0x7C000) return a - 0x3C000;
    return a;
}
static inline unsigned map_addr(const V9990 *v, unsigned a)
{
    a &= 0x7FFFF;
    if (v->mode == V9_P1) return a;
    if (v->mode == V9_P2) return tr_p2(a);
    return tr_bx(a);
}
static inline uint8_t rd_bx(const V9990 *v, unsigned a) { return v->vram[tr_bx(a & 0x7FFFF)]; }
static inline void wr_bx(V9990 *v, unsigned a, uint8_t d) { v->vram[tr_bx(a & 0x7FFFF)] = d; }

/* ---------- 색 ---------- */
static void pal_set(V9990 *v, int reg, uint8_t val)
{
    switch (reg & 3) {
    case 0: val &= 0x9F; break;
    case 1: case 2: val &= 0x1F; break;
    default: val = 0; break;
    }
    v->pal[reg & 255] = val;
    int i = (reg & 255) >> 2;
    v->pal15[i] = (v->pal[i * 4] & 0x80) ? 0   /* YS(투명) 비트가 켜진 색은 openMSX 처럼 검정 (슈퍼임포즈 없이는 비쳐 보일 화면이 없음) — Chase HQ 의 아래 띠 */
        : (uint16_t)(((v->pal[i * 4 + 1] & 31) << 10) | ((v->pal[i * 4] & 31) << 5) | (v->pal[i * 4 + 2] & 31));
}

/* ---------- 모드 ---------- */
static int calc_mode(const V9990 *v)
{
    switch (v->regs[R_SM0] & 0xC0) {
    case 0x00: return V9_P1;
    case 0x40: return V9_P2;
    case 0x80:
        if (v->status & 4) { switch (v->regs[R_SM0] & 0x30) { case 0x00: return V9_B0; case 0x10: return V9_B2; case 0x20: return V9_B4; } }
        else { switch (v->regs[R_SM0] & 0x30) { case 0x00: return V9_B1; case 0x10: return V9_B3; case 0x20: return V9_B7; } }
        break;
    }
    return V9_P1;
}
enum { C_BP2, C_BP4, C_BP6, C_BD8, C_YJK, C_YUV, C_BD16 };
static int color_mode(const V9990 *v)
{
    if (!(v->regs[R_SM0] & 0x80)) return C_BP4;
    switch (v->regs[R_SM0] & 3) {
    case 0: return C_BP2;
    case 1: return C_BP4;
    case 2:
        switch (v->regs[R_PALCTL] & 0xC0) { case 0x00: return C_BP6; case 0x40: return C_BD8; case 0x80: return C_YJK; default: return C_YUV; }
    default: return C_BD16;
    }
}
static inline unsigned image_width(const V9990 *v)
{
    switch (v->regs[R_SM0] & 0xC0) {
    case 0x00: return 256;
    case 0x40: return 512;
    default: return 256u << ((v->regs[R_SM0] & 0x0C) >> 2);
    }
}
static inline int overscan(const V9990 *v) { return v->mode == V9_B0 || v->mode == V9_B2 || v->mode == V9_B4; }

/* 세로 배치: blank, border1, display, border2 */
static void ver_timing(const V9990 *v, int *blank, int *b1, int *disp, int *b2)
{
    *blank = 15;
    if (overscan(v)) { *b1 = 0; *disp = v->pal_t ? 290 : 240; *b2 = 0; }
    else if (v->pal_t) { *b1 = 41; *disp = 212; *b2 = 37; }
    else { *b1 = 14; *disp = 212; *b2 = 14; }
}
static int top_line(const V9990 *v)
{
    int bl, b1, d, b2;
    ver_timing(v, &bl, &b1, &d, &b2);
    return bl + b1 + (((v->regs[R_ADJ] >> 4) ^ 7) - 8);
}
static int bottom_line(const V9990 *v)
{
    int bl, b1, d, b2;
    ver_timing(v, &bl, &b1, &d, &b2);
    return top_line(v) + d;
}
static int left_tick(const V9990 *v)
{
    int blank = overscan(v) ? 372 : 400, b1 = overscan(v) ? 0 : 112;
    return blank + b1 + (((v->regs[R_ADJ] & 0x0F) ^ 7) - 8) * 8;
}
static int right_tick(const V9990 *v) { return left_tick(v) + (overscan(v) ? 2304 : 2048); }

/* ---------- 인터럽트 ---------- */
int v9990_irq(const V9990 *v) { return (v->pending & v->regs[R_INT0] & 7) != 0; }
static inline void raise(V9990 *v, uint8_t f) { v->pending |= f; }

/* ---------- 명령 엔진 ---------- */
static inline unsigned c_pitch(int cm, unsigned width)
{
    switch (cm) {
    case CM_P1: case CM_P2: case CM_BP4: return width / 2;
    case CM_BP2: return width / 4;
    default: return width;
    }
}
static inline unsigned c_addr(int cm, unsigned x, unsigned y, unsigned pitch)
{
    switch (cm) {
    case CM_P1: { unsigned a = ((x / 2) & (pitch - 1)) + y * pitch; return (a & 0x3FFFF) | ((x & 0x200) << 9); }
    case CM_P2: return tr_p2(((x / 2) & (pitch - 1)) + y * pitch) & 0x7FFFF;
    case CM_BP2: return tr_bx(((x / 4) & (pitch - 1)) + y * pitch) & 0x7FFFF;
    case CM_BP4: return tr_bx(((x / 2) & (pitch - 1)) + y * pitch) & 0x7FFFF;
    case CM_BP8: return tr_bx((x & (pitch - 1)) + y * pitch) & 0x7FFFF;
    default: return ((x & (pitch - 1)) + y * pitch) & 0x3FFFF;
    }
}
static inline int c_ppb(int cm)         /* 바이트당 점 수 (BP16은 0) */
{
    switch (cm) { case CM_P1: case CM_P2: case CM_BP4: return 2; case CM_BP2: return 4; case CM_BP8: return 1; default: return 0; }
}
static inline uint8_t c_shift(int cm, uint8_t val, unsigned fx, unsigned tx)
{
    int s;
    switch (cm) {
    case CM_P1: case CM_P2: case CM_BP4: s = 4 * ((int)(tx & 1) - (int)(fx & 1)); break;
    case CM_BP2: s = 2 * ((int)(tx & 3) - (int)(fx & 3)); break;
    default: return val;
    }
    return s > 0 ? (uint8_t)(val >> s) : (uint8_t)(val << -s);
}
static inline uint8_t c_smask(int cm, unsigned x)
{
    switch (cm) {
    case CM_P1: case CM_P2: case CM_BP4: return (x & 1) ? 0x0F : 0xF0;
    case CM_BP2: return (uint8_t)(0xC0 >> (2 * (x & 3)));
    default: return 0xFF;
    }
}
/* 논리 연산 (op 하위 4비트 = 진리표, 0x10 = 투명). 투명이면 원본 한 점이 0일 때 목적지를 그대로 */
static uint8_t lop(int op, int gbits, uint8_t s, uint8_t d)
{
    uint8_t r = 0;
    for (int b = 0; b < 8; b++) r |= (uint8_t)(((op >> ((((s >> b) & 1) << 1) | ((d >> b) & 1))) & 1) << b);
    if (!(op & 0x10)) return r;
    unsigned gm = (1u << gbits) - 1, out = 0;
    for (int g = 0; g < 8; g += gbits) out |= (((s >> g) & gm) == 0 ? d : r) & (gm << g);
    return (uint8_t)out;
}
static inline int c_gbits(int cm) { return cm == CM_BP2 ? 2 : cm == CM_BP8 ? 8 : 4; }
static uint16_t lop16(int op, uint16_t s, uint16_t d)
{
    if ((op & 0x10) && s == 0) return d;
    uint8_t lo = lop(op & 0x0F, 8, (uint8_t)s, (uint8_t)d), hi = lop(op & 0x0F, 8, (uint8_t)(s >> 8), (uint8_t)(d >> 8));
    return (uint16_t)(lo | (hi << 8));
}
static uint16_t c_point(const V9990 *v, int cm, unsigned x, unsigned y, unsigned pitch)
{
    unsigned a = c_addr(cm, x, y, pitch);
    if (cm == CM_BP16) return (uint16_t)(v->vram[a] | (v->vram[a + 0x40000] << 8));
    return v->vram[a];
}
/* srcColor는 목적지 점 위치에 맞춰져 있는 바이트 */
static void c_pset(V9990 *v, int cm, unsigned x, unsigned y, unsigned pitch, uint16_t src, uint16_t mask, int op)
{
    unsigned a = c_addr(cm, x, y, pitch);
    if (cm == CM_BP16) {
        uint16_t dst = (uint16_t)(v->vram[a] | (v->vram[a + 0x40000] << 8));
        uint16_t nw = lop16(op, src, dst), res = (uint16_t)((dst & ~mask) | (nw & mask));
        v->vram[a] = (uint8_t)res; v->vram[a + 0x40000] = (uint8_t)(res >> 8);
        return;
    }
    uint8_t dst = v->vram[a], nw = lop(op, c_gbits(cm), (uint8_t)src, dst);
    uint8_t m1 = (uint8_t)((a & 0x40000) ? (mask >> 8) : (mask & 0xFF)), m2 = m1 & c_smask(cm, x);
    v->vram[a] = (uint8_t)((dst & ~m2) | (nw & m2));
}
static void c_psetc(V9990 *v, int cm, unsigned x, unsigned y, unsigned pitch, uint16_t col, uint16_t mask, int op)
{
    unsigned a = c_addr(cm, x, y, pitch);
    if (cm == CM_BP16) { c_pset(v, cm, x, y, pitch, col, mask, op); return; }
    uint8_t src = (uint8_t)((a & 0x40000) ? (col >> 8) : (col & 0xFF));
    uint8_t dst = v->vram[a], nw = lop(op, c_gbits(cm), src, dst);
    uint8_t m1 = (uint8_t)((a & 0x40000) ? (mask >> 8) : (mask & 0xFF)), m2 = m1 & c_smask(cm, x);
    v->vram[a] = (uint8_t)((dst & ~m2) | (nw & m2));
}

static void cmd_ready(V9990 *v)
{
    v->CMD = 0;
    v->cst &= (uint8_t)~(CE | TR);
    raise(v, 4);
}
static inline unsigned wnx(const V9990 *v) { return v->NX ? v->NX : 2048; }
static inline unsigned wny(const V9990 *v) { return v->NY ? v->NY : 4096; }
#define DXD ((v->ARG & DIX) ? (uint16_t)-1 : 1)
#define DYD ((v->ARG & DIY) ? (uint16_t)-1 : 1)

static void cmd_set_mode(V9990 *v)
{
    if (v->mode == V9_P1) v->cmode = CM_P1;
    else if (v->mode == V9_P2) v->cmode = CM_P2;
    else switch (color_mode(v)) {
        case C_BP2: v->cmode = CM_BP2; break;
        case C_BP4: v->cmode = CM_BP4; break;
        case C_BD16: v->cmode = CM_BP16; break;
        default: v->cmode = CM_BP8; break;
    }
}

/* CPU와 주고받는 명령의 한 단계 (TR이 꺼져 있을 때) */
static void cmd_step(V9990 *v)
{
    int cm = v->cmode;
    unsigned pitch = c_pitch(cm, image_width(v));
    unsigned op = v->CMD >> 4;
    if (v->cst & TR) return;
    switch (op) {
    case 1: {                                           /* LMMC */
        v->cst |= TR;
        if (cm == CM_BP16) {
            if (v->cbits) { v->cbits = 0; v->cpart = v->cdata; return; }
            v->cbits = 1;
            uint16_t val = (uint16_t)((v->cdata << 8) | v->cpart);
            c_pset(v, cm, v->DX, v->DY, pitch, val, v->WM, v->LOG);
            uint16_t dx = DXD;
            v->DX = (uint16_t)(v->DX + dx);
            if (!--v->ANX) {
                v->DX = (uint16_t)(v->DX - (uint16_t)(v->NX * dx)); v->DY = (uint16_t)(v->DY + DYD);
                if (!--v->ANY) cmd_ready(v); else v->ANX = (uint16_t)wnx(v);
            }
            return;
        }
        for (int i = 0; v->ANY > 0 && i < c_ppb(cm); i++) {
            uint8_t d = c_shift(cm, v->cdata, (unsigned)i, v->DX);
            c_pset(v, cm, v->DX, v->DY, pitch, d, v->WM, v->LOG);
            uint16_t dx = DXD;
            v->DX = (uint16_t)(v->DX + dx);
            if (!--v->ANX) {
                v->DX = (uint16_t)(v->DX - (uint16_t)(v->NX * dx)); v->DY = (uint16_t)(v->DY + DYD);
                if (!--v->ANY) cmd_ready(v); else v->ANX = v->NX;
            }
        }
        break;
    }
    case 3: {                                           /* LMCM */
        v->cst |= TR;
        if (cm == CM_BP16 && v->cbits) { v->cbits = 0; v->cdata = v->cpart; return; }
        unsigned d = 0;
        for (int i = 0; v->ANY > 0 && i < (cm == CM_BP16 ? 1 : c_ppb(cm)); i++) {
            uint16_t src = c_point(v, cm, v->SX, v->SY, pitch);
            if (cm == CM_BP16) d = src;
            else d |= (unsigned)(c_shift(cm, (uint8_t)src, v->SX, (unsigned)i) & c_smask(cm, (unsigned)i));
            uint16_t dx = DXD;
            v->SX = (uint16_t)(v->SX + dx);
            if (!--v->ANX) {
                v->SX = (uint16_t)(v->SX - (uint16_t)(v->NX * dx)); v->SY = (uint16_t)(v->SY + DYD);
                if (!--v->ANY) v->cend = 1; else v->ANX = (uint16_t)wnx(v);
            }
        }
        if (cm == CM_BP16) { v->cdata = (uint8_t)d; v->cpart = (uint8_t)(d >> 8); v->cbits = 1; }
        else v->cdata = (uint8_t)d;
        break;
    }
    case 5: {                                           /* CMMC */
        v->cst |= TR;
        for (int i = 0; i < 8; i++) {
            int bit = (v->cdata & 0x80) != 0;
            v->cdata = (uint8_t)(v->cdata << 1);
            c_psetc(v, cm, v->DX, v->DY, pitch, bit ? v->fg : v->bg, v->WM, v->LOG);
            uint16_t dx = DXD;
            v->DX = (uint16_t)(v->DX + dx);
            if (!--v->ANX) {
                v->DX = (uint16_t)(v->DX - (uint16_t)(v->NX * dx)); v->DY = (uint16_t)(v->DY + DYD);
                if (!--v->ANY) { cmd_ready(v); return; }
                v->ANX = (uint16_t)wnx(v);
            }
        }
        break;
    }
    case 0xD:                                           /* POINT 16비트 두 번째 바이트 */
        v->cst |= TR;
        v->cdata = v->cpart;
        v->cend = 1;
        break;
    default: break;
    }
}

static void cmd_start(V9990 *v, uint8_t value)
{
    int cm;
    cmd_set_mode(v);
    cm = v->cmode;
    v->CMD = value;
    v->cst |= CE;
    unsigned pitch = c_pitch(cm, image_width(v));
    unsigned width = image_width(v);
    uint16_t dx = DXD, dy = DYD;
    switch (value >> 4) {
    case 0x0: cmd_ready(v); break;                      /* STOP */
    case 0x1:                                           /* LMMC */
        v->ANX = (uint16_t)wnx(v); v->ANY = (uint16_t)wny(v); v->cst |= TR;
        if (cm == CM_BP16) v->cbits = 1;
        break;
    case 0x2: {                                         /* LMMV */
        unsigned ny = wny(v), nx;
        while (ny--) {
            nx = wnx(v);
            while (nx--) { c_psetc(v, cm, v->DX, v->DY, pitch, v->fg, v->WM, v->LOG); v->DX = (uint16_t)(v->DX + dx); }
            v->DX = (uint16_t)(v->DX - (uint16_t)(v->NX * dx)); v->DY = (uint16_t)(v->DY + dy);
        }
        cmd_ready(v);
        break;
    }
    case 0x3:                                           /* LMCM */
        v->ANX = (uint16_t)wnx(v); v->ANY = (uint16_t)wny(v); v->cst &= (uint8_t)~TR; v->cend = 0; v->cbits = 0;
        cmd_step(v);
        break;
    case 0x4: {                                         /* LMMM */
        unsigned ny = wny(v), nx;
        while (ny--) {
            nx = wnx(v);
            while (nx--) {
                uint16_t s = c_point(v, cm, v->SX, v->SY, pitch);
                if (cm != CM_BP16) s = c_shift(cm, (uint8_t)s, v->SX, v->DX);
                c_pset(v, cm, v->DX, v->DY, pitch, s, v->WM, v->LOG);
                v->DX = (uint16_t)(v->DX + dx); v->SX = (uint16_t)(v->SX + dx);
            }
            v->DX = (uint16_t)(v->DX - (uint16_t)(v->NX * dx)); v->SX = (uint16_t)(v->SX - (uint16_t)(v->NX * dx));
            v->DY = (uint16_t)(v->DY + dy); v->SY = (uint16_t)(v->SY + dy);
        }
        cmd_ready(v);
        break;
    }
    case 0x5:                                           /* CMMC */
        v->ANX = (uint16_t)wnx(v); v->ANY = (uint16_t)wny(v); v->cst |= TR;
        break;
    case 0x6: cmd_ready(v); break;                      /* CMMK (구현 안 됨, openMSX와 같이 바로 끝) */
    case 0x7: {                                         /* CMMM: VRAM의 1비트 글꼴 → 사각형 */
        unsigned sa = (v->SX & 0xFF) + ((unsigned)(v->SY & 0x7FF) << 8), ny = wny(v), nx;
        int bits = 0;
        uint8_t d = 0;
        while (ny--) {
            nx = wnx(v);
            while (nx--) {
                if (!bits) { d = rd_bx(v, sa++); bits = 8; }
                bits--;
                int bit = (d & 0x80) != 0;
                d = (uint8_t)(d << 1);
                c_psetc(v, cm, v->DX, v->DY, pitch, bit ? v->fg : v->bg, v->WM, v->LOG);
                v->DX = (uint16_t)(v->DX + dx);
            }
            v->DX = (uint16_t)(v->DX - (uint16_t)(v->NX * dx)); v->DY = (uint16_t)(v->DY + dy);
        }
        cmd_ready(v);
        break;
    }
    case 0x8: {                                         /* BMXL: 선형 VRAM → 사각형 */
        unsigned sa = (v->SX & 0xFF) + ((unsigned)(v->SY & 0x7FF) << 8);
        unsigned ny = wny(v), nx = wnx(v);
        if (cm == CM_BP16) {
            while (ny) {
                uint16_t s = (uint16_t)(rd_bx(v, sa) | (rd_bx(v, sa + 1) << 8)); sa += 2;
                c_pset(v, cm, v->DX, v->DY, pitch, s, v->WM, v->LOG);
                v->DX = (uint16_t)(v->DX + dx);
                if (!--nx) { v->DX = (uint16_t)(v->DX - (uint16_t)(v->NX * dx)); v->DY = (uint16_t)(v->DY + dy); if (!--ny) break; nx = wnx(v); }
            }
        } else {
            int ppb = c_ppb(cm);
            while (ny) {
                uint8_t d = rd_bx(v, sa++);
                for (int i = 0; ny && i < ppb; i++) {
                    c_pset(v, cm, v->DX, v->DY, pitch, c_shift(cm, d, (unsigned)i, v->DX), v->WM, v->LOG);
                    v->DX = (uint16_t)(v->DX + dx);
                    if (!--nx) { v->DX = (uint16_t)(v->DX - (uint16_t)(v->NX * dx)); v->DY = (uint16_t)(v->DY + dy); if (!--ny) break; nx = wnx(v); }
                }
            }
        }
        cmd_ready(v);
        break;
    }
    case 0x9: {                                         /* BMLX: 사각형 → 선형 VRAM */
        unsigned da = (v->DX & 0xFF) + ((unsigned)(v->DY & 0x7FF) << 8);
        unsigned ny = wny(v), nx = wnx(v);
        if (cm == CM_BP16) {
            while (ny) {
                uint16_t s = c_point(v, cm, v->SX, v->SY, pitch);
                wr_bx(v, da++, (uint8_t)s); wr_bx(v, da++, (uint8_t)(s >> 8));
                v->SX = (uint16_t)(v->SX + dx);
                if (!--nx) { v->SX = (uint16_t)(v->SX - (uint16_t)(v->NX * dx)); v->SY = (uint16_t)(v->SY + dy); if (!--ny) break; nx = wnx(v); }
            }
        } else {
            int ppb = c_ppb(cm);
            int fin = 0;
            while (!fin) {
                uint8_t d = 0;
                for (int i = 0; i < ppb; i++) {
                    uint8_t s = (uint8_t)c_point(v, cm, v->SX, v->SY, pitch);
                    d |= (uint8_t)(c_shift(cm, s, v->SX, (unsigned)i) & c_smask(cm, (unsigned)i));
                    v->SX = (uint16_t)(v->SX + dx);
                    if (!--nx) { v->SX = (uint16_t)(v->SX - (uint16_t)(v->NX * dx)); v->SY = (uint16_t)(v->SY + dy); if (!--ny) { fin = 1; break; } nx = wnx(v); }
                }
                wr_bx(v, da++, d);
            }
        }
        cmd_ready(v);
        break;
    }
    case 0xA: {                                         /* BMLL: 선형 → 선형 */
        unsigned sa = (v->SX & 0xFF) + ((unsigned)(v->SY & 0x7FF) << 8);
        unsigned da = (v->DX & 0xFF) + ((unsigned)(v->DY & 0x7FF) << 8);
        unsigned nb = (v->NX & 0xFF) + ((unsigned)(v->NY & 0x7FF) << 8);
        if (!nb) nb = 0x80000;
        if (cm == CM_BP16) {
            sa >>= 1; da >>= 1; nb >>= 1;
            while (nb) {
                uint16_t s = (uint16_t)(v->vram[sa & 0x3FFFF] | (v->vram[(sa & 0x3FFFF) + 0x40000] << 8));
                uint16_t d = (uint16_t)(v->vram[da & 0x3FFFF] | (v->vram[(da & 0x3FFFF) + 0x40000] << 8));
                uint16_t n = lop16(v->LOG, s, d), r = (uint16_t)((d & ~v->WM) | (n & v->WM));
                v->vram[da & 0x3FFFF] = (uint8_t)r; v->vram[(da & 0x3FFFF) + 0x40000] = (uint8_t)(r >> 8);
                sa = (sa + 1) & 0x3FFFF; da = (da + 1) & 0x3FFFF; nb--;
            }
        } else {
            while (nb) {
                uint8_t s = rd_bx(v, sa);
                unsigned ad = tr_bx(da);
                uint8_t d = v->vram[ad], n = lop(v->LOG, c_gbits(cm), s, d);
                uint8_t m = (uint8_t)((ad & 0x40000) ? (v->WM >> 8) : (v->WM & 0xFF));
                v->vram[ad] = (uint8_t)((d & ~m) | (n & m));
                sa = (sa + 1) & 0x7FFFF; da = (da + 1) & 0x7FFFF; nb--;
            }
        }
        cmd_ready(v);
        break;
    }
    case 0xB: {                                         /* LINE */
        uint16_t asx = (uint16_t)((v->NX - 1) / 2), adx = v->DX, anx = 0, tx = dx, ty = dy;
        for (;;) {
            c_psetc(v, cm, adx, v->DY, pitch, v->fg, v->WM, v->LOG);
            if (!(v->ARG & MAJ)) {
                adx = (uint16_t)(adx + tx);
                if (asx < v->NY) { asx = (uint16_t)(asx + v->NX); v->DY = (uint16_t)(v->DY + ty); }
            } else {
                v->DY = (uint16_t)(v->DY + ty);
                if (asx < v->NY) { asx = (uint16_t)(asx + v->NX); adx = (uint16_t)(adx + tx); }
            }
            asx = (uint16_t)(asx - v->NY);
            if (anx++ == v->NX || (adx & width)) break;
        }
        cmd_ready(v);
        break;
    }
    case 0xC: {                                         /* SRCH */
        uint16_t asx = v->SX;
        int aeq = (v->ARG & NEQ) != 0;
        for (;;) {
            int hit;
            if (cm == CM_BP16) {
                hit = (c_point(v, cm, asx, v->SY, pitch) == v->fg);
            } else {
                unsigned a = c_addr(cm, asx, v->SY, pitch);
                uint8_t val = v->vram[a], col = (uint8_t)((a & 0x40000) ? (v->fg >> 8) : (v->fg & 0xFF));
                uint8_t bm = cm == CM_BP2 ? 3 : cm == CM_BP8 ? 0xFF : 0x0F;
                uint8_t m2 = c_shift(cm, bm, 3, asx);
                hit = ((val & m2) == (col & m2));
            }
            if (hit ^ aeq) { v->cst |= BD; v->borderX = asx; break; }
            asx = (uint16_t)(asx + dx);
            if (asx & width) { v->cst &= (uint8_t)~BD; v->borderX = asx; break; }
        }
        cmd_ready(v);
        break;
    }
    case 0xD: {                                         /* POINT */
        uint16_t d = c_point(v, cm, v->SX, v->SY, pitch);
        v->cdata = (uint8_t)d;
        if (cm == CM_BP16) { v->cpart = (uint8_t)(d >> 8); v->cend = 0; } else v->cend = 1;
        v->cst |= TR;
        break;
    }
    case 0xE:                                           /* PSET */
        c_psetc(v, cm, v->DX, v->DY, pitch, v->fg, v->WM, v->LOG);
        cmd_ready(v);
        break;
    default: cmd_ready(v); break;                       /* ADVN (구현 안 됨) */
    }
}

static void cmd_reg(V9990 *v, int reg, uint8_t val)
{
    switch (reg - 32) {
    case 0: v->SX = (uint16_t)((v->SX & 0x0700) | val); break;
    case 1: v->SX = (uint16_t)((v->SX & 0x00FF) | ((val & 7) << 8)); break;
    case 2: v->SY = (uint16_t)((v->SY & 0x0F00) | val); break;
    case 3: v->SY = (uint16_t)((v->SY & 0x00FF) | ((val & 15) << 8)); break;
    case 4: v->DX = (uint16_t)((v->DX & 0x0700) | val); break;
    case 5: v->DX = (uint16_t)((v->DX & 0x00FF) | ((val & 7) << 8)); break;
    case 6: v->DY = (uint16_t)((v->DY & 0x0F00) | val); break;
    case 7: v->DY = (uint16_t)((v->DY & 0x00FF) | ((val & 15) << 8)); break;
    case 8: v->NX = (uint16_t)((v->NX & 0x0F00) | val); break;
    case 9: v->NX = (uint16_t)((v->NX & 0x00FF) | ((val & 15) << 8)); break;
    case 10: v->NY = (uint16_t)((v->NY & 0x0F00) | val); break;
    case 11: v->NY = (uint16_t)((v->NY & 0x00FF) | ((val & 15) << 8)); break;
    case 12: v->ARG = val & 15; break;
    case 13: v->LOG = val & 0x1F; break;
    case 14: v->WM = (uint16_t)((v->WM & 0xFF00) | val); break;
    case 15: v->WM = (uint16_t)((v->WM & 0x00FF) | (val << 8)); break;
    case 16: v->fg = (uint16_t)((v->fg & 0xFF00) | val); break;
    case 17: v->fg = (uint16_t)((v->fg & 0x00FF) | (val << 8)); break;
    case 18: v->bg = (uint16_t)((v->bg & 0xFF00) | val); break;
    case 19: v->bg = (uint16_t)((v->bg & 0x00FF) | (val << 8)); break;
    case 20: cmd_start(v, val); break;
    default: break;
    }
}

/* ---------- 레지스터 ---------- */
static inline unsigned get_addr(const V9990 *v, int base)
{
    return (unsigned)v->regs[base] + ((unsigned)v->regs[base + 1] << 8) + (((unsigned)v->regs[base + 2] & 7) << 16);
}
static inline void set_addr(V9990 *v, int base, unsigned a)
{
    v->regs[base] = (uint8_t)a;
    v->regs[base + 1] = (uint8_t)(a >> 8);
    v->regs[base + 2] = (uint8_t)(((a >> 16) & 7) | (v->regs[base + 2] & 0x80));
}
static void write_reg(V9990 *v, int reg, uint8_t val)
{
    if (!(reg_acc[reg] & ACC_W)) return;
    if (reg >= 32) { cmd_reg(v, reg, val); return; }
    val &= reg_mask[reg];
    if (reg == R_AY0 && v->disp_en) v->voff_a = v->cur_line;
    if (reg == R_BY0 && v->disp_en) v->voff_b = v->cur_line;
    v->regs[reg] = val;
    switch (reg) {
    case R_PALCTL: break;
    case R_WA2: break;
    case R_RA2: v->rdbuf = v->vram[map_addr(v, get_addr(v, R_RA0))]; break;
    case R_SM0: case R_SM1: v->mode = calc_mode(v); break;
    case R_INT0: break;
    default: break;
    }
}
static uint8_t read_reg(const V9990 *v, int reg)
{
    if (v->sysreset) return 0xFF;
    if (!(reg_acc[reg] & ACC_R)) return 0xFF;
    if (reg < R_BX_LO) return v->regs[reg];
    return reg == R_BX_LO ? (uint8_t)v->borderX : (uint8_t)(v->borderX >> 8);
}

/* ---------- 포트 ---------- */
void v9990_init(V9990 *v)
{
    
    memset(v, 0, sizeof *v);
    for (int i = 0; i < 0x80000; i += 1024) { memset(v->vram + i, 0x00, 512); memset(v->vram + i + 512, 0xFF, 512); }
    v9990_reset(v);
}
void v9990_reset(V9990 *v)
{
    memset(v->regs, 0, sizeof v->regs);
    v->status = 0; v->regsel = 0xFF; v->rdbuf = 0; v->sysreset = 0; v->pending = 0;
    v->mode = calc_mode(v);
    for (int i = 0; i < 64; i++) { pal_set(v, i * 4, 0x9F); pal_set(v, i * 4 + 1, 0x1F); pal_set(v, i * 4 + 2, 0x1F); pal_set(v, i * 4 + 3, 0); }
    v->CMD = 0; v->cst = 0; v->cend = 0; v->borderX = 0;
    v->disp_en = 0; v->shown = 0; v->rows = 240;
    v->voff_a = v->voff_b = 0;
    memset(v->fb, 0, sizeof v->fb);
}

uint8_t v9990_read(V9990 *v, int port, int tick)
{
    port &= 15;
    uint8_t r = 0xFF;
    switch (port) {
    case 0: r = v->rdbuf; break;
    case 1: r = v->pal[v->regs[R_PALPTR]]; break;
    case 2:
        if (v->cst & TR) {
            r = v->cdata;
            v->cst &= (uint8_t)~TR;
            if (v->cend) { v->cend = 0; cmd_ready(v); }
            else cmd_step(v);
        }
        return r;
    case 3: r = read_reg(v, v->regsel & 0x3F); break;
    case 5: {
        int left = left_tick(v), right = right_tick(v), top = top_line(v), bot = bottom_line(v);
        int hr = tick < left || tick >= right, vr = v->cur_line < top || v->cur_line >= bot;
        return (uint8_t)(v->cst | (vr ? 0x40 : 0) | (hr ? 0x20 : 0) | (v->status & 6));
    }
    case 6: return v->pending;
    default: return 0xFF;
    }
    if (v->sysreset) return r;
    switch (port) {
    case 0:
        if (!(v->regs[R_RA2] & 0x80)) {
            unsigned p = get_addr(v, R_RA0) + 1;
            set_addr(v, R_RA0, p);
            v->rdbuf = v->vram[map_addr(v, p)];
        }
        break;
    case 1:
        if (!(v->regs[R_PALCTL] & 0x10)) {
            uint8_t *pp = &v->regs[R_PALPTR];
            switch (*pp & 3) { case 0: case 1: *pp += 1; break; case 2: *pp += 2; break; default: *pp -= 3; break; }
        }
        break;
    case 3:
        if (!(v->regsel & 0x40)) v->regsel = (uint8_t)((v->regsel + 1) & ~0x40);
        break;
    }
    return r;
}

void v9990_write(V9990 *v, int port, uint8_t val)
{
    port &= 15;
    switch (port) {
    case 0: {
        if (v->sysreset) return;
        unsigned a = get_addr(v, R_WA0);
        v->vram[map_addr(v, a)] = val;
        if (!(v->regs[R_WA2] & 0x80)) set_addr(v, R_WA0, a + 1);
        break;
    }
    case 1: {
        if (v->sysreset) { pal_set(v, 0, 0); return; }
        uint8_t *pp = &v->regs[R_PALPTR];
        pal_set(v, *pp, val);
        switch (*pp & 3) { case 0: case 1: *pp += 1; break; case 2: *pp += 2; break; default: *pp -= 3; break; }
        break;
    }
    case 2:
        v->cdata = val;
        v->cst &= (uint8_t)~TR;
        cmd_step(v);
        break;
    case 3:
        if (v->sysreset) val = 0;
        write_reg(v, v->regsel & 0x3F, val);
        if (!(v->regsel & 0x80)) v->regsel = (uint8_t)((v->regsel & 0xC0) | ((v->regsel + 1) & 0x3F));
        break;
    case 4:
        v->regsel = v->sysreset ? 0 : val;
        break;
    case 6:
        v->pending &= (uint8_t)~val;
        break;
    case 7: {
        v->status = (uint8_t)((v->status & 0xFB) | ((val & 1) << 2));
        v->mode = calc_mode(v);
        int nr = (val & 2) != 0;
        if (nr != v->sysreset) {
            v->sysreset = (uint8_t)nr;
            if (nr) { for (int i = 0; i < 64; i++) write_reg(v, i, 0); v->pending = 0; }
        }
        break;
    }
    default: break;
    }
}

int v9990_visible(const V9990 *v) { return !v->sysreset && v->shown; }

/* ---------- 프레임·줄 ---------- */
static void frame_start(V9990 *v, int total)
{
    v->disp_en = (v->regs[R_CTL] & 0x80) != 0;
    v->pal_t = (v->regs[R_SM1] & 0x08) != 0;
    v->interlaced = (v->regs[R_SM1] & 0x02) != 0;
    v->ay_hi = v->regs[R_AY1];
    v->by_hi = v->regs[R_BY1];
    v->status ^= 0x02;
    v->total = total;
    v->voff_a = v->voff_b = top_line(v);
    v->rows = 240;
    if (v->disp_en && !v->sysreset) v->shown = 1;
}
void v9990_line_start(V9990 *v, int line, int total)
{
    v->cur_line = line;
    if (line == 0) frame_start(v, total);
    /* 표시가 끝나는 줄이 MSX 한 화면(total 줄)보다 뒤면(MSX 60 Hz 기계에 V9990 을 PAL 시간으로 둔 경우) 수직 인터럽트(VI)가 영영 안 와서
     * VI 로 시간을 재는 게임(Chase H.Q. V9990 의 turbo R 속도 늦추기 루프)이 멈춤 → 마지막 줄에서 세움 */
    if (line == bottom_line(v) || (line == total - 1 && bottom_line(v) >= total)) raise(v, 1);
    if (v->regs[R_INT2] & 0x80) raise(v, 2);
    else if (line == (int)v->regs[R_INT1] + 256 * (v->regs[R_INT2] & 3) + top_line(v)) raise(v, 2);
}

/* ---------- 그리기 ---------- */
static uint16_t c_bd8[256];
static int c8_ok;
static const uint8_t map_rg[8] = { 0, 4, 9, 13, 18, 22, 27, 31 }, map_b[4] = { 0, 11, 21, 31 };

static inline unsigned roll_mask(const V9990 *v, unsigned max)
{
    static const unsigned rm[4] = { 0xFFFF, 0x00FF, 0x01FF, 0x00FF };
    unsigned t = (unsigned)v->regs[R_AY1] >> 6;
    return t ? rm[t] : max;
}
static unsigned prio_x(const V9990 *v) { unsigned t = v->regs[R_PRIO] & 3; return t == 0 ? 256 : t << 6; }
static unsigned prio_y(const V9990 *v) { unsigned t = v->regs[R_PRIO] & 0x0C; return t == 0 ? 256 : t << 4; }

/* 패턴 모드 한 층의 점 (P1: 두 층 각각, P2: 한 층). 반환: 팔레트 번호 0~15 */
static inline unsigned p1_pix(const V9990 *v, unsigned X, unsigned Y, unsigned nametab, unsigned pattab)
{
    unsigned na = nametab + ((Y >> 3) * 64 + (X >> 3)) * 2;
    unsigned pn = (v->vram[na & 0x7FFFF] + v->vram[(na + 1) & 0x7FFFF] * 256u) & 0x1FFF;
    unsigned ad = pattab + (pn >> 5) * 1024 + (Y & 7) * 128 + (pn & 31) * 4 + ((X & 7) >> 1);
    uint8_t d = v->vram[ad & 0x7FFFF];
    return (X & 1) ? (d & 15) : (d >> 4);
}
static inline unsigned p2_pix(const V9990 *v, unsigned X, unsigned Y, int *odd)
{
    unsigned na = 0x7C000 + ((Y >> 3) * 128 + (X >> 3)) * 2;
    unsigned pn = (v->vram[na & 0x7FFFF] + v->vram[(na + 1) & 0x7FFFF] * 256u) & 0x1FFF;
    unsigned ad = (pn >> 6) * 2048 + (Y & 7) * 256 + (pn & 63) * 4 + ((X & 7) >> 1);
    uint8_t d = rd_bx(v, ad);
    *odd = (int)(ad & 1);
    return (X & 1) ? (d & 15) : (d >> 4);
}

static void sprites(const V9990 *v, uint16_t *buf, uint8_t *info, int n, unsigned dispY, int p2)
{
    const unsigned tab = 0x3FE00;
    int vis[17], cnt = 0, maxn = 16;
    unsigned spat = p2 ? ((unsigned)(v->regs[R_SPAT] & 0x0F) << 15) : ((unsigned)(v->regs[R_SPAT] & 0x0E) << 14);
    for (int s = 0; s < 125; s++) {
        unsigned ai = tab + 4u * (unsigned)s;
        uint8_t sy = (uint8_t)(v->vram[ai] + 1);
        uint8_t py = (uint8_t)(dispY - sy);
        if (py < 16) {
            if (v->vram[ai + 3] & 0x10) maxn--; else vis[cnt++] = s;
            if (cnt == maxn) break;
        }
    }
    vis[cnt] = -1;
    for (int k = 0; vis[k] != -1; k++) {
        unsigned a = tab + 4u * (unsigned)vis[k];
        uint8_t attr = v->vram[a + 3];
        int level = (attr & 0x20) ? 1 : 2;
        int sx = v->vram[a + 2] + 256 * (attr & 3);
        if (sx > 1008) sx -= 1024;
        uint8_t sy = v->vram[a], no = v->vram[a + 1];
        unsigned row = (uint8_t)(dispY - (uint8_t)(sy + 1));
        unsigned pa = spat + (p2 ? 256u * (((no & 0xE0u) >> 1) + row) + 8u * (no & 0x1F) : 128u * ((no & 0xF0u) + row) + 8u * (no & 0x0F));
        unsigned po = (attr >> 2) & 0x30;
        for (int x = 0; x < 16; x += 2) {
            uint8_t d = p2 ? rd_bx(v, pa) : v->vram[pa & 0x7FFFF];
            pa++;
            for (int h = 0; h < 2; h++) {
                int xp = sx + x + h;
                unsigned p = h ? (d & 15u) : (unsigned)(d >> 4);
                if (xp >= 0 && xp < n && p) {
                    if (info[xp] < level) buf[xp] = v->pal15[po + p];
                    info[xp] = 2;
                }
            }
        }
    }
}

static void line_p1(const V9990 *v, uint16_t *buf, int n, unsigned dispY, unsigned dYA, unsigned dYB)
{
    uint8_t info[256];
    unsigned px = prio_x(v), py = prio_y(v);
    if (dispY >= py) px = 0;
    unsigned roll = roll_mask(v, 0x1FF);
    unsigned sAY = v->regs[R_AY0] + 256u * v->ay_hi, sBY = v->regs[R_BY0] + 256u * v->by_hi;
    unsigned sAX = v->regs[R_AX0] + 8u * v->regs[R_AX1], sBX = v->regs[R_BX0] + 8u * v->regs[R_BX1];
    unsigned base = sAY & ~roll & 0x1FF;
    unsigned Ay = base + ((dYA + sAY) & roll), By = (dYB + sBY) & 0x1FF;
    unsigned off = v->regs[R_PALCTL] & 15;
    unsigned palA = (off & 3) << 4, palB = (off & 12) << 2;
    uint16_t bg = v->pal15[v->regs[R_BACK] & 63];
    for (int x = 0; x < n; x++) {
        unsigned a = p1_pix(v, (x + sAX) & 511, Ay & 511, 0x7C000, 0x00000);
        unsigned b = p1_pix(v, (x + sBX) & 511, By, 0x7E000, 0x40000);
        unsigned front, back, pf, pb;
        if ((unsigned)x < px) { front = a; pf = palA; back = b; pb = palB; }
        else { front = b; pf = palB; back = a; pb = palA; }
        if (front) { buf[x] = v->pal15[pf + front]; info[x] = 1; }
        else { info[x] = 0; buf[x] = back ? v->pal15[pb + back] : bg; }
    }
    if (!(v->regs[R_CTL] & 0x40)) sprites(v, buf, info, n, dispY, 0);
}
static void line_p2(const V9990 *v, uint16_t *buf, int n, unsigned dispY, unsigned dYA)
{
    uint8_t info[512];
    unsigned roll = roll_mask(v, 0x1FF);
    unsigned sY = v->regs[R_AY0] + 256u * v->ay_hi, sX = v->regs[R_AX0] + 8u * v->regs[R_AX1];
    unsigned base = sY & ~roll & 0x1FF;
    unsigned Ay = base + ((dYA + sY) & roll);
    unsigned off = v->regs[R_PALCTL] & 15;
    unsigned pal0 = (off & 3) << 4, pal1 = (off & 12) << 2;
    uint16_t bg = v->pal15[v->regs[R_BACK] & 63];
    for (int x = 0; x < n; x++) {
        int odd;
        unsigned p = p2_pix(v, (x + sX) & 1023, Ay & 511, &odd);
        info[x] = p != 0;
        buf[x] = p ? v->pal15[(odd ? pal1 : pal0) + p] : bg;
    }
    if (!(v->regs[R_CTL] & 0x40)) sprites(v, buf, info, n, dispY, 1);
}

static inline void yjk_group(const V9990 *v, uint16_t *out, unsigned addr, int yjk, int first)
{
    uint8_t d[4];
    for (int i = 0; i < 4; i++) d[i] = rd_bx(v, addr + (unsigned)i);
    int u = (d[2] & 7) + ((d[3] & 3) << 3) - ((d[3] & 4) << 3);
    int vv = (d[0] & 7) + ((d[1] & 3) << 3) - ((d[1] & 4) << 3);
    for (int i = first; i < 4; i++) {
        int y = (d[i] & 0xF8) >> 3;
        int r = y + u, g = (5 * y - 2 * u - vv) / 4, b = y + vv;
        r = r < 0 ? 0 : r > 31 ? 31 : r; g = g < 0 ? 0 : g > 31 ? 31 : g; b = b < 0 ? 0 : b > 31 ? 31 : b;
        if (yjk) { int t = g; g = b; b = t; }
        out[i - first] = (uint16_t)((g << 10) + (r << 5) + b);
    }
}

/* Bx 한 줄을 15비트로. x는 시작 점, y는 이미지 줄 */
static void raster_bx(const V9990 *v, uint16_t *out, int n, unsigned x, unsigned y)
{
    unsigned iw = image_width(v);
    int hi = v->mode == V9_B4 || v->mode == V9_B5 || v->mode == V9_B6 || v->mode == V9_B7;
    unsigned off = v->regs[R_PALCTL] & 15;
    switch (color_mode(v)) {
    case C_BD16: {
        unsigned a = 2 * (x + y * iw);
        for (int i = 0; i < n; i++, a += 2) out[i] = (uint16_t)((rd_bx(v, a) + 256 * rd_bx(v, a + 1)) & 0x7FFF);
        break;
    }
    case C_BD8: {
        unsigned a = x + y * iw;
        for (int i = 0; i < n; i++) out[i] = c_bd8[rd_bx(v, a++)];
        break;
    }
    case C_BP6: {
        unsigned a = x + y * iw;
        for (int i = 0; i < n; i++) out[i] = v->pal15[rd_bx(v, a++) & 0x3F];
        break;
    }
    case C_BP4: {
        unsigned a = (x + y * iw) / 2;
        unsigned ad = a;
        int i = 0;
        if (hi) {
            unsigned o = (off & 4) << 2;
            if (x & 1) { uint8_t d = rd_bx(v, ad++); out[i++] = v->pal15[o + (32 | (d & 15))]; }
            while (i < n) { uint8_t d = rd_bx(v, ad++); out[i++] = v->pal15[o + (d >> 4)]; if (i < n) out[i++] = v->pal15[o + (32 | (d & 15))]; }
        } else {
            unsigned o = (off & 12) << 2;
            if (x & 1) { uint8_t d = rd_bx(v, ad++); out[i++] = v->pal15[o + (d & 15)]; }
            while (i < n) { uint8_t d = rd_bx(v, ad++); out[i++] = v->pal15[o + (d >> 4)]; if (i < n) out[i++] = v->pal15[o + (d & 15)]; }
        }
        break;
    }
    case C_BP2: {
        unsigned ad = (x + y * iw) / 4;
        int i = 0;
        unsigned o = hi ? (off & 7) << 2 : off << 2;
        unsigned k = x & 3;
        while (i < n) {
            uint8_t d = rd_bx(v, ad++);
            for (; k < 4 && i < n; k++, i++) {
                unsigned p = (d >> (6 - 2 * k)) & 3;
                unsigned idx = hi ? (((x + (unsigned)i) & 1) ? 32 : 0) | p : p;
                out[i] = v->pal15[(o + idx) & 63];
            }
            k = 0;
        }
        break;
    }
    case C_YJK: case C_YUV: {
        unsigned a = (x & ~3u) + y * iw;
        int i = 0, first = (int)(x & 3);
        while (i < n) {
            uint16_t g[4];
            yjk_group(v, g, a, color_mode(v) == C_YJK, 0);
            a += 4;
            for (int k = first; k < 4 && i < n; k++) out[i++] = g[k];
            first = 0;
        }
        break;
    }
    }
}

typedef struct { unsigned x; uint32_t pat; uint16_t color; int xorf; } Cur;
static void cur_setup(const V9990 *v, Cur *c, unsigned attr, unsigned patA, int cy)
{
    c->x = (unsigned)-1; c->pat = 0;
    unsigned ay = rd_bx(v, attr) + (rd_bx(v, attr + 2) & 1u) * 256u;
    ay += v->interlaced ? 2 : 1;
    unsigned cl = ((unsigned)cy - ay) & 511;
    if (cl >= 32) return;
    uint8_t at = rd_bx(v, attr + 6);
    if ((at & 0x10) || (at & 0xE0) == 0) return;
    uint32_t pt = ((uint32_t)rd_bx(v, patA + 4 * cl) << 24) | ((uint32_t)rd_bx(v, patA + 4 * cl + 1) << 16) | ((uint32_t)rd_bx(v, patA + 4 * cl + 2) << 8) | rd_bx(v, patA + 4 * cl + 3);
    if (!pt) return;
    c->pat = pt;
    c->x = rd_bx(v, attr + 4) + (at & 3u) * 256u;
    c->xorf = (at & 0xE0) == 0x20;
    c->color = v->pal15[((unsigned)(v->regs[R_SPAL] << 2) + (at >> 6)) & 63];
    if (at & 0x20) c->color ^= 0x7FFF;
}
static inline int cur_dot(const Cur *c) { return c->x == 0 && (c->pat & 0x80000000u); }
static inline void cur_shift(Cur *c) { if (c->x) c->x--; else c->pat <<= 1; }

static void line_bx(const V9990 *v, uint16_t *buf, int n, unsigned dispY, unsigned dYA)
{
    int cyo = overscan(v) ? (v->pal_t ? 41 : 14) : 0;
    unsigned x = v->regs[R_AX0] + 8u * v->regs[R_AX1];
    int lstep = 1;
    int cursorY = (int)dispY - cyo;
    if (v->regs[R_SM1] & 4) {
        dYA *= 2;
        if (v->status & 2) { dispY++; dYA++; cursorY++; }
        lstep = 2;
    }
    (void)lstep;
    unsigned sY = v->regs[R_AY0] + 256u * v->ay_hi, roll = roll_mask(v, 0x1FFF);
    unsigned base = sY & ~roll & 0x1FFF;
    unsigned y = base + ((dYA + sY) & roll);
    raster_bx(v, buf, n, x, y);
    if (!(v->regs[R_CTL] & 0x40)) {
        Cur c0, c1;
        cur_setup(v, &c0, 0x7FE00, 0x7FF00, cursorY);
        cur_setup(v, &c1, 0x7FE08, 0x7FF80, cursorY);
        if (c0.x != (unsigned)-1 || c1.x != (unsigned)-1) {
            for (int i = 0; i < n; i++) {
                if (cur_dot(&c0)) { if (c0.xorf) buf[i] ^= 0x7FFF; else buf[i] = c0.color; }
                else if (cur_dot(&c1)) { if (c1.xorf) buf[i] ^= 0x7FFF; else buf[i] = c1.color; }
                cur_shift(&c0); cur_shift(&c1);
                if (!c0.pat && !c1.pat) break;
            }
        }
    }
}

static const int tpp_tab[10] = { 8, 4, 12, 8, 6, 4, 3, 8, 8, 2 };

void v9990_line_end(V9990 *v, int line)
{
    if (!c8_ok) {
        c8_ok = 1;
        for (int g = 0; g < 8; g++) for (int r = 0; r < 8; r++) for (int b = 0; b < 4; b++)
            c_bd8[(g << 5) | (r << 2) | b] = (uint16_t)((map_rg[g] << 10) | (map_rg[r] << 5) | map_b[b]);
    }
    int bl, b1, disp, b2;
    ver_timing(v, &bl, &b1, &disp, &b2);
    int row = line - (bl + b1 + (disp - 240) / 2);          /* openMSX 처럼 가운데 240줄만 (PAL 은 위아래를 잘라 냄) */
    if (row < 0 || row >= 240) return;
    uint32_t *out = v->fb[row];
    uint32_t border = overscan(v) ? 0 : rgb15(v->pal15[v->regs[R_BACK] & 63]);
    int top = top_line(v);
    if (!v->disp_en || v->sysreset || line < top || line >= top + disp) {
        uint32_t c = v->disp_en && !v->sysreset ? border : rgb15(v->pal15[v->regs[R_BACK] & 63]);
        if (overscan(v)) c = 0;
        for (int x = 0; x < V9_FBW; x++) out[x] = c;
        return;
    }
    int tpp = tpp_tab[v->mode];
    int dticks = overscan(v) ? 2304 : 2048;
    int colzero = overscan(v) ? 244 : 256;
    int ds = (left_tick(v) - colzero) / 4, dw = dticks / 4;
    int srcw = dticks / tpp;
    uint16_t *buf = v->lbuf;
    unsigned dispY = (unsigned)(line - top), dYA = (unsigned)(line - v->voff_a), dYB = (unsigned)(line - v->voff_b);
    if (v->mode == V9_P1) line_p1(v, buf, srcw, dispY, dYA, dYB);
    else if (v->mode == V9_P2) line_p2(v, buf, srcw, dispY, dYA);
    else line_bx(v, buf, srcw, dispY, dYA);
    /* openMSX 화면과 가로 폭을 맞춤 — Chase H.Q. (B3) 로 openMSX 그림을 대 보니 같은 그림이 가로 1.155 배 (37/32) 로 보임.
       표시 영역의 가운데를 기준으로 가로로 늘림 (영역 밖은 테두리색) */
    int cx = ds + dw / 2;
    for (int x = 0; x < V9_FBW; x++) {
        int t = (x - cx) * 32, q = t >= 0 ? t / 37 : -((-t + 36) / 37);
        int o = dw / 2 + q;
        if (o < 0 || o >= dw) { out[x] = border; continue; }
        int sx = o * 4 / tpp;
        if (sx >= srcw) sx = srcw - 1;
        out[x] = rgb15(buf[sx] & 0x7FFF);
    }
}

/* V9990 프레임의 줄 수 — PAL 시간표(R#7 비트 3)면 313, 아니면 262. MSX 프레임(262/313줄)과 따로 센다. */
int v9990_frame_lines(const V9990 *v) { return (v->regs[R_SM1] & 0x08) ? 313 : 262; }
