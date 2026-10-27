/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* amddisp.c - AMD Renoir(DCN 2.1) HDMI 켜기 전용 (4.0.116: DisplaySelect 1번 v53 에서 켜는 순서만 남김; 진단·시험 단계 제거)
 *
 * 순서: 파이프1 타이밍(40) → 42 → 43 → OTG1(44) → DIG1 FE + VBIOS SetPixelClock(70) → VBIOS Transmitter + DIG/OPP/ODM/AFMT/HUBP1/OTG1 + 소리 GPU쪽(71) / 끄기 = 원값 복원.
 * 주소는 전부 BAR5 dword 인덱스. RD/WR 은 idx&0x8000 이면 BASE_IDX1(0x00C0) 아니면 BASE_IDX2(0x34C0) 기준.
 * ATOM 인터프리터는 Linux drivers/gpu/drm/amd/amdgpu/atom.c (Copyright 2008 AMD, Stanislaw Skowronek, MIT) 의 연산 의미를 옮긴 것 (THIRD-PARTY-NOTICES 참조). */
#include "amddisp.h"

#define RD(mm, idx) (((volatile uint32_t *)(mm))[(((uint32_t)(idx) & 0x8000u) ? 0x00C0u : 0x34C0u) + ((uint32_t)(idx) & 0x7FFFu)])
#define WR(mm, idx, v) (((volatile uint32_t *)(mm))[(((uint32_t)(idx) & 0x8000u) ? 0x00C0u : 0x34C0u) + ((uint32_t)(idx) & 0x7FFFu)] = (uint32_t)(v))

/* ---- 시간: TSC 를 UEFI Stall 로 한 번 보정 (tpu = 마이크로초당 TSC 틱) ---- */
#ifdef HOST
extern uint64_t fake_tsc(void);
static inline uint64_t tsc(void) { return fake_tsc(); }
#else
static inline uint64_t tsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
#endif
static uint64_t tpu = 2400;
static void (*stall_fn)(uint32_t);
void amddisp_timing(void (*stall)(uint32_t))
{
    stall_fn = stall;
    if (!stall) return;
    uint64_t t = tsc(); stall(20000); uint64_t d = tsc() - t;
    if (d > 20000ull * 500 && d < 20000ull * 10000) tpu = d / 20000;
}
static void wait_us(uint32_t us)
{
    if (stall_fn && us >= 1000) { stall_fn(us); return; }
    uint64_t t = tsc(), e = (uint64_t)us * tpu; while (tsc() - t < e) ;
}

/* ---- 기록 (LOG\AMD_HDMI.TXT 용) ---- */
static void hx(char *o, uint32_t v, int digits)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = 0; i < digits; i++) o[i] = H[(v >> ((digits - 1 - i) * 4)) & 15];
}
static int put(char *l, const char *t) { int n = 0; while (*t) l[n++] = *t++; l[n] = 0; return n; }
static char hist[400][64];
static int nhist;
static void hadd(const char *s) { if (nhist >= 400) return; int n = 0; while (s[n] && n < 63) { hist[nhist][n] = s[n]; n++; } hist[nhist][n] = 0; nhist++; }
int amddisp_hist(char (*lines)[64], int max)
{
    int k = 0;
    for (int i = 0; i < nhist && k < max; i++, k++) { int n = 0; while (hist[i][n]) { lines[k][n] = hist[i][n]; n++; } lines[k][n] = 0; }
    return k;
}

/* ---- 상태 ---- */
static struct { uint16_t idx; uint32_t orig; } av[128];   /* 절대 인덱스로 쓴 것(ATOM·w5) 원값: 끌 때 역순 복원 */
static struct { uint16_t idx; uint32_t orig; } sv[64];    /* RD/WR 기준 인덱스로 쓴 것(pw) 원값 */
static int nav, nsv, bad, at_dead;
static uint32_t pf0, pb0, pa0;       /* 내장 패널(eDP) 기준값: 쓰는 동안 이게 바뀌면 중단 */
static volatile uint32_t *MM;

static int edp_alive(void)
{ return (RD(MM, 0x1b41) & 1) && RD(MM, 0x2068) == pf0 && RD(MM, 0x20af) == pb0 && RD(MM, 0x060a) == pa0; }

static uint32_t pw(uint32_t idx, uint32_t w)
{
    int f = 0;
    for (int i = 0; i < nsv; i++) if (sv[i].idx == idx) f = 1;
    if (!f && nsv < 64) { sv[nsv].idx = (uint16_t)idx; sv[nsv].orig = RD(MM, idx); nsv++; }
    WR(MM, idx, w);
    uint32_t r = RD(MM, idx);
    char b[64]; int n = put(b, "amd pw "); hx(b + n, idx & 0x7FFFu, 4); n += 4; b[n++] = ':'; hx(b + n, w, 8); n += 8; b[n++] = '>'; hx(b + n, r, 8); n += 8; b[n] = 0; hadd(b);
    if (r == 0xFFFFFFFFu) bad = 1;
    return r;
}
static uint32_t w5(uint32_t idx, uint32_t v)
{   /* 절대 인덱스 쓰기 1회: 원값 저장, 읽어서 확인 */
    volatile uint32_t *p = MM; char b[64]; int n, f = 0;
    for (int i = 0; i < nav; i++) if (av[i].idx == idx) f = 1;
    if (!f && nav < 128) { av[nav].idx = (uint16_t)idx; av[nav].orig = p[idx]; nav++; }
    uint32_t o = p[idx]; p[idx] = v; uint32_t r = p[idx];
    n = put(b, "amd w5 "); hx(b + n, idx, 4); n += 4; b[n++] = ':'; hx(b + n, o, 8); n += 8; b[n++] = '>'; hx(b + n, v, 8); n += 8; b[n++] = '='; hx(b + n, r, 8); n += 8; b[n] = 0; hadd(b);
    if (r == 0xFFFFFFFFu) { bad = 1; hadd("amd w5 FFFFFFFF - stop"); }
    return o;
}

/* ---- ATOM(VBIOS) 인터프리터 ---- */
#define AT_OP_CNT 127
#define AT_OP_EOT 91
#define AT_ARG_REG 0
#define AT_ARG_PS 1
#define AT_ARG_WS 2
#define AT_ARG_FB 3
#define AT_ARG_ID 4
#define AT_ARG_IMM 5
#define AT_ARG_PLL 6
#define AT_ARG_MC 7
#define AT_IO_MM 0
#define AT_IO_PCI 1
#define AT_IO_SYSIO 2
#define AT_MAXDEPTH 8
#define AT_MAXOPS 300000
enum { AC_ABOVE, AC_ABOVEEQ, AC_ALWAYS, AC_BELOW, AC_BELOWEQ, AC_EQUAL, AC_NOTEQ };

