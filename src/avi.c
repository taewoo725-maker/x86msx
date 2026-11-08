/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* avi.c (8.0.120) - AVI 머리(hdrl)·색인(idx1)을 읽어 영상/소리 조각 표를 만든다. 색인이 없으면 movi 를 차례로 훑는다. */
#include "avi.h"
#include <string.h>

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t fcc(const char *s) { return (uint32_t)(uint8_t)s[0] | (uint32_t)(uint8_t)s[1] << 8 | (uint32_t)(uint8_t)s[2] << 16 | (uint32_t)(uint8_t)s[3] << 24; }

int avi_is(Mp4Read rd, void *ctx, uint32_t size)
{
    uint8_t h[12];
    if (size < 32 || rd(ctx, 0, h, 12)) return 0;
    return !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "AVI ", 4);
}

typedef struct { Mp4Read rd; void *ctx; uint32_t size; AviInfo *o; int vstream, astream, cur; uint32_t movi, movi_end, idx, idx_n; int hasidx; uint32_t usec; } Ctx;

static int rd_(Ctx *c, uint32_t off, uint8_t *b, uint32_t n) { return (off > c->size || n > c->size - off) ? -1 : c->rd(c->ctx, off, b, n); }

static void parse_strl(Ctx *c, uint32_t p, uint32_t end)
{
    int type = 0; uint32_t handler = 0, sc = 0, rt = 0;
    int idx = c->cur;
    while (p + 8 <= end) {
        uint8_t h[8]; if (rd_(c, p, h, 8)) return;
        uint32_t id = le32(h), sz = le32(h + 4), body = p + 8;
        if (sz > end - body) sz = end - body;
        uint8_t b[48];
        if (id == fcc("strh") && sz >= 36 && !rd_(c, body, b, 36)) {
            type = (int)le32(b); handler = le32(b + 4); sc = le32(b + 20); rt = le32(b + 24);
            if (type == (int)fcc("vids") && c->vstream < 0) { c->vstream = idx; c->o->vfcc = handler; c->o->scale = sc; c->o->rate = rt; }
            else if (type == (int)fcc("auds") && c->astream < 0) c->astream = idx;
        } else if (id == fcc("strf") && sz >= 16 && !rd_(c, body, b, sz < 48 ? sz : 48)) {
            if (type == (int)fcc("vids") && idx == c->vstream && sz >= 20) {
                c->o->w = (int)le32(b + 4); c->o->h = (int)le32(b + 8); if (c->o->h < 0) c->o->h = -c->o->h;
                uint32_t comp = le32(b + 16); if (comp) c->o->vfcc = comp;      /* biCompression 이 있으면 그것이 정확 */
            } else if (type == (int)fcc("auds") && idx == c->astream) {
                c->o->has_audio = 1; c->o->atag = (int)le16(b); c->o->ach = (int)le16(b + 2); c->o->arate = le32(b + 4); c->o->abits = sz >= 16 ? (int)le16(b + 14) : 0;
            }
        }
        p = body + sz + (sz & 1);
    }
    (void)handler;
}
static void parse_hdrl(Ctx *c, uint32_t p, uint32_t end)
{
    while (p + 8 <= end) {
        uint8_t h[12]; if (rd_(c, p, h, 12)) return;
        uint32_t id = le32(h), sz = le32(h + 4), body = p + 8;
        if (sz > end - body) sz = end - body;
        if (id == fcc("avih") && sz >= 40) { uint8_t b[40]; if (!rd_(c, body, b, 40)) { c->usec = le32(b); if (!c->o->w) c->o->w = (int)le32(b + 32); if (!c->o->h) c->o->h = (int)le32(b + 36); } }
        else if (id == fcc("LIST") && sz >= 4) {
            if (!memcmp(h + 8, "strl", 4)) { parse_strl(c, body + 4, body + sz); c->cur++; }
            else parse_hdrl(c, body + 4, body + sz);
        }
        p = body + sz + (sz & 1);
    }
}

static int is_vid(const Ctx *c, const uint8_t *id) { return c->vstream >= 0 && id[0] == '0' + c->vstream / 10 && id[1] == '0' + c->vstream % 10 && id[2] == 'd' && (id[3] == 'b' || id[3] == 'c'); }
static int is_aud(const Ctx *c, const uint8_t *id) { return c->astream >= 0 && id[0] == '0' + c->astream / 10 && id[1] == '0' + c->astream % 10 && id[2] == 'w' && id[3] == 'b'; }

