/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * hda.c - Intel High Definition Audio 출력 드라이버 (베어메탈, 폴링)
 *
 * 참고: Intel High Definition Audio Specification Rev 1.0a
 *   컨트롤러 레지스터: GCAP 0x00, GCTL 0x08, STATESTS 0x0E, CORB 0x40~, RIRB 0x50~,
 *                     즉시 명령 0x60/0x64/0x68, 스트림 디스크립터 0x80 + n*0x20
 *   코덱 명령: [31:28] 코덱 [27:20] 노드 [19:0] 동사+데이터
 */
#include "hda.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* 포트, MMIO                                                           */
/* ------------------------------------------------------------------ */
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }

static volatile uint8_t *bar;
static inline uint8_t  r8(uint32_t o)  { return *(volatile uint8_t *)(bar + o); }
static inline uint16_t r16(uint32_t o) { return *(volatile uint16_t *)(bar + o); }
static inline uint32_t r32(uint32_t o) { return *(volatile uint32_t *)(bar + o); }
static inline void w8(uint32_t o, uint8_t v)   { *(volatile uint8_t *)(bar + o) = v; }
static inline void w16(uint32_t o, uint16_t v) { *(volatile uint16_t *)(bar + o) = v; }
static inline void w32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(bar + o) = v; }

static uint32_t pci_read(int b, int d, int f, int off)
{
    outl(0xCF8, 0x80000000u | (uint32_t)(b << 16) | (uint32_t)(d << 11) | (uint32_t)(f << 8) | (uint32_t)(off & 0xFC));
    return inl(0xCFC);
}
static void pci_write(int b, int d, int f, int off, uint32_t v)
{
    outl(0xCF8, 0x80000000u | (uint32_t)(b << 16) | (uint32_t)(d << 11) | (uint32_t)(f << 8) | (uint32_t)(off & 0xFC));
    outl(0xCFC, v);
}

/* ------------------------------------------------------------------ */
/* DMA 메모리 (UC 페이지)                                               */
/* ------------------------------------------------------------------ */
static uintptr_t dma_base;
static int64_t dma_dev_off;                    /* 장치가 보는 주소 - 물리 주소 (VT-d 로 다르게 매핑된 PC) */
#define DEV_LO(a) ((uint32_t)((uint64_t)(a) + (uint64_t)dma_dev_off))
#define DEV_HI(a) ((uint32_t)(((uint64_t)(a) + (uint64_t)dma_dev_off) >> 32))
void hda_set_dma_dev(int64_t off) { dma_dev_off = off; }
#define CORB_ADDR   (dma_base + 0x0000)         /* 256 x 4 */
#define RIRB_ADDR   (dma_base + 0x0400)         /* 256 x 8 */
#define BDL_ADDR    (dma_base + 0x1000)
#define RING_ADDR   (dma_base + 0x2000)
#define RING_FRAMES 4096                        /* 85 ms, 스테레오 16비트 */
#define BDL_ENTRIES 4
#define TARGET_LAT  1600                        /* 목표 지연 33 ms */
#define LOW_LAT     400
#include "sndrs.h"
static SndRs rs;                                /* 부드러운 속도 맞춤 */
extern int snd_sync_mode;                       /* 5.3.17: 1 기본(5.3.14 루틴), 2 시험 루틴 (x86msx.ini SndSync) */
static uint32_t g_under, g_over; static int g_under_d;   /* 5.3.51: 소리 끊김 세기 (모자람 건너뜀 / 넘쳐 버림) */
static int prev_pos = -1, prev_q;                /* 5.3.54: 지난번 쓴 직후의 재생 위치와 그때 쌓여 있던 양 — 멈춤 판별용 */
static int tgt_lat = TARGET_LAT, ok_frames;     /* 목표 지연은 프레임이 늦어 소리가 끊길 때마다 늘리고(최대 2800 = 58 ms), 20초 조용하면 조금씩 되돌림 */

static volatile uint32_t *corb;
static volatile uint32_t *rirb;
static volatile int16_t *ring;

static int corb_n, rirb_n, rirb_rp, use_immediate;
/* 소리 진단(2.8.1): 쓰는 코덱·핀과 DAC까지 경로, 마지막에 넣은 소리 크기 */
typedef struct { uint8_t pin, len, nid[8], dev, pd, ctl, muted, cad; } UsedPath;   /* 3.7.2: dev 0 라인 1 스피커 2 헤드폰, pd = 꽂힘 감지 가능 (2 = 감지에 트리거 필요) */
static UsedPath upath[4];
static int pin_filter;                          /* 4.0.72: 0 전부(자동), 1 스피커·라인만, 2 이어폰(헤드폰)만 */
static uint16_t g_codecs;                       /* 4.0.69: 열린 컨트롤러의 코덱 주소 비트 */
static int nused, used_cad = -1, used_afg = -1, peak_out;
static uint32_t sd;                             /* 출력 스트림 디스크립터 오프셋 */
static int ok, wpos, sd_index;
static int out_pref;                        /* 0 자동, 1 아날로그만, 2 디지털(HDMI)만 */
static int stall_frames, last_raw = -1, recoveries;
static int pos_estimated, est_pos;          /* 장치가 재생 위치를 제대로 알려 주지 않으면 만든 만큼 재생됐다고 추정 */
static int32_t dc_x, dc_y;                      /* 직류 제거 필터 상태 */

int hda_ok(void) { return ok; }

/* ------------------------------------------------------------------ */
/* 코덱 명령                                                            */
/* ------------------------------------------------------------------ */
static int cmd_immediate(uint32_t verb, uint32_t *resp)
{
    for (int i = 0; i < 1000 && (r16(0x68) & 1); i++) delay_ms(1);
    w16(0x68, (uint16_t)(r16(0x68) | 2));          /* IRV 지우기 */
    w32(0x60, verb);
    w16(0x68, (uint16_t)(r16(0x68) | 1));          /* ICB */
    for (int i = 0; i < 100; i++) {
        if ((r16(0x68) & 3) == 2) { *resp = r32(0x64); return 0; }
        delay_ms(1);
    }
    return -1;
}

static int corb_seen_ok;                        /* 4.0.21: 이 컨트롤러에서 CORB/RIRB로 응답을 한 번이라도 받음 */
static int cmd_corb(uint32_t verb, uint32_t *resp)
{
    int wp = (r16(0x48) + 1) % corb_n;
    corb[wp] = verb;
    w16(0x48, (uint16_t)wp);
    for (int i = 0; i < 200; i++) {
        int hw = r16(0x58) % rirb_n;
        if (hw != rirb_rp) {
            rirb_rp = (rirb_rp + 1) % rirb_n;
            *resp = rirb[rirb_rp * 2];
            w8(0x5D, 0x05);                 /* RIRBSTS 지우기 (응답 횟수 도달 상태 해제) */
            corb_seen_ok = 1;
            return 0;
        }
        delay_ms(1);
    }
    return -1;
}

static int ctrl_intel;                          /* 4.0.19: 열린 컨트롤러가 Intel */
static int cmd_failed;                          /* 4.0.17: 마지막 cmd가 응답을 못 받음 */
void (*hda_trace)(const char *);                /* 진단 추적 (fe_trace_ext) — 명령마다 한 줄 */
static void htr(const char *what, uint32_t v)
{
    if (!hda_trace) return;
    char l[48]; int k = 0;
    for (const char *q = "hda "; *q; q++) l[k++] = *q;
    for (const char *q = what; *q && k < 30; q++) l[k++] = *q;
    l[k++] = ' ';
    for (int i = 28; i >= 0; i -= 4) l[k++] = "0123456789ABCDEF"[(v >> i) & 15];
    l[k] = 0;
    hda_trace(l);
}
static uint32_t cmd(int cad, int nid, uint32_t verb_payload)
{
    uint32_t v = ((uint32_t)cad << 28) | ((uint32_t)(nid & 0xFF) << 20) | (verb_payload & 0xFFFFF);
    htr(use_immediate ? "cmd imm" : "cmd corb", v);
    uint32_t r = 0;
    int bad = 1;
    if (use_immediate) {
        for (int t = 0; t < 3; t++) if (!cmd_immediate(v, &r)) { bad = 0; break; }     /* 응답이 없으면 다시 (AMD 등) */
    } else if (cmd_corb(v, &r)) {
        if (corb_seen_ok) { cmd_failed = 1; return 0; }   /* 4.0.21: CORB가 이미 동작한 컨트롤러에서 응답이 없으면 그 코덱(없는 주소 등)만 응답 없음. 직접 명령으로 바꾸지 않음 (바꾸면 응답 없는 주소를 기다리며 직접 명령이 영구히 멈춤: Arrow Lake 3번 PC) */
        use_immediate = 1;
        if (ctrl_intel) { w8(0x4C, 0); w8(0x5C, 0); for (int i = 0; i < 10 && ((r8(0x4C) | r8(0x5C)) & 2); i++) delay_ms(1); w16(0x68, 2); }   /* 4.0.19: Intel은 직접 명령으로 바꿀 때 CORB/RIRB 엔진을 끔 (켜 둔 채로는 직접 명령이 응답 없이 멈춤: Arrow Lake 3번 PC) */
        for (int t = 0; t < 3; t++) if (!cmd_immediate(v, &r)) { bad = 0; break; }
    } else bad = 0;
    cmd_failed = bad;
    return r;
}
static uint32_t param(int cad, int nid, int p) { return cmd(cad, nid, 0xF0000u | (uint32_t)p); }

