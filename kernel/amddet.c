/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* amddet.c - 1단계 읽기 전용 PCI 열거 (설명 amddet.h). CF8/CFC 직접 접근이라 UEFI·레거시 공용 */
#include "amddet.h"
#include <stdint.h>

static inline void p_outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t p_inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static uint32_t pcfg(int b, int d, int f, int off)
{
    p_outl(0xCF8, 0x80000000u | (uint32_t)(b << 16) | (uint32_t)(d << 11) | (uint32_t)(f << 8) | (uint32_t)(off & 0xFC));
    return p_inl(0xCFC);
}

static void hx(char *o, uint32_t v, int digits)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = 0; i < digits; i++) o[i] = H[(v >> ((digits - 1 - i) * 4)) & 15];
}

typedef struct { uint8_t b, d, f; uint16_t vid, did; uint32_t cls; } PciDev;
static PciDev disp[8];
static int ndisp, namd;
static PciDev wifi[4];
static int nwifi;
static int scanned;

static void scan(void)
{
    if (scanned) return;
    scanned = 1;
    for (int b = 0; b < 256; b++)
        for (int d = 0; d < 32; d++)
            for (int f = 0; f < 8; f++) {
                uint32_t id = pcfg(b, d, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (f == 0) break; continue; }
                uint32_t cls = pcfg(b, d, f, 0x08) >> 8;
                if ((cls >> 16) == 0x03) {
                    if (ndisp < 8) {
                        disp[ndisp].b = (uint8_t)b; disp[ndisp].d = (uint8_t)d; disp[ndisp].f = (uint8_t)f;
                        disp[ndisp].vid = (uint16_t)(id & 0xFFFF); disp[ndisp].did = (uint16_t)(id >> 16);
                        disp[ndisp].cls = cls & 0xFFFFFFu;
                        if ((id & 0xFFFF) == 0x1022 || (id & 0xFFFF) == 0x1002) namd = 1;
                        ndisp++;
                    }
                } else if ((cls >> 16) == 0x02 && ((cls >> 8) & 0xFF) == 0x80) {
                    if (nwifi < 4) {
                        wifi[nwifi].b = (uint8_t)b; wifi[nwifi].d = (uint8_t)d; wifi[nwifi].f = (uint8_t)f;
                        wifi[nwifi].vid = (uint16_t)(id & 0xFFFF); wifi[nwifi].did = (uint16_t)(id >> 16);
                        wifi[nwifi].cls = cls & 0xFFFFFFu;
                        nwifi++;
                    }
                }
                if (f == 0 && !(pcfg(b, d, f, 0x0C) & 0x00800000u)) break;
            }
}

/* "gpu 01:00.0 1002:1636 cls030000 *AMD" (표시 1줄/장치, 최대 3) */
static int emit_dev(char *l, const PciDev *v, int is_amd)
{
    int n = 0;
    const char *t = "gpu ";
    while (*t) l[n++] = *t++;
    hx(l + n, v->b, 2); n += 2; l[n++] = ':';
    hx(l + n, v->d, 2); n += 2; l[n++] = '.';
    l[n++] = (char)('0' + v->f);
    l[n++] = ' ';
    hx(l + n, v->vid, 4); n += 4; l[n++] = ':';
    hx(l + n, v->did, 4); n += 4; l[n++] = ' ';
    t = "cls";
    while (*t) l[n++] = *t++;
    hx(l + n, v->cls, 6); n += 6;
    if (is_amd) { t = " *AMD"; while (*t) l[n++] = *t++; }
    l[n] = 0;
    return n;
}

int amddet_info(char (*lines)[64], int max)
{
    int k = 0;
    scan();
    int shown = 0;
    for (int i = 0; i < ndisp && k < max && shown < 3; i++, shown++) {
        emit_dev(lines[k], &disp[i], disp[i].vid == 0x1022 || disp[i].vid == 0x1002);
        k++;
    }
    if (!ndisp && k < max) {
        const char *t = "gpu: none (class 03)";
        int n = 0;
        while (*t) lines[k][n++] = *t++;
        lines[k][n] = 0;
        k++;
    }
    /* 첫 AMD 그래픽(없으면 첫 그래픽)의 BAR0~3 원시값 + 명령 레지스터 */
    const PciDev *g = 0;
    for (int i = 0; i < ndisp; i++) if (disp[i].vid == 0x1022 || disp[i].vid == 0x1002) { g = &disp[i]; break; }
    if (!g && ndisp) g = &disp[0];
    if (g && k < max) {
        char *l = lines[k];
        int n = 0;
        const char *t = " BAR";
        while (*t) l[n++] = *t++;
        for (int r = 0; r < 4; r++) {
            uint32_t v = pcfg(g->b, g->d, g->f, 0x10 + r * 4);
            l[n++] = (char)('0' + r);
            l[n++] = '=';
            hx(l + n, v, 8); n += 8;
            l[n++] = ' ';
        }
        t = "CMD=";
        while (*t) l[n++] = *t++;
        hx(l + n, pcfg(g->b, g->d, g->f, 0x04) & 0xFFFFu, 4); n += 4;
        l[n] = 0;
        k++;
        if (k < max) {                                  /* 4.0.34: BAR4·5 (AMD는 BAR5 = 레지스터 창) */
            l = lines[k]; n = 0;
            t = " BAR4=";
            while (*t) l[n++] = *t++;
            hx(l + n, pcfg(g->b, g->d, g->f, 0x20), 8); n += 8;
            t = " 5=";
            while (*t) l[n++] = *t++;
            hx(l + n, pcfg(g->b, g->d, g->f, 0x24), 8); n += 8;
            l[n] = 0;
            k++;
        }
    }
    if (!namd && k < max) {
        const char *t = "note: no AMD display (1022)";
        int n = 0;
        while (*t) lines[k][n++] = *t++;
        lines[k][n] = 0;
        k++;
    }
    return k;
}