static const uint8_t *av_bios;
static uint32_t av_size;
void amddisp_setbios(const uint8_t *p, uint32_t n) { av_bios = p; av_size = n; }
static struct {
    uint32_t cmd_table, data_table;
    uint16_t iio[256];
    uint16_t data_block, io_attr, reg_block;
    uint32_t fb_base, divmul[2];
    uint8_t shift;
    int cs_equal, cs_above, io_mode;
    uint32_t scratch[64];
    int depth, abort;
    uint32_t nops;
    uint64_t t_end;
} A;
static uint32_t at_nwr, at_ndeny;
/* VBIOS 가 쓸 수 있는 레지스터 허용목록 (SetPixelClock·Transmitter 호스트 추적에서 뽑음; 이 밖은 거부) */
static const uint16_t at_allow[] = {0x566F,0x5670,0x56A1,0x5D2F,0x5D30,0x5E0F,0x5EC1,0x5EC8,0x5EC9,0x5ECB,0x5ECC,0x5ECD,0x5ECF,0x5ED8,0x5EDA,0x5EDB,0x5EDC,0x5EDE,0x5EDF,0x5EE0,0x5EE1,0x5EE2,0x5EE3,0x5EE4,0x5EE5,0x5EE6,0x5EE7,0x5EE8,0x5EE9,0x5EEA,0x0101,0x0144,0x0147,0x5EC0,0x569A,0x562B,0x5628};

static uint8_t  a8 (uint32_t p) { if (p >= av_size) { A.abort = 1; return 0; } return av_bios[p]; }
static uint16_t a16(uint32_t p) { return (uint16_t)(a8(p) | ((uint16_t)a8(p + 1) << 8)); }
static uint32_t a32(uint32_t p) { return a16(p) | ((uint32_t)a16(p + 2) << 16); }

static uint32_t at_rd(uint32_t idx)
{
    if (!((idx >= 0x00C0u && idx < 0x1000u) || (idx >= 0x34C0u && idx < 0x9000u))) return 0;   /* DCN 영역만 실제 읽기 */
    uint32_t v = MM[idx];
    if (v == 0xFFFFFFFFu) A.abort = 2;
    return v;
}
static void at_wr(uint32_t idx, uint32_t val)
{
    int ok = 0, f = 0; uint32_t rb;
    if (at_dead) { A.abort = 20; return; }
    for (unsigned i = 0; i < sizeof at_allow / 2; i++) if (at_allow[i] == idx) ok = 1;
    if (idx == 0x0161u && val == 0x00000101u) ok = 1;      /* SYMCLKB_CLOCK_ENABLE: VBIOS 자체 값(0x101)만 */
    if (!ok) { at_ndeny++; return; }
    for (int i = 0; i < nav; i++) if (av[i].idx == idx) f = 1;
    if (!f) { if (nav >= 128) { A.abort = 21; return; } av[nav].idx = (uint16_t)idx; av[nav].orig = MM[idx]; nav++; }
    MM[idx] = val;
    rb = MM[idx];
    at_nwr++;
    { char b[64]; int n = put(b, "amd W "); hx(b + n, idx, 4); n += 4; b[n++] = '='; hx(b + n, val, 8); n += 8; b[n++] = '>'; hx(b + n, rb, 8); n += 8; b[n] = 0; hadd(b); }
    if (rb == 0xFFFFFFFFu || !edp_alive()) { at_dead = 1; A.abort = 20; hadd("amd F detected - stop, no restore"); }
}

static const uint32_t at_mask[8] = { 0xFFFFFFFFu, 0xFFFFu, 0xFFFF00u, 0xFFFF0000u, 0xFFu, 0xFF00u, 0xFF0000u, 0xFF000000u };
static const int at_shift[8] = { 0, 0, 8, 16, 0, 8, 16, 24 };
static const int at_d2s[8][4] = { {0,0,0,0}, {1,2,3,0}, {1,2,3,0}, {1,2,3,0}, {4,5,6,7}, {4,5,6,7}, {4,5,6,7}, {4,5,6,7} };
static const int at_defdst[8] = { 0, 0, 1, 2, 0, 1, 2, 3 };

typedef struct { uint32_t *ps, *ws; int ps_size, ws_size, ps_shift; uint16_t start; int abort; } aex_t;
static uint32_t wsbuf[AT_MAXDEPTH + 1][256];

static uint32_t at_iio(int base, uint32_t index, uint32_t data)
{
    uint32_t temp = 0xCDCDCDCDu;
    while ((uint32_t)base + 3 < av_size && !A.abort)
        switch (a8(base)) {
        case 0: base++; break;
        case 2: temp = at_rd(a16(base + 1)); base += 3; break;
        case 3: at_wr(a16(base + 1), temp); base += 3; break;
        case 4: temp &= ~((0xFFFFFFFFu >> (32 - a8(base + 1))) << a8(base + 2)); base += 3; break;
        case 5: temp |= (0xFFFFFFFFu >> (32 - a8(base + 1))) << a8(base + 2); base += 3; break;
        case 6: temp &= ~((0xFFFFFFFFu >> (32 - a8(base + 1))) << a8(base + 3));
                temp |= ((index >> a8(base + 2)) & (0xFFFFFFFFu >> (32 - a8(base + 1)))) << a8(base + 3); base += 4; break;
        case 8: temp &= ~((0xFFFFFFFFu >> (32 - a8(base + 1))) << a8(base + 3));
                temp |= ((data >> a8(base + 2)) & (0xFFFFFFFFu >> (32 - a8(base + 1)))) << a8(base + 3); base += 4; break;
        case 7: temp &= ~((0xFFFFFFFFu >> (32 - a8(base + 1))) << a8(base + 3));
                temp |= (((uint32_t)A.io_attr >> a8(base + 2)) & (0xFFFFFFFFu >> (32 - a8(base + 1)))) << a8(base + 3); base += 4; break;
        case 9: return temp;
        default: A.abort = 3; return 0;
        }
    A.abort = 3;
    return 0;
}

