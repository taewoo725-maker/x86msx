/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mp4play.c (8.0.91) - MP4 → H.264(baseline) 해독 → MSX 비트맵(SCREEN 5·6·7·8) 그림.
 *  표 읽기: 샘플 크기·위치·시각·키프레임을 열 때 한 번에 메모리로 (최대 MP4P_MAXS 샘플)
 *  해독: third_party/h264bsd (malloc 대신 아래 고정 영역 할당기)
 *  색 줄이기: 화면 크기로 줄이고(상자 평균, 위아래/좌우 검은 띠) → 3비트 RGB 512칸 히스토그램에서 팔레트(중앙값 자르기 + k-평균)
 *            → 오차 확산(Floyd-Steinberg). 팔레트는 새 것이 15% 넘게 좋아질 때만 바꿈 (깜박임 방지). */
#include <string.h>
#include "mp4play.h"
#include "mp4skip.h"
#include "mp4limits.h"
#include "v9_conv.h"
#include "../third_party/h264bsd/h264bsd_decoder.h"
#include "h264oh.h"
#include "../third_party/faad2/neaacdec.h"

#ifdef MP4P_DEBUG
#include <stdio.h>
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif
#define MP4P_MAXS   262144u
#define MP4P_MAXSMP (2u << 20)            /* 한 샘플 최대 2 MiB (8.0.140: 1080p 키프레임) */
#define ARENA_SIZE  (64u << 20)           /* 8.0.140: 28 → 64 MiB (1080p 그림 한 장 3 MB × 참조 프레임 여러 장) */
#define MAXW 512
#define MAXH 212

/* ---- 해독기용 고정 영역 할당기 (해제는 영역 전체를 다시 쓸 때만) ---- */
static uint8_t arena[ARENA_SIZE] __attribute__((aligned(16)));
static uint32_t arena_used;
void *mp4h_alloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (n == 0) n = 16;
    if (arena_used + n > ARENA_SIZE || arena_used + n < arena_used) return 0;
    void *p = arena + arena_used; arena_used += (uint32_t)n;
    return p;
}
void mp4h_free(void *p) { (void)p; }
uint32_t mp4p_arena_used(void) { return arena_used; }

/* ---- 상태 ---- */
static struct {
    Mp4Read rd; void *ctx; uint32_t fsize;
    int open; uint32_t n; Mp4Info mi;
    int lenSize;
    storage_t *dec;
    int mj; int oh; uint8_t *ybuf; uint32_t ybuf_n; uint32_t lag_ms;   /* 8.0.96 Main/High: OpenH264 해독기, 그림 복사 자리, 그림 순서 바꾸기로 늦게 나오는 시간 */
    uint32_t cur;                         /* 다음에 해독할 샘플 */
    int have_pic; uint32_t pic_sample;    /* 마지막으로 해독한 그림이 있고 아직 화면용으로 안 바꿨음 */
    const uint8_t *yuv; uint32_t cw, ch, cl, ct, ccw, cch, mbw, mbh;   /* 마지막 해독 그림 (자르기 포함) */
    int scr, lines, fit, dither, maxfps;
    Mp4Pic pic; uint32_t dropped, skipped; int later_in_window; uint32_t shown_ms; int ended;
    int pal_valid; uint8_t pal[16][3]; int npal;
    uint32_t dur_ms;
} P;

/* 8.0.122 표 크기: 샘플 262,144개 이하는 아래 고정 표(전과 같음), 그보다 많은 긴 영상만 열 때 영상 크기에 맞춰 작업 메모리에서 따로 잡음 */
static uint32_t s_off_st[MP4P_MAXS], s_size_st[MP4P_MAXS], s_dts_st[MP4P_MAXS];
static uint8_t s_key_st[MP4P_MAXS / 8];
static uint32_t *s_off = s_off_st, *s_size = s_size_st, *s_dts = s_dts_st;
static uint8_t *s_key = s_key_st;
static uint32_t s_cap = MP4P_MAXS;
static uint8_t s_buf[MP4P_MAXSMP], s_ab[MP4P_MAXSMP + 4096];
static uint8_t s_img[MAXW * MAXH];                    /* 완성 그림 (가장 큰 SCREEN 7·8: 256 B × 212 줄 = 54272) */
static uint8_t s_rgb[MAXW * MAXH * 3];

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int rdb(uint32_t off, uint8_t *b, uint32_t n) { return (off > P.fsize || n > P.fsize - off) ? -1 : P.rd(P.ctx, off, b, n); }
#define MP4A_MAXS 262144u
#define MP4A_MAXSMP 16384u
static uint32_t a_off_st[MP4A_MAXS], a_size_st[MP4A_MAXS];
static uint32_t *a_off = a_off_st, *a_size = a_size_st;
static uint32_t a_cap = MP4A_MAXS;
static void tabs_reset(void) { s_off = s_off_st; s_size = s_size_st; s_dts = s_dts_st; s_key = s_key_st; s_cap = MP4P_MAXS; a_off = a_off_st; a_size = a_size_st; a_cap = MP4A_MAXS; }
static int s_why;                                           /* 8.0.124 진단: 코드 4 를 돌려준 곳 번호(1~10) */
static int pwhy(int n) { s_why = n; return MP4E_PROFILE; }
int mp4p_why(void) { return s_why; }
static int tabs_big_v(uint32_t n)          /* 영상 표를 n 개 크기로 새로 잡음. 0 = 성공 */
{
    uint32_t *o = (uint32_t *)mp4h_alloc((size_t)n * 4), *z = (uint32_t *)mp4h_alloc((size_t)n * 4), *d = (uint32_t *)mp4h_alloc((size_t)n * 4);
    uint8_t *k = (uint8_t *)mp4h_alloc((n + 7) / 8 + 8);
    if (!o || !z || !d || !k) return -1;
    s_off = o; s_size = z; s_dts = d; s_key = k; s_cap = n; return 0;
}
static int tabs_big_a(uint32_t n)          /* 소리 표 */
{
    uint32_t *o = (uint32_t *)mp4h_alloc((size_t)n * 4), *z = (uint32_t *)mp4h_alloc((size_t)n * 4);
    if (!o || !z) return -1;
    a_off = o; a_size = z; a_cap = n; return 0;
}
static uint8_t a_buf[MP4A_MAXSMP];
static struct { NeAACDecHandle h; int open; uint32_t n, cur, delta, ts, rate, frames; int ch, prime; int pcm, bits, ba; uint32_t pi, po; int pk; uint32_t bf; int alac; } A;   /* pk = 8.0.153 MOV 의 PCM 소리 (1 sowt, 2 twos, 3 raw, 4 in24, 5 in32), bf = 한 점(프레임)의 바이트 */
static int decode_sample(uint32_t i);
#include "alacdec.h"
#include "mp4play_avi.inc"                                 /* 8.0.121 AVI(MJPEG+PCM) 부품: 이 파일의 MP4 코드는 건드리지 않고 분기 한 줄씩만 연결 */

/* 순서대로 u32 읽기 (표를 256개씩 끊어서) */
typedef struct { uint32_t base, cnt, idx, bn, bi; int stride; uint8_t b[2048]; int err; } Seq;
static void seq_init(Seq *s, uint32_t base, uint32_t cnt, int stride) { s->base = base; s->cnt = cnt; s->idx = 0; s->bn = s->bi = 0; s->stride = stride; s->err = 0; }
static const uint8_t *seq_next(Seq *s)
{
    if (s->idx >= s->cnt) { s->err = 1; return 0; }
    if (s->bi >= s->bn) {
        uint32_t per = sizeof s->b / (uint32_t)s->stride, m = s->cnt - s->idx < per ? s->cnt - s->idx : per;
        if (rdb(s->base + s->idx * (uint32_t)s->stride, s->b, m * (uint32_t)s->stride)) { s->err = 1; return 0; }
        s->bn = m; s->bi = 0;
    }
    const uint8_t *r = s->b + s->bi * (uint32_t)s->stride; s->bi++; s->idx++;
    return r;
}

