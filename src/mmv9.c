/* 만든이: KimTaeWoo
 * 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * mmv9.c (8.0.131) - 설명은 mmv9.h. V9990 의 내부 변수는 건드리지 않고 포트 쓰기(v9990_write)만 씀 → 실물 카트리지가 하는 일과 같다. */
#include <stdint.h>
#include <stddef.h>
#include "mmv9.h"
#include <string.h>
#include "mmbar.h"
#include "mp4play.h"
#include "v9_conv.h"

#define BW 768
#define BH 240
static uint32_t rgb[BW * BH];
static int diffuse_prev_ok, diffuse_prev_w, diffuse_prev_ov;       /* 8.0.155 앞 프레임의 오차 확산 결과를 다시 쓸 수 있는지 (mmv9_start 가 지움) */
int mmv9_is(int scr) { return scr == MMV9_B2_16 || scr == MMV9_B4_256; }
static void reg(V9990 *v, int r, uint8_t val) { v9990_write(v, 4, (uint8_t)(0x80 | r)); v9990_write(v, 3, val); }   /* 80h = 번호 자동 증가 끔 */
static void wa(V9990 *v, uint32_t a) { reg(v, 0, (uint8_t)a); reg(v, 1, (uint8_t)(a >> 8)); reg(v, 2, (uint8_t)((a >> 16) & 7)); }

void mmv9_start(V9990 *v, int scr)
{
    diffuse_prev_ok = 0;
    v9990_write(v, 7, 0x02);                         /* 소프트 리셋 (레지스터 모두 0) */
    v9990_write(v, 7, 0x01);                         /* 해제 + MCLK (B2·B4 는 이 방식) */
    reg(v, 6, scr == MMV9_B2_16 ? 0x97 : 0xAA);      /* 비트맵 | B2/B4 | 이미지 폭 512/1024 | 16비트/8비트 색 */
    reg(v, 7, 0x00);
    reg(v, 13, scr == MMV9_B2_16 ? 0x00 : 0x40);     /* 8비트 직접색(GGGRRRBB) */
    reg(v, 15, 0); reg(v, 16, 0);
    for (int r = 17; r <= 20; r++) reg(v, r, 0);     /* 스크롤 0 */
    wa(v, 0);
    for (uint32_t i = 0; i < 240u * 1024u; i++) v9990_write(v, 0, 0);   /* 화면 비움 */
    reg(v, 8, 0xC0);                                 /* 화면 켬, 커서 끔 */
}

void mmv9_stop(V9990 *v) { v9990_write(v, 7, 0x02); }


/* 8.0.147: 256색(GGGRRRBB)으로 줄일 때 오차 확산(Floyd-Steinberg, 줄마다 방향 바꿈). 점 하나가 못 낸 색 차이를 옆·아래 점에 나눠 줘서 부드러운 색 변화(그라데이션)와 가는 무늬가 4x4 바둑판 흔들기보다 훨씬 원본에 가깝다.
 * 오차는 1/16 점 단위 정수. plain_top 보다 위 줄과 plain_bot 이하 줄(글자 줄)은 오차를 퍼뜨리지 않고 가장 가까운 색으로 (글자 둘레에 얼룩이 안 생기게). */
static int quant_level(int v, int n) { return (v * n * 2 + 255) / 510; }          /* 0..255 -> 0..n (반올림) */
static int level_value(int q, int n) { return (q * 255 * 2 + n) / (2 * n); }     /* 0..n -> 0..255 */
/* 8.0.155: 점마다 나눗셈 6번을 없애려고 표로 바꿈 (결과는 8.0.147 과 바이트까지 같음, tests/v9difftest 가 옛 계산과 비교).
 * dq_r/dq_b: 0..255 -> 단계 (R·G 는 7단계, B 는 3단계), dv_r/dv_b: 단계 -> 다시 만든 값 x 16 */
