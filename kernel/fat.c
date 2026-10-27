/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fat.c - FAT16/FAT32 읽기 (레거시 부팅에서 실행 중 파일 접근)
 *
 * 섹터 읽기 함수를 받아 쓰므로 호스트에서 이미지 파일로 시험한다 (tests/fattest.c).
 * 지원: MBR 파티션(0x01·0x04·0x06·0x0B·0x0C·0x0E) 또는 파티션 없는 디스크, 섹터 512바이트,
 *       긴 파일 이름(VFAT), 8.3 이름의 소문자 표시 플래그, 만든 날짜(없으면 고친 날짜).
 */
#include "fat.h"
#include <string.h>

/* 파일 날짜 (2.10): 플랫폼이 시계를 주면 그 값, 없으면 2026-10-04 */
static void (*clock_fn)(uint16_t *date, uint16_t *time);
void fat_set_clock(void (*fn)(uint16_t *date, uint16_t *time)) { clock_fn = fn; }
static void fat_now(uint16_t *d, uint16_t *t) { *d = (uint16_t)((46u << 9) | (10u << 5) | 4u); *t = 0; if (clock_fn) clock_fn(d, t); }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* ------------------------------------------------------------------ */
/* 섹터 캐시 (한 섹터)                                                  */
/* ------------------------------------------------------------------ */
static const uint8_t *sector(FatVol *v, uint32_t lba)
{
    if (v->cache_lba != lba) {
        if (v->read(v->ctx, lba, 1, v->cache)) { v->cache_lba = 0xFFFFFFFFu; return 0; }
        v->cache_lba = lba;
    }
    return v->cache;
}

static uint32_t next_cluster(FatVol *v, uint32_t c)
{
    uint32_t off = v->fat32 ? c * 4 : c * 2;
    const uint8_t *s = sector(v, v->fat_lba + off / 512);
    if (!s) return 0x0FFFFFFF;
    if (v->fat32) return rd32(s + off % 512) & 0x0FFFFFFF;
    uint32_t x = rd16(s + off % 512);
    return x >= 0xFFF8 ? 0x0FFFFFFF : x;
}

/* 64섹터씩 나눠 읽기 (BIOS 버퍼 한계) */
static int read_secs(FatVol *v, uint32_t lba, uint32_t n, uint8_t *buf)
{
    while (n) {
        uint32_t k = n > 64 ? 64 : n;
        if (v->read(v->ctx, lba, k, buf)) return -1;
        lba += k; n -= k; buf += k * 512;
    }
    return 0;
}

static int end_cluster(FatVol *v, uint32_t c) { return c < 2 || c >= 0x0FFFFFF8 || c >= v->clusters + 2; }
static uint32_t cluster_lba(FatVol *v, uint32_t c) { return v->data_lba + (c - 2) * v->spc; }

/* ------------------------------------------------------------------ */
/* 볼륨 찾기                                                            */
/* ------------------------------------------------------------------ */
static void lc_drop(void);
static int mount(FatVol *v, FatRead rd, void *ctx, uint32_t lba)
{
    lc_drop();
    static uint8_t b[512];
    if (rd(ctx, lba, 1, b)) return -1;
    if (b[510] != 0x55 || b[511] != 0xAA) return -1;
    if (rd16(b + 11) != 512) return -1;
    uint32_t spc = b[13], rsvd = rd16(b + 14), nfats = b[16], rootents = rd16(b + 17);
    uint32_t total = rd16(b + 19) ? rd16(b + 19) : rd32(b + 32);
    uint32_t fatsz = rd16(b + 22) ? rd16(b + 22) : rd32(b + 36);
    if (!spc || (spc & (spc - 1)) || !rsvd || !nfats || nfats > 2 || !fatsz || !total) return -1;
    memset(v, 0, sizeof *v);
    v->read = rd; v->ctx = ctx; v->part_lba = lba; v->spc = spc;
    v->fat_lba = lba + rsvd;
    v->root_secs = (rootents * 32 + 511) / 512;
    v->root_lba = v->fat_lba + nfats * fatsz;
    v->nfats = nfats;
    v->fatsz = fatsz;
    v->data_lba = v->root_lba + v->root_secs;
    if (total <= v->data_lba - lba) return -1;
    v->clusters = (total - (v->data_lba - lba)) / spc;
    if (v->clusters < 4085) return -1;                  /* FAT12는 지원 안 함 */
    v->fat32 = v->clusters >= 65525;
    if (v->fat32) { v->root_clus = rd32(b + 44); v->root_secs = 0; if (rd16(b + 48) && rd16(b + 48) < rsvd) v->fsinfo_lba = lba + rd16(b + 48); }
    v->bytes = (uint64_t)total * 512;
    const uint8_t *lab = b + (v->fat32 ? 71 : 43);
    memcpy(v->label, lab, 11);
    v->label[11] = 0;
    for (int i = 10; i >= 0 && v->label[i] == ' '; i--) v->label[i] = 0;
    if (!strcmp(v->label, "NO NAME")) v->label[0] = 0;
    v->cache_lba = 0xFFFFFFFFu;
    return 0;
}

int fat_mount(FatVol *v, FatRead rd, void *ctx, uint32_t lba) { return mount(v, rd, ctx, lba); }   /* 3.7.0 */