static void add(Ctx *c, AviTabs *t, int vid, uint32_t dataoff, uint32_t sz, int key)
{
    if (vid) { if (t && c->o->nv < t->vcap) { t->voff[c->o->nv] = dataoff; t->vsize[c->o->nv] = sz; if (key) t->vkey[c->o->nv >> 3] |= (uint8_t)(1u << (c->o->nv & 7)); } c->o->nv++; }
    else { if (t && c->o->na < t->acap) { t->aoff[c->o->na] = dataoff; t->asize[c->o->na] = sz; } c->o->na++; }
}

int avi_parse(Mp4Read rd, void *ctx, uint32_t size, AviInfo *out, AviTabs *t)
{
    Ctx c; memset(&c, 0, sizeof c); memset(out, 0, sizeof *out);
    c.rd = rd; c.ctx = ctx; c.size = size; c.o = out; c.vstream = c.astream = -1;
    if (!avi_is(rd, ctx, size)) return MP4E_READ;
    uint32_t p = 12;
    for (int g = 0; g < 64 && p + 8 <= size; g++) {            /* 최상위 조각 */
        uint8_t h[12]; if (rd_(&c, p, h, 12 > size - p ? size - p : 12)) break;
        uint32_t id = le32(h), sz = le32(h + 4), body = p + 8;
        if (sz > size - body) sz = size - body;
        if (id == fcc("LIST") && sz >= 4) {
            if (!memcmp(h + 8, "hdrl", 4)) parse_hdrl(&c, body + 4, body + sz);
            else if (!memcmp(h + 8, "movi", 4)) { c.movi = body; c.movi_end = body + sz; }      /* movi = 'movi' 4글자 위치 */
        } else if (id == fcc("idx1")) { c.idx = body; c.idx_n = sz / 16; c.hasidx = 1; }
        p = body + sz + (sz & 1);
    }
    if (c.vstream < 0) return MP4E_NOVIDEO;
    if (!c.movi) return MP4E_NOMOOV;
    if (out->w <= 0 || out->h <= 0) return MP4E_READ;
    int ok = 0;
    if (c.hasidx && c.idx_n) {                                 /* 색인: 첫 항목으로 오프셋 기준 (movi 시작 기준 / 파일 시작 기준) 정함 */
        uint8_t e[16], hh[8]; uint32_t base = 0; int found = 0;
        if (!rd_(&c, c.idx, e, 16)) {
            uint32_t o = le32(e + 8);
            if (!rd_(&c, c.movi + o, hh, 8) && !memcmp(hh, e, 4)) { base = c.movi; found = 1; }
            else if (!rd_(&c, o, hh, 8) && !memcmp(hh, e, 4)) { base = 0; found = 1; }
        }
        if (found) {
            for (uint32_t i = 0; i < c.idx_n; ) {
                uint8_t b[16 * 128]; uint32_t m = c.idx_n - i < 128 ? c.idx_n - i : 128;
                if (rd_(&c, c.idx + i * 16, b, m * 16)) break;
                for (uint32_t k = 0; k < m; k++) {
                    const uint8_t *q = b + k * 16;
                    uint32_t fl = le32(q + 4), off = le32(q + 8), sz = le32(q + 12);
                    if (base + off + 8 > size || sz > size - (base + off + 8)) continue;
                    if (is_vid(&c, q)) add(&c, t, 1, base + off + 8, sz, (fl & 0x10) != 0);
                    else if (is_aud(&c, q)) add(&c, t, 0, base + off + 8, sz, 0);
                }
                i += m;
            }
            ok = out->nv > 0;
        }
    }
    if (!ok) {                                                  /* 색인이 없거나 안 맞음: movi 를 차례로 훑음 */
        out->nv = out->na = 0;
        if (t) memset(t->vkey, 0, (t->vcap + 7) / 8);
        uint32_t q = c.movi + 4;
        while (q + 8 <= c.movi_end) {
            uint8_t hh[12]; if (rd_(&c, q, hh, 8)) break;
            uint32_t sz = le32(hh + 4), body = q + 8;
            if (!memcmp(hh, "LIST", 4)) { q = body + 4; continue; }       /* rec 목록은 안으로 */
            if (sz > c.movi_end - body && sz > size - body) break;
            if (is_vid(&c, hh)) add(&c, t, 1, body, sz, 1);
            else if (is_aud(&c, hh)) add(&c, t, 0, body, sz, 0);
            q = body + sz + (sz & 1);
            if (out->nv >= 262144u) break;
        }
        ok = out->nv > 0;
    }
    if (!ok) return MP4E_NOVIDEO;
    if (t) {
        if (out->nv > t->vcap) out->nv = t->vcap;
        if (out->na > t->acap) out->na = t->acap;
        if (!(t->vkey[0] & 1)) t->vkey[0] |= 1;                 /* 첫 조각은 키 */
    }
    if (!out->rate || !out->scale) { out->scale = c.usec ? c.usec : 66667; out->rate = 1000000; }
    return MP4E_NONE;
}