static uint32_t at_src(aex_t *c, uint8_t attr, int *ptr, uint32_t *saved)
{
    uint32_t idx, val = 0xCDCDCDCDu, align = (attr >> 3) & 7, arg = attr & 7;
    switch (arg) {
    case AT_ARG_REG:
        idx = a16(*ptr); *ptr += 2; idx += A.reg_block;
        if (A.io_mode == AT_IO_MM) val = at_rd(idx);
        else if (A.io_mode & 0x80) { if (!A.iio[A.io_mode & 0x7F]) { A.abort = 4; return 0; } val = at_iio(A.iio[A.io_mode & 0x7F], idx, 0); }
        else { A.abort = 5; return 0; }
        break;
    case AT_ARG_PS:
        idx = a8(*ptr); (*ptr)++;
        if ((int)idx < c->ps_size) val = c->ps[idx]; else A.abort = 6;
        break;
    case AT_ARG_WS:
        idx = a8(*ptr); (*ptr)++;
        switch (idx) {
        case 0x40: val = A.divmul[0]; break;
        case 0x41: val = A.divmul[1]; break;
        case 0x42: val = A.data_block; break;
        case 0x43: val = A.shift; break;
        case 0x44: val = 1u << A.shift; break;
        case 0x45: val = ~(1u << A.shift); break;
        case 0x46: val = A.fb_base; break;
        case 0x47: val = A.io_attr; break;
        case 0x48: val = A.reg_block; break;
        default: if ((int)idx < c->ws_size) val = c->ws[idx]; else A.abort = 7;
        }
        break;
    case AT_ARG_ID:
        idx = a16(*ptr); *ptr += 2;
        val = a32(idx + A.data_block);
        break;
    case AT_ARG_FB:
        idx = a8(*ptr); (*ptr)++;
        if (A.fb_base / 4 + idx < 64) val = A.scratch[A.fb_base / 4 + idx]; else val = 0;
        break;
    case AT_ARG_IMM:
        switch (align) {
        case 0: val = a32(*ptr); *ptr += 4; break;
        case 1: case 2: case 3: val = a16(*ptr); *ptr += 2; break;
        default: val = a8(*ptr); (*ptr)++; break;
        }
        return val;
    case AT_ARG_PLL: case AT_ARG_MC:
        (*ptr)++; A.abort = 8;  return 0;
    }
    if (saved) *saved = val;
    val &= at_mask[align];
    val >>= at_shift[align];
    return val;
}
static void at_skip(int *ptr, uint8_t attr)
{
    uint32_t align = (attr >> 3) & 7, arg = attr & 7;
    switch (arg) {
    case AT_ARG_REG: case AT_ARG_ID: *ptr += 2; break;
    case AT_ARG_PLL: case AT_ARG_MC: case AT_ARG_PS: case AT_ARG_WS: case AT_ARG_FB: (*ptr)++; break;
    case AT_ARG_IMM: *ptr += align == 0 ? 4 : align <= 3 ? 2 : 1; break;
    }
}
static uint32_t at_src_direct(int align, int *ptr)
{
    uint32_t v;
    if (align == 0) { v = a32(*ptr); *ptr += 4; }
    else if (align <= 3) { v = a16(*ptr); *ptr += 2; }
    else { v = a8(*ptr); (*ptr)++; }
    return v;
}
static uint32_t at_dst(aex_t *c, int arg, uint8_t attr, int *ptr, uint32_t *saved)
{ return at_src(c, (uint8_t)(arg | (at_d2s[(attr >> 3) & 7][(attr >> 6) & 3] << 3)), ptr, saved); }
static void at_skip_dst(int *ptr, int arg, uint8_t attr)
{ at_skip(ptr, (uint8_t)(arg | (at_d2s[(attr >> 3) & 7][(attr >> 6) & 3] << 3))); }
static void at_put(aex_t *c, int arg, uint8_t attr, int *ptr, uint32_t val, uint32_t saved)
{
    uint32_t align = (uint32_t)at_d2s[(attr >> 3) & 7][(attr >> 6) & 3], idx;
    val <<= at_shift[align];
    val &= at_mask[align];
    saved &= ~at_mask[align];
    val |= saved;
    switch (arg) {
    case AT_ARG_REG:
        idx = a16(*ptr); *ptr += 2; idx += A.reg_block;
        if (A.io_mode == AT_IO_MM) { if (idx == 0) at_wr(idx, val << 2); else at_wr(idx, val); }
        else if (A.io_mode & 0x80) { if (!A.iio[A.io_mode & 0xFF]) { A.abort = 4; return; } at_iio(A.iio[A.io_mode & 0xFF], idx, val); }
        else A.abort = 5;
        break;
    case AT_ARG_PS:
        idx = a8(*ptr); (*ptr)++;
        if ((int)idx >= c->ps_size) { A.abort = 6; return; }
        c->ps[idx] = val;
        break;
    case AT_ARG_WS:
        idx = a8(*ptr); (*ptr)++;
        switch (idx) {
        case 0x40: A.divmul[0] = val; break;
        case 0x41: A.divmul[1] = val; break;
        case 0x42: A.data_block = (uint16_t)val; break;
        case 0x43: A.shift = (uint8_t)val; break;
        case 0x44: case 0x45: break;
        case 0x46: A.fb_base = val; break;
        case 0x47: A.io_attr = (uint16_t)val; break;
        case 0x48: A.reg_block = (uint16_t)val; break;
        default: if ((int)idx >= c->ws_size) { A.abort = 7; return; } c->ws[idx] = val;
        }
        break;
    case AT_ARG_FB:
        idx = a8(*ptr); (*ptr)++;
        if (A.fb_base / 4 + idx < 64) A.scratch[A.fb_base / 4 + idx] = val;
        break;
    case AT_ARG_PLL: case AT_ARG_MC:
        (*ptr)++; A.abort = 8;  break;
    }
}

static int at_exec(int index, uint32_t *params, int psz);