int fat_scan_disk(FatRead rd, void *ctx, FatVol *out, int max)
{
    static uint8_t mbr[512];
    if (max <= 0 || rd(ctx, 0, 1, mbr) || mbr[510] != 0x55 || mbr[511] != 0xAA) return 0;
    int n = 0;
    for (int i = 0; i < 4 && n < max; i++) {
        const uint8_t *e = mbr + 446 + i * 16;
        uint8_t t = e[4];
        uint32_t start = rd32(e + 8);
        if ((e[0] & 0x7F) || !start) continue;           /* 상태 바이트는 0x00/0x80만 */
        if (t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E)
            if (!mount(&out[n], rd, ctx, start)) n++;
    }
    if (!n && !mount(&out[0], rd, ctx, 0)) n = 1;       /* 파티션 없는 디스크 */
    return n;
}

/* ------------------------------------------------------------------ */
/* 디렉터리 읽기                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    FatVol *v;
    uint32_t clus, lba, left;       /* 현재 클러스터, 다음 섹터, 이 구역에 남은 섹터 */
    int root16;
    int idx;                        /* 섹터 안 항목 번호 */
} DirIter;

static void dir_open(FatVol *v, uint32_t clus, DirIter *it)
{
    memset(it, 0, sizeof *it);
    it->v = v;
    if (!clus && !v->fat32) { it->root16 = 1; it->lba = v->root_lba; it->left = v->root_secs; }
    else {
        if (!clus) clus = v->root_clus;
        it->clus = clus; it->lba = cluster_lba(v, clus); it->left = v->spc;
    }
}

/* 폴더 목록 읽기 중에는 섹터를 16개씩 묶어 읽음 (USB는 512바이트씩 읽으면 명령마다 시간이 걸려 큰 폴더가 느림). 쓰기 중에는 쓰지 않음 */
static struct { FatVol *v; uint32_t lba; uint32_t n; uint8_t buf[16 * 512]; } dw;
static int dw_on;
static const uint8_t *dsector(FatVol *v, uint32_t lba, uint32_t left)
{
    if (!dw_on) return sector(v, lba);
    if (!(dw.v == v && dw.n && lba >= dw.lba && lba < dw.lba + dw.n)) {
        uint32_t n = left > 16 ? 16 : left;
        if (n < 1) n = 1;
        dw.n = 0;
        if (v->read(v->ctx, lba, n, dw.buf)) return sector(v, lba);
        dw.v = v; dw.lba = lba; dw.n = n;
    }
    return dw.buf + (lba - dw.lba) * 512;
}

/* 다음 32바이트 항목 (끝이면 0) */
static const uint8_t *dir_next(DirIter *it)
{
    for (;;) {
        if (it->idx >= 16) {
            it->idx = 0;
            it->lba++;
            if (--it->left == 0) {
                if (it->root16) return 0;
                it->clus = next_cluster(it->v, it->clus);
                if (end_cluster(it->v, it->clus)) return 0;
                it->lba = cluster_lba(it->v, it->clus);
                it->left = it->v->spc;
            }
        }
        if (!it->left) return 0;
        const uint8_t *s = dsector(it->v, it->lba, it->left);
        if (!s) return 0;
        const uint8_t *e = s + 32 * it->idx++;
        if (e[0] == 0) return 0;
        return e;
    }
}

static int utf8_put(uint32_t c, char *d, int k, int max)
{
    if (c < 0x80) { if (k + 1 < max) d[k++] = (char)c; }
    else if (c < 0x800) { if (k + 2 < max) { d[k++] = (char)(0xC0 | (c >> 6)); d[k++] = (char)(0x80 | (c & 0x3F)); } }
    else if (k + 3 < max) { d[k++] = (char)(0xE0 | (c >> 12)); d[k++] = (char)(0x80 | ((c >> 6) & 0x3F)); d[k++] = (char)(0x80 | (c & 0x3F)); }
    return k;
}

/* 다음 파일 항목 (긴 이름 조합). 끝이면 -1 */
static int dir_entry(DirIter *it, FatEntry *out)
{
    static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    uint16_t lfn[260];
    int have_lfn = 0;
    const uint8_t *e;
    while ((e = dir_next(it)) != 0) {
        if (e[0] == 0xE5) { have_lfn = 0; continue; }
        if (e[11] == 0x0F) {                            /* 긴 이름 조각 */
            int seq = (e[0] & 0x1F) - 1;
            if (e[0] & 0x40) { for (int i = 0; i < 260; i++) lfn[i] = 0xFFFF; have_lfn = 1; }
            if (seq >= 0 && seq < 20)
                for (int j = 0; j < 13; j++) lfn[seq * 13 + j] = rd16(e + pos[j]);
            continue;
        }
        if (e[11] & 0x08) { have_lfn = 0; continue; }   /* 볼륨 이름 */
        memset(out, 0, sizeof *out);
        int k = 0, dk = 0;
        if (have_lfn) {
            for (int i = 0; i < 260 && lfn[i] && lfn[i] != 0xFFFF; i++) {
                k = utf8_put(lfn[i], out->key, k, sizeof out->key);
                if (dk < (int)sizeof out->disp - 1) out->disp[dk++] = (lfn[i] >= 0x20 && lfn[i] < 0x7F) ? (char)lfn[i] : '?';
            }
        } else {                                        /* 8.3 (소문자 플래그 반영) */
            int lb = e[12] & 0x08, le = e[12] & 0x10;
            for (int i = 0; i < 8 && e[i] != ' '; i++) out->key[k++] = (char)(lb ? lower(e[i]) : e[i]);
            if (e[0] == 0x05) out->key[0] = (char)0xE5;
            if (e[8] != ' ') {
                out->key[k++] = '.';
                for (int i = 8; i < 11 && e[i] != ' '; i++) out->key[k++] = (char)(le ? lower(e[i]) : e[i]);
            }
            for (dk = 0; dk < k && dk < (int)sizeof out->disp - 1; dk++) out->disp[dk] = out->key[dk];
        }
        out->key[k] = 0;
        out->disp[dk] = 0;
        have_lfn = 0;
        out->is_dir = (e[11] & 0x10) != 0;
        out->ent_lba = it->lba;
        out->ent_off = (uint16_t)((it->idx - 1) * 32);
        out->clus = ((uint32_t)rd16(e + 20) << 16) | rd16(e + 26);
        if (!it->v->fat32) out->clus &= 0xFFFF;
        out->size = rd32(e + 28);
        uint16_t date = rd16(e + 16), time = rd16(e + 14);
        if (!date) { date = rd16(e + 24); time = rd16(e + 22); }
        if (date) {
            out->year = (uint16_t)(1980 + (date >> 9));
            out->mon = (uint8_t)((date >> 5) & 15);
            out->day = (uint8_t)(date & 31);
            out->hour = (uint8_t)(time >> 11);
            out->min = (uint8_t)((time >> 5) & 63);
        }
        return 0;
    }
    return -1;
}