/* 4.0.34: 진단 8번 탭(WiFi): 무선랜 PCI 정보 (읽기 전용). 줄은 모두 "wifi"로 시작 */
static const char *wifi_name(uint16_t v, uint16_t d)
{
    if (v == 0x10EC) switch (d) { case 0xC822: return "Realtek RTL8822CE"; case 0xB822: return "Realtek RTL8822BE"; case 0x8821: return "Realtek RTL8821CE"; case 0xC821: return "Realtek RTL8821CE"; case 0x8852: return "Realtek RTL8852AE"; case 0xB852: return "Realtek RTL8852BE"; case 0x8723: return "Realtek RTL8723BE"; case 0xC82F: return "Realtek RTL8822CE"; default: return "Realtek"; }
    if (v == 0x8086) return "Intel Wi-Fi";
    if (v == 0x14C3) return "MediaTek";
    if (v == 0x168C) return "Qualcomm Atheros";
    if (v == 0x17CB) return "Qualcomm";
    if (v == 0x14E4) return "Broadcom";
    return "unknown";
}
static int put(char *l, int n, const char *t) { while (*t) l[n++] = *t++; return n; }
int amddet_wifi_info(char (*lines)[64], int max)
{
    int k = 0;
    scan();
    for (int i = 0; i < nwifi && k + 4 <= max; i++) {
        const PciDev *w = &wifi[i];
        char *l = lines[k]; int n = 0;
        n = put(l, n, "wifi "); l[n++] = (char)('0' + i); l[n++] = ' ';
        hx(l + n, w->b, 2); n += 2; l[n++] = ':'; hx(l + n, w->d, 2); n += 2; l[n++] = '.'; l[n++] = (char)('0' + w->f); l[n++] = ' ';
        hx(l + n, w->vid, 4); n += 4; l[n++] = ':'; hx(l + n, w->did, 4); n += 4;
        n = put(l, n, " rev "); hx(l + n, pcfg(w->b, w->d, w->f, 0x08) & 0xFF, 2); n += 2; l[n] = 0; k++;
        l = lines[k]; n = put(lines[k], 0, "wifi "); l[n++] = (char)('0' + i); n = put(l, n, " name "); n = put(l, n, wifi_name(w->vid, w->did)); l[n] = 0; k++;
        l = lines[k]; n = put(l, 0, "wifi "); l[n++] = (char)('0' + i);
        n = put(l, n, " CMD="); hx(l + n, pcfg(w->b, w->d, w->f, 0x04) & 0xFFFF, 4); n += 4;
        n = put(l, n, " BAR0="); hx(l + n, pcfg(w->b, w->d, w->f, 0x10), 8); n += 8;
        n = put(l, n, " BAR2="); hx(l + n, pcfg(w->b, w->d, w->f, 0x18), 8); n += 8; l[n] = 0; k++;
        /* 구성 공간의 기능 목록에서 PCIe(0x10)를 찾아 링크 상태 (속도 세대·폭) */
        l = lines[k]; n = put(l, 0, "wifi "); l[n++] = (char)('0' + i);
        uint32_t ss = pcfg(w->b, w->d, w->f, 0x2C);
        n = put(l, n, " subsys "); hx(l + n, ss & 0xFFFF, 4); n += 4; l[n++] = ':'; hx(l + n, ss >> 16, 4); n += 4;
        uint32_t st = pcfg(w->b, w->d, w->f, 0x04) >> 16;
        if (st & 0x10) {
            uint32_t cp = pcfg(w->b, w->d, w->f, 0x34) & 0xFC;
            for (int g = 0; g < 48 && cp >= 0x40; g++) {
                uint32_t c = pcfg(w->b, w->d, w->f, (int)cp);
                if ((c & 0xFF) == 0x10) {
                    uint32_t ls = pcfg(w->b, w->d, w->f, (int)cp + 0x10) >> 16;
                    n = put(l, n, " PCIe Gen"); l[n++] = (char)('0' + (ls & 15)); n = put(l, n, " x");
                    uint32_t wd = (ls >> 4) & 63; if (wd >= 10) l[n++] = (char)('0' + wd / 10); l[n++] = (char)('0' + wd % 10);
                    break;
                }
                cp = (c >> 8) & 0xFC;
            }
        }
        l[n] = 0; k++;
    }
    if (!nwifi && k < max) { const char *t = "wifi: none (PCI class 0280 not found)"; int n = put(lines[k], 0, t); lines[k][n] = 0; k++; }
    return k;
}