/* 연산 1개 실행. op>=1. 반환 1이면 종료(EOT) */
static int at_op(aex_t *c, int op, int *ptr)
{
    int arg = 0, kind = 0;       /* kind: 연산 그룹 */
    uint8_t attr; uint32_t dst, src, saved; int dptr;
    if (op <= 6) { kind = 1; arg = op - 1; }
    else if (op <= 12) { kind = 2; arg = op - 7; }
    else if (op <= 18) { kind = 3; arg = op - 13; }
    else if (op <= 24) { kind = 4; arg = op - 19; }
    else if (op <= 30) { kind = 5; arg = op - 25; }
    else if (op <= 36) { kind = 6; arg = op - 31; }
    else if (op <= 42) { kind = 7; arg = op - 37; }
    else if (op <= 48) { kind = 8; arg = op - 43; }
    else if (op <= 54) { kind = 9; arg = op - 49; }
    else if (op <= 57) { kind = 10; arg = op - 55; }
    else if (op == 58) kind = 11;
    else if (op == 59) kind = 12;
    else if (op <= 65) { kind = 13; arg = op - 60; }
    else if (op == 66) kind = 14;
    else if (op <= 73) { kind = 15; arg = op - 67; }
    else if (op <= 79) { kind = 16; arg = op - 74; }
    else if (op <= 81) { kind = 17; arg = op - 80; }
    else if (op == 82) kind = 18;
    else if (op == 83) kind = 19;
    else if (op <= 89) { kind = 20; arg = op - 84; }
    else if (op == 90) kind = 21;
    else if (op == 91) kind = 22;
    else if (op <= 97) { kind = 23; arg = op - 92; }
    else if (op == 98) kind = 24;
    else if (op == 99) kind = 25;
    else if (op == 100 || op == 101) kind = 26;
    else if (op == 102) kind = 27;
    else if (op <= 108) { kind = 28; arg = op - 103; }
    else if (op <= 114) { kind = 29; arg = op - 109; }
    else if (op <= 120) { kind = 30; arg = op - 115; }
    else if (op == 121) kind = 31;
    else if (op == 122) kind = 32;
    else if (op <= 124) { kind = 33; arg = op == 123 ? AT_ARG_PS : AT_ARG_WS; }
    else { kind = 34; arg = op == 125 ? AT_ARG_PS : AT_ARG_WS; }
    switch (kind) {
    case 1: /* move */
        attr = a8((*ptr)++); dptr = *ptr;
        if (((attr >> 3) & 7) != 0) at_dst(c, arg, attr, ptr, &saved); else { at_skip_dst(ptr, arg, attr); saved = 0xCDCDCDCDu; }
        src = at_src(c, attr, ptr, 0);
        at_put(c, arg, attr, &dptr, src, saved);
        break;
    case 2: case 3: case 8: case 9: case 28: { /* and, or, add, sub, xor */
        attr = a8((*ptr)++); dptr = *ptr;
        dst = at_dst(c, arg, attr, ptr, &saved);
        src = at_src(c, attr, ptr, 0);
        if (kind == 2) dst &= src; else if (kind == 3) dst |= src; else if (kind == 8) dst += src; else if (kind == 9) dst -= src; else dst ^= src;
        at_put(c, arg, attr, &dptr, dst, saved);
        break; }
    case 4: case 5: { /* shift_left/right (immediate byte) */
        uint8_t sh;
        attr = a8((*ptr)++); dptr = *ptr;
        attr &= 0x38; attr |= (uint8_t)(at_defdst[attr >> 3] << 6);
        dst = at_dst(c, arg, attr, ptr, &saved);
        sh = (uint8_t)at_src_direct(4, ptr);
        if (kind == 4) dst <<= sh; else dst >>= sh;
        at_put(c, arg, attr, &dptr, dst, saved);
        break; }
    case 29: case 30: { /* shl/shr (src operand) */
        uint32_t dalign;
        attr = a8((*ptr)++); dptr = *ptr;
        dalign = (uint32_t)at_d2s[(attr >> 3) & 7][(attr >> 6) & 3];
        at_dst(c, arg, attr, ptr, &saved);
        dst = saved;
        src = at_src(c, attr, ptr, 0) & 0xFF;
        if (kind == 29) dst <<= src; else dst >>= src;
        dst &= at_mask[dalign]; dst >>= at_shift[dalign];
        at_put(c, arg, attr, &dptr, dst, saved);
        break; }
    case 6: /* mul */
        attr = a8((*ptr)++);
        dst = at_dst(c, arg, attr, ptr, 0); src = at_src(c, attr, ptr, 0);
        A.divmul[0] = dst * src;
        break;
    case 7: /* div */
        attr = a8((*ptr)++);
        dst = at_dst(c, arg, attr, ptr, 0); src = at_src(c, attr, ptr, 0);
        if (src) { A.divmul[0] = dst / src; A.divmul[1] = dst % src; } else { A.divmul[0] = A.divmul[1] = 0; }
        break;
    case 33: { /* mul32 */
        uint64_t v;
        attr = a8((*ptr)++);
        dst = at_dst(c, arg, attr, ptr, 0); src = at_src(c, attr, ptr, 0);
        v = (uint64_t)dst * src; A.divmul[0] = (uint32_t)v; A.divmul[1] = (uint32_t)(v >> 32);
        break; }
    case 34: { /* div32 */
        uint64_t v;
        attr = a8((*ptr)++);
        dst = at_dst(c, arg, attr, ptr, 0); src = at_src(c, attr, ptr, 0);
        if (src) { v = dst | ((uint64_t)A.divmul[1] << 32); v /= src; A.divmul[0] = (uint32_t)v; A.divmul[1] = (uint32_t)(v >> 32); }
        else A.divmul[0] = A.divmul[1] = 0;
        break; }
    case 10: /* setport */
        if (arg == 0) { int port = a16(*ptr); A.io_mode = port ? (0x80 | port) : AT_IO_MM; *ptr += 2; }
        else if (arg == 1) { A.io_mode = AT_IO_PCI; (*ptr)++; }
        else { A.io_mode = AT_IO_SYSIO; (*ptr)++; }
        break;
    case 11: A.reg_block = a16(*ptr); *ptr += 2; break;                       /* setregblock */
    case 12: attr = a8((*ptr)++); A.fb_base = at_src(c, attr, ptr, 0); break; /* setfbbase */
    case 13: /* compare */
        attr = a8((*ptr)++);
        dst = at_dst(c, arg, attr, ptr, 0); src = at_src(c, attr, ptr, 0);
        A.cs_equal = dst == src; A.cs_above = dst > src;
        break;
    case 16: /* test */
        attr = a8((*ptr)++);
        dst = at_dst(c, arg, attr, ptr, 0); src = at_src(c, attr, ptr, 0);
        A.cs_equal = (dst & src) == 0;
        break;
    case 14: { /* switch */
        attr = a8((*ptr)++);
        src = at_src(c, attr, ptr, 0);
        while (a16(*ptr) != 0x5A5A && !A.abort) {
            if (a8(*ptr) == 0x63) {
                uint32_t val, target;
                (*ptr)++;
                val = at_src(c, (uint8_t)((attr & 0x38) | AT_ARG_IMM), ptr, 0);
                target = a16(*ptr);
                if (val == src) { *ptr = c->start + (int)target; return 0; }
                *ptr += 2;
            } else { A.abort = 9; return 0; }
        }
        *ptr += 2;
        break; }
    case 15: { /* jump */
        static const int jc[7] = { AC_ALWAYS, AC_EQUAL, AC_BELOW, AC_ABOVE, AC_BELOWEQ, AC_ABOVEEQ, AC_NOTEQ };
        int execute = 0, target = a16(*ptr);
        *ptr += 2;
        arg = jc[arg];
        switch (arg) {
        case AC_ABOVE: execute = A.cs_above; break;
        case AC_ABOVEEQ: execute = A.cs_above || A.cs_equal; break;
        case AC_ALWAYS: execute = 1; break;
        case AC_BELOW: execute = !(A.cs_above || A.cs_equal); break;
        case AC_BELOWEQ: execute = !A.cs_above; break;
        case AC_EQUAL: execute = A.cs_equal; break;
        case AC_NOTEQ: execute = !A.cs_equal; break;
        }
        if (execute) *ptr = c->start + target;
        break; }
    case 17: {                                                                  /* delay: 트레이스는 건너뜀, 실기는 TSC 대기 */
        uint32_t cnt = a8((*ptr)++);
        { uint64_t t1 = tsc() + (uint64_t)cnt * tpu * (arg == 0 ? 1000u : 1u); while (tsc() < t1) ; }
        break; }
    case 18: { /* calltable */
        int idx = a8((*ptr)++);
        if (a16(A.cmd_table + 4 + 2 * idx)) {
              
            if (at_exec(idx, c->ps + c->ps_shift, c->ps_size - c->ps_shift)) c->abort = 1;
        }
        break; }
    case 20: /* clear */
        attr = a8((*ptr)++); dptr = *ptr;
        attr &= 0x38; attr |= (uint8_t)(at_defdst[attr >> 3] << 6);
        at_dst(c, arg, attr, ptr, &saved);
        at_put(c, arg, attr, &dptr, 0, saved);
        break;
    case 23: { /* mask */
        uint32_t mask;
        attr = a8((*ptr)++); dptr = *ptr;
        dst = at_dst(c, arg, attr, ptr, &saved);
        mask = at_src_direct((attr >> 3) & 7, ptr);
        src = at_src(c, attr, ptr, 0);
        dst &= mask; dst |= src;
        at_put(c, arg, attr, &dptr, dst, saved);
        break; }
    case 24: case 31: (*ptr)++; break;                                          /* postcard, debug */
    case 27: { /* setdatablock */
        int idx = a8(*ptr); (*ptr)++;
        if (!idx) A.data_block = 0; else if (idx == 255) A.data_block = c->start; else A.data_block = a16(A.data_table + 4 + 2 * idx);
        break; }
    case 32: { uint16_t v = a16(*ptr); *ptr += v + 2; break; }                  /* processds */
    case 22: return 1;                                                          /* eot */
    default: break;                                                             /* nop/beep/repeat/savereg/restorereg */
    }
    return 0;
}