/* UTF-8 이름 비교 (ASCII 대소문자 무시) */
static int name_eq(const char *a, const char *b, int blen)
{
    int i = 0;
    for (; i < blen && a[i]; i++) if (lower((uint8_t)a[i]) != lower((uint8_t)b[i])) return 0;
    return i == blen && !a[i];
}

/* 경로를 따라가 마지막 항목을 찾는다. 최상위면 is_dir=1, clus=0 */
static int resolve(FatVol *v, const char *path, FatEntry *out)
{
    memset(out, 0, sizeof *out);
    out->is_dir = 1;
    out->clus = 0;
    const char *p = path;
    while (*p) {
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        int len = 0;
        while (p[len] && p[len] != '\\' && p[len] != '/') len++;
        if (!out->is_dir) return -1;
        DirIter it;
        dir_open(v, out->clus, &it);
        FatEntry e;
        int found = 0;
        while (!dir_entry(&it, &e)) {
            if (name_eq(e.key, p, len)) { *out = e; found = 1; break; }
        }
        if (!found) return -1;
        if (out->is_dir && out->clus == 0 && v->fat32) out->clus = v->root_clus;   /* ".." → 최상위 */
        p += len;
    }
    return 0;
}

/* 폴더 목록 읽기 위치 기억 — 같은 폴더를 이어서 읽을 때(skip이 지난번 끝 위치와 같을 때) 처음부터 다시 훑지 않음.
 * 이전에는 쪽(48개)마다 폴더를 처음부터 다시 읽어 항목 수의 제곱에 비례해 느렸음 (5번에서 1192개 폴더 진입이 오래 걸리고 5824개에서 멈춤).
 * 쓰기·만들기·지우기·마운트 때는 무효로 함. */
static struct { FatVol *v; char path[512]; DirIter it; int pos; int valid; } lc;
static void lc_drop(void) { lc.valid = 0; dw.n = 0; }
int fat_list_skip(FatVol *v, const char *path, FatEntry *out, int max, int skip)   /* 4.0.29: 앞의 skip개를 건너뜀 (작은 버퍼로 쪽마다 읽기) */
{
    DirIter it;
    int pos = 0;
    if (lc.valid && lc.v == v && lc.pos == skip && !strcmp(lc.path, path)) { it = lc.it; pos = skip; skip = 0; }
    else {
        FatEntry d;
        lc.valid = 0;
        if (resolve(v, path, &d) || !d.is_dir) return -1;
        dir_open(v, d.clus, &it);
    }
    int n = 0;
    FatEntry e;
    dw_on = 1;
    while (n < max && !dir_entry(&it, &e)) {
        if (e.key[0] == '.' && (!e.key[1] || (e.key[1] == '.' && !e.key[2]))) continue;
        if (skip > 0) { skip--; pos++; continue; }
        out[n++] = e; pos++;
    }
    dw_on = 0;
    if (skip == 0 && strlen(path) < sizeof lc.path) { lc.v = v; memcpy(lc.path, path, strlen(path) + 1); lc.it = it; lc.pos = pos; lc.valid = 1; }
    return n;
}
int fat_list(FatVol *v, const char *path, FatEntry *out, int max) { return fat_list_skip(v, path, out, max, 0); }

int fat_read_file(FatVol *v, const char *path, uint8_t *buf, uint32_t max, uint32_t *size)
{
    FatEntry f;
    if (resolve(v, path, &f) || f.is_dir) return -1;
    *size = f.size;
    uint32_t want = f.size < max ? f.size : max, done = 0;
    uint32_t c = f.clus, csize = v->spc * 512;
    static uint8_t tmp[512];
    while (done < want && !end_cluster(v, c)) {
        /* 이어진 클러스터를 모아 한 번에 (최대 64섹터) */
        uint32_t run = 1, last = c;
        while ((run + 1) * v->spc <= 128 && done + (run + 1) * csize <= want) {
            uint32_t nx = next_cluster(v, last);
            if (nx != last + 1) break;
            last = nx;
            run++;
        }
        uint32_t secs = run * v->spc, bytes = secs * 512;
        if (done + bytes <= want) {
            if (read_secs(v, cluster_lba(v, c), secs, buf + done)) return -1;
            done += bytes;
        } else {                                        /* 마지막: 섹터 단위로 필요한 만큼 */
            for (uint32_t s = 0; s < secs && done < want; s++) {
                if (v->read(v->ctx, cluster_lba(v, c) + s, 1, tmp)) return -1;
                uint32_t n = want - done < 512 ? want - done : 512;
                memcpy(buf + done, tmp, n);
                done += n;
            }
        }
        c = next_cluster(v, last);
    }
    return done == want ? 0 : -1;
}