/* ------------------------------------------------------------------ */
/* 코덱 위젯 그래프                                                     */
/* ------------------------------------------------------------------ */
#define MAX_NID 128
#define MAX_CONN 16
typedef struct {
    uint32_t caps, pincaps, config, outamp, inamp;
    uint8_t type, nconn;
    uint8_t conn[MAX_CONN];
} Widget;
static Widget w[MAX_NID];
static int w_first, w_last;

static void read_connections(int cad, int nid)
{
    uint32_t len = param(cad, nid, 0x0E);
    int n = len & 0x7F, longform = (len >> 7) & 1;
    int k = 0;
    for (int i = 0; i < n && k < MAX_CONN; ) {
        uint32_t r = cmd(cad, nid, 0xF0200u | (uint32_t)i);
        int per = longform ? 2 : 4;
        for (int j = 0; j < per && i < n && k < MAX_CONN; j++, i++) {
            uint32_t e = longform ? (r >> (16 * j)) & 0xFFFF : (r >> (8 * j)) & 0xFF;
            w[nid].conn[k++] = (uint8_t)(e & (longform ? 0x7FFF : 0x7F));
        }
    }
    w[nid].nconn = (uint8_t)k;
}

/* pin에서 DAC까지 경로 (노드, 선택한 연결 번호) */
typedef struct { uint8_t nid, idx; } Hop;
static Hop path[8];

static int find_dac(int nid, int depth)
{
    if (nid < w_first || nid > w_last) return -1;
    if (w[nid].type == 0) { path[depth].nid = (uint8_t)nid; path[depth].idx = 0; return depth + 1; }
    if (depth >= 6) return -1;
    for (int i = 0; i < w[nid].nconn; i++) {
        int c = w[nid].conn[i];
        if (c < w_first || c > w_last) continue;
        int t = w[c].type;
        if (t != 0 && t != 2 && t != 3) continue;        /* DAC, 믹서, 선택기만 */
        path[depth].nid = (uint8_t)nid;
        path[depth].idx = (uint8_t)i;
        int r = find_dac(c, depth + 1);
        if (r > 0) return r;
    }
    return -1;
}

static void set_amp(int cad, int nid, int output, int index, uint32_t ampcaps)
{
    uint32_t gain = ampcaps & 0x7F;                       /* 0 dB 위치 */
    uint32_t payload = (output ? 0x8000u : 0x4000u) | 0x3000u | ((uint32_t)index << 8) | gain;
    cmd(cad, nid, 0x30000u | payload);
}

static char info[6][64];
static int ninfo;
static char nc_buf[2][64];                  /* 5.3.6: 코덱이 안 보일 때 남기는 구조 시도·레지스터 값 (소리 진단 화면용) */
static char *cur_info;                      /* 지금 시험 중인 컨트롤러의 정보 줄 */

static int strstr_simple(const char *h, const char *n)
{
    for (; *h; h++) {
        int i = 0;
        while (n[i] && h[i] == n[i]) i++;
        if (!n[i]) return 1;
    }
    return 0;
}

static void info_add(const char *t)
{
    if (!cur_info) return;
    int k = 0;
    while (cur_info[k]) k++;
    while (*t && k < 63) cur_info[k++] = *t++;
    cur_info[k] = 0;
}
static void info_hex(uint32_t v, int digits)
{
    char t[9];
    for (int i = 0; i < digits; i++) t[i] = "0123456789ABCDEF"[(v >> (4 * (digits - 1 - i))) & 15];
    t[digits] = 0;
    info_add(t);
}

