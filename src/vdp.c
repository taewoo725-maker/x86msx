/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * vdp.c - TMS9918A / V9938 (설계서 §6.4)
 *
 * 화면 모드 비트: M1 = R1.4, M2 = R1.3, M3 = R0.1, M4 = R0.2, M5 = R0.3
 *   TEXT1 = M1, TEXT2 = M1+M4, MC = M2, G1 = 0, G2 = M3, G3 = M4,
 *   G4 = M3+M4 (SCREEN 5), G5 = M5 (6), G6 = M3+M5 (7), G7 = M3+M4+M5 (8)
 * VRAM은 모든 모드에서 CPU가 보는 논리 주소 그대로 둔다 (G6/G7 인터리브는 흉내 내지 않음).
 * 명령 엔진: 결과는 바로 VRAM 에 쓰지만, "명령 시간 흉내"(속성 ▶ 비디오 VDP 명령 타이밍, 기본 켬)이면
 *   CE(실행 중) 비트가 실기만큼 오래 서 있다 (msx.c vdp_cmd_end). CPU 전송 명령 HMMC/LMMC/LMCM 은 바이트마다 진행.
 *
 * 버스 흐름: CPU OUT/IN → msx.c I/O 버스 → 98h vdp_write_data / vdp_read_data (VRAM, 주소 자동 증가)
 *                                         → 99h vdp_write_ctrl (2바이트 래치: 주소 설정 또는 레지스터 쓰기) / vdp_read_status
 *                                         → 9Ah 팔레트, 9Bh 간접 레지스터 (V9938 이상)
 * 화면: 줄 단위. msx.c 가 줄마다 vdp_line_start (줄 일치 FH) → CPU 한 줄 → vdp_line_end → vdp_render_line(그 줄의 레지스터로 그림).
 *   줄 중간에 바꾼 레지스터는 다음 줄부터 보인다 (사이클 단위 그리기는 아님).
 */
#include "vdp.h"
#include "vdpk.h"
#ifdef VDP_TRACE
#include <stdio.h>
#include <stdlib.h>
unsigned long long vdp_trace_frame;
int vdp_trace_line;
#endif
#include <string.h>

/* V9938 기본 팔레트 (RGB 각 3비트) */
static const uint8_t pal_9938[16][3] = {
    {0,0,0},{0,0,0},{1,6,1},{3,7,3},{1,1,7},{2,3,7},{5,1,1},{2,6,7},
    {7,1,1},{7,3,3},{6,6,1},{6,6,4},{1,4,1},{6,2,5},{5,5,5},{7,7,7},
};

/* 색 크기를 openMSX 기본 설정(감마 1.1)이 실제로 화면에 내는 값에 맞춤.
 * 3비트 DAC(V9938)는 255*(i/7)^(1/1.1), V9958(MSX2+)은 3비트를 5비트로 늘린 뒤 255*(i/31)^(1/1.1). 소수점 이하는 버림. */
static const uint8_t g7_9938[8] = { 0, 43, 81, 118, 153, 187, 221, 255 };
static const uint8_t g31_tab[32] = { 0, 11, 21, 30, 39, 48, 57, 65, 74, 82, 91, 99, 107, 115, 123, 131,
                                     139, 147, 155, 163, 171, 178, 186, 194, 202, 209, 217, 224, 232, 239, 247, 255 };
static uint8_t dac3(const VDP *v, int i) { i &= 7; return v->pal_58 ? g31_tab[(i << 2) | (i >> 1)] : g7_9938[i]; }

/* TMS9918A 팔레트 (openMSX 의 TMS9XXXA 아날로그 출력값 → YPbPr → RGB, 감마 1.1 적용 뒤 화면 값) — MSX1 기종 */
static const uint8_t pal_tms[16][3] = {
    {0,5,0},{0,5,0},{60,197,72},{115,217,124},{91,96,223},{130,130,238},{189,105,80},{98,228,235},
    {221,112,89},{254,146,124},{204,204,92},{221,216,134},{55,175,64},{185,112,181},{202,211,201},{249,255,248},
};

void vdp_default_palette(VDP *v)                    /* 4.0.34: MSX1(TMS9918A)은 칩 고유 색 */
{
    if (!v->v9938) { memcpy(v->palette, pal_tms, sizeof v->palette); return; }
    for (int i = 0; i < 16; i++)
        for (int c = 0; c < 3; c++)
            v->palette[i][c] = dac3(v, pal_9938[i][c]);
}

void vdp_g7_rgb(const VDP *v, uint8_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *g = dac3(v, (c >> 5) & 7);
    *r = dac3(v, (c >> 2) & 7);
    *b = dac3(v, (c & 3) == 3 ? 7 : (c & 3) * 2);       /* openMSX: 파랑 2비트 → 0,2,4,7 */
}

static void set_pal_entry(VDP *v, int i, int r, int g, int b)
{
    v->palette[i][0] = dac3(v, r);
    v->palette[i][1] = dac3(v, g);
    v->palette[i][2] = dac3(v, b);
    v->pal_dirty = 1;
    v->pal_changed = 1;
}

void vdp_init(VDP *v, int kind)
{
    memset(v, 0, sizeof *v);
    v->v9938 = kind >= 1;
    v->v9958 = kind == 2;
    v->pal_58 = v->v9958;
    v->pal_nsnap = 1;
    v->vram_mask = v->v9938 ? VDP_VRAM - 1 : 0x3FFF;
    vdp_default_palette(v);
}

static void vdp_check_il(VDP *v);
void vdp_reset(VDP *v)
{
    int k = v->v9938, k58 = v->v9958;
    uint32_t mask = v->vram_mask;
    memset(v->reg, 0, sizeof v->reg);
    memset(v->stat, 0, sizeof v->stat);
    v->vram_mask = mask;
    v->v9938 = k;
    v->v9958 = k58;
    v->pal_58 = k58;
    v->addr = 0;
    v->latch_full = v->pal_latch_full = 0;
    v->read_buf = 0;
    v->cmd = 0;
    v->job_active = 0;
    v->lines = 192;
    vdp_check_il(v);                                    /* 3.4.2: 끼워 넣기 순서였으면 되돌림 */
    if (k) {
        for (int i = 0; i < 16; i++) set_pal_entry(v, i, pal_9938[i][0], pal_9938[i][1], pal_9938[i][2]);
        v->stat[2] = 0x0C;                  /* 쓰이지 않는 비트 */
    } else vdp_default_palette(v);
    memset(v->fb, 0, sizeof v->fb);
    memset(v->line_direct, 0, sizeof v->line_direct);
}

/* ------------------------------------------------------------------ */
/* 모드                                                                 */
/* ------------------------------------------------------------------ */
enum { M_TEXT1, M_TEXT2, M_MC, M_G1, M_G2, M_G3, M_G4, M_G5, M_G6, M_G7, M_UNKNOWN };

/* VRAM 주소 (논리 배치 그대로. 실제 칩의 SCREEN 7·8 두 뱅크 교차 배치는 흉내 내지 않음 — 저장 상태 호환) */
static inline uint32_t vpa(const VDP *v, uint32_t a) { return a & v->vram_mask; }

static int mode(const VDP *v)
{
    int m1 = (v->reg[1] >> 4) & 1, m2 = (v->reg[1] >> 3) & 1;
    int m3 = (v->reg[0] >> 1) & 1, m4 = (v->reg[0] >> 2) & 1, m5 = (v->reg[0] >> 3) & 1;
    if (!v->v9938) { m4 = m5 = 0; }
    if (m1) return m4 ? M_TEXT2 : M_TEXT1;
    if (m2) return M_MC;
    int k = m3 | (m4 << 1) | (m5 << 2);
    switch (k) {
    case 0: return M_G1;
    case 1: return M_G2;
    case 2: return M_G3;
    case 3: return M_G4;
    case 4: return M_G5;
    case 5: return M_G6;
    case 7: return M_G7;
    default: return M_UNKNOWN;
    }
}

uint8_t vdp_backdrop(const VDP *v)
{
    if (v->v9938 && mode(v) == M_G5) return v->reg[7] & 0x03;
    return v->reg[7] & 0x0F;
}

