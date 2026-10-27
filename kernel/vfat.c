/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vfat.c - 3.7.1 가상 하드 (vfat.h 참고) */
#include "vfat.h"
#include "msxfat.h"
#include "dskimg.h"
#include <string.h>

#define DOS1_MAX_SPC 32                                 /* Nextor DOS1 모드: FAT12, FAT 3섹터 이하 → 클러스터 1022개 */

/* ---------------- 덮개 (쓴 자료 섹터) ---------------- */
#define PK_NONE 0xFFFFFFFFu
static uint8_t *pd;                                     /* 섹터 자료 */
static uint32_t *pk;                                    /* 열쇠 = id << 24 | lba */
static int32_t *ph;                                     /* 해시 → 칸 (-1 빈칸) */
static uint32_t pcap, pn, pdead, phmask;

void vf_pool_init(uint8_t *mem, uint32_t size)
{
    uint32_t cap = size / 528, hc = 1;
    for (;;) {
        hc = 1; while (hc < cap * 2) hc <<= 1;
        if ((uint64_t)cap * 516 + (uint64_t)hc * 4 <= size || !cap) break;
        cap -= cap / 16 + 1;
    }
    pcap = cap; pn = pdead = 0; phmask = hc - 1;
    pd = mem; pk = (uint32_t *)(mem + (uintptr_t)cap * 512); ph = (int32_t *)(pk + cap);
    for (uint32_t i = 0; i < hc; i++) ph[i] = -1;
}
uint32_t vf_pool_used(void) { return pn - pdead; }
uint32_t vf_pool_cap(void) { return pcap; }
static uint32_t hsh(uint32_t k) { k ^= k >> 16; k *= 0x45D9F3Bu; k ^= k >> 16; return k; }
static int32_t pool_find(uint32_t key)
{
    if (!pcap) return -1;
    for (uint32_t i = hsh(key) & phmask; ; i = (i + 1) & phmask) {
        int32_t s = ph[i];
        if (s < 0) return -1;
        if (pk[s] == key) return s;
    }
}
static void pool_rehash(void)
{
    for (uint32_t i = 0; i <= phmask; i++) ph[i] = -1;
    for (uint32_t s = 0; s < pn; s++) {
        if (pk[s] == PK_NONE) continue;
        uint32_t i = hsh(pk[s]) & phmask;
        while (ph[i] >= 0) i = (i + 1) & phmask;
        ph[i] = (int32_t)s;
    }
}
static void pool_compact(void)
{
    uint32_t w = 0;
    for (uint32_t s = 0; s < pn; s++) {
        if (pk[s] == PK_NONE) continue;
        if (w != s) { pk[w] = pk[s]; memcpy(pd + (uintptr_t)w * 512, pd + (uintptr_t)s * 512, 512); }
        w++;
    }
    pn = w; pdead = 0;
    pool_rehash();
}
static int pool_put(uint32_t key, const uint8_t *b)
{
    int32_t s = pool_find(key);
    if (s < 0) {
        if (pn >= pcap) { if (!pdead) return -1; pool_compact(); }
        s = (int32_t)pn++;
        pk[s] = key;
        uint32_t i = hsh(key) & phmask;
        while (ph[i] >= 0) i = (i + 1) & phmask;
        ph[i] = s;
    }
    memcpy(pd + (uintptr_t)s * 512, b, 512);
    return 0;
}
static void pool_del(uint32_t key) { int32_t s = pool_find(key); if (s >= 0) { pk[s] = PK_NONE; pdead++; } }
static uint32_t pkey(const Vf *v, uint32_t lba) { return (uint32_t)v->id << 24 | lba; }

/* ---------------- 작은 도움 ---------------- */
static void w16(uint8_t *p, uint32_t x) { p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); }
static void w32(uint8_t *p, uint32_t x) { w16(p, x); w16(p + 2, x >> 16); }
static uint32_t r16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return r16(p) | r16(p + 2) << 16; }
static void cpy(char *d, int max, const char *s) { int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }
static void cat(char *d, int max, const char *s) { int n = (int)strlen(d); cpy(d + n, max - n, s); }
static int is_fat16(const Vf *v) { return v->kind == VF_FAT16; }

static uint32_t fat_get(const Vf *v, uint32_t c)
{
    if (is_fat16(v)) return r16(v->fat + c * 2);
    uint32_t o = c * 3 / 2, x = r16(v->fat + o);
    return (c & 1) ? x >> 4 : x & 0xFFF;
}
static void fat_set(Vf *v, uint32_t c, uint32_t val)
{
    if (is_fat16(v)) { w16(v->fat + c * 2, val); return; }
    uint32_t o = c * 3 / 2;
    if (c & 1) { v->fat[o] = (uint8_t)((v->fat[o] & 0x0F) | (val << 4)); v->fat[o + 1] = (uint8_t)(val >> 4); }
    else { v->fat[o] = (uint8_t)val; v->fat[o + 1] = (uint8_t)((v->fat[o + 1] & 0xF0) | ((val >> 8) & 0x0F)); }
}
static int fat_eoc(const Vf *v, uint32_t x) { return is_fat16(v) ? x >= 0xFFF8 : x >= 0xFF8; }
static uint32_t eoc(const Vf *v) { return is_fat16(v) ? 0xFFFF : 0xFFF; }