static char hda_dbg[700]; static int hda_dbgn;       /* 4.0.84: HDMI 소리 선택 과정 기록 (HDA_SEL.TXT에 기록됨) */
static void dbg_s(const char *t) { while (*t && hda_dbgn < (int)sizeof hda_dbg - 2) hda_dbg[hda_dbgn++] = *t++; hda_dbg[hda_dbgn] = 0; }
static void dbg_h(uint32_t v, int digits) { char t[9]; for (int i = 0; i < digits; i++) t[i] = "0123456789ABCDEF"[(v >> (4 * (digits - 1 - i))) & 15]; t[digits] = 0; dbg_s(t); }
const char *hda_dbg_text(void) { return hda_dbg; }
static int setup_codec(int cad, int analog_only, int *dacs_used)
{
    uint32_t sub = param(cad, 0, 0x04);
    int start = (sub >> 16) & 0xFF, count = sub & 0xFF;
    int afg = -1;
    for (int n = start; n < start + count; n++)
        if ((param(cad, n, 0x05) & 0xFF) == 1) { afg = n; break; }
    if (afg < 0) return 0;
    uint32_t vid = param(cad, 0, 0x00);
    if ((vid >> 16) == 0x8086 && analog_only == 1) return 0;   /* 4.0.101: 스피커 선택 때는 그래픽(HDMI) 코덱을 건드리지 않음 (건드리면 BIOS가 화면을 HDMI로 돌림) */
    info_add(" codec ");
    info_hex(vid >> 16, 4); info_add(":"); info_hex(vid & 0xFFFF, 4);

    cmd(cad, afg, 0x70500);                               /* D0 */
    delay_ms(10);
    if ((vid >> 16) == 0x1002) {                          /* 4.0.115: AMD GPU HDMI 코덱 (DisplaySelect 1번 v53 az_tone): D0 -> D3 -> D0 로 설정 리셋을 지우고 시작 */
        delay_ms(10);
        cmd(cad, afg, 0x70503); delay_ms(100);
        cmd(cad, afg, 0x70500); delay_ms(100);
        cmd(cad, afg, 0x70500); delay_ms(50);
    }
    if ((vid >> 16) == 0x8086) {                          /* 4.0.83: Intel 디스플레이 코덱 — 벤더 verb(노드 8)로 모든 핀·변환기를 켜야 HDMI 핀(5~7)이 보이고 쓰기가 먹음 (DisplaySelect v6에서 소리 성공, Linux INTEL_EN_ALL_PIN_CVTS) */
        delay_ms(20);
        uint32_t vp = cmd(cad, 8, 0xF8100);
        dbg_s("  c"); dbg_h((uint32_t)cad, 1); dbg_s(" vendor-verb="); dbg_h(vp, 8);
        if (!cmd_failed && vp != 0xFFFFFFFFu && !(vp & 1)) { cmd(cad, 8, 0x78100u | ((vp | 1u) & 0xFFu)); delay_ms(10); dbg_s(" ->"); dbg_h(cmd(cad, 8, 0xF8100), 8); }
        dbg_s("\r\n");
    }
    uint32_t afg_out = param(cad, afg, 0x12), afg_in = param(cad, afg, 0x0D);
    sub = param(cad, afg, 0x04);
    w_first = (sub >> 16) & 0xFF;
    w_last = w_first + (int)(sub & 0xFF) - 1;
    if (w_last >= MAX_NID) w_last = MAX_NID - 1;
    memset(w, 0, sizeof w);
    for (int n = w_first; n <= w_last; n++) {
        Widget *x = &w[n];
        x->caps = param(cad, n, 0x09);
        x->type = (uint8_t)((x->caps >> 20) & 0xF);
        x->outamp = (x->caps & (1u << 3)) ? param(cad, n, 0x12) : afg_out;   /* 3 = amp override */
        x->inamp = (x->caps & (1u << 3)) ? param(cad, n, 0x0D) : afg_in;
        if (x->type == 4) {
            x->pincaps = param(cad, n, 0x0C);
            x->config = cmd(cad, n, 0xF1C00);
        }
        if (x->caps & (1u << 8)) read_connections(cad, n);
    }

    if ((vid >> 16) == 0x8086) { dbg_s("  widgets "); dbg_h((uint32_t)w_first, 2); dbg_s(".."); dbg_h((uint32_t)w_last, 2); dbg_s("\r\n"); }
    int used = 0;
    for (int pass = analog_only == 2 ? 1 : 0; pass < (analog_only == 1 ? 1 : 2) && !used; pass++) {
        for (int n = w_first; n <= w_last; n++) {
            Widget *x = &w[n];
            if (x->type != 4 || !(x->pincaps & (1u << 4))) continue;   /* 출력 가능한 핀 */
            int conn = (x->config >> 30) & 3, dev = (x->config >> 20) & 0xF;
            if (pin_filter == 1 && dev == 2) continue;                  /* 스피커만: 헤드폰 핀 제외 */
            if (pin_filter == 2 && dev != 2) continue;                  /* 이어폰만 */
            if (analog_only == 2 && dev <= 2) continue;                 /* 4.0.67: 디지털만 — 라인·스피커·헤드폰 핀은 건너뜀 */
            if (pass == 0 && (conn == 1 || dev > 2)) continue;          /* 연결 없음, 출력 장치 아님 */
            if ((x->pincaps & (1u << 7)) && !(cmd(cad, n, 0xF0900) >> 31)) continue;   /* 4.0.83: HDMI 핀은 모니터가 연결된 것만 */
            int len = find_dac(n, 0);
            if ((vid >> 16) == 0x8086 && analog_only == 2) { dbg_s("  pin "); dbg_h((uint32_t)n, 2); dbg_s(" pincap="); dbg_h(x->pincaps, 8); dbg_s(" cfg="); dbg_h(x->config, 8); dbg_s(" sense="); dbg_h(cmd(cad, n, 0xF0900), 8); dbg_s(" conn="); dbg_h((uint32_t)w[n].nconn, 1); dbg_s(" dacpath="); dbg_h((uint32_t)(len > 0 ? len : 0), 1); dbg_s("\r\n"); }
            if (len <= 0) continue;
            for (int h = 0; h < len; h++) {
                int nid = path[h].nid, idx = path[h].idx;
                Widget *y = &w[nid];
                cmd(cad, nid, 0x70500);                                 /* D0 */
                if (h < len - 1) {
                    if (y->type == 2) set_amp(cad, nid, 0, idx, y->inamp);       /* 믹서 입력 */
                    else if (y->nconn > 1) cmd(cad, nid, 0x70100u | (uint32_t)idx); /* 연결 선택 */
                }
                if (y->caps & (1u << 2)) set_amp(cad, nid, 1, 0, y->outamp);     /* 출력 앰프 */
            }
            int hp = dev == 2 && (x->pincaps & (1u << 3));
            if (x->pincaps & (1u << 7)) { for (int k = 0; k < len; k++) cmd(cad, path[k].nid, 0x70500); delay_ms(40); cmd(cad, n, 0x70100u | (uint32_t)path[0].idx); }   /* 4.0.84: HDMI — 전원이 올라올 시간을 주고 연결 선택 (v6와 같은 순서) */
            cmd(cad, n, 0x70700u | 0x40u | (hp ? 0x80u : 0));                   /* 핀 출력 켜기 */
            if ((x->pincaps & (1u << 7)) && !(cmd(cad, n, 0xF0700) & 0x40)) {   /* 4.0.82: HDMI 핀이 출력 제어값을 받지 않으면(코덱이 ELD를 못 받은 상태) 쓸 수 없는 출력으로 보고 건너뜀 */
                info_add(" hdmi pin locked"); dbg_s("  pin "); dbg_h((uint32_t)n, 2); dbg_s(" ctl write ignored\r\n");
                cmd(cad, n, 0x70700);
                continue;
            }
            if (x->pincaps & (1u << 16)) cmd(cad, n, 0x70C02);                   /* EAPD */
            int dac = path[len - 1].nid;
            for (int k = 0; k < len; k++) cmd(cad, path[k].nid, 0x70500);       /* 경로 위 위젯 모두 D0 (일부 Realtek은 필요) */
            info_add(used ? "," : " pins ");
            info_hex((uint32_t)n, 2);
            if (nused < 4) {
                UsedPath *u = &upath[nused++];
                u->pin = (uint8_t)n; u->len = (uint8_t)len;
                u->cad = (uint8_t)cad; u->dev = (uint8_t)dev; u->ctl = (uint8_t)(0x40u | (hp ? 0x80u : 0)); u->muted = 2;          /* 4.0.72: 처음 한 번은 꽂힘 상태에 맞게 핀을 다시 써 줌 (이어폰을 꽂았다 빼면 소리가 나던 증상) */
                u->pd = (x->pincaps & (1u << 2)) && conn != 2 ? ((x->pincaps & (1u << 1)) ? 2 : 1) : 0;   /* 고정(내장) 핀은 감지 안 함 */
                for (int k = 0; k < len && k < 8; k++) u->nid[k] = path[k].nid;
                used_cad = cad; used_afg = afg;
            }
            cmd(cad, dac, 0x20000u | 0x0011u);                                  /* 48 kHz, 16비트, 2채널 */
            cmd(cad, dac, 0x70600u | 0x10u);                                    /* 스트림 1, 채널 0 */
            cmd(cad, dac, 0x72D01u);                                            /* 변환기 채널 수 2 (HDMI 변환기용) */
            cmd(cad, dac, 0x70D01u);                                            /* 디지털 변환기 켜기 (HDMI·S/PDIF, 아날로그는 무시) */
            if ((vid >> 16) == 0x1002 && (x->pincaps & (1u << 7))) {            /* 4.0.115: AMD(ATI) HDMI — v53 에서 소리 확인된 순서. 인포프레임은 AFMT(GPU)가 보내므로 Intel 방식(0x73x)은 쓰지 않음 */
                static const uint16_t av[11][2] = { {0x777,0x01},{0x785,0x11},{0x778,0x00},{0x786,0x00},{0x779,0x00},{0x787,0x00},{0x77A,0x00},{0x788,0x00},{0x771,0x00},{0x772,0x00},{0x789,0x01} };
                cmd(cad, n, 0xF7000); cmd(cad, n, 0xF7300);                     /* 스피커 배치·F73 읽기 (v45) */
                cmd(cad, n, 0x78000);                                           /* 싱크 정보 읽기 (리눅스 get_eld_ati 처럼) */
                for (int k = 0; k < 8; k++) cmd(cad, n, 0xF8100);
                for (int k = 8; k < 23; k++) { cmd(cad, n, 0x78000u | (uint32_t)k); cmd(cad, n, 0xF8100); }
                for (int k = 0; k < 15; k++) { cmd(cad, n, 0x77600u | (uint32_t)(k << 3)); cmd(cad, n, 0xF7600); }
                cmd(cad, n, 0xF7B00);
                cmd(cad, dac, 0x72400);
                for (int k = 0; k < 11; k++) cmd(cad, n, ((uint32_t)av[k][0] << 8) | av[k][1]);
                cmd(cad, dac, 0x70E00); cmd(cad, n, 0x70800);
                cmd(cad, dac, 0x770B4);
                cmd(cad, dac, 0x70610);                                         /* 스트림 1, 채널 0 (마지막) */
                dbg_s("  ati pin "); dbg_h((uint32_t)n, 2); dbg_s(" F73="); dbg_h(cmd(cad, n, 0xF7300), 8); dbg_s("\r\n");
            } else
            if ((vid >> 16) == 0x8086 && (x->pincaps & (1u << 7))) {            /* 4.0.83: 오디오 인포프레임 + 채널 슬롯 배치 (DisplaySelect v6와 같은 순서) */
                static const uint8_t af[17] = { 0x84, 0x01, 0x0A, 0x70, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                for (int k = 0; k < 8; k++) cmd(cad, n, 0x73400u | (uint32_t)((k << 4) | k));
                cmd(cad, n, 0x73000); cmd(cad, n, 0x73200);
                cmd(cad, n, 0x73000);
                for (int k = 0; k < 32; k++) cmd(cad, n, 0x73100);
                cmd(cad, n, 0x73000);
                for (int k = 0; k < 17; k++) cmd(cad, n, 0x73100u | af[k]);
                cmd(cad, n, 0x73000);
                cmd(cad, n, 0x732C0);
            }
            used++;
        }
    }
    if (!used) info_add(analog_only ? " no analog out" : " no output");
    *dacs_used += used;
    return used;
}

/* ------------------------------------------------------------------ */
/* 초기화                                                               */
/* ------------------------------------------------------------------ */
static void copy_status(char *dst, int len, const char *s)
{
    int i = 0;
    for (; s[i] && i < len - 1; i++) dst[i] = s[i];
    dst[i] = 0;
}

#define POSBUF_ADDR (dma_base + 0x1800)
static volatile uint32_t *posbuf;
static int use_posbuf;

/* 컨트롤러 리셋을 풀고 코덱이 나타날 때까지 기다림. 리셋 실패 -1, 아니면 0 (코덱 표는 *codecs) */
static int ctrl_reset_wait(uint16_t *codecs, int wait_ms)
{
    w32(0x08, r32(0x08) & ~1u);
    for (int i = 0; i < 100 && (r32(0x08) & 1); i++) delay_ms(1);
    delay_ms(1);
    w32(0x08, r32(0x08) | 1u);
    for (int i = 0; i < 100 && !(r32(0x08) & 1); i++) delay_ms(1);
    if (!(r32(0x08) & 1)) return -1;
    uint16_t c = 0;
    for (int i = 0; i < wait_ms && !(c = r16(0x0E)); i++) delay_ms(1);   /* 코덱이 나타날 때까지 */
    delay_ms(25);                                                         /* 나타난 뒤 안정 시간 */
    *codecs = r16(0x0E);
    return 0;
}

/* 5.3.6: 인텔 컨트롤러인데 코덱이 안 보일 때만 쓰는 구조 시도 (Firebat T9 Plus / N100 시험용).
 *  1) PCI 전원 상태가 D0이 아니면 D0으로 올림
 *  2) 확장 기능 목록의 멀티링크(ML, ID 2)에서 링크 전원(SPA)을 켬 - 새 인텔 칩은 링크를 따로 켜야 코덱이 보이는 경우가 있음
 * 무엇을 보고 무엇을 바꿨는지 info에 남김. 바꾼 것이 있으면 1 */
static int nocodec_rescue(int fb, int fd, int ff)
{
    int did = 0;
    char *keep = cur_info; cur_info = nc_buf[0]; nc_buf[0][0] = 0;
    uint32_t cp = pci_read(fb, fd, ff, 0x34) & 0xFF;
    for (int n = 0; cp >= 0x40 && n < 16; n++) {
        uint32_t h = pci_read(fb, fd, ff, cp);
        if ((h & 0xFF) == 0x01) {
            uint32_t pm = pci_read(fb, fd, ff, cp + 4);
            info_add(" pm"); info_hex(pm & 3, 1);
            if (pm & 3) { pci_write(fb, fd, ff, cp + 4, pm & ~3u); delay_ms(20); did = 1; }
            break;
        }
        cp = (h >> 8) & 0xFF;
    }
    uint32_t off = r32(0x14) & 0xFFFFFFFCu;
    info_add(" ll"); info_hex(off, 4);
    for (int n = 0; n < 16 && off >= 0x20 && off < 0x3F00; n++) {
        uint32_t hdr = r32(off);
        if (hdr == 0xFFFFFFFFu || hdr == 0) break;
        if (((hdr >> 16) & 0xFFF) == 2) {                          /* 멀티링크 */
            for (int idx = 0; idx < 4; idx++) {
                uint32_t ml = off + 0x40 + 0x40u * (uint32_t)idx;
                if (ml + 8 > 0x4000) break;
                uint32_t lcap = r32(ml);
                if (lcap == 0 || lcap == 0xFFFFFFFFu) continue;
                uint32_t lctl = r32(ml + 4);
                info_add(" l"); info_hex((uint32_t)idx, 1); info_hex(lctl >> 16, 4);
                if (!(lctl & 0x10000u)) {
                    w32(ml + 4, lctl | 0x10000u);
                    for (int i = 0; i < 20 && !(r32(ml + 4) & 0x800000u); i++) delay_ms(1);
                    info_add(">"); info_hex(r32(ml + 4) >> 16, 4);
                    did = 1;
                }
            }
        }
        uint32_t nx = hdr & 0xFFFF;
        if (nx == 0 || nx == off) break;
        off = nx;
    }
    cur_info = keep;
    return did;
}

/* 컨트롤러 하나 열기: 성공 0 (출력 스트림까지 동작) */
static int open_ctrl(int fb, int fd, int ff, int analog_only, char *status, int len)
{
    uint64_t bar_addr = pci_read(fb, fd, ff, 0x10) & 0xFFFFFFF0u;
    if ((pci_read(fb, fd, ff, 0x10) & 0x6) == 0x4) bar_addr |= (uint64_t)pci_read(fb, fd, ff, 0x14) << 32;
    if (sizeof(void *) < 8 && (bar_addr >> 32)) { copy_status(status, len, "snd: BAR>4GB"); info_add(" BAR>4GB"); return -1; }
    bar = (volatile uint8_t *)(uintptr_t)bar_addr;
    paging_set_uc((uintptr_t)bar_addr);
    uint32_t cmdreg = pci_read(fb, fd, ff, 0x04);
    pci_write(fb, fd, ff, 0x04, (cmdreg & 0xFFFF0000u) | (cmdreg & 0xFFFF) | 0x0006u | 0x0400u);  /* 메모리, 버스 마스터, 인터럽트 끔 */
    if ((pci_read(fb, fd, ff, 0) & 0xFFFF) == 0x8086) {
        uint32_t v = pci_read(fb, fd, ff, 0x44);                  /* TCSEL = TC0 */
        pci_write(fb, fd, ff, 0x44, v & ~0x07u);
        { uint32_t dv = pci_read(fb, fd, ff, 0x78); if (dv & 0x0800u) pci_write(fb, fd, ff, 0x78, dv & ~0x0800u); }   /* 4.0.98: DEVC no-snoop(비트 11) 끔 — BIOS가 켜 두면 HDA DMA가 캐시를 거치지 않고 옛 메모리를 읽음 (5번 Ivy Bridge HDMI 잡음·무음 원인, 리눅스 hda_intel과 같은 처리) */
    }
    use_immediate = 0;
    corb_seen_ok = 0;
    ctrl_intel = (pci_read(fb, fd, ff, 0) & 0xFFFF) == 0x8086;

    /* 컨트롤러 리셋 */
    uint16_t codecs = 0;
    if (ctrl_reset_wait(&codecs, 500) < 0) { copy_status(status, len, "snd: reset fail"); info_add(" reset fail"); return -1; }
    static int rescued;                                                    /* 5.3.7: 켜질 때 한 번만 (되풀이하면 시작이 십수 초 멈춤) */
    if (!codecs && ctrl_intel && !rescued && (rescued = 1) && nocodec_rescue(fb, fd, ff)) {   /* 5.3.6: 인텔에서 코덱이 안 보일 때만 (정상 PC는 이 길을 안 탐) */
        if (ctrl_reset_wait(&codecs, 700) < 0) { copy_status(status, len, "snd: reset fail"); info_add(" reset fail"); return -1; }
    }
    g_codecs = codecs;
    if (!codecs) {
        char *keep = cur_info; cur_info = nc_buf[1]; nc_buf[1][0] = 0;
        info_add("GCAP "); info_hex(r32(0x00) & 0xFFFF, 4); info_add(" GCTL "); info_hex(r32(0x08), 4); info_add(" STATESTS "); info_hex(r16(0x0E), 4);
        cur_info = keep;
        copy_status(status, len, "snd: no codec"); info_add(" no codec"); return -1;
    }
    w32(0x20, 0);                                                  /* 인터럽트 끔 */

    /* CORB / RIRB */
    w8(0x4C, 0); w8(0x5C, 0);
    delay_ms(1);
    uint8_t cs = r8(0x4E), rs = r8(0x5E);
    int csel = (cs & 0x40) ? 2 : (cs & 0x20) ? 1 : 0;
    int rsel = (rs & 0x40) ? 2 : (rs & 0x20) ? 1 : 0;
    corb_n = csel == 2 ? 256 : csel == 1 ? 16 : 2;
    rirb_n = rsel == 2 ? 256 : rsel == 1 ? 16 : 2;
    memset((void *)CORB_ADDR, 0, 0x1000);
    w32(0x40, DEV_LO(CORB_ADDR)); w32(0x44, DEV_HI(CORB_ADDR)); w8(0x4E, (uint8_t)((cs & 0xFC) | csel));
    w16(0x4A, 0x8000);
    for (int i = 0; i < 10 && !(r16(0x4A) & 0x8000); i++) delay_ms(1);
    w16(0x4A, 0);
    for (int i = 0; i < 10 && (r16(0x4A) & 0x8000); i++) delay_ms(1);
    w16(0x48, 0);
    w32(0x50, DEV_LO(RIRB_ADDR)); w32(0x54, DEV_HI(RIRB_ADDR)); w8(0x5E, (uint8_t)((rs & 0xFC) | rsel));
    w16(0x58, 0x8000);
    w16(0x5A, 0xFF);                    /* RINTCNT */
    rirb_rp = 0;
    w8(0x4C, 0x02);
    w8(0x5C, 0x02);
    delay_ms(1);

    /* 코덱 설정 */
    int dacs = 0;
    nused = 0; used_cad = -1; used_afg = -1;
    for (int cad = 0; cad < 15; cad++)
        if (codecs & (1u << cad)) setup_codec(cad, analog_only, &dacs);
    if (!dacs) { copy_status(status, len, "snd: no output"); return -1; }

    /* 출력 스트림 */
    uint16_t gcap = r16(0x00);
    int iss = (gcap >> 8) & 0xF, oss = (gcap >> 12) & 0xF;
    if (!oss) { copy_status(status, len, "snd: no stream"); info_add(" no stream"); return -1; }
    sd = 0x80u + (uint32_t)iss * 0x20u;
    w32(sd, r32(sd) & ~2u);
    delay_ms(1);
    w32(sd, (r32(sd) & 0x00FFFFFFu) | 1u);
    for (int i = 0; i < 100 && !(r32(sd) & 1); i++) delay_ms(1);
    w32(sd, r32(sd) & 0x00FFFFFEu);
    for (int i = 0; i < 100 && (r32(sd) & 1); i++) delay_ms(1);

    memset((void *)RING_ADDR, 0, RING_FRAMES * 4);
    volatile uint32_t *bdl = (volatile uint32_t *)BDL_ADDR;
    uint32_t chunk = RING_FRAMES * 4 / BDL_ENTRIES;
    for (int i = 0; i < BDL_ENTRIES; i++) {
        bdl[i * 4 + 0] = DEV_LO(RING_ADDR + (uintptr_t)i * chunk);
        bdl[i * 4 + 1] = DEV_HI(RING_ADDR + (uintptr_t)i * chunk);
        bdl[i * 4 + 2] = chunk;
        bdl[i * 4 + 3] = 0;
    }
    /* DMA 위치 버퍼 (AMD 등은 LPIB가 부정확할 수 있음) */
    posbuf = (volatile uint32_t *)POSBUF_ADDR;
    memset((void *)POSBUF_ADDR, 0, 256);
    w32(0x74, DEV_HI(POSBUF_ADDR));
    w32(0x70, DEV_LO(POSBUF_ADDR) | 1u);
    __asm__ volatile("wbinvd" : : : "memory");        /* 4.0.99: BDL·CORB·RIRB를 메모리로 내보냄 */
    w32(sd + 0x08, RING_FRAMES * 4);           /* CBL */
    w16(sd + 0x0C, BDL_ENTRIES - 1);           /* LVI */
    w16(sd + 0x12, 0x0011);                    /* 48 kHz, 16비트, 2채널 */
    w32(sd + 0x18, DEV_LO(BDL_ADDR));
    w32(sd + 0x1C, DEV_HI(BDL_ADDR));
    w32(sd, (1u << 20) | 2u);                  /* 스트림 태그 1, RUN */
    /* 스트림이 실제로 도는지 확인 (최대 0.2초): 어느 위치 값도 안 움직이면 시작 실패 → 다시 시도
     * (AMD 등에서 가끔 첫 시작에 DMA가 돌지 않음. 2.4.7까지는 이를 놓쳐 무음으로 부팅되기도 했음) */
    uint32_t l0 = r32(sd + 0x04), p0 = posbuf[(iss) * 2], l1 = l0, p1 = p0;
    for (int t = 0; t < 200 && l1 == l0 && p1 == p0; t++) {
        delay_ms(1);
        l1 = r32(sd + 0x04);
        p1 = posbuf[(iss) * 2];
    }
    if (l1 == l0 && p1 == p0) {
        /* 위치가 안 움직여도 RUN·FIFORDY(SDSTS 비트 5)가 켜져 있으면 DMA는 돌고 위치 보고만 안 되는 장치 (AMD 등)
         * → 추정 위치로 사용. FIFO도 준비되지 않았으면 진짜로 멈춘 것 → 다시 시도 */
        if ((r32(sd) & 2u) && (r8(sd + 3) & 0x20)) {
            pos_estimated = 1;
            est_pos = 0;
            info_add(" est");
        } else {
            w32(sd, r32(sd) & ~2u);                     /* 스트림 멈춤 */
            copy_status(status, len, "snd: stream stuck");
            info_add(use_immediate ? " stuck/imm" : " stuck/corb");   /* 코덱 명령이 CORB(DMA)로 됐나 — imm 이면 DMA 자체가 막힌 것 (VT-d 등) */
            return -1;
        }
    }
    use_posbuf = (l1 == l0) && (p1 != p0);
    sd_index = iss;
    info_add(use_immediate ? " imm" : " corb");
    if (use_posbuf) info_add(" posbuf");
    wpos = TARGET_LAT; prev_pos = -1;
    sndrs_reset(&rs, TARGET_LAT); tgt_lat = TARGET_LAT; ok_frames = 0;
    copy_status(status, len, use_immediate ? "snd: on (imm)" : "snd: on");
    return 0;
}

static void stop_ctrl(void)
{
    if (!bar) return;
    w8(0x4C, 0); w8(0x5C, 0);
    w32(0x08, r32(0x08) & ~1u);                 /* 컨트롤러를 리셋 상태로 */
}



int hda_init(char *status, int len, uintptr_t dma)
{
    dma_base = dma;
    corb = (volatile uint32_t *)CORB_ADDR;
    rirb = (volatile uint32_t *)RIRB_ADDR;
    ring = (volatile int16_t *)RING_ADDR;
    paging_set_uc(dma_base);
    /* 모든 HD Audio 장치 (PCI 04:03) */
    int list[6][3], n = 0;
    for (int b = 0; b < 256; b++)
        for (int d = 0; d < 32; d++)
            for (int f = 0; f < 8 && n < 6; f++) {
                uint32_t id = pci_read(b, d, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (f == 0) break; continue; }
                uint32_t cls = pci_read(b, d, f, 0x08) >> 16;
                /* 04:03 HD Audio. 최신 인텔 칩셋 내장 사운드는 04:01(멀티미디어 오디오)로 보고되기도 함 */
                if (cls == 0x0403 || cls == 0x0401) { list[n][0] = b; list[n][1] = d; list[n][2] = f; n++; }
                if (f == 0 && !(pci_read(b, d, f, 0x0C) & 0x00800000)) break;   /* 단일 기능 */
            }
    ninfo = 0;
    if (!n) { copy_status(status, len, "snd: no HDA"); return -1; }
    for (int i = 0; i < n; i++) {                   /* 정보 줄: 위치와 PCI ID */
        uint32_t id = pci_read(list[i][0], list[i][1], list[i][2], 0);
        cur_info = info[ninfo++];
        cur_info[0] = 0;
        info_add("HDA ");
        info_hex((uint32_t)list[i][0], 2); info_add(":"); info_hex((uint32_t)list[i][1], 2); info_add(".");
        info_hex((uint32_t)list[i][2], 1); info_add(" ");
        info_hex(id & 0xFFFF, 4); info_add(":"); info_hex(id >> 16, 4);
    }
    /* 1차: 스피커·이어폰·라인 출력(아날로그)이 있는 장치, 2차: 아무 출력 (HDMI 등) */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            if ((out_pref == 1 || out_pref == 3) && pass == 1) continue;       /* 아날로그만 */
            if (out_pref == 2 && pass == 0) continue;       /* 디지털만: 아날로그가 있는 장치는 건너뜀 */
            cur_info = info[i];
            if (pass == 1) info_add(" | any:");
            for (int tries = 0; tries < 4; tries++) {
                if (tries) info_add(" R");       /* 다시 시도 (64글자 안에 들게 짧게) */
                if (!open_ctrl(list[i][0], list[i][1], list[i][2], out_pref == 2 ? 2 : pass == 0, status, len)) {   /* 4.0.67: 디지털은 같은 컨트롤러 안의 HDMI 코덱을 씀 */
                    info_add(" <- used");
                    cur_info = 0;
                    ok = 1;
                    return 0;
                }
                stop_ctrl();
                if (strstr_simple(cur_info, "no analog") || strstr_simple(cur_info, "no output")) break;   /* 코덱 구성 문제는 다시 해도 같음 */
                delay_ms(50 * (tries + 1));             /* 다시 시도 전 조금 더 기다림 (컨트롤러·코덱 안정) */
            }
        }
    cur_info = 0;
    return -1;
}

