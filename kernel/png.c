/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * png.c - 아이콘·배지용 PNG 해독 (최대 512 x 128, 인터레이스 아님)
 * 색 형식: 0 흑백, 2 RGB, 3 팔레트, 4 흑백+투명도, 6 RGBA (1~16비트). 압축 해제는 tinf(zlib 라이선스).
 */
#include "png.h"
#include "../third_party/tinf/tinf.h"
#include <string.h>

#define MAXW 512
#define MAXH 128
static uint8_t idat[131072];
static uint8_t raw[MAXH * (MAXW * 4 + 1)];      /* 8비트 RGBA 512 x 128까지 (16비트는 더 작게) */
/* 4.0.31: 미리보기용 큰 그림 (최대 1280 x 800, 파일 2 MB까지) */
#define BIGW 1280
#define BIGH 800
#define BIGRAW_SZ (BIGH * (BIGW * 3 + 1))
#define BIGIDAT_SZ (2u << 20)
static uint8_t *rawp = raw, *idatp = idat;
static uint32_t rawcap = sizeof raw, idatcap = sizeof idat, maxw = MAXW, maxh = MAXH;
/* png_load가 채움 */
static uint32_t W, H, stride;
static int depth, ctype, ch, has_alpha;
static uint8_t pal[256][3], palpha[256];

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c;
}

static int png_load_core(const uint8_t *png, uint32_t len, uint32_t *w_out, uint32_t *h_out)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    if (len < 33 || memcmp(png, sig, 8)) return -1;
    uint32_t w = 0, h = 0, pos = 8, ilen = 0;
    int has_trns = 0;
    depth = 0; ctype = 0;
    memset(pal, 0, sizeof pal); memset(palpha, 255, sizeof palpha);
    while (pos + 12 <= len) {
        uint32_t n = be32(png + pos);
        const uint8_t *t = png + pos + 4, *d = png + pos + 8;
        if (pos + 12 + n > len) return -1;
        if (!memcmp(t, "IHDR", 4)) {
            w = be32(d); h = be32(d + 4); depth = d[8]; ctype = d[9];
            if (d[12] || !w || !h || w > maxw || h > maxh) return -1;     /* 인터레이스·큰 그림 안 됨 */
        } else if (!memcmp(t, "PLTE", 4)) {
            for (uint32_t i = 0; i < n / 3 && i < 256; i++) memcpy(pal[i], d + i * 3, 3);
        } else if (!memcmp(t, "tRNS", 4)) {
            has_trns = 1;
            if (ctype == 3) for (uint32_t i = 0; i < n && i < 256; i++) palpha[i] = d[i];
        } else if (!memcmp(t, "IDAT", 4)) {
            if (ilen + n > idatcap) return -1;
            memcpy(idatp + ilen, d, n); ilen += n;
        } else if (!memcmp(t, "IEND", 4)) break;
        pos += 12 + n;
    }
    ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!w || !ch || !ilen) return -1;
    uint32_t bpp_bits = (uint32_t)ch * (uint32_t)depth, bpp = (bpp_bits + 7) / 8;
    stride = (w * bpp_bits + 7) / 8;
    unsigned int rlen = (unsigned int)(h * (stride + 1));
    if (rlen > rawcap) return -1;
    unsigned int got = rlen;
    if (tinf_zlib_uncompress(rawp, &got, idatp, ilen) != TINF_OK || got < rlen) return -1;
    /* 줄마다 필터 복원 (제자리) */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = rawp + y * (stride + 1), f = row[0], *cur = row + 1;
        const uint8_t *prev = y ? rawp + (y - 1) * (stride + 1) + 1 : 0;
        for (uint32_t x = 0; x < stride; x++) {
            int a = x >= bpp ? cur[x - bpp] : 0, b = prev ? prev[x] : 0, c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (f) {
            case 1: cur[x] = (uint8_t)(cur[x] + a); break;
            case 2: cur[x] = (uint8_t)(cur[x] + b); break;
            case 3: cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1)); break;
            case 4: cur[x] = (uint8_t)(cur[x] + paeth(a, b, c)); break;
            default: break;
            }
        }
    }
    has_alpha = ctype == 4 || ctype == 6 || (ctype == 3 && has_trns);
    W = w; H = h;
    if (w_out) *w_out = w;
    if (h_out) *h_out = h;
    return 0;
}