static uint32_t sadd(Vf *v, const char *s)              /* 문자열 자리에 넣고 위치 (실패 0xFFFFFFFF) */
{
    uint32_t n = (uint32_t)strlen(s) + 1;
    if (v->nstr + n > v->str_cap) return 0xFFFFFFFFu;
    memcpy(v->str + v->nstr, s, n);
    v->nstr += n;
    return v->nstr - n;
}
static void dir_path(const Vf *v, int d, char *out, int max)
{
    if (d <= 0) { cpy(out, max, v->root); return; }
    dir_path(v, v->dirs[d].parent, out, max);
    cat(out, max, "\\"); cat(out, max, v->str + v->dirs[d].host);
}
static void file_path(const Vf *v, int f, char *out, int max)
{
    dir_path(v, v->files[f].parent, out, max);
    cat(out, max, "\\"); cat(out, max, v->str + v->files[f].host);
}
static void n83_str(const uint8_t *n, char *out)
{
    int k = 0;
    for (int i = 0; i < 8 && n[i] != ' '; i++) out[k++] = (char)n[i];
    if (n[8] != ' ') { out[k++] = '.'; for (int i = 8; i < 11 && n[i] != ' '; i++) out[k++] = (char)n[i]; }
    out[k] = 0;
}
static int n83_used(const Vf *v, int parent, const uint8_t *n)
{
    for (int i = 0; i < v->nfiles; i++) if (v->files[i].alive && v->files[i].parent == parent && !memcmp(v->files[i].n83, n, 11)) return 1;
    for (int i = 1; i < v->ndirs; i++) if (v->dirs[i].alive && v->dirs[i].parent == parent && !memcmp(v->dirs[i].n83, n, 11)) return 1;
    return 0;
}
/* USB 이름 → 같은 디렉토리 안에서 겹치지 않는 8.3 이름 (안 맞으면 NAME~1.EXT) */
static void make_n83(const Vf *v, int parent, const char *name, uint8_t n[11])
{
    int lossy = mf_make83(name, n);
    if (!lossy && !n83_used(v, parent, n)) return;
    uint8_t base[11]; memcpy(base, n, 11);
    for (int k = 1; k < 1000; k++) {
        char t[8]; int tl = 0;
        t[tl++] = '~';
        if (k >= 100) t[tl++] = (char)('0' + k / 100);
        if (k >= 10) t[tl++] = (char)('0' + k / 10 % 10);
        t[tl++] = (char)('0' + k % 10);
        int bl = 0; while (bl < 8 && base[bl] != ' ') bl++;
        if (bl > 8 - tl) bl = 8 - tl;
        memcpy(n, base, 11); memset(n, ' ', 8);
        memcpy(n, base, (size_t)bl); memcpy(n + bl, t, (size_t)tl);
        if (!n83_used(v, parent, n)) return;
    }
}

/* ---------------- 배치 ---------------- */
uint32_t vf_min_mb(int kind) { return kind == VF_FAT16 ? 3 : 1; }
uint32_t vf_max_mb(int kind) { return kind == VF_FAT16 ? 4095 : kind == VF_FAT12 ? 32 : 15; }   /* DOS1: 클러스터 1022개 × 16 KiB */

static int geom(Vf *v, uint32_t mb)
{
    if (mb < vf_min_mb(v->kind) || mb > vf_max_mb(v->kind)) return -1;
    v->total = mb * 2048; v->part_start = 1; v->part_secs = v->total - 1;
    int d1 = v->kind == VF_DOS1 || v->dos1geo;
    v->root_ents = d1 ? 112 : 512; v->root_secs = v->root_ents * 32 / 512;
    int f16 = is_fat16(v);
    uint32_t spc, fatsz = 1, cl = 0, part = v->part_secs;
    for (spc = 1; spc <= 128; spc <<= 1) {
        uint32_t c = part / spc;
        fatsz = f16 ? (c * 2 + 4 + 511) / 512 : ((c * 3 + 1) / 2 + 3 + 511) / 512;
        if (part < 1 + 2 * fatsz + v->root_secs + spc) continue;
        cl = (part - 1 - 2 * fatsz - v->root_secs) / spc;
        if (d1) { if (cl <= 1022 && fatsz <= 3) break; }
        else if (f16 ? cl < 65525 : cl < 4085) break;
    }
    if (spc > 128 || !cl) return -1;
    if (d1 && spc > DOS1_MAX_SPC) return -1;
    if (f16 && cl < 4085) return -1;
    if (!f16) fatsz = ((cl + 2) * 3 / 2 + 1 + 511) / 512;   /* 실제 클러스터 수로 다시 */
    else fatsz = ((cl + 2) * 2 + 511) / 512;
    v->spc = spc; v->fatsz = fatsz; v->nclus = cl; v->clb = spc * 512;
    v->first_fat = v->part_start + 1;
    v->first_root = v->first_fat + 2 * fatsz;
    v->first_data = v->first_root + v->root_secs;
    if (v->first_data + cl * spc > v->total) v->nclus = (v->total - v->first_data) / spc;
    return 0;
}

