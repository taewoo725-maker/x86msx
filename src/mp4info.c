/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mp4info.c (8.0.90) - MP4/MOV 상자 훑기. 표(stts·stsz 등)는 통째로 읽지 않고 머리만 읽으므로 몇 GB 파일도 빠르다. */
#include <string.h>
#include "mp4info.h"
#include "avi.h"

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

typedef struct {
    Mp4Read rd; void *ctx; uint32_t fsize;
    Mp4Info *o; int err;
    /* 지금 보는 트랙 */
    uint32_t handler, mdts, mddur, nsamp, entry; uint16_t tw, th;
    uint8_t prof, lvl, aobj; uint16_t ach; uint32_t arate;
    uint32_t mvts, mvdur;
    uint32_t tb_stsz, tb_stco, tb_co64, tb_stsc, tb_stts, tb_stss, tb_avcc, tb_avcc_len;
    uint32_t tb_asc, asc_len;
    int boxes; uint32_t last_top, ntop;
} Ctx;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static int rd(Ctx *c, uint32_t off, uint8_t *b, uint32_t n) { return (off > c->fsize || n > c->fsize - off) ? -1 : c->rd(c->ctx, off, b, n); }

static int ucase(int ch) { return ch >= 'a' && ch <= 'z' ? ch - 32 : ch; }
int mp4_name_is(const char *n)
{
    int l = 0; while (n[l]) l++;
    if (l < 5 || n[l - 4] != '.') return 0;
    int a = ucase((uint8_t)n[l - 3]), b = ucase((uint8_t)n[l - 2]), d = ucase((uint8_t)n[l - 1]);
    return (a == 'M' && b == 'P' && d == '4') || (a == 'M' && b == '4' && d == 'V') || (a == 'M' && b == 'O' && d == 'V') || (a == 'A' && b == 'V' && d == 'I');
}

static void walk(Ctx *c, uint32_t start, uint32_t end, int depth);

static void sample_entry(Ctx *c, uint32_t off, uint32_t boxend)   /* stsd 의 첫 항목: off = 항목(크기·종류) 시작 */
{
    uint8_t h[0x60];
    uint32_t n = boxend - off < sizeof h ? boxend - off : (uint32_t)sizeof h;
    if (n < 16 || rd(c, off, h, n)) return;
    c->entry = be32(h + 4);
    if (c->handler == FOURCC('v', 'i', 'd', 'e') && n >= 0x30) {
        c->tw = be16(h + 8 + 24); c->th = be16(h + 8 + 26);                /* 항목 머리 8 + 예비 6 + 참조 2 + 버전·예비 16 → 가로·세로 */
        /* 안쪽 상자 (avcC 등): 항목 본체는 8 + 78 바이트 뒤 */
        uint32_t p = off + 8 + 78;
        while (p + 8 <= boxend) {
            uint8_t b[16]; if (rd(c, p, b, 8)) break;
            uint32_t sz = be32(b), ty = be32(b + 4);
            if (sz < 8 || sz > boxend - p) break;
            if (ty == FOURCC('a', 'v', 'c', 'C') && sz >= 13 && !rd(c, p + 8, b, 4)) { c->prof = b[1]; c->lvl = b[3]; c->tb_avcc = p + 8; c->tb_avcc_len = sz - 8; }
            p += sz;
        }
    } else if (c->handler == FOURCC('s', 'o', 'u', 'n') && n >= 0x20) {
        c->ach = be16(h + 8 + 16); c->arate = (uint32_t)be16(h + 8 + 24);   /* 채널·표본 수 (16.16 의 위쪽 16비트) */
        uint32_t ver = be16(h + 8 + 8);                                     /* 8.0.148: QuickTime(MOV) 소리 항목은 버전 1·2 면 머리가 더 길다 (+16, +36 바이트) */
        uint32_t p = off + 8 + 28 + (ver == 1 ? 16u : ver == 2 ? 36u : 0u);
        while (p + 8 <= boxend) {                                           /* esds 에서 객체 종류 */
            uint8_t b[64]; if (rd(c, p, b, 8)) break;
            uint32_t sz = be32(b), ty = be32(b + 4);
            if (sz < 8 || sz > boxend - p) break;
            if (ty == FOURCC('w', 'a', 'v', 'e')) { p += 8; continue; }      /* 8.0.148: MOV 는 esds 가 'wave' 상자 안에 있음 → 안으로 들어감 */
            if (ty == FOURCC('a', 'l', 'a', 'c') && sz >= 8 + 4 + 24) { c->tb_asc = p + 8 + 4; c->asc_len = 24; }       /* 8.0.153 ALAC: 마법 쿠키 = 버전·플래그 4 뒤 ALACSpecificConfig 24바이트 */
            if (ty == FOURCC('e', 's', 'd', 's') && sz >= 12 + 2) {
                uint32_t m = sz - 8 < sizeof b ? sz - 8 : (uint32_t)sizeof b;
                if (!rd(c, p + 8, b, m)) {                                   /* 버전·플래그 4 뒤 ES_Descr(03) → DecoderConfig(04) → 객체 종류, 이어서 DecoderSpecificInfo(05) = AudioSpecificConfig */
                    uint32_t i = 4;
                    for (int t = 0; t < 3 && i < m; t++) {
                        uint8_t tag = b[i++]; uint32_t len = 0; int k = 0;
                        while (i < m && k < 4) { uint8_t x = b[i++]; len = len << 7 | (x & 0x7F); k++; if (!(x & 0x80)) break; }
                        if (tag == 0x03) i += 3;
                        else if (tag == 0x04) { if (i < m) c->aobj = b[i]; i += 13; }
                        else if (tag == 0x05) { if (len && len <= 32 && i + len <= m) { c->tb_asc = p + 8 + i; c->asc_len = len; } break; }
                        else break;
                    }
                }
            }
            p += sz;
        }
    }
}

