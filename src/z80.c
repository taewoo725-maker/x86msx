/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * z80.c - MSX1 에뮬레이터 Z80 코어
 *
 * 구조: z80_step()이 접두(DD/FD) 처리 후 exec_main / exec_ed / exec_cb로 나눈다.
 * DD/FD 접두는 idx(1 = IX, 2 = IY)로 전달되어 H, L, HL, (HL)을 IXH, IXL, IX, (IX+d)로 바꾼다.
 * T-state는 표준 Z80 값이며, M1 대기는 fetch_m1()에서 z->extra로 더한다.
 * 플래그 SZ5H3PNC: S·Z·5·3(·P) 는 표 sz53 / sz53p 로 한 번에, H·V·C 는 비트 연산으로 (ZEXALL 전부 통과).
 *
 * 버스 흐름:
 *   명령 가져오기 fetch_m1 → rd(PC) : R 레지스터 +1, M1 대기(MSX 는 1 T)를 더함
 *   rd(a)  → z->frd[a>>13] 이면 z->rdp[a>>13][a&0x1FFF] (분기 하나, 함수 호출 없음)
 *          → 아니면 z->mem_read(ctx, a)  = msx.c mem_read (특수 장치 사슬·FFFFh)
 *   wr(a)  → z->fwr[a>>13] 이면 z->wrp[...] 에 곧장, 아니면 z->mem_write = msx.c mem_write
 *   in/out → z->io_read / io_write = msx.c I/O 버스 (포트별 장치 사슬)
 *   (IX+d)/(IY+d): d 는 부호 있는 8비트 ((int8_t)), 주소 = (uint16_t)(IX + d), WZ = 그 주소.
 *   R800(turbo R): 같은 코어에 r800 = 1, 접근마다 r8_mem() 이 R800 클럭을 셈 (msx.c 가 Z80 시간으로 바꿈).
 */
#ifdef PC_TRACE
#include <stdio.h>
#include <stdlib.h>
#endif
#include "z80.h"
#ifdef VDP_TRACE
#include <stdio.h>
#endif
#include <string.h>

_Static_assert(sizeof(Z80Pair) == 2, "Z80Pair must be 2 bytes");

#define FC 0x01
#define FN 0x02
#define FP 0x04
#define FX 0x08
#define FH 0x10
#define FY 0x20
#define FZ 0x40
#define FS 0x80

static uint8_t sz53[256];       /* S, Z, X, Y */
static uint8_t sz53p[256];      /* S, Z, X, Y, P */
static int tables_ready;

static void init_tables(void)
{
    for (int i = 0; i < 256; i++) {
        uint8_t f = (uint8_t)((i & (FS | FX | FY)) | (i ? 0 : FZ));
        int p = i;
        p ^= p >> 4; p ^= p >> 2; p ^= p >> 1;
        sz53[i] = f;
        sz53p[i] = (uint8_t)(f | ((p & 1) ? 0 : FP));
    }
    tables_ready = 1;
}

#define RA z->af.b.h
#define RF z->af.b.l
#define RB z->bc.b.h
#define RC z->bc.b.l
#define RD z->de.b.h
#define RE z->de.b.l
#define RH z->hl.b.h
#define RL z->hl.b.l
#define AF z->af.w
#define BC z->bc.w
#define DE z->de.w
#define HL z->hl.w
#define SP z->sp.w
#define WZ z->wz.w
#define PC z->pc

/* 플래그를 쓰는 모든 명령은 SETF를 거친다 (Q 레지스터 추적) */
#define SETF(v) do { RF = (uint8_t)(v); z->q = RF; } while (0)

/* 메인 명령 기본 T-state (조건 성립 시 추가분은 코드에서 더함) */
static const uint8_t cyc_main[256] = {
/*        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */   4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,
/* 1 */   8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
/* 2 */   7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,
/* 3 */   7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
/* 4 */   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/* 5 */   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/* 6 */   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/* 7 */   7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
/* 8 */   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/* 9 */   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/* A */   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/* B */   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
/* C */   5,10,10,10,10,11, 7,11, 5,10,10, 0,10,17, 7,11,
/* D */   5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 0, 7,11,
/* E */   5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 0, 7,11,
/* F */   5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 0, 7,11,
};

