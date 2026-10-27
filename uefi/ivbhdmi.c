/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ivbhdmi.c - DisplaySelect v39 ivb_dual.inc / ivb_audio.inc 를 그대로 옮김 (순서·값 변경 없음) */
#include "ivbhdmi.h"

static void (*stall)(uint32_t);
static void (*logw)(const char *, const uint8_t *, uint32_t);
static volatile uint8_t *gm;
static uint32_t devid;
static int mapped, state;                   /* state 1 = 내장 패널을 내가 켬 */
static char lb[6000]; static uint32_t ln;
static void sp(const char *t) { while (*t && ln < sizeof lb - 2) lb[ln++] = *t++; }
static void sh(uint32_t v) { static const char H[] = "0123456789ABCDEF"; for (int i = 7; i >= 0 && ln < sizeof lb - 2; i--) lb[ln++] = H[(v >> (i * 4)) & 15]; }
static void rg(const char *n, uint32_t o, uint32_t v) { sp("  "); sp(n); sp(" ["); sh(o); sp("] = "); sh(v); sp("\r\n"); }
static void flush(void) { if (logw) logw("0:\\LOG\\IVB_HDMI.TXT", (const uint8_t *)lb, ln); }
static uint32_t rd32(uint32_t o) { return *(volatile uint32_t *)(gm + o); }
static void wr32(uint32_t o, uint32_t v) { *(volatile uint32_t *)(gm + o) = v; }
static uint32_t pci_rd(int off)
{
    __asm__ volatile("outl %0, %1" : : "a"(0x80000000u | (2u << 11) | (uint32_t)(off & 0xFC)), "Nd"((uint16_t)0xCF8));
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)0xCFC)); return v;
}
static int wait_bits(uint32_t o, uint32_t mask, uint32_t val, int ms)
{
    for (int i = 0; i <= ms; i++) { if ((rd32(o) & mask) == val) return 1; stall(1000); }
    return (rd32(o) & mask) == val;
}
void ivbhdmi_init(void (*s)(uint32_t), void (*l)(const char *, const uint8_t *, uint32_t)) { stall = s; logw = l; }

static int map(void)
{
    if (mapped) return mapped > 0;
    mapped = -1;
    uint32_t id = pci_rd(0), cmd = pci_rd(4);
    if ((id & 0xFFFF) != 0x8086 || (pci_rd(8) >> 24) != 0x03) return 0;
    uint32_t d = id >> 16;
    if (d != 0x0152 && d != 0x0156 && d != 0x015A && d != 0x0162 && d != 0x0166 && d != 0x016A) return 0;     /* Ivy Bridge 계열만 */
    uint32_t b0 = pci_rd(0x10), b1 = pci_rd(0x14);
    uint64_t base = (b0 & 6) == 4 ? (((uint64_t)b1 << 32) | (b0 & ~0xFu)) : (uint64_t)(b0 & ~0xFu);
    if (!(cmd & 2) || !base) return 0;
    gm = (volatile uint8_t *)(uintptr_t)base; devid = d; mapped = 1;
    return 1;
}
int ivbhdmi_keep_mode(void) { return map() && (rd32(0xE1140) & 0x80000000u) != 0; }
int ivbhdmi_hdmi_on(void) { return stall && map() && (rd32(0xE1140) & 0x80000000u) != 0; }