int fat_write_inplace(FatVol *v, const char *path, const uint8_t *data, uint32_t len)
{
    lc_drop();
    FatEntry f;
    if (!v->write || resolve(v, path, &f) || f.is_dir || f.size != len || !len) return -1;
    static uint8_t tmp[512];
    uint32_t done = 0, c = f.clus;
    while (done < len && !end_cluster(v, c)) {
        for (uint32_t s = 0; s < v->spc && done < len; s++) {
            uint32_t n = len - done < 512 ? len - done : 512;
            memset(tmp, 0, 512);
            memcpy(tmp, data + done, n);
            uint32_t lba = cluster_lba(v, c) + s;
            if (v->write(v->ctx, lba, 1, tmp)) return -1;
            if (v->cache_lba == lba) memcpy(v->cache, tmp, 512);
            done += n;
        }
        c = next_cluster(v, c);
    }
    return done == len ? 0 : -1;
}

/* ---- 크기를 바꾸는 쓰기 ---- */
static int fat_set(FatVol *v, uint32_t c, uint32_t val)
{
    static uint8_t t[512];
    if (v->fat32 && v->fsinfo_lba && !v->fsinfo_done) {     /* FSInfo의 빈 클러스터 수·다음 빈 자리를 '모름'으로 (2.9) */
        v->fsinfo_done = 1;
        if (!v->read(v->ctx, v->fsinfo_lba, 1, t) && t[0] == 'R' && t[1] == 'R' && t[484] == 'r') {
            memset(t + 488, 0xFF, 8);
            v->write(v->ctx, v->fsinfo_lba, 1, t);
            if (v->cache_lba == v->fsinfo_lba) memcpy(v->cache, t, 512);
        }
    }
    uint32_t off = v->fat32 ? c * 4 : c * 2;
    for (uint32_t k = 0; k < v->nfats; k++) {               /* FAT 사본 모두 */
        uint32_t lba = v->fat_lba + k * v->fatsz + off / 512;
        if (v->read(v->ctx, lba, 1, t)) return -1;
        uint8_t *p = t + off % 512;
        if (v->fat32) {
            uint32_t old = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            uint32_t nv = (old & 0xF0000000u) | (val & 0x0FFFFFFFu);
            p[0] = (uint8_t)nv; p[1] = (uint8_t)(nv >> 8); p[2] = (uint8_t)(nv >> 16); p[3] = (uint8_t)(nv >> 24);
        } else { p[0] = (uint8_t)val; p[1] = (uint8_t)(val >> 8); }
        if (v->write(v->ctx, lba, 1, t)) return -1;
        if (v->cache_lba == lba) memcpy(v->cache, t, 512);
    }
    return 0;
}

static uint32_t find_free(FatVol *v, uint32_t from)
{
    for (uint32_t c = from < 2 ? 2 : from; c < v->clusters + 2; c++) if (next_cluster(v, c) == 0) return c;
    return 0;
}

