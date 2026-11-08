/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* hdfat.c - 8.0.110 하드 디스크 이미지 안의 파일 읽기 전용 (hdfat.h) */
#include "hdfat.h"
#include <string.h>

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }
static int rdsec(HdFat *f, uint32_t lba, uint8_t *b) { return f->rd(f->ctx, lba, b); }

static int is_bpb(const uint8_t *b)
{
    if (b[510] != 0x55 || b[511] != 0xAA) return 0;
    uint32_t bps = le16(b + 11);
    if (bps != 512 || !b[13] || (b[13] & (b[13] - 1)) || !le16(b + 14) || !b[16]) return 0;
    return b[0] == 0xEB || b[0] == 0xE9;
}
int hf_part(HfRead rd, void *ctx, int n, uint32_t *start, uint32_t *nsec)
{
    uint8_t m[512];
    if (rd(ctx, 0, m)) return 0;
    if (is_bpb(m)) { if (n != 0) return 0; *start = 0; *nsec = 0; return 1; }
    if (m[510] != 0x55 || m[511] != 0xAA) return 0;
    int k = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *p = m + 0x1BE + i * 16;
        uint8_t t = p[4];
        if (t != 0x01 && t != 0x04 && t != 0x06 && t != 0x0B && t != 0x0C && t != 0x0E && t != 0x14 && t != 0x16 && t != 0x1B && t != 0x1C && t != 0x1E) continue;
        uint32_t st = le32(p + 8), ns = le32(p + 12);
        if (!st || !ns) continue;
        if (k++ == n) { *start = st; *nsec = ns; return 1; }
    }
    return 0;
}
int hf_open(HdFat *f, HfRead rd, void *ctx, uint32_t start, uint32_t nsec)
{
    uint8_t b[512];
    memset(f, 0, sizeof *f);
    f->rd = rd; f->ctx = ctx; f->start = start; f->nsec = nsec; f->fsec_lba = 0xFFFFFFFFu;
    if (rd(ctx, start, b) || !is_bpb(b)) return -1;
    f->spc = b[13];
    uint32_t rsv = le16(b + 14), nfat = b[16], rootents = le16(b + 17);
    uint32_t tot = le16(b + 19) ? le16(b + 19) : le32(b + 32);
    uint32_t fatsz = le16(b + 22) ? le16(b + 22) : le32(b + 36);
    if (!fatsz || !tot) return -1;
    if (!f->nsec) f->nsec = tot;
    uint32_t rootsecs = (rootents * 32 + 511) / 512;
    f->fat_lba = start + rsv; f->fat_secs = fatsz;
    f->root_ents = rootents; f->root_lba = start + rsv + nfat * fatsz;
    f->data_lba = f->root_lba + rootsecs;
    uint32_t dsec = tot > rsv + nfat * fatsz + rootsecs ? tot - rsv - nfat * fatsz - rootsecs : 0;
    f->clusters = dsec / f->spc;
    f->fat_bits = f->clusters < 4085 ? 12 : f->clusters < 65525 ? 16 : 32;    /* 8.0.114 마스터 결정: 막지 않고 FAT12/16/32 모두 */
    if (f->fat_bits == 32) { f->root_clus = le32(b + 36 + 8); f->root_ents = 0; if (f->root_clus < 2) return -1; }
    else if (rootents == 0) return -1;
    return 0;
}
/* FAT 항목 (클러스터 → 다음). 끝·잘못이면 0xFFFFFFFF */
static uint32_t fat_next(HdFat *f, uint32_t c)
{
    uint32_t off = f->fat_bits == 12 ? c + c / 2 : f->fat_bits == 16 ? c * 2 : c * 4;
    uint32_t lba = f->fat_lba + off / 512, o = off % 512;
    if (lba >= f->fat_lba + f->fat_secs) return 0xFFFFFFFFu;
    if (f->fsec_lba != lba) { if (rdsec(f, lba, f->fsec)) return 0xFFFFFFFFu; f->fsec_lba = lba; }
    uint32_t v;
    if (f->fat_bits == 12) {
        uint32_t lo = f->fsec[o], hi;
        if (o == 511) { uint8_t t[512]; if (rdsec(f, lba + 1, t)) return 0xFFFFFFFFu; hi = t[0]; } else hi = f->fsec[o + 1];
        v = lo | (hi << 8); v = (c & 1) ? v >> 4 : v & 0xFFF;
        return v >= 0xFF8 || v < 2 ? 0xFFFFFFFFu : v;
    }
    if (f->fat_bits == 16) { v = le16(f->fsec + o); return v >= 0xFFF8 || v < 2 ? 0xFFFFFFFFu : v; }
    v = le32(f->fsec + o) & 0x0FFFFFFFu; return v >= 0x0FFFFFF8u || v < 2 ? 0xFFFFFFFFu : v;
}
static uint32_t clus_lba(HdFat *f, uint32_t c) { return f->data_lba + (c - 2) * f->spc; }

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static void sfn_text(const uint8_t *e, char *o)         /* 8+3 → "NAME.EXT" */
{
    int n = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++) o[n++] = (char)e[i];
    if (e[8] != ' ') { o[n++] = '.'; for (int i = 8; i < 11 && e[i] != ' '; i++) o[n++] = (char)e[i]; }
    o[n] = 0;
}
static int ieq(const char *a, const char *b) { while (*a && *b) { if (lower((unsigned char)*a) != lower((unsigned char)*b)) return 0; a++; b++; } return *a == *b; }