/* ---- 내장 패널 추가로 켜기 (DisplaySelect v39 lvb_on / lv_fdi_train 그대로) ---- */
static int fdi_train(uint32_t TX, uint32_t RX, uint32_t IIR, uint32_t IMR, uint32_t ftx, uint32_t frx)
{
    static const uint32_t SW[4] = { 0x00, 0x3a, 0x39, 0x38 };
    uint32_t tx = ftx & ~0x80000000u & ~0x300u & ~0x400u & ~(0x3Fu << 22), rx = frx & ~0x80000000u & ~0x300u & ~0x400u;
    int bl = 0, sl = 0;
    for (int tr = 0; tr < 8 && !(bl && sl); tr++) {
        uint32_t t0 = tx | (SW[tr >> 1] << 22) | 0x4000;
        wr32(TX, t0 & ~0x80000000u); wr32(RX, rx | 0x2000 | 0x10); stall(250);
        wr32(IMR, rd32(IMR) & ~0x300u); wr32(IIR, 0x300);
        wr32(TX, t0 | 0x80000000u); wr32(RX, rx | 0x2000 | 0x10 | 0x80000000u); stall(150);
        bl = sl = 0;
        for (int i = 0; i < 100 && !bl; i++) { if (rd32(IIR) & 0x100) bl = 1; else stall(20); }
        wr32(IIR, 0x100);
        if (bl) {
            wr32(TX, t0 | 0x80000000u | 0x100); wr32(RX, rx | 0x2000 | 0x10 | 0x80000000u | 0x100); stall(150);
            for (int i = 0; i < 100 && !sl; i++) { if (rd32(IIR) & 0x200) sl = 1; else stall(20); }
            wr32(IIR, 0x200);
        }
        if (!(bl && sl)) { wr32(TX, t0 & ~0x80000000u); wr32(RX, rx | 0x2000 | 0x10); stall(500); }
    }
    { uint32_t cur = rd32(TX) & (0x3Fu << 22);
      wr32(TX, (ftx & ~0x400u & ~(0x3Fu << 22)) | cur | 0x4000 | 0x80000000u); wr32(RX, (frx & ~0x400u) | 0x80000000u); stall(1000); }
    return bl && sl;
}
static void regs(const char *tag)
{
    static const struct { const char *n; uint32_t o; } R[] = {
        { "PCH_LVDS", 0xE1180 }, { "PCH_HDMIB", 0xE1140 }, { "PIPEACONF", 0x70008 }, { "PIPEBCONF", 0x71008 }, { "TRANSBCONF", 0xF1008 },
        { "PIPESRC_A", 0x6001C }, { "PIPESRC_B", 0x6101C }, { "DSPACNTR", 0x70180 }, { "DSPASTRIDE", 0x70188 }, { "DSPASURF", 0x7019C },
        { "DSPBCNTR", 0x71180 }, { "DSPBSTRIDE", 0x71188 }, { "DSPBSURF", 0x7119C }, { "PF_CTL_B", 0x68880 }, { "PCH_DPLL_B", 0xC6018 },
        { "PCH_DREF_CONTROL", 0xC6200 }, { "PCH_DPLL_SEL", 0xC7000 }, { "FDI_TX_CTL_B", 0x61100 }, { "FDI_RX_CTL_B", 0xF100C }, { "FDI_RX_IIR_B", 0xF1014 }, { "PP_STATUS", 0xC7200 },
    };
    sp(tag); sp("\r\n");
    for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) rg(R[i].n, R[i].o, rd32(R[i].o));
}
static uint32_t o_dref, o_sel, o_lvds;
static void dual_on(void)
{
    o_dref = rd32(0xC6200); o_sel = rd32(0xC7000); o_lvds = rd32(0xE1180);   /* 끌 때 되돌릴 처음 값 */
    ln = 0; sp("\xEF\xBB\xBF" "X86MSX Ivy Bridge: internal panel on pipe B (DisplaySelect v39)\r\n");
    sp("device 8086:"); sh(devid); sp("\r\n");
    regs("[before]");
    wr32(0xC6200, 0x1402); stall(1000);
    wr32(0xC6048, 0x00021007); wr32(0xC604C, 0x00021007);
    for (int i = 0; i < 3; i++) { wr32(0xC6018, 0x88046004u); stall(250); }
    wr32(0xC7000, (rd32(0xC7000) & ~0xF0u) | 0x90);
    wr32(0xE1180, 0xA0300302u); stall(1000);
    static const uint32_t T[6] = { 0x05DB0555, 0x05DB0555, 0x0599057E, 0x031302FF, 0x031302FF, 0x03060302 };
    for (int i = 0; i < 6; i++) { wr32(0x61000 + 4 * i, T[i]); wr32(0xE1000 + 4 * i, T[i]); }
    wr32(0x6101C, rd32(0x6001C));                                       /* DisplaySelect는 0x03FF02FF (1024x768)로 고정 — 여기서는 지금 화면(파이프 A)과 같게 */
    wr32(0x61030, 0x7E4BBBBB); wr32(0x61034, 0x00800000); wr32(0x61040, 0x00021A8C); wr32(0x61044, 0x00080000);
    wr32(0xF1010, 0x00200080); wr32(0xF1030, 0x7E000000);
    wr32(0x68870, 0); wr32(0x68874, (1366u << 16) | 768); wr32(0x68880, 0xA0800000u);
    wr32(0x71008, 0x80000050u); wait_bits(0x71008, 0x40000000u, 0x40000000u, 300);
    int fdi = fdi_train(0x61100, 0xF100C, 0xF1014, 0xF1018, 0x80044B00u, 0x80022B50u);
    wr32(0xF1008, 0x80000000u); wait_bits(0xF1008, 0x40000000u, 0x40000000u, 300);
    wr32(0x71188, rd32(0x70188)); wr32(0x71180, rd32(0x70180) | 0x80000000u); wr32(0x7119C, rd32(0x7019C)); stall(20000);
    wr32(0xC7204, 0xABCD0003u); wait_bits(0xC7200, 0x80000000u, 0x80000000u, 800);
    wr32(0x48254, 0x000003D0); wr32(0xC8254, 0x03D003D0); wr32(0x48250, 0x80000000u); wr32(0xC8250, 0x80000000u);
    state = 1;
    sp(fdi ? "FDI B train OK\r\n" : "FDI B train FAIL\r\n");
    regs("[after]");
    flush();
}
/* 내장 패널 끄기 (DisplaySelect v39 lvb_off 그대로) — HDMI(파이프 A)는 그대로 */
static void dual_off(void)
{
    ln = 0; sp("\xEF\xBB\xBF" "X86MSX Ivy Bridge: internal panel off (HDMI only, DisplaySelect v39 lvb_off)\r\n");
    wr32(0xC8250, rd32(0xC8250) & ~0x80000000u); wr32(0x48250, rd32(0x48250) & ~0x80000000u);
    wr32(0xC7204, 0xABCD0000u); wait_bits(0xC7200, 0x80000000u, 0, 600);
    wr32(0x71180, rd32(0x71180) & ~0x80000000u); wr32(0x7119C, rd32(0x7119C)); stall(20000);
    wr32(0xE1180, o_lvds & ~0x80000000u);
    wr32(0x71008, rd32(0x71008) & ~0x80000000u); wait_bits(0x71008, 0x40000000u, 0, 300);
    wr32(0x68880, 0);
    wr32(0x61100, rd32(0x61100) & ~0x80000000u); wr32(0xF100C, rd32(0xF100C) & ~0x80000000u); stall(1000);
    wr32(0xF1008, rd32(0xF1008) & ~0x80000000u); wait_bits(0xF1008, 0x40000000u, 0, 300);
    wr32(0xC6018, rd32(0xC6018) & ~0x80000000u); stall(500);
    wr32(0xC7000, o_sel); wr32(0xC6200, o_dref); stall(1000);
    state = 0;
    regs("[after off]");
    flush();
}
static int g_boot_hdmi = -1;            /* 부팅 때 HDMI 포트가 켜져 있었으면 1 */
static uint32_t g_set_ms; static int g_armed, g_fresh;
/* ---- 화면 출력 목록 연결 (HdmiDrv): avail = 부팅 때 HDMI가 켜져 있던 Ivy Bridge, set(0/1/2) = 내부 / 복사 / HDMI만 ---- */
int ivbhdmi_avail(void)
{
    if (!stall || !map()) return 0;
    if (g_boot_hdmi < 0) g_boot_hdmi = (rd32(0xE1140) & 0x80000000u) != 0;
    return g_boot_hdmi ? (int)devid : 0;
}
static int panel_ok(void) { return state || rd32(0xE1180) == 0x20300302u; }
int ivbhdmi_set(int mode)
{
    if (!ivbhdmi_avail()) return 0;
    g_armed = 1; g_set_ms = 0; g_fresh = 0;
    if (mode == 2) { if (state) dual_off(); }
    else if (!state) {
        if (!panel_ok()) return 0;
        dual_on();
    }
    return 1;
}
int ivbhdmi_lost(void) { return 0; }
int ivbhdmi_audio_fresh(void) { int f = g_fresh; g_fresh = 0; return f; }
void ivbhdmi_tick(uint32_t now_ms)
{
    static uint32_t t0; static int done;
    if (g_armed) { if (!g_set_ms) g_set_ms = now_ms ? now_ms : 1; else if (now_ms - g_set_ms >= 1500) { g_armed = 0; g_fresh = 1; } }   /* 전환 1.5초 뒤 한 번: 소리 선택 시점 */
    if (done || !stall) return;
    if (!t0) { t0 = now_ms ? now_ms : 1; return; }
    if (now_ms - t0 < 2500) return;                                     /* 부팅 직후 화면이 안정된 뒤 한 번 */
    done = 1;
    if (!map()) return;
    uint32_t hd = rd32(0xE1140), lv = rd32(0xE1180);
    uint32_t srca = rd32(0x6001C), stra = rd32(0x70188);
    if ((hd & 0x80000000u) && lv == 0x20300302u && srca == 0x03FF02FFu && stra == 0x1000u) dual_on();   /* DisplaySelect가 시험한 조건(1024x768, 줄 간격 0x1000)일 때만 */
    else if ((hd & 0x80000000u) && lv == 0x20300302u) { ln = 0; sp("\xEF\xBB\xBF" "X86MSX Ivy Bridge: skipped, screen mode differs from DisplaySelect test (need PIPESRC_A 03FF02FF, stride 1000)\r\n"); rg("PIPESRC_A", 0x6001C, srca); rg("DSPASTRIDE", 0x70188, stra); rg("PCH_HDMIB", 0xE1140, hd); rg("PCH_LVDS", 0xE1180, lv); flush(); }            /* HDMI는 켜져 있고 내장 패널은 BIOS가 꺼 둔 상태 (이 PC의 1366x768 패널 값) */
    else { ln = 0; sp("\xEF\xBB\xBF" "X86MSX Ivy Bridge: skipped\r\n"); rg("PCH_HDMIB", 0xE1140, hd); rg("PCH_LVDS", 0xE1180, lv); flush(); }
}