/* DOS1 부트 코드 (X86MSX가 새로 짬): 디스크 롬이 C01Eh를 CY=1로 부르면 이 드라이브를 기본으로 고르고(BDOS 0Eh)
 * MSXDOS.SYS를 0100h에 읽어 실행 (COMMAND.COM도 이 드라이브에서). 없거나 오류면 SP를 되돌려 Disk BASIC */
static void boot_dos1_at(uint8_t *b, int drive0, int org)
{
    static const uint8_t code[] = {
        0xD0,                                           /* 1E RET NC */
        0xED, 0x73, 0x5E, 0xC0,                         /* 1F LD (C05Eh),SP */
        0x36, 0x59, 0x23, 0x36, 0xC0,                   /* 23 LD (HL),C059h  디스크 오류 때 갈 곳 */
        0x31, 0x1F, 0xF5,                               /* 28 LD SP,F51Fh */
        0x1E, 0x00, 0x0E, 0x0E, 0xCD, 0x7D, 0xF3,       /* 2B LD E,드라이브 / LD C,0Eh / CALL F37Dh (기본 드라이브) */
        0x11, 0x60, 0xC0, 0x0E, 0x0F, 0xCD, 0x7D, 0xF3, /* 32 FCB 열기 */
        0x3C, 0x28, 0x1C,                               /* 3A INC A / JR Z,C059h */
        0x11, 0x00, 0x01, 0x0E, 0x1A, 0xCD, 0x7D, 0xF3, /* 3D DTA = 0100h */
        0x21, 0x01, 0x00, 0x22, 0x6E, 0xC0,             /* 45 레코드 크기 1 (FCB+0Eh) */
        0x21, 0x00, 0x3F, 0x11, 0x60, 0xC0, 0x0E, 0x27, 0xCD, 0x7D, 0xF3,   /* 4B 3F00h 바이트 읽기 */
        0xC3, 0x00, 0x01,                               /* 56 JP 0100h */
        0xED, 0x7B, 0x5E, 0xC0, 0xC9,                   /* 59 LD SP,(C05Eh) / RET → Disk BASIC */
        0x00, 0x00,                                     /* 5E SP 저장 */
        0x00, 'M', 'S', 'X', 'D', 'O', 'S', ' ', ' ', 'S', 'Y', 'S'   /* 60 FCB (기본 드라이브, 나머지 0) */
    };
    /* 코드를 org(1Eh 또는 3Eh)에 놓음. C000h 기준 절대 주소(되돌아갈 곳·SP 저장·FCB)는 옮긴 만큼 더함 */
    uint8_t c[sizeof code]; memcpy(c, code, sizeof code);
    int sh = org - 0x1E;
    for (size_t i = 0; i + 1 < sizeof c; i++) {
        if (c[i + 1] == 0xC0 && (c[i] == 0x5E || c[i] == 0x60 || c[i] == 0x6E) && i > 0 && (c[i - 1] == 0x73 || c[i - 1] == 0x7B || c[i - 1] == 0x11 || c[i - 1] == 0x22)) c[i] = (uint8_t)(c[i] + sh);
        if (c[i] == 0x59 && i > 0 && c[i - 1] == 0x36 && c[i + 1] == 0x23) c[i] = (uint8_t)(c[i] + sh);
    }
    c[0x2C - 0x1E] = (uint8_t)drive0;
    memset(b + org, 0, 0x1FE - org);
    memcpy(b + org, c, sizeof c);
}
static void boot_dos1(uint8_t *b, int drive0) { boot_dos1_at(b, drive0, 0x1E); }

static void make_boot(Vf *v, const char *label)
{
    uint8_t *b = v->boot;
    memset(b, 0, 512);
    int f16 = is_fat16(v);
    b[0] = 0xEB; b[1] = f16 ? 0x3C : 0xFE; b[2] = 0x90;
    memcpy(b + 3, v->kind == VF_DOS1 ? "X86MSX  " : f16 ? "NEXTOR20" : "MSXDOS20", 8);
    w16(b + 0x0B, 512); b[0x0D] = (uint8_t)v->spc; w16(b + 0x0E, 1); b[0x10] = 2;
    w16(b + 0x11, v->root_ents);
    if (v->part_secs < 65536) w16(b + 0x13, v->part_secs); else w32(b + 0x20, v->part_secs);
    b[0x15] = 0xF8; w16(b + 0x16, v->fatsz); w16(b + 0x18, 32); w16(b + 0x1A, 16);
    if (f16) {
        w32(b + 0x1C, v->part_start); b[0x24] = 0x80; b[0x26] = 0x29; w32(b + 0x27, 0x58384D53u + (uint32_t)v->id);
        for (int i = 0; i < 11; i++) b[0x2B + i] = (uint8_t)(label[i] ? label[i] : ' ');
        for (int i = 0; label[i] && i < 11; i++) ;
        memcpy(b + 0x36, "FAT16   ", 8);
        b[0x3E] = 0xC9;
    } else if (v->kind == VF_FAT12) {                   /* MSX-DOS2 모양 (dskimg.c와 같음) */
        w16(b + 0x1C, v->part_start); b[0x1E] = 0x18; b[0x1F] = 0x1E;
        memcpy(b + 0x20, "VOL_ID", 6); b[0x26] = 0; w32(b + 0x27, 0x58384D53u + (uint32_t)v->id);
        b[0x3E] = 0xC9;
        if (v->dos1geo) boot_dos1_at(b, 0, 0x3E);        /* Nextor가 DOS1 모드(매퍼 96KB 미만)로 떨어지면 MSXDOS.SYS로 부팅 */
    } else {                                            /* MSX-DOS1 모양: Nextor가 DOS1 모드로 부팅, 부트 코드가 MSXDOS.SYS를 읽음 */
        w16(b + 0x1C, v->part_start);
        boot_dos1(b, 0);                                /* DOS1 모드는 A:로만 부팅 (fe_vhd.inc가 이 하드를 A:에 둠) */
    }
    b[510] = 0x55; b[511] = 0xAA;
}

