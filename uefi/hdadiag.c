/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
#include "hdadiag.h"
#include <stdarg.h>
#include <stddef.h>

extern int hda_pin_fix, hda_pin_last;
static char lb[32768];
static uint32_t ln;
static const HdaDiagIo *io;
static void put(const char *f, ...)
{
    va_list ap; va_start(ap, f);
    for (; *f && ln < sizeof lb - 80; f++) {
        if (*f != '%') { lb[ln++] = *f; continue; }
        f++;
        if (*f == 's') { const char *s = va_arg(ap, const char *); while (*s && ln < sizeof lb - 80) lb[ln++] = *s++; }
        else if (*f == 'x' || *f == 'd') {
            int dec = (*f == 'd'), w = 0;
            if (f[1] >= '0' && f[1] <= '9') { w = f[1] - '0'; f++; }
            uint32_t v = va_arg(ap, uint32_t), base = dec ? 10 : 16;
            char t[12]; int n = 0;
            do { t[n++] = "0123456789ABCDEF"[v % base]; v /= base; } while (v);
            while (n < w) t[n++] = '0';
            while (n) lb[ln++] = t[--n];
        }
    }
    va_end(ap);
}
static void flush(void) { io->logw("0:\\LOG\\HDA_DIAG.TXT", (const uint8_t *)lb, ln); }

static volatile uint8_t *mm;
static uint32_t r32(uint32_t o) { return *(volatile uint32_t *)(mm + o); }
static void w32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(mm + o) = v; }
static uint16_t r16(uint32_t o) { return *(volatile uint16_t *)(mm + o); }
static void w16(uint32_t o, uint16_t v) { *(volatile uint16_t *)(mm + o) = v; }

/* 즉시 명령 인터페이스 (ICOI 0x60, ICII 0x64, ICIS 0x68) */
static int verb(uint32_t cad, uint32_t nid, uint32_t v12, uint32_t pl, uint32_t *res)
{
    uint32_t cmd = (cad << 28) | (nid << 20) | (v12 << 8) | (pl & 0xFF);
    for (int i = 0; i < 2000 && (r16(0x68) & 1); i++) io->stall_us(10);
    if (r16(0x68) & 1) return -1;
    w16(0x68, 2);                                   /* IRV 지움 */
    w32(0x60, cmd);
    w16(0x68, 1);                                   /* ICB */
    for (int i = 0; i < 3000; i++) {
        uint16_t s = r16(0x68);
        if (!(s & 1) && (s & 2)) { *res = r32(0x64); return 0; }
        io->stall_us(10);
    }
    return -1;
}
static uint32_t get(uint32_t cad, uint32_t nid, uint32_t v12, uint32_t pl)
{
    uint32_t r = 0xDEAD0000;
    if (verb(cad, nid, v12, pl, &r)) return 0xFFFFFFFF;
    return r;
}

static const char *wtype(uint32_t t)
{
    static const char *n[] = { "AudioOut", "AudioIn", "Mixer", "Selector", "Pin", "Power", "VolKnob", "BeepGen" };
    return t < 8 ? n[t] : "?";
}