int hda_info(char (*lines)[64], int max)
{
    int k = 0;
    for (int i = 0; i < ninfo && k < max; i++, k++) memcpy(lines[k], info[i], 64);
    if (pos_estimated && k < max) {                     /* 재생 위치를 추정으로 바꿨음 */
        const char *t = "HDA position: estimated (device reports no position)";
        int j = 0;
        while (t[j] && j < 63) { lines[k][j] = t[j]; j++; }
        lines[k++][j] = 0;
    }
    if (recoveries && k < max) {                        /* 자동 복구 횟수 */
        char *l = lines[k++];
        const char *t = "HDA auto re-init: ";
        int j = 0;
        while (*t) l[j++] = *t++;
        if (recoveries >= 10) l[j++] = (char)('0' + recoveries / 10);
        l[j++] = (char)('0' + recoveries % 10);
        l[j] = 0;
    }
    return k;
}

/* ------------------------------------------------------------------ */
/* 재생                                                                 */
/* ------------------------------------------------------------------ */
int hda_recoveries(void) { return recoveries; }

static int32_t dc_x2, dc_y2;                    /* 3.3.3: 오른쪽 통로 직류 제거 */
static int hp_cur = -1;                         /* 4.0.34: 헤드폰 꽂힘 (-1 = 아직 모름) */
int hda_headphone(void) { return hp_cur; }
/* 3.7.2: 헤드폰(이어폰)을 꽂으면 스피커·라인 출력을 끄고 헤드폰으로만, 빼면 다시 켬. 약 0.5초마다 핀 감지 (F09) */
int hda_pin_fix, hda_pin_last = -1;   /* 4.0.78: 디지털 핀 제어값 재설정 횟수, 마지막으로 읽은 값 */
static void hp_poll(void)
{
    static int tick, dead;
    if (dead || ++tick < 30 || used_cad < 0) return;
    tick = 0;
    int hp_in = 0;
    for (int i = 0; i < nused; i++) {
        UsedPath *u = &upath[i];
        if (u->dev != 2 || !u->pd) continue;
        if (u->pd == 2) cmd(u->cad, u->pin, 0x70900);              /* 감지 실행 */
        if (cmd_failed) { dead = 1; return; }
        if (cmd(u->cad, u->pin, 0xF0900) >> 31) hp_in = 1;
        if (cmd_failed) { dead = 1; return; }   /* 4.0.17: 코덱이 응답하지 않으면 헤드폰 감지를 끔 (명령마다 수백 ms씩 기다려 화면이 멈췄음) */
    }
    for (int i = 0; i < nused; i++) {                               /* 4.0.78: 디지털(HDMI) 핀은 출력 제어값이 0으로 돌아가 있으면 다시 씀 (HDA_DIAG: ctl=0) */
        UsedPath *u = &upath[i];
        if (u->dev <= 2) continue;
        uint32_t cur = cmd(u->cad, u->pin, 0xF0700) & 0xFF;
        if (cmd_failed) { dead = 1; return; }
        hda_pin_last = (int)cur;
        if (cur != u->ctl) { cmd(u->cad, u->pin, 0x70700u | u->ctl); hda_pin_fix++; }
    }
    hp_cur = hp_in;
    for (int i = 0; i < nused; i++) {
        UsedPath *u = &upath[i];
        if (u->dev > 1 || u->muted == hp_in) continue;              /* 라인 출력·스피커만 */
        u->muted = (uint8_t)hp_in;
        cmd(u->cad, u->pin, 0x70700u | (hp_in ? 0u : u->ctl));
        if (cmd_failed) { dead = 1; return; }
    }
}