/* ---- HDMI 소리 (DisplaySelect v30 ivb_on_run: 포트 모드·오디오 비트, AUD_CONFIG 클럭 항목, GCP) ---- */
void ivbhdmi_audio(void)
{
    if (!stall || !map()) return;
    static const uint32_t PR[3] = { 0xE1140, 0xE1150, 0xE1160 };
    uint32_t port = 0, orig = 0;
    for (int i = 0; i < 3 && !port; i++) { uint32_t v = rd32(PR[i]); if (v & 0x80000000u) { port = PR[i]; orig = v; } }
    ln = 0; sp("\xEF\xBB\xBF" "X86MSX Ivy Bridge HDMI audio (DisplaySelect v30)\r\n");
    if (!port) { sp("no HDMI port on\r\n"); flush(); return; }
    uint32_t tsel = (orig >> 29) & 3, nv = (orig & ~0xC00u) | 0x800u | 0x200u | 0x40u;
    uint32_t ac = 0xE5000 + 0x100 * tsel, ac_orig = rd32(ac), dip = 0xE0200 + 0x1000 * tsel;
    if (nv != orig) wr32(port, nv);
    uint32_t ht = rd32(0x60000 + 0x1000 * tsel), vt = rd32(0x6000C + 0x1000 * tsel);
    uint32_t khz = ((ht >> 16) + 1) * ((vt >> 16) + 1) / 1000 * 60;
    static const struct { uint32_t k; uint32_t i; } T[] = { { 25200, 1 }, { 27000, 2 }, { 54000, 4 }, { 74250, 7 }, { 148500, 9 } };
    uint32_t idx = 1, best = 0xFFFFFFFFu;
    for (int j = 0; j < 5; j++) { uint32_t dd = khz > T[j].k ? khz - T[j].k : T[j].k - khz; if (dd < best) { best = dd; idx = T[j].i; } }
    if (best * 50 > khz) idx = 1;
    uint32_t nc = (ac_orig & ~(0xFu << 16)) | (idx << 16);
    if (nc != ac_orig) wr32(ac, nc);
    wr32(dip + 0x10, 0); wr32(dip, rd32(dip) | (1u << 25));
    stall(100000);
    rg("PCH_HDMI before", port, orig); rg("PCH_HDMI after", port, rd32(port)); rg("HTOTAL", 0x60000 + 0x1000 * tsel, ht); rg("VTOTAL", 0x6000C + 0x1000 * tsel, vt);
    rg("kHz", 0, khz); rg("item", 0, idx); rg("AUD_CONFIG before", ac, ac_orig); rg("AUD_CONFIG after", ac, rd32(ac)); rg("DIP_CTL", dip, rd32(dip)); rg("AUD_CNTRL_ST2", 0xE50C0, rd32(0xE50C0));
    flush();
}
