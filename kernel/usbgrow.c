/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * usbgrow.c - 첫 FAT32 파티션을 디스크 전체 크기로 다시 만든다 (파일 보존)
 *
 * 순서: ① MBR에서 첫 파티션 확인 ② 파일 트리를 메모리로 (fat.c로 읽기)
 *       ③ 새 FAT32 크기 계산 ④ 예약 영역(BPB, FSInfo, 백업) ⑤ FAT 2개 ⑥ 폴더·파일 데이터
 *       ⑦ 마지막으로 MBR 파티션 항목 (부트 코드와 LBA 1~2047 로더는 건드리지 않음)
 * 폴더와 파일은 연속 클러스터로 배치하고, 이름은 긴 이름(VFAT) + 8.3 짧은 이름으로 쓴다.
 */
#include "usbgrow.h"
#include "fat.h"
#include <string.h>

#define MAX_NODES   4096
#define CHUNK       64                      /* 한 번에 쓰는 섹터 수 */

typedef struct {
    const char *name;                       /* UTF-8 (3.4.7: io->mem 끝쪽 이름 자리, 긴 이름 255자까지) */
    int parent, is_dir;
    uint32_t size;
    uint64_t data;                          /* io->mem 안의 위치 */
    uint16_t date, time;
    uint32_t first, nclus;                  /* 새 파일 시스템의 첫 클러스터, 클러스터 수 */
    uint8_t sfn[11];
} Node;

static Node nodes[MAX_NODES];
static int nnodes;
static uint64_t mem_lim;                    /* 3.4.7: 파일 데이터가 쓸 수 있는 끝 (그 뒤는 이름) */
static uint8_t zero[CHUNK * 512];
static uint8_t buf[CHUNK * 512];

static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

static void set_err(char *e, int n, const char *s)
{
    int i = 0;
    for (; s[i] && i < n - 1; i++) e[i] = s[i];
    e[i] = 0;
}

static GrowIO *IO;
static int rd(void *ctx, uint64_t lba, uint32_t count, uint8_t *b) { (void)ctx; return IO->read(IO->ctx, lba, count, b); }

/* 여러 섹터 쓰기 (64섹터씩) */
static int write_secs(uint64_t lba, uint64_t n, const uint8_t *data)
{
    while (n) {
        uint32_t k = n > CHUNK ? CHUNK : (uint32_t)n;
        if (IO->write(IO->ctx, lba, k, data)) return -1;
        lba += k; n -= k;
        if (data != zero) data += (uint64_t)k * 512;
    }
    return 0;
}

static int write_zero(uint64_t lba, uint64_t n)
{
    while (n) {
        uint32_t k = n > CHUNK ? CHUNK : (uint32_t)n;
        if (IO->write(IO->ctx, lba, k, zero)) return -1;
        lba += k; n -= k;
    }
    return 0;
}

uint64_t usbgrow_part_sectors(GrowIO *io)
{
    static uint8_t mbr[512];
    if (io->read(io->ctx, 0, 1, mbr) || mbr[510] != 0x55 || mbr[511] != 0xAA) return 0;
    return rd32(mbr + 446 + 12);
}

/* ------------------------------------------------------------------ */
/* ② 파일 트리 읽기                                                     */
/* ------------------------------------------------------------------ */
static FatEntry tmp[1024];

