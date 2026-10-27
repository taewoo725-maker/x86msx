/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * uefi/main.c - MSX1 에뮬레이터 UEFI 플랫폼 (x86_64, \EFI\BOOT\BOOTX64.EFI)
 *
 * 펌웨어 서비스(부트 서비스)를 끝내지 않고 계속 쓴다:
 *   - 파일: Simple File System으로 모든 FAT 드라이브를 폴더 단위로 탐색, 고른 파일만 읽음
 *   - USB: 펌웨어 USB 스택 위에서 키보드 HID 보고를 직접 받음 (usbkbd.c)
 *   - 노트북 내장 키보드: 펌웨어 PS/2 드라이버를 떼어내고 8042를 직접 폴링
 *   - 화면: GOP 프레임 버퍼. 펌웨어 페이지 테이블에서 프레임 버퍼는 WC, 사운드 DMA는 UC로
 * 화면·메뉴·키 처리는 공통 프런트엔드(kernel/fe.c).
 */
#include "efi.h"
#include "../kernel/pcdb.h"
#include "../src/msx.h"
#include "../kernel/fe.h"
#include "../kernel/hda.h"
#include "../kernel/smbios.h"
#include "../kernel/amddet.h"
#include "usbkbd.h"
#include "usbpad.h"
#include "usbmidi.h"
#include "rtwdet.h"
#include "amdedid.h"
#include "amddisp.h"
#include "intelhdmi.h"
#include "ivbhdmi.h"
#include "amdhdmi.h"
#include "hdadiag.h"
#include "../kernel/version.h"
#include "vfctdet.h"
#include "usbmouse.h"
#include "../kernel/hidmouse.h"
#include "../kernel/machine.h"
#include "netuefi.h"
#include "../kernel/net/netstack.h"   /* 무선(lwIP) */
#include "../kernel/net/nicdb.h"      /* 랜카드 표 */
#include "../kernel/usbgrow.h"
#include "../kernel/fat.h"
#include <string.h>
#include "usbgen.h"

static uint8_t *g_back;                    /* 화면 뒤 버퍼 (GOP Blt 갱신) */
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_gop;
static uint32_t g_w;
static uint32_t g_h;
static uint8_t *g_fb;                      /* 3.5.5: 화면 메모리 (직접 복사할 때) */
static uint32_t g_fb_pitch;
static int g_direct;                       /* 1 = 뒤 버퍼 → 화면 메모리 직접 복사 (펌웨어 Blt가 느린 기계) */
/* 바뀐 사각형을 화면으로: 직접 복사(WC 메모리에 rep movs) 또는 GOP Blt */
static void present_rect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    if (!g_back || y1 <= y0 || x1 <= x0) return;
    if (g_direct) {
        UINTN bytes = (UINTN)(x1 - x0) * 4;
        for (uint32_t y = y0; y < y1; y++) {
            void *d = g_fb + (UINTN)y * g_fb_pitch + (UINTN)x0 * 4;
            const void *s = g_back + ((UINTN)y * g_w + x0) * 4;
            UINTN n = bytes / 8;
            __asm__ volatile("rep movsq" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
            if (bytes & 4) *(uint32_t *)d = *(const uint32_t *)s;
        }
        __asm__ volatile("sfence" ::: "memory");
    } else g_gop->Blt(g_gop, g_back, 2, x0, y0, x0, y0, x1 - x0, y1 - y0, (UINTN)g_w * 4);
}
static void present_rows(uint32_t y0, uint32_t y1) { present_rect(0, y0, g_w, y1); }
static void plat_flush(void) { present_rows(0, g_h); }
static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;
static EFI_HANDLE boot_dev;

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static inline uint64_t rdmsr(uint32_t msr) { uint32_t lo, hi; __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr)); return ((uint64_t)hi << 32) | lo; }
static inline void wrmsr(uint32_t msr, uint64_t v) { __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32))); }

/* ------------------------------------------------------------------ */
/* 텍스트 출력 (그래픽 전 오류 표시)                                      */
/* ------------------------------------------------------------------ */
static void print(const char *s)
{
    CHAR16 buf[256];
    int k = 0;
    for (; *s && k < 254; s++) {
        if (*s == '\n') buf[k++] = '\r';
        buf[k++] = (CHAR16)(uint8_t)*s;
    }
    buf[k] = 0;
    ST->ConOut->OutputString(ST->ConOut, buf);
}

static void die(const char *msg, const char *path)
{
    print("X86MSX emulator (UEFI): ");
    print(msg);
    if (path) { print(" "); print(path); }
    print("\nPress the power button to turn off.\n");
    for (;;) BS->Stall(1000000);
}

/* ------------------------------------------------------------------ */
/* 시간                                                                 */
/* ------------------------------------------------------------------ */
static uint64_t tsc_hz, tsc_per_ms;
static uint32_t plat_ms(void) { return (uint32_t)(rdtsc() / tsc_per_ms); }

/* 4.0.29: 화면 갱신 맞추기. 새 프레임이 시작되는 순간(화면 맨 위 줄을 그리기 시작할 때)에 위에서 아래로 복사하면
 * 복사(약 3 ms)가 화면 스캔(약 16 ms)보다 빨라 줄(찢김)이 안 생긴다.
 * 방법 1: 인텔 내장 그래픽의 현재 스캔 줄 레지스터(PIPE DSL, 0x70000/71000/72000) — PCI 00:02.0 BAR0
 * 방법 2: 레거시 VGA 상태 레지스터(0x3DA/0x3BA) 비트 3
 * 둘 다 주기가 40~100 Hz로 확인될 때만 켜고, 아니면 끈다 */
static int vs_port;                 /* 방법 2 포트 */
static int vs_fail;
static volatile uint32_t *vs_dsl;
static uint32_t vs_mask = 0x1FFF;
static uint32_t vs_shift;   /* 줄 번호 비트 */
static int vs_nv;                   /* 1 = 엔비디아 */   /* 방법 1 레지스터 */
static uint32_t vs_pci_rd(int b, int d, int f, int off)
{
    __asm__ volatile("outl %0, %1" : : "a"(0x80000000u | (uint32_t)(b << 16) | (uint32_t)(d << 11) | (uint32_t)(f << 8) | (uint32_t)(off & 0xFC)), "Nd"((uint16_t)0xCF8));
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)0xCFC)); return v;
}
static uint64_t vs_wt;
static uint32_t vs_hz100;           /* 잰 화면 주기 (Hz x 100), 0 = 모름 */
#define VS_AUTO 0x7FFF
static int vs_off = VS_AUTO;        /* 맞추기 시점 옮김 (줄). + = 새 프레임 시작 뒤, - = 앞. 기본(자동) = 전체 줄의 8.5% 앞 (AMD 노트북 실기: -96/1125) */
static uint32_t vs_vmax;            /* 본 줄 번호 최대 */
static int vs_effoff(void) { return vs_off != VS_AUTO ? vs_off : -(int)(vs_vmax * 85 / 1000); }
static int vs_wait(void)
{
    uint64_t lim = tsc_per_ms * 40, t0 = rdtsc();
    if (vs_dsl) {
        uint32_t prev = (*vs_dsl >> vs_shift) & vs_mask;
        int off = vs_effoff(), wrapped = 0, armed = 0;
        for (;;) {                                          /* 줄 번호가 줄어들면 = 새 프레임 시작 */
            uint32_t cur = (*vs_dsl >> vs_shift) & vs_mask;
            if (cur > vs_vmax && cur < 4000) vs_vmax = cur;
            if (off < 0 && vs_vmax > 200 && !wrapped) { if ((int)cur < (int)vs_vmax + off) armed = 1; else if (armed && cur < 4000) return 1; }
            if (cur < prev && (cur < 64 || prev - cur > 300)) {   /* 되감긴 값이 64 미만이 아니어도(300 넘게 줄면) 새 프레임으로 */
                if (off < 0 && vs_vmax > 200) armed = 1;             /* 앞당김 모드에서는 되감겼다고 끝내지 않고 앞당긴 줄까지 기다림 (끝내면 15 ms·1 ms 가 번갈아 나와 주기 확인이 늘 실패했음: LG gram DIAG screen per pA 015 001) */
                else { if (off <= 0) return 1; wrapped = 1; }
            }
            if (wrapped && (int)cur >= off) return 1;
            prev = cur;
            __asm__ volatile("pause");
            if (rdtsc() - t0 > lim) return 0;
        }
    }
    while (inb((uint16_t)vs_port) & 8) if (rdtsc() - t0 > lim) return 0;     /* 귀선 중이면 끝날 때까지 */
    while (!(inb((uint16_t)vs_port) & 8)) if (rdtsc() - t0 > lim) return 0;  /* 다음 귀선 시작까지 */
    return 1;
}
static void plat_vs_adj(int d) { if (vs_off == VS_AUTO) vs_off = vs_effoff(); vs_off += d; if (vs_off > 400) vs_off = 400; if (vs_off < -400) vs_off = -400; }
static uint32_t vs_per[8]; static int vs_pern;   /* 마지막 주기 확인에서 잰 간격(ms) — 실패 원인 기록 */
static int vs_period_ok(void)
{
    uint64_t prev; int ok = 0;
    vs_pern = 0;
    if (!vs_wait()) { vs_per[0] = 999; vs_pern = 1; return 0; }   /* 999 = 첫 프레임 시작도 못 잡음 (40 ms 시간 초과) */
    prev = rdtsc();
    for (int k = 0; k < 6; k++) {
        if (!vs_wait()) { vs_per[vs_pern++] = 999; return 0; }
        uint64_t now = rdtsc(), d = (now - prev) / (tsc_per_ms ? tsc_per_ms : 1);   /* ms */
        prev = now;
        vs_per[vs_pern++] = (uint32_t)d;
        if (d >= 10 && d <= 24) ok++; else return 0;   /* 40~100 Hz */
    }
    return ok >= 5;
}
static char vs_dbg[16][64]; static int vs_dbgn;
static void vs_hex(char *d, uint32_t v, int digits)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < digits; i++) d[i] = hx[(v >> ((digits - 1 - i) * 4)) & 15];
}
/* 진단 화면용 원시 값 (어떤 레지스터가 되는지 알아내기 위해): "screen gpu VVVV:DDDD bar XXXXXXXX" / "screen reg OOOOOO a b c" */
static void vs_dbg_gpu(int b, int d)
{
    if (vs_dbgn >= 16) return;
    char *l = vs_dbg[vs_dbgn++]; const char *t = "screen gpu "; int n = 0; while (*t) l[n++] = *t++;
    uint32_t id = vs_pci_rd(b, d, 0, 0), bar = vs_pci_rd(b, d, 0, 0x10);
    vs_hex(l + n, id & 0xFFFF, 4); n += 4; l[n++] = ':'; vs_hex(l + n, id >> 16, 4); n += 4;
    t = " bar "; while (*t) l[n++] = *t++;
    vs_hex(l + n, bar, 8); n += 8; l[n] = 0;
}
static void vs_dbg_reg(uint64_t base, uint32_t off, uint32_t stride)
{
    if (vs_dbgn >= 16) return;
    char *l = vs_dbg[vs_dbgn++]; const char *t = "screen reg "; int n = 0; while (*t) l[n++] = *t++;
    vs_hex(l + n, off, 6); n += 6;
    for (int h = 0; h < 2; h++) {
        volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)(base + off + (uint32_t)h * stride);
        uint32_t a = *r; uint64_t t0 = rdtsc(); while (rdtsc() - t0 < tsc_per_ms * 3) __asm__ volatile("pause");
        uint32_t c = *r;
        l[n++] = ' '; vs_hex(l + n, a & 0xFFFFFF, 6); n += 6; l[n++] = '/'; vs_hex(l + n, c & 0xFFFFFF, 6); n += 6;
    }
    l[n] = 0;
}

static void vs_dbg_per(const char *tag)   /* 주기 확인 실패 기록 "screen per <tag> 16 17 999" */
{
    if (vs_dbgn >= 16) return;
    char *l = vs_dbg[vs_dbgn++]; const char *t = "screen per "; int n = 0; while (*t) l[n++] = *t++;
    while (*tag && n < 20) l[n++] = *tag++;
    for (int i = 0; i < vs_pern && i < 7; i++) { l[n++] = ' '; uint32_t v = vs_per[i]; if (v > 999) v = 999; l[n++] = (char)('0' + v / 100 % 10); l[n++] = (char)('0' + v / 10 % 10); l[n++] = (char)('0' + v % 10); }
    l[n] = 0;
}

/* 4.0.29: 레지스터 위치를 모르는 칩(AMD 등)용 자동 찾기. MMIO 창을 읽어서 "줄 번호처럼 움직이는" 값을 찾는다.
 * 1단계: 창 전체를 3번 읽어 값(16비트 반쪽)이 매번 바뀌고 2300 미만인 것만 남김.
 * 2단계: 남은 것을 0.7 ms 간격으로 24번 읽어 거의 늘기만 하고(프레임 한 번에 한 번만 되돌아감) 빨리 움직이는 것을 고름.
 * 읽기만 한다 (쓰지 않음) */
static uint32_t vs_pass[3][0x20000];
static int vs_find(uint64_t base, uint32_t bytes)
{
    uint32_t n = bytes / 4;
    if (n > 0x20000) n = 0x20000;
    volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)base;
    for (int k = 0; k < 3; k++) {
        for (uint32_t i = 0; i < n; i++) vs_pass[k][i] = r[i];
        uint64_t t0 = rdtsc(); while (rdtsc() - t0 < tsc_per_ms * 7) __asm__ volatile("pause");
    }
    uint32_t ci[48]; uint8_t cs[48]; int nc = 0;
    for (uint32_t i = 0; i < n && nc < 48; i++) for (int h = 0; h < 2 && nc < 48; h++) {
        uint32_t a = (vs_pass[0][i] >> (h * 16)) & 0xFFFF, b = (vs_pass[1][i] >> (h * 16)) & 0xFFFF, c = (vs_pass[2][i] >> (h * 16)) & 0xFFFF;
        if (a < 2300 && b < 2300 && c < 2300 && a != b && b != c && (vs_pass[0][i] >> 16 != 0xFFFF)) { ci[nc] = i; cs[nc] = (uint8_t)(h * 16); nc++; }
    }
    if (!nc) {
        if (vs_dbgn < 16) { char *l = vs_dbg[vs_dbgn++]; const char *t = "screen scan: 0 cand, window "; int q = 0; while (*t) l[q++] = *t++; vs_hex(l + q, (uint32_t)(uintptr_t)(base & 0xFFFFFFFFu), 8); q += 8; l[q] = 0; }
        return 0;
    }
    static uint16_t sm[24][48];
    for (int k = 0; k < 24; k++) {
        uint64_t t0 = rdtsc();
        for (int c = 0; c < nc; c++) sm[k][c] = (uint16_t)((r[ci[c]] >> cs[c]) & 0xFFFF);
        while (rdtsc() - t0 < tsc_per_ms * 7 / 10) __asm__ volatile("pause");
    }
    for (int c = 0; c < nc; c++) {
        int drops = 0, big = 0; uint32_t up = 0;
        for (int k = 1; k < 24; k++) {
            if (sm[k][c] < sm[k - 1][c]) drops++;
            else { uint32_t d = (uint32_t)(sm[k][c] - sm[k - 1][c]); if (d > 400) big = 1; up += d; }
        }
        if (vs_dbgn < 15) { char *l = vs_dbg[vs_dbgn++]; const char *t = "cand "; int q = 0; while (*t) l[q++] = *t++; vs_hex(l + q, ci[c] * 4, 5); q += 5; l[q++] = ' '; l[q++] = cs[c] ? 'H' : 'L'; l[q++] = ' '; l[q++] = 'd'; l[q++] = (char)('0' + (drops > 9 ? 9 : drops)); l[q++] = ' '; l[q++] = big ? 'B' : '-'; l[q++] = ' '; vs_hex(l + q, up, 4); q += 4; l[q] = 0; }   /* 후보별 측정값 (줄 번호 레지스터를 못 찾은 원인 분석) */
        if (drops == 1 && up >= 400 && vs_dbgn < 15) {   /* 줄 번호 후보의 측정값 앞 12개를 그대로 남김 (되감기 값 확인용) */
            char *l = vs_dbg[vs_dbgn++]; const char *t = "smp "; int q = 0; while (*t) l[q++] = *t++;
            for (int k = 0; k < 12; k++) { vs_hex(l + q, sm[k][c], 3); q += 3; l[q++] = ' '; } l[q] = 0;
        }
        if (drops >= 1 && drops <= 3 && up >= 400) {   /* 기준 완화 (큰 점프 금지 제거) — 마지막 판정은 vs_period_ok (10~24 ms 주기 5번) */
            vs_dsl = r + ci[c]; vs_shift = cs[c]; vs_mask = 0xFFFF;
            if (vs_period_ok()) {
                if (vs_dbgn < 16) { char *l = vs_dbg[vs_dbgn++]; const char *t = "screen scan found reg "; int q = 0; while (*t) l[q++] = *t++; vs_hex(l + q, (uint32_t)(((uintptr_t)(r + ci[c]) - (uintptr_t)r)), 6); q += 6; t = " shift "; while (*t) l[q++] = *t++; l[q++] = cs[c] ? '1' : '0'; l[q] = 0; }
                return 1;
            }
            vs_dbg_per("amd "); vs_dsl = 0; vs_shift = 0;
        }
    }
    if (vs_dbgn < 16) { char *l = vs_dbg[vs_dbgn++]; const char *t = "screen scan: no scanline reg ("; int q = 0; while (*t) l[q++] = *t++; vs_hex(l + q, (uint32_t)nc, 2); q += 2; t = " cand)"; while (*t) l[q++] = *t++; l[q] = 0; }
    return 0;
}
static int vs_probe(void)
{
    {   /* 그래픽 장치 (PCI 클래스 03) 전부 진단에 표시 */
        int found = 0;
        for (int b = 0; b < 16; b++) for (int d = 0; d < 32; d++) {
            if ((vs_pci_rd(b, d, 0, 0) & 0xFFFF) == 0xFFFF) continue;
            if ((vs_pci_rd(b, d, 0, 8) >> 24) != 0x03) continue;
            vs_dbg_gpu(b, d); found++;
        }
        if (!found && vs_dbgn < 16) { const char *t = "screen gpu: none on PCI bus 0-15"; char *l = vs_dbg[vs_dbgn++]; int n = 0; while (*t) l[n++] = *t++; l[n] = 0; }
    }
    /* 방법 1: 인텔 내장 그래픽 */
    if ((vs_pci_rd(0, 2, 0, 0) & 0xFFFF) == 0x8086 && (vs_pci_rd(0, 2, 0, 8) >> 24) == 0x03) {
        uint32_t bar = vs_pci_rd(0, 2, 0, 0x10);
        uint64_t base = bar & 0xFFFFFFF0u;
        if ((bar & 6) == 4) base |= (uint64_t)vs_pci_rd(0, 2, 0, 0x14) << 32;
        uint32_t cmd = vs_pci_rd(0, 2, 0, 4);
        if (base && (cmd & 2)) {                            /* 메모리 켜져 있을 때만 (펌웨어가 켠 상태) */
            vs_dbg_reg(base, 0x70000, 0x1000);
            static const uint32_t pipes[3] = { 0x70000, 0x71000, 0x72000 };
            for (int pi = 0; pi < 3; pi++) {
                vs_dsl = (volatile uint32_t *)(uintptr_t)(base + pipes[pi]);
                if (vs_period_ok()) return 1;
                { char tg[4] = { 'p', (char)('A' + pi), ' ', 0 }; vs_dbg_per(tg); }
            }
            vs_dsl = 0;
        }
    }
    /* 방법 1b: 엔비디아 (nouveau의 rgpos 레지스터: Volta 이상 0x616330, Fermi~Pascal 0x616340, 머리마다 0x800) — BAR0 */
    for (int b = 0; b < 16 && !vs_dsl; b++) for (int d = 0; d < 32 && !vs_dsl; d++) {
        uint32_t id = vs_pci_rd(b, d, 0, 0);
        if ((id & 0xFFFF) != 0x10DE || (vs_pci_rd(b, d, 0, 8) >> 24) != 0x03) continue;
        uint32_t bar = vs_pci_rd(b, d, 0, 0x10), cmd = vs_pci_rd(b, d, 0, 4);
        if ((bar & 1) || !(cmd & 2) || !(bar & 0xFFFFFFF0u)) continue;
        uint64_t base = bar & 0xFFFFFFF0u;
        vs_dbg_reg(base, 0x616330, 0x800); vs_dbg_reg(base, 0x616340, 0x800);
        static const uint32_t offs[2] = { 0x616330, 0x616340 };
        for (int oi = 0; oi < 2 && !vs_dsl; oi++) for (int hd = 0; hd < 4; hd++) {
            vs_dsl = (volatile uint32_t *)(uintptr_t)(base + offs[oi] + (uint32_t)hd * 0x800);
            vs_mask = 0xFFFF;
            if (vs_period_ok()) { vs_nv = 1; break; }
            vs_dsl = 0;
        }
    }
    if (vs_dsl) return 1;
    /* 방법 1c: 자동 찾기 — AMD(BAR5 = MMIO, DCN 블록 0x6800~0x77FF만) · 엔비디아(디스플레이 창) */
    for (int b = 0; b < 16 && !vs_dsl; b++) for (int d = 0; d < 32 && !vs_dsl; d++) {
        uint32_t id = vs_pci_rd(b, d, 0, 0), cmd = vs_pci_rd(b, d, 0, 4);
        if ((vs_pci_rd(b, d, 0, 8) >> 24) != 0x03 || !(cmd & 2)) continue;
        if ((id & 0xFFFF) == 0x1002) {
            uint32_t bar = vs_pci_rd(b, d, 0, 0x24);
            if (vs_dbgn < 16) { char *l = vs_dbg[vs_dbgn++]; const char *t = "screen amd bar5 "; int q = 0; while (*t) l[q++] = *t++; vs_hex(l + q, bar, 8); q += 8; l[q] = 0; }
            if (!(bar & 1) && (bar & 0xFFFFFFF0u)) { vs_mask = 0xFFFF; vs_find((uint64_t)(bar & 0xFFFFFFF0u) + 0x13000, 0x1800); }   /* DCN 디스플레이 블록(BASE_IDX 2 = 0x34C0 dword + OTG 0x1b00대) 부근만 (전체 512 KiB를 읽다가 Renoir가 멈춤) */
        } else if ((id & 0xFFFF) == 0x10DE) {
            uint32_t bar = vs_pci_rd(b, d, 0, 0x10);
            if (!(bar & 1) && (bar & 0xFFFFFFF0u)) { vs_mask = 0xFFFF; vs_find((uint64_t)(bar & 0xFFFFFFF0u) + 0x610000, 0x20000); }
        }
    }
    if (vs_dsl) return 1;
    vs_mask = 0x1FFF; vs_shift = 0;
    /* 방법 2: 레거시 VGA */
    static const uint16_t ports[2] = { 0x3DA, 0x3BA };
    for (int pi = 0; pi < 2; pi++) {
        vs_port = ports[pi];
        if (vs_period_ok()) return 1;
    }
    vs_port = 0;
    return 0;
}
uint32_t timer_ms(void) { return plat_ms(); }

void delay_ms(uint32_t ms)
{
    uint64_t end = rdtsc() + (uint64_t)ms * tsc_per_ms;
    while (rdtsc() < end) __asm__ volatile("pause");
}

/* ------------------------------------------------------------------ */
/* 문자열 변환: 경로는 UTF-8, UEFI는 UTF-16                              */
/* ------------------------------------------------------------------ */
static int utf16_to_utf8(const CHAR16 *s, char *d, int max)
{
    int k = 0;
    for (; *s; s++) {
        uint32_t c = *s;
        if (c < 0x80) { if (k + 1 >= max) break; d[k++] = (char)c; }
        else if (c < 0x800) { if (k + 2 >= max) break; d[k++] = (char)(0xC0 | (c >> 6)); d[k++] = (char)(0x80 | (c & 0x3F)); }
        else { if (k + 3 >= max) break; d[k++] = (char)(0xE0 | (c >> 12)); d[k++] = (char)(0x80 | ((c >> 6) & 0x3F)); d[k++] = (char)(0x80 | (c & 0x3F)); }
    }
    d[k] = 0;
    return k;
}

static void utf8_to_utf16(const char *s, CHAR16 *d, int max)
{
    int k = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && k < max - 1) {
        uint32_t c = *p++;
        if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 0x0F) << 12) | ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        else if (c >= 0xC0 && p[0]) { c = ((c & 0x1F) << 6) | (p[0] & 0x3F); p += 1; }
        d[k++] = (CHAR16)c;
    }
    d[k] = 0;
}

/* 표시용: ASCII 아닌 글자는 '?' */
static void display_name(const CHAR16 *s, char *d, int max)
{
    int k = 0;
    for (; s[k] && k < max - 1; k++) d[k] = (s[k] >= 0x20 && s[k] < 0x7F) ? (char)s[k] : '?';
    d[k] = 0;
}

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* ------------------------------------------------------------------ */
/* 드라이브와 파일 (실행 중에도 펌웨어로 읽음)                            */
/* ------------------------------------------------------------------ */
#define MAX_VOLS 32
static EFI_HANDLE vols[MAX_VOLS];
static int nvols;