/* 폴더(dclus: 0 = 고정 루트, 아니면 클러스터 사슬)에서 이름(n 글자)을 찾음. 찾으면 1 */
static int dir_find(HdFat *f, uint32_t dclus, const char *nm, int n, uint8_t *ent_out)
{
    char want[261]; if (n > 255) return 0; memcpy(want, nm, (size_t)n); want[n] = 0;
    char lfn[261]; int lfn_ok = 0; lfn[0] = 0;
    uint32_t clus = dclus, lba, nsec_in; int fixed = (dclus == 0 && f->fat_bits != 32);
    if (dclus == 0 && f->fat_bits == 32) clus = f->root_clus;
    uint32_t cnt = 0;
    for (;;) {
        if (fixed) { lba = f->root_lba; nsec_in = (f->root_ents * 32 + 511) / 512; }
        else { if (clus < 2 || clus >= f->clusters + 2) return 0; lba = clus_lba(f, clus); nsec_in = f->spc; }
        for (uint32_t s = 0; s < nsec_in; s++) {
            uint8_t b[512];
            if (rdsec(f, lba + s, b)) return 0;
            for (int i = 0; i < 16; i++) {
                const uint8_t *e = b + i * 32;
                if (e[0] == 0) return 0;
                if (e[0] == 0xE5) { lfn_ok = 0; continue; }
                if (e[11] == 0x0F) {                          /* 긴 이름 조각 */
                    int seq = e[0] & 0x1F; if (seq < 1 || seq > 20) { lfn_ok = 0; continue; }
                    if (e[0] & 0x40) { memset(lfn, 0, sizeof lfn); lfn_ok = 1; }
                    if (!lfn_ok) continue;
                    static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                    for (int k = 0; k < 13; k++) {
                        int idx = (seq - 1) * 13 + k; if (idx >= 255) break;
                        uint32_t u = le16(e + pos[k]); if (u == 0 || u == 0xFFFF) { if (idx < 255) lfn[idx] = 0; break; }
                        lfn[idx] = u < 128 ? (char)u : '?';
                    }
                    continue;
                }
                if (e[11] & 0x08) { lfn_ok = 0; continue; }     /* 볼륨 이름 */
                char sn[16]; sfn_text(e, sn);
                int hit = ieq(sn, want) || (lfn_ok && lfn[0] && ieq(lfn, want));
                lfn_ok = 0;
                if (hit) { memcpy(ent_out, e, 32); return 1; }
            }
        }
        if (fixed) return 0;
        clus = fat_next(f, clus);
        if (clus == 0xFFFFFFFFu || ++cnt > f->clusters) return 0;
    }
}
int hf_find(HdFat *f, const char *path, uint32_t *size)
{
    uint32_t dclus = 0;
    f->f_first = 0; f->f_size = 0; f->cur_idx = 0; f->cur_clus = 0; memset(f->ck, 0, sizeof f->ck);
    while (*path == '\\' || *path == '/') path++;
    for (;;) {
        const char *e = path; while (*e && *e != '\\' && *e != '/') e++;
        int n = (int)(e - path); if (n <= 0) return 0;
        uint8_t ent[32];
        if (!dir_find(f, dclus, path, n, ent)) return 0;
        uint32_t clus = le16(ent + 26) | (f->fat_bits == 32 ? le16(ent + 20) << 16 : 0);
        if (*e == 0) {
            if (ent[11] & 0x10) return 0;                      /* 폴더 */
            f->f_first = clus; f->f_size = le32(ent + 28); f->cur_clus = clus; f->cur_idx = 0;
            if (clus >= 2) f->ck[0] = clus;
            if (size) *size = f->f_size;
            return f->f_size ? 1 : 0;
        }
        if (!(ent[11] & 0x10)) return 0;
        dclus = clus ? clus : 0;
        path = e + 1;
    }
}
/* 사슬 번호 idx 의 클러스터. 실패 0 */
static uint32_t chain_at(HdFat *f, uint32_t idx)
{
    uint32_t i, c;
    uint32_t k = idx / HF_CKSTEP; if (k >= HF_CK) k = HF_CK - 1;
    if (idx >= f->cur_idx && f->cur_clus >= 2 && (f->cur_idx / HF_CKSTEP == k || !f->ck[k])) { i = f->cur_idx; c = f->cur_clus; }
    else {
        while (k > 0 && !f->ck[k]) k--;
        i = k * HF_CKSTEP; c = f->ck[k];
        if (idx >= f->cur_idx && f->cur_clus >= 2 && f->cur_idx > i) { i = f->cur_idx; c = f->cur_clus; }
    }
    if (c < 2) return 0;
    while (i < idx) {
        c = fat_next(f, c); if (c == 0xFFFFFFFFu) return 0;
        i++;
        if (i % HF_CKSTEP == 0 && i / HF_CKSTEP < HF_CK) f->ck[i / HF_CKSTEP] = c;
    }
    f->cur_idx = idx; f->cur_clus = c;
    return c;
}
int hf_read(HdFat *f, uint32_t off, uint8_t *buf, uint32_t len)
{
    if (!f->f_first || (uint64_t)off + len > f->f_size) return -1;
    uint32_t csz = f->spc * 512;
    while (len) {
        uint32_t idx = off / csz, in = off % csz;
        uint32_t c = chain_at(f, idx); if (!c) return -1;
        uint32_t sec = in / 512, so = in % 512;
        uint32_t lba = clus_lba(f, c) + sec;
        if (so == 0 && len >= 512) {                           /* 섹터 전체는 바로 buf 로 */
            if (rdsec(f, lba, buf)) return -1;
            buf += 512; off += 512; len -= 512; continue;
        }
        uint8_t t[512]; if (rdsec(f, lba, t)) return -1;
        uint32_t n = 512 - so; if (n > len) n = len;
        memcpy(buf, t + so, n); buf += n; off += n; len -= n;
    }
    return 0;
}