static void codec(uint32_t cad)
{
    put("-- codec %d: vendor/device %x8  rev %x8\n", cad, get(cad, 0, 0xF00, 0), get(cad, 0, 0xF00, 2));
    uint32_t sub = get(cad, 0, 0xF00, 4);
    uint32_t fg0 = (sub >> 16) & 0xFF, fgn = sub & 0xFF;
    for (uint32_t f = fg0; f < fg0 + fgn && f < 8; f++) {
        uint32_t ft = get(cad, f, 0xF00, 5) & 0xFF;
        put("  function group nid %d type %d %s\n", f, ft, ft == 1 ? "(Audio)" : "");
        if (ft != 1) continue;
        { uint32_t z; verb(cad, f, 0x705, 0, &z); }   /* D0 */
        uint32_t ws = get(cad, f, 0xF00, 4), w0 = (ws >> 16) & 0xFF, wn = ws & 0xFF;
        put("  widgets %d..%d\n", w0, w0 + wn - 1);
        for (uint32_t n = w0; n < w0 + wn && n < w0 + 40; n++) {
            uint32_t caps = get(cad, n, 0xF00, 9), t = (caps >> 20) & 15;
            put("  nid %d %s caps=%x8", n, wtype(t), caps);
            if (t == 0) put("  PCMrates=%x8 fmts=%x8", get(cad, n, 0xF00, 0xA), get(cad, n, 0xF00, 0xB));
            if (t == 0) put("  fmt=%x4 dig=%x8 chcnt=%x8 pwr=%x8", get(cad, n, 0xA00, 0) & 0xFFFF, get(cad, n, 0xF0D, 0), get(cad, n, 0xF2D, 0), get(cad, n, 0xF05, 0));
            if (t == 4) {
                uint32_t pc = get(cad, n, 0xF00, 0xC), cfg = get(cad, n, 0xF1C, 0);
                uint32_t sense = get(cad, n, 0xF09, 0);
                put("  pincap=%x8 cfg=%x8 sense=%x8 ctl=%x8", pc, cfg, sense, get(cad, n, 0xF07, 0));
                put("  pwr=%x8 eapd=%x8 dipsize=%x8 xmit=%x8", get(cad, n, 0xF05, 0), get(cad, n, 0xF0C, 0), get(cad, n, 0xF2E, 0), get(cad, n, 0xF32, 0));
                if (cad == 2) {                              /* 4.0.79: 핀 제어값 쓰기 시험 (쓰고 바로 읽음) */
                    uint32_t z, v0 = 0;
                    verb(cad, n, 0x705, 0, &z);              /* 핀 D0 */
                    put("\n    probe: dipsize(ELD)=%x8 eld0=%x8 eld1=%x8", get(cad, n, 0xF2E, 8), get(cad, n, 0xF2F, 0), get(cad, n, 0xF2F, 1));
                    static const uint8_t t[] = { 0x40, 0x60, 0xC0, 0x20, 0x80, 0x01, 0x00, 0x40 };
                    for (unsigned k = 0; k < sizeof t; k++) { verb(cad, n, 0x707, t[k], &z); v0 = get(cad, n, 0xF07, 0); put(" w%x2->r%x2", t[k], v0 & 0xFF); }
                    put("\n    probe: afgpwr=%x8 pinpwr=%x8 unsol=%x8 pwrcaps=%x8", get(cad, 1, 0xF05, 0), get(cad, n, 0xF05, 0), get(cad, n, 0xF08, 0), get(cad, n, 0xF00, 0xF));
                }
                if ((sense >> 30) & 1) {                    /* ELD 유효 */
                    put("\n    ELD:");
                    for (uint32_t i = 0; i < 48; i++) { uint32_t e = get(cad, n, 0xF2F, i); if (!(e >> 31)) break; put(" %x2", e & 0xFF); }
                }
            }
            put("  conn=%x8 stream=%x8\n", get(cad, n, 0xF00, 0xE), get(cad, n, 0xF06, 0));
        }
    }
}

static const struct { uint32_t o; const char *n; } disp[] = {
    { 0x65000, "AUD_CONFIG_A" }, { 0x65100, "AUD_CONFIG_B" }, { 0x65020, "AUD_VID_DID" }, { 0x65050, "AUD_PIN_BUF_CFG" },
    { 0x65110, "AUD_MISC_CTRL_B" }, { 0x650C0, "AUD_PIN_ELD_CP_VLD" }, { 0x650D4, "AUD_HDMIW_STATUS" },
    { 0x650B4, "AUD_DIP_ELD_CTRL_ST_A" }, { 0x651B4, "AUD_DIP_ELD_CTRL_ST_B" }, { 0x65F10, "AUD_CHICKENBIT" }, { 0x46400, "HSW_AUD_CFG_A?" },
    { 0x45400, "HSW_PWR_WELL_CTL1" }, { 0x45404, "HSW_PWR_WELL_CTL2" }, { 0x46040, "AUD_FREQ_CNTRL" },
};