static int build_tables(void)
{
    const Mp4Info *m = &P.mi; uint8_t h[16];
    if (!m->tb_stsz || !m->tb_stsc || (!m->tb_stco && !m->tb_co64) || !m->tb_stts || !m->timescale) return MP4E_READ;
    if (rdb(m->tb_stsz, h, 12)) return MP4E_READ;
    uint32_t fixed = be32(h + 4), n = be32(h + 8);
    if (n == 0) return MP4E_READ;
    if (n > s_cap && (n > 4000000u || tabs_big_v(n))) { DBG("too many samples %u\n", n); return MP4E_TOOLONG; }   /* 8.0.122 긴 영상 */
    P.n = n;
    Seq q;
    if (fixed) for (uint32_t i = 0; i < n; i++) s_size[i] = fixed;
    else { seq_init(&q, m->tb_stsz + 12, n, 4); for (uint32_t i = 0; i < n; i++) { const uint8_t *e = seq_next(&q); if (!e) return MP4E_READ; s_size[i] = be32(e); } }
    /* 위치: stsc 묶음 × stco 덩어리 */
    uint32_t nchunk, nruns; int is64 = m->tb_co64 != 0; uint32_t cbase = is64 ? m->tb_co64 : m->tb_stco;
    if (rdb(cbase + 4, h, 4)) return MP4E_READ; nchunk = be32(h);
    if (rdb(m->tb_stsc + 4, h, 4)) return MP4E_READ; nruns = be32(h);
    if (nchunk == 0 || nruns == 0) return MP4E_READ;
    Seq cq, rq; seq_init(&cq, cbase + 8, nchunk, is64 ? 8 : 4); seq_init(&rq, m->tb_stsc + 8, nruns, 12);
    const uint8_t *r = seq_next(&rq); if (!r) return MP4E_READ;
    uint32_t spc = be32(r + 4), nfirst = 0xFFFFFFFFu, nspc = 0, s = 0;
    if (rq.idx < rq.cnt) { const uint8_t *r2 = seq_next(&rq); if (!r2) return MP4E_READ; nfirst = be32(r2); nspc = be32(r2 + 4); }
    for (uint32_t c = 1; c <= nchunk && s < n; c++) {
        while (c >= nfirst) {                                  /* 다음 묶음으로 */
            spc = nspc;
            if (rq.idx < rq.cnt) { const uint8_t *r3 = seq_next(&rq); if (!r3) return MP4E_READ; nfirst = be32(r3); nspc = be32(r3 + 4); }
            else nfirst = 0xFFFFFFFFu;
        }
        const uint8_t *e = seq_next(&cq); if (!e) return MP4E_READ;
        uint32_t o = is64 ? (be32(e) ? 0xFFFFFFFFu : be32(e + 4)) : be32(e);
        if (o == 0xFFFFFFFFu) return pwhy(1);               /* 4GB 넘는 위치 */
        for (uint32_t k = 0; k < spc && s < n; k++, s++) { s_off[s] = o; o += s_size[s]; }
    }
    if (s < n) return MP4E_READ;
    /* 시각 (밀리초) */
    if (rdb(m->tb_stts + 4, h, 4)) return MP4E_READ;
    uint32_t nstts = be32(h); Seq tq; seq_init(&tq, m->tb_stts + 8, nstts, 8);
    uint64_t t = 0; uint32_t i = 0;
    for (uint32_t k = 0; k < nstts && i < n; k++) {
        const uint8_t *e = seq_next(&tq); if (!e) return MP4E_READ;
        uint32_t cnt = be32(e), d = be32(e + 4);
        for (uint32_t j = 0; j < cnt && i < n; j++, i++) { s_dts[i] = (uint32_t)(t * 1000u / m->timescale); t += d; }
    }
    if (i < n) { uint32_t last = i ? s_dts[i - 1] : 0, step = i > 1 ? s_dts[i - 1] - s_dts[i - 2] : 67; for (; i < n; i++) { last += step; s_dts[i] = last; } }
    P.dur_ms = (uint32_t)(t * 1000u / m->timescale);
    /* 키프레임 */
    memset(s_key, 0, (n + 7) / 8);
    if (m->tb_stss) {
        if (rdb(m->tb_stss + 4, h, 4)) return MP4E_READ;
        uint32_t ns = be32(h); Seq kq; seq_init(&kq, m->tb_stss + 8, ns, 4);
        for (uint32_t k = 0; k < ns; k++) { const uint8_t *e = seq_next(&kq); if (!e) return MP4E_READ; uint32_t v = be32(e); if (v >= 1 && v <= n) s_key[(v - 1) >> 3] |= (uint8_t)(1u << ((v - 1) & 7)); }
    } else memset(s_key, 0xFF, (n + 7) / 8);
    if (!(s_key[0] & 1)) s_key[0] |= 1;
    return MP4E_NONE;
}
static int is_key(uint32_t i) { return (s_key[i >> 3] >> (i & 7)) & 1; }

/* ---- 해독 ---- */
static int feed_nal(const uint8_t *nal, uint32_t len)          /* 시작 코드를 붙여 해독기에 넣음 */
{
    static uint8_t tmp[4096 + 8];
    if (len + 4 > sizeof tmp) return -1;
    tmp[0] = 0; tmp[1] = 0; tmp[2] = 0; tmp[3] = 1; memcpy(tmp + 4, nal, len);
    if (P.oh) { Oh264Pic op; (void)oh264_decode(tmp, (int)len + 4, 0, &op); return 0; }
    u32 left = len + 4, used = 0; u8 *p = tmp; int pic = 0, guard = 8;
    while (left > 0 && guard-- > 0) {
        u32 r = h264bsdDecode(P.dec, p, left, 0, &used);
        if (r == H264BSD_PIC_RDY) pic = 1;
        else if (r == H264BSD_ERROR || r == H264BSD_PARAM_SET_ERROR || r == H264BSD_MEMALLOC_ERROR) { if (r != H264BSD_ERROR) return -1; }
        if (used == 0 && r != H264BSD_HDRS_RDY) break;
        p += used; left -= used;
    }
    return pic;
}
static void grab_pic(void)                                      /* 해독 결과 그림을 잡아 둠 */
{
    u32 id, idr, err; u8 *y = h264bsdNextOutputPicture(P.dec, &id, &idr, &err), *last = 0;
    while (y) { last = y; y = h264bsdNextOutputPicture(P.dec, &id, &idr, &err); }
    if (!last) return;
    P.mbw = h264bsdPicWidth(P.dec) * 16; P.mbh = h264bsdPicHeight(P.dec) * 16;
    u32 flag, l, w, t, h; h264bsdCroppingParams(P.dec, &flag, &l, &w, &t, &h);
    if (flag && w && h && l + w <= P.mbw && t + h <= P.mbh) { P.cl = l; P.cw = w; P.ct = t; P.ch = h; } else { P.cl = P.ct = 0; P.cw = P.mbw; P.ch = P.mbh; }
    P.yuv = last; P.have_pic = 1;
}
static void oh_take(const Oh264Pic *op)                         /* OpenH264 그림 → I420 복사 (줄 간격 = 짝수 맞춘 폭) */
{
    uint32_t w = (uint32_t)op->w, h = (uint32_t)op->h, mw = (w + 1) & ~1u, mh = (h + 1) & ~1u;
    if (!mp4lim_size_ok(mw, mh)) return;
    uint8_t *d = P.ybuf, *du = d + mw * mh, *dv = du + (mw / 2) * (mh / 2);
    for (uint32_t y = 0; y < mh; y++) { uint32_t sy = y < h ? y : h - 1; memcpy(d + y * mw, op->y + (size_t)sy * (uint32_t)op->sy, w); if (mw > w) d[y * mw + w] = d[y * mw + w - 1]; }
    for (uint32_t y = 0; y < mh / 2; y++) {
        uint32_t sy = y < (h + 1) / 2 ? y : (h + 1) / 2 - 1;
        memcpy(du + y * (mw / 2), op->u + (size_t)sy * (uint32_t)op->suv, (w + 1) / 2);
        memcpy(dv + y * (mw / 2), op->v + (size_t)sy * (uint32_t)op->suv, (w + 1) / 2);
    }
    P.mbw = mw; P.mbh = mh; P.cl = P.ct = 0; P.cw = w; P.ch = h; P.yuv = d; P.have_pic = 1;
}
static void oh_drain(void) { Oh264Pic op; while (oh264_flush(&op) > 0) {} }   /* 해독기 안에 남은 그림을 버림 (건너뛰기 때) */
static int decode_sample(uint32_t i)                            /* 1 = 그림 나옴, 0 = 안 나옴, -1 = 오류 */
{
    uint32_t sz = s_size[i];
    if (sz == 0 || sz > MP4P_MAXSMP) return 0;
    if (rdb(s_off[i], s_buf, sz)) return -1;
    if (P.mj) return avi_decode(i, sz);                          /* 8.0.121 AVI: mp4play_avi.inc */
    if (P.oh && sz > (uint32_t)P.lenSize && mp4skip_can_skip(mp4p_skip_nonref, s_buf[P.lenSize], P.later_in_window)) { P.skipped++; return 0; }   /* 8.0.139: 참조 안 되는 프레임은 화면에 안 나오므로 해독 생략 */
    uint32_t p = 0; int pic = 0;
    while (p + (uint32_t)P.lenSize <= sz) {
        uint32_t l = 0; for (int k = 0; k < P.lenSize; k++) l = l << 8 | s_buf[p + (uint32_t)k];
        p += (uint32_t)P.lenSize;
        if (l == 0 || l > sz - p) break;
        /* 큰 NAL 은 start code 와 함께 s_ab 에 복사해 해독 */
        s_ab[0] = 0; s_ab[1] = 0; s_ab[2] = 0; s_ab[3] = 1; memcpy(s_ab + 4, s_buf + p, l);
        if (P.oh) {
            Oh264Pic op; int r = oh264_decode(s_ab, (int)l + 4, i, &op);
            if (r > 0) { oh_take(&op); P.pic_sample = op.ts; pic = 1; uint32_t lg = op.ts < i ? s_dts[i] - s_dts[op.ts] : 0; if (lg > P.lag_ms && lg < 1000) P.lag_ms = lg; }
            p += l; continue;
        }
        u32 left = l + 4, used = 0; u8 *q = s_ab; int guard = 8;
        while (left > 0 && guard-- > 0) {
            u32 r = h264bsdDecode(P.dec, q, left, 0, &used); DBG("  nal type %d len %u -> r=%u used=%u\n", q[4] & 31, l, r, used);
            if (r == H264BSD_PIC_RDY) pic = 1;
            else if (r == H264BSD_MEMALLOC_ERROR) return -1;
            if (used == 0 && r != H264BSD_HDRS_RDY) break;
            q += used; left -= used;
        }
        p += l;
    }
    if (pic && !P.oh) { grab_pic(); P.pic_sample = i; }
    return pic;
}