/* ------------------------------------------------------------------ */
/* 버스 접근                                                           */
/* ------------------------------------------------------------------ */
static inline void r8_mem(Z80 *z, uint16_t a)          /* 3.4.2 ACC-01 (openMSX R800 시간 모형) */
{
    int pg = a >> 8;
    uint32_t d = z->r8_delay[a >> 14];
    z->r8c += 1 + d + ((pg != z->r8_page || d) ? 1u : 0u);
    z->r8_page = pg;
}
static inline uint8_t rd(Z80 *z, uint16_t a)
{
    if (z->r800) r8_mem(z, a);
    if (z->frd[a >> 13] && a != 0xFFFF) return z->rdp[a >> 13][a & 0x1FFF];
    return z->mem_read(z->ctx, a);
}
static inline void wr(Z80 *z, uint16_t a, uint8_t v)
{
    if (z->r800) r8_mem(z, a);
#ifdef PC_TRACE
    { static long ww = -2; if (ww == -2) { const char *e = getenv("ZWATCH"); ww = e ? strtol(e, 0, 16) : -1; } if (a == ww) fprintf(stderr, "ZW %04X=%02X pc %04X\n", a, v, z->pc); }
#endif
    if (z->fwr[a >> 13] && a != 0xFFFF) { z->wrp[a >> 13][a & 0x1FFF] = v; return; }
    z->mem_write(z->ctx, a, v);
}
static inline uint8_t in(Z80 *z, uint16_t p) { if (z->r800) { z->r8c += 7; z->r8_page = -1; } return z->io_read(z->ctx, p); }   /* I/O 6 + 버스 맞춤 1 */
static inline void out(Z80 *z, uint16_t p, uint8_t v) { if (z->r800) { z->r8c += 7; z->r8_page = -1; } z->io_write(z->ctx, p, v); }

static inline uint8_t imm8(Z80 *z) { return rd(z, PC++); }
static inline uint16_t imm16(Z80 *z)
{
    uint16_t lo = imm8(z);
    return (uint16_t)(lo | (imm8(z) << 8));
}
static inline uint16_t rd16(Z80 *z, uint16_t a)
{
    return (uint16_t)(rd(z, a) | (rd(z, (uint16_t)(a + 1)) << 8));
}
static inline void wr16(Z80 *z, uint16_t a, uint16_t v)
{
    wr(z, a, (uint8_t)v);
    wr(z, (uint16_t)(a + 1), (uint8_t)(v >> 8));
}
static inline void push(Z80 *z, uint16_t v)
{
    SP--; wr(z, SP, (uint8_t)(v >> 8));
    SP--; wr(z, SP, (uint8_t)v);
}
static inline uint16_t pop(Z80 *z)
{
    uint16_t v = rd(z, SP); SP++;
    v |= (uint16_t)(rd(z, SP) << 8); SP++;
    return v;
}
static inline void inc_r(Z80 *z) { z->r = (uint8_t)((z->r & 0x80) | ((z->r + 1) & 0x7F)); }
static inline uint8_t fetch_m1(Z80 *z)
{
    inc_r(z);
    z->extra += z->m1_wait;
    return rd(z, PC++);
}

/* ------------------------------------------------------------------ */
/* 레지스터 선택 (idx: 0 = HL, 1 = IX, 2 = IY)                          */
/* ------------------------------------------------------------------ */
static inline Z80Pair *hlp(Z80 *z, int idx)
{
    return idx == 1 ? &z->ix : idx == 2 ? &z->iy : &z->hl;
}
static inline uint8_t getr(Z80 *z, int r, int idx)
{
    switch (r) {
    case 0: return RB;
    case 1: return RC;
    case 2: return RD;
    case 3: return RE;
    case 4: return hlp(z, idx)->b.h;
    case 5: return hlp(z, idx)->b.l;
    default: return RA;
    }
}
static inline void setr(Z80 *z, int r, int idx, uint8_t v)
{
    switch (r) {
    case 0: RB = v; break;
    case 1: RC = v; break;
    case 2: RD = v; break;
    case 3: RE = v; break;
    case 4: hlp(z, idx)->b.h = v; break;
    case 5: hlp(z, idx)->b.l = v; break;
    default: RA = v; break;
    }
}
static inline uint16_t *rp(Z80 *z, int p, int idx)
{
    switch (p & 3) {
    case 0: return &BC;
    case 1: return &DE;
    case 2: return &hlp(z, idx)->w;
    default: return &SP;
    }
}
/* (HL) 또는 (IX+d)의 주소. 색인이면 변위를 읽고 8 T-state를 더한다. */
static inline uint16_t maddr(Z80 *z, int idx)
{
    if (!idx) return HL;
    int8_t d = (int8_t)imm8(z);
    uint16_t a = (uint16_t)(hlp(z, idx)->w + d);
    WZ = a;
    z->extra += 8;
    return a;
}
static inline int cond(Z80 *z, int cc)
{
    switch (cc & 7) {
    case 0: return !(RF & FZ);
    case 1: return RF & FZ;
    case 2: return !(RF & FC);
    case 3: return RF & FC;
    case 4: return !(RF & FP);
    case 5: return RF & FP;
    case 6: return !(RF & FS);
    default: return RF & FS;
    }
}