int fat_write_file(FatVol *v, const char *path, const uint8_t *data, uint32_t len)
{
    lc_drop();
    FatEntry f;
    if (!v->write || resolve(v, path, &f) || f.is_dir) return -1;
    uint32_t csize = v->spc * 512, need = (len + csize - 1) / csize, eoc = v->fat32 ? 0x0FFFFFFFu : 0xFFFFu;
    if (!need) need = 1;
    static uint32_t chain[262144];                          /* 3.2.13: 큰 파일(영상)도 — 2 KiB 클러스터면 512 MiB까지 */
    uint32_t have = 0;
    for (uint32_t c = f.clus; !end_cluster(v, c) && have < 262144; c = next_cluster(v, c)) chain[have++] = c;
    if (need > 262144) return -1;
    uint32_t have0 = have;
    while (have < need) {                                   /* 빈 클러스터 이어 붙이기 */
        uint32_t c = find_free(v, have ? chain[have - 1] + 1 : 2);
        if (!c) c = find_free(v, 2);
        if (!c || fat_set(v, c, eoc) || (have && fat_set(v, chain[have - 1], c))) {
            /* 3.4.5: 디스크가 가득 차면 새로 붙인 클러스터를 되돌림 (전에는 사슬이 늘어난 채 남거나, 빈 파일이면 잃어버린 클러스터가 됨) */
            if (c && !have) fat_set(v, c, 0);
            for (uint32_t i = have0; i < have; i++) fat_set(v, chain[i], 0);
            if (have0) fat_set(v, chain[have0 - 1], eoc);
            return -1;
        }
        chain[have++] = c;
    }
    if (have > need) {                                      /* 남는 클러스터 풀기 */
        if (fat_set(v, chain[need - 1], eoc)) return -1;
        for (uint32_t i = need; i < have; i++) if (fat_set(v, chain[i], 0)) return -1;
    }
    static uint8_t tmp[512];
    uint32_t done = 0;
    for (uint32_t i = 0; i < need; i++) {                   /* 데이터: 꽉 찬 클러스터는 한 번에 (2.10, 64섹터씩) */
        if (len - done >= csize) {
            /* 이어진 클러스터는 묶어서 64섹터씩 (작은 클러스터 USB에서 영상 같은 큰 파일 저장이 너무 느렸음: 512 B씩 호출) */
            uint32_t full = (len - done) / csize, run = 1;
            if (full > need - i) full = need - i;
            while (run < full && chain[i + run] == chain[i + run - 1] + 1) run++;
            uint32_t lba = cluster_lba(v, chain[i]), total = run * v->spc;
            for (uint32_t s2 = 0; s2 < total; ) {
                uint32_t k = total - s2 > 64 ? 64 : total - s2;
                if (v->write(v->ctx, lba + s2, k, data + done)) return -1;
                if (v->cache_lba >= lba + s2 && v->cache_lba < lba + s2 + k) v->cache_lba = 0xFFFFFFFFu;
                s2 += k; done += k * 512;
            }
            i += run - 1;
            continue;
        }
        for (uint32_t s2 = 0; s2 < v->spc && done < len; s2++) {
            uint32_t n = len - done < 512 ? len - done : 512;
            memset(tmp, 0, 512);
            memcpy(tmp, data + done, n);
            uint32_t lba = cluster_lba(v, chain[i]) + s2;
            if (v->write(v->ctx, lba, 1, tmp)) return -1;
            if (v->cache_lba == lba) memcpy(v->cache, tmp, 512);
            done += n;
        }
    }
    if (v->read(v->ctx, f.ent_lba, 1, tmp)) return -1;     /* 디렉터리 항목: 크기·첫 클러스터 */
    uint8_t *e = tmp + f.ent_off;
    e[28] = (uint8_t)len; e[29] = (uint8_t)(len >> 8); e[30] = (uint8_t)(len >> 16); e[31] = (uint8_t)(len >> 24);
    e[26] = (uint8_t)chain[0]; e[27] = (uint8_t)(chain[0] >> 8);
    { uint16_t dd, tt; fat_now(&dd, &tt); e[22] = (uint8_t)tt; e[23] = (uint8_t)(tt >> 8); e[24] = (uint8_t)dd; e[25] = (uint8_t)(dd >> 8); }   /* 고친 날짜 (2.10) */
    if (v->fat32) { e[20] = (uint8_t)(chain[0] >> 16); e[21] = (uint8_t)(chain[0] >> 24); }
    if (v->write(v->ctx, f.ent_lba, 1, tmp)) return -1;
    if (v->cache_lba == f.ent_lba) memcpy(v->cache, tmp, 512);
    return 0;
}

int fat_file_extents(FatVol *v, const char *path, FatExtent *ext, int max, uint32_t *size)
{
    FatEntry f;
    if (resolve(v, path, &f) || f.is_dir || !f.size) return -1;
    *size = f.size;
    uint32_t need = (f.size + v->spc * 512 - 1) / (v->spc * 512), have = 0;
    int n = 0;
    uint32_t c = f.clus;
    while (have < need && !end_cluster(v, c) && c >= 2) {
        uint64_t lba = cluster_lba(v, c);
        if (n && ext[n - 1].lba + ext[n - 1].secs == lba) ext[n - 1].secs += v->spc;   /* 이어진 클러스터 */
        else {
            if (n >= max) return -1;
            ext[n].lba = lba; ext[n].secs = v->spc; n++;
        }
        have++;
        c = next_cluster(v, c);
    }
    return have == need ? n : -1;
}

/* ---- 새 파일·폴더 만들기 (2.9, 머신 편집기 저장) ---- */
static int wr_sector(FatVol *v, uint32_t lba, const uint8_t *b)
{
    if (v->write(v->ctx, lba, 1, b)) return -1;
    if (v->cache_lba == lba) memcpy(v->cache, b, 512);
    return 0;
}

static uint8_t lfn_sum(const uint8_t *sn)
{
    uint8_t s = 0;
    for (int i = 0; i < 11; i++) s = (uint8_t)(((s & 1) << 7) + (s >> 1) + sn[i]);
    return s;
}

/* 디렉터리 dclus의 모든 항목 자리를 차례로 (lba, 위치). 끝까지(0x00 뒤도) 센다 */
#define SLOT_MAX 4096
static uint32_t slot_lba[SLOT_MAX];
static uint16_t slot_off[SLOT_MAX];
static int dir_slots(FatVol *v, uint32_t dclus, uint32_t *last_clus)
{
    int n = 0;
    *last_clus = 0;
    if (!dclus && !v->fat32) {
        for (uint32_t s = 0; s < v->root_secs; s++)
            for (int i = 0; i < 16 && n < SLOT_MAX; i++) { slot_lba[n] = v->root_lba + s; slot_off[n++] = (uint16_t)(i * 32); }
        return n;
    }
    if (!dclus) dclus = v->root_clus;
    for (uint32_t c = dclus; !end_cluster(v, c); c = next_cluster(v, c)) {
        *last_clus = c;
        for (uint32_t s = 0; s < v->spc; s++)
            for (int i = 0; i < 16 && n < SLOT_MAX; i++) { slot_lba[n] = cluster_lba(v, c) + s; slot_off[n++] = (uint16_t)(i * 32); }
        if (n >= SLOT_MAX) break;
    }
    return n;
}