/* ---- 색 줄이기 ---- */
static const uint8_t lv8[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };          /* 3비트 → 8비트 */
static int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void yuv_to_rgb(int Y, int U, int V, int hd, uint8_t *o)
{
    int c = (Y - 16) * 298, d = U - 128, e = V - 128, r, g, b;
    if (hd) { r = (c + 459 * e + 128) >> 8; g = (c - 55 * d - 136 * e + 128) >> 8; b = (c + 541 * d + 128) >> 8; }
    else    { r = (c + 409 * e + 128) >> 8; g = (c - 100 * d - 208 * e + 128) >> 8; b = (c + 516 * d + 128) >> 8; }
    o[0] = (uint8_t)clamp8(r); o[1] = (uint8_t)clamp8(g); o[2] = (uint8_t)clamp8(b);
}

static void scale_to_rgb(void)                                  /* 해독 그림 → 화면 점 크기 RGB (검은 띠 포함) */
{
    int W = P.pic.w, H = P.pic.h, wx = W / (W > 256 ? 2 : 1);   /* 256 기준 가로 */
    uint32_t sw = P.cw, sh = P.ch, cl = P.cl, ct = P.ct;
    uint32_t dw, dh;                                            /* 256 기준 */
    if (P.fit == 2) { dw = 256; dh = (uint32_t)H; }
    else if (P.fit == 0) {                                      /* 가득 채움: 화면 비율에 맞는 가운데 부분만 */
        uint32_t nw = sh * 256u / (uint32_t)H;                   /* 화면 비율에서 sh 높이가 요구하는 가로 */
        if (nw > sw) { uint32_t nh = sw * (uint32_t)H / 256u; ct += (sh - nh) / 2; sh = nh; }
        else { cl += (sw - nw) / 2; sw = nw; }
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        dw = 256; dh = (uint32_t)H;
    } else {
        uint32_t a = 256u * sh, b = (uint32_t)H * sw;           /* 가로 맞춤이면 dh = 256*sh/sw */
        if (a <= b) { dw = 256; dh = 256u * sh / sw; } else { dh = (uint32_t)H; dw = (uint32_t)H * sw / sh; }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    int mul = W / wx;                                           /* 512 폭이면 2 */
    uint32_t DW = dw * (uint32_t)mul;
    uint32_t ox = ((uint32_t)W - DW) / 2, oy = ((uint32_t)H - dh) / 2;
    memset(s_rgb, 0, (size_t)W * (size_t)H * 3);
    int hd = sh >= 720;
    const uint8_t *Y = P.yuv, *U = Y + P.mbw * P.mbh, *V = U + (P.mbw / 2) * (P.mbh / 2);
    uint32_t cwid = P.mbw / 2;
    uint32_t sx_step = (sw << 16) / DW, sy_step = (sh << 16) / dh;
    int nx = (int)((sx_step + 0xFFFF) >> 16); if (nx < 1) nx = 1; if (nx > 3) nx = 3;
    int ny = (int)((sy_step + 0xFFFF) >> 16); if (ny < 1) ny = 1; if (ny > 3) ny = 3;
    for (uint32_t y = 0; y < dh; y++) {
        uint8_t *row = s_rgb + (((size_t)(oy + y)) * (size_t)W + ox) * 3;
        for (uint32_t x = 0; x < DW; x++) {
            uint32_t sy0 = y * sy_step, sx0 = x * sx_step; int sy_acc = 0, su = 0, sv = 0, cnt = 0;
            for (int j = 0; j < ny; j++) {
                uint32_t sy = ct + ((sy0 + (sy_step * (uint32_t)(2 * j + 1)) / (uint32_t)(2 * ny)) >> 16); if (sy >= P.mbh) sy = P.mbh - 1;
                for (int i = 0; i < nx; i++) {
                    uint32_t sx = cl + ((sx0 + (sx_step * (uint32_t)(2 * i + 1)) / (uint32_t)(2 * nx)) >> 16); if (sx >= P.mbw) sx = P.mbw - 1;
                    sy_acc += Y[sy * P.mbw + sx]; su += U[(sy >> 1) * cwid + (sx >> 1)]; sv += V[(sy >> 1) * cwid + (sx >> 1)]; cnt++;
                }
            }
            yuv_to_rgb(sy_acc / cnt, su / cnt, sv / cnt, hd, row + x * 3);
        }
    }
}

/* 8.0.131: 마지막 해독 그림을 V9990 용 32비트 RGB(0x00RRGGBB)로. dst 는 dw x dh 점. 화면 모양 4:3 에 맞춰 비율 유지, 남는 곳은 검은 띠.
 * 돌려줌: 1 = 그렸음, 0 = 아직 그림 없음 */
uint32_t mp4p_fill;                                                /* 8.0.148: 영상 둘레 띠(위아래·좌우)를 채울 색 0x00RRGGBB — V9990 으로 내보낼 때 MSX 테두리 색 (기본 0 = 검정) */
int mp4p_raw;                                                      /* 8.0.131: 1 = 화면 변환(줄이기·색 줄이기)을 건너뜀 — V9990 으로 내보낼 때 mp4p_rgb32 가 직접 */

/* 8.0.136: mp4p_rgb32 (8.0.159: C 판 삭제, 이것 하나만). 표본 모으기(줄마다 한 번 계산한 표본 줄을 써서 점마다 다시 계산하지 않음)는 C, YUV→RGB 는 src/v9_conv.asm.
 * 평균 나누기는 표본 수 1·2·4 일 때 이동, 나머지는 나눗셈 (C 판과 같은 값) */
int mp4p_rgb32(uint32_t *dst, int dw, int dh, int stride, int quick)
{
    if (!P.yuv || !P.cw || !P.ch || dw < 2 || dh < 2) return 0;
    uint32_t sw = P.cw, sh = P.ch, W = (uint32_t)dw, H = (uint32_t)dh, tw = W, th = H;
    if (3u * sw >= 4u * sh) th = (uint32_t)((uint64_t)H * 4u * sh / (3u * sw));
    else tw = (uint32_t)((uint64_t)W * 3u * sw / (4u * sh));
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (tw > W) tw = W;
    if (th > H) th = H;
    uint32_t ox = (W - tw) / 2, oy = (H - th) / 2;
    for (uint32_t y = 0; y < H; y++) { uint32_t *z = dst + (size_t)y * (size_t)stride; if (mp4p_fill) for (uint32_t x = 0; x < W; x++) z[x] = mp4p_fill; else memset(z, 0, (size_t)W * 4u); }
    int hd = sh >= 720;
    const uint8_t *Y = P.yuv, *U = Y + P.mbw * P.mbh, *V = U + (P.mbw / 2) * (P.mbh / 2);
    uint32_t cwid = P.mbw / 2, sx_step = (sw << 16) / tw, sy_step = (sh << 16) / th;
    int nx = (int)((sx_step + 0xFFFF) >> 16); if (nx < 1) nx = 1; if (nx > 3) nx = 3;
    int ny = (int)((sy_step + 0xFFFF) >> 16); if (ny < 1) ny = 1; if (ny > 3) ny = 3;
    if (quick) { if (nx > 2) nx = 2; if (ny > 2) ny = 2; }
    static uint16_t cx[2048][3];                                                              /* 가로 표본 위치 (줄마다 같음) */
    for (uint32_t x = 0; x < tw && x < 2048; x++) for (int i = 0; i < nx; i++) { uint32_t sx = P.cl + (((x * sx_step) + (sx_step * (uint32_t)(2 * i + 1)) / (uint32_t)(2 * nx)) >> 16); cx[x][i] = (uint16_t)(sx >= P.mbw ? P.mbw - 1 : sx); }
    static uint8_t yb[2048 + 8], ub[2048 + 8], vb[2048 + 8];
    static uint32_t tmp[2048 + 8];
    int cnt = nx * ny, sh_cnt = cnt == 1 ? 0 : cnt == 2 ? 1 : cnt == 4 ? 2 : -1;
    uint32_t n = tw < 2048 ? tw : 2048, n8 = (n + 7) & ~7u;
    for (uint32_t y = 0; y < th; y++) {
        const uint8_t *yr[3], *ur[3], *vr[3]; uint32_t sy0 = y * sy_step;
        for (int j = 0; j < ny; j++) {
            uint32_t sy = P.ct + ((sy0 + (sy_step * (uint32_t)(2 * j + 1)) / (uint32_t)(2 * ny)) >> 16); if (sy >= P.mbh) sy = P.mbh - 1;
            yr[j] = Y + (size_t)sy * P.mbw; ur[j] = U + (size_t)(sy >> 1) * cwid; vr[j] = V + (size_t)(sy >> 1) * cwid;
        }
        if (nx == 1 && ny == 1) {
            for (uint32_t x = 0; x < n; x++) { uint32_t sx = cx[x][0]; yb[x] = yr[0][sx]; ub[x] = ur[0][sx >> 1]; vb[x] = vr[0][sx >> 1]; }
        } else if (nx == 2 && ny == 2) {
            for (uint32_t x = 0; x < n; x++) {
                uint32_t a = cx[x][0], b = cx[x][1], a2 = a >> 1, b2 = b >> 1;
                yb[x] = (uint8_t)((yr[0][a] + yr[0][b] + yr[1][a] + yr[1][b]) >> 2);
                ub[x] = (uint8_t)((ur[0][a2] + ur[0][b2] + ur[1][a2] + ur[1][b2]) >> 2);
                vb[x] = (uint8_t)((vr[0][a2] + vr[0][b2] + vr[1][a2] + vr[1][b2]) >> 2);
            }
        } else {
            for (uint32_t x = 0; x < n; x++) {
                int sy_acc = 0, su = 0, sv = 0;
                for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) { uint32_t sx = cx[x][i]; sy_acc += yr[j][sx]; su += ur[j][sx >> 1]; sv += vr[j][sx >> 1]; }
                if (sh_cnt >= 0) { yb[x] = (uint8_t)(sy_acc >> sh_cnt); ub[x] = (uint8_t)(su >> sh_cnt); vb[x] = (uint8_t)(sv >> sh_cnt); }
                else { yb[x] = (uint8_t)(sy_acc / cnt); ub[x] = (uint8_t)(su / cnt); vb[x] = (uint8_t)(sv / cnt); }
            }
        }
        for (uint32_t x = n; x < n8; x++) yb[x] = ub[x] = vb[x] = 128;                       /* 8칸 맞춤 덤 */
        V9Cv a = { yb, ub, vb, tmp, (int)n8, hd };
        v9_yuv2rgb(&a);
        memcpy(dst + (size_t)(oy + y) * (size_t)stride + ox, tmp, (size_t)n * 4u);
    }
    return 1;
}