/* 볼륨에 설정 파일이 있는가 (시스템 USB 찾기) */
static int vol_is_system(EFI_HANDLE h)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root, *f;
    if (BS->HandleProtocol(h, &SIMPLE_FS_GUID, (void **)&fs) != EFI_SUCCESS) return 0;
    if (fs->OpenVolume(fs, &root) != EFI_SUCCESS) return 0;
    int ok = 0;
    static CHAR16 n1[] = { '\\', 'x', '8', '6', 'm', 's', 'x', '.', 'i', 'n', 'i', 0 };
    static CHAR16 n2[] = { '\\', 'm', 's', 'x', 'e', 'm', 'u', 'l', 'a', 't', 'o', 'r', '.', 'i', 'n', 'i', 0 };
    if (root->Open(root, &f, n1, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS || root->Open(root, &f, n2, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS) {
        f->Close(f);
        ok = 1;
    }
    root->Close(root);
    return ok;
}

static void refresh_volumes(void)
{
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    nvols = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &SIMPLE_FS_GUID, 0, &n, &hs) != EFI_SUCCESS) return;
    int boot_found = 0;
    for (UINTN i = 0; i < n; i++) if (hs[i] == boot_dev) boot_found = 1;
    if (!boot_found)                                    /* 시스템 USB를 뽑았다 다시 꽂음: 설정 파일이 있는 볼륨 */
        for (UINTN i = 0; i < n; i++) if (vol_is_system(hs[i])) { boot_dev = hs[i]; break; }
    for (UINTN i = 0; i < n && nvols < MAX_VOLS; i++) if (hs[i] == boot_dev) vols[nvols++] = hs[i];   /* 시스템 USB 먼저 */
    for (UINTN i = 0; i < n && nvols < MAX_VOLS; i++) if (hs[i] != boot_dev) vols[nvols++] = hs[i];
    BS->FreePool(hs);
}

/* 새로 꽂은 USB 저장장치에 드라이버 연결 (USB 분류 08만: 키보드는 우리 드라이버가 쓰므로 건드리지 않음) */
static void connect_storage(void)
{
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &n, &hs) != EFI_SUCCESS) return;
    for (UINTN i = 0; i < n; i++) {
        EFI_USB_IO_PROTOCOL *io;
        EFI_USB_INTERFACE_DESCRIPTOR ifd;
        if (BS->HandleProtocol(hs[i], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
        if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;
        if (ifd.InterfaceClass == 0x08) BS->ConnectController(hs[i], 0, 0, 1);
    }
    BS->FreePool(hs);
}

/* 시스템 USB(설정 파일이 있는 볼륨)가 지금 목록에 있는가 */
static int system_vol_present(void)
{
    for (int i = 0; i < nvols; i++) if (vols[i] == boot_dev && vol_is_system(vols[i])) return 1;
    return 0;
}
/* 뽑았다 다시 꽂은 USB 를 펌웨어가 아직 못 잡았을 때 — USB 호스트 컨트롤러와 허브·저장 장치 핸들에 드라이버를 다시 연결 (EFI 셸 map -r 과 같은 방식).
 * 키보드·마우스·패드·랜 등 우리가 쓰는 USB 장치(허브·저장 장치가 아닌 USB_IO)와 화면 장치는 건드리지 않음 */
static void connect_usb_bus(void)
{
    static const EFI_GUID usb2hc = { 0x3e745226, 0x9818, 0x45b6, { 0xa2, 0xac, 0xd7, 0xcd, 0x0e, 0x8b, 0xa2, 0xbc } };
    EFI_HANDLE *hs = 0; UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &usb2hc, 0, &n, &hs) == EFI_SUCCESS && hs) {
        for (UINTN i = 0; i < n; i++) BS->ConnectController(hs[i], 0, 0, 0);
        BS->FreePool(hs); hs = 0; n = 0;
    }
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &n, &hs) == EFI_SUCCESS && hs) {
        for (UINTN i = 0; i < n; i++) {
            EFI_USB_IO_PROTOCOL *io; EFI_USB_INTERFACE_DESCRIPTOR ifd;
            if (BS->HandleProtocol(hs[i], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
            if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;
            if (ifd.InterfaceClass == 0x08 || ifd.InterfaceClass == 0x09) BS->ConnectController(hs[i], 0, 0, 1);
        }
        BS->FreePool(hs);
    }
}
static uint32_t g_rescan_n, g_rescan_deep, g_rescan_fail;   /* 진단 (정보 화면) */
static void rescan_volumes(void)
{
    g_rescan_n++;
    connect_storage();
    refresh_volumes();
    if (system_vol_present()) return;
    g_rescan_deep++;
    for (int t = 0; t < 3; t++) {                       /* 펌웨어가 새 장치를 잡을 시간을 주며 세 번까지 */
        connect_usb_bus();
        BS->Stall(300000);
        connect_storage();
        refresh_volumes();
        if (system_vol_present()) return;
    }
    g_rescan_fail++;
}

static EFI_FILE_PROTOCOL *open_volume_once(int v)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root;
    if (v < 0 || v >= nvols) return 0;
    if (BS->HandleProtocol(vols[v], &SIMPLE_FS_GUID, (void **)&fs) != EFI_SUCCESS) return 0;
    if (fs->OpenVolume(fs, &root) != EFI_SUCCESS) return 0;
    return root;
}

/* 볼륨을 못 열면(장치가 빠졌거나 다시 꽂혀 핸들이 바뀜) 다시 찾고 한 번 더 */
static EFI_FILE_PROTOCOL *open_volume(int v)
{
    EFI_FILE_PROTOCOL *r = open_volume_once(v);
    if (!r) { rescan_volumes(); r = open_volume_once(v); }
    return r;
}

/* "N:" 또는 "N:\a\b" → 볼륨 번호, UTF-16 하위 경로("\a\b" 또는 "\") */
static int parse_path(const char *path, CHAR16 *sub, int max)
{
    int v = 0, i = 0;
    while (path[i] >= '0' && path[i] <= '9') v = v * 10 + (path[i++] - '0');
    if (!i || path[i] != ':') return -1;
    i++;
    if (!path[i]) { sub[0] = '\\'; sub[1] = 0; return v; }
    utf8_to_utf16(path + i, sub, max);
    return v;
}

static void fmt_size(char *d, uint64_t bytes)
{
    uint64_t v = bytes >> 30;
    const char *u = " GB";
    if (v == 0) { v = bytes >> 20; u = " MB"; }
    char num[24]; int n = 0;
    do { num[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    int k = 0;
    while (n) d[k++] = num[--n];
    while (*u) d[k++] = *u++;
    d[k] = 0;
}

static int hidden_dir(const char *n)
{
    if (n[0] == '.' || n[0] == '$') return 1;                       /* ., .., $RECYCLE.BIN */
    const char *sys = "system volume information";
    int i = 0;
    while (sys[i] && lower(n[i]) == sys[i]) i++;
    return !sys[i] && !n[i];
}

static MSX m;

static int list_all;                                            /* 3.0.2: 모든 파일 (디스크 파일 복사) */
static int plat_list(const char *path, FeDirEntry *out, int max)
{
    int n = 0, base = fe_list_skip, raw = -1;               /* 4.0.34: 창 읽기 (한 번 쓰면 지움) */
    fe_list_skip = 0; fe_list_next = -1;
    if (!path[0]) {                                                 /* 최상위: 드라이브 목록 (새 USB 확인) */
        rescan_volumes();
        for (int v = 0; v < nvols && n < max; v++) {
            EFI_FILE_PROTOCOL *r = open_volume(v);
            if (!r) continue;
            FeDirEntry *e = &out[n];
            memset(e, 0, sizeof *e);
            char label[40] = "", size[16] = "";
            uint8_t ib[512] __attribute__((aligned(8)));
            UINTN isz = sizeof ib;
            if (r->GetInfo(r, &FS_INFO_GUID, &isz, ib) == EFI_SUCCESS) {
                EFI_FILE_SYSTEM_INFO *fi = (EFI_FILE_SYSTEM_INFO *)ib;
                display_name(fi->VolumeLabel, label, 24);
                fmt_size(size, fi->VolumeSize);
            }
            r->Close(r);
            char *q = e->name;
            const char *t = "Drive ";
            while (*t) *q++ = *t++;
            if (v + 1 >= 10) *q++ = (char)('0' + (v + 1) / 10);
            *q++ = (char)('0' + (v + 1) % 10);
            *q++ = ' ';
            for (int i = 0; label[i] && q < e->name + 40; i++) *q++ = label[i];
            if (label[0]) *q++ = ' ';
            for (int i = 0; size[i] && q < e->name + 52; i++) *q++ = size[i];
            if (vols[v] == boot_dev) { t = " (boot)"; while (*t && q < e->name + 62) *q++ = *t++; }
            *q = 0;
            q = e->key;
            if (v >= 10) *q++ = (char)('0' + v / 10);
            *q++ = (char)('0' + v % 10);
            *q++ = ':';
            *q = 0;
            e->is_dir = 1;
            n++;
        }
        return n;
    }
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *dir;
    if (!r) return -1;
    if (r->Open(r, &dir, sub, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS) { r->Close(r); return -1; }
    static uint8_t buf[2048] __attribute__((aligned(8)));
    for (;;) {
        UINTN sz = sizeof buf;
        if (dir->Read(dir, &sz, buf) != EFI_SUCCESS || sz == 0) break;
        raw++;
        if (raw < base) continue;                      /* 4.0.34: 앞 창에서 이미 읽은 항목 */
        EFI_FILE_INFO *fi = (EFI_FILE_INFO *)buf;
        int isdir = (fi->Attribute & EFI_FILE_DIRECTORY) != 0;
        if (n >= max) { fe_list_next = raw; break; }                           /* 4.0.29: 칸이 가득 차면 out[max]를 건드리지 않음 (256개 넘는 폴더에서 이웃 창의 개수가 0이 되던 문제) */
        FeDirEntry *e = &out[n];
        memset(e, 0, sizeof *e);
        utf16_to_utf8(fi->FileName, e->name, sizeof e->name);   /* 3.4.7: 표시 이름도 UTF-8 (글자 단위로 자름) */
        if (isdir && hidden_dir(e->name)) continue;     /* 3.6.2: 파일은 모두 (종류 거르기는 프런트엔드가. 전에는 롬·디스크 등만 보여 .sf2·.zip·.txt·디렉토리 삽입 파일이 빠짐) */
        utf16_to_utf8(fi->FileName, e->key, sizeof e->key);
        e->is_dir = (uint8_t)isdir;
        e->size = fi->FileSize > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)fi->FileSize;
        const EFI_TIME *ct = (const EFI_TIME *)fi->CreateTime;
        if (ct->Year >= 1980 && ct->Year < 2200 && ct->Month >= 1 && ct->Month <= 12) {
            e->year = ct->Year; e->mon = ct->Month; e->day = ct->Day; e->hour = ct->Hour; e->min = ct->Minute;
        }
        n++;
    }
    dir->Close(dir);
    r->Close(r);
    return n;
}
static int plat_list_all(const char *path, FeDirEntry *out, int max) { list_all = 1; int n = plat_list(path, out, max); list_all = 0; return n; }

static void ra_flush(void);
static uint32_t g_cst, g_wst;   /* 마지막 파일 만들기·쓰기 결과(EFI 상태값, 0 = 성공, 0xFFFF0001 = 볼륨을 못 엶) — 정보 화면에 표시 */
static int plat_write(const char *path, const uint8_t *data, uint32_t len)
{
    ra_flush();                                         /* 3.7.1 */
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *f;
    if (!r) { g_wst = 0xFFFF0001u; return -1; }
    { EFI_STATUS os = r->Open(r, &f, sub, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0); g_wst = (uint32_t)os; if (os != EFI_SUCCESS) { r->Close(r); return -1; } }
    int rc = -1;
    uint8_t ib[1024] __attribute__((aligned(8)));
    UINTN isz = sizeof ib;
    if (f->GetInfo(f, &FILE_INFO_GUID, &isz, ib) == EFI_SUCCESS && f->SetPosition(f, 0) == EFI_SUCCESS) {
        UINTN n = len;
        EFI_FILE_INFO *fi = (EFI_FILE_INFO *)ib;
        EFI_STATUS ws = f->Write(f, &n, (void *)data); g_wst = (uint32_t)ws;
        if (ws == EFI_SUCCESS && n == len) {
            rc = 0;
            if (fi->FileSize > len) {                       /* 줄어든 파일: 크기 고치기 */
                fi->FileSize = len;
                if (f->SetInfo(f, &FILE_INFO_GUID, isz, fi) != EFI_SUCCESS) rc = -1;
            }
            if (f->Flush(f) != EFI_SUCCESS) rc = -1;
        }
    }
    f->Close(f);
    r->Close(r);
    return rc;
}

/* 3.5.5: 파일 크기를 바꾸지 않고 일부만 고쳐 씀 (설정 파일: 바뀐 섹터만 → USB 쓰기 짧게) */
static int plat_write_at(const char *path, uint32_t off, const uint8_t *data, uint32_t len)
{
    ra_flush();                                         /* 3.7.1 */
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *f;
    if (!r) return -1;
    if (r->Open(r, &f, sub, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0) != EFI_SUCCESS) { r->Close(r); return -1; }
    int rc = -1;
    UINTN n = len;
    uint8_t ib[1024] __attribute__((aligned(8)));
    UINTN isz = sizeof ib;
    if (f->GetInfo(f, &FILE_INFO_GUID, &isz, ib) == EFI_SUCCESS && ((EFI_FILE_INFO *)ib)->FileSize >= (uint64_t)off + len &&   /* 파일보다 길게 쓰지 않음 */
        f->SetPosition(f, off) == EFI_SUCCESS && f->Write(f, &n, (void *)data) == EFI_SUCCESS && n == len && f->Flush(f) == EFI_SUCCESS) rc = 0;
    f->Close(f);
    r->Close(r);
    return rc;
}

static int plat_append(const char *path, const uint8_t *data, uint32_t len)   /* 파일 끝에 덧붙임 */
{
    ra_flush();
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *f;
    if (!r) return -1;
    if (r->Open(r, &f, sub, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0) != EFI_SUCCESS) { r->Close(r); return -1; }
    int rc = -1;
    UINTN n = len;
    if (f->SetPosition(f, 0xFFFFFFFFFFFFFFFFull) == EFI_SUCCESS && f->Write(f, &n, (void *)data) == EFI_SUCCESS && n == len && f->Flush(f) == EFI_SUCCESS) rc = 0;
    f->Close(f);
    r->Close(r);
    return rc;
}
static int plat_create(const char *path, int is_dir)
{
    ra_flush();                                         /* 3.7.1 */
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *f;
    if (!r) { g_cst = 0xFFFF0001u; return -1; }
    EFI_STATUS st = r->Open(r, &f, sub, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, is_dir ? EFI_FILE_DIRECTORY : 0);
    g_cst = (uint32_t)st;
    if (st == EFI_SUCCESS) { f->Flush(f); f->Close(f); }
    r->Close(r);
    return st == EFI_SUCCESS ? 0 : -1;
}

/* 3.0.2: 파일 지우기 (디스크 파일 복사 창의 .dsk) */
static int plat_delete(const char *path)
{
    ra_flush();                                         /* 3.7.1 */
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *f;
    if (!r) return -1;
    EFI_STATUS st = r->Open(r, &f, sub, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (st == EFI_SUCCESS) st = f->Delete(f);
    r->Close(r);
    return st == EFI_SUCCESS ? 0 : -1;
}

/* 3.0.2: 큰 파일 만들기 — 앞부분을 쓰고 나머지는 0을 256 KiB씩 (진행 표시) */
static uint8_t zero_chunk[256 * 1024];
static int plat_make_file(const char *path, uint32_t size, const uint8_t *prefix, uint32_t plen, void (*progress)(int pct))
{
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *f;
    if (!r) return -1;
    if (r->Open(r, &f, sub, EFI_FILE_MODE_READ, 0) == EFI_SUCCESS) { f->Close(f); r->Close(r); return -1; }   /* 이미 있음 */
    if (r->Open(r, &f, sub, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0) != EFI_SUCCESS) { r->Close(r); return -1; }
    int rc = 0;
    UINTN n = plen;
    if (plen && (f->Write(f, &n, (void *)prefix) != EFI_SUCCESS || n != plen)) rc = -1;
    uint32_t done = plen;
    while (!rc && done < size) {
        uint32_t k = size - done < sizeof zero_chunk ? size - done : (uint32_t)sizeof zero_chunk;
        n = k;
        if (f->Write(f, &n, zero_chunk) != EFI_SUCCESS || n != k) rc = -1;
        done += k;
        if (progress && (done & ((4u << 20) - 1)) < k) progress((int)((uint64_t)done * 100 / size));
    }
    if (f->Flush(f) != EFI_SUCCESS) rc = -1;
    f->Close(f);
    r->Close(r);
    if (progress) progress(100);
    return rc;
}

static int plat_read(const char *path, uint8_t *buf, uint32_t max, uint32_t *size)
{
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *f;
    if (!r) return -1;
    if (r->Open(r, &f, sub, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS) { r->Close(r); return -1; }
    int rc = -1;
    uint8_t ib[1024] __attribute__((aligned(8)));
    UINTN isz = sizeof ib;
    if (f->GetInfo(f, &FILE_INFO_GUID, &isz, ib) == EFI_SUCCESS) {
        uint64_t fs = ((EFI_FILE_INFO *)ib)->FileSize;
        UINTN want = (UINTN)(fs < max ? fs : max);
        UINTN got = want;
        if (fs <= 0xFFFFFFFFu && f->Read(f, &got, buf) == EFI_SUCCESS && got == want) { *size = (uint32_t)fs; rc = 0; }
    }
    f->Close(f);
    r->Close(r);
    return rc;
}

/* ---- 3.7.1 가상 하드: 파일 일부 읽기 (최근 파일 4개를 열어 둠) ---- */
#define RA_N 4
static struct { char path[1024]; EFI_FILE_PROTOCOL *root, *f; uint64_t size; uint32_t use; } ra[RA_N];
static uint32_t ra_clock;
static void ra_flush(void)
{
    for (int i = 0; i < RA_N; i++) {
        if (ra[i].f) ra[i].f->Close(ra[i].f);
        if (ra[i].root) ra[i].root->Close(ra[i].root);
        ra[i].f = ra[i].root = 0; ra[i].path[0] = 0;
    }
}
static int plat_read_at(const char *path, uint32_t off, uint8_t *buf, uint32_t len)
{
    int k = -1;
    for (int i = 0; i < RA_N; i++) if (ra[i].f && !strcmp(ra[i].path, path)) { k = i; break; }
    if (k < 0) {
        k = 0;
        for (int i = 1; i < RA_N; i++) if (ra[i].use < ra[k].use) k = i;
        if (ra[k].f) ra[k].f->Close(ra[k].f);
        if (ra[k].root) ra[k].root->Close(ra[k].root);
        ra[k].f = ra[k].root = 0; ra[k].path[0] = 0;
        CHAR16 sub[1024];
        int v = parse_path(path, sub, 1024);
        EFI_FILE_PROTOCOL *r = open_volume(v), *f;
        if (!r) return -1;
        if (r->Open(r, &f, sub, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS) { r->Close(r); return -1; }
        uint8_t ib[1024] __attribute__((aligned(8)));
        UINTN isz = sizeof ib;
        if (f->GetInfo(f, &FILE_INFO_GUID, &isz, ib) != EFI_SUCCESS) { f->Close(f); r->Close(r); return -1; }
        ra[k].size = ((EFI_FILE_INFO *)ib)->FileSize;
        ra[k].root = r; ra[k].f = f;
        int i = 0; for (; path[i] && i < 1023; i++) ra[k].path[i] = path[i]; ra[k].path[i] = 0;
    }
    ra[k].use = ++ra_clock;
    memset(buf, 0, len);
    if (off >= ra[k].size) return 0;
    if (ra[k].f->SetPosition(ra[k].f, off) != EFI_SUCCESS) return -1;
    UINTN n = len;
    return ra[k].f->Read(ra[k].f, &n, buf) == EFI_SUCCESS ? 0 : -1;
}

/* ---- 하드 디스크 이미지 (2.8): 파일을 열어 두고 섹터 단위로 ---- */
static EFI_FILE_PROTOCOL *hd_root[4], *hd_file[4];     /* 3.4.3 DEV-15: 0 마스터, 1 슬레이브, 3.7.0: 2 = 이미지 파일 관리 */
static uint32_t hd_secs[4];
static int hd_dirty[4];

static void plat_hd_close(int u)
{
    if (u < 0 || u > 3) return;
    if (hd_file[u]) { if (hd_dirty[u]) hd_file[u]->Flush(hd_file[u]); hd_file[u]->Close(hd_file[u]); }
    if (hd_root[u]) hd_root[u]->Close(hd_root[u]);
    hd_file[u] = hd_root[u] = 0; hd_secs[u] = 0; hd_dirty[u] = 0;
}

static int plat_hd_open(int u, const char *path, uint32_t *sectors)
{
    if (u < 0 || u > 3) u = 2;
    plat_hd_close(u);
    CHAR16 sub[1024];
    int v = parse_path(path, sub, 1024);
    EFI_FILE_PROTOCOL *r = open_volume(v), *f;
    if (!r) return -1;
    if (r->Open(r, &f, sub, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0) != EFI_SUCCESS) { r->Close(r); return -1; }
    uint8_t ib[1024] __attribute__((aligned(8)));
    UINTN isz = sizeof ib;
    if (f->GetInfo(f, &FILE_INFO_GUID, &isz, ib) != EFI_SUCCESS) { f->Close(f); r->Close(r); return -1; }
    uint64_t fs = ((EFI_FILE_INFO *)ib)->FileSize / 512;
    if (!fs || fs > 0xFFFFFFFFu) { f->Close(f); r->Close(r); return -1; }
    hd_root[u] = r; hd_file[u] = f; hd_secs[u] = (uint32_t)fs;
    *sectors = hd_secs[u];
    return 0;
}

static int plat_hd_read(int u, uint32_t lba, uint8_t *buf)
{
    if (u < 0 || u > 3) u = 2;
    if (!hd_file[u] || lba >= hd_secs[u] || hd_file[u]->SetPosition(hd_file[u], (uint64_t)lba * 512) != EFI_SUCCESS) return -1;
    UINTN n = 512;
    return hd_file[u]->Read(hd_file[u], &n, buf) == EFI_SUCCESS && n == 512 ? 0 : -1;
}

static int plat_hd_write(int u, uint32_t lba, const uint8_t *buf)
{
    if (u < 0 || u > 3) u = 2;
    if (!hd_file[u] || lba >= hd_secs[u] || hd_file[u]->SetPosition(hd_file[u], (uint64_t)lba * 512) != EFI_SUCCESS) return -1;
    UINTN n = 512;
    if (hd_file[u]->Write(hd_file[u], &n, (void *)buf) != EFI_SUCCESS || n != 512) return -1;
    hd_dirty[u] = 1;
    return 0;
}

static void plat_hd_flush(void) { for (int u = 0; u < 4; u++) if (hd_file[u] && hd_dirty[u]) { hd_file[u]->Flush(hd_file[u]); hd_dirty[u] = 0; } }

/* ------------------------------------------------------------------ */
/* USB 전체 용량 사용: 부팅 파티션의 부모 디스크 블록 장치                */
/* ------------------------------------------------------------------ */
static void put_line(char *d, const char *s);
static EFI_BLOCK_IO_PROTOCOL *boot_disk;
static uint8_t *bounce;                                     /* 64섹터, 4 KiB 정렬 */

static UINTN dp_size(EFI_DEVICE_PATH *dp)
{
    UINTN n = 0;
    for (int guard = 0; guard < 64; guard++) {
        uint16_t len = (uint16_t)(dp->Length[0] | (dp->Length[1] << 8));
        if (dp->Type == 0x7F && dp->SubType == 0xFF) return n;
        if (len < 4) return 0;
        n += len;
        dp = (EFI_DEVICE_PATH *)((uint8_t *)dp + len);
    }
    return 0;
}

static EFI_BLOCK_IO_PROTOCOL *find_boot_disk(void)
{
    EFI_DEVICE_PATH *pdp;
    if (BS->HandleProtocol(boot_dev, &DEVICE_PATH_GUID, (void **)&pdp) != EFI_SUCCESS) return 0;
    UINTN plen = dp_size(pdp);
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &BLOCK_IO_GUID, 0, &n, &hs) != EFI_SUCCESS) return 0;
    EFI_BLOCK_IO_PROTOCOL *found = 0;
    for (UINTN i = 0; i < n && !found; i++) {
        EFI_BLOCK_IO_PROTOCOL *b;
        EFI_DEVICE_PATH *dp;
        if (BS->HandleProtocol(hs[i], &BLOCK_IO_GUID, (void **)&b) != EFI_SUCCESS) continue;
        if (!b->Media || b->Media->LogicalPartition || !b->Media->MediaPresent || b->Media->BlockSize != 512) continue;
        if (BS->HandleProtocol(hs[i], &DEVICE_PATH_GUID, (void **)&dp) != EFI_SUCCESS) continue;
        UINTN dlen = dp_size(dp);
        if (dlen && dlen < plen && !memcmp(dp, pdp, dlen)) found = b;     /* 디스크 경로가 파티션 경로의 앞부분 */
    }
    BS->FreePool(hs);
    return found;
}

static int disk_rd(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf)
{
    (void)ctx;
    if (boot_disk->ReadBlocks(boot_disk, boot_disk->Media->MediaId, lba, count * 512, bounce) != EFI_SUCCESS) return -1;
    memcpy(buf, bounce, count * 512);
    return 0;
}

static int disk_wr(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf)
{
    (void)ctx;
    memcpy(bounce, buf, count * 512);
    return boot_disk->WriteBlocks(boot_disk, boot_disk->Media->MediaId, lba, count * 512, bounce) == EFI_SUCCESS ? 0 : -1;
}

static int plat_grow_query(uint64_t *part, uint64_t *disk)
{
    if (!boot_disk || boot_disk->Media->ReadOnly) return -1;
    GrowIO io = { disk_rd, disk_wr, 0, boot_disk->Media->LastBlock + 1, 0, 0, 0 };
    uint64_t ps = usbgrow_part_sectors(&io);
    if (!ps) return -1;
    *part = ps * 512; *disk = io.disk_sectors * 512;
    return 0;
}

static int plat_grow_run(void (*progress)(int, const char *), char *err, int errlen)
{
    if (!boot_disk) return -1;
    EFI_PHYSICAL_ADDRESS mem = (64ull << 30) - 1;
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (80u << 20) / 4096, &mem) != EFI_SUCCESS) {
        put_line(err, "not enough memory"); (void)errlen; return -1;
    }
    GrowIO io = { disk_rd, disk_wr, 0, boot_disk->Media->LastBlock + 1, (uint8_t *)(uintptr_t)mem, 80u << 20, progress };
    /* 확장하기 전에 펌웨어 FAT 드라이버를 이 파티션에서 뗀다. 드라이버는 옛 FAT·FSInfo·폴더를 캐시로 갖고 있다가
     * 닫을 때·볼륨 상태를 고칠 때 직접 USB 에 다시 쓰는데, 확장 뒤에 그러면 새 파일 시스템이 옛 내용으로 덮여 Windows 가 손상으로 본다
     * (실기 덤프로 확인: FAT 첫 섹터·FSInfo 가 옛 값). 떼는 순간 캐시를 먼저 비우고, 그 뒤에는 쓸 드라이버가 없다.
     * 확장 코드는 디스크 전체 Block I/O 만 쓰므로 영향 없음 (이 USB 의 파일은 확장 뒤 다시 시작할 때까지 못 읽음) */
    BS->DisconnectController(boot_dev, 0, 0);
    int r = usbgrow_run(&io, err, errlen);
    boot_disk->FlushBlocks(boot_disk);
    return r;
}

/* ------------------------------------------------------------------ */
/* 메모리 형식: 펌웨어 페이지 테이블 항목을 고쳐 WC/UC 지정               */
/* PAT 항목 1 = WC (PWT), 항목 3 = UC (PCD|PWT)                         */
/* ------------------------------------------------------------------ */
#define ADDR_MASK 0x000FFFFFFFFFF000ull

static uint64_t *table_of(uint64_t e) { return (uint64_t *)(uintptr_t)(e & ADDR_MASK); }

/* attr: 0x08 = WC, 0x18 = UC. 성공한 페이지 수 반환 */
static int set_mem_type(uint64_t base, uint64_t size, uint64_t attr)
{
    uint64_t cr0, cr3, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    int la57 = (cr4 >> 12) & 1;                                     /* 3.4.3 PLAT-05: 5단계 페이징(CR4.LA57)이면 PML5 한 단계를 더 따라감 */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 & ~(1ull << 16)));  /* WP 끄기 (페이지 표가 읽기 전용일 수 있음) */
    int count = 0;
    uint64_t *top = table_of(cr3);
    for (uint64_t a = base & ~0xFFFull; a < base + size; ) {
        uint64_t *pml4 = top;
        if (la57) {
            uint64_t *e5 = &top[(a >> 48) & 511];
            if (!(*e5 & 1)) { a = (a | ((1ull << 48) - 1)) + 1; continue; }
            pml4 = table_of(*e5);
        }
        uint64_t *e4 = &pml4[(a >> 39) & 511];
        if (!(*e4 & 1)) { a = (a | ((1ull << 39) - 1)) + 1; continue; }
        uint64_t *e3 = &table_of(*e4)[(a >> 30) & 511];
        if (!(*e3 & 1)) { a = (a | ((1ull << 30) - 1)) + 1; continue; }
        if (*e3 & 0x80) {                                           /* 1 GiB 페이지 → 2 MiB 페이지로 나눔 */
            EFI_PHYSICAL_ADDRESS pg = 0xFFFFFFFFu;
            if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1, &pg) != EFI_SUCCESS) break;
            uint64_t *pd = (uint64_t *)(uintptr_t)pg, old = *e3;
            uint64_t gb = old & 0x000FFFFFC0000000ull, keep = old & (0x1FFull | (1ull << 12) | (1ull << 63));
            for (int i = 0; i < 512; i++) pd[i] = (gb + ((uint64_t)i << 21)) | keep;
            *e3 = (uint64_t)pg | (old & 0x7);
        }
        uint64_t *e2 = &table_of(*e3)[(a >> 21) & 511];
        if (!(*e2 & 1)) { a = (a | 0x1FFFFF) + 1; continue; }
        if (*e2 & 0x80) {                                           /* 2 MiB 페이지 */
            *e2 = (*e2 & ~(0x18ull | (1ull << 12))) | attr;
            a = (a | 0x1FFFFF) + 1;
        } else {                                                    /* 4 KiB 페이지 */
            uint64_t *e1 = &table_of(*e2)[(a >> 12) & 511];
            if (*e1 & 1) *e1 = (*e1 & ~(0x18ull | 0x80ull)) | attr;
            a += 0x1000;
        }
        count++;
    }
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");     /* TLB 비우기 */
    __asm__ volatile("wbinvd" ::: "memory");
    return count;
}