static int at_exec(int index, uint32_t *params, int psz)
{
    int base = a16(A.cmd_table + 4 + 2 * index), ws, ps, ptr, ret = 0;
    aex_t c;
    if (!base) return -1;
    if (A.depth >= AT_MAXDEPTH) { A.abort = 10; return -2; }
    ws = a8(base + 4); ps = a8(base + 5) & 0x7F; ptr = base + 6;
    c.ps_shift = ps / 4; c.start = (uint16_t)base; c.ps = params; c.ps_size = psz; c.abort = 0;
    c.ws = wsbuf[A.depth]; c.ws_size = ws;
    for (int i = 0; i < 256; i++) c.ws[i] = 0;
    A.depth++;
    for (;;) {
        int op = a8(ptr++);
        if (A.abort || c.abort) { ret = -3; break; }
        if (++A.nops > AT_MAXOPS || tsc() > A.t_end) { A.abort = 11; ret = -4; break; }
        if (op > 0 && op < AT_OP_CNT) { if (at_op(&c, op, &ptr)) break; } else break;
    }
    A.depth--;
    return ret;
}

/* 외부 진입: 테이블 index를 파라미터 ps[psn]로 트레이스 실행. 반환: 0 정상, 음수/양수 중단 사유 */
static int atom_run(int index, uint32_t *ps, int psn)
{
    A.data_block = 0; A.reg_block = 0; A.fb_base = 0; A.io_mode = AT_IO_MM; A.divmul[0] = A.divmul[1] = 0;
    A.cs_equal = A.cs_above = 0; A.shift = 0; A.depth = 0; A.abort = 0; A.nops = 0;
    A.t_end = tsc() + 3125000ull * tpu;             /* 약 3초 */
    int r = at_exec(index, ps, psn);
    return r ? r : (A.abort ? 100 + A.abort : 0);
}

/* VBIOS 검증 + 테이블 포인터 설정. 반환 0=OK, 그 외 사유 코드 */
static int atom_parse(uint32_t *rlen, uint32_t *rsum)
{
    uint32_t base, i, sum = 0, len;
    if (!av_bios || av_size < 0x100) return 1;
    if (a8(0) != 0x55 || a8(1) != 0xAA) return 2;
    len = (uint32_t)a8(2) * 512;
    if (len > av_size || len < 0x200) return 3;
    for (i = 0; i < len; i++) sum += av_bios[i];
    if (rlen) *rlen = len;
    if (rsum) *rsum = sum & 0xFF;
    if ((sum & 0xFF) != 0) return 4;
    base = a16(0x48);
    if (base < 0x40 || base + 0x30 > av_size) return 5;
    if (a8(base + 4) != 'A' || a8(base + 5) != 'T' || a8(base + 6) != 'O' || a8(base + 7) != 'M') return 6;
    A.cmd_table = a16(base + 0x1E);
    A.data_table = a16(base + 0x20);
    if (!A.cmd_table || !A.data_table || A.cmd_table + 8 > av_size || A.data_table + 8 > av_size) return 7;
    for (i = 0; i < 256; i++) A.iio[i] = 0;
    {   /* IIO 메서드 색인 (atom_index_iio) */
        static const int iol[10] = { 1, 2, 3, 3, 3, 3, 4, 4, 4, 3 };
        uint32_t b = a16(A.data_table + 0x32 + 4);
        A.abort = 0;
        while (b + 1 < av_size && a8(b) == 1) {
            uint8_t idx = a8(b + 1); uint32_t st = b + 2; b += 2;
            while (b < av_size && a8(b) != 9) { uint8_t o = a8(b); if (o >= 10) { b = av_size; break; } b += (uint32_t)iol[o]; }
            if (b >= av_size) break;
            A.iio[idx] = (uint16_t)st; b += 3;
        }
        A.abort = 0;
    }
    return 0;
}

/* ---- 켜기 ---- */
static int stage_ok(void) { if (bad || !edp_alive()) { bad = 1; hadd("amd ABORT bus/edp bad"); return 0; } return 1; }
static int hold_us(uint32_t us)        /* 아무것도 안 쓰고 기다리되 내장 패널 상태는 계속 확인 */
{
    uint64_t t = tsc(), e = (uint64_t)us * tpu;
    while (tsc() - t < e) { if (!edp_alive()) return 0; }
    return 1;
}
static void unlock_wait(void) { uint64_t t = tsc(); while ((RD(MM, 0x1c0b) & 0x100u) && tsc() - t < 1250000ull * tpu) ; }