/* 팔레트 만들기: 3비트 RGB 512칸 히스토그램 → 중앙값 자르기 → k-평균 2번 */
static uint32_t hist[512];
static int dist3(int r1, int g1, int b1, int r2, int g2, int b2) { int dr = r1 - r2, dg = g1 - g2, db = b1 - b2; return 2 * dr * dr + 4 * dg * dg + 3 * db * db; }

static uint64_t pal_error(const uint8_t pal[][3], int n)
{
    uint64_t e = 0;
    for (int b = 0; b < 512; b++) {
        if (!hist[b]) continue;
        int r = b >> 6, g = (b >> 3) & 7, bl = b & 7, best = 0x7FFFFFFF;
        for (int k = 0; k < n; k++) { int d = dist3(r, g, bl, pal[k][0], pal[k][1], pal[k][2]); if (d < best) best = d; }
        e += (uint64_t)best * hist[b];
    }
    return e;
}
static void make_palette(uint8_t out[][3], int n)
{
    /* 중앙값 자르기: 상자 = 칸 목록 구간 */
    static uint16_t idx[512]; int m = 0;
    for (int b = 0; b < 512; b++) if (hist[b]) idx[m++] = (uint16_t)b;
    if (m == 0) { for (int k = 0; k < n; k++) out[k][0] = out[k][1] = out[k][2] = 0; return; }
    int lo[16], hi[16], nb = 1; lo[0] = 0; hi[0] = m;
    while (nb < n) {
        int bi = -1; uint64_t bv = 0;
        for (int k = 0; k < nb; k++) {                         /* 분산이 가장 큰 상자 */
            if (hi[k] - lo[k] < 2) continue;
            uint64_t w = 0, sr = 0, sg = 0, sb = 0, v = 0;
            for (int j = lo[k]; j < hi[k]; j++) { uint32_t c = hist[idx[j]]; int b = idx[j]; w += c; sr += (uint64_t)c * (uint64_t)(b >> 6); sg += (uint64_t)c * (uint64_t)((b >> 3) & 7); sb += (uint64_t)c * (uint64_t)(b & 7); }
            if (!w) continue;
            for (int j = lo[k]; j < hi[k]; j++) { uint32_t c = hist[idx[j]]; int b = idx[j]; v += (uint64_t)c * (uint64_t)dist3(b >> 6, (b >> 3) & 7, b & 7, (int)(sr / w), (int)(sg / w), (int)(sb / w)); }
            if (bi < 0 || v > bv) { bi = k; bv = v; }
        }
        if (bi < 0) break;
        /* 가장 넓게 퍼진 축으로 정렬해 가중치 중앙에서 자름 */
        int rmin = 7, rmax = 0, gmin = 7, gmax = 0, bmin = 7, bmax = 0;
        for (int j = lo[bi]; j < hi[bi]; j++) { int b = idx[j], r = b >> 6, g = (b >> 3) & 7, bl = b & 7; if (r < rmin) rmin = r; if (r > rmax) rmax = r; if (g < gmin) gmin = g; if (g > gmax) gmax = g; if (bl < bmin) bmin = bl; if (bl > bmax) bmax = bl; }
        int ax = (rmax - rmin) * 2 >= (gmax - gmin) * 3 ? ((rmax - rmin) * 2 >= (bmax - bmin) * 2 ? 0 : 2) : ((gmax - gmin) * 3 >= (bmax - bmin) * 2 ? 1 : 2);
        for (int a = lo[bi] + 1; a < hi[bi]; a++) {            /* 삽입 정렬 (칸이 많아야 512) */
            uint16_t t = idx[a]; int j = a - 1;
            #define KEY(b) (ax == 0 ? ((b) >> 6) : ax == 1 ? (((b) >> 3) & 7) : ((b) & 7))
            while (j >= lo[bi] && KEY(idx[j]) > KEY(t)) { idx[j + 1] = idx[j]; j--; }
            idx[j + 1] = t;
            #undef KEY
        }
        uint64_t tot = 0, acc = 0; for (int j = lo[bi]; j < hi[bi]; j++) tot += hist[idx[j]];
        int cut = lo[bi] + 1;
        for (int j = lo[bi]; j < hi[bi] - 1; j++) { acc += hist[idx[j]]; cut = j + 1; if (acc * 2 >= tot) break; }
        if (cut <= lo[bi]) cut = lo[bi] + 1;
        if (cut >= hi[bi]) cut = hi[bi] - 1;
        for (int k = nb; k > bi + 1; k--) { lo[k] = lo[k - 1]; hi[k] = hi[k - 1]; }
        lo[bi + 1] = cut; hi[bi + 1] = hi[bi]; hi[bi] = cut; nb++;
    }
    for (int k = 0; k < n; k++) {
        if (k >= nb) { out[k][0] = out[k % nb][0]; out[k][1] = out[k % nb][1]; out[k][2] = out[k % nb][2]; continue; }
        uint64_t w = 0, sr = 0, sg = 0, sb = 0;
        for (int j = lo[k]; j < hi[k]; j++) { uint32_t c = hist[idx[j]]; int b = idx[j]; w += c; sr += (uint64_t)c * (uint64_t)(b >> 6); sg += (uint64_t)c * (uint64_t)((b >> 3) & 7); sb += (uint64_t)c * (uint64_t)(b & 7); }
        out[k][0] = (uint8_t)((sr + w / 2) / w); out[k][1] = (uint8_t)((sg + w / 2) / w); out[k][2] = (uint8_t)((sb + w / 2) / w);
    }
    for (int it = 0; it < 2; it++) {                           /* k-평균 */
        uint64_t w[16] = { 0 }, sr[16] = { 0 }, sg[16] = { 0 }, sb[16] = { 0 };
        for (int b = 0; b < 512; b++) {
            if (!hist[b]) continue;
            int r = b >> 6, g = (b >> 3) & 7, bl = b & 7, best = 0x7FFFFFFF, bk = 0;
            for (int k = 0; k < n; k++) { int d = dist3(r, g, bl, out[k][0], out[k][1], out[k][2]); if (d < best) { best = d; bk = k; } }
            w[bk] += hist[b]; sr[bk] += (uint64_t)hist[b] * (uint64_t)r; sg[bk] += (uint64_t)hist[b] * (uint64_t)g; sb[bk] += (uint64_t)hist[b] * (uint64_t)bl;
        }
        for (int k = 0; k < n; k++) if (w[k]) { out[k][0] = (uint8_t)((sr[k] + w[k] / 2) / w[k]); out[k][1] = (uint8_t)((sg[k] + w[k] / 2) / w[k]); out[k][2] = (uint8_t)((sb[k] + w[k] / 2) / w[k]); }
    }
}