static int wc_ready;
static void pat_setup(void)
{
    uint64_t pat = rdmsr(0x277);
    pat = (pat & ~(0xFFull << 8)) | (0x01ull << 8);                 /* 항목 1 = WC */
    __asm__ volatile("wbinvd" ::: "memory");
    wrmsr(0x277, pat);
    wc_ready = 1;
}

void paging_set_uc(uintptr_t addr) { if (wc_ready && addr >= HDA_DMA_MIN) set_mem_type(addr, 1, 0x18); }

/* ------------------------------------------------------------------ */
/* 노트북 내장 키보드 (8042): 펌웨어 PS/2 드라이버를 떼고 직접 폴링        */
/* ------------------------------------------------------------------ */
static int have_8042, ps2_detached;

static void detach_ps2_driver(void)
{
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &TEXT_IN_GUID, 0, &n, &hs) != EFI_SUCCESS) return;
    for (UINTN i = 0; i < n; i++) {
        EFI_DEVICE_PATH *dp;
        if (BS->HandleProtocol(hs[i], &DEVICE_PATH_GUID, (void **)&dp) != EFI_SUCCESS) continue;
        for (int guard = 0; guard < 32 && !(dp->Type == 0x7F && dp->SubType == 0xFF); guard++) {
            uint16_t len = (uint16_t)(dp->Length[0] | (dp->Length[1] << 8));
            if (dp->Type == 2 && dp->SubType == 1 && len >= 12) {   /* ACPI 장치: PNP0303 / PNP030B */
                uint32_t hid;
                memcpy(&hid, (uint8_t *)dp + 4, 4);
                if (hid == 0x030341D0u || hid == 0x030B41D0u) {
                    if (BS->DisconnectController(hs[i], 0, 0) == EFI_SUCCESS) ps2_detached = 1;
                }
            }
            if (len < 4) break;
            dp = (EFI_DEVICE_PATH *)((uint8_t *)dp + len);
        }
    }
    BS->FreePool(hs);
}

#include "../kernel/ps2aux.inc"

/* 3.0: 펌웨어 PS/2 마우스(터치패드) 드라이버 떼기 — 8042를 직접 읽으므로 */
static void detach_ps2_mouse(void)
{
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &SIMPLE_POINTER_GUID, 0, &n, &hs) != EFI_SUCCESS) return;
    for (UINTN i = 0; i < n; i++) {
        EFI_DEVICE_PATH *dp;
        if (BS->HandleProtocol(hs[i], &DEVICE_PATH_GUID, (void **)&dp) != EFI_SUCCESS) continue;
        for (int guard = 0; guard < 32 && !(dp->Type == 0x7F && dp->SubType == 0xFF); guard++) {
            uint16_t len = (uint16_t)(dp->Length[0] | (dp->Length[1] << 8));
            if (dp->Type == 2 && dp->SubType == 1 && len >= 12) {   /* ACPI PNP0F03 / PNP0F13 (PS/2 마우스) */
                uint32_t hid;
                memcpy(&hid, (uint8_t *)dp + 4, 4);
                if (hid == 0x0F0341D0u || hid == 0x0F1341D0u) BS->DisconnectController(hs[i], 0, 0);
            }
            if (len < 4) break;
            dp = (EFI_DEVICE_PATH *)((uint8_t *)dp + len);
        }
    }
    BS->FreePool(hs);
}

/* 3.9.2: UEFI 전용 BIOS 모드에서는 펌웨어 드라이버를 뗀 뒤 키보드가 꺼져(첫째 포트 클럭 꺼짐·스캔 중지·번역 꺼짐) 있는 PC가 있다.
 * 첫째 포트를 켜고 번역(세트 2→1)을 켜고, 키보드에 기본값(F6)과 스캔 시작(F4)을 보냄. 진단용으로 값을 남김 */
static int kb_cfg0 = -1, kb_cfg1 = -1, kb_a6 = -1, kb_a4 = -1;
static int kbd_cmd(uint8_t v)
{
    for (int tries = 0; tries < 3; tries++) {
        aux_wait_write(); outb(0x60, v);
        for (int i = 0; i < 60000; i++) {
            uint8_t st = inb(0x64);
            if (st == 0xFF) return -1;
            if (!(st & 1)) { (void)inb(0x80); continue; }
            uint8_t d = inb(0x60);
            if (st & 0x20) continue;
            if (d == 0xFA) return 0;
            if (d == 0xFE) break;                                   /* 다시 보내 달라고 함 */
        }
    }
    return -1;
}
static int kbc_cfg_read(void)
{
    aux_wait_write(); outb(0x64, 0x20);
    for (int i = 0; i < 60000; i++) { uint8_t st = inb(0x64); if (st & 1) { uint8_t v = inb(0x60); if (!(st & 0x20)) return v; } else (void)inb(0x80); }
    return -1;
}
static void kbd_revive(void)
{
    aux_wait_write(); outb(0x64, 0xAE);                             /* 키보드(첫째) 포트 켜기 */
    int cb = kbc_cfg_read();
    kb_cfg0 = cb;
    if (cb >= 0) {
        cb = (cb & ~0x10) | 0x40;                                   /* 첫째 클럭 켜기, 번역 켜기 */
        aux_wait_write(); outb(0x64, 0x60);
        aux_wait_write(); outb(0x60, (uint8_t)cb);
        kb_cfg1 = kbc_cfg_read();
    }
    kb_a6 = kbd_cmd(0xF6);                                          /* 기본값 (스캔 코드 세트 2) */
    kb_a4 = kbd_cmd(0xF4);                                          /* 스캔 시작 */
}

static void kbc_init(void)
{
    if (inb(0x64) == 0xFF) return;
    have_8042 = 1;
    for (int i = 0; i < 1000 && (inb(0x64) & 2); i++) delay_ms(0);
    outb(0x64, 0xA7);                                               /* 마우스 끄기 */
    for (int i = 0; i < 64 && (inb(0x64) & 1); i++) (void)inb(0x60);
    kbd_revive();                                                   /* 3.9.2 */
    detach_ps2_mouse();
    ps2_mouse_init();                                               /* 3.0: PS/2 마우스·터치패드가 있으면 켬 */
}

/* 8042 읽기를 펌웨어 타이머(2 ms, TPL_NOTIFY)로도 한다.
 * 한 프레임이 길어지면(USB 파일 읽기 등) 노트북 EC가 쌓아 둘 수 있는 양을 넘어 바이트를 버렸음 —
 * 방향키의 앞 바이트 E0를 잃어 숫자판 키로 바뀌고, 뗌 신호를 잃어 키가 계속 눌린 것으로 남았다 (KEY_X5.0.180 기록).
 * 타이머는 날 바이트를 kraw 고리에만 넣고(생산자 하나), 프레임 쪽이 꺼내 키 큐·마우스로 나눈다(소비자 하나). */
static volatile uint16_t kraw[512];
static volatile uint16_t kraw_head, kraw_tail;
static volatile uint32_t kraw_lost, kraw_ticks;
static void kbc_read_hw(void)                                       /* TPL_NOTIFY에서만 부름 (타이머, 또는 RaiseTPL 안) */
{
    for (int i = 0; i < 32; i++) {
        uint8_t st = inb(0x64);
        if (st == 0xFF || !(st & 1)) break;
        uint8_t v = inb(0x60);
        uint16_t h = kraw_head;
        if ((uint16_t)((h + 1) & 511) == kraw_tail) { kraw_lost++; continue; }
        kraw[h] = (uint16_t)(v | ((st & 0x20) ? 0x100 : 0));
        kraw_head = (uint16_t)((h + 1) & 511);
    }
}
static void kbc_tick(void *ev, void *ctx) { (void)ev; (void)ctx; kraw_ticks++; if (have_8042) kbc_read_hw(); }
static void kbc_timer_init(void)
{
    void *ev = 0;
    if (!have_8042 || !BS->CreateEvent) return;
    if (BS->CreateEvent(0x80000000u | 0x00000200u, 16, kbc_tick, 0, &ev) == EFI_SUCCESS && ev)   /* EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_NOTIFY */
        BS->SetTimer(ev, 1, 20000);                                 /* 2 ms마다 */
}
uint32_t plat_kbc_lost(void) { return kraw_lost; }
uint32_t plat_kbc_ticks(void) { return kraw_ticks; }
static void kbc_poll(void)
{
    if (!have_8042) return;
    UINTN old = BS->RaiseTPL(16);                                   /* 타이머와 같이 읽지 않게 */
    kbc_read_hw();
    BS->RestoreTPL(old);
    while (kraw_tail != kraw_head) {
        uint16_t w = kraw[kraw_tail];
        kraw_tail = (uint16_t)((kraw_tail + 1) & 511);
        if (w & 0x100) aux_byte((uint8_t)w);                        /* 3.0: 마우스 묶음 */
        else fe_push_key((uint8_t)w);
    }
}

/* PC 입력 드라이버 표 (구조개편 방식: 새 USB 입력 장치 = 드라이버 파일 하나 + 이 표 한 줄).
   stage: 1 = 부팅 때 먼저 찾기, 2 = i2c 터치패드 찾은 뒤. fast: 1 = 프레임 사이 빈 시간에도 부름. */
typedef struct { const char *name; int stage, fast; int (*init)(EFI_BOOT_SERVICES *); void (*poll)(void); int (*info)(char (*)[64], int); } InputDrv;
static int midi_init_shim(EFI_BOOT_SERVICES *bs) { midi_usb_init(bs); return 0; }
static const InputDrv input_drv[] = {
    { "kbd",   1, 1, kbd_usb_init,   kbd_usb_poll,   kbd_usb_info },
    { "pad",   1, 0, pad_usb_init,   pad_usb_poll,   pad_usb_info },
    { "midi",  1, 0, midi_init_shim, 0,              midi_usb_info },   /* 3.4.4 DEV-35: USB MIDI 입력 */
    { "mouse", 2, 0, mouse_usb_init, mouse_usb_poll, mouse_usb_info },
};
#define INPUT_N ((int)(sizeof input_drv / sizeof input_drv[0]))
static int input_init(EFI_BOOT_SERVICES *bs, int stage) { int r = 0; for (int i = 0; i < INPUT_N; i++) if (input_drv[i].stage == stage && input_drv[i].init) { int k = input_drv[i].init(bs); if (i == 0) r = k; } return r; }
static void input_poll(int fast_only) { for (int i = 0; i < INPUT_N; i++) if (input_drv[i].poll && (!fast_only || input_drv[i].fast)) input_drv[i].poll(); }
static int input_info(char (*lines)[64], int max) { int n = 0; for (int i = 0; i < INPUT_N; i++) if (input_drv[i].info && n < max) n += input_drv[i].info(lines + n, max - n); return n; }

static void plat_key_poll(void) { kbc_poll(); kbd_usb_poll(); intelhdmi_tick(plat_ms()); ivbhdmi_tick(plat_ms()); amdhdmi_tick(plat_ms()); }
void i2c_tp_init(void);
int i2c_tp_present(void);
int i2c_tp_off(void);
void i2c_tp_set_off(int off);
int i2c_tp_read(int *dx, int *dy, int *dz, int *btn);
/* 3.0: USB 마우스 + PS/2 마우스를 합침 */
static int plat_mouse_read(int *dx, int *dy, int *dz, int *buttons, char *name, int max)
{
    int ux, uy, uz, ub, px = 0, py = 0, pz = 0, pb = 0;
    char un[24] = "";
    int nu = mouse_usb_read(&ux, &uy, &uz, &ub, un, sizeof un);
    int np = have_8042 ? ps2_mouse_take(&px, &py, &pz, &pb, 0, 0) : 0;
    *dx = ux + px; *dy = uy + py; *dz = uz + pz; *buttons = ub | pb;
    int ni = i2c_tp_read(dx, dy, dz, buttons);                  /* 3.8.9: I2C 터치패드 */
    if (name && max > 0) {
        int n = 0;
        const char *t = un;
        while (*t && n < max - 1) name[n++] = *t++;
        if (np) { t = n ? " + PS/2" : "PS/2 mouse"; while (*t && n < max - 1) name[n++] = *t++; }
        if (ni) { t = n ? " + I2C" : "I2C touchpad"; while (*t && n < max - 1) name[n++] = *t++; }
        name[n] = 0;
    }
    return nu + np + ni;
}

static int plat_mouse_dev(int n, char *name, int max, int *on)
{
    int nu = mouse_usb_count();
    if (n < nu) return mouse_usb_dev(n, name, max, on);
    int k = n - nu;
    const char *t = 0;
    if (have_8042 && ps2_mouse_present()) { if (k == 0) { t = "PS/2 mouse / touchpad"; *on = !ps2_off; } k--; }
    if (!t && k == 0 && i2c_tp_present()) { t = "I2C touchpad"; *on = !i2c_tp_off(); }   /* 3.8.9 */
    if (!t) return 0;
    int i = 0;
    while (t[i] && i < max - 1) { name[i] = t[i]; i++; }
    name[i] = 0;
    return 1;
}
static void plat_mouse_set(int n, int on)
{
    int nu = mouse_usb_count();
    if (n < nu) { mouse_usb_set(n, on); return; }
    int k = n - nu;
    if (have_8042 && ps2_mouse_present()) { if (k == 0) { ps2_off = !on; return; } k--; }
    if (k == 0 && i2c_tp_present()) i2c_tp_set_off(!on);
}

/* ------------------------------------------------------------------ */
/* 기타                                                                 */
/* ------------------------------------------------------------------ */
/* PC 시계 (2.14): 펌웨어 GetTime
 * 3.5.5: GetTime은 기계에 따라 CMOS 갱신 대기·SMI로 몇 ms씩 걸려 1초마다 부르면 화면이 주기적으로 끊김.
 *        한 번 읽고 TSC로 흘러간 시간을 더함 (10분마다만 다시 읽음) */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}
static int64_t now_base_s;                          /* 펌웨어에서 읽은 시각 (1970년부터 초) */
static uint64_t now_base_tsc;
static int now_ok;
static int efi_now(int t[6])
{
    uint64_t tsc = rdtsc();
    if (!now_ok || tsc - now_base_tsc >= tsc_hz * 600) {
        EFI_TIME tm;
        EFI_STATUS (*get)(EFI_TIME *, void *) = (EFI_STATUS (*)(EFI_TIME *, void *))ST->RuntimeServices->GetTime;
        if (!get || get(&tm, 0) != EFI_SUCCESS || tm.Year < 1980 || !tm.Month || tm.Month > 12) {
            if (!now_ok) return -1;
            uint64_t el = (tsc - now_base_tsc) / tsc_hz;    /* 실패: TSC로 그대로 이어서 (10분 뒤 다시) */
            now_base_s += (int64_t)el; now_base_tsc += el * tsc_hz;
        } else {
            now_base_s = days_from_civil(tm.Year, tm.Month, tm.Day) * 86400 + tm.Hour * 3600 + tm.Minute * 60 + tm.Second;
            now_base_tsc = tsc = rdtsc();
            now_ok = 1;
        }
    }
    int64_t s = now_base_s + (int64_t)((tsc - now_base_tsc) / tsc_hz);
    int64_t days = s / 86400, r = s % 86400;
    civil_from_days(days, &t[0], &t[1], &t[2]);
    t[3] = (int)(r / 3600); t[4] = (int)(r / 60 % 60); t[5] = (int)(r % 60);
    return 0;
}

static void plat_reboot(void)
{
    hda_silence();                                  /* 3.8.1: 다시 켜지는 동안 마지막 소리가 남지 않게 */
    ST->RuntimeServices->ResetSystem(0, EFI_SUCCESS, 0, 0);
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);
    for (;;) __asm__ volatile("hlt");
}

static void plat_audio(const int16_t *s, int n, int volume, int mute) { hda_write(s, n, volume, mute); }
static void plat_audio2(const int16_t *l, const int16_t *r, int n, int volume, int mute) { hda_write2(l, r, n, volume, mute); }