static int zero_cluster(FatVol *v, uint32_t c)
{
    static uint8_t z[512];
    memset(z, 0, 512);
    for (uint32_t s = 0; s < v->spc; s++) if (wr_sector(v, cluster_lba(v, c) + s, z)) return -1;
    return 0;
}

static void put16(uint8_t *p, uint32_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }

int fat_create(FatVol *v, const char *path, int is_dir)
{
    lc_drop();
    FatEntry f, par;
    if (!v->write) return -1;
    if (!resolve(v, path, &f)) return f.is_dir == (is_dir != 0) ? 0 : -1;    /* 이미 있음 */
    /* 부모 경로와 이름 */
    int len = (int)strlen(path), cut = len;
    while (cut > 0 && path[cut - 1] != '\\' && path[cut - 1] != '/') cut--;
    const char *name = path + cut;
    if (!*name || cut >= 1024) return -1;
    char ppath[1024];
    memcpy(ppath, path, (size_t)cut); ppath[cut] = 0;
    if (resolve(v, ppath, &par) || !par.is_dir) return -1;
    uint32_t pclus = par.clus;
    if (pclus == v->root_clus && v->fat32) pclus = 0;          /* 최상위 */
    /* UTF-8 → UTF-16 */
    uint16_t u[256];
    int ul = 0;
    for (const uint8_t *p = (const uint8_t *)name; *p && ul < 255; ) {
        uint32_t c = *p++;
        if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((uint32_t)(p[0] & 63) << 6) | (p[1] & 63); p += 2; }
        else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
        u[ul++] = (uint16_t)c;
    }
    /* 짧은 이름: 영문·숫자만 대문자로, "이름~N.EXT" */
    uint8_t sn[11];
    memset(sn, ' ', 11);
    int dot = -1;
    for (int i = 0; name[i]; i++) if (name[i] == '.') dot = i;
    if (dot == 0) dot = -1;
    int bl = 0;
    for (int i = 0; name[i] && (dot < 0 || i < dot) && bl < 6; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) sn[bl++] = (uint8_t)c;
    }
    if (!bl) sn[bl++] = 'X';
    if (dot >= 0) for (int i = dot + 1, k = 8; name[i] && k < 11; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) sn[k++] = (uint8_t)c;
    }
    uint32_t last;
    int ns = dir_slots(v, pclus, &last);
    static uint8_t b[512];
    for (int t = 1; t < 1000; t++) {                            /* 겹치지 않는 ~N (5.0.175: 99 를 넘으면 ~NNN 까지 — 버전별 로그가 쌓여도 새 파일이 만들어지게) */
        uint8_t c[11];
        memcpy(c, sn, 11);
        int p = bl < 6 ? bl : 6;
        if (t >= 100) { if (p > 4) p = 4; c[p] = '~'; c[p + 1] = (uint8_t)('0' + t / 100); c[p + 2] = (uint8_t)('0' + (t / 10) % 10); c[p + 3] = (uint8_t)('0' + t % 10); }
        else if (t >= 10) { if (p > 5) p = 5; c[p] = '~'; c[p + 1] = (uint8_t)('0' + t / 10); c[p + 2] = (uint8_t)('0' + t % 10); }
        else { c[p] = '~'; c[p + 1] = (uint8_t)('0' + t); }
        int dup = 0;
        for (int i = 0; i < ns && !dup; i++) {
            const uint8_t *s = sector(v, slot_lba[i]);
            if (!s) return -1;
            const uint8_t *e = s + slot_off[i];
            if (e[0] == 0) break;
            if (e[0] != 0xE5 && e[11] != 0x0F && !memcmp(e, c, 11)) dup = 1;
        }
        if (!dup) { memcpy(sn, c, 11); break; }
        if (t == 999) return -1;
    }
    int nl = (ul + 12) / 13, need = nl + 1;
    /* 빈 자리 need개 연속 (0x00 뒤는 모두 빈 자리) */
    int start = -1, run = 0, ended = 0;
    for (int i = 0; i < ns; i++) {
        int fr;
        if (ended) fr = 1;
        else {
            const uint8_t *s = sector(v, slot_lba[i]);
            if (!s) return -1;
            uint8_t e0 = s[slot_off[i]];
            if (e0 == 0) ended = 1;
            fr = e0 == 0 || e0 == 0xE5;
        }
        if (fr) { if (!run) start = i; if (++run == need) break; }
        else run = 0;
    }
    if (run < need) {                                           /* 폴더 늘리기 (FAT16 최상위는 못 늘림) */
        if (!pclus && !v->fat32) return -1;
        uint32_t eoc = v->fat32 ? 0x0FFFFFFFu : 0xFFFFu;
        uint32_t c = find_free(v, 2);
        if (!c || fat_set(v, c, eoc) || fat_set(v, last, c) || zero_cluster(v, c)) return -1;
        ns = dir_slots(v, pclus, &last);
        /* run이 0이면 새 클러스터 처음부터, 아니면 이어서 */
        if (!run) start = ns - (int)(v->spc * 16);
        if (start + need > ns) return -1;
    }
    /* 폴더면 클러스터 하나와 . .. */
    uint32_t nc = 0;
    if (is_dir) {
        uint32_t eoc = v->fat32 ? 0x0FFFFFFFu : 0xFFFFu;
        nc = find_free(v, 2);
        if (!nc || fat_set(v, nc, eoc) || zero_cluster(v, nc)) return -1;
        memset(b, 0, 512);
        memset(b, ' ', 11); b[0] = '.'; b[11] = 0x10; put16(b + 26, nc); put16(b + 20, nc >> 16);
        memset(b + 32, ' ', 11); b[32] = '.'; b[33] = '.'; b[43] = 0x10; put16(b + 58, pclus); put16(b + 52, pclus >> 16);
        if (wr_sector(v, cluster_lba(v, nc), b)) return -1;
    }
    /* 항목 쓰기: 긴 이름 조각(뒤 조각부터) + 짧은 이름 */
    static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    uint8_t sum = lfn_sum(sn);
    for (int k = 0; k < need; k++) {
        int i = start + k;
        const uint8_t *s = sector(v, slot_lba[i]);
        if (!s) return -1;
        memcpy(b, s, 512);
        uint8_t *e = b + slot_off[i];
        memset(e, 0, 32);
        if (k < nl) {
            int seq = nl - k;                                   /* nl, nl-1, ..., 1 */
            e[0] = (uint8_t)(seq | (k == 0 ? 0x40 : 0));
            e[11] = 0x0F; e[13] = sum;
            for (int j = 0; j < 13; j++) {
                int ci = (seq - 1) * 13 + j;
                uint16_t ch = ci < ul ? u[ci] : ci == ul ? 0 : 0xFFFF;
                put16(e + pos[j], ch);
            }
        } else {
            memcpy(e, sn, 11);
            e[11] = is_dir ? 0x10 : 0x20;
            uint16_t dd, tt;
            fat_now(&dd, &tt);
            put16(e + 14, tt); put16(e + 16, dd);               /* 만든 시각·날짜 */
            put16(e + 22, tt); put16(e + 24, dd);               /* 고친 시각·날짜 */
            put16(e + 18, dd);
            put16(e + 26, nc); put16(e + 20, nc >> 16);
        }
        if (wr_sector(v, slot_lba[i], b)) return -1;
    }
    return 0;
}

