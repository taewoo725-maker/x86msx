/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * usbgrow.c - USB 전체 용량 사용 (5.3.8 새로 만듦)
 *
 * 예전 방식(첫 파티션 자리에서 FAT32를 다시 쓰기)은 UEFI 펌웨어가 기억하던 옛 FAT를 나중에 덮어써서 번번이 깨졌다.
 * 새 방식은 기존 파티션을 한 바이트도 고치지 않는다:
 *   ① 검사   MBR 첫 항목이 FAT32이고 나머지 항목이 비었는지, 뒤에 충분한 빈 공간이 있는지
 *   ② 목록   옛 파일 시스템의 폴더·파일 목록과 크기만 읽음 (파일 내용은 메모리에 올리지 않음)
 *   ③ 만들기 기존 파티션 바로 뒤(1 MiB 맞춤)부터 디스크 끝까지 새 FAT32: 예약 영역·FAT 2개·폴더
 *   ④ 복사   파일 내용을 옛 구역에서 새 구역으로 섹터째 복사 (연속 배치)
 *   ⑤ 확인   새 구역을 FAT32 드라이버로 다시 열어 폴더 수·파일 크기·내용을 옛것과 하나씩 비교
 *   ⑥ 전환   맨 마지막에 MBR 첫 항목만 새 구역으로 바꿈 (쓴 뒤 다시 읽어 확인, 세 번까지)
 * ⑥ 앞에서 어떤 일이 생겨도 MBR은 그대로라서 USB는 예전처럼 부팅된다. 옛 구역(63 MiB)은 쓰이지 않는 빈 자리로 남는다.
 * 앞쪽 LBA 1~2047(레거시 로더)과 MBR 부트 코드는 그대로.
 */
#include "usbgrow.h"
#include "fat.h"
#include <string.h>

#define MAX_NODES   4096
#define MAX_EXT     256                     /* 옛 파일 한 개가 쪼개져 있을 수 있는 최대 구간 수 */
#define MAX_EXT_NEW 16                      /* 새 구역은 연속 배치라 1개면 됨 */
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