/* 4.0.34: 화면 출력 고르기. GOP 핸들(그래픽 출력)이 여러 개면 목록에 올리고, x86msx.ini의 VideoOut=번호로 고른 것을 쓴다 (바꾸면 다시 시작해야 적용) */
#define VO_MAX 8
static int g_vo_n, g_vo_act;
static char g_vo_txt[VO_MAX][64];
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_vo_gop[VO_MAX];
static EFI_HANDLE g_vo_h[VO_MAX];
typedef struct VO_PCI {
    void *PollMem, *PollIo, *MemRead, *MemWrite, *IoRead, *IoWrite;
    EFI_STATUS (*PciRead)(struct VO_PCI *, int width, uint32_t off, UINTN count, void *buf);
    void *PciWrite, *CopyMem, *Map, *Unmap, *AllocateBuffer, *FreeBuffer, *Flush;
    EFI_STATUS (*GetLocation)(struct VO_PCI *, UINTN *seg, UINTN *bus, UINTN *dev, UINTN *fn);
} VO_PCI;
static void vo_num(char *d, int max, unsigned v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 11); int l = 0; while (d[l]) l++; while (n && l < max - 1) d[l++] = t[--n]; d[l] = 0; }
static void vo_cat(char *d, int max, const char *a) { int l = 0; while (d[l]) l++; while (*a && l < max - 1) d[l++] = *a++; d[l] = 0; }
static void vo_hex(char *d, int max, unsigned v) { int l = 0; while (d[l]) l++; for (int i = 3; i >= 0 && l < max - 1; i--) d[l++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15]; d[l] = 0; }
static int k_first_edid;
static uint32_t g_mm_w, g_mm_h;                         /* EDID 화면 크기 mm */
static void vo_edid_name(EFI_HANDLE h, char *d, int max)   /* 4.0.34: 모니터 이름 (EDID: 이름 블록, 없으면 제조사 3글자+제품 번호) */
{
    static const EFI_GUID ED_G = { 0x1c0c34f6, 0xd380, 0x41fa, { 0xa0, 0x49, 0x8a, 0xd0, 0x6c, 0x1a, 0x66, 0xaa } };
    static const EFI_GUID EA_G = { 0xbd8c1056, 0x9f36, 0x44ec, { 0x92, 0xa8, 0xa6, 0x33, 0x7f, 0x81, 0x79, 0x86 } };
    struct { uint32_t size; uint8_t *edid; } *ed = 0;
    if (BS->HandleProtocol(h, (EFI_GUID *)&EA_G, (void **)&ed) != EFI_SUCCESS || !ed || !ed->edid || ed->size < 128) {
        ed = 0;
        if (k_first_edid && BS->LocateProtocol((EFI_GUID *)&EA_G, 0, (void **)&ed) == EFI_SUCCESS && ed && ed->edid && ed->size >= 128) goto have;
        ed = 0;
        if (BS->HandleProtocol(h, (EFI_GUID *)&ED_G, (void **)&ed) != EFI_SUCCESS || !ed || !ed->edid || ed->size < 128) { vo_cat(d, max, " - no monitor info"); return; }
    }
have:;
    const uint8_t *e = ed->edid;
    char nm[16]; int n = 0;
    for (int b = 0x36; b <= 0x6C && !n; b += 18)
        if (e[b] == 0 && e[b + 1] == 0 && e[b + 3] == 0xFC)
            for (int i = 0; i < 13 && e[b + 5 + i] != 0x0A && e[b + 5 + i] >= 32; i++) nm[n++] = (char)e[b + 5 + i];
    if (!n) {
        unsigned m = ((unsigned)e[8] << 8) | e[9];
        nm[n++] = (char)('@' + ((m >> 10) & 31)); nm[n++] = (char)('@' + ((m >> 5) & 31)); nm[n++] = (char)('@' + (m & 31));
    }
    nm[n] = 0;
    vo_cat(d, max, " - "); vo_cat(d, max, nm);
}
static int vo_ini_pref(void)
{
    static uint8_t ib[8192];
    uint32_t n = 0;
    if (plat_read("0:\\x86msx.ini", ib, sizeof ib - 1, &n) || !n) return 0;
    ib[n] = 0;
    for (uint32_t i = 0; i + 9 < n; i++)
        if ((i == 0 || ib[i - 1] == '\n') && !memcmp(ib + i, "VideoOut=", 9)) { int v = 0; for (uint32_t j = i + 9; j < n && ib[j] >= '0' && ib[j] <= '9'; j++) v = v * 10 + (ib[j] - '0'); return v; }
    return 0;
}
static int g_boot_failed;
static void blog(const char *tag)   /* 4.0.34: 켜기 단계 기록 (까만 화면에서 멈추면 States\\boot.log 마지막 줄이 멈춘 곳) */
{
    static char lb[768]; static int ln; static uint64_t t0;
    if (!t0) t0 = rdtsc();
    uint64_t ms = tsc_hz ? (rdtsc() - t0) / (tsc_hz / 1000) : 0;
    char t[24]; int n = 0; uint64_t v = ms; char r[20]; int k = 0; do { r[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) t[n++] = r[--k]; t[n++] = ' '; t[n] = 0;
    for (const char *c = t; *c && ln < (int)sizeof lb - 2; c++) lb[ln++] = *c;
    for (const char *c = tag; *c && ln < (int)sizeof lb - 2; c++) lb[ln++] = *c;
    lb[ln++] = '\n';
    plat_create("0:\\States", 1);
    plat_write("0:\\States\\boot.log", (const uint8_t *)lb, (uint32_t)ln);
}
/* 4.0.59: 화면 출력 목록에 올라가는 "HDMI 미러" 항목. PC마다 하나씩 시험해 통과하면 이 표에 한 줄씩 추가한다 (지금: Intel Gen9) */
static void log_dir(void);
static void hang_nv_move(void);
static EFI_GRAPHICS_OUTPUT_PROTOCOL *g_vo_first;
static uint64_t g_vo_first_fb(void) { return g_vo_first && g_vo_first->Mode ? (uint64_t)g_vo_first->Mode->FrameBufferBase : 0; }
static void ih_stall(uint32_t us) { BS->Stall(us); }
static void ih_logw(const char *path, const uint8_t *d, uint32_t n) { log_dir(); plat_create(path, 0); plat_write(path, d, n); }
static int ih_ready(void) { static int i; if (!i) { i = 1; ivbhdmi_init(ih_stall, ih_logw); amdhdmi_init(ST, ih_stall, ih_logw); intelhdmi_init(ih_stall, ih_logw, g_gop ? (uint64_t)g_gop->Mode->FrameBufferBase : g_vo_first_fb()); } return 1; }
typedef struct { const char *name; int (*avail)(void); int (*set)(int mode); int maxmode; int (*lost)(void); int (*afresh)(void); } HdmiDrv;
/* 공용 로그 규약 DRV.* — 영상 모듈이 불러와졌나·켜졌나를 LOG\DRV_X<버전>.TXT에 남김 (동작은 그대로) */
static char drv_buf[1400]; static int drv_n;
static void drv_flush(void) { log_dir(); plat_create("0:\\LOG\\DRV_X" MSXEMU_VERSION ".TXT", 0); plat_write("0:\\LOG\\DRV_X" MSXEMU_VERSION ".TXT", (const uint8_t *)drv_buf, (uint32_t)drv_n); }
static void drv_s(const char *s) { while (*s && drv_n < (int)sizeof drv_buf - 1) drv_buf[drv_n++] = *s++; drv_buf[drv_n] = 0; }
static void drv_note(const char *what, const char *name, int v)
{
    if (!drv_n) drv_s("#LOG 1\r\n#prog=X86MSX ver=" MSXEMU_VERSION "\r\n");
    drv_s("DRV."); drv_s(what); drv_s(" name="); drv_s(name);
    drv_s(v ? " res=ok" : " res=skip why=not_present_or_off"); if (!v && what[0] == 's') drv_s("\r\nDRV.fallback to=default"); drv_s("\r\n");
    drv_flush();
}
static int hd_intel_avail(void) { static int d; ih_ready(); int r = intelhdmi_avail(); if (!d) { d = 1; drv_note("load", "intel_hdmi", r); } return r; }
static int hd_ivb_avail(void) { static int d; ih_ready(); int r = ivbhdmi_avail(); if (!d) { d = 1; drv_note("load", "ivb_hdmi", r); } return r; }
static int hd_amd_avail(void) { static int sl, d; if (!sl) { static uint8_t lk[8]; uint32_t ln = 0; sl = 1; amddisp_slow(!plat_read("0:\\AMDSLOW.OK", lk, sizeof lk, &ln)); } ih_ready(); int r = amdhdmi_avail(); if (!d) { d = 1; drv_note("load", "amd_hdmi", r); } return r; }   /* 4.0.116: USB 루트에 AMDSLOW.OK 가 있으면 긴 대기(v53) */
static int hd_intel_set(int m) { int r = intelhdmi_set(m); if (m) drv_note("set", "intel_hdmi", r); return r; }
static int hd_amd_set(int m) { int r = amdhdmi_set(m); if (m) drv_note("set", "amd_hdmi", r); return r; }
static int hd_ivb_set(int m) { int r = ivbhdmi_set(m); if (m) drv_note("set", "ivb_hdmi", r); return r; }
static const HdmiDrv hdmi_drv[] = { { "Intel", hd_intel_avail, hd_intel_set, 2, intelhdmi_lost, intelhdmi_audio_fresh }, { "AMD", hd_amd_avail, hd_amd_set, 2, amdhdmi_lost, amdhdmi_audio_fresh }, { "Intel", hd_ivb_avail, hd_ivb_set, 2, ivbhdmi_lost, ivbhdmi_audio_fresh } };   /* 4.0.117: AMD Renoir(Lenovo V14-ARE)도 복사 / HDMI만(내장 패널 백라이트 끔) 2종 */
#define HD_N ((int)(sizeof hdmi_drv / sizeof hdmi_drv[0]))
static int g_vo_hd[VO_MAX];                 /* 이 출력 항목이 HDMI 미러면 hdmi_drv 번호 + 1, 아니면 0 */
static int g_vo_hm[VO_MAX];                 /* HDMI 항목의 모드: 1 = 복사(내부+HDMI), 2 = HDMI만 */
static int g_hd_on = -1;                    /* 지금 미러를 켜 둔 hdmi_drv 번호 (없으면 -1) */
static int vo_has_nomon(const char *t) { const char *k = "no monitor info"; for (; *t; t++) { int i = 0; while (k[i] && t[i] == k[i]) i++; if (!k[i]) return 1; } return 0; }
static void plat_boot_ok(void) { plat_delete("0:\\States\\boot.flg"); }
static void vo_scan(void)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *first = g_vo_first;
    EFI_HANDLE *hs = 0; UINTN hn = 0;
    static const EFI_GUID PCI_G = { 0x4cf5b200, 0x68b8, 0x4ca5, { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } };
    g_vo_n = 0; g_vo_gop[g_vo_n++] = first;
    for (int q = 0; q < VO_MAX; q++) g_vo_hd[q] = g_vo_hm[q] = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &GOP_GUID, 0, &hn, &hs) == EFI_SUCCESS && hs)
        for (UINTN i = 0; i < hn && g_vo_n < VO_MAX; i++) {
            EFI_GRAPHICS_OUTPUT_PROTOCOL *g = 0;
            if (BS->HandleProtocol(hs[i], &GOP_GUID, (void **)&g) != EFI_SUCCESS || !g || !g->Mode || g == first) continue;
            { int dup = 0; for (int q = 0; q < g_vo_n; q++) if (g_vo_gop[q] == g || g_vo_gop[q]->Mode->FrameBufferBase == g->Mode->FrameBufferBase) dup = 1; if (dup) continue; }   /* 같은 화면 메모리 = 같은 출력 (펌웨어가 핸들을 둘 만든 것) */
            g_vo_gop[g_vo_n++] = g;
        }
    for (int k = 0; k < g_vo_n; k++) {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *g = g_vo_gop[k];
        char *t = g_vo_txt[k]; t[0] = 0; g_vo_h[k] = 0;
        vo_num(t, 64, (unsigned)k + 1); vo_cat(t, 64, ": ");
        if (g->Mode->Info) { vo_num(t, 64, g->Mode->Info->HorizontalResolution); vo_cat(t, 64, "x"); vo_num(t, 64, g->Mode->Info->VerticalResolution); }
        vo_cat(t, 64, " ");
        {   /* 같은 핸들의 PCI 장치 번호 (제조사:장치) */
            for (UINTN i = 0; i < hn && hs; i++) {
                EFI_GRAPHICS_OUTPUT_PROTOCOL *g2 = 0; VO_PCI *io = 0;
                if (BS->HandleProtocol(hs[i], &GOP_GUID, (void **)&g2) != EFI_SUCCESS || g2 != g) continue;
                g_vo_h[k] = hs[i];
                if (BS->HandleProtocol(hs[i], &PCI_G, (void **)&io) == EFI_SUCCESS && io && io->PciRead) {
                    uint16_t id[2] = { 0, 0 };
                    if (io->PciRead(io, 1, 0, 2, id) == EFI_SUCCESS) { vo_cat(t, 64, id[0] == 0x10DE ? "NVIDIA " : id[0] == 0x1002 ? "AMD " : id[0] == 0x8086 ? "Intel " : "GPU "); vo_hex(t, 64, id[1]); }
                }
                k_first_edid = (k == 0);
                vo_edid_name(hs[i], t, 64);
                break;
            }
        }
    }
    if (hs) BS->FreePool(hs);
    for (int q = 0; q < HD_N && g_vo_n + 1 < VO_MAX; q++) {       /* 4.0.64: HDMI 항목 2개 — 복사(내부+HDMI) / HDMI만 */
        int id = hdmi_drv[q].avail();
        if (!id) continue;
        for (int md = 1; md <= hdmi_drv[q].maxmode; md++) {
            int k = g_vo_n++;
            g_vo_gop[k] = first; g_vo_h[k] = 0; g_vo_hd[k] = q + 1; g_vo_hm[k] = md;
            char *t = g_vo_txt[k]; t[0] = 0;
            vo_num(t, 64, (unsigned)k + 1); vo_cat(t, 64, ": HDMI "); vo_cat(t, 64, hdmi_drv[q].name); vo_cat(t, 64, " "); vo_hex(t, 64, (unsigned)id);
            vo_cat(t, 64, md == 1 ? " copy: internal+HDMI" : " only: HDMI (internal off)");
        }
    }
    /* 4.0.128: 순서 고정 — 1 = 내부, 2 = 내부+HDMI(복사), 3 = HDMI만, 그 뒤에 다른 GOP 출력. (Intel·AMD 모두 같게) */
    {
        int nh = 0; for (int k = 1; k < g_vo_n; k++) if (g_vo_hd[k]) nh++;
        if (nh) {
            EFI_GRAPHICS_OUTPUT_PROTOCOL *og[VO_MAX]; EFI_HANDLE oh[VO_MAX]; int ohd[VO_MAX], ohm[VO_MAX]; char ot[VO_MAX][64];
            int n = g_vo_n, w = 0;
            for (int k = 0; k < n; k++) { og[k] = g_vo_gop[k]; oh[k] = g_vo_h[k]; ohd[k] = g_vo_hd[k]; ohm[k] = g_vo_hm[k]; for (int j = 0; j < 64; j++) ot[k][j] = g_vo_txt[k][j]; }
            for (int pass = 0; pass < 3; pass++)
                for (int k = 0; k < n; k++) {
                    int pick = pass == 0 ? k == 0 : pass == 1 ? ohd[k] != 0 : (k != 0 && !ohd[k]);
                    if (!pick) continue;
                    g_vo_gop[w] = og[k]; g_vo_h[w] = oh[k]; g_vo_hd[w] = ohd[k]; g_vo_hm[w] = ohm[k];
                    { char *t = g_vo_txt[w]; int q = 0; while (ot[k][q] && ot[k][q] != ':') q++;
                      t[0] = 0; vo_num(t, 64, (unsigned)w + 1); vo_cat(t, 64, ot[k] + q); }
                    w++;
                }
        }
    }
}
static EFI_GRAPHICS_OUTPUT_PROTOCOL *vo_pick(EFI_GRAPHICS_OUTPUT_PROTOCOL *first)
{
    g_vo_first = first;
    { static int i0; if (!i0) { i0 = 1; ivbhdmi_init(ih_stall, ih_logw); intelhdmi_init(ih_stall, ih_logw, (uint64_t)first->Mode->FrameBufferBase); } }
    vo_scan();
    /* 4.0.34: 켜기 표시 파일 — 지난번 켜다 멈췄으면(표시가 남음) 저장된 화면 출력·이어하기 상태를 이번 한 번 건너뜀 */
    { static uint8_t fb[4]; uint32_t fn = 0; g_boot_failed = !plat_read("0:\\States\\boot.flg", fb, sizeof fb, &fn);
      if (!g_boot_failed) { plat_create("0:\\States", 1); plat_write("0:\\States\\boot.flg", (const uint8_t *)"1", 1); } }
    int want = 0; (void)vo_ini_pref;            /* 4.0.71: 화면 출력은 저장값과 상관없이 항상 기본(내부 모니터)으로 시작 */
    if (want > 0 && want < g_vo_n && vo_has_nomon(g_vo_txt[want])) want = 0;   /* 모니터가 안 꽂힌 출력이면 첫째로 */
    if (want < 0 || want >= g_vo_n) want = 0;
    g_vo_act = want;
    if (g_vo_hd[want]) { int q = g_vo_hd[want] - 1; if (hdmi_drv[q].set(g_vo_hm[want])) g_hd_on = q; }   /* 4.0.59: 저장된 선택이 HDMI 미러면 켜는 중에 같이 켬 (켜다 멈추면 boot.flg로 다음 번은 건너뜀) */
    return g_vo_gop[want];
}
static int g_hd_lostflag, g_ap, g_want_hda, g_hd_only;   /* g_hd_only: 4.0.127 'HDMI만'(내부 모니터 끔) 항목 — 소리 '자동'도 HDMI */
/* 무선랜 백그라운드 실행 (두 번째 코어). 메인 코어는 PCI 설정 접근(begin/end)과 로그 저장만 한다. */
typedef struct EFI_MP_SERVICES {
    EFI_STATUS (*GetNumberOfProcessors)(struct EFI_MP_SERVICES *, UINTN *, UINTN *);
    EFI_STATUS (*GetProcessorInfo)(struct EFI_MP_SERVICES *, UINTN, void *);
    EFI_STATUS (*StartupAllAPs)(struct EFI_MP_SERVICES *, void (*)(void *), uint8_t, void *, UINTN, void *, void **);
    EFI_STATUS (*StartupThisAP)(struct EFI_MP_SERVICES *, void (*)(void *), UINTN, void *, UINTN, void *, uint8_t *);
    EFI_STATUS (*SwitchBSP)(struct EFI_MP_SERVICES *, UINTN, uint8_t);
    EFI_STATUS (*EnableDisableAP)(struct EFI_MP_SERVICES *, UINTN, uint8_t, uint32_t *);
    EFI_STATUS (*WhoAmI)(struct EFI_MP_SERVICES *, UINTN *);
} EFI_MP_SERVICES;
static const EFI_GUID MP_SERVICES_GUID = { 0x3fdda605, 0xa76e, 0x4f46, { 0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3b, 0x08 } };
static char g_rtw_wb[40000]; static int g_rtw_wn;
static volatile int g_rtw_state, g_rtw_done; static uint8_t g_rtw_fin;
static void rtw_ap_main(void *arg) { (void)arg; rtwdet_work(); g_rtw_done = 1; }
static void rtw_log_write(void)
{
    log_dir(); plat_create("0:\\LOG\\WIFI_X" MSXEMU_VERSION ".TXT", 0);
    if (plat_write("0:\\LOG\\WIFI_X" MSXEMU_VERSION ".TXT", (const uint8_t *)g_rtw_wb, (uint32_t)g_rtw_wn) < 0) { plat_create("0:\\LOG\\WIFI.TXT", 0); plat_write("0:\\LOG\\WIFI.TXT", (const uint8_t *)g_rtw_wb, (uint32_t)g_rtw_wn); }
}
static int g_lwip_kind;                                 /* 지금 lwIP 가 쓰는 랜카드 종류 (0 = 안 씀, NK_WIRED, NK_WIFI) */
static int g_wifi_cfg, g_host_dirty, g_wifi_started, g_rtw_hdr, g_ini_rn = -1, g_ini_sl, g_ini_pl;   /* 실시간 무선 (RTW.INI POWER=1, LIVE=0 이 아닐 때) */
static void net_log_write(const char *b, int n)
{
    log_dir(); plat_create("0:\\LOG\\NET_X" MSXEMU_VERSION ".TXT", 0);
    if (plat_write("0:\\LOG\\NET_X" MSXEMU_VERSION ".TXT", (const uint8_t *)b, (uint32_t)n) < 0) { plat_create("0:\\LOG\\NET.TXT", 0); plat_write("0:\\LOG\\NET.TXT", (const uint8_t *)b, (uint32_t)n); }
}
static void rtw_bg_poll(void)
{
    if (g_rtw_state == 1 && g_rtw_done) { g_rtw_state = 2; g_rtw_wn += rtwdet_end(); rtw_log_write(); }
    if (g_wifi_cfg || g_lwip_kind) {   /* 무선 기록(WIFI_X*.TXT)과 lwIP 기록(NET_X*.TXT)을 3초마다, 바뀌었을 때만 USB에 씀 */
        static uint32_t next; static int last_r = -1, last_n = -1; uint32_t now = plat_ms();
        if ((int32_t)(now - next) >= 0) {
            next = now + 3000;
            int rl = g_wifi_cfg ? rtwdet_loglen() : 0;
            if (g_wifi_cfg && rl != last_r) { last_r = rl; g_rtw_wn = g_rtw_hdr + rl; rtw_log_write(); }
            if (g_wifi_started || g_lwip_kind) {
                static char nb[14000]; int nl = net_log_take(nb, (int)sizeof nb);
                if (nl != last_n) { last_n = nl; net_log_write(nb, nl); }
            }
        }
    }
}