static uint8_t dq_r[256], dq_b[256];
static int dv_r[8], dv_b[4], dtab_ready;
static void diffuse_tables(void)
{
    for (int v = 0; v < 256; v++) { dq_r[v] = (uint8_t)quant_level(v, 7); dq_b[v] = (uint8_t)quant_level(v, 3); }
    for (int q = 0; q <= 7; q++) dv_r[q] = level_value(q, 7) * 16;
    for (int q = 0; q <= 3; q++) dv_b[q] = level_value(q, 3) * 16;
    dtab_ready = 1;
}
static int clamp16(int v) { return v < 0 ? 0 : (v > 255 * 16 ? 255 * 16 : v); }
/* 한 줄을 오차 확산으로 변환. dir=+1 왼쪽→오른쪽, -1 오른쪽→왼쪽.  cur 는 이 줄에 들어온 오차, nxt 는 다음 줄로 줄 오차 (둘 다 가로 w+2칸, x+1 이 이 점) */
static void diffuse_row(const uint32_t *srow, uint8_t *drow, int w, int dir, int (*cur)[BW + 2], int (*nxt)[BW + 2])
{
    int *cr = cur[0], *cg = cur[1], *cb = cur[2], *nr = nxt[0], *ng = nxt[1], *nb = nxt[2];
    int x = dir > 0 ? 0 : w - 1;
    for (int i = 0; i < w; i++, x += dir) {
        uint32_t px = srow[x];
        int vr = clamp16((int)((px >> 16) & 255) * 16 + cr[x + 1]), vg = clamp16((int)((px >> 8) & 255) * 16 + cg[x + 1]), vb = clamp16((int)(px & 255) * 16 + cb[x + 1]);
        int qr = dq_r[(vr + 8) >> 4], qg = dq_r[(vg + 8) >> 4], qb = dq_b[(vb + 8) >> 4];
        int er = vr - dv_r[qr], eg = vg - dv_r[qg], eb = vb - dv_b[qb];
        int r7 = (er * 7) >> 4, r3 = (er * 3) >> 4, r5 = (er * 5) >> 4;
        cr[x + 1 + dir] += r7; nr[x + 1 - dir] += r3; nr[x + 1] += r5; nr[x + 1 + dir] += er - r7 - r3 - r5;
        r7 = (eg * 7) >> 4; r3 = (eg * 3) >> 4; r5 = (eg * 5) >> 4;
        cg[x + 1 + dir] += r7; ng[x + 1 - dir] += r3; ng[x + 1] += r5; ng[x + 1 + dir] += eg - r7 - r3 - r5;
        r7 = (eb * 7) >> 4; r3 = (eb * 3) >> 4; r5 = (eb * 5) >> 4;
        cb[x + 1 + dir] += r7; nb[x + 1 - dir] += r3; nb[x + 1] += r5; nb[x + 1 + dir] += eb - r7 - r3 - r5;
        drow[x] = (uint8_t)((qg << 5) | (qr << 2) | qb);
    }
}
/* 글자 줄(plain): 오차를 받지도 퍼뜨리지도 않고 가장 가까운 색 */
static void nearest_row(const uint32_t *srow, uint8_t *drow, int w)
{
    for (int x = 0; x < w; x++) {
        uint32_t px = srow[x];
        drow[x] = (uint8_t)((dq_r[(px >> 8) & 255] << 5) | (dq_r[(px >> 16) & 255] << 2) | dq_b[px & 255]);
    }
}
void mmv9_diffuse8(const uint32_t *src, int w, int h, uint8_t *dst, int plain_top, int plain_bot)
{
    static int err_row[2][3][BW + 2];
    if (!dtab_ready) diffuse_tables();
    memset(err_row, 0, sizeof err_row);
    for (int y = 0; y < h; y++) {
        const uint32_t *srow = src + (size_t)y * (size_t)w;
        uint8_t *drow = dst + (size_t)y * (size_t)w;
        if (y < plain_top || y >= plain_bot) { nearest_row(srow, drow, w); memset(err_row[(y & 1) ^ 1], 0, sizeof err_row[0]); continue; }   /* 다음 줄이 받을 오차는 0 */
        int (*cur)[BW + 2] = err_row[y & 1], (*nxt)[BW + 2] = err_row[(y & 1) ^ 1];
        for (int c = 0; c < 3; c++) memset(nxt[c], 0, sizeof(int) * (size_t)(w + 2));   /* 다음 줄 오차는 이 줄이 채움 */
        diffuse_row(srow, drow, w, (y & 1) ? -1 : 1, cur, nxt);
    }
}

uint32_t mmv9_bg;
#define MMV9_OVL_GREEN 0x10D040u                                 /* 0x00RRGGBB */
static void ovl(int w, const uint8_t *ov)           /* 글자 줄(X86MV 가 보낸 4096바이트)을 RGB 위에: 위 24줄·아래 40줄, 가로 256칸을 w 폭으로 늘림, 색 0=검정 1=흰색 */
{
    for (int y = 0; y < BH; y++) {
        int blk = y < 24 ? y >> 3 : (y >= BH - 40 ? 3 + ((y - (BH - 40)) >> 3) : -1);
        if (blk < 0) continue;
        uint32_t *row = rgb + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            int sx = x * 256 / w, ix = blk * 256 + (sx >> 3) * 8 + (y & 7);
            uint8_t cb = ov[2048 + ix];
            int on = (ov[ix] >> (7 - (sx & 7))) & 1, lit = on ? (cb >> 4) : (cb & 15);
            row[x] = lit ? MMV9_OVL_GREEN : mmv9_bg;                      /* 8.0.143: X86MV 의 SCREEN 2 화면처럼 초록 (예전: 흰색) */
        }
    }
}