/* ------------------------------------------------------------------ */
/* 연산                                                                 */
/* ------------------------------------------------------------------ */
static void alu(Z80 *z, int op, uint8_t v)
{
    uint8_t a = RA;
    unsigned res;
    switch (op) {
    case 0: /* ADD */
    case 1: /* ADC */
        res = a + v + (op == 1 ? (RF & FC) : 0);
        SETF(sz53[res & 0xFF] | ((res >> 8) & FC) | ((a ^ v ^ res) & FH)
             | ((((a ^ ~v) & (a ^ res)) & 0x80) >> 5));
        RA = (uint8_t)res;
        break;
    case 2: /* SUB */
    case 3: /* SBC */
        res = a - v - (op == 3 ? (RF & FC) : 0);
        SETF(sz53[res & 0xFF] | ((res >> 8) & FC) | FN | ((a ^ v ^ res) & FH)
             | ((((a ^ v) & (a ^ res)) & 0x80) >> 5));
        RA = (uint8_t)res;
        break;
    case 4: RA = a & v; SETF(sz53p[RA] | FH); break;
    case 5: RA = a ^ v; SETF(sz53p[RA]); break;
    case 6: RA = a | v; SETF(sz53p[RA]); break;
    default: /* CP: X/Y는 피연산자에서 */
        res = a - v;
        SETF((sz53[res & 0xFF] & (FS | FZ)) | (v & (FX | FY)) | ((res >> 8) & FC) | FN
             | ((a ^ v ^ res) & FH) | ((((a ^ v) & (a ^ res)) & 0x80) >> 5));
        break;
    }
}

static uint8_t inc8(Z80 *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    SETF((RF & FC) | sz53[r] | ((v & 0x0F) == 0x0F ? FH : 0) | (v == 0x7F ? FP : 0));
    return r;
}
static uint8_t dec8(Z80 *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    SETF((RF & FC) | FN | sz53[r] | ((v & 0x0F) == 0 ? FH : 0) | (v == 0x80 ? FP : 0));
    return r;
}
static uint16_t add16(Z80 *z, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    WZ = (uint16_t)(a + 1);
    SETF((RF & (FS | FZ | FP)) | ((r >> 16) & FC) | ((r >> 8) & (FX | FY))
         | (((a ^ b ^ r) >> 8) & FH));
    return (uint16_t)r;
}
static void adc16(Z80 *z, uint16_t v)
{
    uint16_t a = HL;
    uint32_t r = (uint32_t)a + v + (RF & FC);
    WZ = (uint16_t)(a + 1);
    SETF(((r >> 16) & FC) | ((r >> 8) & (FS | FX | FY)) | ((r & 0xFFFF) ? 0 : FZ)
         | (((a ^ v ^ r) >> 8) & FH) | (((~(a ^ v)) & (a ^ r) & 0x8000) >> 13));
    HL = (uint16_t)r;
}
static void sbc16(Z80 *z, uint16_t v)
{
    uint16_t a = HL;
    uint32_t r = (uint32_t)a - v - (RF & FC);
    WZ = (uint16_t)(a + 1);
    SETF(FN | ((r >> 16) & FC) | ((r >> 8) & (FS | FX | FY)) | ((r & 0xFFFF) ? 0 : FZ)
         | (((a ^ v ^ r) >> 8) & FH) | (((a ^ v) & (a ^ r) & 0x8000) >> 13));
    HL = (uint16_t)r;
}