static int plat_video_lost(void)       /* 4.0.74: HDMI를 뽑았으면 HDMI를 끄고 내부 모니터로 (fe가 화면·소리 선택도 기본으로 되돌림) */
{
    { static int bl; if (!bl && plat_ms() > 2500) { bl = 1;   /* 켤 때마다 한 번 무조건 남기는 부팅 로그 (로그가 하나도 없는 상태를 없애려고) */
        char b[900]; int n = 0; uint32_t g = vs_pci_rd(0, 2, 0, 0); { uint32_t sg; if (pcdb_scan((uint32_t (*)(int, int, int, int))vs_pci_rd, &sg)) g = sg; }
        const char *a = "#LOG 1\r\n#prog=X86MSX ver=" MSXEMU_VERSION " build=" MSXEMU_BUILD "\r\n#boot=uefi gpu=";
        while (*a) b[n++] = *a++;
        for (int i = 28; i >= 0; i -= 4) b[n++] = "0123456789ABCDEF"[(g >> i) & 15];
        { const PcEntry *pe = pcdb_find(g); const char *e = pe ? "\r\n#pc=" : "\r\n#pc=?"; while (*e) b[n++] = *e++;
          if (pe) { b[n++] = (char)('0' + pe->pc); const char *q = " name=\""; while (*q) b[n++] = *q++; for (q = pe->name; *q && n < 380; ) b[n++] = *q++; b[n++] = '"'; } }
        const char *e = "\r\nBOOT.start res=ok\r\n"; while (*e) b[n++] = *e++;
        n = pcdb_dump_class((uint32_t (*)(int, int, int, int))vs_pci_rd, 0x02, "NET", b, n, (int)sizeof b);
        hang_nv_move();                                  /* 지난번 멈춤 기록 (UEFI 변수) → LOG\\LASTHANG.TXT */
        log_dir(); plat_create("0:\\LOG\\BOOT_X" MSXEMU_VERSION ".TXT", 0);
        if (plat_write("0:\\LOG\\BOOT_X" MSXEMU_VERSION ".TXT", (const uint8_t *)b, (uint32_t)n) < 0) {   /* 긴 이름이 안 되는 펌웨어를 위해 8.3 이름과 USB 루트로 차례로 다시 */
            plat_create("0:\\LOG\\BOOT.TXT", 0);
            if (plat_write("0:\\LOG\\BOOT.TXT", (const uint8_t *)b, (uint32_t)n) < 0) { plat_create("0:\\BOOT.TXT", 0); plat_write("0:\\BOOT.TXT", (const uint8_t *)b, (uint32_t)n); } } } }
    { static int rw; if (!rw && plat_ms() > 5000) { rw = 1;   /* 무선랜 (칩 전원 켜기 → 펌웨어 → PHY/RF → 채널 → 수신·송신 시험). USB의 DRV\\RTW.INI 에 POWER=1 이 있을 때만 (없으면 아무것도 안 함) */
        static uint8_t ri[2048]; uint32_t rn = 0;
        if (!plat_read("0:\\DRV\\RTW.INI", ri, sizeof ri - 1, &rn) && rn) {
            ri[rn < sizeof ri ? rn : sizeof ri - 1] = 0; int on = 0, dlf = 0, core0 = 0;
            for (uint32_t i = 0; i + 7 <= rn; i++) if (!__builtin_memcmp(ri + i, "POWER=1", 7)) on = 1;
            for (uint32_t i = 0; i + 6 <= rn; i++) if (!__builtin_memcmp(ri + i, "FWDL=1", 6)) dlf = 1;
            int live = 1; for (uint32_t i = 0; i + 6 <= rn; i++) if (!__builtin_memcmp(ri + i, "LIVE=0", 6) && (i == 0 || ri[i - 1] == 10 || ri[i - 1] == 13)) live = 0;   /* 기본은 실시간(lwIP). LIVE=0 이면 예전 진단 흐름(DHCP·ping 시험 후 끝) */
            for (uint32_t i = 0; i + 6 <= rn; i++) if (!__builtin_memcmp(ri + i, "CORE=0", 6) && (i == 0 || ri[i - 1] == 10 || ri[i - 1] == 13)) core0 = 1;   /* 줄 맨 앞일 때만 (주석 ;CORE=0 에 걸리던 오류 수정) */   /* CORE=0 이면 두 번째 코어를 쓰지 않고 이 코어에서 막고 실행(비교 시험용) */
            { /* 접속할 공유기 (SSID= / PASS=, 줄 처음부터 줄 끝까지). 비밀번호는 USB의 이 파일에만 둔다 */
                static char ss[40], pw[72]; ss[0] = pw[0] = 0;
                for (uint32_t i = 0; i < rn; ) {
                    uint32_t e = i; while (e < rn && ri[e] != '\r' && ri[e] != '\n') e++;
                    if (e - i > 5 && !__builtin_memcmp(ri + i, "SSID=", 5) && e - i - 5 < sizeof ss) { uint32_t n = e - i - 5; for (uint32_t j = 0; j < n; j++) ss[j] = (char)ri[i + 5 + j]; ss[n] = 0; }
                    if (e - i > 5 && !__builtin_memcmp(ri + i, "PASS=", 5) && e - i - 5 < sizeof pw) { uint32_t n = e - i - 5; for (uint32_t j = 0; j < n; j++) pw[j] = (char)ri[i + 5 + j]; pw[n] = 0; }
                    while (e < rn && (ri[e] == '\r' || ri[e] == '\n')) e++;
                    i = e;
                }
                if (!live) rtwdet_setcred(ss, pw);   /* 실시간 모드는 기본 공유기·암호가 없다 (네트워크 창에서 고르고 접속에 성공하면 저장) */
                { int sl_ = 0, pl_ = 0; while (ss[sl_]) sl_++; while (pw[pl_]) pl_++; g_ini_rn = (int)rn; g_ini_sl = sl_; g_ini_pl = pl_; }   /* 비밀번호는 길이만 기록 */
            }
            if (on) {
                g_rtw_wn = 0;
                const char *a = "#LOG 1\r\n#prog=X86MSX ver=" MSXEMU_VERSION " build=" MSXEMU_BUILD "\r\n"; while (*a) g_rtw_wb[g_rtw_wn++] = *a++;
                static uint8_t fwb[262144]; uint32_t fwn = 0;   /* DRV\\rtw8822c_fw.bin (없으면 검사만 건너뜀) */
                if (plat_read("0:\\DRV\\rtw8822c_fw.bin", fwb, sizeof fwb, &fwn)) fwn = 0;
                static uint8_t phb[131072]; uint32_t phn = 0;   /* DRV\\rtw8822c_phy.bin */
                if (plat_read("0:\\DRV\\rtw8822c_phy.bin", phb, sizeof phb, &phn)) phn = 0;
                EFI_PHYSICAL_ADDRESS wd = 0xFFFFFFFFu; void *wdp = 0;   /* 4GB 아래 DMA 쪽 */
                if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 140, &wd) == EFI_SUCCESS) wdp = (void *)(uintptr_t)wd;
                g_rtw_hdr = g_rtw_wn;
                if (rtwdet_begin(g_rtw_wb + g_rtw_wn, (int)sizeof g_rtw_wb - g_rtw_wn, fwn ? fwb : 0, fwn, wdp, wdp ? 140u * 4096u : 0, dlf, phn ? phb : 0, phn)) {
                  if (live && wdp && dlf && fwn) {   /* 실시간 — 칩 초기화·접속은 네트워크 카트리지가 꽂혀 MSX 가 돌 때 프레임마다 조금씩 (uefi_net_poll) */
                    rtwdet_live_enable(plat_ms);
                    g_wifi_cfg = 1; msx_net_host = nethost_get(); g_host_dirty = 1;
                    rtwdet_log("WIFI.live res=armed note=starts_when_network_cartridge_is_inserted\r\n");
                    { char t[80]; int n = 0; const char *k = "WIFI.ini size="; while (*k) t[n++] = *k++;
                      int v[3] = { g_ini_rn, g_ini_sl, g_ini_pl }; const char *nm[3] = { "", " ssid_len=", " pass_len=" };
                      for (int q = 0; q < 3; q++) { const char *m = nm[q]; while (*m) t[n++] = *m++; char r[12]; int rn2 = 0; int x = v[q]; if (x <= 0) r[rn2++] = '0'; while (x > 0) { r[rn2++] = (char)('0' + x % 10); x /= 10; } while (rn2) t[n++] = r[--rn2]; }
                      t[n++] = '\r'; t[n++] = '\n'; t[n] = 0; rtwdet_log(t); }
                    g_rtw_wn = g_rtw_hdr + rtwdet_loglen(); rtw_log_write();
                  } else {
                    int started = 0;
                    if (!core0) {   /* 두 번째 코어에서 실행 → 에뮬레이터(MSX)는 멈추지 않음 */
                        EFI_MP_SERVICES *mp = 0;
                        if (BS->LocateProtocol(&MP_SERVICES_GUID, 0, (void **)&mp) == EFI_SUCCESS && mp) {
                            UINTN tot = 0, en = 0, me = 0; mp->GetNumberOfProcessors(mp, &tot, &en); mp->WhoAmI(mp, &me);
                            char t[64]; int tn = 0; const char *p0 = "WIFI.core cpus="; while (*p0) t[tn++] = *p0++; t[tn++] = (char)('0' + (tot % 10)); const char *p1 = " enabled="; while (*p1) t[tn++] = *p1++; t[tn++] = (char)('0' + (en % 10)); const char *p2 = " bsp="; while (*p2) t[tn++] = *p2++; t[tn++] = (char)('0' + (me % 10)); t[tn] = 0;
                            rtwdet_log(t);
                            void *ev = 0; BS->CreateEvent(0, 0, 0, 0, &ev);
                            for (UINTN i = 0; i < tot && !started; i++) {
                                if (i == me) continue;
                                EFI_STATUS r = mp->StartupThisAP(mp, rtw_ap_main, i, ev, 0, 0, &g_rtw_fin);
                                if (r == EFI_SUCCESS) { started = 1; rtwdet_log(" ap="); char u[2] = { (char)('0' + (i % 10)), 0 }; rtwdet_log(u); rtwdet_log(" res=started\r\n"); }
                            }
                            if (!started) rtwdet_log(" res=no_ap_started\r\n");
                        } else rtwdet_log("WIFI.core res=no_mp_services\r\n");
                    } else rtwdet_log("WIFI.core res=off(CORE=0)\r\n");
                    if (started) { g_rtw_wn += 0; g_rtw_state = 1; }
                    else { rtwdet_log("WIFI.core res=inline note=MSX_stops_during_run\r\n"); rtwdet_work(); g_rtw_state = 1; g_rtw_done = 1; }
                  }
                } else { g_rtw_wn += rtwdet_loglen(); rtw_log_write(); }
            } } } }
    rtw_bg_poll();
    intelhdmi_tick(plat_ms());
    if (g_hd_on >= 0 && hdmi_drv[g_hd_on].lost()) {
        hdmi_drv[g_hd_on].set(0); g_hd_on = -1; g_vo_act = 0; g_hd_lostflag = 1; g_want_hda = 0; g_hd_only = 0;
        present_rows(0, g_h);                           /* 4.0.90: 내장 화면을 처음부터 다시 그림 (부팅 전 화면이 남지 않게) */
    }
    if (g_hd_on >= 0 && hdmi_drv[g_hd_on].afresh() && (g_ap == 2 || g_want_hda)) {   /* 4.0.83: 그래픽 소리가 켜지면 HDMI 소리 핀을 켬 (비디오 HDMI 선택 시 자동) */
        char st[96]; int w = g_want_hda; g_want_hda = 0;
        if (hda_select(2, st, sizeof st)) { hda_select(0, st, sizeof st); g_ap = 0; return 2; }
        g_ap = 2; if (w) return 3;
    }
    if (g_hd_lostflag) { g_hd_lostflag = 0; return 1; }
    return 0;
}
static int plat_video_switch(int idx)   /* 4.0.34: 재시작 없이 다른 모니터로. 같은 해상도 모드가 있을 때만 (0 = 바꿈, -1 = 재시작 필요) */
{
    if (idx < 0 || idx >= g_vo_n || !g_back || !g_gop) return -1;
    if (g_hd_on >= 0 && !g_vo_hd[idx]) { hdmi_drv[g_hd_on].set(0); g_hd_on = -1; g_want_hda = 0; g_hd_only = 0; present_rows(0, g_h); }          /* 4.0.59: 다른 출력을 고르면 HDMI 미러는 끔 */
    if (g_vo_hd[idx]) {                                                                         /* HDMI 미러 항목: 내장 패널 화면을 그대로 HDMI에도 */
        int q = g_vo_hd[idx] - 1;
        if (!hdmi_drv[q].set(g_vo_hm[idx])) return -1;
        g_hd_on = q;
        g_vo_act = idx;
        g_hd_only = g_vo_hm[idx] == 2;
        g_want_hda = g_hd_only;                            /* 4.0.127: 복사(1) = 내장 소리 그대로, HDMI만(2) = HDMI 소리 */
        return 0;
    }
    EFI_GRAPHICS_OUTPUT_PROTOCOL *n = g_vo_gop[idx];
    if (n == g_gop) { g_vo_act = idx; return 0; }
    uint32_t pf_old = g_gop->Mode->Info->PixelFormat, found = ~0u;
    for (uint32_t i = 0; i < n->Mode->MaxMode && found == ~0u; i++) {
        UINTN isz = 0; EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
        if (n->QueryMode(n, i, &isz, &info) != EFI_SUCCESS || !info) continue;
        if (info->HorizontalResolution == g_w && info->VerticalResolution == g_h) found = i;
    }
    if (found == ~0u || !n->Blt) return -1;
    if (n->Mode->Mode != found && n->SetMode(n, found) != EFI_SUCCESS) return -1;
    g_gop = n;
    g_vo_act = idx;
    if (n->Mode->Info->PixelFormat != pf_old) g_direct = 0;
    if (g_direct) {
        g_fb = (uint8_t *)(uintptr_t)n->Mode->FrameBufferBase;
        g_fb_pitch = n->Mode->Info->PixelsPerScanLine * 4;
        if (!set_mem_type(n->Mode->FrameBufferBase, n->Mode->FrameBufferSize, 0x08) || !g_fb || (uint64_t)g_fb_pitch * g_h > n->Mode->FrameBufferSize) g_direct = 0;
    }
    present_rows(0, g_h);
    return 0;
}
static int plat_video_diag(char (*lines)[64], int max)   /* 4.0.34: 진단 > Video outputs: 화면 출력마다 두 줄 */
{
    static const EFI_GUID ED_G = { 0x1c0c34f6, 0xd380, 0x41fa, { 0xa0, 0x49, 0x8a, 0xd0, 0x6c, 0x1a, 0x66, 0xaa } };
    static const EFI_GUID EA_G = { 0xbd8c1056, 0x9f36, 0x44ec, { 0x92, 0xa8, 0xa6, 0x33, 0x7f, 0x81, 0x79, 0x86 } };
    int n = 0;
    for (int k = 0; k < g_vo_n && n + 2 <= max; k++) {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *g = g_vo_gop[k];
        char *t = lines[n++]; t[0] = 0;
        vo_cat(t, 64, k == g_vo_act ? "> " : "  ");
        vo_cat(t, 64, g_vo_txt[k]);
        t = lines[n++]; t[0] = 0;
        vo_cat(t, 64, "    modes "); vo_num(t, 64, g->Mode->MaxMode);
        vo_cat(t, 64, " fmt "); vo_num(t, 64, g->Mode->Info ? g->Mode->Info->PixelFormat : 9);
        vo_cat(t, 64, " fb "); vo_hex(t, 64, (unsigned)(g->Mode->FrameBufferBase >> 16)); vo_cat(t, 64, "xxxx ");
        vo_num(t, 64, (unsigned)(g->Mode->FrameBufferSize >> 20)); vo_cat(t, 64, "MB");
        struct { uint32_t size; uint8_t *edid; } *ed = 0;
        if (g_vo_h[k] && (BS->HandleProtocol(g_vo_h[k], (EFI_GUID *)&EA_G, (void **)&ed) == EFI_SUCCESS || BS->HandleProtocol(g_vo_h[k], (EFI_GUID *)&ED_G, (void **)&ed) == EFI_SUCCESS) && ed) { vo_cat(t, 64, " edid "); vo_num(t, 64, ed->size); }
        else vo_cat(t, 64, " edid -");
    }
    if (!n) { vo_cat(lines[0], 64, "none"); n = 1; }
    return n;
}
static void plat_video_rescan(void)   /* 4.0.34: 모니터를 새로 꽂았으면 펌웨어에게 장치를 다시 찾게 한 뒤 목록을 새로 만듦 */
{
    EFI_HANDLE *hs = 0; UINTN hn = 0;
    if (BS->LocateHandleBuffer(AllHandles, 0, 0, &hn, &hs) == EFI_SUCCESS && hs) {
        for (UINTN i = 0; i < hn; i++) BS->ConnectController(hs[i], 0, 0, 1);
        BS->FreePool(hs);
    }
    vo_scan();
}
static int plat_hdmi_active(void) { return g_hd_on >= 0 || ivbhdmi_hdmi_on(); }
static int plat_video_outputs(char (*lines)[64], int max, int *active)
{
    int n = g_vo_n < max ? g_vo_n : max;
    for (int i = 0; i < n; i++) { int j = 0; for (; g_vo_txt[i][j] && j < 63; j++) lines[i][j] = g_vo_txt[i][j]; lines[i][j] = 0; }
    if (active) *active = g_vo_act;
    return n;
}
static void log_dir(void);
static int plat_audio_select(int pref, char *status, int len)
{
    uint32_t t0 = plat_ms();
    if (pref == 0 && g_hd_only && g_hd_on >= 0) pref = 2;   /* 4.0.127: HDMI만 출력 중이면 '자동' = HDMI 소리 */
    if (pref == 2 && g_hd_on >= 0) intelhdmi_wake();     /* 4.0.84 */
    { static const char bm[] = "begin\r\n"; log_dir(); plat_create("0:\\LOG\\HDA_SEL.TXT", 0); plat_write("0:\\LOG\\HDA_SEL.TXT", (const uint8_t *)bm, 7); }   /* 4.0.111: 멈추면 이 줄만 남음 → hda_select 안에서 멈춘 것 */
    if (pref == 2) ivbhdmi_audio();                       /* 4.0.110: Ivy Bridge(Acer V5-171) HDMI 소리 클럭·포트 비트 */
    int r = hda_select(pref, status, len);
    if (!r) g_ap = pref;
    uint32_t dt = plat_ms() - t0;
    static char lg[2048]; static int ln;          /* 4.0.67: 고를 때마다 LOG\HDA_SEL.TXT에 이어 씀 */
    if (ln > 1000) ln = 0;
    const char *h = "pref "; for (; *h && ln < 1900; h++) lg[ln++] = *h;
    lg[ln++] = (char)('0' + pref); lg[ln++] = ' '; lg[ln++] = r ? 'F' : 'O'; lg[ln++] = 'K'; lg[ln++] = ' ';
    for (const char *t = status; *t && ln < 1900; t++) lg[ln++] = *t;
    { const char *m = "  ms="; for (; *m; m++) lg[ln++] = *m; char d[12]; int k = 0; uint32_t v = dt; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 11); while (k) lg[ln++] = d[--k]; }
    lg[ln++] = '\r'; lg[ln++] = '\n';
    for (const char *t = hda_dbg_text(); *t && ln < 1900; t++) lg[ln++] = *t;
    { char ln2[8][64]; int k = hda_info(ln2, 8); for (int i = 0; i < k; i++) { for (const char *t = ln2[i]; *t && ln < 1900; t++) lg[ln++] = *t; lg[ln++] = '\r'; lg[ln++] = '\n'; } }
    log_dir(); plat_create("0:\\LOG\\HDA_SEL.TXT", 0); plat_write("0:\\LOG\\HDA_SEL.TXT", (const uint8_t *)lg, (uint32_t)ln);
    return r;
}

static void plat_poweroff(void)
{
    ST->RuntimeServices->ResetSystem(2, EFI_SUCCESS, 0, 0);        /* EfiResetShutdown */
    plat_reboot();
}

static char info_screen[48], info_snd[24], info_wc[80], info_present[96];

static void put_line(char *d, const char *s) { int i = 0; for (; s[i] && i < 63; i++) d[i] = s[i]; d[i] = 0; }

/* 3.0.4 진단: USB에 붙은 모든 장치(인터페이스마다 한 줄) "USB VID:PID ifN 종류" */
static int usb_list_info(char (*lines)[64], int max)
{
    EFI_HANDLE *hs = 0;
    UINTN cnt = 0;
    int n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &cnt, &hs) != EFI_SUCCESS) cnt = 0;
    static const char hx[] = "0123456789ABCDEF";
    for (UINTN j = 0; j < cnt && n < max; j++) {
        EFI_USB_IO_PROTOCOL *io;
        if (BS->HandleProtocol(hs[j], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
        EFI_USB_DEVICE_DESCRIPTOR dd;
        EFI_USB_INTERFACE_DESCRIPTOR ifd;
        memset(&dd, 0, sizeof dd);
        if (io->UsbGetDeviceDescriptor) io->UsbGetDeviceDescriptor(io, &dd);
        if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;
        char *q = lines[n++];
        const char *t = "USB ";
        while (*t) *q++ = *t++;
        for (int k = 12; k >= 0; k -= 4) *q++ = hx[(dd.IdVendor >> k) & 15];
        *q++ = ':';
        for (int k = 12; k >= 0; k -= 4) *q++ = hx[(dd.IdProduct >> k) & 15];
        t = " if"; while (*t) *q++ = *t++;
        *q++ = (char)('0' + ifd.InterfaceNumber % 10);
        *q++ = ' ';
        uint8_t c = ifd.InterfaceClass, sc = ifd.InterfaceSubClass, pr = ifd.InterfaceProtocol;
        t = c == 0x03 ? (sc == 1 && pr == 1 ? "HID keyboard" : sc == 1 && pr == 2 ? "HID mouse" : "HID")
          : c == 0x08 ? "storage" : c == 0x09 ? "hub" : c == 0x01 ? "audio" : c == 0x02 || c == 0x0A ? "comm"
          : c == 0x0E ? "camera" : c == 0xE0 ? "wireless (Bluetooth)" : c == 0x07 ? "printer" : c == 0x0B ? "smart card"
          : c == 0xFF ? "vendor" : "class";
        while (*t) *q++ = *t++;
        *q++ = ' '; *q++ = hx[c >> 4]; *q++ = hx[c & 15]; *q++ = '/'; *q++ = hx[sc >> 4]; *q++ = hx[sc & 15]; *q++ = '/'; *q++ = hx[pr >> 4]; *q++ = hx[pr & 15];
        if (dd.IdVendor == 0x046D) { t = " Logitech"; while (*t) *q++ = *t++; }
        *q = 0;
    }
    if (hs) BS->FreePool(hs);
    if (!cnt && n < max) put_line(lines[n++], "USB devices: none found");
    return n;
}

/* 3.0.6 진단: 펌웨어 타이머 이벤트가 도는지 (USB 비동기 전송·펌웨어 마우스는 이것으로 움직인다) */
static volatile uint32_t fw_ticks;
static int plat_create(const char *path, int is_dir);
static int plat_write(const char *path, const uint8_t *data, uint32_t len);
static void log_dir(void);
/* 멈춤 감시 — 프레임 수(fe_frame_count)가 1.5초 넘게 그대로면 fe 의 단계 고리를 LOG\HANG.TXT 로 (한 번만).
 * 펌웨어 타이머(TPL_CALLBACK)라 본 프로그램이 돌다 멈춰 있어도 불림. 본 프로그램이 USB 쓰기 중에 멈췄으면 쓰지 않음 */
extern volatile uint32_t fe_frame_count;
int fe_hang_dump(char *b, int max);
static volatile int fw_in_io;                       /* 본 프로그램이 파일을 쓰는 중 */
/* 멈춘 곳 찾기 — 타이머가 불렸을 때 스택을 훑어 이 프로그램(BOOTX64.EFI) 안 주소를 RVA(이미지 시작에서 떨어진 값)로 남김.
 * 맨 앞 몇 개가 멈춘 함수·부른 함수들 (build/BOOTX64.map 으로 이름을 찾음) */
extern char __ImageBase;
static int stack_rvas(char *b, int max)
{
    const uint8_t *ib = (const uint8_t *)&__ImageBase;
    uint32_t lfa = *(const uint32_t *)(ib + 0x3C);
    uint32_t isz = *(const uint32_t *)(ib + lfa + 24 + 56);
    uintptr_t lo = (uintptr_t)ib + 0x1000, hi = (uintptr_t)ib + isz;
    uintptr_t sp; __asm__ volatile("mov %%rsp, %0" : "=r"(sp));
    static const char H[] = "0123456789ABCDEF";
    int q = 0, got = 0;
    const char *h = "stack rva:"; while (*h && q < max - 1) b[q++] = *h++;
    for (uintptr_t a = sp & ~(uintptr_t)7; a < (sp & ~(uintptr_t)7) + 16384 && got < 40; a += 8) {
        uintptr_t v = *(const uintptr_t *)a;
        if (v < lo || v >= hi) continue;
        uint32_t r = (uint32_t)(v - (uintptr_t)ib);
        if (q > max - 12) break;
        b[q++] = ' '; for (int i = 20; i >= 0; i -= 4) b[q++] = H[(r >> i) & 15];
        got++;
    }
    b[q++] = '\r'; b[q++] = '\n'; b[q] = 0;
    return q;
}

/* ---- 5.2.28: 멈춤·느려짐 원인 찾기 — 메모리 종류(페이지 표+PAT), 인터럽트(LAPIC IRR/ISR, PCI INTx), 속도 재기 ---- */
static uintptr_t g_dma_dbg;
static int g_back_wb = -1;
static const char HXD[] = "0123456789ABCDEF";
static int hx_put(char *b, int q, int max, uint64_t v, int digits) { for (int i = (digits - 1) * 4; i >= 0 && q < max - 1; i -= 4) b[q++] = HXD[(v >> i) & 15]; b[q] = 0; return q; }
static int st_put(char *b, int q, int max, const char *t) { while (*t && q < max - 1) b[q++] = *t++; b[q] = 0; return q; }
static int dec_put(char *b, int q, int max, uint64_t v) { char t[24]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n && q < max - 1) b[q++] = t[--n]; b[q] = 0; return q; }
static const char *mt_name(int t) { static const char *const nm[8] = { "UC", "WC", "?2", "?3", "WT", "WP", "WB", "UC-" }; return nm[t & 7]; }
static int memtype_of(uint64_t a, char *b, int q, int max)          /* "WB/2M" 처럼: 실제 페이지 표 항목 + PAT */
{
    uint64_t cr3, cr4; __asm__ volatile("mov %%cr3, %0" : "=r"(cr3)); __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    uint64_t *t = (uint64_t *)(uintptr_t)(cr3 & 0x000FFFFFFFFFF000ull), e = 0; const char *sz = "?";
    if ((cr4 >> 12) & 1) { e = t[(a >> 48) & 511]; if (!(e & 1)) return st_put(b, q, max, "notmapped"); t = (uint64_t *)(uintptr_t)(e & 0x000FFFFFFFFFF000ull); }
    e = t[(a >> 39) & 511]; if (!(e & 1)) return st_put(b, q, max, "notmapped");
    t = (uint64_t *)(uintptr_t)(e & 0x000FFFFFFFFFF000ull); e = t[(a >> 30) & 511]; if (!(e & 1)) return st_put(b, q, max, "notmapped");
    int pat;
    if (e & 0x80) { sz = "1G"; pat = (int)(((e >> 12) & 1) << 2 | ((e >> 3) & 3)); }
    else {
        t = (uint64_t *)(uintptr_t)(e & 0x000FFFFFFFFFF000ull); e = t[(a >> 21) & 511]; if (!(e & 1)) return st_put(b, q, max, "notmapped");
        if (e & 0x80) { sz = "2M"; pat = (int)(((e >> 12) & 1) << 2 | ((e >> 3) & 3)); }
        else { t = (uint64_t *)(uintptr_t)(e & 0x000FFFFFFFFFF000ull); e = t[(a >> 12) & 511]; if (!(e & 1)) return st_put(b, q, max, "notmapped"); sz = "4K"; pat = (int)(((e >> 7) & 1) << 2 | ((e >> 3) & 3)); }
    }
    uint64_t pm = rdmsr(0x277);
    q = st_put(b, q, max, mt_name((int)(pm >> (pat * 8)) & 7)); q = st_put(b, q, max, "/"); q = st_put(b, q, max, sz);
    q = st_put(b, q, max, "/p"); q = dec_put(b, q, max, (uint64_t)pat);
    return q;
}
static int mtrr_type_of(uint64_t a)                                  /* MTRR 이 정한 종류 (가변 MTRR 만, 고정 MTRR 은 1 MB 아래) */
{
    uint64_t cap = rdmsr(0xFE), def = rdmsr(0x2FF); int n = (int)(cap & 255), t = -1;
    if (!((def >> 11) & 1)) return 0;                                /* MTRR 꺼짐 = UC */
    for (int i = 0; i < n && i < 32; i++) {
        uint64_t base = rdmsr(0x200 + 2 * i), mask = rdmsr(0x201 + 2 * i);
        if (!((mask >> 11) & 1)) continue;
        uint64_t m = mask & 0x000FFFFFFFFFF000ull;
        if ((a & m) == (base & m)) { int ty = (int)(base & 0xFF); if (t < 0) t = ty; else if (ty == 0 || t == 0) t = 0; else if (ty != t) t = ty == 4 || t == 4 ? 4 : ty; }
    }
    return t < 0 ? (int)(def & 0xFF) : t;
}
static uint32_t pci_cf8(int b, int d, int f, int o) { outl(0xCF8, 0x80000000u | (uint32_t)b << 16 | (uint32_t)d << 11 | (uint32_t)f << 8 | (uint32_t)(o & 0xFC)); return inl(0xCFC); }
static int irq_text(char *b, int q, int max)
{
    uint64_t ab = rdmsr(0x1B); int x2 = (int)((ab >> 10) & 1);
    q = st_put(b, q, max, x2 ? "lapic x2apic" : "lapic xapic");
    for (int k = 0; k < 2; k++) {
        q = st_put(b, q, max, k ? " ISR" : " IRR");
        int any = 0;
        for (int i = 0; i < 8; i++) {
            uint32_t v = x2 ? (uint32_t)rdmsr((k ? 0x810u : 0x820u) + (uint32_t)i) : *(volatile uint32_t *)(uintptr_t)((ab & 0x000FFFFFFFFFF000ull) + (k ? 0x100u : 0x200u) + (uint32_t)i * 0x10);
            for (int j = 0; j < 32; j++) if (v >> j & 1) { q = st_put(b, q, max, " "); q = hx_put(b, q, max, (uint64_t)(i * 32 + j), 2); any = 1; }
        }
        if (!any) q = st_put(b, q, max, " -");
    }
    q = st_put(b, q, max, "\r\npci intx asserted:");
    int any = 0;
    for (int bus = 0; bus < 8; bus++) for (int d = 0; d < 32; d++) for (int f = 0; f < 8; f++) {
        uint32_t id = pci_cf8(bus, d, f, 0); if (id == 0xFFFFFFFFu || !id) { if (!f) break; continue; }
        uint32_t cs = pci_cf8(bus, d, f, 4);
        if ((cs >> 16) & 8) { q = st_put(b, q, max, " "); q = hx_put(b, q, max, (uint64_t)bus, 2); q = st_put(b, q, max, ":"); q = hx_put(b, q, max, (uint64_t)d, 2); q = st_put(b, q, max, "."); q = hx_put(b, q, max, (uint64_t)f, 1); q = st_put(b, q, max, "="); q = hx_put(b, q, max, id & 0xFFFF, 4); q = st_put(b, q, max, ":"); q = hx_put(b, q, max, id >> 16, 4); q = st_put(b, q, max, (cs >> 10) & 1 ? "(masked)" : ""); any = 1; }
        if (!f && !((pci_cf8(bus, d, 0, 0x0C) >> 16) & 0x80)) break;
    }
    if (!any) q = st_put(b, q, max, " none");
    return st_put(b, q, max, "\r\n");
}
static uint32_t bench_cpu_us, bench_fb_us, bench_bss_us, bench_min_cpu = 0xFFFFFFFFu, bench_min_fb = 0xFFFFFFFFu, bench_min_bss = 0xFFFFFFFFu, bench_slow_n;
static uint8_t bench_buf[65536];
static void bench_tick(void)                                         /* 1초마다: 같은 일을 하는 데 걸린 시간 (느려지면 메모리 종류·인터럽트 문제) */
{
    if (!tsc_hz) return;
    uint64_t t0 = rdtsc(); volatile uint32_t acc = 0; for (uint32_t i = 0; i < 20000; i++) acc += i * 3;
    uint64_t t1 = rdtsc(); memset(bench_buf, (int)(t1 & 0x7F), sizeof bench_buf);
    uint64_t t2 = rdtsc(); if (g_back) { volatile uint32_t sum = 0; const volatile uint32_t *p = (const volatile uint32_t *)g_back; for (int i = 0; i < 65536; i += 16) sum += p[i]; (void)sum; }   /* 뒤 버퍼 읽기 (4 KB 건너 64번 x 16 = 줄마다) */
    uint64_t t3 = rdtsc();
    bench_cpu_us = (uint32_t)((t1 - t0) * 1000000ull / tsc_hz); bench_bss_us = (uint32_t)((t2 - t1) * 1000000ull / tsc_hz); bench_fb_us = (uint32_t)((t3 - t2) * 1000000ull / tsc_hz);
    if (bench_cpu_us < bench_min_cpu) bench_min_cpu = bench_cpu_us;
    if (bench_bss_us < bench_min_bss) bench_min_bss = bench_bss_us;
    if (g_back && bench_fb_us < bench_min_fb) bench_min_fb = bench_fb_us;
    int slow = bench_cpu_us > bench_min_cpu * 10 + 50 || bench_bss_us > bench_min_bss * 10 + 50 || (g_back && bench_fb_us > bench_min_fb * 10 + 50);
    if (slow && bench_slow_n < 8) {
        bench_slow_n++;
        extern void fe_trace_plat(const char *what, const char *x);
        char t[24]; int q = 0; q = dec_put(t, q, sizeof t, bench_cpu_us); q = st_put(t, q, sizeof t, "/"); q = dec_put(t, q, sizeof t, bench_bss_us); q = st_put(t, q, sizeof t, "/"); q = dec_put(t, q, sizeof t, bench_fb_us);
        fe_trace_plat("SLOW us cpu/bss/back", t);
    }
}
static int bench_text(char *b, int q, int max)
{
    q = st_put(b, q, max, "bench us cpu "); q = dec_put(b, q, max, bench_cpu_us); q = st_put(b, q, max, " (min "); q = dec_put(b, q, max, bench_min_cpu == 0xFFFFFFFFu ? 0 : bench_min_cpu);
    q = st_put(b, q, max, ") bss64K "); q = dec_put(b, q, max, bench_bss_us); q = st_put(b, q, max, " (min "); q = dec_put(b, q, max, bench_min_bss == 0xFFFFFFFFu ? 0 : bench_min_bss);
    q = st_put(b, q, max, ") back256K "); q = dec_put(b, q, max, bench_fb_us); q = st_put(b, q, max, " (min "); q = dec_put(b, q, max, bench_min_fb == 0xFFFFFFFFu ? 0 : bench_min_fb);
    return st_put(b, q, max, ")\r\n");
}
static int memtypes_text(char *b, int q, int max)
{
    extern void fe_hot_addrs(uintptr_t a[4]);
    uintptr_t a[4]; fe_hot_addrs(a);
    static const char *const nm[8] = { "fe_fb", "line", "msx", "vdpfb", "code", "dma", "g_back", "gop_fb" };
    uintptr_t all[8] = { a[0], a[1], a[2], a[3], (uintptr_t)&memtypes_text, g_dma_dbg, (uintptr_t)g_back, g_gop ? (uintptr_t)g_gop->Mode->FrameBufferBase : 0 };
    q = st_put(b, q, max, "memtype");
    for (int i = 0; i < 8; i++) {
        if (!all[i]) continue;
        q = st_put(b, q, max, " "); q = st_put(b, q, max, nm[i]); q = st_put(b, q, max, "="); q = hx_put(b, q, max, all[i], 9); q = st_put(b, q, max, ":");
        q = memtype_of(all[i], b, q, max); q = st_put(b, q, max, "/mtrr "); q = st_put(b, q, max, mt_name(mtrr_type_of(all[i])));
    }
    q = st_put(b, q, max, " back_alloc="); q = st_put(b, q, max, g_back_wb == 1 ? "wb" : g_back_wb == 2 ? "NOT-wb" : g_back_wb == 0 ? "fail" : "-");
    q = st_put(b, q, max, " PAT="); q = hx_put(b, q, max, rdmsr(0x277), 16);
    return st_put(b, q, max, "\r\n");
}
/* 큰 버퍼는 MTRR 이 WB(보통 메모리)인 자리에. LG gram(Meteor Lake)은 8 GB 위 메모리가 MTRR UC 라
 * 그곳에 잡힌 화면 뒤 버퍼에 쓰기·읽기가 수백 배 느려 화면 전체를 다시 그릴 때(진단 탭 바꾸기·창 닫기) 수십~수백 초 멈췄음 (5.2.28 memtype back=200000000 mtrr UC) */