static void cflush(const volatile void *p, int bytes)        /* 4.0.99: 쓴 샘플을 메모리로 내보냄 (캐시 → RAM, DMA가 읽는 곳) */
{
    uintptr_t a = (uintptr_t)p & ~(uintptr_t)63, e = (uintptr_t)p + (uintptr_t)bytes;
    for (; a < e; a += 64) __asm__ volatile("clflush (%0)" : : "r"(a) : "memory");
    __asm__ volatile("mfence" : : : "memory");
}
uint32_t fe_now_ms(void);
static uint32_t g_wr_n, g_ur_f[8], g_ur_dd[8], g_ur_ms[8];   /* 8.0.21: 모자람이 일어난 시각(ms) 도 — 파일 쓰기 기록과 맞춰 보려고 */   /* 8.0.19: 소리 쓰기 호출 번호 / 모자람이 일어난 호출 번호와 그때 남은 양 (끊김이 언제 일어나는지 보려는 진단) */
void hda_write2(const int16_t *mono, const int16_t *right, int n, int volume, int mute)
{
    g_wr_n++;
    if (!ok) return;
    hp_poll();
    uint32_t raw = use_posbuf ? posbuf[sd_index * 2] : r32(sd + 0x04);
    int pos;
    if (pos_estimated) {                                /* 추정 위치: 이번에 만든 만큼 진행 */
        pos = est_pos;
        est_pos = (est_pos + n) % RING_FRAMES;
    } else {
        if ((int)raw == last_raw) {                     /* 재생 위치가 안 움직임 */
            if (++stall_frames > 120) {                 /* 약 2초 멈춤 */
                stall_frames = 0;
                if ((r32(sd) & 2u) && (r8(sd + 3) & 0x20)) {                    /* 스트림은 돌고(RUN·FIFORDY) 위치 보고만 멈춘 장치 (AMD 15E3 등): 다시 초기화(몇 초 끊김)하지 않고 바로 추정 위치로 */
                    pos_estimated = 1;
                    est_pos = (wpos - TARGET_LAT + RING_FRAMES) % RING_FRAMES;
                    return;
                }
                if (recoveries < 3) {                   /* 먼저 다시 초기화 (스트림이 실제로 멈춘 경우가 대부분) */
                    char st[24];
                    last_raw = -1;
                    recoveries++;
                    hda_select(out_pref, st, sizeof st);
                    return;
                }
                pos_estimated = 1;                      /* 3번 해도 위치가 안 움직이면 마지막 수단: 추정 위치 */
                est_pos = (wpos - TARGET_LAT + RING_FRAMES) % RING_FRAMES;
            }
        } else { stall_frames = 0; last_raw = (int)raw; }
        pos = (int)(raw / 4) % RING_FRAMES;
    }
    int d = (wpos - pos + RING_FRAMES) % RING_FRAMES;
    /* 5.3.54: 쌓아 둔 소리(약 45 ms)보다 오래 멈췄다 돌아오면 재생 위치가 쓴 자리를 넘어갔는데, 나머지 계산(d)으로는 링이 꽉 찬 것처럼 보여
     * "모자람" 대신 "넘침"으로 잘못 읽혔음 (지연이 80 ms 로 커지고 이후 프레임마다 소리 뒤쪽이 버려짐). 지난번 이후 재생 위치가 쌓인 양보다 많이 갔으면 모자람으로 */
    if (!pos_estimated && prev_pos >= 0 && ((pos - prev_pos + RING_FRAMES) % RING_FRAMES) > prev_q) d = 0;
    if (d < LOW_LAT) {                                            /* 모자람(프레임이 많이 늦음): 앞으로 건너뜀. 5.2.148: 건너뛴 자리에 낡은 소리가 남아 되풀이되던 것(음이탈·울림)을 무음으로 */
        g_ur_f[g_under & 7] = g_wr_n; g_ur_dd[g_under & 7] = (uint32_t)d; g_ur_ms[g_under & 7] = fe_now_ms();
        g_under++; g_under_d = d;
        if (tgt_lat < 2800) { tgt_lat += 400; if (tgt_lat > 2800) tgt_lat = 2800; }   /* 8.0.22: 2800 상한을 넘겨 3040 이 되면 지연+한 프레임(800)이 넘침 기준(4096-256)에 닿아 매 프레임 소리 뒤쪽을 버렸음 (SNDGLITCH: lat=3040 OVER 가 프레임마다) */
        ok_frames = 0;
        int np = (pos + tgt_lat) % RING_FRAMES;
        for (int k = 0; k < tgt_lat; k++) { int q = (pos + k) % RING_FRAMES; ring[q * 2] = 0; ring[q * 2 + 1] = 0; }
        cflush(&ring[0], RING_FRAMES * 4);
        wpos = np;
        d = tgt_lat;
        rs.d_avg = tgt_lat;
    } else if (tgt_lat > TARGET_LAT && ++ok_frames >= 1200) { ok_frames = 0; tgt_lat -= 160; if (tgt_lat < TARGET_LAT) tgt_lat = TARGET_LAT; }
    if (snd_sync_mode == 2 && d > RING_FRAMES - 400) {            /* 5.3.15 (SndSync=2 만): 넘침(프레임이 몰려 옴): 이번 프레임 소리는 버리고 한 번에 맞춤 (천천히 속도를 올려 음이 들뜨는 것보다 짧은 한 번이 덜 거슬림) */
        rs.d_avg = tgt_lat;
        return;
    }

    int32_t gain = mute ? 0 : volume;                             /* 0~100, 출력 = 입력 x gain / 50 */
    { static int dig = -1, dn = -1; if (dn != nused) { dn = nused; dig = 0; for (int u = 0; u < nused; u++) if (upath[u].dev == 4 || upath[u].dev == 5) dig = 1; }
      if (dig) gain *= 2; }                                       /* HDMI·DP 디지털 출력은 앰프가 없어 소리가 작음 -> 2배(+6 dB) */
    int fw0 = wpos, fcnt = 0;
    /* 화면 주기에 맞춰 돌 때(60.00 대 59.92 Hz) 생기는 아주 작은 속도 차이는 선형 보간으로 부드럽게 흡수
     * (전에는 64샘플마다 하나 빼거나 더해 음높이가 1.5% 튀고 5 Hz 정도로 떨렸음). 5.3.15: 목표 +-8 ms 안에서는 속도 고정, 밖에서는 +-0.1% 까지만 (sndrs.h) */
    static int16_t rl[2400], rr[2400];
    if (n > 2000) n = 2000;
    int32_t step = snd_sync_mode == 2 ? sndrs_step2(&rs, d, tgt_lat) : sndrs_step(&rs, d, tgt_lat);
    int on = sndrs_run(&rs, mono, right, n, step, rl, rr, 2400);
    for (int i = 0; i < on; i++) {
        d = (wpos - pos + RING_FRAMES) % RING_FRAMES;
        if (d > RING_FRAMES - 256) { g_over++; break; }           /* 넘침: 나머지 버림 */
        /* 직류 제거: y = x - x' + 0.996 y' */
        int32_t x = rl[i];
        dc_y = x - dc_x + (dc_y * 255) / 256;
        dc_x = x;
        int32_t s = (dc_y * gain) / 50, s2 = s;
        if (right) {                                              /* 3.3.3 스테레오: mono = 왼쪽 */
            int32_t x2 = rr[i];
            dc_y2 = x2 - dc_x2 + (dc_y2 * 255) / 256;
            dc_x2 = x2;
            s2 = (dc_y2 * gain) / 50;
        }
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (s2 > 32767) s2 = 32767;
        if (s2 < -32768) s2 = -32768;
        if ((s < 0 ? -s : s) > peak_out) peak_out = s < 0 ? -s : s;
        ring[wpos * 2] = (int16_t)s;
        ring[wpos * 2 + 1] = (int16_t)s2;
        wpos = (wpos + 1) % RING_FRAMES;
        fcnt++;
    }
    prev_pos = pos; prev_q = (wpos - pos + RING_FRAMES) % RING_FRAMES;
    if (fcnt) {
        if (fw0 + fcnt <= RING_FRAMES) cflush(&ring[fw0 * 2], fcnt * 4);
        else { cflush(&ring[fw0 * 2], (RING_FRAMES - fw0) * 4); cflush(&ring[0], (fw0 + fcnt - RING_FRAMES) * 4); }
    }
}
/* 3.8.1: 재부팅·끄기 직전 — 재생 버퍼를 비우고 스트림을 멈춰, 다시 켜지는 동안 마지막 소리가 되풀이되지 않게 */
void hda_silence(void)
{
    if (!ok || !ring) return;
    for (int i = 0; i < RING_FRAMES * 2; i++) ring[i] = 0;
    w32(sd, r32(sd) & ~2u);
}
void hda_write(const int16_t *mono, int n, int volume, int mute) { hda_write2(mono, 0, n, volume, mute); }