static uint8_t nn_lut[32768];                                  /* 5비트 RGB → 가장 가까운 팔레트 칸 (0xFF = 아직) */
static int nn_find(int r, int g, int b)                         /* 0..255 입력 */
{
    int key = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
    uint8_t v = nn_lut[key]; if (v != 0xFF) return v;
    int rr = (r & 0xF8) | 4, gg = (g & 0xF8) | 4, bb = (b & 0xF8) | 4, best = 0x7FFFFFFF, bk = 0;
    for (int k = 0; k < P.npal; k++) {
        int dr = rr - lv8[P.pal[k][0]], dg = gg - lv8[P.pal[k][1]], db = bb - lv8[P.pal[k][2]];
        int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db; if (d < best) { best = d; bk = k; }
    }
    nn_lut[key] = (uint8_t)bk; return bk;
}

static int16_t er[2][MAXW + 2][3];
static void quantize(void)
{
    int W = P.pic.w, H = P.pic.h, scr = P.scr;
    memset(er, 0, sizeof er);
    uint8_t *img = s_img; memset(img, 0, sizeof s_img);
    int cur = 0;
    for (int y = 0; y < H; y++) {
        int nxt = cur ^ 1; memset(er[nxt], 0, sizeof er[nxt]);
        const uint8_t *px = s_rgb + (size_t)y * (size_t)W * 3;
        uint8_t *dst = img + (size_t)y * (size_t)P.pic.stride;
        for (int x = 0; x < W; x++) {
            int r = clamp8(px[x * 3] + er[cur][x + 1][0] / 16), g = clamp8(px[x * 3 + 1] + er[cur][x + 1][1] / 16), b = clamp8(px[x * 3 + 2] + er[cur][x + 1][2] / 16);
            int qr, qg, qb, code;
            if (scr == 8) {
                int gr = g >> 5, rr = r >> 5, bb = b >> 6; code = gr << 5 | rr << 2 | bb;
                qr = lv8[rr]; qg = lv8[gr]; qb = bb * 85;
                dst[x] = (uint8_t)code;
            } else {
                code = nn_find(r, g, b); qr = lv8[P.pal[code][0]]; qg = lv8[P.pal[code][1]]; qb = lv8[P.pal[code][2]];
                if (scr == 5) { if (x & 1) dst[x >> 1] |= (uint8_t)code; else dst[x >> 1] = (uint8_t)(code << 4); }
                else if (scr == 7) { if (x & 1) dst[x >> 1] |= (uint8_t)code; else dst[x >> 1] = (uint8_t)(code << 4); }
                else { dst[x >> 2] |= (uint8_t)(code << (6 - 2 * (x & 3))); }
            }
            int e0 = (r - qr) * P.dither / 100, e1 = (g - qg) * P.dither / 100, e2 = (b - qb) * P.dither / 100;
            /* Floyd-Steinberg (7 3 5 1)/16 */
            er[cur][x + 2][0] += (int16_t)(e0 * 7); er[cur][x + 2][1] += (int16_t)(e1 * 7); er[cur][x + 2][2] += (int16_t)(e2 * 7);
            er[nxt][x][0] += (int16_t)(e0 * 3); er[nxt][x][1] += (int16_t)(e1 * 3); er[nxt][x][2] += (int16_t)(e2 * 3);
            er[nxt][x + 1][0] += (int16_t)(e0 * 5); er[nxt][x + 1][1] += (int16_t)(e1 * 5); er[nxt][x + 1][2] += (int16_t)(e2 * 5);
            er[nxt][x + 2][0] += (int16_t)e0; er[nxt][x + 2][1] += (int16_t)e1; er[nxt][x + 2][2] += (int16_t)e2;
        }
        cur = nxt;
    }
}

/* SCREEN 12 (YJK): 점마다 Y(5비트, 상위 5비트), 4점 묶음이 K(앞 두 바이트 하위 3비트씩)·J(뒤 두 바이트) 6비트 부호를 공유.
   복원 R=Y+J, G=Y+K, B=(5Y-2J-K)/4 → 인코딩 Y=(4B+2R+G)/8. 오차 확산은 밝기(Y)에만, J·K 는 양자화된 Y 기준 평균(8.0.92) */
static int16_t ey[2][MAXW + 2];
static void quantize_yjk(void)
{
    int W = P.pic.w, H = P.pic.h;
    memset(ey, 0, sizeof ey);
    memset(s_img, 0, sizeof s_img);
    int cur = 0;
    for (int y = 0; y < H; y++) {
        int nxt = cur ^ 1; memset(ey[nxt], 0, sizeof ey[nxt]);
        const uint8_t *px = s_rgb + (size_t)y * (size_t)W * 3;
        uint8_t *dst = s_img + (size_t)y * (size_t)P.pic.stride;
        for (int x0 = 0; x0 < W; x0 += 4) {
            int yq[4], sj = 0, sk = 0;
            for (int i = 0; i < 4; i++) {
                int x = x0 + i, r = px[x * 3], g = px[x * 3 + 1], b = px[x * 3 + 2];
                int yn = 4 * b + 2 * r + g + ey[cur][x + 1] / 16;               /* 0..1785 (= 8 × Y8) */
                int y5 = (yn * 31 + 1020) / 2040; y5 = y5 < 0 ? 0 : y5 > 31 ? 31 : y5;
                int e = (yn - (y5 * 2040 + 15) / 31) * P.dither / 100;
                ey[cur][x + 2] += (int16_t)(e * 7); ey[nxt][x] += (int16_t)(e * 3); ey[nxt][x + 1] += (int16_t)(e * 5); ey[nxt][x + 2] += (int16_t)e;
                yq[i] = y5;
                sj += (r * 31 + 127) / 255 - y5; sk += (g * 31 + 127) / 255 - y5;
            }
            int j = (sj + (sj >= 0 ? 2 : -2)) / 4, k = (sk + (sk >= 0 ? 2 : -2)) / 4;
            j = j < -32 ? -32 : j > 31 ? 31 : j; k = k < -32 ? -32 : k > 31 ? 31 : k;
            j &= 63; k &= 63;
            dst[x0] = (uint8_t)(yq[0] << 3 | (k & 7)); dst[x0 + 1] = (uint8_t)(yq[1] << 3 | (k >> 3));
            dst[x0 + 2] = (uint8_t)(yq[2] << 3 | (j & 7)); dst[x0 + 3] = (uint8_t)(yq[3] << 3 | (j >> 3));
        }
        cur = nxt;
    }
}

static void make_pic(void)
{
    P.pic.pal_changed = 0;
    if (P.scr == 12) { quantize_yjk(); return; }
    if (P.scr != 8) {
        int n = P.npal;
        memset(hist, 0, sizeof hist);
        int W = P.pic.w, H = P.pic.h;
        for (int y = 0; y < H; y += 1) {                       /* 모든 점 (512 폭이면 2점 건너뛰어 시간 절약) */
            const uint8_t *px = s_rgb + (size_t)y * (size_t)W * 3;
            int stepx = W > 256 ? 2 : 1;
            for (int x = 0; x < W; x += stepx) hist[((px[x * 3] * 7 + 127) / 255) << 6 | ((px[x * 3 + 1] * 7 + 127) / 255) << 3 | ((px[x * 3 + 2] * 7 + 127) / 255)]++;
        }
        uint8_t np[16][3]; make_palette(np, n);
        if (!P.pal_valid) { memcpy(P.pal, np, sizeof P.pal); P.pal_valid = 1; P.pic.pal_changed = 1; }
        else {
            uint64_t eo = pal_error(P.pal, n), en = pal_error(np, n);
            if (en * 100 < eo * 85) { memcpy(P.pal, np, sizeof P.pal); P.pic.pal_changed = 1; }
        }
        if (P.pic.pal_changed) memset(nn_lut, 0xFF, sizeof nn_lut);
        memcpy(P.pic.pal, P.pal, sizeof P.pic.pal);
    }
    quantize();
}

