/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* msxfat.c - 3.0.2 MSX 디스크 이미지 안 파일 (msxfat.h 참고) */
#include "msxfat.h"
#include "dskimg.h"
#include <string.h>

static uint32_t r16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static void w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int mf_open(MsxFat *f, uint8_t *img, uint32_t size)
{
    memset(f, 0, sizeof *f);
    if (!img || size < 512 * 8) return MF_EBAD;
    f->img = img; f->size = size;
    const uint8_t *b = img;
    uint32_t bps = r16(b + 0x0B), spc = b[0x0D], res = r16(b + 0x0E), nf = b[0x10], root = r16(b + 0x11);
    uint32_t tot = r16(b + 0x13), fsz = r16(b + 0x16);
    int ok = bps == 512 && spc && !(spc & (spc - 1)) && spc <= 64 && res >= 1 && nf >= 1 && nf <= 2 && root && !(root % 16)
             && fsz && fsz <= 12 && tot && tot * 512 <= size && b[0x15] >= 0xF0;
    if (ok) {
        f->media = b[0x15]; f->spc = spc; f->nfats = nf; f->fatsz = fsz; f->fat_off = res * 512; f->root_ents = root;
    } else {                                            /* BPB 없음: 매체 바이트와 크기로 */
        uint8_t m = img[512];
        int k = -1;
        for (int i = 0; i < DSK_FORMATS; i++) if (dsk_formats[i].media == m && (uint32_t)dsk_formats[i].sectors * 512 == size) k = i;
        if (k < 0) return MF_EBAD;
        const DskFormat *d = &dsk_formats[k];
        f->media = m; f->spc = d->spc; f->nfats = d->nfats; f->fatsz = d->fat_secs; f->fat_off = 512; f->root_ents = d->root_entries;
        tot = d->sectors;
    }
    f->root_off = f->fat_off + f->nfats * f->fatsz * 512;
    f->data_off = f->root_off + f->root_ents * 32;
    if (f->data_off >= tot * 512) return MF_EBAD;
    f->clusters = (tot * 512 - f->data_off) / (f->spc * 512);
    if (f->clusters >= 4085) return MF_EFAT16;
    uint32_t maxc = (f->fatsz * 512 * 2) / 3;           /* FAT가 담을 수 있는 수 */
    if (f->clusters + 2 > maxc) f->clusters = maxc - 2;
    return MF_OK;
}

static uint32_t fat_get(MsxFat *f, uint32_t n)
{
    const uint8_t *p = f->img + f->fat_off + n * 3 / 2;
    uint32_t v = r16(p);
    return n & 1 ? v >> 4 : v & 0xFFF;
}
static void fat_set(MsxFat *f, uint32_t n, uint32_t v)
{
    for (uint32_t k = 0; k < f->nfats; k++) {
        uint8_t *p = f->img + f->fat_off + k * f->fatsz * 512 + n * 3 / 2;
        if (n & 1) { p[0] = (uint8_t)((p[0] & 0x0F) | (v << 4)); p[1] = (uint8_t)(v >> 4); }
        else { p[0] = (uint8_t)v; p[1] = (uint8_t)((p[1] & 0xF0) | ((v >> 8) & 0x0F)); }
    }
}
static int clus_ok(MsxFat *f, uint32_t c) { return c >= 2 && c < f->clusters + 2; }
static uint32_t clus_off(MsxFat *f, uint32_t c) { return f->data_off + (c - 2) * f->spc * 512; }
static uint32_t csize(MsxFat *f) { return f->spc * 512; }

uint32_t mf_free(MsxFat *f)
{
    uint32_t n = 0;
    for (uint32_t c = 2; c < f->clusters + 2; c++) if (!fat_get(f, c)) n++;
    return n * csize(f);
}