static void s40(void)   /* OTG1 타이밍 1920x1080@60 (148.5MHz) + DTO */
{
    pw(0x1baa, 0x897u); pw(0x1bab, 0x00C00840u); pw(0x1bac, 0x002C0000u); pw(0x1bad, 1u);
    pw(0x1baf, 0x464u); pw(0x1bb6, 0x00290461u); pw(0x1bb7, 0x00050000u); pw(0x1bb8, 1u);
    pw(0x1bdb, 0x00010000u); pw(0x1c06, 1u); pw(0x8085, 148500000u); pw(0x8084, 0x10u);
}
static void s42(void) { pw(0x0e30, 0xF0000010u); pw(0x18e6, 3u); }
static void s43(void)   /* 파이프0(BIOS) 값을 파이프1 로 복사 (이중버퍼 잠금 안에서) */
{
    static const uint16_t h0[] = { 0x05ea, 0x05ee, 0x05f1, 0x05f3, 0x05f4, 0x0607, 0x060b, 0x060f, 0x0626, 0x062f, 0x0637, 0x0638, 0x064a, 0x064b, 0x064c, 0x061b, 0x061c, 0x0651, 0x0652 };
    pw(0x1c0b, 1u);
    for (unsigned i = 0; i < sizeof h0 / 2 && !bad; i++) pw((uint32_t)h0[i] + 0xDCu, RD(MM, h0[i]));
    pw(0x1c0b, 0u);
    unlock_wait();
}

static void atom_ps(uint32_t *ps) { for (int i = 0; i < 16; i++) ps[i] = 0; }
static void spc(void)   /* VBIOS SetPixelClock: 148.5MHz, UNIPHY 0x1E, HDMI */
{
    uint32_t ps[16]; uint8_t *p = (uint8_t *)ps; uint32_t pc = 1485000u;
    atom_ps(ps);
    p[0] = (uint8_t)pc; p[1] = (uint8_t)(pc >> 8); p[2] = (uint8_t)(pc >> 16); p[3] = (uint8_t)(pc >> 24);
    p[4] = 21; p[5] = 0x1E; p[6] = 3; p[7] = 0x00; p[8] = 1; p[9] = 0;
    int rr = atom_run(0x0C, ps, 16);
    { char b[64]; int n = put(b, "amd spc rc "); hx(b + n, (uint32_t)(rr & 0xFFFF), 4); n += 4; n += put(b + n, " abort "); hx(b + n, (uint32_t)A.abort, 2); n += 2; b[n] = 0; hadd(b); }
    if (rr) {   /* VBIOS 가 중간에 끊기면(PLL 포트 등) 끝부분(0101,0147,0144,0101)을 직접 실행 */
        A.abort = 0;
        at_wr(0x0101u, 0x00000000u); at_wr(0x0147u, 0x00000001u); at_wr(0x0144u, 0x00000000u); at_wr(0x0101u, 0x00000101u);
    }
    if (at_dead || A.abort == 20) { hadd("amd live F DETECTED - reboot, no restore"); bad = 1; }
}
static void tx(void)    /* OTG1 재기동 → VBIOS Transmitter INIT → Encoder STREAM_SETUP → Transmitter ENABLE → SYMCLKB */
{
    uint32_t ps[16]; uint8_t *p = (uint8_t *)ps; int rr;
    { uint32_t v = MM[0x5081];
      if (v != 0xFFFFFFFFu && (v & 1u)) { w5(0x5081u, v & ~1u); wait_us(20000); w5(0x5081u, v); wait_us(40000); } }
    atom_ps(ps);
    p[0] = 1; p[1] = 7; p[2] = 3; p[3] = 4; p[4] = 14850 & 0xFF; p[5] = 14850 >> 8; p[8] = 2; p[9] = 0x00; p[10] = 0x00;
    rr = atom_run(0x4C, ps, 16);
    if (!rr) wait_us(40000);
    if (!rr) {
        atom_ps(ps);
        p[0] = 1; p[1] = 0x0F; p[2] = 3; p[3] = 4; p[4] = 14850 & 0xFF; p[5] = 14850 >> 8;
        rr = atom_run(0x04, ps, 16);
    }
    if (!rr) {
        atom_ps(ps);
        p[0] = 1; p[1] = 1; p[2] = 3; p[3] = 4; p[4] = 14850 & 0xFF; p[5] = 14850 >> 8; p[8] = 2; p[9] = 0x00; p[10] = 0x00;
        rr = atom_run(0x4C, ps, 16);
    }
    { char b[64]; int n = put(b, "amd tx rc "); hx(b + n, (uint32_t)(rr & 0xFFFF), 4); n += 4; b[n] = 0; hadd(b); }
    A.abort = 0;
    at_wr(0x0161u, 0x00000101u);
    { char b[64]; int n = put(b, "amd atom wr "); n += put(b + n, ""); hx(b + n, at_nwr, 4); n += 4; n += put(b + n, " deny "); hx(b + n, at_ndeny, 4); n += 4; b[n] = 0; hadd(b); }
    if (at_dead || A.abort == 20) { hadd("amd live F DETECTED - reboot, no restore"); bad = 1; }
}