/* 3.0.2: 큰 파일 만들기 (하드 디스크 이미지). 이미 만든 빈 파일(fat_create)에 len 바이트만큼 클러스터를 이어 붙이고,
 * 앞부분은 prefix로 채운다(나머지 데이터는 쓰지 않음 — 빈 자리의 옛 내용이 남지만 이미지 안에서는 빈 영역).
 * 빈 클러스터를 먼저 구간(run)으로 모은 뒤 FAT 섹터마다 한 번씩 고쳐 쓴다. 성공 0 */
int fat_alloc_file(FatVol *v, const char *path, uint32_t len, const uint8_t *prefix, uint32_t plen, void (*progress)(int pct))
{
    lc_drop();
    FatEntry f;
    if (!v->write || resolve(v, path, &f) || f.is_dir || f.clus) return -1;
    uint32_t csize = v->spc * 512, need = (len + csize - 1) / csize, eoc = v->fat32 ? 0x0FFFFFFFu : 0xFFFFu;
    if (!need) return -1;
    #define RUNS 2048
    static uint32_t rs[RUNS], rl[RUNS];
    static uint8_t t[512];
    int nr = 0;
    uint32_t got = 0, per = 512 / (v->fat32 ? 4 : 2);
    /* 1. 빈 클러스터 모으기 */
    for (uint32_t s = 0; s < v->fatsz && got < need; s++) {
        if (v->read(v->ctx, v->fat_lba + s, 1, t)) return -1;
        for (uint32_t i = 0; i < per && got < need; i++) {
            uint32_t c = s * per + i;
            if (c < 2 || c >= v->clusters + 2) continue;
            uint32_t val = v->fat32 ? ((uint32_t)t[i * 4] | (uint32_t)t[i * 4 + 1] << 8 | (uint32_t)t[i * 4 + 2] << 16 | (uint32_t)(t[i * 4 + 3] & 0x0F) << 24)
                                    : ((uint32_t)t[i * 2] | (uint32_t)t[i * 2 + 1] << 8);
            if (val) continue;
            if (nr && rs[nr - 1] + rl[nr - 1] == c) rl[nr - 1]++;
            else { if (nr >= RUNS) return -1; rs[nr] = c; rl[nr] = 1; nr++; }
            got++;
        }
        if (progress && (s & 63) == 0) progress((int)(got * 30ull / need));
    }
    if (got < need) return -1;                              /* 자리 없음 */
    if (v->fat32 && v->fsinfo_lba && !v->fsinfo_done) {     /* FSInfo 빈 수를 '모름'으로 */
        v->fsinfo_done = 1;
        if (!v->read(v->ctx, v->fsinfo_lba, 1, t) && t[0] == 'R' && t[1] == 'R' && t[484] == 'r') { memset(t + 488, 0xFF, 8); v->write(v->ctx, v->fsinfo_lba, 1, t); }
    }
    /* 2. FAT 쓰기 (섹터마다 한 번, 사본 모두) */
    uint32_t cur = 0xFFFFFFFFu, done = 0;
    for (int r = 0; r < nr; r++)
        for (uint32_t k = 0; k < rl[r]; k++) {
            uint32_t c = rs[r] + k, nx = k + 1 < rl[r] ? c + 1 : r + 1 < nr ? rs[r + 1] : eoc;
            uint32_t sec = c / per, off = c % per;
            if (sec != cur) {
                if (cur != 0xFFFFFFFFu) for (uint32_t m = 0; m < v->nfats; m++) if (v->write(v->ctx, v->fat_lba + m * v->fatsz + cur, 1, t)) return -1;
                cur = sec;
                if (v->read(v->ctx, v->fat_lba + cur, 1, t)) return -1;
            }
            if (v->fat32) { uint32_t o = off * 4; t[o] = (uint8_t)nx; t[o + 1] = (uint8_t)(nx >> 8); t[o + 2] = (uint8_t)(nx >> 16); t[o + 3] = (uint8_t)((t[o + 3] & 0xF0) | ((nx >> 24) & 0x0F)); }
            else { t[off * 2] = (uint8_t)nx; t[off * 2 + 1] = (uint8_t)(nx >> 8); }
            if (progress && (++done & 4095) == 0) progress(30 + (int)(done * 60ull / need));
        }
    if (cur != 0xFFFFFFFFu) for (uint32_t m = 0; m < v->nfats; m++) if (v->write(v->ctx, v->fat_lba + m * v->fatsz + cur, 1, t)) return -1;
    v->cache_lba = 0xFFFFFFFFu;
    /* 3. 앞부분 데이터 */
    uint32_t pd = 0;
    for (int r = 0; r < nr && pd < plen; r++)
        for (uint32_t k = 0; k < rl[r] && pd < plen; k++) {
            uint32_t lba = cluster_lba(v, rs[r] + k);
            for (uint32_t s2 = 0; s2 < v->spc && pd < plen; s2++, pd += 512) {
                uint32_t n = plen - pd < 512 ? plen - pd : 512;
                memset(t, 0, 512); memcpy(t, prefix + pd, n);
                if (v->write(v->ctx, lba + s2, 1, t)) return -1;
            }
        }
    /* 4. 디렉터리 항목: 크기·첫 클러스터 */
    if (v->read(v->ctx, f.ent_lba, 1, t)) return -1;
    uint8_t *e = t + f.ent_off;
    e[28] = (uint8_t)len; e[29] = (uint8_t)(len >> 8); e[30] = (uint8_t)(len >> 16); e[31] = (uint8_t)(len >> 24);
    e[26] = (uint8_t)rs[0]; e[27] = (uint8_t)(rs[0] >> 8);
    if (v->fat32) { e[20] = (uint8_t)(rs[0] >> 16); e[21] = (uint8_t)(rs[0] >> 24); }
    if (v->write(v->ctx, f.ent_lba, 1, t)) return -1;
    v->cache_lba = 0xFFFFFFFFu;
    if (progress) progress(100);
    return 0;
    #undef RUNS
}