/* 폴더의 항목 자리(바이트 위치) 목록 */
#define MF_SLOTS 4096
static uint32_t slots[MF_SLOTS];
static int dir_slots(MsxFat *f, uint16_t dc)
{
    int n = 0;
    if (!dc) {
        for (uint32_t i = 0; i < f->root_ents && n < MF_SLOTS; i++) slots[n++] = f->root_off + i * 32;
        return n;
    }
    uint32_t c = dc, guard = 0;
    while (clus_ok(f, c) && guard++ < f->clusters) {
        for (uint32_t i = 0; i < csize(f) / 32 && n < MF_SLOTS; i++) slots[n++] = clus_off(f, c) + i * 32;
        c = fat_get(f, c);
    }
    return n;
}

static void raw_to_name(const uint8_t *r, char *out)
{
    int k = 0;
    for (int i = 0; i < 8 && r[i] != ' '; i++) { uint8_t c = r[i]; if (i == 0 && c == 0x05) c = 0xE5; out[k++] = c >= 0x20 && c < 0x7F ? (char)c : '_'; }
    if (r[8] != ' ') {
        out[k++] = '.';
        for (int i = 8; i < 11 && r[i] != ' '; i++) out[k++] = r[i] >= 0x20 && r[i] < 0x7F ? (char)r[i] : '_';
    }
    out[k] = 0;
}

static void fill_entry(MsxFat *f, uint32_t off, MfEntry *e)
{
    const uint8_t *d = f->img + off;
    memset(e, 0, sizeof *e);
    memcpy(e->raw, d, 11);
    raw_to_name(d, e->name);
    e->attr = d[11]; e->is_dir = (d[11] & 0x10) != 0;
    e->time = (uint16_t)r16(d + 22); e->date = (uint16_t)r16(d + 24);
    e->clus = (uint16_t)r16(d + 26);
    e->size = r16(d + 28) | r16(d + 30) << 16;
    e->off = off;
}

int mf_list(MsxFat *f, uint16_t dclus, MfEntry *out, int max)
{
    int ns = dir_slots(f, dclus), n = 0;
    for (int i = 0; i < ns && n < max; i++) {
        const uint8_t *d = f->img + slots[i];
        if (!d[0]) break;
        if (d[0] == 0xE5 || d[11] == 0x0F || (d[11] & 0x08) || d[0] == '.') continue;
        fill_entry(f, slots[i], &out[n++]);
    }
    return n;
}

uint32_t mf_read(MsxFat *f, const MfEntry *e, uint8_t *buf, uint32_t max)
{
    uint32_t left = e->size < max ? e->size : max, got = 0, c = e->clus, guard = 0;
    while (left && clus_ok(f, c) && guard++ < f->clusters) {
        uint32_t k = left < csize(f) ? left : csize(f), o = clus_off(f, c);
        if (o + k > f->size) break;
        memcpy(buf + got, f->img + o, k);
        got += k; left -= k;
        c = fat_get(f, c);
    }
    return got;
}

static uint32_t chain_len(MsxFat *f, uint32_t c)
{
    uint32_t n = 0;
    while (clus_ok(f, c) && n < f->clusters) { n++; c = fat_get(f, c); }
    return n;
}
static void chain_free(MsxFat *f, uint32_t c)
{
    uint32_t guard = 0;
    while (clus_ok(f, c) && guard++ < f->clusters) { uint32_t nx = fat_get(f, c); fat_set(f, c, 0); c = nx; }
}
static uint32_t alloc_one(MsxFat *f, uint32_t from)
{
    for (uint32_t c = from < 2 ? 2 : from; c < f->clusters + 2; c++) if (!fat_get(f, c)) return c;
    for (uint32_t c = 2; c < from && c < f->clusters + 2; c++) if (!fat_get(f, c)) return c;
    return 0;
}