int vdp_irq(const VDP *v)
{
    if ((v->stat[0] & 0x80) && (v->reg[1] & 0x20)) return 1;
    if (v->v9938 && (v->stat[1] & 0x01) && (v->reg[0] & 0x10)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 포트                                                                 */
/* ------------------------------------------------------------------ */
static void addr_inc(VDP *v)
{
    /* 2.16: V9938은 MSX2 화면 모드(M4·M5, 곧 T2·G3~G7)에서만 14비트 넘김이 R#14로 올라간다.
     * TMS 모드(T1·G1·G2·MC)에서는 아래 14비트만 돌고 R#14는 그대로 (openMSX VDP::executeCpuVramAccess와 같음).
     * 전에는 모든 모드에서 올려서 SCREEN 1에서 VRAM 128K를 시험하는 TESTRAM이 화면을 망가뜨렸음 */
    if (v->v9938 && !(v->reg[0] & 0x0C)) {
        v->addr = (v->addr & ~0x3FFFu) | ((v->addr + 1) & 0x3FFF);
        v->addr &= v->vram_mask;
        return;
    }
    v->addr = (v->addr + 1) & v->vram_mask;
    if (v->v9938) v->reg[14] = (uint8_t)((v->addr >> 14) & 7);
}

uint8_t vdp_read_data(VDP *v)
{
    uint8_t r = v->read_buf;
    v->read_buf = v->vram[vpa(v, v->addr)];
    addr_inc(v);
    v->latch_full = 0;
    return r;
}

static void cmd_lmcm_next(VDP *v);

uint8_t vdp_read_status(VDP *v)
{
    v->latch_full = 0;
    int s = v->v9938 ? (v->reg[15] & 0x0F) : 0;
    uint8_t r;
    switch (s) {
    case 0:
        r = v->stat[0];
        v->stat[0] &= 0x1F;                 /* F, 5S, C 해제 */
        return r;
    case 1:
        r = (uint8_t)((v->stat[1] & 0x01) | (v->v9958 ? 0x04 : 0x00));   /* FH, 칩 번호 (V9938 0, V9958 2) */
        v->stat[1] &= ~0x01;
        return r;
    case 2:
        r = (uint8_t)((v->stat[2] & ~0x20) | (v->hr ? 0x20 : 0) | 0x80);   /* TR 항상 1 */
        return r;
    case 7:
        r = v->stat[7];
        if (v->cmd == 0xA0) cmd_lmcm_next(v);
        return r;
    case 3: case 4: case 5: case 6: case 8: case 9:
        return v->stat[s];
    default:
        return 0xFF;
    }
}

void vdp_write_data(VDP *v, uint8_t val)
{
    v->vram[vpa(v, v->addr)] = val;
    v->read_buf = val;
    addr_inc(v);
    v->latch_full = 0;
}

static void cmd_start(VDP *v);
static void cmd_cpu_byte(VDP *v, uint8_t val);

/* 3.4.2 VID-06: G6·G7(SCREEN 7·8, YJK)은 VRAM을 두 뱅크에 끼워 넣는다: 논리 주소 L → 실제 (L >> 1) | ((L & 1) << 16).
 * 배열은 늘 지금 모드의 논리 주소로 두고, 끼워 넣는 모드와 아닌 모드 사이를 오갈 때 128 KB를 다시 배치한다 */
static void vdp_check_il(VDP *v)
{
    int m = mode(v), il = v->v9938 && v->vram_mask == VDP_VRAM - 1 && (m == M_G6 || m == M_G7);
    if (il == v->il) return;
    static uint8_t t[VDP_VRAM];
    memcpy(t, v->vram, VDP_VRAM);
    if (il) for (uint32_t l = 0; l < VDP_VRAM; l++) v->vram[l] = t[(l >> 1) | ((l & 1) << 16)];   /* 실제 → G7 논리 */
    else for (uint32_t l = 0; l < VDP_VRAM; l++) v->vram[(l >> 1) | ((l & 1) << 16)] = t[l];      /* G7 논리 → 실제 */
    v->il = (uint8_t)il;
}

/* 5.3.43 줄 중간 동기화 (5.3.45부터 항상 켬 - 옵션 없음).
 * 줄 중간에 모드·화면 주소·색 레지스터(R#0,2~8,12,13)를 처음 바꾼 가로 위치를 적어 두고, 그 줄은 "줄 시작 값"으로 한 번,
 * "줄 끝 값"으로 한 번 그려 왼쪽은 앞것, 오른쪽은 뒤것을 쓴다 (두 번째 변경 이후는 뒤것으로 통일). 팔레트는 아직 줄 단위. */
static int ml_pos = -1;
static uint8_t ml_reg[48];
static int ml_watch(int r) { return r == 0 || (r >= 2 && r <= 8) || r == 12 || r == 13; }

static void write_reg(VDP *v, int r, uint8_t val)
{
    if (!v->v9938) {
        static const uint8_t mask[8] = { 0x03, 0xFB, 0x0F, 0xFF, 0x07, 0x7F, 0x07, 0xFF };
        r &= 7;
        v->reg[r] = val & mask[r];
        return;
    }
    if (r >= 47) return;
#ifdef VDP_TRACE
    if (r < 28 || r == 46) { extern unsigned long long vdp_trace_frame; fprintf(stderr, "R%d=%02X f%llu l%d\n", r, val, vdp_trace_frame, vdp_trace_line); }
#endif
    if (r == 1 && ((val ^ v->reg[1]) & 0x40)) {     /* 4.0.29: 줄 중간에 표시를 켜고 끄면 그 위치부터 바뀜 (Aleste 2 상태 표시줄) */
        int x = (v->line_t * 6 - 270) / 4;
        v->de_pos = x < 0 ? 0 : x > 256 ? 256 : x;
        /* 5.3.46: R800 은 상태 확인 반복문 때문에 쓰기 위치가 줄마다 흔들려, 줄 끝을 넘겨 끄면 다음 줄이 통째로 비었다 (Aleste 2 Morente 상태 표시줄 아랫글자 깜박임).
           Z80 에서 쓰는 위치(다음 줄 x≈190)로 옮긴다. */
        if (v->late_defer && !(val & 0x40)) { if (x >= 256) v->pend_off = 1; else if (x < 190) v->de_pos = 190; }
    }
    if (ml_pos < 0 && ml_watch(r) && v->reg[r] != val) {
        int x = (v->line_t * 6 - 270) / 4;
        ml_pos = x < 0 ? 0 : x > 256 ? 256 : x;
    }
    v->reg[r] = val;
    if (r == 14) v->addr = (v->addr & 0x3FFF) | ((uint32_t)(val & 7) << 14);
    if (r == 16) v->pal_latch_full = 0;
    if (r == 44 && (v->cmd == 0xF0 || v->cmd == 0xB0)) cmd_cpu_byte(v, val);
    if (r == 46) cmd_start(v);
    if (r <= 1) vdp_check_il(v);                       /* 3.4.2 VID-06 */
}

void vdp_write_ctrl(VDP *v, uint8_t val)
{
    if (!v->latch_full) { v->latch = val; v->latch_full = 1; return; }
    v->latch_full = 0;
    if (val & 0x80) {
        write_reg(v, val & (v->v9938 ? 0x3F : 0x07), v->latch);
    } else {
        uint32_t hi = v->v9938 ? ((uint32_t)(v->reg[14] & 7) << 14) : 0;
        v->addr = (hi | ((uint32_t)(val & 0x3F) << 8) | v->latch) & v->vram_mask;
        if (!(val & 0x40)) {                /* 읽기 준비 */
            v->read_buf = v->vram[vpa(v, v->addr)];
            addr_inc(v);
        }
    }
}

void vdp_write_palette(VDP *v, uint8_t val)
{
    if (!v->v9938) return;
    if (!v->pal_latch_full) { v->pal_latch = val; v->pal_latch_full = 1; return; }
    v->pal_latch_full = 0;
    int i = v->reg[16] & 15;
    set_pal_entry(v, i, (v->pal_latch >> 4) & 7, val & 7, v->pal_latch & 7);
    v->reg[16] = (uint8_t)((i + 1) & 15);
}

void vdp_write_indirect(VDP *v, uint8_t val)
{
    if (!v->v9938) return;
    int r = v->reg[17] & 0x3F;
    if (r != 17) write_reg(v, r, val);
    if (!(v->reg[17] & 0x80)) v->reg[17] = (uint8_t)((v->reg[17] & 0xC0) | ((r + 1) & 0x3F));
}

/* ------------------------------------------------------------------ */
/* 프레임·줄 시간                                                       */
/* ------------------------------------------------------------------ */
void vdp_frame_start(VDP *v)
{
    v->stat[2] &= ~0x40;                    /* VR 해제 */
    v->lines = (v->v9938 && (v->reg[9] & 0x80)) ? 212 : 192;
    v->frame++;
}

void vdp_frame_end(VDP *v)
{
    v->stat[0] |= 0x80;
    v->stat[2] |= 0x40;                     /* VR */
}

void vdp_line_start(VDP *v, int line)
{
#ifdef VDP_TRACE
    vdp_trace_line = line;
#endif
    ml_pos = -1; memcpy(ml_reg, v->reg, sizeof ml_reg);
    (void)line; v->de_pos = -1; v->de_start = (uint8_t)((v->reg[1] >> 6) & 1);
    if (v->pend_off) { v->pend_off = 0; if (!(v->reg[1] & 0x40)) { v->de_start = 1; v->de_pos = 190; } }
    if (!v->v9938) return;
    /* 가로 스크롤은 줄이 시작할 때 값으로 (프로그램은 앞 줄의 수평 귀선(HR) 동안 다음 줄 값을 쓴다) */
    v->hs25 = v->reg[25]; v->hs26 = v->reg[26]; v->hs27 = v->reg[27];
#ifdef VDP_TRACE
    { extern unsigned long long vdp_trace_frame; if (vdp_trace_frame == (unsigned long long)atoi(getenv("LF") ? getenv("LF") : "30") && line >= 28 && line < 70) fprintf(stderr, "L%d %02X %02X\n", line, v->hs26, v->hs27); }
#endif
    /* FH(줄 인터럽트)는 IE1(R#0 비트 4)이 켜져 있을 때만 켜진다 (openMSX와 같음). 0~255 모든 줄 (테두리·귀선 포함).
     * 2.7까지는 IE1과 상관없이 켜서, FH를 직접 확인하는 게임(Zanac Ex)이 프레임마다 페이지를 바꿔 깜박였음 */
}

/* 4.0.29: FH(줄 인터럽트)는 그 줄을 다 그린 뒤 수평 귀선 시작에 켠다 (openMSX scheduleHScan: 일치 시각 = 줄 N 표시 영역 끝).
 * 전엔 줄 맨 앞에서 켜서 인터럽트가 한 줄 일찍 와, 프로그램이 줄 중간에 쓰는 R#1·R#23이 한 줄 일찍 적용됐다 (Aleste 2 상태 표시줄이 잘림) */
void vdp_line_end(VDP *v, int line)
{
    if (!v->v9938) return;
    if ((v->reg[0] & 0x10) && line == ((v->reg[19] - v->reg[23]) & 0xFF)) v->stat[1] |= 0x01;
}

/* ------------------------------------------------------------------ */
/* 그리기                                                               */
/* ------------------------------------------------------------------ */
#define VR(a) v->vram[vpa(v, (uint32_t)(a))]

static inline uint8_t col0b(const VDP *v, uint8_t bd, uint8_t c)   /* 줄 시작에 한 번 구한 테두리 색을 받음 (점마다 모드를 다시 판별하지 않음) */
{
    if (c) return c;
    if (v->v9938 && (v->reg[8] & 0x20)) return 0;
    return bd;
}

/* 스프라이트 (256폭 기준 좌표). line은 256폭 색 번호, direct면 G7 색 */
static void render_sprites(VDP *v, int y, uint8_t *line, int mode2, int g7)
{
    int big = (v->reg[1] & 0x02) != 0, mag = (v->reg[1] & 0x01) != 0;
    int size = (big ? 16 : 8) << mag;
    uint32_t sat, spg = (uint32_t)v->reg[6] << 11;
    if (!v->v9938) { sat = (uint32_t)(v->reg[5] & 0x7F) << 7; spg &= 0x3800; }
    else if (mode2) sat = ((uint32_t)(v->reg[11] & 3) << 15) | ((uint32_t)(v->reg[5] & 0xFC) << 7);
    else sat = ((uint32_t)(v->reg[11] & 3) << 15) | ((uint32_t)v->reg[5] << 7);
    if (v->v9938 && (v->reg[8] & 0x02)) return;     /* SPD: 스프라이트 끔 */
    uint8_t hide_line[512];
    if (v->spr_hide) line = hide_line;              /* 3.2.0: 설정으로 숨김. 5.2.73: 그리기만 버리고 충돌·5번째 스프라이트 상태는 실제대로 (GT 부팅 로고가 충돌 비트를 기다림) */
    uint32_t sct = sat - 512;
    int max_per_line = mode2 ? 8 : 4, end_y = mode2 ? 216 : 208;
    int sy_line = v->v9938 ? (y + v->reg[23]) & 0xFF : y;   /* 4.0.29: V9938 스프라이트는 R#23 세로 스크롤을 따라감 (openMSX SpriteChecker와 같음). 4.0에서 잘못 뺐다가 되돌림 — MSX2 세로 스크롤 게임에서 스프라이트가 어긋남 */
    int list[32], n = 0, over = 0;
    for (int i = 0; i < 32; i++) {
        int sy = VR(sat + i * 4);
        if (sy == end_y) break;
        int row = (sy_line - (sy + 1)) & 0xFF;
        if (row >= size) continue;
        if (n >= max_per_line && !over) {
            if (!(v->stat[0] & 0x40)) v->stat[0] = (uint8_t)((v->stat[0] & 0xE0) | 0x40 | i);
            over = 1;
            if (!v->spr_nolimit) break;             /* 3.2.0: 제한을 풀면 상태 플래그만 실제처럼 두고 계속 그림 */
        }
        list[n++] = i;
    }
    if (!over && !(v->stat[0] & 0x40)) v->stat[0] = (uint8_t)((v->stat[0] & 0xE0) | (n ? list[n - 1] : 0));
    if (!n) return;                                 /* 5.3.38: 이 줄에 스프라이트가 없으면 아래 배열 5개(약 1.6 KB)를 비우고 도는 일을 통째로 건너뜀 (그림·충돌·상태는 위에서 이미 끝남) */
    uint8_t occupied[256 + 64];
    memset(occupied, 0, sizeof occupied);
    static const uint8_t g7spr[16] = { 0x00, 0x02, 0x10, 0x12, 0x80, 0x82, 0x90, 0x92, 0x49, 0x0B, 0x59, 0x4B, 0xD9, 0xCB, 0xDB, 0xFF };
    /* 4.0: 앞 번호(우선순위 높은 것)부터 그림. CC(비트 6) 스프라이트는 바로 앞 번호 스프라이트와 겹친 점에서 색을 OR한다 (MSX2 스프라이트 색 합치기) */
    uint8_t acc[256 + 64], shown[256 + 64], chainvis[256 + 64];
    int8_t stamp[256 + 64];
    memset(shown, 0, sizeof shown); memset(chainvis, 0, sizeof chainvis); memset(acc, 0, sizeof acc); memset(stamp, -5, sizeof stamp);
    int tp = v->v9938 && (v->reg[8] & 0x20);
    for (int k = 0; k < n; k++) {
        int i = list[k];
        int sy = VR(sat + i * 4), x = VR(sat + i * 4 + 1), pat = VR(sat + i * 4 + 2);
        int row = ((sy_line - (sy + 1)) & 0xFF) >> mag;
        uint8_t attr = mode2 ? VR(sct + i * 16 + row) : VR(sat + i * 4 + 3);
        int cc = mode2 && (attr & 0x40), ic = mode2 && (attr & 0x20);
        uint8_t col = attr & 0x0F;
        if (attr & 0x80) x -= 32;
        if (big) pat &= 0xFC;
        uint32_t pa = spg + (uint32_t)pat * 8 + (uint32_t)row;
        uint16_t bits = (uint16_t)(VR(pa) << 8);
        if (big) bits |= VR(pa + 16);
        for (int px = 0; px < ((big ? 16 : 8) << mag); px++) {
            if (!(bits & (0x8000 >> (px >> mag)))) continue;
            int sx = x + px;
            if (sx < 0 || sx > 255) continue;
            if (!cc) {
                if (occupied[sx] && !ic) v->stat[0] |= 0x20;    /* 충돌 */
                occupied[sx] = 1;
                acc[sx] = col;
                chainvis[sx] = !shown[sx];
                if (chainvis[sx] && (col || tp)) { shown[sx] = 1; line[sx] = g7 ? g7spr[col] : col; }
                stamp[sx] = (int8_t)k;
            } else {
                if (occupied[sx]) {                             /* 앞 번호 스프라이트(CC 아닌 것부터 이어진 것)와 겹침: 색 OR */
                    acc[sx] |= col;
                    if (chainvis[sx] && (acc[sx] || tp)) { shown[sx] = 1; line[sx] = g7 ? g7spr[acc[sx]] : acc[sx]; }
                } else if (!shown[sx] && !occupied[sx] && (col || tp)) {   /* 합칠 앞 스프라이트가 없으면 제 색으로 */
                    shown[sx] = 1; chainvis[sx] = 1; acc[sx] = col; line[sx] = g7 ? g7spr[col] : col;
                }
                stamp[sx] = (int8_t)k;
            }
        }
    }
}

static void put256(uint8_t *o, const uint8_t *l256) { vdpk->put256(o, l256); }   /* 7단계: C판·어셈블러판 함수표 (src/vdpk.h) */

/* 점 번호 → 색 번호 표 (col0b 를 16개 한 번에). 4비트 모드 펼치기 커널이 씀 */
static void colmap16(const VDP *v, uint8_t bd, uint8_t *m)
{
    for (int i = 0; i < 16; i++) m[i] = (uint8_t)i;
    m[0] = col0b(v, bd, 0);
}

/* V9958 가로 스크롤 (R#26 8점 단위, R#27 1점 단위): 배경 줄을 돌리고, MSK면 왼쪽 8점을 테두리로 */
static void hscroll(const VDP *v, uint8_t *line, int w, uint8_t border)
{
    if (!v->v9958) return;
    int sc = ((v->hs26 & 0x3F) * 8 - (v->hs27 & 7)) & 255;
    if (w == 512) sc *= 2;
    if (sc) {
        uint8_t t[512];
        for (int x = 0; x < w; x++) t[x] = line[(x + sc) % w];
        memcpy(line, t, (size_t)w);
    }
    if (v->hs25 & 0x02) memset(line, border, (size_t)(w == 512 ? 16 : 8));
}

/* SP2 (R#25 비트 0, 2.8.1): 가로 스크롤이 두 페이지(짝수 페이지 왼쪽, 홀수 페이지 오른쪽) 폭으로.
 * line = R#2가 가리키는 페이지의 줄, other = 다른 페이지(페이지 비트를 뒤집은 것)의 같은 줄.
 * MSX2+·turbo R 로고가 양옆에서 미끄러져 들어오는 연출이 이것을 쓴다 */
/* 실기 확인(MSX.org 'V9958 two page horizontal scrolling'): SP2가 켜져 있어도 R#2의 A15(비트 5)가 0이면 옆 페이지는 안 보이고
 * 그 페이지 안에서만 돈다. A15=1일 때만 (A15를 뺀 페이지 | 쓴 페이지) 두 페이지로 이어진다. Tataku Mania 곡 고르기 화면(R#2=1F, SETSCROLL 160..255)이
 * 옆 페이지의 옛 그림 찌꺼기로 깨져 보이던 원인 */
static int sp2_on(const VDP *v) { return v->v9958 && (v->hs25 & 0x01) && (v->reg[2] & 0x20); }
static void hscroll_sp2(const VDP *v, uint8_t *line, const uint8_t *other, int w, int line_is_odd, uint8_t border)
{
    uint8_t t[1024];
    const uint8_t *left = line_is_odd ? other : line, *right = line_is_odd ? line : other;
    memcpy(t, left, (size_t)w); memcpy(t + w, right, (size_t)w);
    int sc = ((v->hs26 & 0x3F) * 8 - (v->hs27 & 7)) & 511;
    if (w == 512) sc *= 2;
    for (int x = 0; x < w; x++) line[x] = t[(x + sc) % (2 * w)];
    if (v->hs25 & 0x02) memset(line, border, (size_t)(w == 512 ? 16 : 8));
}

static void render_into(VDP *v, int y, uint8_t *out, uint8_t *dflag)
{
    uint8_t l[256];
    uint8_t bd = vdp_backdrop(v);
    int m = mode(v);
    *dflag = 0;
    if (!(v->reg[1] & 0x40) || y >= v->lines) {     /* 표시 꺼짐 또는 192줄 모드의 아래쪽 */
        if (m == M_G7 && v->v9938) { *dflag = 1; memset(out, v->reg[7], VDP_W); }
        else memset(out, bd, VDP_W);
        return;
    }
    int sy = v->v9938 ? (y + v->reg[23]) & 0xFF : y;   /* 세로 스크롤 */
    int row = sy >> 3, yy = sy & 7;
    uint32_t nt;

    switch (m) {
    case M_TEXT1: case M_TEXT2: {
        int w80 = m == M_TEXT2;
        uint8_t fg = col0b(v, bd, v->reg[7] >> 4), bg = col0b(v, bd, v->reg[7] & 0x0F);
        uint32_t pg = v->v9938 ? ((uint32_t)v->reg[4] << 11) & 0x1F800 : (uint32_t)(v->reg[4] & 7) << 11;
        nt = v->v9938 ? (w80 ? ((uint32_t)(v->reg[2] & 0xFC) << 10) : ((uint32_t)v->reg[2] << 10)) : (uint32_t)(v->reg[2] & 0x0F) << 10;
        int r = y >> 3;                             /* 글자 모드는 화면 줄 기준 */
        yy = y & 7;
        int rows = v->lines == 212 ? 26 : 24;
        if (r >= rows) { memset(out, bg, VDP_W); return; }
        if (!w80) {
            memset(l, bg, 256);
            for (int c = 0; c < 40; c++) {
                uint8_t p = VR(pg + VR(nt + r * 40 + c) * 8u + (uint32_t)yy);
                for (int b = 0; b < 6; b++) l[8 + c * 6 + b] = (p & (0x80 >> b)) ? fg : bg;
            }
            put256(out, l);
        } else {
            uint32_t ct = ((uint32_t)v->reg[10] << 14) | ((uint32_t)(v->reg[3] & 0xF8) << 6);
            uint8_t bfg = col0b(v, bd, v->reg[12] >> 4), bbg = col0b(v, bd, v->reg[12] & 0x0F);
            int on = v->reg[13] >> 4, off = v->reg[13] & 15, per = (on + off) * 10;
            int blink_on = per && ((v->frame % per) < on * 10);
            memset(out, bg, VDP_W);
            for (int c = 0; c < 80; c++) {
                uint8_t p = VR(pg + VR(nt + r * 80 + c) * 8u + (uint32_t)yy);
                int battr = (VR(ct + r * 10 + (c >> 3)) >> (7 - (c & 7))) & 1;
                uint8_t f = fg, b = bg;
                if (battr && blink_on) { f = bfg; b = bbg; }
                for (int k = 0; k < 6; k++) out[16 + c * 6 + k] = (p & (0x80 >> k)) ? f : b;
            }
        }
        return;                                     /* 글자 모드에는 스프라이트 없음 */
    }
    case M_MC: {
        nt = v->v9938 ? ((uint32_t)v->reg[2] << 10) & 0x1FC00 : (uint32_t)(v->reg[2] & 0x0F) << 10;
        uint32_t pg = v->v9938 ? ((uint32_t)v->reg[4] << 11) & 0x1F800 : (uint32_t)(v->reg[4] & 7) << 11;
        for (int c = 0; c < 32; c++) {
            uint8_t cb = VR(pg + VR(nt + row * 32 + c) * 8u + (uint32_t)(((row & 3) << 1) + ((sy >> 2) & 1)));
            memset(l + c * 8, col0b(v, bd, cb >> 4), 4);
            memset(l + c * 8 + 4, col0b(v, bd, cb & 15), 4);
        }
        hscroll(v, l, 256, bd);
        render_sprites(v, y, l, 0, 0);
        put256(out, l);
        return;
    }
    case M_G1: {
        nt = v->v9938 ? ((uint32_t)v->reg[2] << 10) & 0x1FC00 : (uint32_t)(v->reg[2] & 0x0F) << 10;
        uint32_t pg = v->v9938 ? ((uint32_t)v->reg[4] << 11) & 0x1F800 : (uint32_t)(v->reg[4] & 7) << 11;
        uint32_t ct = v->v9938 ? (((uint32_t)v->reg[10] << 14) | ((uint32_t)v->reg[3] << 6)) : (uint32_t)v->reg[3] << 6;
        uint8_t gp[32], gf[32], gb[32];
        for (int c = 0; c < 32; c++) {
            uint8_t name = VR(nt + row * 32 + c);
            uint8_t cb = VR(ct + (name >> 3));
            gp[c] = VR(pg + name * 8u + (uint32_t)yy);
            gf[c] = col0b(v, bd, cb >> 4); gb[c] = col0b(v, bd, cb & 15);
        }
        vdpk->glyph8(l, gp, gf, gb, 32);                /* 7단계: 칸 32개 × 8점 펼치기 (C판·어셈블러판) */
        hscroll(v, l, 256, bd);
        render_sprites(v, y, l, 0, 0);
        put256(out, l);
        return;
    }
    case M_G2: case M_G3: {
        nt = v->v9938 ? ((uint32_t)v->reg[2] << 10) & 0x1FC00 : (uint32_t)(v->reg[2] & 0x0F) << 10;
        uint32_t pg = v->v9938 ? ((uint32_t)(v->reg[4] & 0x3C) << 11) : (uint32_t)(v->reg[4] & 0x04) << 11;
        uint32_t ct = v->v9938 ? (((uint32_t)v->reg[10] << 14) | ((uint32_t)(v->reg[3] & 0x80) << 6)) : (uint32_t)(v->reg[3] & 0x80) << 6;
        uint32_t pmask = ((uint32_t)(v->reg[4] & 3) << 8) | 0xFF, cmask = ((uint32_t)(v->reg[3] & 0x7F) << 3) | 7;
        uint32_t third = (uint32_t)(sy >> 6) << 8;
        uint8_t gp[32], gf[32], gb[32];
        for (int c = 0; c < 32; c++) {
            uint32_t ch = VR(nt + row * 32 + c) + third;
            uint8_t cb = VR(ct + ((ch & cmask) << 3) + (uint32_t)yy);
            gp[c] = VR(pg + ((ch & pmask) << 3) + (uint32_t)yy);
            gf[c] = col0b(v, bd, cb >> 4); gb[c] = col0b(v, bd, cb & 15);
        }
        vdpk->glyph8(l, gp, gf, gb, 32);                /* 7단계 */
        hscroll(v, l, 256, bd);
        render_sprites(v, y, l, m == M_G3, 0);
        put256(out, l);
        return;
    }
    case M_G4: {                                    /* 256 x 212, 4비트 */
        uint32_t base = ((uint32_t)(v->reg[2] & 0x60) << 10) + (uint32_t)sy * 128;
        uint8_t cm[16]; colmap16(v, bd, cm);
        vdpk->nib16(l, &v->vram[vpa(v, base)], 128, cm);     /* 줄은 128 바이트 경계라 마스크 안에서 이어짐 */
        if (sp2_on(v)) {
            uint8_t o[256];
            for (int x = 0; x < 128; x++) { uint8_t b = VR((base ^ 0x8000) + x); o[2 * x] = col0b(v, bd, b >> 4); o[2 * x + 1] = col0b(v, bd, b & 15); }
            hscroll_sp2(v, l, o, 256, (base & 0x8000) != 0, bd);
        } else
        hscroll(v, l, 256, bd);
        render_sprites(v, y, l, 1, 0);
        put256(out, l);
        return;
    }
    case M_G5: {                                    /* 512 x 212, 2비트 */
        uint32_t base = ((uint32_t)(v->reg[2] & 0x60) << 10) + (uint32_t)sy * 128;
        uint8_t cm4[4] = { col0b(v, bd, 0), col0b(v, bd, 1), col0b(v, bd, 2), col0b(v, bd, 3) };
        vdpk->crumb4(out, &v->vram[vpa(v, base)], 128, cm4);
        if (sp2_on(v)) {
            uint8_t o[512];
            for (int x = 0; x < 128; x++) { uint8_t b = VR((base ^ 0x8000) + x); for (int k = 0; k < 4; k++) o[4 * x + k] = col0b(v, bd, (b >> (6 - 2 * k)) & 3); }
            hscroll_sp2(v, out, o, 512, (base & 0x8000) != 0, bd);
        } else
        hscroll(v, out, 512, bd);
        memset(l, 0xFF, 256);
        render_sprites(v, y, l, 1, 0);
        /* G5: 스프라이트 점 하나 = 화면 점 둘. 왼쪽은 색의 위 2비트, 오른쪽은 아래 2비트 (V9938 규칙).
         * 예: 색 5(0101b) → 1, 1. MSX2+·turbo R 로고의 가림막 스프라이트가 배경색으로 보이지 않게 됨 */
        for (int x = 0; x < 256; x++)
            if (l[x] != 0xFF) { out[2 * x] = col0b(v, bd, (l[x] >> 2) & 3); out[2 * x + 1] = col0b(v, bd, l[x] & 3); }
        return;
    }
    case M_G6: {                                    /* 512 x 212, 4비트 */
        uint32_t base = ((uint32_t)(v->reg[2] & 0x20) << 11) + (uint32_t)sy * 256;
        uint8_t cm[16]; colmap16(v, bd, cm);
        vdpk->nib16(out, &v->vram[vpa(v, base)], 256, cm);   /* 줄은 256 바이트 경계 */
        if (sp2_on(v)) {
            uint8_t o[512];
            for (int x = 0; x < 256; x++) { uint8_t b = VR((base ^ 0x10000) + x); o[2 * x] = col0b(v, bd, b >> 4); o[2 * x + 1] = col0b(v, bd, b & 15); }
            hscroll_sp2(v, out, o, 512, (base & 0x10000) != 0, bd);
        } else
        hscroll(v, out, 512, bd);
        memset(l, 0xFF, 256);
        render_sprites(v, y, l, 1, 0);
        for (int x = 0; x < 256; x++) if (l[x] != 0xFF) out[2 * x] = out[2 * x + 1] = l[x];
        return;
    }
    case M_G7: {                                    /* 256 x 212, 직접 색 */
        uint32_t base = ((uint32_t)(v->reg[2] & 0x20) << 11) + (uint32_t)sy * 256;
        int yjk = v->v9958 && (v->reg[25] & 0x08);
        *dflag = (uint8_t)(yjk ? ((v->reg[25] & 0x10) ? 3 : 2) : 1);
        for (int x = 0; x < 256; x++) l[x] = VR(base + x);
        if (sp2_on(v)) {
            uint8_t o[256];
            for (int x = 0; x < 256; x++) o[x] = VR((base ^ 0x10000) + x);
            hscroll_sp2(v, l, o, 256, (base & 0x10000) != 0, v->reg[7]);
        } else
        hscroll(v, l, 256, v->reg[7]);
        uint8_t spr[256];
        memset(spr, 0xFF, 256);
        render_sprites(v, y, spr, 1, 1);
        if (yjk) memcpy(v->spr_ov[y], spr, 256);       /* YJK: 원래 바이트는 그대로, 스프라이트는 따로 */
        else for (int x = 0; x < 256; x++) if (spr[x] != 0xFF) l[x] = spr[x];
        put256(out, l);
        return;
    }
    default:
        memset(out, bd, VDP_W);
        return;
    }
}

void vdp_render_line(VDP *v, int y)
{
    static int vdpk_seen;
    if (vdpk_seen != vdpk_asm_on + 1) { vdpk_seen = vdpk_asm_on + 1; vdpk_apply(); }   /* ini VdpAsm 이 바뀌면 표를 바꿈 */
    if (y >= VDP_H) return;
    if (y == 0) { v->pal_nsnap = 1; v->pal_cur = 0; memcpy(v->pal_snap[0], v->palette, sizeof v->pal_snap[0]); v->pal_changed = 0; }
    else if (v->pal_changed) {                            /* 4.0.30: 이 줄부터 새 팔레트 */
        if (v->pal_nsnap < VDP_PAL_SNAPS) v->pal_cur = v->pal_nsnap++; else v->pal_cur = v->pal_nsnap - 1;
        memcpy(v->pal_snap[v->pal_cur], v->palette, sizeof v->pal_snap[0]);
        v->pal_changed = 0;
    }
    v->line_pal[y] = (uint8_t)v->pal_cur;
    v->line_adj[y] = v->reg[18];
#ifdef VDP_TRACE
    { extern unsigned long long vdp_trace_frame; if (vdp_trace_frame == (unsigned long long)atoi(getenv("LF") ? getenv("LF") : "30") && y < 70) fprintf(stderr, "Y%d r0=%02X r1=%02X r2=%02X r23=%02X r18=%02X r25=%02X r8=%02X r9=%02X\n", y, v->reg[0], v->reg[1], v->reg[2], v->reg[23], v->reg[18], v->reg[25], v->reg[8], v->reg[9]); }
#endif
    v->line_bd[y] = vdp_backdrop(v);
    int m = mode(v);
    int eo = v->v9938 && (v->reg[9] & 0x0C) == 0x0C && m >= M_G4 && m <= M_G7;
    v->line_eo[y] = (uint8_t)eo;
    if (!eo) {
        int de_cur = (v->reg[1] >> 6) & 1;
        if (v->de_pos >= 0 && v->de_start != de_cur && m != M_G7) {     /* 줄 중간에 표시가 바뀜: 켜진 쪽만 그림 */
            uint8_t r1 = v->reg[1], sv[48];
            int old = !de_cur;                            /* 5.3.43: 줄 중간에 꺼지면 보이는 왼쪽은 줄 시작 때 레지스터(R#2·R#23 등)로 (Aleste 2 Morente 판 상태 표시줄) */
            if (old) { memcpy(sv, v->reg, sizeof sv); memcpy(v->reg, ml_reg, sizeof sv); }
            v->reg[1] = (uint8_t)(r1 | 0x40);
            render_into(v, y, v->fb[y], &v->line_direct[y]);
            if (old) memcpy(v->reg, sv, sizeof sv);
            v->reg[1] = r1;
            int px = v->de_pos * (VDP_W / 256);
            uint8_t bd = v->line_bd[y];
            if (de_cur) memset(v->fb[y], bd, (size_t)px);                    /* 꺼져 있다가 켜짐: 앞쪽은 테두리색 */
            else memset(v->fb[y] + px, bd, (size_t)(VDP_W - px));            /* 켜져 있다가 꺼짐: 뒤쪽 */
            return;
        }
        if (ml_pos > 0 && ml_pos < 256) {             /* 줄 중간에 레지스터가 바뀜: 왼쪽 앞것, 오른쪽 뒤것 */
            uint8_t cur[48], dnew;
            memcpy(cur, v->reg, sizeof cur);
            memcpy(v->reg, ml_reg, sizeof cur);
            uint8_t dold;
            static uint8_t tmp[VDP_W];
            render_into(v, y, tmp, &dold);                              /* 앞것 */
            memcpy(v->reg, cur, sizeof cur);
            render_into(v, y, v->fb[y], &dnew);                         /* 뒤것 */
            v->line_direct[y] = dnew;
            if (dold == dnew) {                                         /* 색 표현이 같을 때만 합침 */
                int px = ml_pos * (VDP_W / 256);
                memcpy(v->fb[y], tmp, (size_t)px);
            }
            return;
        }
        render_into(v, y, v->fb[y], &v->line_direct[y]); return;
    }
    /* 짝수 필드 = 짝수 페이지(R#2 페이지 비트 끔), 홀수 필드 = R#2가 가리키는 페이지 */
    uint8_t r2 = v->reg[2];
    v->reg[2] = (uint8_t)(r2 & ~0x20);
    render_into(v, y, v->fb[y], &v->line_direct[y]);
    v->reg[2] = (uint8_t)(r2 | 0x20);
    render_into(v, y, v->fb_odd[y], &v->line_direct_odd[y]);
    v->reg[2] = r2;
}

/* ------------------------------------------------------------------ */
/* 명령 엔진                                                            */
/* ------------------------------------------------------------------ */
/* 비트맵 모드별 점 크기: G4 4비트 256폭, G5 2비트 512폭, G6 4비트 512폭, G7 8비트 256폭.
 * 비트맵이 아니면 G7처럼 다룬다. */
static int cmd_mode(const VDP *v)
{
    int m = mode(v);
    return (m == M_G4 || m == M_G5 || m == M_G6) ? m : M_G7;
}

/* 4.0.4: 지금 화면 모드의 가로 점 수 (SCREEN 6·7 = 512, 그 밖 = 256) — 마우스 이동량을 가로·세로 같은 길이로 맞추는 데 씀 */
int vdp_hres(const VDP *v) { int m = mode(v); return (m == M_G5 || m == M_G6) ? 512 : 256; }

static uint32_t pix_addr(int m, int x, int y)
{
    switch (m) {
    case M_G4: return (uint32_t)(y & 1023) * 128 + (uint32_t)((x & 255) >> 1);
    case M_G5: return (uint32_t)(y & 1023) * 128 + (uint32_t)((x & 511) >> 2);
    case M_G6: return ((uint32_t)(y & 511) * 256 + (uint32_t)((x & 511) >> 1));
    default:   return ((uint32_t)(y & 511) * 256 + (uint32_t)(x & 255));
    }
}

static uint8_t pget(VDP *v, int m, int x, int y)
{
    uint8_t b = VR(pix_addr(m, x, y));
    switch (m) {
    case M_G4: case M_G6: return (x & 1) ? (b & 15) : (b >> 4);
    case M_G5: return (uint8_t)((b >> (6 - 2 * (x & 3))) & 3);
    default: return b;
    }
}

static void pset(VDP *v, int m, int x, int y, uint8_t c, int op)
{
    uint32_t a = vpa(v, pix_addr(m, x, y));
    uint8_t b = v->vram[a], mask, sh;
    switch (m) {
    case M_G4: case M_G6: sh = (x & 1) ? 0 : 4; mask = (uint8_t)(15 << sh); c &= 15; break;
    case M_G5: sh = (uint8_t)(6 - 2 * (x & 3)); mask = (uint8_t)(3 << sh); c &= 3; break;
    default: sh = 0; mask = 0xFF; break;
    }
    if ((op & 8) && c == 0) return;                 /* T 연산: 색 0은 건너뜀 (5.0.55: 마스크한 뒤 — openMSX) */
    if ((op & 7) >= 5) return;                      /* 정의 안 된 연산(5~7)은 아무것도 안 함 — openMSX DummyOp */
    uint8_t d = (uint8_t)((b & mask) >> sh), r;
    switch (op & 7) {
    case 0: r = c; break;                           /* IMP */
    case 1: r = c & d; break;                       /* AND */
    case 2: r = c | d; break;                       /* OR */
    case 3: r = c ^ d; break;                       /* EOR */
    case 4: r = (uint8_t)~c; break;                 /* NOT */
    default: r = c; break;
    }
    v->vram[a] = (uint8_t)((b & ~mask) | ((r << sh) & mask));
}

static int ppb(int m) { return m == M_G5 ? 4 : (m == M_G4 || m == M_G6) ? 2 : 1; }
static int width_of(int m) { return (m == M_G5 || m == M_G6) ? 512 : 256; }

#define R16(lo) ((int)(v->reg[lo] | (v->reg[(lo) + 1] << 8)))

#ifdef VDP_TRACE
static int cdump_on, cdump_n;
static void cmd_done(VDP *v)
{
    if (!cdump_on && v->reg[44] == 0xFF && v->reg[40] == 0x0E && (v->reg[46] & 0xF0) == 0xC0) cdump_on = 1;
    if (cdump_on && cdump_n < 40 && getenv("CDUMP")) {
        char nm[200]; snprintf(nm, sizeof nm, "%s/%03d.bin", getenv("CDUMP"), cdump_n);
        FILE *f = fopen(nm, "wb"); if (f) { fwrite(v->vram, 1, 65536, f); fclose(f); }
        fprintf(stderr, "CD %03d cmd=%02X\n", cdump_n, v->reg[46] & 0xF0);
        cdump_n++;
    }
    v->cmd = 0; v->stat[2] &= ~0x01;
}
#else
static void cmd_done(VDP *v) { v->cmd = 0; v->stat[2] &= ~0x01; }
#endif

/* CPU → VRAM 전송 (HMMC 0xF0, LMMC 0xB0): R#44에 쓸 때마다 한 점(바이트) */
static void cmd_cpu_byte(VDP *v, uint8_t val)
{
    int m = cmd_mode(v);
    if (v->cmd == 0xF0) {                           /* 바이트 단위 */
        uint32_t a = vpa(v, pix_addr(m, v->c_x, v->c_y));
        v->vram[a] = val;
        v->c_x += v->c_dx * ppb(m);
    } else {
        pset(v, m, v->c_x, v->c_y, val, v->reg[46] & 15);
        v->c_x += v->c_dx;
    }
    if (--v->c_left <= 0) {
        v->c_left = v->c_nx;
        v->c_x = v->c_x0;
        v->c_y += v->c_dy;
        if (--v->c_ny <= 0) cmd_done(v);
    }
}

/* VRAM → CPU (LMCM 0xA0): S#7을 읽을 때마다 다음 점 */
static void cmd_lmcm_next(VDP *v)
{
    int m = cmd_mode(v);
    if (--v->c_left <= 0) {
        v->c_left = v->c_nx;
        v->c_x = v->c_x0;
        v->c_y += v->c_dy;
        if (--v->c_ny <= 0) { cmd_done(v); return; }
    } else v->c_x += v->c_dx;
    v->stat[7] = pget(v, m, v->c_x, v->c_y);
}

#ifdef VDP_TRACE
#include <stdio.h>
#endif
/* openMSX VDPCmdEngine의 clipNX/clipNY와 같게: 가로는 화면 끝에서 그 줄을 마치고(되감지 않음), 바이트 명령은 NX를 바이트로 내림 */
static int clipn1_px(int dx, int nx, int W, int dix) { if (dx >= W) return 1; if (!nx) nx = W; return dix < 0 ? (nx < dx + 1 ? nx : dx + 1) : (nx < W - dx ? nx : W - dx); }
static int clipn2_px(int sx, int dx, int nx, int W, int dix)
{
    if (sx >= W || dx >= W) return 1;
    if (!nx) nx = W;
    int mn = sx < dx ? sx : dx, mx = sx > dx ? sx : dx;
    return dix < 0 ? (nx < mn + 1 ? nx : mn + 1) : (nx < W - mx ? nx : W - mx);
}
static int clipn1_byte(int dx, int nx, int W, int p, int dix)
{
    int B = W / p; dx /= p;
    if (B <= dx) return 1;
    nx /= p; if (!nx) nx = B;
    return dix < 0 ? (nx < dx + 1 ? nx : dx + 1) : (nx < B - dx ? nx : B - dx);
}
static int clipn2_byte(int sx, int dx, int nx, int W, int p, int dix)
{
    int B = W / p; sx /= p; dx /= p;
    if (B <= sx || B <= dx) return 1;
    nx /= p; if (!nx) nx = B;
    int mn = sx < dx ? sx : dx, mx = sx > dx ? sx : dx;
    return dix < 0 ? (nx < mn + 1 ? nx : mn + 1) : (nx < B - mx ? nx : B - mx);
}
static int clipny1(int dy, int ny, int diy) { return diy < 0 ? (ny < dy + 1 ? ny : dy + 1) : ny; }
static int clipny2(int sy, int dy, int ny, int diy) { int mn = sy < dy ? sy : dy; return diy < 0 ? (ny < mn + 1 ? ny : mn + 1) : ny; }

/* 줄 단위 명령 몸체. job_p = { m, W, op, clr, sx, sy, dx, dy, dix, diy, bn(바이트 폭/점 폭), p } */
static void job_row(VDP *v, int j)
{
    int *q = v->job_p, m = q[0], op = q[2], clr = q[3], sx = q[4], sy = q[5], dx = q[6], dy = q[7], dix = q[8], diy = q[9], bn = q[10], p = q[11];
    switch (v->job_kind) {
    case 0xC0: { int bx = (dx & 511) / p; for (int i = 0; i < bn; i++) v->vram[vpa(v, pix_addr(m, (bx + i * dix) * p, dy + j * diy))] = (uint8_t)clr; break; }
    case 0xD0:
        for (int i = 0; i < bn; i++) {
            uint8_t b = VR(pix_addr(m, sx + i * dix * p, sy + j * diy));
            v->vram[vpa(v, pix_addr(m, dx + i * dix * p, dy + j * diy))] = b;
        }
        break;
    case 0xE0:
        for (int k = 0, x = dx; k < bn; k++, x += dix * p)
            v->vram[vpa(v, pix_addr(m, x, dy + j * diy))] = VR(pix_addr(m, x, sy + j * diy));
        break;
    case 0x80: for (int i = 0; i < bn; i++) pset(v, m, dx + i * dix, dy + j * diy, (uint8_t)clr, op); break;
    case 0x90: for (int i = 0; i < bn; i++) pset(v, m, dx + i * dix, dy + j * diy, pget(v, m, sx + i * dix, sy + j * diy), op); break;
    }
}
void vdp_job_run(VDP *v, int upto)
{
    if (!v->job_active) return;
    if (upto > v->job_tny) upto = v->job_tny;
    while (v->job_done < upto) job_row(v, v->job_done++);
    if (v->job_done >= v->job_tny) { v->job_active = 0; cmd_done(v); }
}
static void job_set(VDP *v, int kind, int m, int W, int op, int clr, int sx, int sy, int dx, int dy, int dix, int diy, int bn, int p, int tny)
{
    int *q = v->job_p;
    q[0] = m; q[1] = W; q[2] = op; q[3] = clr; q[4] = sx; q[5] = sy; q[6] = dx; q[7] = dy; q[8] = dix; q[9] = diy; q[10] = bn; q[11] = p;
    v->job_kind = kind; v->job_done = 0; v->job_tny = tny; v->job_active = 1; v->job_t0 = v->job_t1 = 0;
    if (!v->progressive) vdp_job_run(v, tny);
}

static void cmd_start(VDP *v)
{
    if (v->job_active) { if ((v->reg[46] & 0xF0) == 0) { v->job_active = 0; cmd_done(v); } else vdp_job_run(v, v->job_tny); }
    int cmd = v->reg[46] & 0xF0, op = v->reg[46] & 15;
#ifdef VDP_TRACE
    { static int seen; extern unsigned long long vdp_trace_frame; if (!seen && vdp_trace_frame > 2500 && (v->vram[40 * 128 + 7] == 0xFF || v->vram[32768 + 40 * 128 + 7] == 0xFF)) { seen = 1; fprintf(stderr, "FIRSTFF f%llu p0=%02X p1=%02X\n", vdp_trace_frame, v->vram[40 * 128 + 7], v->vram[32768 + 40 * 128 + 7]); } }
    { extern unsigned long long vdp_trace_frame; fprintf(stderr, "f%llu ", vdp_trace_frame); } fprintf(stderr, "cmd %02X op %X mode %d sx %d sy %d dx %d dy %d nx %d ny %d clr %02X arg %02X\n", cmd, op, mode(v),
            R16(32) & 511, R16(34) & 1023, R16(36) & 511, R16(38) & 1023, R16(40) & 1023, R16(42) & 1023, v->reg[44], v->reg[45]);
#endif
    int m = cmd_mode(v), W = width_of(m);
    int sx = R16(32) & 511, sy = R16(34) & 1023, dx = R16(36) & 511, dy = R16(38) & 1023;
    int nx = R16(40) & 1023, ny = R16(42) & 1023, raw_nx = nx;
    int arg = v->reg[45];
    int dix = (arg & 4) ? -1 : 1, diy = (arg & 8) ? -1 : 1;
    int line_nx = nx, line_ny = ny;                 /* 3.0.10: LINE의 NX=0은 점 하나 (0 → 너비로 바꾸면 한 줄 전체를 그어 Maison Ikkoku 그림이 깨졌음) */
    if (!nx) nx = W;
    if (!ny) ny = 1024;
    v->stat[2] |= 0x01;                             /* CE */
    /* 명령 시간 (2.8.1, 3.4.2 ACC-03): 결과는 바로 만들지만 실제 VDP처럼 CE를 그 시간 동안 켜 둔다(msx.c).
     * openMSX가 잰 VRAM 접근 간격(VDP 클럭)을 명령마다 더함: 시작 지연 + 점·바이트마다 읽기·쓰기 + 줄 끝.
     * 접근은 그때 쓸 수 있는 자리(슬롯)를 기다리므로 화면 상태로 자리 간격을 정해 올림:
     * 화면 끔 154자리/줄, 스프라이트 끔 88, 스프라이트 켬 31 (한 줄 1368클럭) */
    {
        int p = ppb(m), bpl = (nx + p - 1) / p, dots = nx * ny;
        uint32_t sp, cc = 0;
        for (int pass = 0; pass < 2; pass++) {
        /* pass1 = 화면 밖(수직 귀선·테두리) 구간의 속도 — 화면 끔 표와 같음 */
        sp = (pass || !(v->reg[1] & 0x40)) ? 888 : (v->reg[8] & 0x02) ? 1555 : 4413;   /* 자리 간격 × 100 */
#define ACC(d) ((((uint32_t)(d) * 100u + sp - 1) / sp) * sp / 100u)
        switch (cmd) {
        case 0xC0: cc = 112 + (uint32_t)(bpl * ny) * ACC(46) + (uint32_t)ny * 58; break;                     /* HMMV */
        case 0xD0: cc = 100 + (uint32_t)(bpl * ny) * (ACC(24) + ACC(60)) + (uint32_t)ny * 68; break;         /* HMMM */
        case 0xE0: cc = 100 + (uint32_t)(((W - (dx & 511)) / p + 1) * ny) * (ACC(24) + ACC(36)) + (uint32_t)ny * 68; break;   /* YMMM */
        case 0x80: cc = 88 + (uint32_t)dots * (ACC(24) + ACC(72)) + (uint32_t)ny * 58; break;               /* LMMV */
        case 0x90: cc = 64 + (uint32_t)dots * (ACC(32) + ACC(24) + ACC(60)) + (uint32_t)ny * 68; break;     /* LMMM */
        case 0x70: {                                                                                                   /* LINE: 짧은 축 걸음마다 36 */
            int minor = line_ny < line_nx ? line_ny : line_nx;
            cc = 112 + (uint32_t)(line_nx + 1) * (ACC(24) + ACC(84)) + (uint32_t)minor * 36;
            break;
        }
        case 0x60: cc = 88 + (uint32_t)(W - (sx & 511)) * ACC(88) / 2; break;                                 /* SRCH: 평균 반 줄 */
        case 0x50: cc = 88 + ACC(24) + ACC(24); break;                                                         /* PSET */
        case 0x40: cc = 63 + ACC(24); break;                                                                   /* POINT */
        default: cc = 0; break;
        }
        if (pass) v->cmd_cost_off = cc; else v->cmd_cost = cc;
        }
#undef ACC
    }
    switch (cmd) {
    case 0x00: cmd_done(v); return;                 /* STOP */
    case 0x40: v->stat[7] = pget(v, m, sx, sy); cmd_done(v); return;      /* POINT */
    case 0x50: pset(v, m, dx, dy, v->reg[44], op); cmd_done(v); return;   /* PSET */
    case 0x60: {                                    /* SRCH */
        int eq = (arg & 2) != 0, x = sx;
        uint8_t c = (uint8_t)(v->reg[44] & (m == M_G5 ? 3 : m == M_G7 ? 0xFF : 15));
        v->stat[2] &= ~0x10;
        for (;;) {
            int hit = eq ? (pget(v, m, x, sy) != c) : (pget(v, m, x, sy) == c);
            if (hit) { v->stat[2] |= 0x10; v->stat[8] = (uint8_t)x; v->stat[9] = (uint8_t)((x >> 8) | 0xFE); break; }
            x += dix;
            if (x < 0 || x >= W) break;
        }
        cmd_done(v);
        return;
    }
    case 0x70: {                                    /* LINE (5.0.55: openMSX executeLine과 같은 순서·끝 조건) */
        int maj = arg & 1, x = dx, y = dy;
        uint32_t asx = (uint32_t)((line_nx - 1) >> 1);
        int anx = 0;
        for (;;) {
            pset(v, m, x & (W - 1), y & 1023, v->reg[44], op);
            if (!maj) {
                x += dix;
                if (anx++ == line_nx || (x & W)) break;
                if (asx < (uint32_t)line_ny) { asx += (uint32_t)line_nx; y += diy; if (diy < 0 && y < 0) break; }
                asx -= (uint32_t)line_ny; asx &= 1023;
            } else {
                y += diy;
                if (diy < 0 && y < 0) break;
                if (asx < (uint32_t)line_ny) { asx += (uint32_t)line_nx; x += dix; }
                asx -= (uint32_t)line_ny; asx &= 1023;
                if (anx++ == line_nx || (x & W)) break;
            }
        }
        cmd_done(v);
        return;
    }
    case 0x80: {                                    /* LMMV: 논리 채우기 */
        int tnx = clipn1_px(dx, nx, W, dix), tny = clipny1(dy, ny, diy);
        job_set(v, 0x80, m, W, op, v->reg[44], 0, 0, dx, dy, dix, diy, tnx, 1, tny);
        return;
    }
    case 0x90: {                                    /* LMMM: 논리 복사 */
        int tnx = clipn2_px(sx, dx, nx, W, dix), tny = clipny2(sy, dy, ny, diy);
        job_set(v, 0x90, m, W, op, 0, sx, sy, dx, dy, dix, diy, tnx, 1, tny);
        return;
    }
    case 0xA0:                                      /* LMCM: VRAM → CPU */
        v->cmd = 0xA0; v->c_x = v->c_x0 = sx; v->c_y = sy; v->c_dx = dix; v->c_dy = diy;
        v->c_nx = v->c_left = nx; v->c_ny = ny;
        v->stat[7] = pget(v, m, sx, sy);
        return;
    case 0xB0: case 0xF0:                           /* LMMC / HMMC: CPU → VRAM */
        v->cmd = cmd; v->c_x = v->c_x0 = dx; v->c_y = dy; v->c_dx = dix; v->c_dy = diy;
        if (cmd == 0xF0) { nx = clipn1_byte(dx & 511, raw_nx, W, ppb(m), dix); v->c_x = v->c_x0 = dx & ~(ppb(m) - 1); }
        else nx = clipn1_px(dx, raw_nx, W, dix);
        ny = clipny1(dy, ny, diy);
        v->c_nx = v->c_left = nx; v->c_ny = ny;
        cmd_cpu_byte(v, v->reg[44]);                /* 첫 바이트는 CLR */
        return;
    case 0xC0: {                                    /* HMMV: 바이트 채우기 */
        int bn = clipn1_byte(dx & 511, raw_nx, W, ppb(m), dix), tny = clipny1(dy, ny, diy);
        job_set(v, 0xC0, m, W, op, v->reg[44], 0, 0, dx, dy, dix, diy, bn, ppb(m), tny);
        return;
    }
    case 0xD0: {                                    /* HMMM: 바이트 복사 */
        int p = ppb(m), bn = clipn2_byte(sx, dx, raw_nx, W, p, dix), tny = clipny2(sy, dy, ny, diy);
        job_set(v, 0xD0, m, W, op, 0, sx, sy, dx, dy, dix, diy, bn, p, tny);
        return;
    }
    case 0xE0: {                                    /* YMMM: 세로 바이트 복사 (X는 DX부터 끝까지) */
        int p = ppb(m), bn = clipn1_byte(dx, 512, W, p, dix), tny = clipny2(sy, dy, ny, diy);
        job_set(v, 0xE0, m, W, op, 0, sx, sy, dx, dy, dix, diy, bn, p, tny);
        return;
    }
    default:
        cmd_done(v);
        return;
    }
}

/* YJK 한 줄 해독: raw = 256바이트(원래 값), 결과 RGB(각 0~255). yae면 A 비트(비트 3)가 1인 점은 팔레트 색(비트 7~4) */
void vdp_yjk_line(const uint8_t *raw, int yae, uint8_t rgb[256][3], uint8_t pal_idx[256])
{
    for (int x = 0; x < 256; x += 4) {
        const uint8_t *p = raw + x;
        int k = (p[0] & 7) | ((p[1] & 7) << 3), j = (p[2] & 7) | ((p[3] & 7) << 3);
        if (k & 0x20) k -= 64;
        if (j & 0x20) j -= 64;
        for (int i = 0; i < 4; i++) {
            pal_idx[x + i] = 0xFF;
            if (yae && (p[i] & 0x08)) { pal_idx[x + i] = (uint8_t)(p[i] >> 4); continue; }
            int y = yae ? (p[i] >> 4) * 2 : (p[i] >> 3);             /* YAE면 Y 4비트 */
            int r = y + j, g = y + k, b = (5 * y - 2 * j - k) / 4;
            r = r < 0 ? 0 : r > 31 ? 31 : r;
            g = g < 0 ? 0 : g > 31 ? 31 : g;
            b = b < 0 ? 0 : b > 31 ? 31 : b;
            rgb[x + i][0] = g31_tab[r];
            rgb[x + i][1] = g31_tab[g];
            rgb[x + i][2] = g31_tab[b];
        }
    }
}

#include "vdpk.inc"