static int range_wb(uint64_t a, UINTN pages)
{
    uint64_t end = a + (uint64_t)pages * 4096;
    for (uint64_t x = a; x < end; x += 0x100000) if (mtrr_type_of(x) != 6) return 0;
    return mtrr_type_of(end - 1) == 6;
}
static int alloc_wb(UINTN pages, EFI_PHYSICAL_ADDRESS *out)           /* 1 = WB 자리, 2 = 잡았지만 WB 아님, 0 = 못 잡음 */
{
    static const uint64_t lim[3] = { 0xFFFFFFFFull, (16ull << 30) - 1, (64ull << 30) - 1 };
    for (int i = 0; i < 3; i++) {
        EFI_PHYSICAL_ADDRESS a = lim[i];
        if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, pages, &a) != EFI_SUCCESS || !a) continue;
        if (range_wb(a, pages)) { *out = a; return 1; }
        BS->FreePages(a, pages);
    }
    EFI_PHYSICAL_ADDRESS a = 0;
    if (BS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &a) == EFI_SUCCESS && a) { *out = a; return range_wb(a, pages) ? 1 : 2; }
    return 0;
}

/* 멈춤 기록을 UEFI 변수(NVRAM)에도 — USB 쓰기 중에 멈추면 HANG.TXT 를 못 쓰므로. 다음 부팅에 LOG\LASTHANG.TXT 로 옮기고 지움 */
static EFI_GUID hang_guid = { 0x7e1a6d3b, 0x4c52, 0x4f0e, { 0x9b, 0x12, 0x58, 0x36, 0x4d, 0x53, 0x58, 0x02 } };
static const CHAR16 hang_vname[] = { 'X','8','6','M','S','X','H','A','N','G', 0 };
static void hang_nv_put(const char *b, int n)
{
    if (!ST || !ST->RuntimeServices) return;
    EFI_STATUS (*sv)(const CHAR16 *, EFI_GUID *, uint32_t, UINTN, void *) = (EFI_STATUS (*)(const CHAR16 *, EFI_GUID *, uint32_t, UINTN, void *))ST->RuntimeServices->SetVariable;
    if (n > 6000) { b += n - 6000; n = 6000; }                  /* 끝쪽(최근)만 */
    if (sv) sv(hang_vname, &hang_guid, 3, (UINTN)n, (void *)b);
}
static void hang_nv_move(void)                                   /* 부팅 때: 지난번 멈춤 기록이 있으면 파일로 */
{
    if (!ST || !ST->RuntimeServices) return;
    EFI_STATUS (*gv)(const CHAR16 *, EFI_GUID *, uint32_t *, UINTN *, void *) = (EFI_STATUS (*)(const CHAR16 *, EFI_GUID *, uint32_t *, UINTN *, void *))ST->RuntimeServices->GetVariable;
    EFI_STATUS (*sv)(const CHAR16 *, EFI_GUID *, uint32_t, UINTN, void *) = (EFI_STATUS (*)(const CHAR16 *, EFI_GUID *, uint32_t, UINTN, void *))ST->RuntimeServices->SetVariable;
    static char vb[6200]; UINTN sz = sizeof vb - 200; uint32_t at = 0;
    if (!gv || gv(hang_vname, &hang_guid, &at, &sz, vb) != EFI_SUCCESS || !sz) return;
    static char ob[6400]; int q = 0;
    const char *h = "#LOG 1\r\n#what=hang record kept in UEFI variable by the previous run (written by ver=" MSXEMU_VERSION " now)\r\n";
    while (*h) ob[q++] = *h++;
    for (UINTN i = 0; i < sz && q < (int)sizeof ob - 1; i++) ob[q++] = vb[i];
    log_dir(); plat_create("0:\\LOG\\LASTHANG.TXT", 0); plat_write("0:\\LOG\\LASTHANG.TXT", (const uint8_t *)ob, (uint32_t)q);
    if (sv) sv(hang_vname, &hang_guid, 0, 0, vb);              /* 지움 */
}
static int hang_seq;                                     /* 이번 부팅에서 멈춘 횟수: 첫 번째 HANG.TXT, 그 뒤 HANG2.TXT (첫 것을 덮지 않음) */
static void fw_tick(void *ev, void *ctx)
{
    (void)ev; (void)ctx; fw_ticks++;
    static uint32_t lastc, stall; static int done, busy;
    if (fe_frame_count != lastc || !fe_frame_count) { lastc = fe_frame_count; stall = 0; if (done) hang_seq++; done = 0; return; }
    ++stall;
    if (busy) return;
    if (!(stall == 150 || stall == 1000 || stall == 6000)) return;   /* 1.5초·10초·60초에 다시 씀 (어디서 도는지 바뀌나) */
    busy = 1; done = 1;
    static char hb[16384];
    int n = 0;
    { const char *t = stall == 150 ? "#stopped 1.5 s\r\n" : stall == 1000 ? "#still stopped 10 s\r\n" : "#still stopped 60 s\r\n"; while (*t) hb[n++] = *t++; }
    n += stack_rvas(hb + n, 1200);
    n = irq_text(hb, n, 4000); n = memtypes_text(hb, n, 5000); n = bench_text(hb, n, 5400);
    n += fe_hang_dump(hb + n, (int)sizeof hb - n - 64);
    const char *t = "(frames stopped — written by firmware timer; stack rva = BOOTX64.EFI offsets, see build map)\r\n"; while (*t && n < (int)sizeof hb - 1) hb[n++] = *t++;
    if (fw_in_io) { const char *w = "(stopped inside USB file I/O — kept in UEFI variable only)\r\n"; while (*w && n < (int)sizeof hb - 1) hb[n++] = *w++; }
    hang_nv_put(hb, n);                                          /* USB 를 못 써도 남게 */
    if (fw_in_io) { busy = 0; return; }
    const char *fn = hang_seq ? "0:\\LOG\\HANG2.TXT" : "0:\\LOG\\HANG.TXT";
    log_dir(); plat_create(fn, 0); plat_write(fn, (const uint8_t *)hb, (uint32_t)n);
    busy = 0;
}
/* 3.5.5: USB 장치 목록이 바뀌었나 (2초마다, 한 곳에서만). 바뀌면 usb_gen을 올려 각 장치 모듈이 다시 찾게 함 */
volatile uint32_t usb_gen;
static uint32_t usb_watch_due;
static UINTN usb_watch_n;
static uintptr_t usb_watch_sig;
static void usb_watch(void)
{
    uint32_t now = plat_ms();
    if ((int32_t)(now - usb_watch_due) < 0) return;
    usb_watch_due = now + 2000;
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &n, &hs) != EFI_SUCCESS) n = 0;
    uintptr_t sig = 0;
    for (UINTN j = 0; j < n; j++) sig = sig * 31 + (uintptr_t)hs[j];
    if (hs) BS->FreePool(hs);
    if (n != usb_watch_n || sig != usb_watch_sig) { usb_watch_n = n; usb_watch_sig = sig; usb_gen++; }
}


/* 4.0.15: USB(파일) 접근 계측 — 진단 화면에 횟수·마지막 경로·걸린 시간(time share의 file)을 보임 */
static uint32_t io_cnt, io_cnt_sec, io_cnt_prev;
static char io_last[60];
void fe_klog_io(const char *path, int w, uint32_t ms);
static void io_note(const char *path, int w, uint64_t t0)
{
    io_cnt++;
    fe_klog_io(path, w, (uint32_t)((rdtsc() - t0) / tsc_per_ms));
    fe_pf_add(9, rdtsc() - t0);
    int i = 0;
    io_last[i++] = w ? 'W' : 'R'; io_last[i++] = ' ';
    for (; path && *path && i < (int)sizeof io_last - 1; ) io_last[i++] = *path++;
    io_last[i] = 0;
}
extern void fe_trace_plat(const char *what, const char *x);
static const char *ptail(const char *p) { int l = 0; while (p && p[l]) l++; return l > 23 ? p + l - 23 : p; }
#define IOTR_B(path) fe_trace_plat("io begin", ptail(path))
#define IOTR_E() fe_trace_plat("io end", 0)
static int io_list(const char *path, FeDirEntry *out, int max) { uint64_t t = rdtsc(); IOTR_B(path); fw_in_io = 1; int r = plat_list(path, out, max); fw_in_io = 0; IOTR_E(); io_note(path, 0, t); return r; }
static int io_list_all(const char *path, FeDirEntry *out, int max) { uint64_t t = rdtsc(); IOTR_B(path); fw_in_io = 1; int r = plat_list_all(path, out, max); fw_in_io = 0; IOTR_E(); io_note(path, 0, t); return r; }
static int io_read(const char *path, uint8_t *buf, uint32_t max, uint32_t *size) { uint64_t t = rdtsc(); IOTR_B(path); fw_in_io = 1; int r = plat_read(path, buf, max, size); fw_in_io = 0; IOTR_E(); io_note(path, 0, t); return r; }
static int io_read_at(const char *path, uint32_t off, uint8_t *buf, uint32_t len) { uint64_t t = rdtsc(); IOTR_B(path); fw_in_io = 1; int r = plat_read_at(path, off, buf, len); fw_in_io = 0; IOTR_E(); io_note(path, 0, t); return r; }
static int io_write(const char *path, const uint8_t *data, uint32_t len) { uint64_t t = rdtsc(); IOTR_B(path); fw_in_io = 1; int r = plat_write(path, data, len); fw_in_io = 0; IOTR_E(); io_note(path, 1, t); return r; }
static int io_append(const char *path, const uint8_t *data, uint32_t len) { uint64_t t = rdtsc(); IOTR_B(path); fw_in_io = 1; int r = plat_append(path, data, len); fw_in_io = 0; IOTR_E(); io_note(path, 1, t); return r; }
static int io_write_at(const char *path, uint32_t off, const uint8_t *data, uint32_t len) { uint64_t t = rdtsc(); IOTR_B(path); fw_in_io = 1; int r = plat_write_at(path, off, data, len); fw_in_io = 0; IOTR_E(); io_note(path, 1, t); return r; }
static int io_mouse_read(int *dx, int *dy, int *dz, int *buttons, char *name, int max) { uint64_t t = rdtsc(); int r = plat_mouse_read(dx, dy, dz, buttons, name, max); fe_pf_add(10, rdtsc() - t); return r; }
static int io_hd_read(int u, uint32_t lba, uint8_t *buf) { uint64_t t = rdtsc(); fw_in_io = 1; int r = plat_hd_read(u, lba, buf); fw_in_io = 0; io_note("(HD image sector)", 0, t); return r; }
static int io_hd_write(int u, uint32_t lba, const uint8_t *buf) { uint64_t t = rdtsc(); fw_in_io = 1; int r = plat_hd_write(u, lba, buf); fw_in_io = 0; io_note("(HD image sector)", 1, t); return r; }

static void fw_timer_init(void)
{
    void *ev = 0;
    if (BS->CreateEvent && BS->CreateEvent(0x80000000u | 0x00000200u, 8, fw_tick, 0, &ev) == EFI_SUCCESS && ev)   /* EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK */
        BS->SetTimer(ev, 1, 100000);                                /* 10 ms마다 */
}

int i2c_diag_info(char (*lines)[64], int max);
void i2c_diag_setup(EFI_SYSTEM_TABLE *st, int (*wr)(const char *, const uint8_t *, uint32_t), int (*cr)(const char *, int));
int i2c_diag_info2(char (*lines)[64], int max);
/* 4.0.34: AMD DCN 2.1(Renoir) HDMI/DP 연결 감지 (읽기 전용) — dcn_2_1_0_offset.h: HPDn_DC_HPD_INT_STATUS = DCN베이스(0x34C0)+0x1f14+8n 레지스터(4바이트 단위), 비트1 = DC_HPD_SENSE, 비트4 = SENSE_DELAYED.
 * DC_GPIO_HPD_Y(0x28f7)는 핀의 원시 상태. 읽기만 하며 BAR5 안 이미 읽는 DCN 블록(0x13000~0x14800)과 같은 범위 (0x14F50~, 0x176DC) */
static int amd_hpd_info(char (*lines)[64], int max)
{
    int k = 0;
    for (int b = 0; b < 16; b++) for (int d = 0; d < 32; d++) {
        uint32_t id = vs_pci_rd(b, d, 0, 0), cmd = vs_pci_rd(b, d, 0, 4);
        if ((id & 0xFFFF) != 0x1002 || (vs_pci_rd(b, d, 0, 8) >> 24) != 0x03 || !(cmd & 2)) continue;
        uint32_t bar = vs_pci_rd(b, d, 0, 0x24);
        if ((bar & 1) || !(bar & 0xFFFFFFF0u) || vs_pci_rd(b, d, 0, 2) >> 16 == 0xFFFF) continue;
        volatile uint32_t *mm = (volatile uint32_t *)(uintptr_t)(bar & 0xFFFFFFF0u);
        if (k < max) {
            char *l = lines[k++]; const char *t = "amd hpd st:"; int q = 0; while (*t) l[q++] = *t++;
            for (int i = 0; i < 5; i++) {
                uint32_t v = mm[0x34C0 + 0x1f14 + 8 * i];
                l[q++] = ' '; l[q++] = (char)('0' + i); l[q++] = '=';
                if (v == 0xFFFFFFFFu) { l[q++] = '?'; continue; }
                l[q++] = (v & 2) ? 'C' : '-';                 /* C = 연결 감지 */
                l[q++] = (v & 0x10) ? 'D' : '-';              /* D = 지연 필터 후 연결 */
            }
            l[q] = 0;
        }
        if (k < max) {
            char *l = lines[k++]; const char *t = "amd hpd gpio Y="; int q = 0; while (*t) l[q++] = *t++;
            vs_hex(l + q, mm[0x34C0 + 0x28f7], 8); q += 8;
            t = " ctl0="; while (*t) l[q++] = *t++;
            vs_hex(l + q, mm[0x34C0 + 0x1f16], 8); q += 8; l[q] = 0;
        }
        if (k < max) {                                      /* 2a: DDC1~5 선 상태 (Y 레지스터만 읽기, 쓰기 없음). 3=SCL·SDA 모두 HIGH */
            char *l = lines[k++]; const char *t = "amd ddc y(scl,sda):"; int q = 0; while (*t) l[q++] = *t++;
            for (int n = 1; n <= 5; n++) {
                uint32_t y = mm[0x34C0 + 0x28d0 + (n - 1) * 4 + 3];
                l[q++] = ' '; l[q++] = (char)('0' + n); l[q++] = ':';
                l[q++] = y == 0xFFFFFFFFu ? '?' : (char)('0' + (((y & 1) << 1) | ((y >> 8) & 1)));
            }
            l[q] = 0;
        }
        for (int w = 0; w < 3 && k < max; w++) {           /* 2a: MASK/A/EN 현재값 덤프 (읽기 전용), 줄당 5개 라인 */
            char *l = lines[k++]; const char *t = w == 0 ? "ddcM" : w == 1 ? "ddcA" : "ddcE"; int q = 0; while (*t) l[q++] = *t++;
            for (int n = 1; n <= 5; n++) {
                l[q++] = ' '; l[q++] = (char)('0' + n); l[q++] = ':';
                vs_hex(l + q, mm[0x34C0 + 0x28d0 + (n - 1) * 4 + w], 8); q += 8;
            }
            l[q] = 0;
        }
        if (k < max) {                                                   /* 4.0.34: VBIOS 단서 (읽기 전용): BAR0 시작 2바이트, 확장 ROM BAR(0x30) */
            char *l = lines[k++]; const char *tt = "amd vbios bar0="; int q = 0; while (*tt) l[q++] = *tt++;
            uint32_t b0 = vs_pci_rd(b, d, 0, 0x10) & 0xFFFFFFF0u;
            if (b0) { const volatile uint8_t *p8 = (const volatile uint8_t *)(uintptr_t)b0; vs_hex(l + q, ((uint32_t)p8[0] << 8) | p8[1], 4); q += 4; } else { l[q++] = '-'; }
            tt = " rom="; while (*tt) l[q++] = *tt++;
            vs_hex(l + q, vs_pci_rd(b, d, 0, 0x30), 8); q += 8; l[q] = 0;
        }
        if (k < max) k += amdedid_info(mm, (mm[0x34C0 + 0x1f14 + 8] & 2) ? 1 : 0, lines + k, max - k);   /* 2b: DDC2 EDID (복원 포함) */
        if (k < max) k += vfct_info(lines + k, max - k);                        /* 4.0.34: VBIOS(ATOM) 유무 — ACPI VFCT 읽기 전용 */
        return k;
    }
    return 0;
}

static void log_dir(void) { static int d; if (!d) { d = 1; plat_create("0:\\LOG", 1); } }   /* 4.0.36: 진단 파일은 USB의 LOG 폴더에 */
/* GPU 진단 파일(LOG\GPU_DIAG.TXT) 저장 + Video 탭용 GPU 시험 (Intel) */
static int plat_info(char (*lines)[64], int max);
static int gpu_dump_file(void);
static int plat_has_amd_gpu(void)
{
    for (int b = 0; b < 16; b++) for (int d = 0; d < 32; d++) { uint32_t id = vs_pci_rd(b, d, 0, 0); if ((id & 0xFFFF) == 0x1002 && (vs_pci_rd(b, d, 0, 8) >> 24) == 0x03) return 1; }
    return 0;
}
/* ACPI 모드 켜기 (실험) — Lenovo V14(AMD Renoir) 내장 키보드가 스페이스+←+↑ 같은 조합의 셋째 키를 안 보냄(KEY_X5.2.160).
 * Windows(ACPI 모드)에서는 되므로, EC가 BIOS 상태에서 키 스캔을 달리 하는지 본다. SCI·GPE 알림은 모두 막고(PM1_EN=0, GPE0_EN=0) 켠다. */
static int plat_acpi_mode(int on, char *out, int outsz)
{
    static const EFI_GUID A20 = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x59, 0x81 } };
    static const EFI_GUID A10 = { 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
    typedef struct { EFI_GUID g; void *t; } CT;
    const char *msg = "no RSDP"; int rc = -1;
    (void)on;
    CT *ct = (CT *)ST->ConfigurationTable; const uint8_t *rsdp = 0;
    for (int pass = 0; pass < 2 && !rsdp; pass++)
        for (UINTN i = 0; i < ST->NumberOfTableEntries; i++)
            if (!__builtin_memcmp(&ct[i].g, pass ? &A10 : &A20, sizeof(EFI_GUID))) { rsdp = (const uint8_t *)ct[i].t; break; }
    const uint8_t *f = 0;
    if (rsdp && !__builtin_memcmp(rsdp, "RSD PTR ", 8)) {
        const uint8_t *root; int esz;
        uint64_t x = rsdp[15] >= 2 ? *(const uint64_t *)(rsdp + 24) : 0;
        if (x) { root = (const uint8_t *)(uintptr_t)x; esz = 8; } else { root = (const uint8_t *)(uintptr_t)*(const uint32_t *)(rsdp + 16); esz = 4; }
        uint32_t rl = root ? *(const uint32_t *)(root + 4) : 0;
        msg = "no FACP";
        if (root && rl >= 36 && rl < 0x100000)
            for (uint32_t o = 36; o + (uint32_t)esz <= rl && !f; o += (uint32_t)esz) {
                const uint8_t *t = (const uint8_t *)(uintptr_t)(esz == 8 ? *(const uint64_t *)(root + o) : *(const uint32_t *)(root + o));
                if (t && !__builtin_memcmp(t, "FACP", 4)) f = t;
            }
    }
    if (f) {
        uint32_t smi = *(const uint32_t *)(f + 48); uint8_t en = f[52];
        uint32_t pm1a_evt = *(const uint32_t *)(f + 56), pm1a_cnt = *(const uint32_t *)(f + 64);
        uint32_t gpe0 = *(const uint32_t *)(f + 80); uint8_t evt_len = f[88], gpe0_len = f[92];
        int sci0 = pm1a_cnt ? (inw((uint16_t)pm1a_cnt) & 1) : -1;
        if (!pm1a_cnt) msg = "no PM1a";
        else if (sci0 == 1) { msg = "already on"; rc = 0; }
        else if (!smi || !en) msg = "no SMI_CMD";
        else {
            if (pm1a_evt && evt_len >= 4) { outw((uint16_t)(pm1a_evt + evt_len / 2), 0); outw((uint16_t)pm1a_evt, 0xFFFF); }   /* PM1 알림 끄기·상태 지우기 */
            if (gpe0 && gpe0_len >= 2) for (int i = 0; i < gpe0_len / 2; i++) { outb((uint16_t)(gpe0 + gpe0_len / 2 + i), 0); outb((uint16_t)(gpe0 + i), 0xFF); }   /* GPE 끄기·지우기 */
            outb((uint16_t)smi, en);
            uint64_t t0 = rdtsc();
            while (!(inw((uint16_t)pm1a_cnt) & 1) && rdtsc() - t0 < tsc_per_ms * 3000) __asm__ volatile("pause");
            rc = (inw((uint16_t)pm1a_cnt) & 1) ? 0 : -1;
            msg = rc ? "SCI_EN did not set" : "turned on";
        }
        if (out && outsz > 40) {
            int q = 0; while (*msg && q < 24) out[q++] = *msg++;
            const char *t = " smi "; while (*t) out[q++] = *t++; vs_hex(out + q, smi, 4); q += 4;
            t = " en "; while (*t) out[q++] = *t++; vs_hex(out + q, en, 2); q += 2;
            t = " pm1a "; while (*t) out[q++] = *t++; vs_hex(out + q, pm1a_cnt, 4); q += 4;
            out[q] = 0;
            return rc;
        }
    }
    if (out && outsz > 0) { int q = 0; while (*msg && q < outsz - 1) out[q++] = *msg++; out[q] = 0; }
    return rc;
}