int mf_write(MsxFat *f, uint16_t dc, const uint8_t raw[11], const uint8_t *data, uint32_t len, uint16_t date, uint16_t time)
{
    int ns = dir_slots(f, dc);
    int32_t slot = -1, old = -1;
    for (int i = 0; i < ns; i++) {
        const uint8_t *d = f->img + slots[i];
        if (!d[0]) { if (slot < 0) slot = i; break; }
        if (d[0] == 0xE5) { if (slot < 0) slot = i; continue; }
        if (d[11] == 0x0F || (d[11] & 0x08)) continue;
        if (!memcmp(d, raw, 11)) { if (d[11] & 0x10) return MF_EBAD; old = i; break; }   /* 같은 이름 폴더면 안 됨 */
    }
    uint32_t need = (len + csize(f) - 1) / csize(f);
    uint32_t have = mf_free(f) / csize(f);
    if (old >= 0) have += chain_len(f, r16(f->img + slots[old] + 26));
    int grow = old < 0 && slot < 0;                     /* 폴더를 한 클러스터 늘려야 함 */
    if (grow && !dc) return MF_EDIRFULL;
    if (need + (uint32_t)grow > have) return MF_ENOSPACE;
    uint32_t off;
    if (old >= 0) { off = slots[old]; chain_free(f, r16(f->img + off + 26)); }
    else if (slot >= 0) off = slots[slot];
    else {                                              /* 폴더 늘리기 */
        uint32_t last = dc, guard = 0;
        while (clus_ok(f, fat_get(f, last)) && guard++ < f->clusters) last = fat_get(f, last);
        uint32_t nc = alloc_one(f, 2);
        if (!nc) return MF_ENOSPACE;
        fat_set(f, last, nc); fat_set(f, nc, 0xFFF);
        memset(f->img + clus_off(f, nc), 0, csize(f));
        off = clus_off(f, nc);
    }
    /* 클러스터 잡고 쓰기 */
    uint32_t first = 0, prev = 0, done = 0, hint = 2;
    for (uint32_t i = 0; i < need; i++) {
        uint32_t c = alloc_one(f, hint);
        if (!c) { chain_free(f, first); return MF_ENOSPACE; }  /* (앞에서 확인했으므로 일어나지 않음) */
        fat_set(f, c, 0xFFF);
        if (prev) fat_set(f, prev, c); else first = c;
        uint32_t k = len - done < csize(f) ? len - done : csize(f);
        uint8_t *p = f->img + clus_off(f, c);
        memcpy(p, data + done, k);
        if (k < csize(f)) memset(p + k, 0, csize(f) - k);
        done += k; prev = c; hint = c + 1;
    }
    uint8_t *d = f->img + off;
    memset(d, 0, 32);
    memcpy(d, raw, 11);
    if (d[0] == 0xE5) d[0] = 0x05;
    d[11] = 0x20;
    w16(d + 22, time); w16(d + 24, date);
    w16(d + 26, first);
    w16(d + 28, len & 0xFFFF); w16(d + 30, len >> 16);
    return MF_OK;
}