int png_load(const uint8_t *png, uint32_t len, uint32_t *w_out, uint32_t *h_out)
{
    rawp = raw; idatp = idat; rawcap = sizeof raw; idatcap = sizeof idat; maxw = MAXW; maxh = MAXH;
    return png_load_core(png, len, w_out, h_out);
}
/* 4.0.31: 미리보기용 큰 그림 (png_rgba로 읽음). 8비트 RGB 1280 x 800까지 */
int png_load_big(const uint8_t *png, uint32_t len, uint32_t *w_out, uint32_t *h_out, uint8_t *mem)
{
    rawp = mem; idatp = mem + BIGRAW_SZ; rawcap = BIGRAW_SZ; idatcap = BIGIDAT_SZ; maxw = BIGW; maxh = BIGH;
    int r = png_load_core(png, len, w_out, h_out);
    return r;
}

/* 5.3.19: 작업 자리 크기와 최대 크기를 부르는 쪽이 정하는 판 (시스템 보기의 기종 사진 — 원본 그대로, 최대 2048 폭).
 * mem 은 memsz 바이트: 앞은 줄 자료(rawcap), 뒤 2 MB 는 압축 자료. png_load_big 은 그대로 둠 */
int png_load_mem(const uint8_t *png, uint32_t len, uint32_t *w_out, uint32_t *h_out, uint8_t *mem, uint32_t memsz, uint32_t mw, uint32_t mh)
{
    if (memsz <= BIGIDAT_SZ + 4096) return -1;
    rawp = mem; idatp = mem + (memsz - BIGIDAT_SZ); rawcap = memsz - BIGIDAT_SZ; idatcap = BIGIDAT_SZ; maxw = mw; maxh = mh;

    int r = png_load_core(png, len, w_out, h_out);
    return r;
}

/* png_load 뒤: (x, y) 점의 RGBA (0~255) */
void png_rgba(uint32_t x, uint32_t y, int rgba[4])
{
    const uint8_t *row = rawp + y * (stride + 1) + 1;
    int v[4] = { 0, 0, 0, 255 };
    for (int k = 0; k < ch; k++) {                      /* 채널 값 (16비트는 위 바이트, 8비트 미만은 늘림) */
        uint32_t bit = (x * (uint32_t)ch + (uint32_t)k) * (uint32_t)depth;
        int s;
        if (depth >= 8) s = row[bit / 8];
        else { s = (row[bit / 8] >> (8 - depth - (bit % 8))) & ((1 << depth) - 1); if (ctype != 3) s = s * 255 / ((1 << depth) - 1); }
        v[k] = s;
    }
    if (ctype == 3) { const uint8_t *p = pal[v[0] & 255]; rgba[0] = p[0]; rgba[1] = p[1]; rgba[2] = p[2]; rgba[3] = palpha[v[0] & 255]; }
    else if (ch <= 2) { rgba[0] = rgba[1] = rgba[2] = v[0]; rgba[3] = ch == 2 ? v[1] : 255; }
    else { rgba[0] = v[0]; rgba[1] = v[1]; rgba[2] = v[2]; rgba[3] = ch == 4 ? v[3] : 255; }
}
int png_has_alpha(void) { return has_alpha; }

int png_icon16(const uint8_t *png, uint32_t len, char out[16][17])
{
    uint32_t w, h;
    if (png_load(png, len, &w, &h)) return -1;
    for (int oy = 0; oy < 16; oy++) {
        for (int ox = 0; ox < 16; ox++) {
            int c[4];
            png_rgba((uint32_t)ox * w / 16, (uint32_t)oy * h / 16, c);
            int lum = (c[0] * 3 + c[1] * 6 + c[2]) / 10;
            out[oy][ox] = (has_alpha ? c[3] >= 128 : lum < 128) ? '#' : '.';
        }
        out[oy][16] = 0;
    }
    return 0;
}