/* CB 회전/이동: 0 RLC 1 RRC 2 RL 3 RR 4 SLA 5 SRA 6 SLL(비문서화) 7 SRL */
static uint8_t rot(Z80 *z, int op, uint8_t v)
{
    uint8_t r, cf;
    switch (op) {
    case 0: cf = v >> 7; r = (uint8_t)((v << 1) | cf); break;
    case 1: cf = v & 1;  r = (uint8_t)((v >> 1) | (cf << 7)); break;
    case 2: cf = v >> 7; r = (uint8_t)((v << 1) | (RF & FC)); break;
    case 3: cf = v & 1;  r = (uint8_t)((v >> 1) | ((RF & FC) << 7)); break;
    case 4: cf = v >> 7; r = (uint8_t)(v << 1); break;
    case 5: cf = v & 1;  r = (uint8_t)((v >> 1) | (v & 0x80)); break;
    case 6: cf = v >> 7; r = (uint8_t)((v << 1) | 1); break;
    default: cf = v & 1; r = (uint8_t)(v >> 1); break;
    }
    SETF(sz53p[r] | cf);
    return r;
}
/* BIT: xy는 X/Y 플래그 출처 (레지스터 값, 또는 메모리 형식이면 MEMPTR 상위) */
static void bit(Z80 *z, int b, uint8_t v, uint8_t xy)
{
    uint8_t f = (uint8_t)((RF & FC) | FH | (xy & (FX | FY)));
    if (!(v & (1 << b))) f |= FZ | FP;
    else if (b == 7) f |= FS;
    SETF(f);
}

static void daa(Z80 *z)
{
    uint8_t a = RA, add = 0, cf = 0, h, res;
    if ((RF & FH) || (a & 0x0F) > 9) add = 0x06;
    if ((RF & FC) || a > 0x99) { add |= 0x60; cf = FC; }
    if (RF & FN) {
        res = (uint8_t)(a - add);
        h = ((RF & FH) && (a & 0x0F) < 6) ? FH : 0;
    } else {
        res = (uint8_t)(a + add);
        h = ((a & 0x0F) > 9) ? FH : 0;
    }
    SETF((RF & FN) | cf | h | sz53p[res]);
    RA = res;
}

/* ------------------------------------------------------------------ */
/* CB 접두                                                              */
/* ------------------------------------------------------------------ */
static int exec_cb(Z80 *z)
{
    uint8_t op = fetch_m1(z);
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
    if (r == 6) {
        uint8_t v = rd(z, HL), res;
        if (x == 1) { bit(z, y, v, z->wz.b.h); return 12; }
        if (x == 0) res = rot(z, y, v);
        else if (x == 2) res = (uint8_t)(v & ~(1 << y));
        else res = (uint8_t)(v | (1 << y));
        wr(z, HL, res);
        return 15;
    }
    uint8_t v = getr(z, r, 0);
    if (x == 1) { bit(z, y, v, v); return 8; }
    if (x == 0) v = rot(z, y, v);
    else if (x == 2) v = (uint8_t)(v & ~(1 << y));
    else v = (uint8_t)(v | (1 << y));
    setr(z, r, 0, v);
    return 8;
}

/* DD CB d op / FD CB d op: d와 op는 M1이 아닌 일반 읽기. 반환값은 DD 접두 4 제외. */
static int exec_idx_cb(Z80 *z, int idx)
{
    int8_t d = (int8_t)imm8(z);
    uint8_t op = imm8(z);
    uint16_t a = (uint16_t)(hlp(z, idx)->w + d);
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
    uint8_t v = rd(z, a), res;
    WZ = a;
    if (x == 1) { bit(z, y, v, (uint8_t)(a >> 8)); return 16; }
    if (x == 0) res = rot(z, y, v);
    else if (x == 2) res = (uint8_t)(v & ~(1 << y));
    else res = (uint8_t)(v | (1 << y));
    wr(z, a, res);
    if (r != 6) setr(z, r, 0, res);    /* 비문서화: 결과를 레지스터에도 복사 */
    return 19;
}

