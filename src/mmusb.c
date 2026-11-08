/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mmusb.c (8.0.80) - X86 Multimedia USB 직접 접근 (읽기 전용). 규격은 mmusb.h. */
#include <string.h>
#include "mmusb.h"

#define RES_MAX 1100
#define MAXV    32
#define NAME_MAX_OUT 100              /* 한 번에 보내는 이름 최대 바이트 */


static const UsbHost *host;
static UsbEnt ents[MMUSB_MAXE];
static struct {
    UsbVol vol[MAXV]; int nv; uint8_t gen; uint32_t sig;
    int sel;                                             /* 고른 USB 번호 (-1 = 없음) */
    char path[1024];                                      /* 지금 폴더 */
    int nent, trunc; uint16_t ord[MMUSB_MAXE]; char lpath[1024]; uint8_t lfilter; int lvalid;
    char rpath[1024]; int rn, rvalid; uint32_t rms;       /* 8.0.108 폴더 원본 목록 캐시 */
    int fopen_; char fpath[1100]; uint32_t fsize, fpos;
    uint8_t res[RES_MAX]; int res_n, res_pos;
} S;

void mmusb_set_host(const UsbHost *h) { host = h; }
void mmusb_flush(void) { S.rvalid = 0; S.lvalid = 0; }   /* 8.0.108 목록 캐시 비우기 (시험·USB 다시 읽기) */
int mmusb_available(void) { return host && host->vols && host->list && host->read_at; }

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scopy(char *d, int max, const char *s) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int ucase(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int ncmp(const char *a, const char *b) { while (*a && ucase((uint8_t)*a) == ucase((uint8_t)*b)) { a++; b++; } return ucase((uint8_t)*a) - ucase((uint8_t)*b); }
static char sepc(void) { return host && host->sep ? host->sep : (char)92; }

void mmusb_reset(void)
{
    memset(&S, 0, sizeof S);
    S.sel = -1;
}
static void res_clear(void) { S.res_n = S.res_pos = 0; }
static void res_put(uint8_t b) { if (S.res_n < RES_MAX) S.res[S.res_n++] = b; }
static void res_u16(uint32_t v) { res_put((uint8_t)v); res_put((uint8_t)(v >> 8)); }
static void res_u32(uint32_t v) { res_u16(v & 0xFFFF); res_u16(v >> 16); }
uint8_t mmusb_get(void) { return S.res_pos < S.res_n ? S.res[S.res_pos++] : 0; }

static void close_file(void) { S.fopen_ = 0; S.fsize = S.fpos = 0; }
static void deselect(void) { S.rvalid = 0; S.sel = -1; S.path[0] = 0; S.lvalid = 0; S.nent = 0; close_file(); }

/* USB 목록을 새로 읽고, 달라졌으면 바뀜 번호 +1. 고른 USB 가 사라지거나 바뀌었으면 선택 해제 */
static void refresh_vols(void)
{
    char selkey[8] = ""; if (S.sel >= 0 && S.sel < S.nv) scopy(selkey, sizeof selkey, S.vol[S.sel].key);
    char selname[64] = ""; if (S.sel >= 0 && S.sel < S.nv) scopy(selname, sizeof selname, S.vol[S.sel].name);
    int n = host && host->vols ? host->vols(S.vol, MAXV) : -1;
    if (n < 0) n = 0;
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) { for (const char *c = S.vol[i].key; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u; for (const char *c = S.vol[i].name; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u; h = (h ^ 0xFF) * 16777619u; }
    S.nv = n;
    if (h != S.sig) { S.sig = h; S.gen++; }
    if (S.sel >= 0) {                                    /* 같은 번호가 같은 USB(키·이름)인지 */
        int ok = S.sel < n && !strcmp(S.vol[S.sel].key, selkey) && !strcmp(S.vol[S.sel].name, selname);
        if (!ok) deselect();
    }
}

/* ---- 폴더 목록 ---- */
static int ext_is(const char *n, const char *e)
{
    int l = slen(n), k = slen(e);
    if (l <= k + 1 || n[l - k - 1] != '.') return 0;
    for (int i = 0; i < k; i++) if (ucase((uint8_t)n[l - k + i]) != ucase((uint8_t)e[i])) return 0;
    return 1;
}
static int pass_filter(const UsbEnt *e, uint8_t f)
{
    if (e->is_dir || !f) return 1;
    int audio = ext_is(e->name, "mp3") || ext_is(e->name, "wav") || ext_is(e->name, "flac");
    int mv2 = ext_is(e->name, "mv2");
    int mp4 = ext_is(e->name, "mp4") || ext_is(e->name, "m4v") || ext_is(e->name, "mov") || ext_is(e->name, "avi");   /* 8.0.117 avi 도 목록에 (재생은 오류 11) */
    int mvx = ext_is(e->name, "mv5") || ext_is(e->name, "mv8") || ext_is(e->name, "mvc");   /* 8.0.101 필터 bit4 (8.0.149: MV6·MV7·MVA·MVB 는 없앰, 8.0.150: MV8 되살림) */
    return ((f & 1) && audio) || ((f & 2) && mv2) || ((f & 8) && (mv2 || mp4)) || ((f & 16) && mvx) || ((f & 4) && !audio && !mv2 && !((f & 8) && mp4) && !((f & 16) && mvx));
}
static int ord_gt(int a, int b)
{
    const UsbEnt *x = &ents[a], *y = &ents[b];
    if (x->is_dir != y->is_dir) return !x->is_dir && y->is_dir;
    return ncmp(x->name, y->name) > 0;
}
static int load_list(uint8_t filter)
{
    if (S.lvalid && S.lfilter == filter && !strcmp(S.lpath, S.path)) return 0;
    S.lvalid = 0; S.nent = 0; S.trunc = 0;
    if (S.sel < 0 || !host || !host->list) return -1;
    uint32_t now = mmusb_ms ? mmusb_ms() : 0;
    int n;
    if (S.rvalid && !strcmp(S.rpath, S.path) && (!mmusb_ms || now - S.rms < 20000u)) n = S.rn;   /* 필터만 바뀐 경우 USB를 다시 읽지 않음 */
    else {
        S.rvalid = 0;
        n = host->list(S.path, ents, MMUSB_MAXE);
        if (n < 0) return -1;
        S.rn = n; scopy(S.rpath, sizeof S.rpath, S.path); S.rms = now; S.rvalid = 1;
    }
    if (n >= MMUSB_MAXE) S.trunc = 1;
    int k = 0;
    for (int i = 0; i < n; i++) {
        UsbEnt *e = &ents[i];
        if (!e->name[0] || (e->name[0] == '.' && (!e->name[1] || (e->name[1] == '.' && !e->name[2])))) continue;
        if (!ncmp(e->name, "X86MSX.VHD")) continue;                 /* 8.0.159: 가상 하드 설정 파일은 MSX 쪽 어디서도 안 보임 */
        if (!pass_filter(e, filter)) continue;
        S.ord[k++] = (uint16_t)i;
    }
    for (int gap = k / 2; gap > 0; gap /= 2)             /* 셸 정렬 */
        for (int i = gap; i < k; i++) {
            uint16_t t = S.ord[i]; int j = i;
            while (j - gap >= 0 && ord_gt(S.ord[j - gap], t)) { S.ord[j] = S.ord[j - gap]; j -= gap; }
            S.ord[j] = t;
        }
    S.nent = k; S.lfilter = filter; scopy(S.lpath, sizeof S.lpath, S.path); S.lvalid = 1;
    return 0;
}
static const UsbEnt *entry(int idx) { return (S.lvalid && idx >= 0 && idx < S.nent) ? &ents[S.ord[idx]] : 0; }
static int child_path(char *out, int max, const UsbEnt *e)
{
    scopy(out, max, S.path);
    int n = slen(out); if (n + 2 >= max) return -1;
    out[n++] = sepc(); out[n] = 0;
    if (n + slen(e->name) + 1 >= max) return -1;
    scopy(out + n, max - n, e->name);
    return 0;
}

/* 소리 쪽(mvsnd 0Ch)이 USB 에서 OPEN 해 둔 파일을 읽을 때 */
int mmusb_cur_file(char *path, int max, uint32_t *size)
{
    if (!S.fopen_) return 0;
    scopy(path, max, S.fpath); if (size) *size = S.fsize;
    return 1;
}

/* 8.0.82 영상 OPEN(P0P1=FFFFh)이 쓰는 것: 마지막 LIST 의 idx 번째 파일 경로·크기. USB 접근의 열린 파일(25h)은 건드리지 않는다 */
void mmusb_dbg(int *sel, int *nent, int *valid, int *filter) { *sel = S.sel; *nent = S.nent; *valid = S.lvalid; *filter = S.lfilter; }   /* 8.0.107 진단 */
int mmusb_entry_file(int idx, char *path, int max, uint32_t *size)
{
    const UsbEnt *e = S.sel >= 0 ? entry(idx) : 0;
    char np[1100];
    if (!e || e->is_dir || child_path(np, sizeof np, e)) return 0;
    scopy(path, max, np); if (size) *size = e->size;
    return 1;
}

int mmusb_args(uint8_t c)
{
    switch (c) {
    case 0x20: case 0x28: case 0x29: return 0;
    case 0x21: case 0x22: return 1;
    case 0x23: case 0x25: case 0x26: return 2;
    case 0x24: case 0x27: return 4;
    default: return -1;
    }
}

void (*mmusb_crumb)(const char *what);
int mmusb_log_read = 1;                                                   /* 8.0.124 ini LogUsbRead: 1 = 기존(데이터 읽기 24 도 중요 흔적), 0 = 안 중요(재생 중 안 씀: 로그 쓰기 약 3.3초 멈춤 방지) */
int mm_crumb_quiet;                                                       /* 8.0.116: MP4 재생 중에는 자주 오는 명령의 흔적을 안 씀(USB·플래시 쓰기가 몇 초씩 멈추게 함) */
void mm_crumb_cmd(const char *tag, int c, int important)
{
    static char b[40]; int n = 0;
    if (!mmusb_crumb) return;
    if (!important && mm_crumb_quiet) return;
    if (!important) b[n++] = '~';
    while (*tag && n < 30) b[n++] = *tag++;
    b[n++] = ' '; b[n++] = "0123456789ABCDEF"[(c >> 4) & 15]; b[n++] = "0123456789ABCDEF"[c & 15]; b[n] = 0;
    mmusb_crumb(b);
}
static uint8_t mmusb_run_(uint8_t c, const uint8_t *a)
{
    uint8_t st = 0;
    res_clear();
    if (c != 0x26 && c != 0x27) mm_crumb_cmd("usb cmd", c, c != 0x20 && c != 0x29 && (mmusb_log_read || c != 0x24));
    if (!mmusb_available()) return MMU_ERR;
    switch (c) {
    case 0x20: refresh_vols(); res_put(S.gen); res_put((uint8_t)S.nv); break;
    case 0x21: {
        int i = a[0];
        if (i == 0) refresh_vols();
        if (i >= S.nv) { res_put(0xFF); st |= MMU_LEND; break; }
        res_put(0); res_put((uint8_t)i); res_put(S.vol[i].boot ? 1 : 0);
        int l = slen(S.vol[i].name); res_put((uint8_t)l);
        for (int k = 0; k < l; k++) res_put((uint8_t)S.vol[i].name[k]);
        break; }
    case 0x22:
        refresh_vols();
        if (a[0] >= S.nv) { st |= MMU_ERR; break; }
        close_file(); S.sel = a[0]; scopy(S.path, sizeof S.path, S.vol[S.sel].key); S.lvalid = 0; S.rvalid = 0; S.nent = 0;
        break;
    case 0x23: {
        int idx = a[0] | (a[1] << 8);
        if (S.sel < 0) { st |= MMU_ERR; break; }
        if (idx == 0xFFFF) {
            int n = slen(S.path), k = n;
            while (k > 0 && S.path[k - 1] != sepc()) k--;
            if (k == 0) { st |= MMU_ERR; break; }                 /* 맨 위 */
            S.path[k - 1] = 0; S.lvalid = 0; S.rvalid = 0; close_file(); break;
        }
        const UsbEnt *e = entry(idx);
        char np[1024];
        if (!e || !e->is_dir || child_path(np, sizeof np, e)) { st |= MMU_ERR; break; }
        scopy(S.path, sizeof S.path, np); S.lvalid = 0; S.rvalid = 0; S.nent = 0; close_file();
        break; }
    case 0x24: {
        uint32_t start = (uint32_t)(a[0] | (a[1] << 8)); int cnt = a[2]; uint8_t f = a[3];
        if (S.sel < 0 || load_list(f)) { st |= MMU_ERR; break; }
        if (cnt < 1) cnt = 1;
        if (cnt > 8) cnt = 8;
        res_u16((uint32_t)S.nent);
        if (start >= (uint32_t)S.nent) { res_put(0); res_put(S.trunc ? 1 : 0); st |= MMU_LEND; break; }
        int m = (int)((uint32_t)S.nent - start); if (m > cnt) m = cnt;
        res_put((uint8_t)m); res_put(S.trunc ? 1 : 0);
        for (int i = 0; i < m; i++) {
            const UsbEnt *e = entry((int)start + i);
            int l = slen(e->name); if (l > NAME_MAX_OUT) { l = NAME_MAX_OUT; while (l > 0 && ((uint8_t)e->name[l] & 0xC0) == 0x80) l--; }
            res_put(e->is_dir ? 1 : 0); res_u32(e->is_dir ? 0 : e->size); res_put((uint8_t)l);
            for (int k = 0; k < l; k++) res_put((uint8_t)e->name[k]);
        }
        break; }
    case 0x25: {
        int idx = a[0] | (a[1] << 8);
        const UsbEnt *e = entry(idx);
        close_file();
        if (S.sel < 0 || !e || e->is_dir || child_path(S.fpath, sizeof S.fpath, e)) { st |= MMU_ERR; break; }
        S.fsize = e->size; S.fpos = 0; S.fopen_ = 1;
        res_u32(S.fsize);
        break; }
    case 0x26: {
        uint32_t n = (uint32_t)(a[0] | (a[1] << 8));
        if (!S.fopen_) { st |= MMU_ERR; break; }
        if (n > 1024) n = 1024;
        if (S.fpos >= S.fsize) { st |= MMU_END; break; }
        if (n > S.fsize - S.fpos) n = S.fsize - S.fpos;
        static uint8_t tmp[1024];
        if (host->read_at(S.fpath, S.fpos, tmp, n)) { st |= MMU_ERR; break; }
        for (uint32_t i = 0; i < n; i++) res_put(tmp[i]);
        S.fpos += n;
        if (S.fpos >= S.fsize) st |= MMU_END;
        break; }
    case 0x27: {
        uint32_t p = (uint32_t)a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24;
        if (!S.fopen_) { st |= MMU_ERR; break; }
        S.fpos = p > S.fsize ? S.fsize : p;
        break; }
    case 0x28: close_file(); break;
    case 0x29:
        res_put(S.sel < 0 ? 0xFF : (uint8_t)S.sel); res_put(S.fopen_ ? 1 : 0); res_u32(S.fsize); res_u32(S.fpos);
        for (const char *p = S.path; *p && S.res_n < 300; p++) res_put((uint8_t)*p);
        res_put(0);
        break;
    default: break;
    }
    return st;
}

/* 8.0.87 진단: 명령 하나가 100 ms 넘게 걸리면 그 명령과 걸린 시간을 기록 (스위치 LogCmdTrail 이 켜져 있을 때만 기록됨) */
uint32_t (*mmusb_ms)(void);
uint8_t mmusb_run(uint8_t c, const uint8_t *a)
{
    uint32_t t0 = mmusb_ms ? mmusb_ms() : 0;
    uint8_t r = mmusb_run_(c, a);
    if (mmusb_ms && mmusb_crumb && c != 0x26 && c != 0x27) {
        uint32_t d = mmusb_ms() - t0;
        if (d >= 100) { char b[40] = "usb SLOW cmd "; int n = 13; b[n++] = "0123456789ABCDEF"[c >> 4]; b[n++] = "0123456789ABCDEF"[c & 15]; b[n++] = ' '; b[n++] = 'm'; b[n++] = 's'; b[n++] = '=';
            char d2[12]; int k = 0; do { d2[k++] = (char)('0' + d % 10); d /= 10; } while (d && k < 11); while (k) b[n++] = d2[--k]; b[n] = 0; mmusb_crumb(b); }
    }
    return r;
}