static void finish_track(Ctx *c)
{
    Mp4Info *o = c->o;
    if (c->handler == FOURCC('v', 'i', 'd', 'e') && !o->vcodec) {
        o->vcodec = c->entry; o->width = c->tw; o->height = c->th; o->nframes = c->nsamp;
        o->avc_profile = c->prof; o->avc_level = c->lvl; o->timescale = c->mdts;
        o->tb_stsz = c->tb_stsz; o->tb_stco = c->tb_stco; o->tb_co64 = c->tb_co64; o->tb_stsc = c->tb_stsc; o->tb_stts = c->tb_stts; o->tb_stss = c->tb_stss; o->tb_avcc = c->tb_avcc; o->tb_avcc_len = c->tb_avcc_len;
        uint32_t ts = c->mdts ? c->mdts : c->mvts, du = c->mdts ? c->mddur : c->mvdur;
        if (ts) {
            o->dur_ms = (uint32_t)((uint64_t)du * 1000 / ts);
            if (du) o->fps100 = (uint32_t)((uint64_t)c->nsamp * ts * 100 / du);
        }
    } else if (c->handler == FOURCC('s', 'o', 'u', 'n') && !o->acodec) {
        o->acodec = c->entry; o->has_audio = 1; o->aac_obj = c->aobj; o->channels = c->ach; o->rate = c->arate;
        o->ta_stsz = c->tb_stsz; o->ta_stco = c->tb_stco; o->ta_co64 = c->tb_co64; o->ta_stsc = c->tb_stsc; o->ta_asc = c->tb_asc; o->ta_asc_len = c->asc_len; o->a_timescale = c->mdts;
    }
}

static void walk(Ctx *c, uint32_t start, uint32_t end, int depth)
{
    uint32_t p = start;
    while (p + 8 <= end && c->boxes++ < 4096) {
        uint8_t h[16]; if (rd(c, p, h, 16 > end - p ? end - p : 16)) { c->err = MP4E_READ; return; }
        uint32_t sz = be32(h), ty = be32(h + 4), hl = 8;
        if (sz == 1) { if (end - p < 16) break; if (be32(h + 8)) sz = end - p; else sz = be32(h + 12); hl = 16; }
        else if (sz == 0) sz = end - p;
        if (sz < hl || sz > end - p) { if (depth == 0 && sz > end - p) sz = end - p; else break; }   /* 최상위 마지막 상자가 잘린 파일은 가능한 데까지 */
        uint32_t body = p + hl, bend = p + sz;
        uint8_t b[32];
        if (depth == 0) { c->last_top = ty; c->ntop++; }
        switch (ty) {
        case FOURCC('m', 'o', 'o', 'v'): walk(c, body, bend, depth + 1); break;
        case FOURCC('m', 'o', 'o', 'f'): case FOURCC('m', 'v', 'e', 'x'): c->o->fragmented = 1; break;
        case FOURCC('t', 'r', 'a', 'k'):
            c->handler = c->mdts = c->mddur = c->nsamp = c->entry = 0; c->tw = c->th = 0; c->prof = c->lvl = c->aobj = 0; c->ach = 0; c->arate = 0;
            c->tb_stsz = c->tb_stco = c->tb_co64 = c->tb_stsc = c->tb_stts = c->tb_stss = c->tb_avcc = c->tb_avcc_len = 0; c->tb_asc = c->asc_len = 0;
            walk(c, body, bend, depth + 1); finish_track(c); break;
        case FOURCC('m', 'd', 'i', 'a'): case FOURCC('m', 'i', 'n', 'f'): case FOURCC('s', 't', 'b', 'l'):
            if (depth < 6) walk(c, body, bend, depth + 1); break;
        case FOURCC('m', 'v', 'h', 'd'):
            if (bend - body >= 20 && !rd(c, body, b, 20)) {
                if (b[0] == 1) { if (bend - body >= 32 && !rd(c, body, b, 32)) { c->mvts = be32(b + 20); c->mvdur = be32(b + 28); } }
                else { c->mvts = be32(b + 12); c->mvdur = be32(b + 16); }
            } break;
        case FOURCC('m', 'd', 'h', 'd'):
            if (bend - body >= 24 && !rd(c, body, b, 32 > bend - body ? bend - body : 32)) {
                if (b[0] == 1) { if (bend - body >= 32) { c->mdts = be32(b + 20); c->mddur = be32(b + 28); } }
                else { c->mdts = be32(b + 12); c->mddur = be32(b + 16); }
            } break;
        case FOURCC('h', 'd', 'l', 'r'):
            if (!c->handler && bend - body >= 12 && !rd(c, body, b, 12)) c->handler = be32(b + 8); break;   /* MOV 는 minf 안에도 hdlr('alis')가 있어 처음 것만 */
        case FOURCC('s', 't', 's', 'd'):
            if (bend - body >= 8 + 16) sample_entry(c, body + 8, bend); break;
        case FOURCC('s', 't', 's', 'z'):
            if (bend - body >= 12 && !rd(c, body, b, 12)) { c->nsamp = be32(b + 8); c->tb_stsz = body; } break;
        case FOURCC('s', 't', 'c', 'o'): c->tb_stco = body; break;
        case FOURCC('c', 'o', '6', '4'): c->tb_co64 = body; break;
        case FOURCC('s', 't', 's', 'c'): c->tb_stsc = body; break;
        case FOURCC('s', 't', 't', 's'): c->tb_stts = body; break;
        case FOURCC('s', 't', 's', 's'): c->tb_stss = body; break;
        case FOURCC('s', 't', 'z', '2'):
            if (bend - body >= 12 && !rd(c, body, b, 12)) c->nsamp = be32(b + 8); break;
        default: break;
        }
        if (c->err) return;
        p = bend;
    }
}