static void put_ent(uint8_t *e, const uint8_t *n83, uint8_t attr, uint32_t clus, uint32_t size, uint16_t date, uint16_t time)
{
    memset(e, 0, 32);
    memcpy(e, n83, 11); e[11] = attr;
    w16(e + 22, time); w16(e + 24, date ? date : (uint16_t)((2026 - 1980) << 9 | 1 << 5 | 1));
    w16(e + 26, clus); w32(e + 28, size);
}

/* 디렉토리 d의 i번째 항목 (하위 디렉토리면 0 = ".", 1 = ".."). 없으면 0 */
static int dir_child(const Vf *v, int d, int i, uint8_t *e)
{
    int k = 0;
    if (d > 0) {
        if (i == 0) { uint8_t n[11]; memset(n, ' ', 11); n[0] = '.'; put_ent(e, n, 0x10, 0, 0, 0, 0); return 1; }
        if (i == 1) { uint8_t n[11]; memset(n, ' ', 11); n[0] = n[1] = '.'; put_ent(e, n, 0x10, 0, 0, 0, 0); return 1; }
        k = 2;
    }
    for (int j = 1; j < v->ndirs; j++) if (v->dirs[j].alive && v->dirs[j].parent == d) { if (k++ == i) { put_ent(e, v->dirs[j].n83, 0x10, 0, 0, 0, 0); return 2 + j; } }
    for (int j = 0; j < v->nfiles; j++) if (v->files[j].alive && v->files[j].parent == d) { if (k++ == i) { put_ent(e, v->files[j].n83, 0x20, 0, v->files[j].size, v->files[j].date, v->files[j].time); return 10000 + j; } }
    return 0;
}

/* ---------------- 마운트 ---------------- */
int vf_folder_mb(const VfHost *h, const char *root)
{
    static VfHostEnt ents[256];
    static char stack[16][256];
    int sp = 0; uint64_t total = 0;
    cpy(stack[sp++], 256, root);
    while (sp) {
        char p[256]; cpy(p, 256, stack[--sp]);
        int n = h->list(h->ctx, p, ents, 256);
        if (n < 0) { if (!strcmp(p, root)) return -1; continue; }
        for (int i = 0; i < n; i++) {
            if (ents[i].is_dir) { if (sp < 16) { cpy(stack[sp], 256, p); cat(stack[sp], 256, "\\"); cat(stack[sp], 256, ents[i].name); sp++; } }
            else total += ents[i].size;
        }
    }
    return (int)((total + (1u << 20) - 1) >> 20);
}

int vf_auto_kind(const VfHost *h, const char *root, uint32_t size_mb)
{
    static VfHostEnt ents[256];
    int n = h->list(h->ctx, root, ents, 256), dos1 = 0, dos2 = 0;
    for (int i = 0; i < n; i++) {
        uint8_t r[11];
        if (ents[i].is_dir || mf_make83(ents[i].name, r)) continue;
        if (!memcmp(r, "MSXDOS  SYS", 11)) dos1 = 1;
        if (!memcmp(r, "MSXDOS2 SYS", 11) || !memcmp(r, "NEXTOR  SYS", 11)) dos2 = 1;
    }
    if (dos1 && !dos2) return VF_DOS1;
    if (dos1 && size_mb <= vf_max_mb(VF_DOS1)) return VF_FAT12;   /* DOS1·DOS2 파일이 같이 있고 작으면 FAT12 (DOS1도 읽는 모양으로) */
    return size_mb < vf_min_mb(VF_FAT16) ? VF_FAT12 : VF_FAT16;
}

