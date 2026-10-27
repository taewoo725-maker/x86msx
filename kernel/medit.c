/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * medit.c - 머신 편집기 자료 (2.9). blueMSX config.ini ↔ MeMachine.
 * 편집하는 키: [General] Id·Manufacturer·Model·System·KeyLayout, [CMOS], [Video], [Subslotted Slots],
 * [External Slots], [FDC] Count, [CPU] Z80 Frequency, [Board] type, [Slots]. 나머지는 other에 원문 보존.
 */
#include "medit.h"
#include "../src/profile.h"
#include "../src/devcat.h"
#include <string.h>

/* ---------- 작은 도우미 ---------- */
static void cpy(char *d, int max, const char *s, int n)
{
    if (n < 0) n = (int)strlen(s);
    if (n > max - 1) n = max - 1;
    memcpy(d, s, (size_t)n);
    d[n] = 0;
}
static int is_sp(int c) { return c == ' ' || c == '\t' || c == '\r'; }
static long to_num(const char *s)
{
    long v = 0; int neg = 0;
    while (is_sp(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}
static const char *rd_num(const char *s, long *v)
{
    while (is_sp(*s)) s++;
    if (!(*s == '-' || (*s >= '0' && *s <= '9'))) return 0;
    *v = to_num(s);
    if (*s == '-') s++;
    while (*s >= '0' && *s <= '9') s++;
    return s;
}
static int keq(const char *k, int kl, const char *w) { return (int)strlen(w) == kl && !memcmp(k, w, (size_t)kl); }

typedef struct { char *p; int n, max, bad; } Out;
static void put(Out *o, const char *s)
{
    while (*s) { if (o->n < o->max - 1) o->p[o->n++] = *s; else o->bad = 1; s++; }
}
static void putn(Out *o, long v)
{
    char t[24]; int k = 0;
    if (v < 0) { put(o, "-"); v = -v; }
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    char r[24]; for (int i = 0; i < k; i++) r[i] = t[k - 1 - i]; r[k] = 0;
    put(o, r);
}
static void kv(Out *o, const char *k, const char *v) { put(o, k); put(o, "="); put(o, v); put(o, "\r\n"); }
static void kvn(Out *o, const char *k, long v) { put(o, k); put(o, "="); putn(o, v); put(o, "\r\n"); }

/* ---------- 형식 (2.15: blueMSX 2.8.2 머신 편집기의 "형식" 목록과 같은 순서·이름·번호) ----------
 * 표는 장치 목록(src/devcat.c) 하나 — 기종 불러오기와 편집기가 같은 표를 봄. 편집기 모양(kind·win)도 거기 */
static MeTypeInfo tinfo[160];
static int tinfo_n = -1;
static void tinfo_init(void)
{
    if (tinfo_n >= 0) return;
    int n = devcat_editor_count();
    if (n > (int)(sizeof tinfo / sizeof tinfo[0])) n = (int)(sizeof tinfo / sizeof tinfo[0]);
    for (int i = 0; i < n; i++) { const DevInfo *d = devcat_at(i); tinfo[i].t = d->type; tinfo[i].name = d->name; tinfo[i].kind = d->ed_kind; tinfo[i].win = d->ed_win; }
    tinfo_n = n;
}
/* X86MSX 기종 불러오기(src/profile.c)가 아는 형식 */
int me_type_supported(int type) { return profile_type_supported(type); }   /* 3.6.8: 기종 불러오기(src/profile.c)와 같은 목록 */
int me_type_count(void) { tinfo_init(); return tinfo_n; }
const MeTypeInfo *me_type_at(int i) { return i >= 0 && i < me_type_count() ? &tinfo[i] : 0; }
int me_type_index(int type) { for (int i = 0; i < me_type_count(); i++) if (tinfo[i].t == type) return i; return -1; }
const MeTypeInfo *me_type_info(int type) { int i = me_type_index(type); return i < 0 ? 0 : &tinfo[i]; }
const char *me_type_name(int type) { const MeTypeInfo *t = me_type_info(type); return t ? t->name : 0; }
int me_type_io(int type) { const MeTypeInfo *t = me_type_info(type); return t && (t->kind == ME_K_DEV || t->kind == ME_K_DEVFILE); }
int me_type_rom(int type)
{
    const MeTypeInfo *t = me_type_info(type);
    return t && (t->kind == ME_K_ROM || t->kind == ME_K_WIN || t->kind == ME_K_FULL || t->kind == ME_K_DEVFILE) && type != 120;
}
/* 이 형식의 메모리 창 크기 (8 KiB 블록). count = config.ini의 블록 수 */
int me_window(int type, int count)
{
    const MeTypeInfo *t = me_type_info(type);
    if (!t) return count > 8 ? 8 : count;
    switch (t->kind) {
    case ME_K_MAPRAM: case ME_K_FULL: return 8;
    case ME_K_DEV: case ME_K_DEVFILE: return 0;
    case ME_K_WIN: case ME_K_NOFILE: case ME_K_RAMFIX: return t->win;
    default: return count < 1 ? 1 : count > 8 ? 8 : count;
    }
}
/* 크기 고르기 (RAM 종류만). sizes는 8 KiB 블록 수, 개수 반환 */
int me_size_options(int type, int *pages, int max)
{
    const MeTypeInfo *t = me_type_info(type);
    static const int ram[] = { 1, 2, 4, 8 }, map[] = { 8, 16, 32, 64, 128, 256, 512 };
    const int *src = 0; int n = 0;
    if (t && t->kind == ME_K_RAM) { src = ram; n = 4; }
    else if (t && t->kind == ME_K_MAPRAM) { src = map; n = 7; }
    if (n > max) n = max;
    for (int i = 0; i < n; i++) pages[i] = src[i];
    return n;
}
/* 블루MSX 롬 데이터베이스의 <type> 글자 → 형식 번호 (모르면 -1) */
int me_db_type(const char *s)
{
    static const struct { const char *k; int t; } m[] = {
        { "Normal", 20 }, { "Mirrored", 19 }, { "0x4000", 42 }, { "0xC000", 86 }, { "basic", 39 }, { "DiskPatch", 21 }, { "CasPatch", 66 },
        { "MB8877A", 64 }, { "WD2793", 65 }, { "Microsol", 63 }, { "TC8566AF", 57 }, { "TC8566AFTR", 143 }, { "SVI738FDC", 67 },
        { "MSX-DOS2", 2 }, { "msxdos2", 2 }, { "MSXDOS2", 2 }, { "Kanji1", 24 }, { "Kanji12", 30 }, { "Jisyo", 29 }, { "Bunsetsu", 28 },
        { "PanasonicDram", 141 }, { "Panasonic8", 139 }, { "PanasonicWx16", 142 }, { "Panasonic16", 27 }, { "Panasonic32", 68 },
        { "A1FMModem", 140 }, { "National", 31 }, { "MSX-MUSIC", 78 }, { "MSX-AUDIO", 50 }, { "MoonSound", 79 }, { "FMPAC", 16 },
        { "FMPAK", 87 }, { "SFG-01", 106 }, { "SFG-05", 105 }, { "SunriseIDE", 96 }, { "BeerIDE", 98 }, { "GoudaSCSI", 135 },
        { "NoWind", 134 }, { "Obsonet", 110 }, { "ForteII", 138 }, { "Halnote", 40 }, { "VMX80", 100 }, { "SonyHBI-V1", 102 },
        { "SVI727", 103 }, { "KonamiSCC", 3 }, { "Konami", 4 }, { "GenericKonami", 17 }, { "ASCII8", 5 }, { "ASCII16", 6 },
        { "ASCII8SRAM", 8 }, { "ASCII16SRAM", 9 }, { "ASCII8SRAM8", 8 }, { "ASCII16SRAM2", 9 }, { "ASCII16SRAM8", 9 },
        { "KoeiSRAM8", 38 }, { "KoeiSRAM32", 38 }, { "GameMaster2", 7 }, { "RType", 10 }, { "CrossBlaim", 11 }, { "HarryFox", 12 },
        { "Zemina80in1", 13 }, { "Zemina126in1", 14 }, { "Zemina90in1", 51 }, { "SCC", 55 }, { "SCC+", 56 }, { "Majutsushi", 62 },
        { "Synthesizer", 61 }, { "KeyboardMaster", 91 }, { "WordPro", 99 }, { "SuperPierrot", 18 }, { "SuperLodeRunner", 41 },
        { "Manbow2", 136 }, { "MatraInk", 144 }, { "NettouYakyuu", 145 }, { "Playball", 109 }, { "HolyQuran", 25 },
    };
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) if (!strcmp(m[i].k, s)) return m[i].t;
    return -1;
}

/* ---------- 읽기 ---------- */
static const char *const known[9] = { "General", "CMOS", "Video", "Subslotted Slots", "External Slots", "FDC", "CPU", "Board", "Slots" };
static int known_idx(const char *sec) { for (int i = 0; i < 9; i++) if (!strcmp(sec, known[i])) return i; return -1; }
static void add_extra(MeMachine *m, int k, const char *line)
{
    int n = (int)strlen(m->sec_extra[k]), l = (int)strlen(line);
    if (n + l + 3 >= (int)sizeof m->sec_extra[k]) return;
    memcpy(m->sec_extra[k] + n, line, (size_t)l); memcpy(m->sec_extra[k] + n + l, "\r\n", 3);
}
int me_parse(const char *text, uint32_t len, MeMachine *m)
{
    memset(m, 0, sizeof *m);
    m->ext_ps[0] = 1; m->ext_ps[1] = 2; m->ext_ps[2] = m->ext_ps[3] = m->ext_ps[4] = -1;
    m->vram = 16; m->fdc = -1; m->z80hz = 3579545;
    cpy(m->vdp, sizeof m->vdp, "TMS99x8A", -1);
    char sec[48] = "";
    int other_sec_written = 0;
    Out ot = { m->other, 0, ME_OTHER, 0 };
    uint32_t i = 0;
    while (i < len) {
        uint32_t j = i;
        while (j < len && text[j] != '\n') j++;
        char line[400];
        int n = (int)(j - i);
        cpy(line, sizeof line, text + i, n);
        i = j + 1;
        char *b = line;
        while (is_sp(*b)) b++;
        char *e = b + strlen(b);
        while (e > b && is_sp(e[-1])) *--e = 0;
        if (!*b) continue;
        if (*b == '[') {
            char *c = strchr(b, ']');
            cpy(sec, sizeof sec, b + 1, c ? (int)(c - b - 1) : -1);
            other_sec_written = 0;
            continue;
        }
        int ki = known_idx(sec);
        int used = 0;
        if (!strcmp(sec, "Slots") && *b != ';' && *b != '#') {
            long v[5];
            const char *s = b;
            int ok = 1;
            for (int k = 0; k < 5 && ok; k++) { s = rd_num(s, &v[k]); if (!s) ok = 0; }
            if (ok && m->nslot < ME_SLOTS) {
                MeSlot *q = &m->slot[m->nslot++];
                q->ps = (int)v[0]; q->ss = (int)v[1]; q->start = (int)v[2]; q->count = (int)v[3]; q->type = (int)v[4];
                const char *a = strchr(s, '"');
                if (a) {
                    const char *z = strchr(a + 1, '"');
                    if (z) {
                        cpy(q->file, sizeof q->file, a + 1, (int)(z - a - 1));
                        const char *a2 = strchr(z + 1, '"');
                        const char *z2 = a2 ? strchr(a2 + 1, '"') : 0;
                        if (z2) cpy(q->extra, sizeof q->extra, a2 + 1, (int)(z2 - a2 - 1));
                    }
                }
                used = 1;
            }
        } else if (*b != ';' && *b != '#') {
            char *eq = strchr(b, '=');
            if (eq) {
                char *ke = eq;
                while (ke > b && is_sp(ke[-1])) ke--;
                int kl = (int)(ke - b);
                char *v = eq + 1;
                while (is_sp(*v)) v++;
                used = 1;
                if (!strcmp(sec, "General")) {
                    if (keq(b, kl, "Id")) cpy(m->name, sizeof m->name, v, -1);
                    else if (keq(b, kl, "Manufacturer")) cpy(m->manufacturer, sizeof m->manufacturer, v, -1);
                    else if (keq(b, kl, "Model")) cpy(m->model, sizeof m->model, v, -1);
                    else if (keq(b, kl, "System")) cpy(m->system, sizeof m->system, v, -1);
                    else if (keq(b, kl, "KeyLayout")) cpy(m->keylayout, sizeof m->keylayout, v, -1);
                    else used = 0;
                } else if (!strcmp(sec, "CMOS")) {
                    if (keq(b, kl, "Enable CMOS")) m->cmos = (int)to_num(v);
                    else if (keq(b, kl, "Battery Backed")) m->battery = (int)to_num(v);
                    else used = 0;
                } else if (!strcmp(sec, "Video")) {
                    if (keq(b, kl, "version")) cpy(m->vdp, sizeof m->vdp, v, -1);
                    else if (keq(b, kl, "vram size")) m->vram = (int)to_num(v);
                    else used = 0;
                } else if (!strcmp(sec, "Subslotted Slots")) {
                    if (kl == 6 && !memcmp(b, "slot ", 5) && b[5] >= '0' && b[5] <= '3') m->subslotted[b[5] - '0'] = (int)to_num(v);
                    else used = 0;
                } else if (!strcmp(sec, "External Slots")) {
                    int x = keq(b, kl, "slot A") ? 0 : keq(b, kl, "slot B") ? 1 : keq(b, kl, "slot C") ? 2 : keq(b, kl, "slot D") ? 3 : keq(b, kl, "slot E") ? 4 : -1;
                    if (x >= 0) { long a, c = 0; const char *s = rd_num(v, &a); if (s) { m->ext_ps[x] = (int)a; if (rd_num(s, &c)) m->ext_ss[x] = (int)c; } }
                    else used = 0;
                } else if (!strcmp(sec, "FDC")) {
                    if (keq(b, kl, "Count")) m->fdc = (int)to_num(v); else used = 0;
                } else if (!strcmp(sec, "CPU")) {
                    if (keq(b, kl, "Z80 Frequency")) m->z80hz = (uint32_t)to_num(v);
                    else if (keq(b, kl, "R800 Frequency")) m->r800hz = (uint32_t)to_num(v);
                    else used = 0;
                } else if (!strcmp(sec, "Board")) {
                    if (keq(b, kl, "type")) cpy(m->board, sizeof m->board, v, -1); else used = 0;
                } else used = 0;
            }
        }
        if (!used) {                                    /* 보존 */
            if (ki >= 0) add_extra(m, ki, b);
            else {
                if (!other_sec_written) { put(&ot, "["); put(&ot, sec); put(&ot, "]\r\n"); other_sec_written = 1; }
                put(&ot, b); put(&ot, "\r\n");
            }
        }
    }
    m->other[ot.n] = 0;
    return m->nslot ? 0 : -1;
}

/* ---------- 쓰기 ---------- */
int me_write(const MeMachine *m, char *out, int max)
{
    Out o = { out, 0, max, 0 };
    put(&o, "[General]\r\n");
    kv(&o, "Id", m->name);
    if (m->manufacturer[0]) kv(&o, "Manufacturer", m->manufacturer);
    if (m->model[0]) kv(&o, "Model", m->model);
    if (m->system[0]) kv(&o, "System", m->system);
    if (m->keylayout[0]) kv(&o, "KeyLayout", m->keylayout);
    put(&o, m->sec_extra[0]);
    put(&o, "\r\n[CMOS]\r\n");
    kvn(&o, "Enable CMOS", m->cmos);
    kvn(&o, "Battery Backed", m->battery);
    put(&o, m->sec_extra[1]);
    put(&o, "\r\n[Video]\r\n");
    kv(&o, "version", m->vdp);
    put(&o, "vram size="); putn(&o, m->vram); put(&o, "kB\r\n");
    put(&o, m->sec_extra[2]);
    put(&o, "\r\n[Subslotted Slots]\r\n");
    for (int i = 0; i < 4; i++) { char k[8] = "slot 0"; k[5] = (char)('0' + i); kvn(&o, k, m->subslotted[i]); }
    put(&o, m->sec_extra[3]);
    put(&o, "\r\n[External Slots]\r\n");
    for (int x = 0; x < 5; x++) {
        if (x >= 2 && (m->ext_ps[x] < 0 || m->ext_ps[x] > 3)) continue;   /* 3.3.1: C·D는 있을 때만 */
        static const char *const kn[5] = { "slot A=", "slot B=", "slot C=", "slot D=", "slot E=" };
        put(&o, kn[x]); putn(&o, m->ext_ps[x]); put(&o, " "); putn(&o, m->ext_ss[x]); put(&o, "\r\n");
    }
    put(&o, m->sec_extra[4]);
    if (m->fdc >= 0 || m->sec_extra[5][0]) { put(&o, "\r\n[FDC]\r\n"); if (m->fdc >= 0) kvn(&o, "Count", m->fdc); put(&o, m->sec_extra[5]); }
    put(&o, "\r\n[CPU]\r\n");
    put(&o, "Z80 Frequency="); putn(&o, (long)m->z80hz); put(&o, "Hz\r\n");
    if (m->r800hz) { put(&o, "R800 Frequency="); putn(&o, (long)m->r800hz); put(&o, "Hz\r\n"); }
    put(&o, m->sec_extra[6]);
    if (m->board[0] || m->sec_extra[7][0]) { put(&o, "\r\n[Board]\r\n"); if (m->board[0]) kv(&o, "type", m->board); put(&o, m->sec_extra[7]); }
    put(&o, "\r\n[Slots]\r\n");
    put(&o, m->sec_extra[8]);
    for (int i = 0; i < m->nslot; i++) {
        const MeSlot *q = &m->slot[i];
        putn(&o, q->ps); put(&o, " "); putn(&o, q->ss); put(&o, " "); putn(&o, q->start); put(&o, " ");
        putn(&o, q->count); put(&o, " "); putn(&o, q->type); put(&o, " \""); put(&o, q->file); put(&o, "\" \""); put(&o, q->extra); put(&o, "\"\r\n");
    }
    if (m->other[0]) { put(&o, "\r\n"); put(&o, m->other); }
    if (o.bad) return -1;
    out[o.n] = 0;
    return o.n;
}

/* ---------- 검사 ---------- */
static void msg(char (*msgs)[ME_MSG], int max, int *n, const char *a, long v1, const char *b)
{
    if (*n >= max) { (*n)++; return; }
    Out o = { msgs[*n], 0, ME_MSG, 0 };
    put(&o, a);
    if (v1 >= 0) putn(&o, v1);
    if (b) put(&o, b);
    msgs[*n][o.n] = 0;
    (*n)++;
}

int me_check(const MeMachine *m, int (*exists)(const char *path), char (*msgs)[ME_MSG], int max)
{
    int n = 0;
    if (!m->name[0]) msg(msgs, max, &n, "NAME", -1, 0);
    for (const char *p = m->name; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') { msg(msgs, max, &n, "NAMECHAR", -1, 0); break; }
    int ram = 0;
    for (int i = 0; i < m->nslot; i++) {
        const MeSlot *q = &m->slot[i];
        if (q->type == 23 || q->type == 22 || q->type == 69) ram = 1;
        int qw = me_window(q->type, q->count);
        if (qw == 0 || q->count == 0) { if (me_type_rom(q->type) && !q->file[0]) msg(msgs, max, &n, "NOFILE ", i + 1, 0); continue; }   /* I/O 장치 */
        if (q->ps < 0 || q->ps > 3 || q->ss < 0 || q->ss > 3) { msg(msgs, max, &n, "RANGE ", i + 1, 0); continue; }
        if (q->ss && !m->subslotted[q->ps]) msg(msgs, max, &n, "NOTSUB ", i + 1, 0);
        int qs = qw == 8 ? 0 : q->start;
        if (qs < 0 || qs + qw > 8) msg(msgs, max, &n, "RANGE ", i + 1, 0);
        for (int k = 0; k < i; k++) {                   /* 같은 슬롯/서브슬롯 블록 겹침 */
            const MeSlot *r = &m->slot[k];
            int rw = me_window(r->type, r->count);
            if (rw == 0 || r->count == 0) continue;
            int rs = rw == 8 ? 0 : r->start;
            if (r->ps == q->ps && (r->ss == q->ss || !m->subslotted[q->ps]) && qs < rs + rw && rs < qs + qw) { msg(msgs, max, &n, "OVERLAP ", k + 1, 0); break; }
        }
        if (me_type_rom(q->type)) {
            if (!q->file[0]) msg(msgs, max, &n, "NOFILE ", i + 1, 0);
            else if (exists && !exists(q->file)) { msg(msgs, max, &n, "MISSING ", i + 1, 0); }
        }
    }
    if (!ram) msg(msgs, max, &n, "NORAM", -1, 0);
    if (strcmp(m->vdp, "TMS99x8A") && strcmp(m->vdp, "TMS9929A") && strcmp(m->vdp, "TMS9918A") && strcmp(m->vdp, "V9938") && strcmp(m->vdp, "V9958"))
        msg(msgs, max, &n, "VDP", -1, 0);
    for (int x = 0; x < 5; x++) if ((x < 2 || m->ext_ps[x] != -1) && (m->ext_ps[x] < 0 || m->ext_ps[x] > 3)) msg(msgs, max, &n, "EXTSLOT", -1, 0);
    return n;
}