static int plat_gpu_try(int run)
{
    if (run < 0) { ih_ready(); return intelhdmi_try(0) >= 0 && !plat_has_amd_gpu() ? 1 : 0; }   /* 4.0.58: -1 = 기본 대상 질의 (1 = Intel) */
    if (!run) { ih_ready(); intelhdmi_try(0); return gpu_dump_file(); }       /* Video 탭을 열면: 텍스트 파일만 저장 (GPU 쓰기 없음) */
    if (run == 4) { ih_ready(); static int a; a = intelhdmi_audio(!a); return a; }   /* 4.0.65: HDMI 소리 그래픽 쪽 시험 (S키) */
    if (run == 3) { HdaDiagIo hio = { vs_pci_rd, ih_stall, ih_logw }; ih_ready(); return hdadiag_run(&hio); }   /* 4.0.62: HDMI 소리 진단 */
    if (run == 2) { ih_ready(); return intelhdmi_try(1) > 0; }   /* 4.0.58: 2 = Intel Gen9 HDMI 미러 (켜기/끄기 토글) */
    return 0;
}
static int plat_info(char (*lines)[64], int max)
{
    int n = 0;
    {
        char tl[64] = "USB timer events: ", *q = tl + 18;
        uint32_t x = fw_ticks; char nb[12]; int nn = 0;
        do { nb[nn++] = (char)('0' + x % 10); x /= 10; } while (x && nn < 11);
        while (nn) *q++ = nb[--nn];
        const char *t = fw_ticks ? " (firmware timer OK)" : " (firmware timer NOT running)";
        while (*t) *q++ = *t++;
        *q = 0;
        if (n < max) put_line(lines[n++], tl);
    }
    if (n < max) {   /* 파일 만들기·쓰기의 마지막 EFI 상태 (0 = 성공) — 로그가 안 생기는 원인 확인용 */
        char wl[64] = "file create/write status: "; static const char hx[] = "0123456789ABCDEF"; int k = (int)strlen(wl);
        for (int i = 28; i >= 0; i -= 4) wl[k++] = hx[(g_cst >> i) & 15];
        wl[k++] = '/'; for (int i = 28; i >= 0; i -= 4) wl[k++] = hx[(g_wst >> i) & 15];
        wl[k] = 0; put_line(lines[n++], wl);
    }
    if (n < max) {   /* USB 다시 찾기 횟수 (전체/깊게/실패), 볼륨 수 */
        char rl[80] = "USB rescan: "; int k = (int)strlen(rl); uint32_t vv[4] = { g_rescan_n, g_rescan_deep, g_rescan_fail, (uint32_t)nvols };
        for (int j = 0; j < 4; j++) { char nb[12]; int nn = 0; uint32_t x = vv[j]; do { nb[nn++] = (char)('0' + x % 10); x /= 10; } while (x); while (nn) rl[k++] = nb[--nn]; rl[k++] = j == 2 ? ' ' : j < 2 ? '/' : 0; if (j == 2) { const char *t = "vols "; while (*t) rl[k++] = *t++; } }
        rl[k] = 0; put_line(lines[n++], rl);
    }
    if (n < max) {
        char il[64] = "file I/O: "; char *q = il + 10; uint32_t x = io_cnt_sec; char nb[12]; int nn = 0;
        do { nb[nn++] = (char)('0' + x % 10); x /= 10; } while (x && nn < 11);
        while (nn) *q++ = nb[--nn];
        const char *t = "/s  total "; while (*t) *q++ = *t++;
        x = io_cnt; nn = 0; do { nb[nn++] = (char)('0' + x % 10); x /= 10; } while (x && nn < 11);
        while (nn) *q++ = nb[--nn];
        *q = 0; put_line(lines[n++], il);
    }
    if (n < max && io_last[0]) put_line(lines[n++], io_last);
    if (n < max) {             /* 화면 모드와 EDID 크기 → PC 점 모양 (1.00 이 아니면 가로 비율을 그만큼 보정함) */
        char sl[80]; int q = 0; q = st_put(sl, q, 80, "display: "); q = dec_put(sl, q, 80, g_w); q = st_put(sl, q, 80, "x"); q = dec_put(sl, q, 80, g_h);
        if (g_mm_w && g_mm_h) {
            uint64_t r = (uint64_t)g_mm_w * g_h * 1000 / ((uint64_t)g_mm_h * g_w);
            q = st_put(sl, q, 80, " edid "); q = dec_put(sl, q, 80, g_mm_w); q = st_put(sl, q, 80, "x"); q = dec_put(sl, q, 80, g_mm_h); q = st_put(sl, q, 80, " mm pixel ");
            q = dec_put(sl, q, 80, r / 1000); q = st_put(sl, q, 80, "."); q = dec_put(sl, q, 80, (r / 100) % 10); q = dec_put(sl, q, 80, (r / 10) % 10);
            if (r < 950 || r > 1050) q = st_put(sl, q, 80, " (non-square, corrected)");
        } else q = st_put(sl, q, 80, " edid size unknown");
        put_line(lines[n++], sl);
    }
    if (n < max && g_back) {   /* 화면 뒤 버퍼 자리의 메모리 종류 (UC 면 화면 전체 그리기가 아주 느림) */
        char bl[64]; int q = 0; q = st_put(bl, q, 64, "video back buffer: "); q = hx_put(bl, q, 64, (uintptr_t)g_back, 9);
        q = st_put(bl, q, 64, " mtrr "); q = st_put(bl, q, 64, mt_name(mtrr_type_of((uintptr_t)g_back)));
        q = st_put(bl, q, 64, g_back_wb == 1 ? " (WB place)" : g_back_wb == 2 ? " (no WB place found)" : "");
        put_line(lines[n++], bl);
    }
    {   /* 속도·메모리 종류·인터럽트 (진단 줄은 64자라 잘라서) */
        static char tb[1200]; int q = 0; q = bench_text(tb, q, sizeof tb); q = memtypes_text(tb, q, sizeof tb); q = irq_text(tb, q, sizeof tb);
        int i = 0;
        while (tb[i] && n < max) {
            char ln[64]; int k = 0; const char *pfx = "perf/irq ";
            for (const char *c = pfx; *c; c++) ln[k++] = *c;
            while (tb[i] && tb[i] != '\r' && tb[i] != '\n' && k < 63) ln[k++] = tb[i++];
            ln[k] = 0; put_line(lines[n++], ln);
            while (tb[i] == '\r' || tb[i] == '\n') i++;
        }
    }
    if (n < max) put_line(lines[n++], "boot mode: UEFI, 64-bit (firmware services kept)");
    {   /* SMBIOS (PC 이름·BIOS·보드) — 진단 요약·LOG 에서 어느 PC 인지 */
        static char sm[4][64]; static int smn = -1;
        if (smn < 0) {
            static const EFI_GUID G3 = { 0xf2fd1544, 0x9794, 0x4a2c, { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } };
            static const EFI_GUID G2 = { 0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
            typedef struct { EFI_GUID g; void *t; } CT;
            CT *ct = (CT *)ST->ConfigurationTable; const uint8_t *ep = 0;
            for (int pass = 0; pass < 2 && !ep; pass++)
                for (UINTN i = 0; i < ST->NumberOfTableEntries; i++)
                    if (!__builtin_memcmp(&ct[i].g, pass ? &G2 : &G3, sizeof(EFI_GUID))) { ep = (const uint8_t *)ct[i].t; break; }
            smn = smbios_lines(ep, sm, 4);
        }
        for (int i = 0; i < smn && n < max; i++) put_line(lines[n++], sm[i]);
    }
    if (n < max) put_line(lines[n++], info_screen);
    if (n < max && info_present[0]) put_line(lines[n++], info_present);
    if (n < max && vs_dsl) {
        char *q = lines[n]; const char *t = "screen sync lines "; while (*t) *q++ = *t++;
        int v = vs_effoff(); if (v < 0) { *q++ = '-'; v = -v; } else *q++ = '+';
        char nb[12]; int nn = 0; do { nb[nn++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (nn) *q++ = nb[--nn];
        t = "  (</> keys)  max "; while (*t) *q++ = *t++;
        uint32_t m = vs_vmax; nn = 0; do { nb[nn++] = (char)('0' + m % 10); m /= 10; } while (m);
        while (nn) *q++ = nb[--nn];
        *q = 0; n++;
    }
    for (int k = 0; k < vs_dbgn && n < max; k++) put_line(lines[n++], vs_dbg[k]);
    if (n < max) put_line(lines[n++], !have_8042 ? "8042 keyboard controller: not present"
                                    : ps2_detached ? "8042 keyboard: present, firmware driver detached"
                                                   : "8042 keyboard: present (polled)");
    if (n < max && have_8042) {
        char *q = lines[n]; const char *t = "8042 cfg ";
        while (*t) *q++ = *t++;
        for (int k = 0; k < 2; k++) {
            int v = k ? kb_cfg1 : kb_cfg0;
            if (v < 0) { *q++ = '?'; *q++ = '?'; } else { *q++ = "0123456789ABCDEF"[(v >> 4) & 15]; *q++ = "0123456789ABCDEF"[v & 15]; }
            if (!k) { t = " -> "; while (*t) *q++ = *t++; }
        }
        t = "  kbd F6/F4 ack "; while (*t) *q++ = *t++;
        *q++ = kb_a6 == 0 ? 'Y' : 'N'; *q++ = '/'; *q++ = kb_a4 == 0 ? 'Y' : 'N'; *q = 0;
        n++;
    }
    n += hda_info(lines + n, max - n);
    { extern int vtd_info(char (*)[64], int); if (n < max) n += vtd_info(lines + n, max - n); }   /* 5.2.20 */
    if (n < max) n += amddet_info(lines + n, max - n);
    if (n < max) n += amddet_wifi_info(lines + n, max - n);
    if (n < max) n += rtwdet_info(lines + n, max - n);        /* 4.0.34: RTL8822CE 식별 (1b, 읽은 뒤 복구) */
    if (n < max) n += amd_hpd_info(lines + n, max - n);       /* 4.0.34: HDMI/DP 연결 감지 (읽기 전용) */        /* 4.0.34: 그래픽·무선랜 PCI 열거 (읽기 전용, 다른 AI 작업물 반영) */
    if (n < max) put_line(lines[n++], "");
    n += input_info(lines + n, max - n);                 /* 키보드·패드·MIDI·마우스 (input_drv[] 순서) */
    n += i2c_diag_info(lines + n, max - n);              /* 3.8.7 */
    n += i2c_diag_info2(lines + n, max - n);             /* 3.8.8 */
    n += usb_list_info(lines + n, max - n);
    if (n < max) {                                                  /* 3.0.4: 받은 바이트·묶음 수 */
        char ml[64] = "", *q = ml;
        const char *t = ps2_ok ? (ps2m.size == 4 ? "PS/2 mouse: on (wheel)" : "PS/2 mouse: on") : "PS/2 mouse: none";
        while (*t) *q++ = *t++;
        if (ps2_ok) {
            uint32_t v[2] = { ms_bytes, ms_packets };
            t = ", bytes "; while (*t) *q++ = *t++;
            for (int k = 0; k < 2; k++) {
                char nb[12]; int nn = 0; uint32_t x = v[k];
                do { nb[nn++] = (char)('0' + x % 10); x /= 10; } while (x && nn < 11);
                while (nn) *q++ = nb[--nn];
                if (!k) { t = ", packets "; while (*t) *q++ = *t++; }
            }
        }
        *q = 0;
        put_line(lines[n++], ml);
    }
    return n;
}

/* 4.0.34: Video 탭을 열면(그리고 G 실험 뒤) 진단 줄 전체를 USB 루트 GPU_DIAG.TXT로 저장 — 사진 대신 이 파일을 다른 AI에게 전달 */
static const char *diag_head;                       /* 4.0.37: fe가 준 시스템 줄 */
static int gpu_dump_file(void)
{
    static char lines[480][64];
    static char buf[480 * 68 + 24000];
    int q = 0, n;
    const char *h = "";
    if (diag_head) { const char *d = diag_head; while (*d && q < (int)sizeof buf - 12000) buf[q++] = *d++; }   /* fe 가 만든 규약 머리·요약·영역별 줄 (정보 줄 포함) */
    h = "== VID outputs ==\r\n"; while (*h) buf[q++] = *h++;
    n = plat_video_diag(lines, 40);
    for (int i = 0; i < n; i++) { for (int c = 0; lines[i][c] && q < (int)sizeof buf - 80; c++) buf[q++] = lines[i][c]; buf[q++] = '\r'; buf[q++] = '\n'; }
    h = "== steps (history) ==\r\n"; while (*h) buf[q++] = *h++;
    n = amddisp_hist(lines, 320);
    for (int i = 0; i < n; i++) { for (int c = 0; lines[i][c] && q < (int)sizeof buf - 80; c++) buf[q++] = lines[i][c]; buf[q++] = '\r'; buf[q++] = '\n'; }
    log_dir();
    plat_create("0:\\LOG\\GPU_DIAG.TXT", 0);
    plat_create("0:\\LOG\\DIAG.TXT", 0); plat_write("0:\\LOG\\DIAG.TXT", (const uint8_t *)buf, (uint32_t)q);   /* 4.0.37: 진단 창을 열 때마다 전체 */
    return plat_write("0:\\LOG\\GPU_DIAG.TXT", (const uint8_t *)buf, (uint32_t)q);
}
static void plat_diag_save(const char *head) { diag_head = head; gpu_dump_file(); }

/* ------------------------------------------------------------------ */
/* 진입점                                                               */
/* ------------------------------------------------------------------ */
static NetUefi g_netuefi;                              /* 반드시 정적(약 65 KB): UEFI가 안의 토큰·버퍼를 계속 가리킨다 */
static NetHost g_nethost;
static int g_net_wired_req;                             /* 지금 lwIP 가 쓰는 랜카드 종류 (0 = 안 씀, NK_WIRED, NK_WIFI), 사용자가 유선을 골랐는지 */
void uefi_net_select(int wired) { g_net_wired_req = wired; }
void uefi_net_lease_set(const uint8_t ip[4]) { net_lease_set(ip); }   /* 5.2.115 */
static int usb_drv_present(void)                        /* X86MSX 드라이버가 있는 USB 랜 어댑터가 꽂혀 있는지 (가끔만 훑음) */
{
    static int last = -1, calls;
    if (last >= 0 && (calls++ % 90) != 0) return last;
    EFI_HANDLE *hs = 0; UINTN cnt = 0; int found = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &cnt, &hs) != EFI_SUCCESS) cnt = 0;
    for (UINTN j = 0; j < cnt && !found; j++) {
        EFI_USB_IO_PROTOCOL *io; EFI_USB_DEVICE_DESCRIPTOR dd; EFI_USB_INTERFACE_DESCRIPTOR ifd;
        if (BS->HandleProtocol(hs[j], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
        memset(&dd, 0, sizeof dd);
        if (io->UsbGetDeviceDescriptor) io->UsbGetDeviceDescriptor(io, &dd);
        if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;
        const NicId *e = nicdb_usb(dd.IdVendor, dd.IdProduct, ifd.InterfaceClass, ifd.InterfaceSubClass);
        if (e && e->kind == NK_WIRED && e->drv) found = 1;
    }
    if (hs) BS->FreePool(hs);
    last = found;
    return found;
}
int uefi_net_poll(void)   /* 프레임마다 (fe.c). 5.0.157: 무선 PC 는 lwIP(netstack) + 무선 Nic. 5.2.110: USB 유선도 lwIP + 유선 Nic */
{
    int want = (g_net_wired_req && !netuefi_has_nic(&g_netuefi) && usb_drv_present()) ? NK_WIRED : (g_wifi_cfg ? NK_WIFI : 0);
    if (want != g_lwip_kind) {
        extern void net_log(const char *s);
        if (g_lwip_kind) { net_stop(); g_wifi_started = 0; }
        g_lwip_kind = want;
        if (want) { net_log(want == NK_WIRED ? "NET.mode wired(usb)" : "NET.mode wireless"); msx_net_host = nethost_get(); }
        else msx_net_host = &g_nethost;
        g_host_dirty = 1;
    }
    if (g_lwip_kind) {
        if (!g_wifi_started) { g_wifi_started = 1; net_set_kind(g_lwip_kind); net_start(plat_ms()); }
        net_poll(plat_ms());
        return nethost_poll();
    }
    return netuefi_poll(&g_netuefi);
}
void uefi_net_rehost(NetCard *nc) { if (g_host_dirty) { g_host_dirty = 0; netcard_set_host(nc, msx_net_host); } }   /* 무선이 켜지면 카트리지의 호스트를 lwIP 쪽으로 바꿈 */

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    ST = st;
    BS = st->BootServices;
    extern void usbnic_init(EFI_BOOT_SERVICES *);
    usbnic_init(BS);                                    /* USB 랜 드라이버가 쓸 부트 서비스 */
    netuefi_init(&g_netuefi, st);                       /* 네트워크 카트리지 호스트(막지 않음) */
    netuefi_host(&g_netuefi, &g_nethost);
    msx_net_host = &g_nethost;
    BS->SetWatchdogTimer(0, 0, 0, 0);
    st->ConOut->ClearScreen(st->ConOut);
    print("X86MSX emulator (UEFI) starting...\n");

    uint64_t t0 = rdtsc();
    BS->Stall(100000);
    tsc_hz = (rdtsc() - t0) * 10;
    tsc_per_ms = tsc_hz / 1000;
    if (!tsc_per_ms) tsc_per_ms = 1;

    EFI_LOADED_IMAGE_PROTOCOL *li;
    if (BS->HandleProtocol(image, &LOADED_IMAGE_GUID, (void **)&li) != EFI_SUCCESS) die("no loaded image protocol", 0);
    boot_dev = li->DeviceHandle;

    /* 모든 장치 연결 (다른 디스크와 USB도 보이게) */
    EFI_HANDLE *all = 0;
    UINTN nall = 0;
    if (BS->LocateHandleBuffer(AllHandles, 0, 0, &nall, &all) == EFI_SUCCESS) {
        int ivb_keep = ivbhdmi_keep_mode();   /* 4.0.114: Ivy Bridge + HDMI 켜짐 → 그래픽 장치·화면 출력 핸들은 건드리지 않음 (DisplaySelect처럼. 다시 연결하면 BIOS가 켠 화면이 꺼짐) */
        static EFI_GUID pciio = { 0x4cf5b200, 0x68b8, 0x4ca5, { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } };
        for (UINTN i = 0; i < nall; i++) {
            if (ivb_keep) {
                void *x = 0; uint32_t cls = 0;
                if (BS->HandleProtocol(all[i], &GOP_GUID, &x) == EFI_SUCCESS) continue;
                if (BS->HandleProtocol(all[i], &pciio, &x) == EFI_SUCCESS && x) {
                    struct PIO { void *r[6]; EFI_STATUS (*rd)(struct PIO *, int, uint32_t, uintptr_t, void *); } *p = x;   /* PollMem PollIo Mem(2) Io(2) 다음이 PciRead */
                    if (p->rd(p, 2, 8, 1, &cls) == EFI_SUCCESS && (cls >> 24) == 3) continue;
                }
            }
            BS->ConnectController(all[i], 0, 0, 1);
        }
        BS->FreePool(all);
    }
    refresh_volumes();
    if (!nvols || vols[0] != boot_dev) die("boot volume not found", 0);

    {   /* 3.4.0: 큰 펌웨어 롬 자리 4 MiB (FS-A1GT 등) */
        EFI_PHYSICAL_ADDRESS fw = 0xFFFFFFFFu;
        if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (4u << 20) / 4096, &fw) == EFI_SUCCESS) { machine_big = (uint8_t *)(uintptr_t)fw; machine_big_size = 4u << 20; }
    }
    {   /* 4.0.129: 큰 카트리지 롬 자리 16 MiB (ASCII16-X·Yamanooto 8 MB). 없으면 그런 롬만 못 꽂음 */
        EFI_PHYSICAL_ADDRESS cb = 0xFFFFFFFFu;
        if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (16u << 20) / 4096, &cb) == EFI_SUCCESS) { cart_big = (uint8_t *)(uintptr_t)cb; cart_big_size = 16u << 20; }
    }
    char err[160];
    const char *first_machine = machine_default_name(plat_read);
    if (machine_load(&m, plat_read, first_machine, 0, err, sizeof err)) die("machine setup failed:", err);

    /* 사운드 DMA: 4 GiB 아래 2 MiB 정렬 */
    EFI_PHYSICAL_ADDRESS dr = 0xFFFFFFFFu;
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 1024, &dr) != EFI_SUCCESS) die("cannot allocate DMA memory", 0);
    uintptr_t dma = ((uintptr_t)dr + 0x1FFFFF) & ~(uintptr_t)0x1FFFFF;
    g_dma_dbg = dma;
    memset((void *)dma, 0, 0x10000);

    /* GOP */
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    if (BS->LocateProtocol(&GOP_GUID, 0, (void **)&gop) != EFI_SUCCESS) die("no graphics output (GOP)", 0);
    gop = vo_pick(gop);                                  /* 4.0.34: 저장된 화면 출력 (VideoOut=) */
    uint32_t best = gop->Mode->Mode, best_area = 0;
    for (uint32_t i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
        UINTN isz;
        if (gop->QueryMode(gop, i, &isz, &info) != EFI_SUCCESS) continue;
        if (info->PixelFormat > PixelBlueGreenRedReserved8BitPerColor) continue;
        uint32_t w = info->HorizontalResolution, h = info->VerticalResolution;
        if (w > 1920 || h > 1200 || w < 640 || h < 480) continue;
        if (w * h > best_area) { best_area = w * h; best = i; }
    }
    blog("gop picked");
    if (best != gop->Mode->Mode && !ivbhdmi_keep_mode()) gop->SetMode(gop, best);   /* 4.0.113: Ivy Bridge에서 HDMI가 켜져 있으면 BIOS가 정한 모드 그대로 (DisplaySelect처럼) — 모드를 바꾸면 화면이 검어짐 */
    blog("mode set");
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *gi = gop->Mode->Info;
    FeDisplay d;
    memset(&d, 0, sizeof d);
    d.fb = (uint8_t *)(uintptr_t)gop->Mode->FrameBufferBase;
    d.w = gi->HorizontalResolution;
    d.h = gi->VerticalResolution;
    d.pitch = gi->PixelsPerScanLine * 4;
    {   /* 뒤 버퍼에 그리고 바뀐 줄만 GOP Blt로 화면에 (일부 AMD 펌웨어에서 직접 쓰면 줄이 지나가는 현상 방지) */
        EFI_PHYSICAL_ADDRESS bb = 0;
        UINTN bytes = (UINTN)d.w * 4 * d.h, pages = (bytes + 4095) / 4096;
        if (gop->Blt && (g_back_wb = alloc_wb(pages, &bb)) != 0 && bb) {
            g_back = (uint8_t *)(uintptr_t)bb;
            memset(g_back, 0, bytes);
            d.fb = g_back;
            d.pitch = d.w * 4;
            g_gop = gop;
            g_w = d.w;
            g_h = d.h;
        }
    }
    d.bpp = 4;
    d.bgr = gi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor;
    uint32_t s = d.w / 256 < d.h / 192 ? d.w / 256 : d.h / 192;
    if (s < 1) s = 1;
    d.scale_x = d.scale_y = s;
    {   /* EDID에서 화면 세로 실제 크기(mm): 상세 타이밍 블록, 없으면 기본 블록의 cm */
        static EFI_GUID EDID_ACTIVE_GUID = { 0xbd8c1056, 0x9f36, 0x44ec, { 0x92, 0xa8, 0xa6, 0x33, 0x7f, 0x81, 0x79, 0x86 } };
        struct { uint32_t size; uint8_t *edid; } *ed = 0;
        if (BS->LocateProtocol(&EDID_ACTIVE_GUID, 0, (void **)&ed) == EFI_SUCCESS && ed && ed->edid && ed->size >= 128) {
            const uint8_t *e = ed->edid;
            uint32_t mm = (uint32_t)e[0x36 + 13] | ((uint32_t)(e[0x36 + 14] & 0x0F) << 8);
            if (!mm || mm > 2000) mm = (uint32_t)e[0x16] * 10;
            if (mm >= 80 && mm <= 2000) d.mm_h = mm;
            uint32_t mw = (uint32_t)e[0x36 + 12] | ((uint32_t)(e[0x36 + 14] & 0xF0) << 4);   /* 가로 mm (점 모양 보정용) */
            if (!mw || mw > 3000) mw = (uint32_t)e[0x15] * 10;
            if (mw >= 80 && mw <= 3000) d.mm_w = mw;
            g_mm_w = d.mm_w; g_mm_h = d.mm_h;
        }
    }

    /* 메모리 형식: 프레임 버퍼 WC, 사운드 DMA UC */
    pat_setup();
    int wcn = set_mem_type(gop->Mode->FrameBufferBase, gop->Mode->FrameBufferSize, 0x08);
    blog(wcn ? "wc on" : "wc off");
    put_line(info_wc, wcn ? "WC: on (PAT)" : "WC: off");
    if (g_back) {   /* 3.5.5: 화면 전체 갱신 시간을 재서 펌웨어 Blt가 느리면 직접 복사 (툴바·메뉴가 뜰 때 끊김 방지) */
        g_fb = (uint8_t *)(uintptr_t)gop->Mode->FrameBufferBase;
        g_fb_pitch = gi->PixelsPerScanLine * 4;
        uint64_t tb = ~0ull, tc = ~0ull;
        int can = g_fb && wcn && (uint64_t)g_fb_pitch * g_h <= gop->Mode->FrameBufferSize;
        for (int k = 0; k < 3; k++) {
            uint64_t t0 = rdtsc();
            g_direct = 0; present_rows(0, g_h);
            uint64_t t1 = rdtsc();
            if (t1 - t0 < tb) tb = t1 - t0;
            if (can) {
                g_direct = 1; present_rows(0, g_h);
                uint64_t t2 = rdtsc();
                if (t2 - t1 < tc) tc = t2 - t1;
            }
        }
#ifdef FORCE_DIRECT
        g_direct = can;
#else
        g_direct = can && tb > tsc_per_ms * 3 && tc * 2 < tb;
#endif
        /* 위: Blt가 3 ms 넘고 직접 복사가 절반 아래일 때만 */
        char *q = info_present;
        const char *t = "screen update: Blt ";
        while (*t) *q++ = *t++;
        uint64_t v[2] = { tb * 10 / tsc_per_ms, can ? tc * 10 / tsc_per_ms : 0 };
        for (int k = 0; k < 2; k++) {
            if (k == 1) { if (!can) break; t = " ms, copy "; while (*t) *q++ = *t++; }
            char nb[12]; int nn = 0; uint64_t x = v[k] / 10;
            do { nb[nn++] = (char)('0' + x % 10); x /= 10; } while (x && nn < 6);
            while (nn) *q++ = nb[--nn];
            *q++ = '.'; *q++ = (char)('0' + v[k] % 10);
        }
        t = g_direct ? " ms -> copy" : " ms -> Blt";
        while (*t) *q++ = *t++;
        *q = 0;
    }
    set_mem_type(dma, 0x200000, 0x18);

    boot_disk = find_boot_disk();
    {
        EFI_PHYSICAL_ADDRESS bb = 0xFFFFFFFFu;
        if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, 8, &bb) == EFI_SUCCESS) bounce = (uint8_t *)(uintptr_t)bb;
        else boot_disk = 0;
    }
    EFI_PHYSICAL_ADDRESS scr = 0xFFFFFFFFu;               /* 상태 저장·소리 녹음 작업 메모리 32 MiB (2.10) */
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (32u << 20) / 4096, &scr) != EFI_SUCCESS) scr = 0;
    EFI_PHYSICAL_ADDRESS xr = 0xFFFFFFFFu;               /* 외장 메모리 매퍼 4 MiB (2.12) */
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (4u << 20) / 4096, &xr) != EFI_SUCCESS) xr = 0;
    EFI_PHYSICAL_ADDRESS v9 = 0xFFFFFFFFu;               /* 4.0.29 V9990(Gfx9000) 2 MiB */
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (2u << 20) / 4096, &v9) != EFI_SUCCESS) v9 = 0;
    EFI_PHYSICAL_ADDRESS vh = 0xFFFFFFFFu;                  /* 3.7.1 가상 하드 24 MiB */
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, (24u << 20) / 4096, &vh) != EFI_SUCCESS) vh = 0;
    EFI_PHYSICAL_ADDRESS wv = 0xFFFFFFFFu;               /* 3.1.0: MoonSound 웨이브 메모리 4 MiB */
    if (BS->AllocatePages(AllocateMaxAddress, EfiLoaderData, OPL4_MEM_SIZE / 4096, &wv) == EFI_SUCCESS) {
        memset((void *)(uintptr_t)wv, 0, OPL4_MEM_SIZE);
        opl4_set_memory((uint8_t *)(uintptr_t)wv, 0);
    }
    EFI_PHYSICAL_ADDRESS vb = 0;                          /* 3.2.13: 영상 녹화 버퍼 (512 → 64 MiB 중 되는 만큼) */
    uint32_t vb_size = 0;
    for (uint32_t mb = 512; mb >= 64; mb /= 2)
        if (alloc_wb((UINTN)mb * 256, &vb) == 1) { vb_size = mb << 20; break; } else if (vb) { BS->FreePages(vb, (UINTN)mb * 256); vb = 0; }   /* WB 자리만 */
    FePlatform p = { .ms = plat_ms, .key_poll = plat_key_poll, .hp_state = hda_headphone, .snd_gpio = hda_gpio_next, .boot_failed = g_boot_failed, .boot_ok = plat_boot_ok, .reboot = plat_reboot, .poweroff = plat_poweroff, .audio = plat_audio, .audio2 = plat_audio2, .video_outputs = plat_video_outputs, .video_rescan = plat_video_rescan, .video_diag = plat_video_diag, .gpu_try = plat_gpu_try, .acpi_mode = plat_acpi_mode, .video_switch = plat_video_switch, .video_lost = plat_video_lost, .hdmi_active = plat_hdmi_active, .audio_select = plat_audio_select, .flush = plat_flush,
                     .info = plat_info, .diag_save = plat_diag_save, .vs_adj = plat_vs_adj, .sound_diag = hda_sound_diag, .silence = hda_silence, .pad_read = pad_usb_read, .pad_view = pad_usb_view, .mouse_read = io_mouse_read, .mouse_dev = plat_mouse_dev, .mouse_set = plat_mouse_set, .midi_read = midi_usb_read, .midi_write = midi_usb_write, .mouse_driver = mouse_usb_driver, .make_file = plat_make_file, .now = efi_now, .list_dir = io_list, .list_dir_all = io_list_all, .read_file = io_read, .write_file = io_write, .write_at = io_write_at, .append = io_append, .create = plat_create, .delete_file = plat_delete,
                     .scratch = (uint8_t *)(uintptr_t)scr, .scratch_size = scr ? 32u << 20 : 0, .vhd_mem = (uint8_t *)(uintptr_t)vh, .vhd_mem_size = vh ? 24u << 20 : 0, .xram = (uint8_t *)(uintptr_t)xr, .v9990 = (void *)(uintptr_t)v9,
                     .vbuf = vb_size ? (uint8_t *)(uintptr_t)vb : 0, .vbuf_size = vb_size,
                     .read_at = io_read_at, .hd_open = plat_hd_open, .hd_close = plat_hd_close, .hd_read = io_hd_read, .hd_write = io_hd_write, .hd_flush = plat_hd_flush,
                     .grow_query = boot_disk ? plat_grow_query : 0, .grow_run = boot_disk ? plat_grow_run : 0 };
    blog("fe_init start");
    fe_init(&d, &p, &m, FE_ROMS, FE_DISP_STORE " > ROMS");
    blog("fe_init done");
    fe_set_default_machine(first_machine);
    {
        char *q = info_screen;
        const char *t = "screen ";
        while (*t) *q++ = *t++;
        uint32_t v[3] = { d.w, d.h, d.scale_x };
        for (int k = 0; k < 3; k++) {
            char num[8]; int nn = 0; uint32_t x = v[k];
            do { num[nn++] = (char)('0' + x % 10); x /= 10; } while (x);
            while (nn) *q++ = num[--nn];
            if (k == 0) *q++ = 'x';
            if (k == 1) { t = " scale x"; while (*t) *q++ = *t++; }
        }
        *q = 0;
    }

    /* 키보드: 펌웨어 PS/2·USB 키보드 드라이버를 떼고 직접 받음 */
    detach_ps2_driver();
    kbc_init();
    usb_watch();                                    /* 3.5.5: 지금 USB 목록을 기준으로 (아래 모듈들이 처음 찾기) */
    int nk = input_init(BS, 1);                     /* 키보드·패드·MIDI (표 input_drv[]) */
    i2c_diag_setup(st, plat_write, plat_create);
    vfct_setup(st);
    i2c_tp_init();                                  /* 3.8.9: I2C 터치패드 찾기 (없으면 바로 돌아옴) */
    input_init(BS, 2);                              /* 마우스 */
    fw_timer_init();
    kbc_timer_init();                               /* 5.0.183 */
    char usb_line[24] = "usb: kbd 0", snd[24];
    usb_line[9] = (char)('0' + nk);
    blog("usb done");
    { extern int64_t vtd_hda_map(EFI_SYSTEM_TABLE *, uintptr_t, uint32_t);   /* 부팅 전 DMA 보호(VT-d) PC — 소리 버퍼를 HDA 에 허락받음 */
      hda_set_dma_dev(vtd_hda_map(st, dma, 0x10000)); blog("vtd map done"); }
    { extern void fe_trace_ext(const char *); hda_trace = fe_trace_ext; }   /* 5.2.23 */
    hda_init(snd, sizeof snd, dma);
    blog("audio done");
    put_line(info_snd, snd);
    if (g_back) {   /* 진단 화면 '영상' 줄 끝에 vsync 상태 (화면 작은 PC에서 아래 줄이 잘려도 보이게) */
        /* 안전장치: 시험 중 PC가 멈추면(그래픽 레지스터 읽기가 문제인 PC) 다음 부팅부터 건너뜀. UEFI 변수에 표시를 남겼다가 끝나면 지움 */
        static EFI_GUID vs_guid = { 0x7e1a6d3b, 0x4c52, 0x4f0e, { 0x9b, 0x12, 0x58, 0x36, 0x4d, 0x53, 0x58, 0x01 } };
        static const CHAR16 vs_name[] = { 'X','8','6','M','S','X','V','S','P','R','O','B','2', 0 };
        EFI_STATUS (*gv)(const CHAR16 *, EFI_GUID *, uint32_t *, UINTN *, void *) = (EFI_STATUS (*)(const CHAR16 *, EFI_GUID *, uint32_t *, UINTN *, void *))ST->RuntimeServices->GetVariable;
        EFI_STATUS (*sv)(const CHAR16 *, EFI_GUID *, uint32_t, UINTN, void *) = (EFI_STATUS (*)(const CHAR16 *, EFI_GUID *, uint32_t, UINTN, void *))ST->RuntimeServices->SetVariable;
        uint8_t mark = 1; UINTN msz = 1; uint32_t mat = 0;
        int skipped = gv && gv(vs_name, &vs_guid, &mat, &msz, &mark) == EFI_SUCCESS;
        int vok = 0;
        if (!skipped) {
            if (sv) sv(vs_name, &vs_guid, 3, 1, &mark);
            vok = vs_probe();
            if (vok) {                                  /* 화면 주기 (Hz x 100) — MSX 프레임과 1.5% 안이면 화면이 박자 */
                if (vs_wait()) { uint64_t m0 = rdtsc(); int k = 0; for (; k < 30; k++) if (!vs_wait()) break;
                    if (k == 30) { uint64_t el = rdtsc() - m0; vs_hz100 = (uint32_t)(tsc_hz * 3000ull / (el ? el : 1)); if (vs_hz100 < 4000 || vs_hz100 > 10000) vs_hz100 = 0; } }
            }
            if (sv) sv(vs_name, &vs_guid, 0, 0, &mark);   /* 지움 */
        }
        char *q = info_wc; while (*q) q++;
        const char *t = skipped ? ", vsync: skipped (probe hung before)" : vok ? (vs_dsl ? (vs_nv ? ", vsync: NVIDIA" : ", vsync: GPU") : ", vsync: VGA") : ", vsync: off";
        while (*t) *q++ = *t++;
        *q = 0;
    }
    fe_set_status_text(info_wc, snd);
    fe_restore();
    fe_set_usb_text(usb_line);

    uint64_t frame_tsc = tsc_hz * 59736 / 3579545;
    uint64_t target = rdtsc(), sec_start = target, busy = 0;
    int frames = 0, last_nk = nk, lock_n = 0;
    for (;;) {
        uint64_t f0 = rdtsc();
        kbc_poll();
        input_poll(0);
        fe_pf_add(5, rdtsc() - f0);
        fe_frame();
        if (g_back) {                                   /* 바뀐 줄만 화면으로 */
            if ((vs_port || vs_dsl) && fe_speed_pct() > 0) { uint64_t w0 = rdtsc(); int wok = vs_wait(); vs_wt += rdtsc() - w0; if (wok) vs_fail = 0; else if (++vs_fail >= 5) { vs_port = 0; vs_dsl = 0; } }   /* 5번 연속 안 오면 끔 */   /* 귀선이 안 오면 끔 */
            uint64_t b0 = rdtsc();
            FeRect rr[8];                               /* 3.5.5: 바뀐 사각형만 */
            int nr = fe_take_dirty_rects(rr, 8);
            for (int k = 0; k < nr; k++) present_rect(rr[k].x0, rr[k].y0, rr[k].x1, rr[k].y1);
            fe_pf_add(6, rdtsc() - b0);
        }
        busy += rdtsc() - f0 - vs_wt; vs_wt = 0;    /* 화면 맞추기 기다린 시간은 일한 시간이 아님 */
        frames++;
        { static int bl_n; if (bl_n < 121 && (++bl_n == 1)) blog("frame 1"); else if (bl_n == 120) blog("frame 120 ok"); }
        int sp = fe_speed_pct();                    /* 속도 %, 0 = 최대 속도 */
        uint32_t fhz = (uint32_t)(357954500ull / fe_frame_tstates());   /* MSX 프레임 Hz x 100 */
        int lock = sp == 100 && g_back && (vs_port || vs_dsl) && vs_hz100 && (vs_hz100 > fhz ? vs_hz100 - fhz : fhz - vs_hz100) * 1000u < fhz * 15u;
        if (lock) lock_n++;
        if (sp == 0 || lock) { usb_watch(); target = rdtsc(); }   /* lock: 화면 맞추기(vs_wait)가 박자 — 두 시계(화면 60.00 Hz, MSX 59.92 Hz)가 엇갈려 가끔 한 장씩 밀리던 것 */
        else {
            frame_tsc = tsc_hz * fe_frame_tstates() / 3579545;   /* 2.14: PAL 기종은 50 Hz */
            target += frame_tsc * 100 / (uint64_t)sp;
            { uint64_t w0 = rdtsc(); usb_watch(); fe_pf_add(7, rdtsc() - w0); }   /* 3.5.5: 남는 시간에 (2초마다) */
            { uint64_t i0 = rdtsc();
            while (rdtsc() < target) {
                kbc_poll();
                input_poll(1);
                __asm__ volatile("pause");
            }
            fe_pf_add(8, rdtsc() - i0); }
        }
        uint64_t now = rdtsc();
        if (now > target + frame_tsc * 4) target = now;
        if (now - sec_start >= tsc_hz) {
            uint64_t el = now - sec_start;
            fe_set_speed((int)((uint64_t)frames * 100 * tsc_hz * 100 / (el * (357954500ull / fe_frame_tstates()))), (int)(busy * 100 / el));
            io_cnt_sec = io_cnt - io_cnt_prev; io_cnt_prev = io_cnt;
            fe_set_vsync(vs_dsl ? "GPU scanline" : vs_port ? "VGA port" : 0, (vs_dsl || vs_port) && g_back ? vs_hz100 : 0, lock_n, frames); lock_n = 0;   /* 5.2.17 */
            sec_start = now; busy = 0; frames = 0;
            bench_tick();                                /* 5.2.28 */
            nk = kbd_usb_count();
            if (nk != last_nk) { usb_line[9] = (char)('0' + nk); fe_set_usb_text(usb_line); last_nk = nk; }
        }
    }
}