static void azw(uint32_t ix, uint32_t v) { MM[0x3846] = ix; MM[0x3847] = v; }
static void audio_gpu(void)   /* 소리 GPU쪽: AFMT 메모리 전원 → DCCG DTO → AZALIA → AFMT/ACR → 엔드포인트(스피커·ELD·이름) → 활성화 */
{
    if (MM[0x539E] != 0 && MM[0x539E] != 0xFFFFFFFFu) w5(0x539Eu, 0u);   /* DIO_MEM_PWR_CTRL: Linux dcn10 init 의 "power AFMT HDMI memory" (소리의 열쇠) */
    w5(0x0176u, 0x00001111u); w5(0x38CCu, 0x00000005u); w5(0x016Bu, 0x00000001u); w5(0x016Cu, 0x0003A980u); w5(0x016Du, 0x0016A8C8u); w5(0x016Eu, 0u); w5(0x016Fu, 0x00000001u);
    w5(0x3882u, 0x00000001u); w5(0x3883u, 0x00300018u); w5(0x3884u, 0u); w5(0x3885u, 0x00000001u);
    w5(0x3887u, 0x0000000Au); w5(0x3888u, 0x0000000Au); w5(0x3895u, 0x00080008u);
    w5(0x565Eu, 0x00000170u); w5(0x566Au, 0x00000801u);
    w5(0x5656u, 0x24414000u); w5(0x5657u, 0x00001000u); w5(0x5658u, 0x28488000u); w5(0x5659u, 0x00001880u); w5(0x565Au, 0x24414000u); w5(0x565Bu, 0x00001800u);
    w5(0x5660u, 0x00100000u); w5(0x5661u, 0x00200000u); w5(0x5667u, 0x00876543u);
    static const struct { uint8_t ix; uint32_t v; } wl[] = { {0x25,0x00010005u},{0x28,0x07070701u},{0x29,0x00500705u},{0x2E,0x00500700u},{0x36,0x00000001u},{0x3A,0x59C66D1Eu},{0x3B,0x0000000Du},{0x3C,0x5558859Eu},{0x3D,0x0D989449u},{0x3E,0x46204432u},{0x3F,0x4C204448u},{0x40,0x56542047u},{0x55,0x10000001u},{0x63,0x00000001u} };
    for (unsigned q = 0; q < sizeof wl / sizeof wl[0]; q++) azw(wl[q].ix, wl[q].v);
    /* Linux 쓰기 트레이스와 같은 순서·값 (0xFF = 38CC=5 자리) */
    static const struct { uint8_t ix; uint32_t v; } l46[] = { {0x54,0x00000001u},{0x25,0x00010B05u},{0x27,0},{0x28,0x07070701u},{0x29,0x00500705u},{0x2A,0},{0x2B,0},{0x2C,0},{0x2D,0},{0x2E,0x00500700u},{0x2F,0},{0x31,0},{0x32,0},{0x33,0},{0x35,0},{0xFF,0},{0x38,1},{0x37,0},{0x37,0},{0x3A,0x59C66D1Eu},{0x3B,0x0000000Du},{0x3C,0x5558859Eu},{0x3D,0x0D989449u},{0x3E,0x46204432u},{0x3F,0x4C204448u},{0x40,0x56542047u},{0x41,0},{0x42,0},{0x54,0x10} };
    for (unsigned q = 0; q < sizeof l46 / sizeof l46[0]; q++) { if (l46[q].ix == 0xFF) { w5(0x38CCu, 5); continue; } azw(l46[q].ix, l46[q].v); }
    wait_us(25000);
    azw(0x54, 0x80000001u); azw(0x54, 0x80000000u);   /* AZ 오디오 인에이블(bit31) */
}

static void hubp_copy(void)   /* HUBP1/DSCL1/CNVC1/MPCC1 ← 파이프0(BIOS) 값, 표면 주소 */
{
    static const uint16_t cp[][2] = {{0x3BC9,0x3AED},{0x3BCA,0x3AEE},{0x3BCB,0x3AEF},{0x3BCC,0x3AF0},{0x3BCD,0x3AF1},{0x3BCF,0x3AF3},{0x3BD0,0x3AF4},{0x3BE2,0x3B06},{0x3BE3,0x3B07},{0x3BE4,0x3B08},{0x3BE5,0x3B09},{0x3BE6,0x3B0A},{0x3BE8,0x3B0C},{0x3BE9,0x3B0D},{0x3BEB,0x3B0F},{0x3BEC,0x3B10},{0x3BED,0x3B11},{0x3BEE,0x3B12},{0x3BEF,0x3B13},{0x3BF1,0x3B15},{0x3BF4,0x3B18},{0x3BF5,0x3B19},{0x3BF6,0x3B1A},{0x3BF8,0x3B1C},{0x3BF9,0x3B1D},{0x3BFA,0x3B1E},{0x3BFB,0x3B1F},{0x3C01,0x3B25},{0x3C02,0x3B26},{0x3C03,0x3B27},{0x3C04,0x3B28},{0x432E,0x41C3},{0x432F,0x41C4},{0x42FB,0x4190}};
    for (unsigned q = 0; q < sizeof cp / 4; q++) { uint32_t v = MM[cp[q][1]]; if (v == 0xFFFFFFFFu) { bad = 1; break; } w5(cp[q][0], v); }
    w5(0x3B8Fu, 0x000F0112u);          /* DCHUBP_CNTL: VTG_SEL=1(OTG1) */
}
static void surf_copy(void)
{
    uint32_t lo = MM[0x3ACA], hi = MM[0x3ACB], sl = MM[0x3ACE], sh = MM[0x3ACF], sc0 = MM[0x3ADA];
    if (lo || hi) {
        w5(0x3BA7u, hi); w5(0x3BA6u, lo);
        w5(0x3BABu, sh ? sh : hi); w5(0x3BAAu, sl ? sl : lo);
        w5(0x3BB6u, sc0);
    }
}

/* 71 단계의 0.1초 칸마다 할 일 (v53 과 같은 시각·순서). 칸 번호 = 71 시작 후 경과 0.1초 */
static void tick_event(int sc)
{
    switch (sc) {
    case 1:  w5(0x566Fu, 0x10030200u); break;
    case 2:  w5(0x4D5Bu, 0x00608900u); w5(0x4D5Au, 0x01000000u); w5(0x4D5Cu, 0u); w5(0x4D5Du, 0u); w5(0x4D5Eu, 0u); w5(0x4D5Fu, 0x00000001u); w5(0x4D59u, 0x00000011u); w5(0x4F9Fu, 0x00000003u); w5(0x50C6u, 0x00000003u); break;
    case 3:  w5(0x5EDBu, 0x0C0C0C0Cu); w5(0x5EC9u, 0x001177F0u); break;
    case 4:  w5(0x5EE8u, 0x1E14403Au); w5(0x5EE9u, 0x3E00403Au); break;
    case 5:  w5(0x4DA6u, 0x00000003u); w5(0x4D63u, 0u); w5(0x4D70u, 0x07800438u); w5(0x4D74u, 0u); w5(0x4D73u, 0x7D007D00u); w5(0x4D72u, 0x10001000u); w5(0x4D71u, 0x7D007D00u); w5(0x4D6Eu, 0x00000041u); break;
    case 6:  w5(0x4F9Bu, 0x0000F100u); w5(0x4F9Cu, 0u); w5(0x39E9u, 0x84610000u); break;
    case 7:  w5(0x506Du, 0x00000000u); break;
    case 8:  w5(0x5648u, 0x00001000u); w5(0x564Cu, 0x000D0282u); w5(0x564Du, 0x80885EFEu); w5(0x564Fu, 0x00000466u); w5(0x5650u, 0x00000899u); w5(0x564Eu, 0u); w5(0x5651u, 0u); w5(0x5652u, 0u); w5(0x5653u, 0u); w5(0x5654u, 0u); break;
    case 9:  w5(0x5635u, 0x31u); w5(0x5636u, 0x10u); w5(0x5637u, 0x200u); w5(0x5638u, 0x3u); w5(0x5634u, 0x00011000u); break;
    case 10: w5(0x5631u, 0x00010019u); break;
    case 11: w5(0x5628u, 0x01000111u); break;
    case 12: w5(0x5070u, 0x464u); w5(0x5071u, 0x464u); w5(0x5073u, 0x00020003u); break;
    case 13: w5(0x0131u, 0x0E11003Cu); w5(0x56A6u, 0x00000101u); break;
    case 14: hubp_copy();
             w5(0x50C7u, MM[0x50C7 - 0x80]); w5(0x50C8u, MM[0x50C8 - 0x80]); w5(0x50C9u, MM[0x50C9 - 0x80]); break;   /* OTG1 VSTARTUP/VUPDATE/VREADY = OTG0 값 (화면이 나오는 열쇠) */
    case 15: w5(0x563Cu, 0x00000B00u); w5(0x5655u, 0x00000002u); w5(0x566Au, 0x04000801u); w5(0x562Cu, 0x00000060u); surf_copy(); break;
    case 16: w5(0x4D6Eu, 0u); break;                                   /* OPP 패턴 생성기 끄기 (안 끄면 자주색) */
    case 18: audio_gpu(); break;
    default: break;
    }
}