/* ------------------------------------------------------------------ */
/* ED 접두                                                              */
/* ------------------------------------------------------------------ */
static int block_op(Z80 *z, uint8_t op)
{
    int y = (op >> 3) & 7, kind = op & 7;
    int dec = y & 1, rep = y >= 6;
    uint16_t step = dec ? 0xFFFF : 1;

    switch (kind) {
    case 0: { /* LDI LDD LDIR LDDR */
        uint8_t v = rd(z, HL);
        wr(z, DE, v);
        HL += step; DE += step; BC--;
        uint8_t n = (uint8_t)(v + RA);
        uint8_t f = (uint8_t)((RF & (FS | FZ | FC)) | (BC ? FP : 0) | (n & FX) | ((n & 0x02) << 4));
        if (rep && BC) {
            PC -= 2; WZ = (uint16_t)(PC + 1);
            f = (uint8_t)((f & ~(FX | FY)) | ((PC >> 8) & (FX | FY)));
            SETF(f);
            return 21;
        }
        SETF(f);
        return 16;
    }
    case 1: { /* CPI CPD CPIR CPDR */
        uint8_t v = rd(z, HL);
        uint8_t res = (uint8_t)(RA - v);
        uint8_t h = (uint8_t)((RA ^ v ^ res) & FH);
        uint8_t n = (uint8_t)(res - (h ? 1 : 0));
        HL += step; BC--; WZ += step;
        uint8_t f = (uint8_t)((RF & FC) | FN | (sz53[res] & (FS | FZ)) | h | (BC ? FP : 0)
                              | (n & FX) | ((n & 0x02) << 4));
        if (rep && BC && res) {
            PC -= 2; WZ = (uint16_t)(PC + 1);
            f = (uint8_t)((f & ~(FX | FY)) | ((PC >> 8) & (FX | FY)));
            SETF(f);
            return 21;
        }
        SETF(f);
        return 16;
    }
    case 2: { /* INI IND INIR INDR */
        uint8_t v = in(z, BC);
        WZ = (uint16_t)(BC + step);
        wr(z, HL, v);
        RB--; HL += step;
        unsigned k = v + (uint8_t)(RC + (dec ? -1 : 1));
        SETF(sz53[RB] | ((v & 0x80) ? FN : 0) | (k > 0xFF ? (FH | FC) : 0)
             | (sz53p[(k & 7) ^ RB] & FP));
        if (rep && RB) { PC -= 2; return 21; }
        return 16;
    }
    case 3: { /* OUTI OUTD OTIR OTDR */
        uint8_t v = rd(z, HL);
        RB--;
        WZ = (uint16_t)(BC + step);
        out(z, BC, v);
        HL += step;
        unsigned k = v + RL;
        SETF(sz53[RB] | ((v & 0x80) ? FN : 0) | (k > 0xFF ? (FH | FC) : 0)
             | (sz53p[(k & 7) ^ RB] & FP));
        if (rep && RB) { PC -= 2; return 21; }
        return 16;
    }
    }
    return 8;
}