/* 네트워크 설정 창(kernel/fe_net.inc)이 부르는 연결부 */
int uefi_net_online(void) { return (g_wifi_cfg || g_lwip_kind) ? net_online() : netuefi_online(&g_netuefi); }
void uefi_net_diag(char *o, int max)                    /* 설정 창 진단 줄 (단계·카드·마지막 UEFI 상태·ConnectController 횟수·주소) */
{
    if (g_wifi_cfg || g_lwip_kind) { net_diag_text(o, max); return; }   /* 5.0.157 */
    static const char H[] = "0123456789ABCDEF";
    const NetUefi *n = &g_netuefi; int k = 0;
    const char *st = n->stage == 2 ? "UP" : n->stage == 1 ? "WAIT" : "IDLE";
    for (const char *p = "stage="; *p && k < max - 1; ) o[k++] = *p++;
    for (const char *p = st; *p && k < max - 1; ) o[k++] = *p++;
    for (const char *p = " nic="; *p && k < max - 1; ) o[k++] = *p++;
    if (k < max - 1) o[k++] = n->cfg ? '1' : '0';
    for (const char *p = " tries="; *p && k < max - 1; ) o[k++] = *p++;
    { unsigned v = (unsigned)n->connect_tries; char t[12]; int m = 0; do { t[m++] = (char)('0' + v % 10); v /= 10; } while (v); while (m && k < max - 1) o[k++] = t[--m]; }
    for (const char *p = " last=0x"; *p && k < max - 1; ) o[k++] = *p++;
    { uint64_t v = (uint64_t)n->last_status; for (int i = 15; i >= 0 && k < max - 1; i--) o[k++] = H[(v >> (i * 4)) & 15]; }
    o[k] = 0;
}
int uefi_net_has_nic(void) { return netuefi_has_nic(&g_netuefi) || usb_drv_present(); }   /* X86MSX 드라이버가 있는 USB 랜 포함 */
int uefi_net_wifi_live(void) { return g_wifi_cfg; }   /* 5.0.157 */
int uefi_net_ipinfo(uint8_t out[20]) { return (g_wifi_cfg || g_lwip_kind) ? net_ipinfo(out) : netuefi_get_ipinfo(&g_netuefi, out); }
void uefi_net_config(int manual, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], const uint8_t dns[4]) { if (g_wifi_cfg || g_lwip_kind) net_config(manual, ip, mask, gw, dns); else netuefi_set_config(&g_netuefi, manual, ip, mask, gw, dns); }
/* 무선: 펌웨어의 Wi-Fi 지원 호출은 Lan 쪽(v0.7)에서 받아 붙인다. 지금은 "무선 어댑터 없음"으로 보인다. */
/* 무선 어댑터 = PCI 클래스 0280xx(기타 네트워크 컨트롤러, Wi-Fi 카드). 노트북 무선이 꺼져(Fn 키·비행기 모드) 있어도 카드는 보이므로
 * 무선 항목이 사라지지 않고, 켜는 자리가 남는다. 설정 공간 읽기만 하고 카드를 건드리지 않는다. */
static uint32_t wf_cfg(int b, int d, int f, int off)
{
    uint32_t a = 0x80000000u | (uint32_t)(b << 16) | (uint32_t)(d << 11) | (uint32_t)(f << 8) | (uint32_t)(off & 0xFC), v;
    __asm__ volatile("outl %0, %1" : : "a"(a), "Nd"((uint16_t)0xCF8));
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)0xCFC));
    return v;
}
int uefi_wifi_present(void)
{
    static int found = -1;
    if (found >= 0) return found;
    found = 0;
    for (int b = 0; b < 256 && !found; b++)
        for (int d = 0; d < 32 && !found; d++) {
            uint32_t id = wf_cfg(b, d, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            int mf = (wf_cfg(b, d, 0, 0x0C) >> 16) & 0x80 ? 8 : 1;
            for (int f = 0; f < mf && !found; f++) {
                if ((wf_cfg(b, d, f, 0) & 0xFFFF) == 0xFFFF) continue;
                { uint32_t id = wf_cfg(b, d, f, 0); const NicId *e = nicdb_pci((uint16_t)id, (uint16_t)(id >> 16), wf_cfg(b, d, f, 0x08) >> 16); if (e && e->kind == NK_WIFI) found = 1; }   /* 표로 판단 */
            }
        }
    return found;
}
/* USB 랜 어댑터 찾기. CDC 이더넷(ECM 02/06, NCM 02/0D)이거나, 제조사 전용(FF) 인터페이스 중 알려진 USB 랜 칩.
 * 알려진 칩: Realtek RTL8152/8153/8155/8156(0BDA:8050·8152·8153·8155·8156, TP-Link 2357:0601, Lenovo 17EF:xxxx, Samsung 04E8:A101),
 *            ASIX AX88772/AX88178/AX88179(0B95:xxxx), Microchip LAN78xx(0424:7800·7850), SMSC LAN95xx(0424:ec00·9500·9e00) 등 */
static int usb_is_ether(uint16_t v, uint16_t p, uint8_t c, uint8_t sc)   /* 판단은 랜카드 표(kernel/net/nicdb.c)가 한다 */
{
    const NicId *e = nicdb_usb(v, p, c, sc);
    return e && e->kind == NK_WIRED;
}
static int usb_ether_present(void)
{
    static int last = -1, calls;
    if (last >= 0 && (calls++ % 90) != 0) return last;                   /* 매 프레임 훑지 않고 가끔만 (꽂으면 곧 나타남) */
    EFI_HANDLE *hs = 0; UINTN cnt = 0; int found = 0;
    extern void net_log(const char *s);
    static const char hx[] = "0123456789ABCDEF";
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &cnt, &hs) != EFI_SUCCESS) cnt = 0;
    for (UINTN j = 0; j < cnt; j++) {
        EFI_USB_IO_PROTOCOL *io; EFI_USB_DEVICE_DESCRIPTOR dd; EFI_USB_INTERFACE_DESCRIPTOR ifd;
        if (BS->HandleProtocol(hs[j], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
        memset(&dd, 0, sizeof dd);
        if (io->UsbGetDeviceDescriptor) io->UsbGetDeviceDescriptor(io, &dd);
        if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;
        if (usb_is_ether(dd.IdVendor, dd.IdProduct, ifd.InterfaceClass, ifd.InterfaceSubClass)) found = 1;
    }
    if (hs) BS->FreePool(hs);
    if (found != last) {
        char t[48]; int k = 0; const char *a = found ? "NET.usb ethernet adapter found" : "NET.usb ethernet adapter none";
        while (*a) t[k++] = *a++; t[k] = 0; (void)hx; net_log(t);
    }
    last = found;
    return found;
}
/* 유선 랜카드 = PCI 클래스 0200(이더넷) 또는 USB 랜 어댑터 또는 펌웨어가 IP 설정을 받아준 랜카드 */
int uefi_wired_present(void)
{
    static int found = -1;
    if (found >= 0) return found || usb_ether_present() || netuefi_has_nic(&g_netuefi);
    found = 0;
    extern void net_log(const char *s);
    static const char hx[] = "0123456789ABCDEF";
    int nall = 0, nnet = 0;
    for (int b = 0; b < 256; b++)                        /* 다 훑고 PCI 장치 목록을 NET 로그에 (유선이 안 보이는 노트북 진단) */
        for (int d = 0; d < 32; d++) {
            if ((wf_cfg(b, d, 0, 0) & 0xFFFF) == 0xFFFF) continue;
            int mf = (wf_cfg(b, d, 0, 0x0C) >> 16) & 0x80 ? 8 : 1;
            for (int f = 0; f < mf; f++) {
                uint32_t id = wf_cfg(b, d, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                uint32_t cls = wf_cfg(b, d, f, 0x08) >> 8, hdr = (wf_cfg(b, d, f, 0x0C) >> 16) & 0x7F;
                const NicId *ne = nicdb_pci((uint16_t)id, (uint16_t)(id >> 16), cls >> 8);   /* 랜카드 표 */
                if (ne && ne->kind == NK_WIRED) found = 1;
                nall++; if ((cls >> 16) == 0x02) nnet++;
                char t[176]; int k = 0;
                const char *pre = "NET.pci "; while (*pre) t[k++] = *pre++;
                t[k++] = hx[(b >> 4) & 15]; t[k++] = hx[b & 15]; t[k++] = ':'; t[k++] = hx[(d >> 4) & 1]; t[k++] = hx[d & 15]; t[k++] = '.'; t[k++] = hx[f & 7];
                t[k++] = ' '; for (int s2 = 12; s2 >= 0; s2 -= 4) t[k++] = hx[(id >> s2) & 15]; t[k++] = ':'; for (int s2 = 28; s2 >= 16; s2 -= 4) t[k++] = hx[(id >> s2) & 15];
                const char *c = " class="; while (*c) t[k++] = *c++; for (int s2 = 20; s2 >= 0; s2 -= 4) t[k++] = hx[(cls >> s2) & 15];
                if (hdr == 1) {                          /* 브리지: 뒤쪽 버스 번호 (전원이 꺼진 장치가 숨는 자리) */
                    uint32_t bn = wf_cfg(b, d, f, 0x18); const char *q = " bus="; while (*q) t[k++] = *q++;
                    t[k++] = hx[(bn >> 12) & 15]; t[k++] = hx[(bn >> 8) & 15]; t[k++] = '-'; t[k++] = hx[(bn >> 20) & 15]; t[k++] = hx[(bn >> 16) & 15];
                }
                if (ne) {                                /* 표에 있는 랜카드면 종류·칩·드라이버 */
                    const char *q = ne->kind == NK_WIFI ? " nic=wifi chip=" : " nic=wired chip="; while (*q && k < 150) t[k++] = *q++;
                    t[k++] = '"'; for (q = ne->chip; *q && k < 160; q++) t[k++] = *q; t[k++] = '"';
                    q = ne->drv ? " drv=yes" : " drv=none"; while (*q && k < 174) t[k++] = *q++;
                }
                t[k] = 0; net_log(t);
            }
        }
    {   char t[64]; int k = 0; const char *a = "NET.pci total="; while (*a) t[k++] = *a++;
        t[k++] = (char)('0' + nall / 100 % 10); t[k++] = (char)('0' + nall / 10 % 10); t[k++] = (char)('0' + nall % 10);
        a = " net="; while (*a) t[k++] = *a++; t[k++] = (char)('0' + nnet / 10 % 10); t[k++] = (char)('0' + nnet % 10);
        a = " wired="; while (*a) t[k++] = *a++; t[k++] = (char)('0' + found); t[k] = 0; net_log(t); }
    return found || usb_ether_present() || netuefi_has_nic(&g_netuefi);
}
int uefi_wifi_scan_begin(void) { return g_wifi_cfg ? rtwdet_ui_scan_begin() : -1; }   /* 5.0.163 */
int uefi_wifi_scanning(void) { return g_wifi_cfg ? rtwdet_ui_scanning() : 0; }
void uefi_wifi_scan_stop(void) { if (g_wifi_cfg) rtwdet_ui_scan_stop(); }
int uefi_wifi_scan_get(char ssid[][36], int *sig, int *sec, int max) { return g_wifi_cfg ? rtwdet_ui_scan_get(ssid, sig, sec, max) : -1; }
int uefi_wifi_connect(const char *ssid, const char *pass) { return g_wifi_cfg ? rtwdet_ui_connect(ssid, pass) : -1; }
int uefi_wifi_state(void) { return g_wifi_cfg ? rtwdet_ui_state() : 0; }