/* 4.0.116: 대기 단축. v53 은 44·70 단계 뒤 6.25초씩, 71 단계 6초 유지(마지막 칸은 1.8초): 중간에 하는 일이 없는 관찰용 대기였다.
 * 화면이 안 나오면 USB 루트에 AMDSLOW.OK 를 만들면 v53 과 같은 대기로 돌아간다 (amddisp_slow). */
static uint32_t hold_plain_us = 500000u, hold_71_us = 2500000u;
#define TICK_US       100000u
void amddisp_slow(int on) { hold_plain_us = on ? 6250000u : 500000u; hold_71_us = on ? 6000000u : 2500000u; }

int amddisp_on(volatile uint32_t *mm)
{
    uint32_t len = 0, sum = 0;
    MM = mm; nhist = 0; nav = 0; nsv = 0; bad = 0; at_dead = 0; at_nwr = at_ndeny = 0;
    pf0 = RD(mm, 0x2068); pb0 = RD(mm, 0x20af); pa0 = RD(mm, 0x060a);
    if (!edp_alive()) return 1;                         /* 내장 패널이 안 켜진 상태 */
    if (atom_parse(&len, &sum)) return 2;               /* VBIOS 이상 */
    if (!stage_ok()) return 3;
    s40(); if (bad) return 3;
    if (!stage_ok()) return 3; s42(); if (bad) return 3;
    if (!stage_ok()) return 3; s43(); if (bad) return 3;
    if (!stage_ok()) return 3; pw(0x1bdc, 1u); if (bad || !hold_us(hold_plain_us)) { bad = 1; return 3; }
    if (!stage_ok()) return 3; pw(0x2168, 1u); spc(); if (bad) return 3;
    if (!hold_us(hold_plain_us)) { bad = 1; return 3; }
    if (!stage_ok()) return 3;
    tx(); if (bad) return 3;
    {   /* 71: 테스트 패턴(흰색) 잠깐 + 0.1초 칸 스케줄 */
        uint64_t t = tsc(), e = (uint64_t)hold_71_us * tpu, tk = (uint64_t)TICK_US * tpu; int sec0 = -1;
        pw(0x1c0b, 1u); pw(0x216c, 0x03FF0061u); pw(0x1c0b, 0u); unlock_wait();
        while (tsc() - t < e && !bad) {
            if (!edp_alive()) { bad = 1; break; }
            int scn = (int)((tsc() - t) / tk);
            while (sec0 < scn) tick_event(++sec0);
        }
    }
    if (bad) return 3;
    hadd("amd on done");
    return 0;
}

void amddisp_off(volatile uint32_t *mm)
{
    MM = mm;
    if (bad || at_dead) { hadd("amd off: SKIP bus bad - reboot"); return; }
    for (int i = nav - 1; i >= 0; i--) mm[av[i].idx] = av[i].orig;
    nav = 0;
    if (!edp_alive()) { hadd("amd off: edp bad"); return; }
    for (int i = nsv - 1; i >= 0; i--) { uint32_t idx = sv[i].idx; WR(mm, idx, sv[i].orig); }
    nsv = 0;
    hadd("amd off done");
}

/* ---- 내장 패널 끄기/켜기 (백라이트 PWM 듀티 0): dce_panel_cntl.c 의 dce_driver_set_backlight 와 같은 순서.
 * BL_PWM_CNTL=0x2888, BL_PWM_CNTL2=0x2889, BL_PWM_PERIOD_CNTL=0x288a, BL_PWM_GRP1_REG_LOCK=0x288b, LVTMA_PWRSEQ_CNTL=0x2883 (모두 BASE_IDX 2) ---- */
#define BL_CNTL   0x2888u
#define BL_LOCK   0x288Bu
static uint32_t bl_orig; static int bl_is_off;
static void bl_log(const char *tag)
{
    char b[64]; int n = put(b, "amd bl "); n += put(b + n, tag); n += put(b + n, " CNTL "); hx(b + n, RD(MM, BL_CNTL), 8); n += 8;
    n += put(b + n, " PER "); hx(b + n, RD(MM, 0x288Au), 8); n += 8; b[n] = 0; hadd(b);
    n = put(b, "amd bl "); n += put(b + n, tag); n += put(b + n, " CNTL2 "); hx(b + n, RD(MM, 0x2889u), 8); n += 8; n += put(b + n, " SEQ "); hx(b + n, RD(MM, 0x2883u), 8); n += 8; b[n] = 0; hadd(b);
}
static void bl_set(uint32_t cntl)
{
    WR(MM, BL_LOCK, RD(MM, BL_LOCK) | 0x80000001u);          /* 그룹 잠금 */
    WR(MM, BL_CNTL, cntl);
    WR(MM, BL_LOCK, RD(MM, BL_LOCK) & ~1u);                  /* 해제 */
    { uint64_t t = tsc(), e = 10000ull * tpu; while ((RD(MM, BL_LOCK) & 0x100u) && tsc() - t < e) ; }
}
int amddisp_panel(volatile uint32_t *mm, int off)
{
    MM = mm;
    if (off) {
        uint32_t c = RD(mm, BL_CNTL);
        bl_log("before");
        if (bl_is_off) return 0;
        if (c == 0xFFFFFFFFu) return 2;
        if (!(c & 0x80000000u)) { hadd("amd bl: PWM not enabled - internal panel left on"); return 1; }
        if ((c & 0xFFFFu) == 0) { hadd("amd bl: already 0"); return 1; }
        bl_orig = c; bl_set(c & ~0xFFFFu); bl_is_off = 1;
        bl_log("after");
        return (RD(mm, BL_CNTL) & 0xFFFFu) == 0 ? 0 : 3;
    }
    if (!bl_is_off) return 0;
    bl_set(bl_orig); bl_is_off = 0;
    bl_log("restored");
    return 0;
}