static int read_tree(FatVol *v, int parent, const char *path, uint64_t *used, char *err, int errlen)
{
    int n = fat_list(v, path, tmp, 1024);
    if (n < 0) { set_err(err, errlen, "cannot read folder"); return -1; }
    int first = nnodes;
    for (int i = 0; i < n; i++) {
        if (nnodes >= MAX_NODES) { set_err(err, errlen, "too many files"); return -1; }
        Node *d = &nodes[nnodes++];
        memset(d, 0, sizeof *d);
        uint32_t nl = (uint32_t)strlen(tmp[i].key) + 1;
        if (*used + nl > mem_lim) { set_err(err, errlen, "not enough memory for files"); return -1; }
        mem_lim -= nl;
        memcpy(IO->mem + mem_lim, tmp[i].key, nl);
        d->name = (const char *)(IO->mem + mem_lim);
        d->parent = parent;
        d->is_dir = tmp[i].is_dir;
        d->size = tmp[i].is_dir ? 0 : tmp[i].size;
        if (tmp[i].year >= 1980) {
            d->date = (uint16_t)(((tmp[i].year - 1980) << 9) | (tmp[i].mon << 5) | tmp[i].day);
            d->time = (uint16_t)((tmp[i].hour << 11) | (tmp[i].min << 5));
        } else { d->date = (uint16_t)((46 << 9) | (10 << 5) | 1); d->time = 0; }   /* 2026-10-01 */
    }
    int last = nnodes;
    for (int k = first; k < last; k++) {                /* 파일 데이터 */
        Node *d = &nodes[k];
        char p[1024];
        int len = 0;
        for (const char *s = path; *s && len < 1020; s++) p[len++] = *s;
        p[len++] = '\\';
        for (const char *s = d->name; *s && len < 1022; s++) p[len++] = *s;
        p[len] = 0;
        if (d->is_dir) {
            if (read_tree(v, k, p, used, err, errlen)) return -1;
            continue;
        }
        if (*used + d->size > mem_lim) { set_err(err, errlen, "not enough memory for files"); return -1; }
        uint32_t got = 0;
        if (d->size && fat_read_file(v, p, IO->mem + *used, d->size, &got)) { set_err(err, errlen, "cannot read file"); return -1; }
        d->data = *used;
        *used += (d->size + 15) & ~15u;
        if (IO->progress && (k & 15) == 0) IO->progress(5 + (int)(*used * 25 / (IO->mem_size ? IO->mem_size : 1)) % 25, "read");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 이름: 8.3 짧은 이름 + 긴 이름                                        */
/* ------------------------------------------------------------------ */
static int sfn_char(uint32_t c)
{
    if (c >= 'a' && c <= 'z') return (int)(c - 32);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (int)c;
    if (c < 0x80 && strchr("$%'-_@~`!(){}^#&", (int)c)) return (int)c;
    return 0;
}

static uint32_t utf8_cp(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((uint32_t)(p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

/* 짧은 이름 만들기. 원래 이름과 똑같이 표현되면(대문자 8.3) 긴 이름이 필요 없다 → 반환 0 */
static int make_sfn(Node *d, int dir_first, int dir_last)
{
    uint8_t base[8], ext[3];
    int nb = 0, ne = 0, lossy = 0;
    memset(base, ' ', 8); memset(ext, ' ', 3);
    const char *dot = 0;
    for (const char *s = d->name; *s; s++) if (*s == '.') dot = s;
    if (dot == d->name) dot = 0;                    /* ".abc" 같은 이름 */
    const char *s = d->name;
    while (*s && s != dot) {
        uint32_t c = utf8_cp(&s);
        if (c == ' ' || c == '.') { lossy = 1; continue; }
        int k = sfn_char(c);
        if (!k) { lossy = 1; k = '_'; }
        if (k != (int)c) lossy = 1;
        if (nb < 8) base[nb++] = (uint8_t)k; else lossy = 1;
    }
    if (dot) {
        s = dot + 1;
        while (*s) {
            uint32_t c = utf8_cp(&s);
            int k = sfn_char(c);
            if (!k) { lossy = 1; k = '_'; }
            if (k != (int)c) lossy = 1;
            if (ne < 3) ext[ne++] = (uint8_t)k; else lossy = 1;
        }
    }
    if (!nb) { base[0] = '_'; nb = 1; lossy = 1; }
    memcpy(d->sfn, base, 8);
    memcpy(d->sfn + 8, ext, 3);
    for (int n = 1; n < 1000000; n++) {
        if (lossy) {                                /* NAME~N */
            char num[8]; int k = 0, v = n;
            do { num[k++] = (char)('0' + v % 10); v /= 10; } while (v);
            int keep = 8 - 1 - k;
            if (keep > nb) keep = nb;
            memcpy(d->sfn, base, 8);
            for (int i = keep; i < 8; i++) d->sfn[i] = ' ';
            d->sfn[keep] = '~';
            for (int i = 0; i < k; i++) d->sfn[keep + 1 + i] = (uint8_t)num[k - 1 - i];
        }
        int clash = 0;
        for (int j = dir_first; j < dir_last; j++)
            if (&nodes[j] != d && nodes[j].parent == d->parent && nodes[j].sfn[0] && !memcmp(nodes[j].sfn, d->sfn, 11)) clash = 1;
        if (!clash) break;
        lossy = 1;
    }
    return lossy;
}

static uint8_t sfn_sum(const uint8_t *n)
{
    uint8_t s = 0;
    for (int i = 0; i < 11; i++) s = (uint8_t)(((s & 1) ? 0x80 : 0) + (s >> 1) + n[i]);
    return s;
}

/* 이 노드가 차지하는 디렉터리 항목 수 */
static int entry_count(Node *d, int lfn)
{
    if (!lfn) return 1;
    int units = 0;
    const char *s = d->name;
    while (*s) { utf8_cp(&s); units++; }
    return 1 + (units + 12) / 13;
}

static uint8_t *put_entries(uint8_t *e, Node *d, int lfn)
{
    if (lfn) {
        uint16_t u[260];
        int n = 0;
        const char *s = d->name;
        while (*s && n < 255) u[n++] = (uint16_t)utf8_cp(&s);
        int parts = (n + 12) / 13;
        uint8_t sum = sfn_sum(d->sfn);
        static const uint8_t pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
        for (int p = parts; p >= 1; p--) {
            memset(e, 0, 32);
            e[0] = (uint8_t)(p | (p == parts ? 0x40 : 0));
            e[11] = 0x0F;
            e[13] = sum;
            for (int j = 0; j < 13; j++) {
                int idx = (p - 1) * 13 + j;
                uint16_t c = idx < n ? u[idx] : (idx == n ? 0x0000 : 0xFFFF);
                wr16(e + pos[j], c);
            }
            e += 32;
        }
    }
    memset(e, 0, 32);
    memcpy(e, d->sfn, 11);
    if (e[0] == 0xE5) e[0] = 0x05;
    e[11] = d->is_dir ? 0x10 : 0x20;
    wr16(e + 14, d->time); wr16(e + 16, d->date); wr16(e + 18, d->date);
    wr16(e + 22, d->time); wr16(e + 24, d->date);
    wr16(e + 20, (uint16_t)(d->first >> 16));
    wr16(e + 26, (uint16_t)d->first);
    wr32(e + 28, d->size);
    return e + 32;
}

/* ------------------------------------------------------------------ */
/* 실행                                                                 */
/* ------------------------------------------------------------------ */
int usbgrow_run(GrowIO *io, char *err, int errlen)
{
    IO = io;
    static uint8_t mbr[512];
    if (io->read(io->ctx, 0, 1, mbr) || mbr[510] != 0x55 || mbr[511] != 0xAA) { set_err(err, errlen, "cannot read MBR"); return -1; }
    uint8_t *pe = mbr + 446;
    uint32_t start = rd32(pe + 8), old_size = rd32(pe + 12);
    if (pe[4] != 0x0B && pe[4] != 0x0C) { set_err(err, errlen, "first partition is not FAT32"); return -1; }
    if (start < 2048 || start >= io->disk_sectors) { set_err(err, errlen, "unexpected partition layout"); return -1; }
    uint64_t total = io->disk_sectors - start;
    if (total > 0xFFFFFFFFull) total = 0xFFFFFFFFull - 7;      /* MBR 한계 2 TiB */
    total &= ~7ull;
    if (total <= (uint64_t)old_size + 2048) return 1;           /* 이미 전체 크기 */

    /* ② 파일 읽기 */
    if (io->progress) io->progress(2, "read");
    static FatVol vols[4];
    int nv = fat_scan_disk(rd, 0, vols, 4);
    if (nv < 1 || vols[0].part_lba != start) { set_err(err, errlen, "cannot mount FAT32"); return -1; }
    nnodes = 0;
    mem_lim = io->mem_size;
    uint64_t used = 0;
    if (read_tree(&vols[0], -1, "", &used, err, errlen)) return -1;
    char label[12];
    memcpy(label, "X86MSX     ", 12);
    if (vols[0].label[0]) { memset(label, ' ', 11); for (int i = 0; i < 11 && vols[0].label[i]; i++) label[i] = vols[0].label[i]; label[11] = 0; }

    /* ③ 새 크기 */
    uint32_t spc = total <= (8ull << 21) ? 8 : total <= (16ull << 21) ? 16 : total <= (32ull << 21) ? 32 : 64;
    uint32_t rsvd = 32, fatsz = 0, clusters = 0;
    for (;;) {
        uint64_t t1 = total - rsvd, t2 = (256ull * spc + 2) / 2;
        fatsz = (uint32_t)((t1 + t2 - 1) / t2);
        clusters = (uint32_t)((total - rsvd - 2ull * fatsz) / spc);
        if (clusters >= 65525 || spc == 1) break;
        spc /= 2;                                               /* 작은 디스크: 클러스터를 줄여 FAT32 유지 */
    }
    if (clusters < 65525) { set_err(err, errlen, "disk too small for FAT32"); return -1; }
    uint32_t cbytes = spc * 512;
    uint64_t fat_lba = start + rsvd, data_lba = fat_lba + 2ull * fatsz;

    /* 짧은 이름, 클러스터 배치: 루트(2) → 폴더들 → 파일들 */
    for (int i = 0; i < nnodes; i++) nodes[i].sfn[0] = 0;
    static uint8_t lfn_need[MAX_NODES];
    for (int i = 0; i < nnodes; i++) lfn_need[i] = (uint8_t)make_sfn(&nodes[i], 0, nnodes);
    uint32_t next = 2;
    int root_entries = 1;                                       /* 볼륨 이름 */
    for (int i = 0; i < nnodes; i++) if (nodes[i].parent < 0) root_entries += entry_count(&nodes[i], lfn_need[i]);
    uint32_t root_clus = (uint32_t)(((uint64_t)root_entries * 32 + cbytes - 1) / cbytes);
    next += root_clus;
    for (int i = 0; i < nnodes; i++) {
        if (!nodes[i].is_dir) continue;
        int ents = 2;
        for (int j = 0; j < nnodes; j++) if (nodes[j].parent == i) ents += entry_count(&nodes[j], lfn_need[j]);
        nodes[i].nclus = (uint32_t)(((uint64_t)ents * 32 + cbytes - 1) / cbytes);
        nodes[i].first = next;
        next += nodes[i].nclus;
    }
    for (int i = 0; i < nnodes; i++) {
        if (nodes[i].is_dir) continue;
        nodes[i].nclus = (uint32_t)(((uint64_t)nodes[i].size + cbytes - 1) / cbytes);
        nodes[i].first = nodes[i].nclus ? next : 0;
        next += nodes[i].nclus;
    }
    if (next - 2 >= clusters) { set_err(err, errlen, "files do not fit"); return -1; }
    uint64_t dir_bytes_max = 0;
    for (int i = 0; i < nnodes; i++) if (nodes[i].is_dir && (uint64_t)nodes[i].nclus * cbytes > dir_bytes_max) dir_bytes_max = (uint64_t)nodes[i].nclus * cbytes;
    if ((uint64_t)root_clus * cbytes > dir_bytes_max) dir_bytes_max = (uint64_t)root_clus * cbytes;
    if (used + dir_bytes_max > mem_lim) { set_err(err, errlen, "not enough memory"); return -1; }
    uint8_t *dirbuf = io->mem + used;

    /* ④ 예약 영역 */
    if (io->progress) io->progress(30, "format");
    static uint8_t rs[32 * 512];
    memset(rs, 0, sizeof rs);
    uint8_t *b = rs;
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
    memcpy(b + 3, "MSXEMU  ", 8);
    wr16(b + 11, 512); b[13] = (uint8_t)spc; wr16(b + 14, (uint16_t)rsvd); b[16] = 2;
    b[21] = 0xF8; wr16(b + 24, 63); wr16(b + 26, 255);
    wr32(b + 28, start); wr32(b + 32, (uint32_t)total); wr32(b + 36, fatsz);
    wr32(b + 44, 2); wr16(b + 48, 1); wr16(b + 50, 6);
    b[64] = 0x80; b[66] = 0x29; wr32(b + 67, (uint32_t)(total ^ 0x4D53584Du));
    memcpy(b + 71, label, 11); memcpy(b + 82, "FAT32   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    uint8_t *fi = rs + 512;
    wr32(fi, 0x41615252); wr32(fi + 484, 0x61417272);
    wr32(fi + 488, clusters - (next - 2)); wr32(fi + 492, next);
    wr32(fi + 508, 0xAA550000);
    memcpy(rs + 6 * 512, rs, 1024);                             /* 백업 (6, 7) */
    if (write_secs(start, rsvd, rs)) { set_err(err, errlen, "write error (boot sector)"); return -1; }
    { static uint8_t chk0[512]; if (io->read(io->ctx, start, 1, chk0) || memcmp(chk0, rs, 512)) { set_err(err, errlen, "boot sector verify failed"); return -1; } }   /* 5.2.154 */

    /* ⑤ FAT 2개 */
    uint64_t used_fat_secs = ((uint64_t)next * 4 + 511) / 512;
    for (uint64_t s = 0; s < fatsz; s += CHUNK) {
        uint32_t k = fatsz - s > CHUNK ? CHUNK : (uint32_t)(fatsz - s);
        const uint8_t *src = zero;
        if (s < used_fat_secs) {
            memset(buf, 0, sizeof buf);
            for (uint32_t i = 0; i < k * 128; i++) {
                uint32_t c = (uint32_t)(s * 128 + i), v = 0;
                if (c == 0) v = 0x0FFFFFF8;
                else if (c == 1) v = 0x0FFFFFFF;
                else if (c < next) {
                    v = c + 1;                                  /* 연속 배치: 다음 클러스터 */
                    if (c == 2 + root_clus - 1) v = 0x0FFFFFFF;
                    for (int j = 0; j < nnodes; j++)
                        if (nodes[j].nclus && c == nodes[j].first + nodes[j].nclus - 1) { v = 0x0FFFFFFF; break; }
                }
                wr32(buf + i * 4, v);
            }
            src = buf;
        }
        if (IO->write(IO->ctx, fat_lba + s, k, src) || IO->write(IO->ctx, fat_lba + fatsz + s, k, src)) {
            set_err(err, errlen, "write error (FAT)"); return -1;
        }
        if (io->progress && (s & 1023) == 0) io->progress(30 + (int)(s * 40 / fatsz), "FAT");
    }

    /* ⑥ 폴더와 파일 */
    if (io->progress) io->progress(70, "files");
    for (int d = -1; d < nnodes; d++) {                         /* d = -1: 루트 */
        if (d >= 0 && !nodes[d].is_dir) continue;
        uint32_t nclus = d < 0 ? root_clus : nodes[d].nclus, first = d < 0 ? 2 : nodes[d].first;
        memset(dirbuf, 0, (uint64_t)nclus * cbytes);
        uint8_t *e = dirbuf;
        if (d < 0) { memcpy(e, label, 11); e[11] = 0x08; e += 32; }
        else {
            Node dot;
            memset(&dot, 0, sizeof dot);
            dot.is_dir = 1; dot.date = nodes[d].date; dot.time = nodes[d].time;
            memset(dot.sfn, ' ', 11); dot.sfn[0] = '.'; dot.first = nodes[d].first;
            e = put_entries(e, &dot, 0);
            dot.sfn[1] = '.';
            dot.first = nodes[d].parent < 0 ? 0 : nodes[nodes[d].parent].first;
            e = put_entries(e, &dot, 0);
        }
        for (int j = 0; j < nnodes; j++) if (nodes[j].parent == d) e = put_entries(e, &nodes[j], lfn_need[j]);
        if (write_secs(data_lba + (uint64_t)(first - 2) * spc, (uint64_t)nclus * spc, dirbuf)) { set_err(err, errlen, "write error (folder)"); return -1; }
    }
    for (int i = 0; i < nnodes; i++) {
        Node *n = &nodes[i];
        if (n->is_dir || !n->nclus) continue;
        uint64_t lba = data_lba + (uint64_t)(n->first - 2) * spc;
        uint32_t whole = n->size / 512, rest = n->size % 512;
        if (whole && write_secs(lba, whole, io->mem + n->data)) { set_err(err, errlen, "write error (file)"); return -1; }
        uint64_t written = whole;
        if (rest) {
            memset(buf, 0, 512);
            memcpy(buf, io->mem + n->data + (uint64_t)whole * 512, rest);
            if (IO->write(IO->ctx, lba + whole, 1, buf)) { set_err(err, errlen, "write error (file)"); return -1; }
            written++;
        }
        if (written < (uint64_t)n->nclus * spc && write_zero(lba + written, (uint64_t)n->nclus * spc - written)) {
            set_err(err, errlen, "write error (file)"); return -1;
        }
        if (io->progress && (i & 7) == 0) io->progress(70 + i * 25 / (nnodes ? nnodes : 1), "files");
    }

    /* ⑦ MBR 파티션 항목 (부트 코드·로더는 그대로) */
    pe[4] = 0x0C;
    pe[5] = 0xFE; pe[6] = 0xFF; pe[7] = 0xFF;                   /* 끝 CHS: 큰 디스크 */
    wr32(pe + 12, (uint32_t)total);
    if (io->progress) io->progress(97, "mbr");
    /* 쓴 뒤 다시 읽어 확인, 안 바뀌었으면 세 번까지 다시 씀 (MBR 이 안 바뀌면 새 FAT32 와 파티션 크기가 어긋나 Windows 가 손상으로 봄) */
    {
        static uint8_t chk[512];
        int ok = 0;
        for (int tries = 0; tries < 3 && !ok; tries++) {
            if (io->write(io->ctx, 0, 1, mbr)) continue;
            if (io->read(io->ctx, 0, 1, chk) == 0 && memcmp(chk, mbr, 512) == 0) ok = 1;
        }
        if (!ok) { set_err(err, errlen, "MBR was not updated (partition table write failed)"); return -1; }
    }
    if (io->progress) io->progress(100, "done");
    return 0;
}