/* ---- 8.0.123 빠른 화면 처리 (ini Mp4Fast=1, 기본 0 = 위 기존 코드) + 시간 진단 ---- */
int mp4p_skip_nonref;                                          /* 8.0.139 ini Mp4SkipNonRef (kernel/fe.c 옵션 표) */
int mp4p_fast;                                                 /* ini Mp4Fast (kernel/fe.c 옵션 표) */
#include "mp4play_fast.inc"


/* ================= 8.0.93 소리 (AAC-LC) ================= */

static int build_atables(void)
{
    const Mp4Info *m = &P.mi; uint8_t h[16];
    if (!m->ta_stsz || !m->ta_stsc || (!m->ta_stco && !m->ta_co64) || !m->a_timescale) return -1;
    if (rdb(m->ta_stsz, h, 12)) return -1;
    uint32_t fixed = be32(h + 4), n = be32(h + 8);
    if (n == 0 || n > a_cap) return -1;
    Seq q;
    if (fixed) { if (fixed > MP4A_MAXSMP) return -1; for (uint32_t i = 0; i < n; i++) a_size[i] = fixed; }
    else { seq_init(&q, m->ta_stsz + 12, n, 4); for (uint32_t i = 0; i < n; i++) { const uint8_t *e = seq_next(&q); if (!e) return -1; a_size[i] = be32(e); } }
    uint32_t nchunk, nruns; int is64 = m->ta_co64 != 0; uint32_t cbase = is64 ? m->ta_co64 : m->ta_stco;
    if (rdb(cbase + 4, h, 4)) return -1; nchunk = be32(h);
    if (rdb(m->ta_stsc + 4, h, 4)) return -1; nruns = be32(h);
    if (nchunk == 0 || nruns == 0) return -1;
    Seq cq, rq; seq_init(&cq, cbase + 8, nchunk, is64 ? 8 : 4); seq_init(&rq, m->ta_stsc + 8, nruns, 12);
    const uint8_t *r = seq_next(&rq); if (!r) return -1;
    uint32_t spc = be32(r + 4), nfirst = 0xFFFFFFFFu, nspc = 0, s = 0;
    if (rq.idx < rq.cnt) { const uint8_t *r2 = seq_next(&rq); if (!r2) return -1; nfirst = be32(r2); nspc = be32(r2 + 4); }
    for (uint32_t c = 1; c <= nchunk && s < n; c++) {
        while (c >= nfirst) {
            spc = nspc;
            if (rq.idx < rq.cnt) { const uint8_t *r3 = seq_next(&rq); if (!r3) return -1; nfirst = be32(r3); nspc = be32(r3 + 4); }
            else nfirst = 0xFFFFFFFFu;
        }
        const uint8_t *e = seq_next(&cq); if (!e) return -1;
        uint32_t o = is64 ? (be32(e) ? 0xFFFFFFFFu : be32(e + 4)) : be32(e);
        if (o == 0xFFFFFFFFu) return -1;
        for (uint32_t k = 0; k < spc && s < n; k++, s++) { a_off[s] = o; o += a_size[s]; }
    }
    if (s < n) return -1;
    A.n = n;
    return 0;
}


/* ================= 8.0.153 MOV 의 PCM 소리 (sowt · twos · raw · in24 · in32) ================= */
/* 표(stsz)의 "한 샘플 크기"가 곧 한 프레임(채널 묶음) 바이트이고 stsc 의 "묶음당 샘플 수"는 프레임 수다. 묶음(chunk)마다 16384 바이트 이하 조각으로 잘라 조각 표를 만든다. */
static uint32_t a_cum_st[MP4A_MAXS];                          /* 조각마다 그 앞까지의 프레임 수 */
static int16_t pcm_out[MP4A_MAXSMP];
static int pcm_kind(uint32_t fourcc)
{
    switch (fourcc) {
    case 0x736F7774u: return 1;     /* 'sowt' 16비트 리틀엔디안 */
    case 0x74776F73u: return 2;     /* 'twos' 16비트 빅엔디안 */
    case 0x72617720u: return 3;     /* 'raw ' 8비트 부호 없음 */
    case 0x696E3234u: return 4;     /* 'in24' 24비트 빅엔디안 */
    case 0x696E3332u: return 5;     /* 'in32' 32비트 빅엔디안 */
    default: return 0;
    }
}
static int pcm_bytes_per_sample(int k) { return k == 3 ? 1 : k == 4 ? 3 : k == 5 ? 4 : 2; }
static int build_pcm_tables(void)                             /* 성공 0. 조각 표 a_off / a_size / a_cum 을 만들고 A.n 에 조각 수 */
{
    const Mp4Info *m = &P.mi; uint8_t h[16];
    if (!m->ta_stsz || !m->ta_stsc || (!m->ta_stco && !m->ta_co64)) return -1;
    if (rdb(m->ta_stsz, h, 12)) return -1;
    uint32_t fsz = be32(h + 4); if (fsz != A.bf) return -1;   /* 한 샘플 = 한 프레임인 평범한 표만 */
    uint32_t nchunk, nruns; int is64 = m->ta_co64 != 0; uint32_t cbase = is64 ? m->ta_co64 : m->ta_stco;
    if (rdb(cbase + 4, h, 4)) return -1; nchunk = be32(h);
    if (rdb(m->ta_stsc + 4, h, 4)) return -1; nruns = be32(h);
    if (nchunk == 0 || nruns == 0) return -1;
    Seq cq, rq; seq_init(&cq, cbase + 8, nchunk, is64 ? 8 : 4); seq_init(&rq, m->ta_stsc + 8, nruns, 12);
    const uint8_t *r = seq_next(&rq); if (!r) return -1;
    uint32_t spc = be32(r + 4), nfirst = 0xFFFFFFFFu, nspc = 0, n = 0, frames = 0, maxf = MP4A_MAXSMP / A.bf;
    if (rq.idx < rq.cnt) { const uint8_t *r2 = seq_next(&rq); if (!r2) return -1; nfirst = be32(r2); nspc = be32(r2 + 4); }
    for (uint32_t c = 1; c <= nchunk; c++) {
        while (c >= nfirst) {
            spc = nspc;
            if (rq.idx < rq.cnt) { const uint8_t *r3 = seq_next(&rq); if (!r3) return -1; nfirst = be32(r3); nspc = be32(r3 + 4); }
            else nfirst = 0xFFFFFFFFu;
        }
        const uint8_t *e = seq_next(&cq); if (!e) return -1;
        uint32_t o = is64 ? (be32(e) ? 0xFFFFFFFFu : be32(e + 4)) : be32(e);
        if (o == 0xFFFFFFFFu) return -1;
        for (uint32_t left = spc; left > 0; ) {
            uint32_t f = left < maxf ? left : maxf;
            if (n >= a_cap || n >= MP4A_MAXS) return -1;
            a_off[n] = o; a_size[n] = f * A.bf; a_cum_st[n] = frames; n++;
            o += f * A.bf; frames += f; left -= f;
        }
    }
    if (n == 0) return -1;
    A.n = n; A.frames = 0;
    return 0;
}
static int pcm_open(void)
{
    int k = pcm_kind(P.mi.acodec);
    if (!k || P.mi.channels < 1 || P.mi.channels > 2 || P.mi.rate < 8000 || P.mi.rate > 65535) return 0;
    A.pk = k; A.ch = P.mi.channels; A.rate = P.mi.rate; A.bf = (uint32_t)(A.ch * pcm_bytes_per_sample(k));
    if (build_pcm_tables() < 0) { memset(&A, 0, sizeof A); return 0; }
    A.open = 1; A.cur = 0;
    return 1;
}
static int pcm_next(const int16_t **pcm, int *frames, int *ch, int *hz)
{
    while (A.cur < A.n) {
        uint32_t i = A.cur++, sz = a_size[i];
        if (rdb(a_off[i], a_buf, sz)) continue;
        uint32_t ns = sz / (uint32_t)pcm_bytes_per_sample(A.pk);
        for (uint32_t j = 0; j < ns; j++) {
            switch (A.pk) {
            case 1: pcm_out[j] = (int16_t)(a_buf[2 * j] | a_buf[2 * j + 1] << 8); break;
            case 2: pcm_out[j] = (int16_t)(a_buf[2 * j] << 8 | a_buf[2 * j + 1]); break;
            case 3: pcm_out[j] = (int16_t)((a_buf[j] - 128) << 8); break;
            case 4: pcm_out[j] = (int16_t)(a_buf[3 * j] << 8 | a_buf[3 * j + 1]); break;
            default: pcm_out[j] = (int16_t)(a_buf[4 * j] << 8 | a_buf[4 * j + 1]); break;
            }
        }
        A.frames++;
        *pcm = pcm_out; *frames = (int)(sz / A.bf); *ch = A.ch; *hz = (int)A.rate;
        return 1;
    }
    return 0;
}
static uint32_t pcm_seek(uint32_t t_ms)
{
    uint64_t want = (uint64_t)t_ms * A.rate / 1000u;
    uint32_t lo = 0, hi = A.n ? A.n - 1 : 0;
    while (lo < hi) { uint32_t mid = (lo + hi + 1) / 2; if (a_cum_st[mid] <= want) lo = mid; else hi = mid - 1; }
    A.cur = lo;
    return (uint32_t)((uint64_t)a_cum_st[lo] * 1000u / A.rate);
}