static int exec_ed(Z80 *z)
{
    uint8_t op = fetch_m1(z);

    if (op >= 0xA0 && op <= 0xBF && (op & 7) < 4)
        return block_op(z, op);
    if (z->r800 && (op == 0xC1 || op == 0xC9 || op == 0xD1 || op == 0xD9)) {   /* R800 MULUB A,B/C/D/E: HL = A * r */
        uint8_t r = op == 0xC1 ? z->bc.b.h : op == 0xC9 ? z->bc.b.l : op == 0xD1 ? z->de.b.h : z->de.b.l;
        uint16_t v = (uint16_t)(z->af.b.h * r);
        z->hl.w = v;
        z->af.b.l = (uint8_t)((z->af.b.l & 0x28) | (v ? 0 : 0x40) | ((v & 0xFF00) ? 0x01 : 0));
        z->r8c += 12;                               /* MULUB 14클럭 (가져오기 2 + 곱 12) */
        return 14;
    }
    if (z->r800 && (op == 0xC3 || op == 0xF3)) {                              /* R800 MULUW HL,BC/SP: DE:HL = HL * rr */
        uint32_t v = (uint32_t)z->hl.w * (op == 0xC3 ? z->bc.w : z->sp.w);
        z->hl.w = (uint16_t)v;
        z->de.w = (uint16_t)(v >> 16);
        z->af.b.l = (uint8_t)((z->af.b.l & 0x28) | (v ? 0 : 0x40) | ((v >> 16) ? 0x01 : 0));
        z->r8c += 34;                               /* MULUW 36클럭 */
        return 36;
    }
    if (op < 0x40 || op >= 0x80)
        return 8;                                   /* 정의되지 않은 ED: NOP 2개 */

    int y = (op >> 3) & 7, p = y >> 1, q = y & 1;
    switch (op & 7) {
    case 0: { /* IN r,(C) / IN (C) */
        uint8_t v = in(z, BC);
        WZ = (uint16_t)(BC + 1);
        SETF((RF & FC) | sz53p[v]);
        if (y != 6) setr(z, y, 0, v);
        return 12;
    }
    case 1: /* OUT (C),r / OUT (C),0 */
        out(z, BC, y == 6 ? 0 : getr(z, y, 0));
        WZ = (uint16_t)(BC + 1);
        return 12;
    case 2:
        if (q) adc16(z, *rp(z, p, 0)); else sbc16(z, *rp(z, p, 0));
        return 15;
    case 3: {
        uint16_t a = imm16(z);
        if (q) *rp(z, p, 0) = rd16(z, a); else wr16(z, a, *rp(z, p, 0));
        WZ = (uint16_t)(a + 1);
        return 20;
    }
    case 4: { /* NEG */
        uint8_t v = RA;
        RA = 0;
        alu(z, 2, v);
        return 8;
    }
    case 5: /* RETN / RETI */
        z->iff1 = z->iff2;
        PC = pop(z); WZ = PC;
        return 14;
    case 6: { /* IM */
        static const uint8_t mode[4] = { 0, 0, 1, 2 };
        z->im = mode[y & 3];
        return 8;
    }
    default:
        switch (y) {
        case 0: z->i = RA; return 9;
        case 1: z->r = RA; return 9;
        case 2: RA = z->i; SETF((RF & FC) | sz53[RA] | (z->iff2 ? FP : 0)); return 9;
        case 3: RA = z->r; SETF((RF & FC) | sz53[RA] | (z->iff2 ? FP : 0)); return 9;
        case 4: { /* RRD */
            uint8_t v = rd(z, HL);
            wr(z, HL, (uint8_t)((RA << 4) | (v >> 4)));
            RA = (uint8_t)((RA & 0xF0) | (v & 0x0F));
            WZ = (uint16_t)(HL + 1);
            SETF((RF & FC) | sz53p[RA]);
            return 18;
        }
        case 5: { /* RLD */
            uint8_t v = rd(z, HL);
            wr(z, HL, (uint8_t)((v << 4) | (RA & 0x0F)));
            RA = (uint8_t)((RA & 0xF0) | (v >> 4));
            WZ = (uint16_t)(HL + 1);
            SETF((RF & FC) | sz53p[RA]);
            return 18;
        }
        default: return 8;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 메인 명령 (idx: 0 = HL, 1 = IX, 2 = IY)                              */
/* ------------------------------------------------------------------ */
static int exec_main(Z80 *z, uint8_t op, int idx)
{
    int cyc = cyc_main[op];

    /* LD r,r' / LD r,(HL) / LD (HL),r / HALT */
    if (op >= 0x40 && op < 0x80) {
        int d = (op >> 3) & 7, s = op & 7;
        if (op == 0x76) { z->halted = 1; if (z->halt_ei && !z->iff1) z->iff1 = z->iff2 = 1; return cyc; }
        if (s == 6) setr(z, d, 0, rd(z, maddr(z, idx)));       /* (IX+d)일 때 H/L은 원래 H/L */
        else if (d == 6) wr(z, maddr(z, idx), getr(z, s, 0));
        else setr(z, d, idx, getr(z, s, idx));
        return cyc;
    }
    /* ALU A,r */
    if (op >= 0x80 && op < 0xC0) {
        int s = op & 7;
        uint8_t v = (s == 6) ? rd(z, maddr(z, idx)) : getr(z, s, idx);
        alu(z, (op >> 3) & 7, v);
        return cyc;
    }
    /* 0x00-0x3F의 INC r / DEC r / LD r,n */
    if (op < 0x40) {
        int y = (op >> 3) & 7;
        switch (op & 7) {
        case 4:
            if (y == 6) { uint16_t a = maddr(z, idx); wr(z, a, inc8(z, rd(z, a))); }
            else setr(z, y, idx, inc8(z, getr(z, y, idx)));
            return cyc;
        case 5:
            if (y == 6) { uint16_t a = maddr(z, idx); wr(z, a, dec8(z, rd(z, a))); }
            else setr(z, y, idx, dec8(z, getr(z, y, idx)));
            return cyc;
        case 6:
            if (y == 6) {
                uint16_t a = maddr(z, idx);
                if (idx) z->extra -= 3;                       /* LD (IX+d),n = 19 */
                wr(z, a, imm8(z));
            } else setr(z, y, idx, imm8(z));
            return cyc;
        }
    }

    switch (op) {
    case 0x00: break;
    case 0x01: case 0x11: case 0x21: case 0x31: *rp(z, op >> 4, idx) = imm16(z); break;
    case 0x02: wr(z, BC, RA); WZ = (uint16_t)(((BC + 1) & 0xFF) | (RA << 8)); break;
    case 0x12: wr(z, DE, RA); WZ = (uint16_t)(((DE + 1) & 0xFF) | (RA << 8)); break;
    case 0x0A: RA = rd(z, BC); WZ = (uint16_t)(BC + 1); break;
    case 0x1A: RA = rd(z, DE); WZ = (uint16_t)(DE + 1); break;
    case 0x03: case 0x13: case 0x23: case 0x33: (*rp(z, op >> 4, idx))++; break;
    case 0x0B: case 0x1B: case 0x2B: case 0x3B: (*rp(z, op >> 4, idx))--; break;
    case 0x09: case 0x19: case 0x29: case 0x39: {
        Z80Pair *h = hlp(z, idx);
        h->w = add16(z, h->w, *rp(z, op >> 4, idx));
        break;
    }
    case 0x07: RA = (uint8_t)((RA << 1) | (RA >> 7));
               SETF((RF & (FS | FZ | FP)) | (RA & (FX | FY | FC))); break;
    case 0x0F: { uint8_t cf = RA & 1; RA = (uint8_t)((RA >> 1) | (cf << 7));
               SETF((RF & (FS | FZ | FP)) | (RA & (FX | FY)) | cf); break; }
    case 0x17: { uint8_t cf = RA >> 7; RA = (uint8_t)((RA << 1) | (RF & FC));
               SETF((RF & (FS | FZ | FP)) | (RA & (FX | FY)) | cf); break; }
    case 0x1F: { uint8_t cf = RA & 1; RA = (uint8_t)((RA >> 1) | ((RF & FC) << 7));
               SETF((RF & (FS | FZ | FP)) | (RA & (FX | FY)) | cf); break; }
    case 0x08: { Z80Pair t = z->af; z->af = z->af_; z->af_ = t; break; }
    case 0x10: { int8_t d = (int8_t)imm8(z); RB--;
               if (RB) { PC = (uint16_t)(PC + d); WZ = PC; cyc += 5; } break; }
    case 0x18: { int8_t d = (int8_t)imm8(z); PC = (uint16_t)(PC + d); WZ = PC; break; }
    case 0x20: case 0x28: case 0x30: case 0x38: {
        int8_t d = (int8_t)imm8(z);
        if (cond(z, (op >> 3) & 3)) { PC = (uint16_t)(PC + d); WZ = PC; cyc += 5; }
        break;
    }
    case 0x22: { uint16_t a = imm16(z); wr16(z, a, hlp(z, idx)->w); WZ = (uint16_t)(a + 1); break; }
    case 0x2A: { uint16_t a = imm16(z); hlp(z, idx)->w = rd16(z, a); WZ = (uint16_t)(a + 1); break; }
    case 0x32: { uint16_t a = imm16(z); wr(z, a, RA); WZ = (uint16_t)(((a + 1) & 0xFF) | (RA << 8)); break; }
    case 0x3A: { uint16_t a = imm16(z); RA = rd(z, a); WZ = (uint16_t)(a + 1); break; }
    case 0x27: daa(z); break;
    case 0x2F: RA = (uint8_t)~RA;
               SETF((RF & (FS | FZ | FP | FC)) | FH | FN | (RA & (FX | FY))); break;
    case 0x37: SETF((RF & (FS | FZ | FP)) | FC | (((z->q_prev ^ RF) | RA) & (FX | FY))); break;
    case 0x3F: SETF(((RF & (FS | FZ | FP | FC)) | ((RF & FC) << 4)
                     | (((z->q_prev ^ RF) | RA) & (FX | FY))) ^ FC); break;

    /* C0-FF */
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        if (cond(z, (op >> 3) & 7)) { PC = pop(z); WZ = PC; cyc += 6; }
        break;
    case 0xC1: case 0xD1: case 0xE1: *rp(z, (op >> 4) & 3, idx) = pop(z); break;
    case 0xF1: AF = pop(z); break;
    case 0xC5: case 0xD5: case 0xE5: push(z, *rp(z, (op >> 4) & 3, idx)); break;
    case 0xF5: push(z, AF); break;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
        uint16_t a = imm16(z); WZ = a;
        if (cond(z, (op >> 3) & 7)) PC = a;
        break;
    }
    case 0xC3: PC = imm16(z); WZ = PC; break;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
        uint16_t a = imm16(z); WZ = a;
        if (cond(z, (op >> 3) & 7)) { push(z, PC); PC = a; cyc += 7; }
        break;
    }
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        alu(z, (op >> 3) & 7, imm8(z)); break;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        push(z, PC); PC = op & 0x38; WZ = PC; break;
    case 0xC9: PC = pop(z); WZ = PC; break;
    case 0xCB: return idx ? exec_idx_cb(z, idx) : exec_cb(z);
    case 0xCD: { uint16_t a = imm16(z); push(z, PC); PC = a; WZ = a; break; }
    case 0xD3: { uint8_t n = imm8(z); out(z, (uint16_t)((RA << 8) | n), RA);
               WZ = (uint16_t)(((n + 1) & 0xFF) | (RA << 8)); break; }
    case 0xDB: { uint16_t port = (uint16_t)((RA << 8) | imm8(z)); RA = in(z, port);
               WZ = (uint16_t)(port + 1); break; }
    case 0xD9: { Z80Pair t;
               t = z->bc; z->bc = z->bc_; z->bc_ = t;
               t = z->de; z->de = z->de_; z->de_ = t;
               t = z->hl; z->hl = z->hl_; z->hl_ = t; break; }
    case 0xE3: { Z80Pair *h = hlp(z, idx); uint16_t t = rd16(z, SP);
               wr16(z, SP, h->w); h->w = t; WZ = t; break; }
    case 0xE9: PC = hlp(z, idx)->w; break;
    case 0xEB: { uint16_t t = DE; DE = HL; HL = t; break; }   /* DD/FD 영향 없음 */
    case 0xF3: z->iff1 = z->iff2 = 0; break;
    case 0xFB: z->iff1 = z->iff2 = 1; z->ei_delay = 1; break;
    case 0xF9: SP = hlp(z, idx)->w; break;
    default: break;
    }
    return cyc;
}

/* ------------------------------------------------------------------ */
/* 공개 함수                                                            */
/* ------------------------------------------------------------------ */
void z80_init(Z80 *z)
{
    if (!tables_ready) init_tables();
    memset(z, 0, sizeof *z);
    z80_reset(z);
}

void z80_reset(Z80 *z)
{
    z->af.w = z->bc.w = z->de.w = z->hl.w = 0xFFFF;
    z->af_.w = z->bc_.w = z->de_.w = z->hl_.w = 0xFFFF;
    z->ix.w = z->iy.w = z->sp.w = 0xFFFF;
    z->wz.w = 0;
    z->pc = 0;
    z->i = z->r = 0;
    z->iff1 = z->iff2 = 0;
    z->im = 0;
    z->halted = 0;
    z->ei_delay = 0;
    z->q = z->q_prev = 0;
    z->int_line = 0;
    z->int_vec = 0xFF;
    z->r8c = 0; z->r8_page = -1;
}

static int accept_int(Z80 *z)
{
    z->halted = 0;
    z->iff1 = z->iff2 = 0;
    inc_r(z);
    z->extra += z->m1_wait;
    push(z, PC);
    if (z->im == 2) {
        PC = rd16(z, (uint16_t)((z->i << 8) | z->int_vec));   /* MSX 데이터 버스는 보통 0xFF (3.4.1: SFG 벡터) */
        WZ = PC;
        return 19;
    }
    PC = 0x0038;                                        /* IM 1, 그리고 0xFF(RST 38h)를 받은 IM 0 */
    WZ = PC;
    return 13;
}

int z80_step(Z80 *z)
{
    int cyc;
    z->extra = 0;
    z->q_prev = z->q;
    z->q = 0;

    if (z->int_line && z->iff1 && !z->ei_delay) {
#ifdef VDP_TRACE
        { extern int vdp_trace_line; extern unsigned long long vdp_trace_frame; fprintf(stderr, "INT f%llu l%d pc=%04X\n", vdp_trace_frame, vdp_trace_line, z->pc); }
#endif
        cyc = accept_int(z);
    } else {
        z->ei_delay = 0;
        if (z->halted) {
            inc_r(z);
            z->extra += z->m1_wait;
            cyc = 4;
        } else {
            uint8_t op = fetch_m1(z);
            int idx = 0;
            cyc = 0;
            while (op == 0xDD || op == 0xFD) {
                idx = (op == 0xDD) ? 1 : 2;
                cyc += 4;
                op = fetch_m1(z);
            }
            cyc += (op == 0xED) ? exec_ed(z) : exec_main(z, op, idx);
        }
    }
    cyc += z->extra;
    z->cycles += (uint64_t)cyc;
    return cyc;
}

/* 여러 명령 이어 실행 (z80.h). 지금은 z80_step 반복 — 7단계에서 어셈블러 고리로 바뀜 */
int z80_run(Z80 *z, uint64_t end, const uint64_t *next)
{
    int r = z->r800, t;                           /* 도중에 R800 전환(I/O 쓰기)이 일어나면 바로 돌아가 호출쪽이 시간 환산을 바꾸게 함 */
    do t = z80_step(z); while (z->cycles < end && z->cycles < *next && z->r800 == r);
    return t;
}