int mp4_probe(Mp4Read rdf, void *ctx, uint32_t size, Mp4Info *out)
{
    if (avi_is(rdf, ctx, size)) {                                /* 8.0.120 AVI: MJPEG + PCM */
        AviInfo a; memset(out, 0, sizeof *out);
        int e = avi_parse(rdf, ctx, size, &a, 0);
        if (e != MP4E_NONE) return e;
        out->avi = 1; out->width = (uint16_t)a.w; out->height = (uint16_t)a.h; out->nframes = a.nv; out->timescale = 1000;
        out->fps100 = (uint32_t)((uint64_t)a.rate * 100u / a.scale);
        out->dur_ms = (uint32_t)((uint64_t)a.nv * 1000u * a.scale / a.rate);
        out->has_audio = (uint8_t)a.has_audio; out->channels = (uint16_t)a.ach; out->rate = a.arate;
        if (a.has_audio) out->acodec = (uint32_t)a.atag;
        uint32_t v = a.vfcc & 0xDFDFDFDFu;                        /* 대문자로 */
        out->vcodec = 0x4D4A5047u;                                /* 'MJPG' */
        if (v != 0x47504A4Du && v != 0x31495641u /* AVI1 */ && v != 0x4745504Au /* JPEG */) { out->vcodec = a.vfcc; return MP4E_VCODEC; }
        return MP4E_NONE;
    }
    Ctx c; memset(&c, 0, sizeof c); memset(out, 0, sizeof *out);
    c.rd = rdf; c.ctx = ctx; c.fsize = size; c.o = out;
    uint8_t h[16];
    if (size < 16 || rd(&c, 0, h, 16)) return MP4E_READ;
    uint32_t t = be32(h + 4);                                    /* 맨 앞 상자는 ftyp 이거나 (MOV 는) moov/mdat/wide/free */
    if (t != FOURCC('f', 't', 'y', 'p') && t != FOURCC('m', 'o', 'o', 'v') && t != FOURCC('m', 'd', 'a', 't') && t != FOURCC('w', 'i', 'd', 'e') && t != FOURCC('f', 'r', 'e', 'e') && t != FOURCC('s', 'k', 'i', 'p')) return MP4E_READ;
    walk(&c, 0, size, 0);
    out->dbg_last = c.last_top; out->dbg_top = c.ntop; out->dbg_mvts = c.mvts; out->dbg_boxes = (uint16_t)(c.boxes > 65535 ? 65535 : c.boxes);
    if (c.err) return c.err;
    if (!out->vcodec && !out->has_audio && !out->width) return out->fragmented ? MP4E_NOMOOV : (c.mvts ? MP4E_NOVIDEO : MP4E_NOMOOV);
    if (!out->vcodec) return MP4E_NOVIDEO;
    if (out->vcodec != FOURCC('a', 'v', 'c', '1') && out->vcodec != FOURCC('a', 'v', 'c', '3') &&
        out->vcodec != FOURCC('m', 'p', '4', 'v') && out->vcodec != FOURCC('m', 'p', 'g', '1')) return MP4E_VCODEC;
    if (!out->nframes && out->fragmented) return MP4E_NOMOOV;
    if (!out->width || !out->height || !out->nframes) return MP4E_READ;
    return MP4E_NONE;
}