/* 여러 섹터 읽기 (64섹터씩) */
static int read_secs(uint64_t lba, uint64_t n, uint8_t *data)
{
    while (n) {
        uint32_t k = n > CHUNK ? CHUNK : (uint32_t)n;
        if (IO->read(IO->ctx, lba, k, data)) return -1;
        lba += k; n -= k; data += (uint64_t)k * 512;
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
/* ② 폴더·파일 목록 (크기만, 내용은 안 읽음)                              */
/* ------------------------------------------------------------------ */
#define PAGE 64                                    /* 폴더 목록은 64개씩 쪽마다 읽음 (정적 메모리를 아끼고, 폴더 안 파일 수 제한도 없음) */
static FatEntry tmp[PAGE];
static char detail[112];
static uint64_t bytes_total, bytes_done;
const char *usbgrow_detail(void) { return detail; }
static void set_detail(const char *a, const char *b)
{
    int k = 0;
    for (; a && a[k] && k < (int)sizeof detail - 1; k++) detail[k] = a[k];
    for (int j = 0; b && b[j] && k < (int)sizeof detail - 1; j++) detail[k++] = b[j];
    detail[k] = 0;
}
static void put_num(char *d, uint32_t v)
{
    char t[12]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    int k = 0; while (d[k]) k++;
    while (n) d[k++] = t[--n];
    d[k] = 0;
}
static void say(int pct, const char *what) { if (IO->progress) IO->progress(pct, what); }

/* 폴더의 항목 수 (쪽마다 읽어 셈), 실패 -1 */
static int count_entries(FatVol *v, const char *path)
{
    int total = 0;
    for (;;) {
        int n = fat_list_skip(v, path, tmp, PAGE, total);
        if (n < 0) return -1;
        total += n;
        if (n < PAGE) return total;
    }
}

static int read_tree(FatVol *v, int parent, const char *path, char *err, int errlen)
{
    int first = nnodes;
    for (int skip = 0;; ) {
        int n = fat_list_skip(v, path, tmp, PAGE, skip);
        if (n < 0) { set_err(err, errlen, "cannot read folder"); return -1; }
        for (int i = 0; i < n; i++) {
            if (nnodes >= MAX_NODES) { set_err(err, errlen, "too many files"); return -1; }
            Node *d = &nodes[nnodes++];
            memset(d, 0, sizeof *d);
            uint32_t nl = (uint32_t)strlen(tmp[i].key) + 1;
            if (nl > mem_lim) { set_err(err, errlen, "not enough memory for names"); return -1; }
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
            if (!d->is_dir) bytes_total += d->size;
        }
        skip += n;
        if (n < PAGE) break;
    }
    int last = nnodes;
    for (int k = first; k < last; k++) {
        if (!nodes[k].is_dir) continue;
        char p[1024];
        int len = 0;
        for (const char *s = path; *s && len < 1020; s++) p[len++] = *s;
        p[len++] = '\\';
        for (const char *s = nodes[k].name; *s && len < 1022; s++) p[len++] = *s;
        p[len] = 0;
        say(5, "list");
        if (read_tree(v, k, p, err, errlen)) return -1;
    }
    return 0;
}

/* 노드의 경로 "\폴더\파일" */
static void node_path(int k, char *out, int max)
{
    int chain[32], depth = 0;
    for (int j = k; j >= 0 && depth < 32; j = nodes[j].parent) chain[depth++] = j;
    int len = 0;
    while (depth--) {
        if (len < max - 1) out[len++] = '\\';
        for (const char *s = nodes[chain[depth]].name; *s && len < max - 1; s++) out[len++] = *s;
    }
    out[len] = 0;
}

static FatExtent ext_a[MAX_EXT], ext_b[MAX_EXT_NEW];
static uint8_t cmpbuf[CHUNK * 512];

/* 같은 파일의 옛 구역(ext) → 새 구역(lba)으로 secs 섹터를 복사 */
static int copy_extents(const FatExtent *ex, int ne, uint64_t secs, uint64_t dst)
{
    uint64_t left = secs;
    for (int e = 0; e < ne && left; e++) {
        uint64_t s = ex[e].secs < left ? ex[e].secs : left, off = 0;
        while (off < s) {
            uint32_t k = s - off > CHUNK ? CHUNK : (uint32_t)(s - off);
            if (IO->read(IO->ctx, ex[e].lba + off, k, buf)) return -1;
            if (IO->write(IO->ctx, dst, k, buf)) return -2;
            dst += k; off += k;
            bytes_done += (uint64_t)k * 512;
            if (IO->progress) IO->progress(25 + (int)(bytes_done * 45 / (bytes_total ? bytes_total : 1)), "copy");
        }
        left -= s;
    }
    return left ? -1 : 0;
}

/* 두 구역의 같은 파일 내용을 비교 */
static int compare_extents(const FatExtent *a, int na, const FatExtent *b, int nb, uint64_t secs)
{
    uint64_t left = secs;
    int ia = 0, ib = 0;
    uint64_t oa = 0, ob = 0;
    while (left) {
        while (ia < na && oa >= a[ia].secs) { ia++; oa = 0; }
        while (ib < nb && ob >= b[ib].secs) { ib++; ob = 0; }
        if (ia >= na || ib >= nb) return -1;
        uint64_t k = left;
        if (k > CHUNK) k = CHUNK;
        if (k > a[ia].secs - oa) k = a[ia].secs - oa;
        if (k > b[ib].secs - ob) k = b[ib].secs - ob;
        if (IO->read(IO->ctx, a[ia].lba + oa, (uint32_t)k, buf) || IO->read(IO->ctx, b[ib].lba + ob, (uint32_t)k, cmpbuf)) return -2;
        if (memcmp(buf, cmpbuf, (size_t)k * 512)) return -3;
        oa += k; ob += k; left -= k;
        bytes_done += k * 512;
        if (IO->progress) IO->progress(71 + (int)(bytes_done * 24 / (bytes_total ? bytes_total : 1)), "verify");   /* 5.3.10: 묶음마다 알려 화면이 움직이게 */
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


static uint32_t g_cbytes;

/* FAT 한 덩어리(s0 섹터부터 k섹터)의 내용: 연속 배치라 "다음 클러스터" 아니면 끝 표시. 쓸 때와 확인할 때 같이 씀 */
static void fill_fat(uint8_t *out, uint64_t s0, uint32_t k, uint32_t next, uint32_t root_clus)
{
    uint64_t used_fat_secs = ((uint64_t)next * 4 + 511) / 512;
    memset(out, 0, (size_t)k * 512);
    if (s0 >= used_fat_secs) return;
    for (uint32_t i = 0; i < k * 128; i++) {
        uint32_t c = (uint32_t)(s0 * 128 + i), v = 0;
        if (c == 0) v = 0x0FFFFFF8;
        else if (c == 1) v = 0x0FFFFFFF;
        else if (c < next) {
            v = c + 1;
            if (c == 2 + root_clus - 1) v = 0x0FFFFFFF;
            for (int j = 0; j < nnodes; j++)
                if (nodes[j].nclus && c == nodes[j].first + nodes[j].nclus - 1) { v = 0x0FFFFFFF; break; }
        }
        wr32(out + i * 4, v);
    }
}

/* 폴더 d(-1 = 루트)의 내용을 out 에 만든다. 쓸 때와 확인할 때 같이 씀 */
static void build_dir(int d, uint8_t *out, uint32_t root_clus, const char *label, const uint8_t *lfn_need)
{
    uint32_t cbytes_n = d < 0 ? root_clus : nodes[d].nclus;
    memset(out, 0, (uint64_t)cbytes_n * g_cbytes);
    uint8_t *e = out;
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
}

int usbgrow_run(GrowIO *io, char *err, int errlen)
{
    IO = io;
    detail[0] = 0; bytes_total = bytes_done = 0;
    /* ① 검사 */
    say(2, "scan");
    static uint8_t mbr[512];
    if (io->read(io->ctx, 0, 1, mbr) || mbr[510] != 0x55 || mbr[511] != 0xAA) { set_err(err, errlen, "cannot read MBR"); return -1; }
    uint8_t *pe = mbr + 446;
    uint32_t old_start = rd32(pe + 8), old_size = rd32(pe + 12);
    if (pe[4] != 0x0B && pe[4] != 0x0C) { set_err(err, errlen, "first partition is not FAT32"); return -1; }
    for (int i = 16; i < 64; i++) if (pe[i]) { set_err(err, errlen, "this USB has other partitions"); return -1; }
    if (old_start < 2048 || (uint64_t)old_start + old_size > io->disk_sectors) { set_err(err, errlen, "unexpected partition layout"); return -1; }
    uint64_t new_start = ((uint64_t)old_start + old_size + 2047) & ~2047ull;           /* 기존 파티션 바로 뒤, 1 MiB 맞춤 */
    if (new_start >= io->disk_sectors) return 1;
    uint64_t total = io->disk_sectors - new_start;
    if (new_start + total > 0xFFFFFFFFull) total = 0xFFFFFFFFull - new_start;           /* MBR 한계 2 TiB */
    total &= ~7ull;
    if (total < (uint64_t)old_size + 262144) return 1;                                  /* 128 MiB 이상 늘지 않으면 할 일 없음 */

    /* ② 목록 */
    say(5, "list");
    static FatVol vols[4], vnew;
    int nv = fat_scan_disk(rd, 0, vols, 4);
    int vi = -1;
    for (int i = 0; i < nv; i++) if (vols[i].part_lba == old_start) vi = i;
    if (vi < 0) { set_err(err, errlen, "cannot mount FAT32"); return -1; }
    FatVol *vold = &vols[vi];
    nnodes = 0;
    mem_lim = io->mem_size;
    if (read_tree(vold, -1, "", err, errlen)) return -1;
    int nfiles = 0;
    for (int i = 0; i < nnodes; i++) if (!nodes[i].is_dir) nfiles++;
    char label[12];
    memcpy(label, "X86MSX     ", 12);
    if (vold->label[0]) { memset(label, ' ', 11); for (int i = 0; i < 11 && vold->label[i]; i++) label[i] = vold->label[i]; label[11] = 0; }

    /* ③ 새 크기와 배치 */
    uint32_t spc = total <= (8ull << 21) ? 8 : total <= (16ull << 21) ? 16 : total <= (32ull << 21) ? 32 : 64;
    uint32_t rsvd = 32, fatsz = 0, clusters = 0;
    for (;;) {
        uint64_t t1 = total - rsvd, t2 = (256ull * spc + 2) / 2;
        fatsz = (uint32_t)((t1 + t2 - 1) / t2);
        clusters = (uint32_t)((total - rsvd - 2ull * fatsz) / spc);
        if (clusters >= 65525 || spc == 1) break;
        spc /= 2;
    }
    if (clusters < 65525) { set_err(err, errlen, "disk too small for FAT32"); return -1; }
    uint32_t cbytes = spc * 512;
    g_cbytes = cbytes;
    uint64_t fat_lba = new_start + rsvd, data_lba = fat_lba + 2ull * fatsz;

    for (int i = 0; i < nnodes; i++) nodes[i].sfn[0] = 0;
    static uint8_t lfn_need[MAX_NODES];
    for (int i = 0; i < nnodes; i++) lfn_need[i] = (uint8_t)make_sfn(&nodes[i], 0, nnodes);
    uint32_t next = 2;
    int root_entries = 1;
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
    uint64_t dir_bytes_max = (uint64_t)root_clus * cbytes;
    for (int i = 0; i < nnodes; i++) if (nodes[i].is_dir && (uint64_t)nodes[i].nclus * cbytes > dir_bytes_max) dir_bytes_max = (uint64_t)nodes[i].nclus * cbytes;
    if (2 * dir_bytes_max > mem_lim) { set_err(err, errlen, "not enough memory"); return -1; }
    uint8_t *dirbuf = io->mem, *dirchk = io->mem + dir_bytes_max;

    /* ④ 새 구역: 예약 영역 */
    say(10, "format");
    static uint8_t rs[32 * 512];
    memset(rs, 0, sizeof rs);
    uint8_t *b = rs;
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
    memcpy(b + 3, "MSXEMU  ", 8);
    wr16(b + 11, 512); b[13] = (uint8_t)spc; wr16(b + 14, (uint16_t)rsvd); b[16] = 2;
    b[21] = 0xF8; wr16(b + 24, 63); wr16(b + 26, 255);
    wr32(b + 28, (uint32_t)new_start); wr32(b + 32, (uint32_t)total); wr32(b + 36, fatsz);
    wr32(b + 44, 2); wr16(b + 48, 1); wr16(b + 50, 6);
    b[64] = 0x80; b[66] = 0x29; wr32(b + 67, (uint32_t)(total ^ 0x4D53584Du ^ (uint32_t)new_start));
    memcpy(b + 71, label, 11); memcpy(b + 82, "FAT32   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    uint8_t *fi = rs + 512;
    wr32(fi, 0x41615252); wr32(fi + 484, 0x61417272);
    wr32(fi + 488, clusters - (next - 2)); wr32(fi + 492, next);
    wr32(fi + 508, 0xAA550000);
    memcpy(rs + 6 * 512, rs, 1024);                             /* 백업 (6, 7) */
    if (write_secs(new_start, rsvd, rs)) { set_err(err, errlen, "write error (boot sector)"); return -1; }
    { static uint8_t chk0[512]; if (io->read(io->ctx, new_start, 1, chk0) || memcmp(chk0, rs, 512)) { set_err(err, errlen, "boot sector verify failed"); return -1; } }

    /* FAT 2개 */
    for (uint64_t s0 = 0; s0 < fatsz; s0 += CHUNK) {
        uint32_t k = fatsz - s0 > CHUNK ? CHUNK : (uint32_t)(fatsz - s0);
        fill_fat(buf, s0, k, next, root_clus);
        if (IO->write(IO->ctx, fat_lba + s0, k, buf) || IO->write(IO->ctx, fat_lba + fatsz + s0, k, buf)) {
            set_err(err, errlen, "write error (FAT)"); return -1;
        }
        if (io->progress && (s0 & 1023) == 0) io->progress(11 + (int)(s0 * 9 / fatsz), "FAT");
    }

    /* 폴더 */
    say(21, "dirs");
    for (int d = -1; d < nnodes; d++) {                         /* d = -1: 루트 */
        if (d >= 0 && !nodes[d].is_dir) continue;
        uint32_t nclus = d < 0 ? root_clus : nodes[d].nclus, first = d < 0 ? 2 : nodes[d].first;
        build_dir(d, dirbuf, root_clus, label, lfn_need);
        if (write_secs(data_lba + (uint64_t)(first - 2) * spc, (uint64_t)nclus * spc, dirbuf)) { set_err(err, errlen, "write error (folder)"); return -1; }
    }

    /* ⑤ 파일 내용 복사 (옛 구역 → 새 구역, 섹터째) */
    int fno = 0;
    for (int i = 0; i < nnodes; i++) {
        Node *n = &nodes[i];
        if (n->is_dir) continue;
        fno++;
        { char pth[1024]; node_path(i, pth, sizeof pth);
          set_detail("", 0); put_num(detail, (uint32_t)fno); set_detail(detail, " / "); put_num(detail, (uint32_t)nfiles); set_detail(detail, "   "); set_detail(detail, pth);
          if (!n->nclus) continue;
          uint32_t sz = 0;
          int ne = fat_file_extents(vold, pth, ext_a, MAX_EXT, &sz);
          if (ne < 0 || sz != n->size) { set_err(err, errlen, "cannot locate file data"); return -1; }
          uint64_t secs = ((uint64_t)n->size + 511) / 512, dst = data_lba + (uint64_t)(n->first - 2) * spc;
          int r = copy_extents(ext_a, ne, secs, dst);
          if (r) { set_err(err, errlen, r == -2 ? "write error (file)" : "read error (file)"); return -1; }
          uint64_t full = (uint64_t)n->nclus * spc;                /* 마지막 클러스터의 남은 섹터는 0으로 */
          if (secs < full && write_zero(dst + secs, full - secs)) { set_err(err, errlen, "write error (file)"); return -1; }
        }
    }

    /* ⑥ 확인: 새 구역을 FAT32 드라이버로 다시 열어 옛것과 하나씩 비교 */
    say(71, "verify"); set_detail("", 0); bytes_done = 0;
    if (fat_mount(&vnew, rd, 0, (uint32_t)new_start) || vnew.clusters != clusters || vnew.spc != spc) { set_err(err, errlen, "verify: new volume cannot be opened"); return -1; }
    fno = 0;
    { static uint8_t rchk[32 * 512];                            /* 예약 영역(부트 영역·FSInfo·백업) 전체 */
      if (read_secs(new_start, rsvd, rchk) || memcmp(rchk, rs, sizeof rs)) { set_err(err, errlen, "verify: boot area differs"); return -1; } }
    for (uint64_t s0 = 0; s0 < fatsz; s0 += CHUNK) {            /* FAT 두 복사본 전체 */
        uint32_t k = fatsz - s0 > CHUNK ? CHUNK : (uint32_t)(fatsz - s0);
        fill_fat(buf, s0, k, next, root_clus);
        if (read_secs(fat_lba + s0, k, cmpbuf) || memcmp(buf, cmpbuf, (size_t)k * 512)) { set_err(err, errlen, "verify: FAT 1 differs"); return -1; }
        if (read_secs(fat_lba + fatsz + s0, k, cmpbuf) || memcmp(buf, cmpbuf, (size_t)k * 512)) { set_err(err, errlen, "verify: FAT 2 differs"); return -1; }
        if (io->progress && (s0 & 1023) == 0) io->progress(71 + (int)(s0 * 4 / fatsz), "verify");
    }
    for (int d = -1; d < nnodes; d++) {                         /* 폴더 내용 전체 */
        if (d >= 0 && !nodes[d].is_dir) continue;
        uint32_t nclus = d < 0 ? root_clus : nodes[d].nclus, first = d < 0 ? 2 : nodes[d].first;
        build_dir(d, dirbuf, root_clus, label, lfn_need);
        if (read_secs(data_lba + (uint64_t)(first - 2) * spc, (uint64_t)nclus * spc, dirchk) || memcmp(dirbuf, dirchk, (uint64_t)nclus * cbytes)) {
            set_err(err, errlen, "verify: folder data differs"); return -1;
        }
        char pth[1024];
        if (d < 0) pth[0] = 0; else node_path(d, pth, sizeof pth);
        int want = 0;
        for (int j = 0; j < nnodes; j++) if (nodes[j].parent == d) want++;
        if (count_entries(&vnew, pth) != want) { set_err(err, errlen, "verify: folder entries differ"); return -1; }   /* 새 FAT32 드라이버가 읽는 모습 */
    }
    for (int i = 0; i < nnodes; i++) {
        Node *n = &nodes[i];
        if (n->is_dir || !n->nclus) continue;
        fno++;
        char pth[1024]; node_path(i, pth, sizeof pth);
        set_detail("", 0); put_num(detail, (uint32_t)fno); set_detail(detail, " / "); put_num(detail, (uint32_t)nfiles); set_detail(detail, "   "); set_detail(detail, pth);
        uint32_t sa = 0, sb = 0;
        int na = fat_file_extents(vold, pth, ext_a, MAX_EXT, &sa), nb = fat_file_extents(&vnew, pth, ext_b, MAX_EXT_NEW, &sb);
        if (na < 0 || nb < 0 || sa != n->size || sb != n->size) { set_err(err, errlen, "verify: file size differs"); return -1; }
        int r = compare_extents(ext_a, na, ext_b, nb, ((uint64_t)n->size + 511) / 512);
        if (r) { set_err(err, errlen, r == -3 ? "verify: file content differs" : "verify: read error"); return -1; }
        if (io->progress && (fno & 3) == 0) io->progress(71 + (int)(bytes_done * 24 / (bytes_total ? bytes_total : 1)), "verify");
    }

    /* ⑦ 전환: MBR 첫 항목만 새 구역으로. 여기까지 오기 전에는 MBR이 그대로라서 옛 구역으로 부팅됨 */
    set_detail("", 0);
    say(97, "mbr");
    pe[4] = 0x0C;
    pe[1] = 0xFE; pe[2] = 0xFF; pe[3] = 0xFF;                   /* 시작 CHS: LBA 사용 */
    pe[5] = 0xFE; pe[6] = 0xFF; pe[7] = 0xFF;                   /* 끝 CHS */
    wr32(pe + 8, (uint32_t)new_start);
    wr32(pe + 12, (uint32_t)total);
    {
        static uint8_t chk[512];
        int ok = 0;
        for (int tries = 0; tries < 3 && !ok; tries++) {
            if (io->write(io->ctx, 0, 1, mbr)) continue;
            if (io->read(io->ctx, 0, 1, chk) == 0 && memcmp(chk, mbr, 512) == 0) ok = 1;
        }
        if (!ok) { set_err(err, errlen, "MBR was not updated (partition table write failed)"); return -1; }
    }
    say(100, "done");
    return 0;
}