/* 출력 장치를 다시 고른다 (pref 0 자동, 1 아날로그, 2 디지털). 성공 0 */

/* 4.0.102: 그래픽(HDMI) 코덱의 모니터 감지 — HDMI를 뽑았는지 알아내는 데만 씀 (소리 경로는 건드리지 않음). -1 모름, 0 없음, 1 있음 */
int hda_hdmi_present(void)
{
    static int cad_g = -2, pins[6], np;
    if (!ok || !g_codecs) return -1;
    if (cad_g == -2) {
        cad_g = -1; np = 0;
        for (int cad = 0; cad < 15 && cad_g < 0; cad++) {
            if (!(g_codecs & (1u << cad))) continue;
            if ((param(cad, 0, 0x00) >> 16) != 0x8086) continue;
            uint32_t sub = param(cad, 0, 0x04);
            int start = (sub >> 16) & 0xFF, count = sub & 0xFF, afg = -1;
            for (int n = start; n < start + count; n++) if ((param(cad, n, 0x05) & 0xFF) == 1) { afg = n; break; }
            if (afg < 0) continue;
            sub = param(cad, afg, 0x04);
            int f = (sub >> 16) & 0xFF, c = sub & 0xFF;
            for (int n = f; n < f + c && np < 6; n++) {
                if (((param(cad, n, 0x09) >> 20) & 0xF) != 4) continue;
                if (param(cad, n, 0x0C) & (1u << 7)) pins[np++] = n;
            }
            if (np) cad_g = cad;
        }
    }
    if (cad_g < 0) return -1;
    for (int i = 0; i < np; i++) if (cmd(cad_g, pins[i], 0xF0900) >> 31) return 1;
    return 0;
}