/* ================= 8.0.153 ALAC (Apple Lossless) 소리: MP4(.m4a 속 소리) · MOV. 패킷 하나 = frameLength 프레임, 해독은 third_party/alac ================= */
static int16_t alac_out[2 * 16384];
static int alac_open(void)
{
    const Mp4Info *m = &P.mi; uint8_t ck[24];
    if (m->ta_asc_len != 24 || rdb(m->ta_asc, ck, 24)) return 0;
    uint32_t fl = 0, hz = 0; int ch = 0, bits = 0;
    if (!alacdec_open(ck, 24, &fl, &hz, &ch, &bits)) return 0;
    if (build_atables() < 0) { alacdec_close(); return 0; }
    A.alac = 1; A.ch = ch; A.rate = hz; A.ts = m->a_timescale; A.delta = (uint32_t)((uint64_t)fl * A.ts / hz); if (!A.delta) A.delta = fl;
    A.open = 1; A.cur = 0; A.frames = 0;
    return 1;
}
static int alac_next(const int16_t **pcm, int *frames, int *ch, int *hz)
{
    for (int tries = 0; tries < 8 && A.cur < A.n; tries++) {
        uint32_t i = A.cur++, sz = a_size[i];
        if (sz == 0 || sz > MP4A_MAXSMP || rdb(a_off[i], a_buf, sz)) continue;
        int n = alacdec_decode(a_buf, sz, alac_out);
        if (n <= 0) continue;
        A.frames++;
        *pcm = alac_out; *frames = n; *ch = A.ch; *hz = (int)A.rate;
        return 1;
    }
    return 0;
}