int vf_mount(Vf *v, const VfHost *h, const char *root, uint32_t size_mb, int kind, int id, uint8_t *arena, char *err, int errlen)
{
    memset(v, 0, sizeof *v);
    v->h = h; cpy(v->root, sizeof v->root, root); v->id = id & 15;
    v->kind = kind == VF_AUTO ? vf_auto_kind(h, root, size_mb) : kind;
    if (v->kind == VF_FAT12 && size_mb <= vf_max_mb(VF_DOS1)) {   /* 루트에 MSXDOS.SYS와 COMMAND.COM이 다 있으면 DOS1 모드 부팅도 되게 */
        VfHostEnt *le = (VfHostEnt *)(arena + VF_ARENA - sizeof(VfHostEnt) * 256); int ln = h->list(h->ctx, root, le, 256), a = 0, c = 0;
        for (int i = 0; i < ln; i++) { uint8_t r[11]; if (le[i].is_dir || mf_make83(le[i].name, r)) continue; if (!memcmp(r, "MSXDOS  SYS", 11)) a = 1; if (!memcmp(r, "COMMAND COM", 11)) c = 1; }
        v->dos1geo = a && c;
    }
    if (size_mb > vf_max_mb(v->kind)) size_mb = vf_max_mb(v->kind);   /* 형식 한도에 맞춤 (DOS1 16 MB, FAT12 32 MB) */
    if (size_mb < vf_min_mb(v->kind)) size_mb = vf_min_mb(v->kind);
    if (geom(v, size_mb)) { cpy(err, errlen, "size"); return -1; }
    /* 작업 메모리 나누기 */
    uint8_t *a = arena;
    v->fat = a; a += 256 * 512;                         /* FAT16 클러스터 65525개 = 256섹터 */
    v->rootd = a; a += 512 * 32;
    v->own = (uint16_t *)a; a += 65536 * 2;
    v->cidx = (uint16_t *)a; a += 65536 * 2;
    v->files = (VfFile *)a; a += sizeof(VfFile) * VF_MAX_FILES;
    v->dirs = (VfDir *)a; a += sizeof(VfDir) * VF_MAX_DIRS;
    VfHostEnt *ents = (VfHostEnt *)a; int emax = 1024; a += sizeof(VfHostEnt) * 1024;   /* 3.9.12: 폴더 하나 256 → 1024개 */
    if ((uintptr_t)(arena + VF_ARENA - a) < 65536) { cpy(err, errlen, "arena"); return -1; }
    v->str = (char *)a; v->str_cap = (uint32_t)(arena + VF_ARENA - a);
    memset(v->fat, 0, (size_t)v->fatsz * 512);
    memset(v->rootd, 0, (size_t)v->root_secs * 512);
    memset(v->own, 0, 65536 * 4);
    v->nstr = 1; v->str[0] = 0;
    /* 폴더 훑기 (넓이 우선): dirs[0] = 루트 */
    v->ndirs = 1; v->dirs[0].alive = 1; v->dirs[0].parent = 0;
    char path[512];
    for (int d = 0; d < v->ndirs; d++) {
        dir_path(v, d, path, sizeof path);
        int n = h->list(h->ctx, path, ents, emax);
        if (n >= emax) v->trunc = 1;
        if (n < 0) { if (!d) { cpy(err, errlen, "folder"); return -1; } continue; }
        for (int pass = 0; pass < 2; pass++)            /* 디렉토리 먼저 */
            for (int i = 0; i < n; i++) {
                VfHostEnt *e = &ents[i];
                if (!e->name[0] || e->name[0] == '.' || (int)e->is_dir != !pass) continue;
                if (pass == 0) {
                    if (v->ndirs >= VF_MAX_DIRS) { v->trunc = 1; continue; }
                    VfDir *q = &v->dirs[v->ndirs];
                    q->parent = (uint16_t)d; q->alive = 1; q->host = sadd(v, e->name);
                    if (q->host == 0xFFFFFFFFu) continue;
                    make_n83(v, d, e->name, q->n83);
                    v->ndirs++;
                } else {
                    if (v->nfiles >= VF_MAX_FILES) { v->trunc = 1; continue; }
                    VfFile *q = &v->files[v->nfiles];
                    memset(q, 0, sizeof *q);
                    q->parent = (uint16_t)d; q->alive = 1; q->size = e->size; q->date = e->date; q->time = e->time; q->host = sadd(v, e->name);
                    if (q->host == 0xFFFFFFFFu) continue;
                    make_n83(v, d, e->name, q->n83);
                    v->nfiles++;
                }
            }
    }
    /* 루트에 다 들어가는가 (볼륨 이름 1칸) */
    int rootn = 1;
    for (int j = 1; j < v->ndirs; j++) if (v->dirs[j].parent == 0) rootn++;
    for (int j = 0; j < v->nfiles; j++) if (v->files[j].parent == 0) rootn++;
    if (rootn > (int)v->root_ents) { cpy(err, errlen, "root"); return -1; }
    /* 클러스터 배치: 디렉토리 → 파일 (이어서) */
    uint32_t next = 2, last = v->nclus + 2;
    fat_set(v, 0, is_fat16(v) ? 0xFFF8 : 0xFF8); fat_set(v, 1, eoc(v));
    static uint16_t dfirst[VF_MAX_DIRS];
    for (int d = 1; d < v->ndirs; d++) {
        int ents_n = 2;
        for (int j = 1; j < v->ndirs; j++) if (v->dirs[j].parent == d) ents_n++;
        for (int j = 0; j < v->nfiles; j++) if (v->files[j].parent == d) ents_n++;
        uint32_t nc = ((uint32_t)ents_n * 32 + v->clb - 1) / v->clb;
        if (next + nc > last) { cpy(err, errlen, "full"); return -1; }
        dfirst[d] = (uint16_t)next;
        for (uint32_t k = 0; k < nc; k++) fat_set(v, next + k, k + 1 < nc ? next + k + 1 : eoc(v));
        next += nc;
    }
    static uint16_t ffirst[VF_MAX_FILES];
    for (int f = 0; f < v->nfiles; f++) {
        uint32_t nc = (v->files[f].size + v->clb - 1) / v->clb;
        ffirst[f] = 0;
        if (!nc) continue;
        if (next + nc > last || nc > 65535) { cpy(err, errlen, "full"); return -1; }
        ffirst[f] = (uint16_t)next;
        for (uint32_t k = 0; k < nc; k++) { fat_set(v, next + k, k + 1 < nc ? next + k + 1 : eoc(v)); v->own[next + k] = (uint16_t)(f + 1); v->cidx[next + k] = (uint16_t)k; }
        next += nc;
    }
    v->used_clus = next - 2;
    /* 볼륨 이름 = 폴더 이름 */
    char label[12]; memset(label, ' ', 11); label[11] = 0;
    {
        const char *nm = root; for (const char *p = root; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') nm = p + 1;
        for (int i = 0; i < 11 && nm[i]; i++) { char c = nm[i]; if (c >= 'a' && c <= 'z') c = (char)(c - 32); label[i] = c; }
    }
    make_boot(v, label);
    /* 루트 디렉토리 */
    uint8_t *re = v->rootd;
    put_ent(re, (const uint8_t *)label, 0x08, 0, 0, 0, 0); re += 32;
    for (int i = 0; ; i++) {
        uint8_t e[32]; int w = dir_child(v, 0, i, e);
        if (!w) break;
        if (w >= 10000) w16(e + 26, ffirst[w - 10000]); else if (w >= 2) w16(e + 26, dfirst[w - 2]);
        memcpy(re, e, 32); re += 32;
    }
    /* 하위 디렉토리 내용 → 덮개 */
    uint8_t sec[512];
    for (int d = 1; d < v->ndirs; d++) {
        uint32_t c = dfirst[d], idx = 0;
        while (c >= 2 && c < last) {
            for (uint32_t s = 0; s < v->spc; s++) {
                memset(sec, 0, 512);
                for (int k = 0; k < 16; k++, idx++) {
                    uint8_t *e = sec + k * 32; int w = dir_child(v, d, (int)idx, e);
                    if (!w) continue;
                    if (w == 1) w16(e + 26, idx == 0 ? dfirst[d] : v->dirs[d].parent ? dfirst[v->dirs[d].parent] : 0);
                    else if (w >= 10000) w16(e + 26, ffirst[w - 10000]); else w16(e + 26, dfirst[w - 2]);
                }
                if (pool_put(pkey(v, v->first_data + (c - 2) * v->spc + s), sec)) { cpy(err, errlen, "memory"); return -1; }
            }
            uint32_t nx = fat_get(v, c);
            if (fat_eoc(v, nx)) break;
            c = nx;
        }
    }
    return 0;
}

void vf_unmount(Vf *v)
{
    if (!v->fat) return;
    for (uint32_t s = 0; s < pn; s++) if (pk[s] != PK_NONE && (pk[s] >> 24) == (uint32_t)v->id) { pk[s] = PK_NONE; pdead++; }
    pool_compact();
    v->fat = 0;
}

/* ---------------- 섹터 읽기·쓰기 ---------------- */
static void make_mbr(const Vf *v, uint8_t *b)
{
    memset(b, 0, 512);
    uint8_t *e = b + 446;
    e[0] = 0x80;                                        /* 부팅 파티션 */
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;
    e[4] = is_fat16(v) ? (v->part_secs >= 65536 ? 0x06 : 0x04) : 0x01;
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
    w32(e + 8, v->part_start); w32(e + 12, v->part_secs);
    b[510] = 0x55; b[511] = 0xAA;
}

int vf_read(Vf *v, uint32_t lba, uint8_t *buf)
{
    if (!v->fat || lba >= v->total) return -1;
    if (lba == 0) { make_mbr(v, buf); return 0; }
    if (lba < v->part_start) { memset(buf, 0, 512); return 0; }
    if (lba == v->part_start) { memcpy(buf, v->boot, 512); return 0; }
    if (lba < v->first_fat) { memset(buf, 0, 512); return 0; }
    if (lba < v->first_root) { memcpy(buf, v->fat + (uintptr_t)((lba - v->first_fat) % v->fatsz) * 512, 512); return 0; }
    if (lba < v->first_data) { memcpy(buf, v->rootd + (uintptr_t)(lba - v->first_root) * 512, 512); return 0; }
    int32_t s = pool_find(pkey(v, lba));
    if (s >= 0) { memcpy(buf, pd + (uintptr_t)s * 512, 512); return 0; }
    uint32_t rel = lba - v->first_data, c = rel / v->spc + 2;
    memset(buf, 0, 512);
    if (c < v->nclus + 2 && v->own[c]) {
        int f = v->own[c] - 1;
        uint32_t off = (uint32_t)v->cidx[c] * v->clb + (rel % v->spc) * 512;
        if (off < v->files[f].size) {
            uint32_t n = v->files[f].size - off; if (n > 512) n = 512;
            char p[512]; file_path(v, f, p, sizeof p);
            if (v->h->read_at(v->h->ctx, p, off, buf, n)) return -1;
            /* 3.8.0 (A-1): MSX-DOS 1.03의 MSXDOS.SYS는 시작하자마자 스택을 9000h(페이지 2)에 둠. 한글 MSX(DPC-200)는 한글 롬이
             * 슬롯 0 페이지 2에 있어 BIOS가 그 롬을 부를 때 스택이 롬에 가려져 멈춤 → MSX가 읽는 내용에서만 LD SP,9000h를 C200h로 (USB 파일은 그대로)
             * C800h는 자유 메모리 위쪽(top)이 CF80h 아래인 기종(소니 F700P+Nextor: C300~CA7F)에서 커널이 옮겨질 자리와 겹쳐 스택이 덮임 → 부트 섹터 자리(C200h) 아래로 */
            if (off == 0 && n > 0x13A && v->kind == VF_DOS1 && v->files[f].parent == 0 && !memcmp(v->files[f].n83, "MSXDOS  SYS", 11)
                && buf[0] == 0xC3 && buf[0x138] == 0x31 && buf[0x139] == 0x00 && buf[0x13A] == 0x90) buf[0x13A] = 0xC2;
        }
    }
    return 0;
}

int vf_write(Vf *v, uint32_t lba, const uint8_t *buf)
{
    if (!v->fat || lba >= v->total) return -1;
    v->dirty = 1;
    if (lba < v->part_start) return 0;                  /* MBR은 바꾸지 않음 (FDISK 막음) */
    if (lba == v->part_start) { memcpy(v->boot, buf, 512); return 0; }
    if (lba < v->first_fat) return 0;
    if (lba < v->first_root) { memcpy(v->fat + (uintptr_t)((lba - v->first_fat) % v->fatsz) * 512, buf, 512); return 0; }
    if (lba < v->first_data) { memcpy(v->rootd + (uintptr_t)(lba - v->first_root) * 512, buf, 512); return 0; }
    return pool_put(pkey(v, lba), buf);
}

/* ---------------- 되쓰기 ---------------- */
typedef struct { uint16_t parent; uint8_t n83[11], attr; uint32_t clus, size; } NewEnt;
#define NE_MAX (VF_MAX_FILES + VF_MAX_DIRS)
static NewEnt ne[NE_MAX];
static int16_t ne_host[NE_MAX];                         /* 디렉토리 항목 → v->dirs 번호 */

static int cluster_clean(const Vf *v, uint32_t c)       /* 덮개에 쓴 섹터가 없음 */
{
    for (uint32_t s = 0; s < v->spc; s++) if (pool_find(pkey(v, v->first_data + (c - 2) * v->spc + s)) >= 0) return 0;
    return 1;
}

int vf_sync(Vf *v, uint8_t *tmp, uint32_t tmpsize, char *err, int errlen)
{
    if (!v->fat) return 0;
    if (!v->dirty) return 0;
    err[0] = 0;
    /* 1. 지금 디렉토리 나무 읽기 (항목 0 = 루트) */
    int nn = 1; memset(&ne[0], 0, sizeof ne[0]); ne_host[0] = 0;
    uint8_t sec[512];
    for (int d = 0; d < nn; d++) {
        if (d && !(ne[d].attr & 0x10)) continue;
        uint32_t c = d ? ne[d].clus : 0, guard = 0;
        uint32_t secs = d ? 0 : v->root_secs;
        for (uint32_t k = 0; ; k++) {
            uint32_t lba;
            if (!d) { if (k >= secs) break; lba = v->first_root + k; }
            else {
                if (c < 2 || c >= v->nclus + 2 || guard++ > 65536) break;
                lba = v->first_data + (c - 2) * v->spc + k % v->spc;
                if (k % v->spc == v->spc - 1) { uint32_t nx = fat_get(v, c); c = fat_eoc(v, nx) ? 0 : nx; }
            }
            if (vf_read(v, lba, sec)) break;
            int end = 0;
            for (int i = 0; i < 16; i++) {
                const uint8_t *e = sec + i * 32;
                if (!e[0]) { end = 1; break; }
                if (e[0] == 0xE5 || e[0] == '.' || (e[11] & 0x08) || e[11] == 0x0F) continue;
                if (nn >= NE_MAX) { cpy(err, errlen, "entries"); break; }
                NewEnt *q = &ne[nn]; q->parent = (uint16_t)d; memcpy(q->n83, e, 11); q->attr = e[11];
                q->clus = r16(e + 26); q->size = r32(e + 28);
                if (q->n83[0] == 0x05) q->n83[0] = 0xE5;
                nn++;
            }
            if (end) break;
        }
    }
    /* 2. 디렉토리 → USB 디렉토리 (없으면 만듦) */
    char p[512], nm[16];
    int written = 0;
    for (int i = 1; i < nn; i++) {
        ne_host[i] = -1;
        if (!(ne[i].attr & 0x10)) continue;
        int hp = ne_host[ne[i].parent];
        if (hp < 0) continue;
        int f = -1;
        for (int j = 1; j < v->ndirs; j++) if (v->dirs[j].alive && v->dirs[j].parent == hp && !memcmp(v->dirs[j].n83, ne[i].n83, 11)) { f = j; break; }
        if (f < 0 && v->ndirs < VF_MAX_DIRS) {
            n83_str(ne[i].n83, nm);
            VfDir *q = &v->dirs[v->ndirs];
            q->parent = (uint16_t)hp; q->alive = 1; memcpy(q->n83, ne[i].n83, 11); q->host = sadd(v, nm);
            if (q->host != 0xFFFFFFFFu) {
                f = v->ndirs++;
                dir_path(v, f, p, sizeof p);
                if (v->h->mkdir) v->h->mkdir(v->h->ctx, p);
            }
        }
        ne_host[i] = (int16_t)f;
    }
    /* 3. 파일: 바뀐 것만 다시 씀 */
    for (int j = 0; j < v->nfiles; j++) v->files[j].seen = 0;
    for (int i = 1; i < nn; i++) {
        if (ne[i].attr & 0x10) continue;
        int hp = ne_host[ne[i].parent];
        if (hp < 0) continue;
        int f = -1;
        for (int j = 0; j < v->nfiles; j++) if (v->files[j].alive && v->files[j].parent == hp && !memcmp(v->files[j].n83, ne[i].n83, 11)) { f = j; break; }
        uint32_t nc = (ne[i].size + v->clb - 1) / v->clb;
        int same = f >= 0 && v->files[f].size == ne[i].size;
        if (same) {                                     /* 클러스터 줄이 그 파일 그대로이고 덮개에 쓴 것이 없음 */
            uint32_t c = ne[i].clus;
            for (uint32_t k = 0; k < nc && same; k++) {
                if (c < 2 || c >= v->nclus + 2 || v->own[c] != f + 1 || v->cidx[c] != k || !cluster_clean(v, c)) same = 0;
                else { uint32_t nx = fat_get(v, c); c = fat_eoc(v, nx) ? 0 : nx; }
            }
        }
        if (f >= 0) v->files[f].seen = 1;
        if (same) continue;
        if (ne[i].size > tmpsize) { cpy(err, errlen, "big"); continue; }
        /* 내용 모으기 */
        uint32_t c = ne[i].clus, got = 0;
        int bad = 0;
        while (got < ne[i].size) {
            if (c < 2 || c >= v->nclus + 2) { bad = 1; break; }
            for (uint32_t s = 0; s < v->spc && got < ne[i].size; s++) {
                if (vf_read(v, v->first_data + (c - 2) * v->spc + s, sec)) { bad = 1; break; }
                uint32_t n = ne[i].size - got; if (n > 512) n = 512;
                memcpy(tmp + got, sec, n); got += n;
            }
            if (bad) break;
            uint32_t nx = fat_get(v, c); c = fat_eoc(v, nx) ? 0 : nx;
        }
        if (bad) { cpy(err, errlen, "chain"); continue; }
        if (f < 0) {                                    /* 새 파일: USB 이름 = 8.3 이름 */
            if (v->nfiles >= VF_MAX_FILES) { cpy(err, errlen, "files"); continue; }
            n83_str(ne[i].n83, nm);
            VfFile *q = &v->files[v->nfiles];
            memset(q, 0, sizeof *q);
            q->parent = (uint16_t)hp; q->alive = 1; memcpy(q->n83, ne[i].n83, 11); q->host = sadd(v, nm);
            if (q->host == 0xFFFFFFFFu) { cpy(err, errlen, "names"); continue; }
            f = v->nfiles++;
            v->files[f].seen = 1;
        }
        file_path(v, f, p, sizeof p);
        if (v->h->write(v->h->ctx, p, tmp, ne[i].size)) { cpy(err, errlen, "write"); continue; }
        written++;
        v->files[f].size = ne[i].size;
        for (uint32_t k = 2; k < v->nclus + 2; k++) if (v->own[k] == f + 1) v->own[k] = 0;
        c = ne[i].clus;
        for (uint32_t k = 0; k < nc && c >= 2 && c < v->nclus + 2; k++) {
            v->own[c] = (uint16_t)(f + 1); v->cidx[c] = (uint16_t)k;
            for (uint32_t s = 0; s < v->spc; s++) pool_del(pkey(v, v->first_data + (c - 2) * v->spc + s));
            uint32_t nx = fat_get(v, c); c = fat_eoc(v, nx) ? 0 : nx;
        }
    }
    /* 4. 없어진 파일은 USB에서도 지움 */
    for (int j = 0; j < v->nfiles; j++) {
        if (!v->files[j].alive || v->files[j].seen) continue;
        if (ne_host[0] < 0) break;
        file_path(v, j, p, sizeof p);
        if (v->h->del) v->h->del(v->h->ctx, p);
        v->files[j].alive = 0;
        for (uint32_t k = 2; k < v->nclus + 2; k++) if (v->own[k] == j + 1) v->own[k] = 0;
        written++;
    }
    if (pdead > pcap / 4) pool_compact();
    v->dirty = 0;
    return err[0] ? -1 : written;
}