int hdadiag_run(const HdaDiagIo *p)
{
    io = p; ln = 0;
    put("X86MSX HDA_DIAG (HDMI sound)\n");
    int found = 0;
    uint64_t ibase = 0;
    { uint32_t id = io->pci_rd(0, 2, 0, 0);
      if ((id & 0xFFFF) == 0x8086) { ibase = io->pci_rd(0, 2, 0, 0x10) & ~0xFull; if ((io->pci_rd(0, 2, 0, 0x10) & 6) == 4) ibase |= (uint64_t)io->pci_rd(0, 2, 0, 0x14) << 32;
        put("Intel GPU 00:02.0 id=%x8 MMIO=%x8\n", id, (uint32_t)ibase); } }
    for (int b = 0; b < 16; b++) for (int d = 0; d < 32; d++) for (int f = 0; f < 8; f++) {
        uint32_t id = io->pci_rd(b, d, f, 0);
        if ((id & 0xFFFF) == 0xFFFF) { if (!f) break; continue; }
        uint32_t cls = io->pci_rd(b, d, f, 8) >> 8;
        if ((cls >> 8) != 0x0403) continue;                 /* HD Audio */
        found++;
        uint32_t bar = io->pci_rd(b, d, f, 0x10), bh = (bar & 6) == 4 ? io->pci_rd(b, d, f, 0x14) : 0;
        put("\n== HDA %x2:%x2.%d id=%x8 cmd=%x4 class=%x6 BAR0=%x8:%x8\n", b, d, f, id, io->pci_rd(b, d, f, 4) & 0xFFFF, cls, bh, bar);
        if (bh || !(bar & ~0xFu) || (bar & 1)) { put("  (BAR 64bit/IO — skip)\n"); continue; }
        if (!(io->pci_rd(b, d, f, 4) & 2)) { put("  memory decode off — skip\n"); continue; }
        mm = (volatile uint8_t *)(uintptr_t)(bar & ~0xFu);
        put("  GCAP=%x4 VMIN/VMAJ=%x4 GCTL=%x8 STATESTS=%x4 INTSTS=%x8\n", r16(0), r16(2), r32(8), r16(0x0E), r32(0x24));
        if (!(r32(8) & 1)) {                                 /* 리셋 상태: 풀기 */
            w32(8, r32(8) | 1);
            for (int i = 0; i < 1000 && !(r32(8) & 1); i++) io->stall_us(100);
            io->stall_us(2000);                              /* 코덱 깨어나기 (>=521us) */
            put("  CRST released: GCTL=%x8 STATESTS=%x4\n", r32(8), r16(0x0E));
        }
        { uint32_t iss = (r16(0) >> 8) & 15, sd = 0x80 + iss * 0x20;
          put("  output stream SD@%x3: CTL=%x6 STS=%x2 LPIB=%x8 CBL=%x8 LVI=%x4 FMT=%x4 DMAPOS=%x8\n", sd, r32(sd) & 0xFFFFFF, *(volatile uint8_t *)(mm + sd + 3), r32(sd + 4), r32(sd + 8), r16(sd + 0x0C), r16(sd + 0x12), r32(0x70)); 
          put("  hdmi pin ctl rewrites=%d lastread=%x2\n", (uint32_t)hda_pin_fix, (uint32_t)(hda_pin_last & 0xFF)); }
        uint16_t st = r16(0x0E);
        for (uint32_t c = 0; c < 15; c++) if (st & (1u << c)) codec(c);
        if (!st) put("  no codec reported\n");
    }
    if (!found) put("no HD Audio controller found\n");
    if (ibase) {
        volatile uint32_t *g = (volatile uint32_t *)(uintptr_t)ibase;
        put("\n== Intel display audio regs\n");
        for (unsigned i = 0; i < sizeof disp / sizeof disp[0]; i++) put("  %s [%x5] = %x8\n", disp[i].n, disp[i].o, g[disp[i].o / 4]);
    }
    if (ibase && mm && (r16(0x0E) & 4)) {                  /* 4.0.80: 코덱 2 (HDMI) 핀 쓰기가 막히는 원인을 찾는 실험들 — 단계마다 ELD 크기·핀 쓰기 결과를 찍음 */
        volatile uint32_t *g = (volatile uint32_t *)(uintptr_t)ibase;
        uint32_t z;
        #define PRB(tag) do { verb(2, 3, 0x707, 0x40, &z); put("  exp %s: eldsize=%x8 eld0=%x8 sense=%x8 ctl(after w40)=%x2 pwr=%x8\n", tag, get(2, 3, 0xF2E, 8), get(2, 3, 0xF2F, 0), get(2, 3, 0xF09, 0), get(2, 3, 0xF07, 0) & 0xFF, get(2, 3, 0xF05, 0)); } while (0)
        put("\n== experiments (codec 2 pin nid 3)\n");
        put("  devlist(param 0x15) nid3=%x8 devsel=%x8\n", get(2, 3, 0xF00, 0x15), get(2, 3, 0xF01, 0));   /* 4.0.81: 핀 하나에 장치(디바이스 엔트리) 여러 개일 수 있음 — 감지·ELD·핀제어가 장치별 */
        for (uint32_t dv = 0; dv < 6; dv++) {
            verb(2, 3, 0x715, dv, &z);
            put("  dev %d: sel=%x8 sense=%x8 eldsize=%x8 eld0=%x8", dv, get(2, 3, 0xF01, 0), get(2, 3, 0xF09, 0), get(2, 3, 0xF2E, 8), get(2, 3, 0xF2F, 0));
            verb(2, 3, 0x707, 0x40, &z); put(" ctl(after w40)=%x2\n", get(2, 3, 0xF07, 0) & 0xFF);
        }
        verb(2, 3, 0x715, 0, &z);
        PRB("base");
        g[0x65F10 / 4] &= ~(1u << 15); io->stall_us(1500); g[0x65F10 / 4] |= (1u << 15); io->stall_us(1500);
        PRB("wake-toggle");
        verb(2, 1, 0x705, 3, &z); verb(2, 3, 0x705, 3, &z); io->stall_us(10000);
        verb(2, 1, 0x705, 0, &z); verb(2, 3, 0x705, 0, &z); verb(2, 2, 0x705, 0, &z); io->stall_us(10000);
        PRB("D3->D0");
        { uint32_t v = g[0x650C0 / 4]; g[0x650C0 / 4] = v & ~0x10u; io->stall_us(2000); g[0x650C0 / 4] = v | 0x10u; io->stall_us(5000); }
        PRB("eld-valid-toggle");
        { uint32_t v = g[0x650C0 / 4]; g[0x650C0 / 4] = v & ~0x50u; io->stall_us(5000); g[0x650C0 / 4] = (v & ~0x10u) | 0x40u; io->stall_us(5000); g[0x650C0 / 4] = v | 0x50u; io->stall_us(20000); }
        PRB("full-reenable");
        { uint32_t v = g[0x650C0 / 4]; g[0x650C0 / 4] = v | 0x05u; io->stall_us(20000); }
        PRB("+pipeA bits");
        { uint32_t v = g[0x650C0 / 4]; g[0x650C0 / 4] = v & ~0x05u; }
        { uint32_t c = g[0x651B4 / 4]; g[0x651B4 / 4] = c & ~(0x1Fu << 5); io->stall_us(2000); put("  ELD_CTRL_B %x8 -> %x8\n", c, g[0x651B4 / 4]); }
        PRB("final");
        #undef PRB
    }
    flush();
    return found;
}