int mp4p_audio_open(void)
{
    memset(&A, 0, sizeof A);
    if (P.open && P.mj) return avi_audio_open();                 /* 8.0.121 */
    if (P.open && P.mi.has_audio && pcm_kind(P.mi.acodec)) return pcm_open();   /* 8.0.153 */
    if (P.open && P.mi.has_audio && P.mi.acodec == 0x616C6163u /* alac */) return alac_open();   /* 8.0.153 */
    if (!P.open || !P.mi.has_audio || P.mi.acodec != 0x6D703461 /* mp4a */ || P.mi.aac_obj != 0x40 /* MPEG-4 AAC */ || !P.mi.ta_asc || P.mi.ta_asc_len < 2) return 0;
    uint8_t asc[32]; if (rdb(P.mi.ta_asc, asc, P.mi.ta_asc_len)) return 0;
    int aot = asc[0] >> 3;
    if (aot != 2 && aot != 5 && aot != 29) return 0;           /* AudioObjectType 2 = AAC-LC, 5 = HE-AAC(SBR), 29 = HE-AACv2(SBR+PS). 8.0.97 */
    static const uint32_t sfi_hz[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    int sfi = ((asc[0] & 7) << 1) | (asc[1] >> 7);              /* 코어(AAC-LC 쪽) 샘플율 번호 */
    if (build_atables() < 0) { memset(&A, 0, sizeof A); return 0; }
    A.h = NeAACDecOpen();
    if (!A.h) { memset(&A, 0, sizeof A); return 0; }
    NeAACDecConfigurationPtr cf = NeAACDecGetCurrentConfiguration(A.h);
    cf->outputFormat = FAAD_FMT_16BIT; cf->downMatrix = 1; cf->dontUpSampleImplicitSBR = 0;   /* 8.0.97: SBR 이 있으면 늘린 샘플율로 (암시적 HE-AAC 도) */
    NeAACDecSetConfiguration(A.h, cf);
    unsigned long hz = 0; unsigned char ch = 0;
    if (NeAACDecInit2(A.h, asc, P.mi.ta_asc_len, &hz, &ch) < 0 || hz < 8000 || hz > 96000 || ch < 1 || ch > 8) { NeAACDecClose(A.h); memset(&A, 0, sizeof A); return 0; }
    A.rate = (uint32_t)hz; A.ch = ch; A.ts = P.mi.a_timescale; { uint32_t core = sfi >= 0 && sfi < 13 ? sfi_hz[sfi] : A.rate; A.delta = core ? (uint32_t)(1024ull * A.ts / core) : 1024u; if (!A.delta) A.delta = 1024u; }   /* 한 프레임 길이 (트랙 시간 단위): 코어 샘플율 기준 1024 점 */
    A.open = 1; A.cur = 0; A.prime = 0;
    return 1;
}

int mp4p_audio_next(const int16_t **pcm, int *frames, int *ch, int *hz)
{
    if (!A.open) return 0;
    if (A.pcm) return avi_audio_next(pcm, frames, ch, hz);     /* 8.0.121 */
    if (A.pk) return pcm_next(pcm, frames, ch, hz);            /* 8.0.153 */
    if (A.alac) return alac_next(pcm, frames, ch, hz);         /* 8.0.153 */
    for (int tries = 0; tries < 8 && A.cur < A.n; tries++) {
        uint32_t i = A.cur++, sz = a_size[i];
        if (sz == 0 || sz > MP4A_MAXSMP || rdb(a_off[i], a_buf, sz)) continue;
        NeAACDecFrameInfo fi; memset(&fi, 0, sizeof fi);
        void *out = NeAACDecDecode(A.h, &fi, a_buf, sz);
        if (!out || fi.error || fi.samples == 0 || fi.channels == 0) continue;
        if (A.prime) { A.prime = 0; continue; }                 /* 이동 직후 첫 프레임은 겹침 상태를 채우는 용도라 버림 */
        A.frames++;
        *pcm = (const int16_t *)out; *frames = (int)(fi.samples / fi.channels); *ch = fi.channels; *hz = (int)fi.samplerate;
        return 1;
    }
    return 0;
}

uint32_t mp4p_audio_seek(uint32_t t_ms)
{
    if (!A.open) return 0;
    if (A.pcm) return avi_audio_seek(t_ms);                      /* 8.0.121 */
    if (A.pk) return pcm_seek(t_ms);                             /* 8.0.153 */
    if (A.alac) { uint64_t i = (uint64_t)t_ms * A.ts / 1000u / A.delta; if (i >= A.n) i = A.n ? A.n - 1 : 0; A.cur = (uint32_t)i; return (uint32_t)(i * A.delta * 1000u / A.ts); }   /* 8.0.153 */
    uint64_t i = (uint64_t)t_ms * A.ts / 1000u / A.delta;
    if (i >= A.n) i = A.n ? A.n - 1 : 0;
    if (i > 0) { i--; A.prime = 1; }                           /* 한 프레임 앞에서 시작해 첫 것을 버림 */
    A.cur = (uint32_t)i;
    return (uint32_t)((uint64_t)(i + (A.prime ? 1 : 0)) * A.delta * 1000u / A.ts);
}
uint32_t mp4p_audio_frames(void) { return A.frames; }

/* ---- 열기·닫기 ---- */
static int pic_dims(int scr, int lines, int *w, int *h, int *stride, int *npal)
{
    *h = lines == 212 ? 212 : 192;
    switch (scr) {
    case 5: *w = 256; *stride = 128; *npal = 16; return 1;
    case 6: *w = 512; *stride = 128; *npal = 4; return 1;
    case 7: *w = 512; *stride = 256; *npal = 16; return 1;
    case 8: *w = 256; *stride = 256; *npal = 0; return 1;
    case 12: *w = 256; *stride = 256; *npal = 0; return 1;
    default: return 0;
    }
}

void mp4p_close(void)
{
    if (P.dec) h264bsdShutdown(P.dec);
    if (P.oh) oh264_close();
    if (A.alac) alacdec_close();
    memset(&A, 0, sizeof A);
    memset(&P, 0, sizeof P);
    arena_used = 0;
    tabs_reset();                                             /* 8.0.122 */
}

int mp4p_open(Mp4Read rd, void *ctx, uint32_t fsize, int scr, int lines, int fit, int dither, int maxfps)
{
    mp4p_close();
    P.rd = rd; P.ctx = ctx; P.fsize = fsize;
    int w, h, st, np;
    if (!pic_dims(scr, lines, &w, &h, &st, &np)) return MP4E_SCREEN;
    int e = mp4_probe(rd, ctx, fsize, &P.mi);
    if (e != MP4E_NONE) return e;
    if (P.mi.avi) return avi_open(rd, ctx, fsize, scr, w, h, st, np, fit, dither, maxfps);   /* 8.0.121 */
    if (P.mi.vcodec != 0x61766331 && P.mi.vcodec != 0x61766333) return MP4E_VCODEC;
    if (!mp4lim_size_ok(P.mi.width, P.mi.height)) return pwhy(2);
    if (!P.mi.tb_avcc || P.mi.tb_avcc_len < 8) return pwhy(3);
    e = build_tables(); DBG("tables %d n=%u\n", e, P.n); if (e != MP4E_NONE) { memset(&P, 0, sizeof P); return e; }
    P.scr = scr; P.lines = h; P.fit = fit; P.dither = dither < 0 ? 0 : dither > 100 ? 100 : dither; P.maxfps = maxfps < 0 ? 0 : maxfps;
    P.pic.scr = scr; P.pic.w = w; P.pic.h = h; P.pic.stride = st; P.pic.npal = np; P.npal = np;
    P.pic.bytes = (uint32_t)(st * h);
    /* avcC → SPS·PPS */
    uint8_t av[1024]; uint32_t al = P.mi.tb_avcc_len < sizeof av ? P.mi.tb_avcc_len : (uint32_t)sizeof av;
    if (rdb(P.mi.tb_avcc, av, al) || av[0] != 1) { memset(&P, 0, sizeof P); return pwhy(4); }
    { uint8_t ah[12]; if (P.mi.has_audio && P.mi.ta_stsz && !rdb(P.mi.ta_stsz, ah, 12)) { uint32_t an = be32(ah + 8); if (an > a_cap && an <= 4000000u && !pcm_kind(P.mi.acodec) && tabs_big_a(an)) { /* 못 잡으면 소리 없이 재생 */ } } }   /* 8.0.122 긴 영상의 소리 표 */
    P.lenSize = (av[4] & 3) + 1;
    P.oh = av[1] != 66;                                         /* Baseline 은 가벼운 h264bsd, Main/High 는 OpenH264 */
    if (P.oh) {
        P.ybuf_n = MP4LIM_PICTURE_BYTES_MAX; P.ybuf = (uint8_t *)mp4h_alloc(P.ybuf_n);
        size_t rest = ARENA_SIZE - arena_used - (256u << 10);
        if (!P.ybuf || oh264_open(rest) != 0) { P.oh = 0; arena_used = 0; memset(&P, 0, sizeof P); return pwhy(5); }
    } else {
        P.dec = h264bsdAlloc();
        if (!P.dec || h264bsdInit(P.dec, 1) != 0) { P.dec = 0; arena_used = 0; memset(&P, 0, sizeof P); return pwhy(6); }
    }
    uint32_t pos = 6; int nsps = av[5] & 31, ok = 1;
    for (int k = 0; k < nsps && ok; k++) { if (pos + 2 > al) { ok = 0; break; } uint32_t l = (uint32_t)av[pos] << 8 | av[pos + 1]; pos += 2; if (pos + l > al) { ok = 0; break; } if (feed_nal(av + pos, l) < 0) ok = 0; pos += l; }
    if (ok && pos < al) { int npps = av[pos++]; for (int k = 0; k < npps && ok; k++) { if (pos + 2 > al) { ok = 0; break; } uint32_t l = (uint32_t)av[pos] << 8 | av[pos + 1]; pos += 2; if (pos + l > al) { ok = 0; break; } if (feed_nal(av + pos, l) < 0) ok = 0; pos += l; } }
    DBG("sps/pps ok=%d lenSize=%d\n", ok, P.lenSize);
    if (!ok) { mp4p_close(); return pwhy(7); }
    P.open = 1; P.cur = 0;
    /* 첫 키프레임을 해독해 보아 이 프로파일(CABAC 등)을 거른다 */
    int r = decode_sample(0); DBG("decode0 r=%d have=%d size=%u\n", r, P.have_pic, s_size[0]);
    if (P.oh) {                                                  /* 순서 바꾸기 때문에 첫 그림이 늦게 나올 수 있음: 몇 샘플 더 해독해 크기를 알아낸 뒤 처음으로 되돌림 */
        for (uint32_t k = 1; k < P.n && k < 16 && !P.have_pic; k++) decode_sample(k);
        if (!P.have_pic) { Oh264Pic op; if (oh264_flush(&op) > 0) oh_take(&op); }
        if (!P.have_pic) { mp4p_close(); return pwhy(8); }
        oh_drain(); r = 1; P.lag_ms = 0;
    } else if (r <= 0 || !P.have_pic) { mp4p_close(); return pwhy(9); }
    if (!mp4lim_size_ok(P.cw, P.ch)) { mp4p_close(); return pwhy(10); }
    P.cur = P.oh ? 0 : 1; if (P.oh) P.have_pic = 0; P.mi.width = (uint16_t)P.cw; P.mi.height = (uint16_t)P.ch;
    P.dropped = 0; P.skipped = 0; P.later_in_window = 0; P.shown_ms = 0; P.ended = 0;
    return MP4E_NONE;
}

uint32_t mp4p_len_ms(void) { return P.dur_ms; }
uint32_t mp4p_fps100(void) { return P.mi.fps100; }
const uint8_t *mp4p_image(void) { return s_img; }
const Mp4Pic *mp4p_pic(void) { return &P.pic; }
int mp4p_ended(void) { return P.ended; }
uint32_t mp4p_dropped(void) { return P.dropped; }
uint32_t mp4p_skipped(void) { return P.skipped; }

uint32_t mp4p_seek(uint32_t t_ms)
{
    if (!P.open) return 0;
    uint32_t i = 0;
    for (uint32_t k = 0; k < P.n && s_dts[k] <= t_ms; k++) if (is_key(k)) i = k;
    P.cur = i; P.have_pic = 0; P.ended = 0; if (P.oh) oh_drain();
    return s_dts[i];
}

int mp4p_frame(uint32_t t_ms, uint32_t budget_ms, uint32_t (*ms)(void), Mp4Pic *pic)
{
    if (!P.open) return -1;
    uint32_t t0 = ms ? ms() : 0; int decoded = 0;
    /* 너무 뒤처지면(1초 넘게) 가장 가까운 키프레임으로 건너뜀 */
    if (P.cur < P.n && t_ms > s_dts[P.cur] + 1000) {
        uint32_t k = P.cur;
        for (uint32_t j = P.cur; j < P.n && s_dts[j] <= t_ms; j++) if (is_key(j)) k = j;
        if (k > P.cur) { P.dropped += k - P.cur; P.cur = k; if (P.oh) oh_drain(); }
    }
    while (P.cur < P.n && s_dts[P.cur] <= t_ms + P.lag_ms) {      /* 순서 바꾸기로 늦게 나오는 만큼 미리 해독 */
        P.later_in_window = P.cur + 1 < P.n && s_dts[P.cur + 1] <= t_ms + P.lag_ms;   /* 8.0.139: 이 샘플이 이번 호출의 마지막이 아니면 건너뛸 수 있음 */
        int r = decode_sample(P.cur);
        P.cur++;
        if (r < 0) { P.ended = 1; break; }
        if (r > 0) decoded = 1;
        if (budget_ms && ms && (uint32_t)(ms() - t0) >= budget_ms) break;
    }
    if (P.cur >= P.n && P.oh) { Oh264Pic op; while (oh264_flush(&op) > 0) oh_take(&op); decoded = 1; }   /* 끝: 해독기에 남은 그림 */
    if (P.cur >= P.n) P.ended = 1;
    if (P.have_pic && P.maxfps && P.shown_ms && t_ms < P.shown_ms + 1000u / (uint32_t)P.maxfps) return 0;   /* 최대 fps 제한: 해독은 하되 화면 변환은 미룸 */
    if (P.have_pic) {
        if (mp4p_raw) { P.have_pic = 0; P.shown_ms = t_ms; if (pic) *pic = P.pic; return 1; }   /* 8.0.131: V9990 으로 내보낼 때는 변환 없이 (mp4p_rgb32 가 해독 그림에서 직접) */
        if (mp4p_fast) fast_scale(); else scale_to_rgb();
        if (mp4p_fast) fast_make_pic(); else make_pic();
        P.have_pic = 0; P.shown_ms = t_ms;
        if (pic) *pic = P.pic;
        return 1;
    }
    return P.ended && P.cur >= P.n && !decoded ? -1 : 0;
}