int hda_select(int pref, char *status, int len)
{
    pin_filter = 0;                                  /* 4.0.101: 스피커(이어폰) = 스피커와 이어폰 핀 모두 */
    hda_dbgn = 0; hda_dbg[0] = 0;
    if (ok && g_codecs && ring) {                    /* 4.0.69: 컨트롤러는 그대로 두고 출력 핀만 옮김 (다시 초기화하면 소리가 안 나고 오래 걸렸음) */
        for (int i = 0; i < nused; i++) {                           /* 지금 쓰는 핀·변환기 끄기 */
            UsedPath *u = &upath[i];
            cmd(u->cad, u->pin, 0x70700);
            cmd(u->cad, u->nid[u->len - 1], 0x70600);
        }
        char *keep = cur_info; cur_info = 0;
        int dacs = 0, mode = pref == 2 ? 2 : 1;      /* pref 3(이어폰)도 아날로그 */
        nused = 0; used_cad = -1; used_afg = -1;
        for (int cad = 0; cad < 15; cad++) if (g_codecs & (1u << cad)) setup_codec(cad, mode, &dacs);
        if (!dacs && pref == 0) for (int cad = 0; cad < 15; cad++) if (g_codecs & (1u << cad)) setup_codec(cad, 0, &dacs);
        cur_info = keep;
        if (dacs) {
            out_pref = pref;
            last_raw = -1; stall_frames = 0; hp_cur = -1;
            copy_status(status, len, pref == 2 ? "snd: on (HDMI)" : "snd: on");
            return 0;
        }
        if (pref == 2) {                                                /* 4.0.82: HDMI 소리를 켤 수 없음 — 컨트롤러는 그대로 두고 자동(내장 소리)으로 되돌리고 실패를 알림 */
            pin_filter = 0; dacs = 0; nused = 0; used_cad = -1; used_afg = -1;
            char *k2 = cur_info; cur_info = 0;
            for (int cad = 0; cad < 15; cad++) if (g_codecs & (1u << cad)) setup_codec(cad, 1, &dacs);
            if (!dacs) for (int cad = 0; cad < 15; cad++) if (g_codecs & (1u << cad)) setup_codec(cad, 0, &dacs);
            cur_info = k2;
            out_pref = 0; last_raw = -1; stall_frames = 0; hp_cur = -1;
            copy_status(status, len, "snd: HDMI unavailable");
        }
        /* 못 찾으면 아래의 전체 초기화로 */
    }
    if (ok) stop_ctrl();
    ok = 0;
    out_pref = pref;
    pos_estimated = 0; stall_frames = 0; last_raw = -1;
    int r = hda_init(status, len, dma_base);
    return r;
}

/* ------------------------------------------------------------------ */
/* 소리 진단 (2.8.1): 스트림 상태와 출력 경로 위젯을 코덱에서 직접 읽는다 */
/* ------------------------------------------------------------------ */
static char *dl; static int dk;
static void d_str(const char *t) { while (*t && dk < 63) dl[dk++] = *t++; dl[dk] = 0; }
static void d_hex(uint32_t v, int digits) { char t[9]; for (int i = 0; i < digits; i++) t[i] = "0123456789ABCDEF"[(v >> (4 * (digits - 1 - i))) & 15]; t[digits] = 0; d_str(t); }
static void d_dec(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); while (n) { char c[2] = { t[--n], 0 }; d_str(c); } }
static void d_line(char (*lines)[64], int *k, int max) { if (*k < max) { dl = lines[(*k)++]; dk = 0; dl[0] = 0; } else { static char sink[64]; dl = sink; dk = 0; } }

/* 4.0.17: 진단 화면용 명령. 코덱이 한 번이라도 응답하지 않으면 다시는 보내지 않는다 (응답 없는 코덱에 명령 20여 개를 보내면 화면이 수십 초 멈춤) */
static int diag_dead;
static uint32_t dcmd(int cad, int nid, uint32_t verb_payload)
{
    if (diag_dead) return 0;
    uint32_t r = cmd(cad, nid, verb_payload);
    if (cmd_failed) diag_dead = 1;
    return r;
}
static int gpio_step = -1;                      /* 소리 진단 G 키 — GPIO 출력 값을 돌려 가며 본체 스피커 앰프가 켜지는지 시험 */
int hda_gpio_next(void)
{
    if (!ok || used_cad < 0 || used_afg < 0 || cmd_failed) return -1;
    uint32_t cnt = param(used_cad, used_afg, 0x11) & 0xFF;
    int bits = cnt > 4 ? 4 : (int)cnt;
    if (!bits) return -1;
    gpio_step = (gpio_step + 1) % (1 << bits);
    uint32_t mask = (1u << bits) - 1u;
    cmd(used_cad, used_afg, 0x71600u | mask);
    cmd(used_cad, used_afg, 0x71700u | mask);
    cmd(used_cad, used_afg, 0x71500u | (uint32_t)gpio_step);
    return gpio_step;
}

int hda_stat(uint32_t *under, uint32_t *over, int *last_d, int *tgt) { *under = g_under; *over = g_over; *last_d = g_under_d; *tgt = tgt_lat; return ok; }   /* 5.3.51 */