/* 8.0.151 파란 표시줄: mmbar 의 칸 역할(어둠·밝음·막대·글자)을 RGB 로 바꿔 맨 아래 20줄에 얹음 (가로 256칸을 w 폭으로 늘림) */
static const uint32_t bar_rgb[4] = { 0x000000u, 0xFFFFFFu, 0x2850D8u, 0xFFFFFFu };
uint32_t mmv9_bar_color(int role) { return bar_rgb[role & 3]; }
static void paint_bar(int w, const MmBarState *st)
{
    uint8_t roles[MMBAR_H][MMBAR_W];
    mmbar_render(st, roles);
    for (int j = 0; j < MMBAR_H; j++) {
        uint32_t *row = rgb + (size_t)(BH - MMBAR_H + j) * (size_t)w;
        for (int x = 0; x < w; x++) row[x] = bar_rgb[roles[j][x * MMBAR_W / w] & 3];
    }
}

static void paint_overlay(int w, int scr, const MmBarState *st, int with_badge)   /* 8.0.152 이름표(글자 줄 아래 28줄째)와 일시정지 큰 표시(가운데) */
{
    uint8_t badge[MMBAR_BADGE_H][MMBAR_BADGE_W], sign[MMBAR_SIGN_H][MMBAR_SIGN_W];
    if (with_badge) {
        mmbar_badge(scr, badge);
        for (int j = 0; j < MMBAR_BADGE_H; j++) for (int x = MMBAR_BADGE_X * w / 256; x < (MMBAR_BADGE_X + MMBAR_BADGE_W) * w / 256; x++) {
            int i = x * 256 / w - MMBAR_BADGE_X; if (i < 0 || i >= MMBAR_BADGE_W || badge[j][i] == MMBAR_NONE) continue;
            rgb[(size_t)(28 + j) * (size_t)w + x] = bar_rgb[badge[j][i] & 3];
        }
    }
    if (st->paused) {
        mmbar_sign(sign);
        for (int j = 0; j < MMBAR_SIGN_H; j++) for (int x = MMBAR_SIGN_X * w / 256; x < (MMBAR_SIGN_X + MMBAR_SIGN_W) * w / 256; x++) {
            int i = x * 256 / w - MMBAR_SIGN_X; if (i < 0 || i >= MMBAR_SIGN_W || sign[j][i] == MMBAR_NONE) continue;
            rgb[(size_t)(BH / 2 - 20 + j) * (size_t)w + x] = bar_rgb[sign[j][i] & 3];
        }
    }
}

void mmv9_frame(V9990 *v, int scr, const uint8_t *ov) { mmv9_frame_bar(v, scr, ov, 0); }

void mmv9_frame_bar(V9990 *v, int scr, const uint8_t *ov, const MmBarState *bar)
{
    int w = scr == MMV9_B2_16 ? 384 : 768;
    mp4p_fill = mmv9_bg;                                   /* 8.0.148: 영상 둘레 띠도 테두리 색으로 */
    int drawn = mp4p_rgb32(rgb, w, BH, w, scr == MMV9_B2_16);
    mp4p_fill = 0;
    if (!drawn) return;
    if (ov && !bar) ovl(w, ov);                                        /* 8.0.161: 파란 표시줄이 있으면 옛 초록 글자 줄은 안 얹음 */
    if (ov && bar) paint_bar(w, bar);
    if (bar) paint_overlay(w, scr, bar, ov != 0);                      /* 글자 줄이 켜졌을 때만 (꺼지면 영상을 가리지 않음) */
    static uint8_t diffused[BW * BH];
    static uint32_t prev_rgb[BW * BH];                                  /* 8.0.155: 앞 프레임과 그림이 똑같으면(일시정지·정지 화면) 오차 확산을 다시 하지 않고 앞 결과를 씀 */
    int use_diffuse = scr == MMV9_B4_256;
    if (use_diffuse) {
        size_t bytes = (size_t)w * (size_t)BH * sizeof(uint32_t);
        int same = diffuse_prev_ok && diffuse_prev_w == w && diffuse_prev_ov == (ov != 0) && memcmp(rgb, prev_rgb, bytes) == 0;
        if (!same) { mmv9_diffuse8(rgb, w, BH, diffused, ov ? 24 : 0, ov ? BH - 40 : BH); memcpy(prev_rgb, rgb, bytes); diffuse_prev_ok = 1; diffuse_prev_w = w; diffuse_prev_ov = ov != 0; }
    }
    for (int y = 0; y < BH; y++) {
        const uint32_t *row = rgb + (size_t)y * (size_t)w;
        wa(v, (uint32_t)y * 1024u);
        static uint8_t ln[BW * 2];
        if (use_diffuse) { v9990_write_vram(v, diffused + (size_t)y * (size_t)w, w); continue; }       /* 8.0.147 */
        V9P15 a = { row, ln, w }; v9_pack15(&a); v9990_write_vram(v, ln, 2 * w);        /* 화면 13 (16비트 색) */
    }
}
