/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * profile.c - blueMSX config.ini 파서 (설계서 §5.1)
 *
 * [Slots] 줄: 슬롯 서브슬롯 시작블록 블록수 종류번호 "파일" ""
 *   2.0에서 다루는 종류: 20 = 일반 ROM(0x0000), 39 = 일반 ROM(0x8000), 23 = 일반 RAM,
 *   84 = MSX 프린터(I/O, 아직 빈 장치). 크기 0인 다른 I/O 장치는 "지원 안 함" 목록에만 남긴다.
 * profile_parse는 표준 라이브러리 없이 동작한다(본체용). profile_load는 호스트 전용.
 */
#include "profile.h"
#include "cart.h"
#include "devcat.h"
#include <string.h>

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static void copy(char *dst, size_t n, const char *src, size_t len)
{
    if (!n) return;
    if (len > n - 1) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static void append(char *dst, size_t n, const char *s)
{
    size_t k = strlen(dst);
    while (*s && k + 1 < n) dst[k++] = *s++;
    dst[k] = 0;
}

static void set_err(char *err, size_t n, const char *a, const char *b)
{
    if (!err || !n) return;
    err[0] = 0;
    append(err, n, a);
    if (b) append(err, n, b);
}

/* 정수 하나 읽기 (앞 공백 무시). 실패 -1 */
static const char *read_int(const char *s, long *v)
{
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (*s < '0' || *s > '9') return 0;
    long x = 0;
    while (*s >= '0' && *s <= '9') x = x * 10 + (*s++ - '0');
    *v = neg ? -x : x;
    return s;
}

static long to_long(const char *s)
{
    long v = 0;
    return read_int(s, &v) ? v : 0;
}

static int str_eq(const char *a, size_t alen, const char *b)
{
    return strlen(b) == alen && !memcmp(a, b, alen);
}

/* 장치 목록의 기종 기능 깃발 → MachineProfile 칸 */
static void set_feature(MachineProfile *p, int f, const char *file)
{
    switch (f) {
    case DF_S1985: p->s1985 = 1; break;                 /* MSX2 엔진 */
    case DF_S1990: p->s1990 = 1; break;                 /* turbo R */
    case DF_TRTIMER: p->trtimer = 1; break;
    case DF_PCM: p->pcm = 1; break;
    case DF_TRPAUSE: p->trpause = 1; break;
    case DF_MIDI: p->midi = p->midi_hw = 1; break;      /* 3.3.3 MSX-MIDI */
    case DF_MATSU: p->matsushita = 1; break;            /* 3.4.0 Matsushita 교환 I/O */
    case DF_MATSU_TURBO: p->matsushita = 2; break;      /* 159 = 터보 있음 (FS-A1WX·WSX) */
    case DF_Y8950: p->y8950 = 1; break;                 /* 2.16: 내장 MSX-AUDIO (Y8950, 포트 C0h·C1h) */
    case DF_JOYREX: p->joyrex = 1; break;               /* 3.6.8: 내장 Joyrex PSG (포트 10h~12h) */
    case DF_MOON: p->moonsound = 1; copy(p->ms_rom, sizeof p->ms_rom, file, strlen(file)); break;   /* 3.1.0: MoonSound */
    case DF_F4N: p->f4 = 1; break;
    case DF_F4I: p->f4 = 2; break;
    case DF_OPLL: p->opll = 1; break;                   /* MSX-MUSIC (포트 7Ch·7Dh) */
    default: break;
    }
}

static int parse_slot_line(const char *line, MachineProfile *p, char *err, size_t errlen)
{
    long v[5];
    const char *s = line;
    for (int i = 0; i < 5; i++) {
        s = read_int(s, &v[i]);
        if (!s) { set_err(err, errlen, "bad [Slots] line: ", line); return -1; }
    }
    int slot = (int)v[0], sub = (int)v[1], start = (int)v[2], count = (int)v[3], type = (int)v[4];
    char file[256] = "";
    const char *q = strchr(s, '"');
    if (q) {
        const char *e = strchr(q + 1, '"');
        if (e && e > q + 1) copy(file, sizeof file, q + 1, (size_t)(e - q - 1));
    }
    const DevInfo *d = devcat_find(type);                /* 장치 목록(devcat.c) 한 줄이 이 형식을 정함 */
    if (count == 0) {                                   /* I/O 장치 (블록 매핑 없음) */
        if (d && d->io == DC_IO_FEATURE) { set_feature(p, d->io_arg, file); return 0; }
        if (d && d->io == DC_IO_SILENT) return 0;       /* 3.4.0 DEV-26·27: 디지타이저·모뎀·프린터 = 인식만 (경고 없음) */
        if (d && (d->io == DC_IO_KANJI || d->io == DC_IO_JISYO) && file[0]) {   /* 한자/한글 글꼴 롬, 3.4.0 사전 롬 */
            if (p->region_count >= MAX_REGIONS) { set_err(err, errlen, "too many regions", 0); return -1; }
            ProfileRegion *k = &p->region[p->region_count++];
            memset(k, 0, sizeof *k);
            k->type = d->io == DC_IO_KANJI ? REGION_KANJI : REGION_JISYO;
            k->blue_type = type;
            copy(k->file, sizeof k->file, file, strlen(file));
            return 0;
        }
        if (type != 84 && p->unsupported_count < MAX_UNSUPPORTED) p->unsupported[p->unsupported_count++] = type;
        return 0;
    }
    int full = d && (d->flags & DC_FULLSLOT);           /* 매퍼 RAM·MegaRAM·External RAM: 블록 수 = 크기 */
    if (slot < 0 || slot > 3 || sub < 0 || sub > 3 || start < 0 || (!full && start + count > 8)) {
        set_err(err, errlen, "slot/block out of range: ", line);
        return -1;
    }
    if (p->region_count >= MAX_REGIONS) { set_err(err, errlen, "too many regions", 0); return -1; }
    ProfileRegion *r = &p->region[p->region_count];
    memset(r, 0, sizeof *r);
    r->slot = (uint8_t)slot;
    r->subslot = (uint8_t)sub;
    r->start_block = (uint8_t)start;
    r->block_count = (uint8_t)count;
    r->blue_type = type;
    switch (d ? d->mem : DC_MEM_NONE) {
    case DC_MEM_REGION:                                 /* 일반·BASIC 롬, RAM, PAC, 펌웨어, 디스크, DOS2 … (78 = MSX-MUSIC 롬 + YM2413) */
        r->type = (RegionType)d->mem_arg;
        if (d->mem_feat) set_feature(p, d->mem_feat, file);
        break;
    case DC_MEM_MAPRAM:                                 /* 메모리 매퍼: 크기는 블록 수, 슬롯 전체(8블록)를 차지 */
        r->type = REGION_RAM_MAPPER;
        r->size_blocks = (uint16_t)count;
        r->start_block = 0;
        r->block_count = 8;
        break;
    case DC_MEM_AUDIO:                                  /* 3.6.8: MSX-AUDIO (Music Module 롬 + Y8950) */
        set_feature(p, d->mem_feat, file);
        if (!file[0]) return 0;
        r->type = (RegionType)d->mem_arg; break;
    case DC_MEM_CART:                                   /* 3.6.8: 카트리지 매퍼를 기종 내장으로 */
        r->type = REGION_CART; r->cart_mapper = d->mem_arg;
        if (full) { r->start_block = 0; r->block_count = 8; }   /* MegaRAM: 블록 수 = 크기, 슬롯 전체 */
        profile_cart_buf(type, count, &r->cart_size, &r->cart_fill);
        break;
    default:                                            /* 아직 없는 장치: 빼고 실행 (요구사항 PROF-05) */
        p->unknown_at[slot][sub] = 1;
        if (p->unsupported_count < MAX_UNSUPPORTED) p->unsupported[p->unsupported_count++] = type;
        return 0;
    }
    if (r->type != REGION_RAM && r->type != REGION_RAM_MAPPER && r->type != REGION_PAC && !(r->type == REGION_CART && r->cart_size)) {
        if (!file[0]) { set_err(err, errlen, "ROM region without file: ", line); return -1; }
        copy(r->file, sizeof r->file, file, strlen(file));
    }
    for (int i = 0; i < p->region_count; i++) {         /* 같은 슬롯/서브슬롯 블록 겹침 */
        ProfileRegion *o = &p->region[i];
        if (o->slot == r->slot && o->subslot == r->subslot &&
            r->start_block < o->start_block + o->block_count &&
            o->start_block < r->start_block + r->block_count) {
            set_err(err, errlen, "overlapping regions", 0);
            return -1;
        }
    }
    p->region_count++;
    return 0;
}

/* 3.6.8: config.ini 형식 번호 → 기종 내장 카트리지 매퍼 (없으면 -1). 5.2.8: 장치 목록에서 */
int profile_cart_mapper(int type)
{
    const DevInfo *d = devcat_find(type);
    return d && d->mem == DC_MEM_CART ? d->mem_arg : -1;
}
/* 파일 없이 만드는 카트리지의 버퍼 크기·채움 값 (파일이 있으면 그 크기). 5.2.8: 장치 목록에서 */
void profile_cart_buf(int type, int count, uint32_t *size, uint8_t *fill)
{
    const DevInfo *d = devcat_find(type);
    *size = 0; *fill = 0xFF;
    if (!d || d->mem != DC_MEM_CART) return;
    switch (d->buf_rule) {
    case DC_BUF_EXTRAM: *size = (uint32_t)(count >= 2 && count <= 8 ? count : 8) * 8192u; *fill = 0; break;
    case DC_BUF_MEGARAM: *size = (uint32_t)(count > 8 ? count : 64) * 8192u; if (*size > (1u << 20)) *size = 1u << 20; break;   /* MegaRAM: 블록 수 (없으면 512 KiB) */
    default: *size = (uint32_t)d->buf_kb * 1024u; *fill = d->buf_fill; break;
    }
}
/* 3.6.8: X86MSX가 켜는 형식 (머신 편집기의 "미지원" 표시) */
int profile_type_supported(int type) { return devcat_supported(type); }

int profile_parse(const char *text, size_t len, MachineProfile *p, char *err, size_t errlen)
{
    char line[512];
    char section[64] = "";
    memset(p, 0, sizeof *p);
    p->z80_hz = 3579545;
    p->vram_kib = 16;
    p->ext_slot[0] = 1;
    p->ext_slot[1] = 2;
    p->ext_slot[2] = p->ext_slot[3] = p->ext_slot[4] = -1;
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && text[j] != '\n') j++;
        size_t n = j - i;
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, text + i, n);
        line[n] = 0;
        i = j + 1;
        /* 앞뒤 공백 제거 */
        char *b = line;
        while (is_space(*b)) b++;
        char *e = b + strlen(b);
        while (e > b && is_space(e[-1])) *--e = 0;
        if (!*b || *b == ';' || *b == '#') continue;
        if (*b == '[') {
            char *c = strchr(b, ']');
            copy(section, sizeof section, b + 1, c ? (size_t)(c - b - 1) : strlen(b + 1));
            continue;
        }
        if (!strcmp(section, "Slots")) {
            if (parse_slot_line(b, p, err, errlen)) return -1;
            continue;
        }
        char *eq = strchr(b, '=');
        if (!eq) continue;
        char *kend = eq;
        while (kend > b && is_space(kend[-1])) kend--;
        size_t klen = (size_t)(kend - b);
        char *val = eq + 1;
        while (is_space(*val)) val++;
        if (!strcmp(section, "General")) {
            if (str_eq(b, klen, "Id")) copy(p->id, sizeof p->id, val, strlen(val));
            else if (str_eq(b, klen, "Manufacturer")) copy(p->manufacturer, sizeof p->manufacturer, val, strlen(val));
            else if (str_eq(b, klen, "Model")) copy(p->model, sizeof p->model, val, strlen(val));
            else if (str_eq(b, klen, "KeyLayout")) copy(p->key_layout, sizeof p->key_layout, val, strlen(val));
        } else if (!strcmp(section, "Video")) {
            if (str_eq(b, klen, "version")) copy(p->vdp, sizeof p->vdp, val, strlen(val));
            else if (str_eq(b, klen, "vram size")) p->vram_kib = (int)to_long(val);
        } else if (!strcmp(section, "Subslotted Slots")) {
            if (klen == 6 && !memcmp(b, "slot ", 5) && b[5] >= '0' && b[5] <= '3') p->subslotted[b[5] - '0'] = (int)to_long(val);
        } else if (!strcmp(section, "External Slots")) {
            int x = str_eq(b, klen, "slot A") ? 0 : str_eq(b, klen, "slot B") ? 1 : str_eq(b, klen, "slot C") ? 2 : str_eq(b, klen, "slot D") ? 3 : str_eq(b, klen, "slot E") ? 4 : -1;
            if (x >= 0) {
                const char *q = val;
                p->ext_slot[x] = (int)to_long(q);
                while (*q == ' ' || *q == '-' || (*q >= '0' && *q <= '9')) { if (*q == ' ') { p->ext_ss[x] = (int)to_long(q + 1) & 3; break; } q++; }
            }
        } else if (!strcmp(section, "CMOS")) {
            if (str_eq(b, klen, "Enable CMOS")) p->cmos = (int)to_long(val);
            else if (str_eq(b, klen, "Battery Backed")) p->cmos_battery = (int)to_long(val);
        } else if (!strcmp(section, "FDC")) {
            if (str_eq(b, klen, "Count")) p->fdc_count = (int)to_long(val);
        } else if (!strcmp(section, "CPU")) {
            if (str_eq(b, klen, "Z80 Frequency")) p->z80_hz = (uint32_t)to_long(val);
        } else if (!strcmp(section, "Board")) {
            if (str_eq(b, klen, "type")) copy(p->board, sizeof p->board, val, strlen(val));
        }
    }
    if (p->region_count == 0) { set_err(err, errlen, "no memory regions", 0); return -1; }
    return 0;
}

#if __STDC_HOSTED__
#include <stdio.h>
#include <stdlib.h>

/* 호스트: dir/config.ini를 읽고, ROM 경로는 dir + 파일 이름으로 바꾼다 */
int profile_load(const char *dir, MachineProfile *p, char *err, size_t errlen)
{
    char path[512];
    snprintf(path, sizeof path, "%s/config.ini", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "cannot open %s", path); return -1; }
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (profile_parse(buf, n, p, err, errlen)) return -1;
    for (int i = 0; i < p->region_count; i++) {
        ProfileRegion *r = &p->region[i];
        if (!r->file[0]) continue;
        const char *base = r->file;
        for (const char *s = r->file; *s; s++) if (*s == '/' || *s == '\\') base = s + 1;
        char tmp[256];
        snprintf(tmp, sizeof tmp, "%s/%s", dir, base);
        memcpy(r->file, tmp, sizeof tmp);
    }
    return 0;
}
#endif