/* 3.7.3: 폴더 만들기 (같은 이름 폴더가 있으면 그것). *out = 폴더 첫 클러스터. 성공 0 */
int mf_mkdir(MsxFat *f, uint16_t dc, const uint8_t raw[11], uint16_t date, uint16_t time, uint16_t *out)
{
    int ns = dir_slots(f, dc);
    int32_t slot = -1;
    for (int i = 0; i < ns; i++) {
        const uint8_t *d = f->img + slots[i];
        if (!d[0]) { if (slot < 0) slot = i; break; }
        if (d[0] == 0xE5) { if (slot < 0) slot = i; continue; }
        if (d[11] == 0x0F || (d[11] & 0x08)) continue;
        if (!memcmp(d, raw, 11)) {
            if (!(d[11] & 0x10)) return MF_EBAD;            /* 같은 이름 파일 */
            *out = (uint16_t)r16(d + 26);
            return MF_OK;
        }
    }
    int grow = slot < 0;
    if (grow && !dc) return MF_EDIRFULL;
    if (mf_free(f) / csize(f) < 1u + (uint32_t)grow) return MF_ENOSPACE;
    uint32_t off;
    if (slot >= 0) off = slots[slot];
    else {
        uint32_t last = dc, guard = 0;
        while (clus_ok(f, fat_get(f, last)) && guard++ < f->clusters) last = fat_get(f, last);
        uint32_t nc = alloc_one(f, 2);
        if (!nc) return MF_ENOSPACE;
        fat_set(f, last, nc); fat_set(f, nc, 0xFFF);
        memset(f->img + clus_off(f, nc), 0, csize(f));
        off = clus_off(f, nc);
    }
    uint32_t c = alloc_one(f, 2);
    if (!c) return MF_ENOSPACE;
    fat_set(f, c, 0xFFF);
    uint8_t *p = f->img + clus_off(f, c);
    memset(p, 0, csize(f));
    for (int k = 0; k < 2; k++) {                       /* "." 과 ".." */
        uint8_t *e = p + k * 32;
        memset(e, ' ', 11); e[0] = '.'; if (k) e[1] = '.';
        e[11] = 0x10;
        w16(e + 22, time); w16(e + 24, date);
        w16(e + 26, k ? dc : c);
    }
    uint8_t *d = f->img + off;
    memset(d, 0, 32);
    memcpy(d, raw, 11);
    if (d[0] == 0xE5) d[0] = 0x05;
    d[11] = 0x10;
    w16(d + 22, time); w16(d + 24, date);
    w16(d + 26, c);
    *out = (uint16_t)c;
    return MF_OK;
}

int mf_delete(MsxFat *f, const MfEntry *e)
{
    if (e->off + 32 > f->size) return MF_EBAD;
    if (e->is_dir) {                                    /* 빈 폴더만 */
        MfEntry tmp;
        if (mf_list(f, e->clus, &tmp, 1)) return MF_EBAD;
    }
    chain_free(f, e->clus);
    f->img[e->off] = 0xE5;
    return MF_OK;
}

/* 3.7.0: 이름 바꾸기 (같은 폴더에 같은 이름이 있으면 MF_EBAD). 폴더도 됨 */
int mf_rename(MsxFat *f, uint16_t dc, const MfEntry *e, const uint8_t raw[11])
{
    if (e->off + 32 > f->size) return MF_EBAD;
    int ns = dir_slots(f, dc);
    for (int i = 0; i < ns; i++) {
        const uint8_t *d = f->img + slots[i];
        if (!d[0]) break;
        if (d[0] == 0xE5 || d[11] == 0x0F || (d[11] & 0x08) || slots[i] == e->off) continue;
        if (!memcmp(d, raw, 11)) return MF_EBAD;
    }
    memcpy(f->img + e->off, raw, 11);
    if (f->img[e->off] == 0xE5) f->img[e->off] = 0x05;
    return MF_OK;
}

static int in_ok(unsigned char c)
{
    static const char ok[] = "!#$%&'()-@^_`{}~";
    for (int i = 0; ok[i]; i++) if ((unsigned char)ok[i] == c) return 1;
    return 0;
}

int mf_make83(const char *name, uint8_t raw[11])
{
    memset(raw, ' ', 11);
    const char *dot = 0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (dot == name) dot = 0;                           /* ".profile" 같은 이름: 점 앞이 없음 */
    int lossy = 0, k = 0;
    for (const char *p = name; *p && p != dot; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '.') { lossy = 1; continue; }
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || in_ok(c))) { c = '_'; lossy = 1; if ((unsigned char)*p >= 0x80) while ((p[1] & 0xC0) == 0x80) p++; }
        if (k < 8) raw[k++] = c; else lossy = 1;
    }
    if (!k) { raw[0] = '_'; lossy = 1; }
    if (dot) {
        k = 8;
        for (const char *p = dot + 1; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == ' ') { lossy = 1; continue; }
            if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
            else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || in_ok(c))) { c = '_'; lossy = 1; if ((unsigned char)*p >= 0x80) while ((p[1] & 0xC0) == 0x80) p++; }
            if (k < 11) raw[k++] = c; else lossy = 1;
        }
    }
    return lossy;
}