/* 3.0.2: 파일 지우기 (3.6.4: 빈 폴더도). 긴 이름 조각과 짧은 이름 항목을 E5로, 클러스터 사슬을 풂 */
int fat_delete(FatVol *v, const char *path)
{
    lc_drop();
    FatEntry f, d;
    if (!v->write || resolve(v, path, &f)) return -1;
    if (f.is_dir) {                                     /* 3.6.4: 빈 폴더도 (., .., 지운 항목, 긴 이름 조각만 있을 때) */
        if (f.clus < 2) return -1;
        DirIter di;
        dir_open(v, f.clus, &di);
        const uint8_t *x;
        while ((x = dir_next(&di)) != 0) {
            if (x[0] == 0xE5 || x[11] == 0x0F || (x[11] & 0x08)) continue;
            if (x[0] == '.' && (x[1] == ' ' || (x[1] == '.' && x[2] == ' '))) continue;
            return -1;                                  /* 비지 않음 */
        }
    }
    char parent[1024];
    int n = 0, last = -1;
    while (path[n] && n < 1023) { parent[n] = path[n]; if (path[n] == '\\' || path[n] == '/') last = n; n++; }
    parent[last > 0 ? last : 0] = 0;
    if (resolve(v, parent, &d) || !d.is_dir) return -1;
    DirIter it;
    dir_open(v, d.clus, &it);
    uint32_t pl[21], po[21];
    int np = 0, found = 0;
    const uint8_t *e;
    while ((e = dir_next(&it)) != 0) {
        uint32_t lba = it.lba, off = (uint32_t)(it.idx - 1) * 32;
        if (lba == f.ent_lba && off == f.ent_off) {
            if (np < 21) { pl[np] = lba; po[np] = off; np++; }
            found = 1;
            break;
        }
        if (e[0] != 0xE5 && e[11] == 0x0F) {
            if (e[0] & 0x40) np = 0;                    /* 새 긴 이름 시작 */
            if (np < 20) { pl[np] = lba; po[np] = off; np++; }
        } else np = 0;
    }
    if (!found) return -1;
    static uint8_t t[512];
    for (int i = 0; i < np; i++) {                      /* 같은 섹터끼리 한 번에 */
        if (i && pl[i] == pl[i - 1]) continue;
        if (v->read(v->ctx, pl[i], 1, t)) return -1;
        for (int j = i; j < np && pl[j] == pl[i]; j++) t[po[j]] = 0xE5;
        if (wr_sector(v, pl[i], t)) return -1;
    }
    uint32_t c = f.clus, guard = 0;
    while (!end_cluster(v, c) && guard++ < v->clusters) { uint32_t nx = next_cluster(v, c); if (fat_set(v, c, 0)) return -1; c = nx; }
    return 0;
}