int hda_sound_diag(char (*lines)[64], int max)
{
    int k = 0;
    d_line(lines, &k, max);
    if (!ok || !bar) {
        d_str("HDA: not running");
        for (int i = 0; i < 2; i++) if (nc_buf[i][0]) { d_line(lines, &k, max); d_str(i ? "" : "rescue: "); d_str(nc_buf[i]); }
        return k;
    }
    uint32_t ctl = r32(sd), lpib = r32(sd + 0x04);
    d_str("stream RUN "); d_dec((ctl >> 1) & 1);
    d_str("  FIFORDY "); d_dec((r8(sd + 3) >> 5) & 1);
    d_str("  LPIB "); d_hex(lpib, 5);
    d_str(use_posbuf ? "  posbuf" : ""); d_str("  est "); d_dec((uint32_t)pos_estimated);
    d_line(lines, &k, max);
    d_str("write pos "); d_dec((uint32_t)wpos); d_str("  peak "); d_dec((uint32_t)peak_out);
    d_str("  calls "); d_dec(g_wr_n);
    d_str("  under "); d_dec(g_under); d_str(" over "); d_dec(g_over);
    d_line(lines, &k, max);
    if (g_under) { uint32_t q0 = g_under > 8 ? g_under - 8 : 0; for (int h = 0; h < 2; h++) { uint32_t a0 = q0 + (uint32_t)h * 4, a1 = a0 + 4 < g_under ? a0 + 4 : g_under; if (a0 >= a1) break; d_str("ur(sec/left)"); for (uint32_t q = a0; q < a1; q++) { d_str(" "); d_dec(g_ur_ms[q & 7] / 1000); d_str("."); d_dec((g_ur_ms[q & 7] / 100) % 10); d_str("/"); d_dec(g_ur_dd[q & 7]); } d_line(lines, &k, max); } }   /* 8.0.22: 64자에 잘려 4개씩 두 줄 */
    d_str("re-init "); d_dec((uint32_t)recoveries); d_str("  latency "); d_dec((uint32_t)tgt_lat);
    { int32_t sp = rs.last - 65536, bs = rs.base_acc >> 12;                     /* 5.3.15: 지금 속도 보정 (만분율) — 늘어질 때 이 값이 움직이면 속도 맞춤 탓 */
      d_str("  sync "); d_dec((uint32_t)snd_sync_mode); d_str(" speed "); if (sp < 0) { d_str("-"); sp = -sp; } d_dec((uint32_t)(sp * 10000 / 65536)); d_str("/10000");
      d_str(" base "); if (bs < 0) { d_str("-"); bs = -bs; } d_dec((uint32_t)(bs * 10000 / 65536)); d_str(" lat "); d_dec((uint32_t)rs.d_avg); }
    peak_out = 0;
    if (use_immediate) {                                /* 4.0.19: 직접 명령으로 바꾼 장치만 컨트롤러 레지스터를 보임 (원인 분석용, 정상 장치의 화면은 그대로) */
    d_line(lines, &k, max);
    d_str("GCTL "); d_hex(r32(0x08), 4); d_str(" STATESTS "); d_hex(r16(0x0E), 4); d_str(" ICS "); d_hex(r16(0x68), 4);
    d_str(" SDSTS "); d_hex(r8(sd + 3), 2);
    d_line(lines, &k, max);
    d_str("CORB wp "); d_hex(r16(0x48), 4); d_str(" rp "); d_hex(r16(0x4A), 4); d_str(" ctl "); d_hex(r8(0x4C), 2);
    d_str("  RIRB wp "); d_hex(r16(0x58), 4); d_str(" ctl "); d_hex(r8(0x5C), 2);
    }
    if (used_cad < 0) return k;
    int cad = used_cad;
    if (diag_dead) { d_line(lines, &k, max); d_str("codec: no response to commands (skipped)"); return k; }
    d_line(lines, &k, max);
    d_str("codec "); d_dec((uint32_t)cad); d_str(" AFG "); d_hex((uint32_t)used_afg, 2);
    d_str(" pwr "); d_hex(dcmd(cad, used_afg, 0xF0500) & 0xFF, 2);
    d_str("  GPIO en "); d_hex(dcmd(cad, used_afg, 0xF1600) & 0xFF, 2);
    d_str(" dir "); d_hex(dcmd(cad, used_afg, 0xF1700) & 0xFF, 2);
    d_str(" data "); d_hex(dcmd(cad, used_afg, 0xF1500) & 0xFF, 2);
    if (gpio_step >= 0) { d_str("  G test "); d_dec((uint32_t)gpio_step); }
    { int dig = 0; for (int u = 0; u < nused; u++) if (upath[u].dev == 4 || upath[u].dev == 5) dig = 1;
      if (dig) {                                        /* 4.0.21: 디지털 코덱의 모든 핀 = 번호:모니터신호/ELD (모니터가 꽂힌 핀 찾기) */
        d_line(lines, &k, max);
        d_str("all pins:");
        int cnt = 0;
        for (int n = w_first; n <= w_last && n < MAX_NID; n++) {
            if (w[n].type != 4) continue;
            uint32_t sn = dcmd(cad, n, 0xF0900);
            if (cnt && cnt % 6 == 0) d_line(lines, &k, max), d_str("  ");
            d_str(" "); d_hex((uint32_t)n, 2); d_str(":"); d_dec(sn >> 31); d_str("/"); d_dec((sn >> 30) & 1);
            cnt++;
        }
      } }
    for (int u = 0; u < nused; u++) {
        const UsedPath *p = &upath[u];
        int pin = p->pin;
        if (p->dev == 4 || p->dev == 5) {               /* 4.0.22: 디지털(HDMI·DP) 핀은 한 줄로 (화면에 다 들어오게) */
            int cv = p->nid[p->len - 1];
            uint32_t sn = dcmd(cad, pin, 0xF0900);
            d_line(lines, &k, max);
            d_str("pin "); d_hex((uint32_t)pin, 2);
            d_str(" sense "); d_dec(sn >> 31); d_str(" eldv "); d_dec((sn >> 30) & 1);
            d_str(" pctl "); d_hex(dcmd(cad, pin, 0xF0700) & 0xFF, 2);
            d_str(" cvt "); d_hex((uint32_t)cv, 2);
            d_str(" ctrl "); d_hex(dcmd(cad, cv, 0xF0D00) & 0xFF, 2);
            d_str(" ch "); d_hex(dcmd(cad, cv, 0xF2D00) & 0xFF, 2);
            d_str(" strm "); d_hex(dcmd(cad, cv, 0xF0600) & 0xFF, 2);
            continue;
        }
        d_line(lines, &k, max);
        d_str("pin "); d_hex((uint32_t)pin, 2);
        d_str(" ctl "); d_hex(dcmd(cad, pin, 0xF0700) & 0xFF, 2);
        d_str(" eapd "); d_hex(dcmd(cad, pin, 0xF0C00) & 0xFF, 2);
        d_str(" pwr "); d_hex(dcmd(cad, pin, 0xF0500) & 0xFF, 2);
        d_str(" sense "); d_dec(dcmd(cad, pin, 0xF0900) >> 31);
        d_str(" amp "); d_hex(dcmd(cad, pin, 0xBA000) & 0xFF, 2); d_str("/"); d_hex(dcmd(cad, pin, 0xB8000) & 0xFF, 2);
        d_line(lines, &k, max);
        d_str("  path");
        for (int h = 1; h < p->len; h++) {
            int nid = p->nid[h];
            d_str(" "); d_hex((uint32_t)nid, 2); d_str(":");
            d_str("p"); d_hex(dcmd(cad, nid, 0xF0500) & 0x0F, 1);
            if (h == p->len - 1) { d_str(" s"); d_hex(dcmd(cad, nid, 0xF0600) & 0xFF, 2); d_str(" f"); d_hex(dcmd(cad, nid, 0xA0000) & 0xFFFF, 4); d_str(" o"); d_hex(dcmd(cad, nid, 0xBA000) & 0xFF, 2); }
            else { d_str(" i"); d_hex(dcmd(cad, nid, 0xB2000) & 0xFF, 2); d_str(" o"); d_hex(dcmd(cad, nid, 0xBA000) & 0xFF, 2); }
        }
    }
    { int dig2 = 0; for (int u = 0; u < nused; u++) if (upath[u].dev == 4 || upath[u].dev == 5) dig2 = 1;
      if (!dig2) {                                      /* 아날로그 코덱의 모든 핀 = 번호:기본 설정 (BIOS가 정한 연결 종류). 쓰지 않은 스피커 핀이 있는지 보는 용도 */
        d_line(lines, &k, max);
        d_str("GPIO n "); d_dec(param(cad, used_afg, 0x11) & 0xFF); d_str("  cfg:");
        int cnt = 0;
        for (int n = w_first; n <= w_last && n < MAX_NID; n++) {
            if (w[n].type != 4) continue;
            if (cnt && cnt % 3 == 0) d_line(lines, &k, max), d_str("  ");
            d_str(" "); d_hex((uint32_t)n, 2); d_str("="); d_hex(dcmd(cad, n, 0xF1C00), 8);
            cnt++;
        }
      } }
    d_line(lines, &k, max);
    d_str("amp: bit7 = mute. pwr 0 = D0(on). eapd bit1 = amp on");
    return k;
}
