/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* intelhdmi.c - Intel Gen9 HDMI 미러 (설명 intelhdmi.h). 로그: 0:\LOG\INTEL_HDMI.TXT (단계마다 다시 저장) */
#include "intelhdmi.h"

static void (*stall)(uint32_t);
static void (*logw)(const char *, const uint8_t *, uint32_t);
static uint64_t gop_base;
static volatile uint8_t *gm;

/* ---- 로그 ---- */
static char lb[24576]; static uint32_t ln;
static void sp(const char *t) { while (*t && ln < sizeof lb - 2) lb[ln++] = *t++; }
static void dip_avi(int on);
static uint32_t pa_ctl_sv, pa_surf_sv;           /* 4.0.90: 켜기 전 파이프 A(내장 패널) 플레인 — 끌 때 다시 써서 내장 화면이 이전 부팅 화면으로 돌아가지 않게 함 */
static void sh(uint32_t v, int d) { static const char H[] = "0123456789ABCDEF"; if (!d) { d = 1; for (uint32_t x = v >> 4; x; x >>= 4) d++; } for (int i = d - 1; i >= 0 && ln < sizeof lb - 2; i--) lb[ln++] = H[(v >> (i * 4)) & 15]; }
static void sn(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n && ln < sizeof lb - 2) lb[ln++] = t[--n]; }
static void nl(void) { sp("\r\n"); }
static void flush(void) { if (logw) logw("0:\\LOG\\INTEL_HDMI.TXT", (const uint8_t *)lb, ln); }
static void reg(const char *n, uint32_t off, uint32_t v) { sp("  "); sp(n); sp(" ["); sh(off, 5); sp("] = "); sh(v, 8); nl(); }

/* ---- MMIO / PCI ---- */
static uint32_t rd32(uint32_t off) { return *(volatile uint32_t *)(gm + off); }
static void wr32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(gm + off) = v; }
static uint32_t pci_rd(int b, int d, int f, int off)
{
    __asm__ volatile("outl %0, %1" : : "a"(0x80000000u | (uint32_t)(b << 16) | (uint32_t)(d << 11) | (uint32_t)(f << 8) | (uint32_t)(off & 0xFC)), "Nd"((uint16_t)0xCF8));
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)0xCFC)); return v;
}
static void hw(const char *n, uint32_t off, uint32_t v) { wr32(off, v); sp("  W "); sp(n); sp(" ["); sh(off, 5); sp("] "); sh(v, 8); sp(" -> "); sh(rd32(off), 8); nl(); }
static int wait_bits(uint32_t off, uint32_t mask, uint32_t val, int ms)
{
    for (int i = 0; i < ms * 10; i++) { if ((rd32(off) & mask) == val) return 1; stall(100); }
    return (rd32(off) & mask) == val;
}
#define FAIL(txt) do { sp("! " txt); nl(); flush(); return 0; } while (0)

#define PW_DDI_B 2
#define PW_PW1 14
#define PW_PW2 15
static int well_on(int idx) { return ((rd32(0x45400) | rd32(0x45404)) & (1u << (idx * 2))) != 0; }

/* ---- GMBUS EDID ---- */
#define GMBUS0 0xC5100u
#define GMBUS1 0xC5104u
#define GMBUS2 0xC5108u
#define GMBUS3 0xC510Cu
#define SOUTH_DSPCLK_GATE_D 0xC2020u
static uint32_t pch_id;
static void find_pch(void)
{
    pch_id = 0;
    for (int d = 0; d < 32 && !pch_id; d++) {
        uint32_t id = pci_rd(0, d, 0, 0);
        if ((id & 0xFFFF) != 0x8086) continue;
        if ((pci_rd(0, d, 0, 8) >> 16) != 0x0601) continue;
        pch_id = id >> 16;
    }
}
static int pch_is_spt(void) { uint32_t t = pch_id & 0xFF80; return t == 0xA100 || t == 0x9D00 || t == 0xA280; }
static int gm_wait(uint32_t status)
{
    for (int i = 0; i < 6000; i++) { uint32_t v = rd32(GMBUS2); if (v & (1u << 10)) return -1; if (v & status) return 0; stall(10); }
    return -2;
}
static int gm_wait_idle(void) { for (int i = 0; i < 3000; i++) { if (!(rd32(GMBUS2) & (1u << 9))) return 0; stall(10); } return -2; }
static void gm_clear(void) { gm_wait_idle(); wr32(GMBUS1, 1u << 31); wr32(GMBUS1, 0); wr32(GMBUS0, 0); }
static int gm_read_edid_block(uint32_t pin, uint32_t offset, uint8_t *buf, uint32_t len)
{
    int ret = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        wr32(GMBUS0, pin);
        wr32(GMBUS1, (2u << 25) | (1u << 25) | (len << 16) | (0x50u << 1) | 1u | (offset << 8) | (1u << 30));
        ret = 0;
        uint32_t left = len, got = 0;
        while (left) {
            int w = gm_wait(1u << 11);
            if (w) { ret = w; break; }
            uint32_t v = rd32(GMBUS3);
            for (int k = 0; k < 4 && left; k++, left--) { buf[got++] = (uint8_t)v; v >>= 8; }
        }
        if (!ret) { int w = gm_wait(1u << 14); if (w) ret = w; }
        if (!ret) { wr32(GMBUS1, (4u << 25) | (1u << 30)); if (gm_wait_idle()) ret = -2; wr32(GMBUS0, 0); return ret; }
        gm_clear();
        if (ret == -1 && attempt == 0) continue;
        return ret;
    }
    return ret;
}
static int edid_ok(const uint8_t *e)
{
    static const uint8_t h[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    uint8_t sum = 0;
    for (int i = 0; i < 8; i++) if (e[i] != h[i]) return 0;
    for (int i = 0; i < 128; i++) sum = (uint8_t)(sum + e[i]);
    return sum == 0;
}

/* ---- EDID 상세 타이밍 ---- */
typedef struct { uint32_t pclk, ha, hb, hso, hsw, va, vb, vso, vsw; int hpos, vpos, il, sep; } Dtd;
static int dtd_parse(const uint8_t *d, Dtd *t)
{
    t->pclk = (uint32_t)(d[0] | (d[1] << 8)) * 10;
    if (!t->pclk) return 0;
    t->ha = d[2] | ((d[4] & 0xF0u) << 4);   t->hb = d[3] | ((d[4] & 0x0Fu) << 8);
    t->va = d[5] | ((d[7] & 0xF0u) << 4);   t->vb = d[6] | ((d[7] & 0x0Fu) << 8);
    t->hso = d[8] | ((d[11] & 0xC0u) << 2); t->hsw = d[9] | ((d[11] & 0x30u) << 4);
    t->vso = (uint32_t)(d[10] >> 4) | ((d[11] & 0x0Cu) << 2);
    t->vsw = (uint32_t)(d[10] & 0xF) | ((d[11] & 0x03u) << 4);
    t->il = (d[17] >> 7) & 1; t->sep = ((d[17] >> 3) & 3) == 3; t->vpos = (d[17] >> 2) & 1; t->hpos = (d[17] >> 1) & 1;
    return 1;
}

/* ---- SKL WRPLL 분주 (intel_dpll_mgr.c skl_ddi_calculate_wrpll) ---- */
static uint64_t absdiff64(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }
static int skl_wrpll(uint32_t clock_khz, uint32_t ref_khz, uint32_t *cfg1, uint32_t *cfg2, uint32_t *pp)
{
    static const uint64_t dcoc[3] = { 8400000000ULL, 9000000000ULL, 9600000000ULL };
    static const uint8_t even[] = { 4, 6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 30, 32, 36, 40, 42, 44, 48, 52, 54, 56, 60, 64, 66, 68,
                                    70, 72, 76, 78, 80, 84, 88, 90, 92, 96, 98 };
    static const uint8_t odd[] = { 3, 5, 7, 9, 15, 21, 35 };
    const uint8_t *lists[2] = { even, odd };
    const int nlst[2] = { (int)sizeof even, (int)sizeof odd };
    uint64_t afe = (uint64_t)clock_khz * 1000 * 5, mind = ~0ULL, cfreq = 0;
    unsigned bp = 0;
    for (int d = 0; d < 2; d++) {
        int done = 0;
        for (int dco = 0; dco < 3 && !done; dco++)
            for (int i = 0; i < nlst[d] && !done; i++) {
                unsigned p = lists[d][i];
                uint64_t df = p * afe, dev = 10000 * absdiff64(df, dcoc[dco]) / dcoc[dco];
                if (df >= dcoc[dco]) { if (dev < 100 && dev < mind) { mind = dev; cfreq = dcoc[dco]; bp = p; } }
                else if (dev < 600 && dev < mind) { mind = dev; cfreq = dcoc[dco]; bp = p; }
                if (mind == 0) done = 1;
            }
        if (d == 0 && bp) break;
    }
    if (!bp) return 0;
    unsigned p0 = 0, p1 = 0, p2 = 0, p = bp;
    if (p % 2 == 0) {
        unsigned half = p / 2;
        if (half == 1 || half == 2 || half == 3 || half == 5) { p0 = 2; p1 = 1; p2 = half; }
        else if (half % 2 == 0) { p0 = 2; p1 = half / 2; p2 = 2; }
        else if (half % 3 == 0) { p0 = 3; p1 = half / 3; p2 = 2; }
        else if (half % 7 == 0) { p0 = 7; p1 = half / 7; p2 = 2; }
    } else if (p == 3 || p == 9) { p0 = 3; p1 = 1; p2 = p / 3; }
    else if (p == 5 || p == 7) { p0 = p; p1 = 1; p2 = 1; }
    else if (p == 15) { p0 = 3; p1 = 1; p2 = 5; }
    else if (p == 21) { p0 = 7; p1 = 1; p2 = 3; }
    else if (p == 35) { p0 = 7; p1 = 1; p2 = 5; }
    if (!p0 || !p1 || !p2) return 0;
    uint32_t central = cfreq == 9600000000ULL ? 0 : cfreq == 9000000000ULL ? 1 : 3;
    uint32_t pdiv = p0 == 1 ? 0 : p0 == 2 ? 1 : p0 == 3 ? 2 : 4;
    uint32_t kdiv = p2 == 5 ? 0 : p2 == 2 ? 1 : p2 == 3 ? 2 : 3;
    uint32_t qmode = p1 == 1 ? 0 : 1;
    uint64_t dcof = (uint64_t)p0 * p1 * p2 * afe;
    uint32_t dint = (uint32_t)(dcof / ((uint64_t)ref_khz * 1000));
    uint32_t dfrac = (uint32_t)(((dcof / (ref_khz / 1000)) - (uint64_t)dint * 1000000) * 0x8000 / 1000000);
    *cfg1 = 0x80000000u | (dfrac << 9) | dint;
    *cfg2 = (p1 << 8) | (qmode << 7) | (kdiv << 5) | (pdiv << 2) | central;
    *pp = bp;
    return 1;
}

/* ---- VBT에서 HDMI-B 자식 장치 ---- */
static int vbt_hdmi_b(uint8_t *child, uint32_t *vbt_ver)
{
    uint32_t asls = pci_rd(0, 2, 0, 0xFC);
    if (!asls) return 0;
    const uint8_t *op = (const uint8_t *)(uintptr_t)asls;
    if (!(op[0x400] == '$' && op[0x401] == 'V' && op[0x402] == 'B' && op[0x403] == 'T')) return 0;
    const uint8_t *vbt = op + 0x400;
    *vbt_ver = (uint32_t)(vbt[20] | (vbt[21] << 8));
    uint32_t bdb_off = vbt[28] | (vbt[29] << 8);
    const uint8_t *bdb = vbt + bdb_off;
    uint16_t bhs = (uint16_t)(bdb[18] | (bdb[19] << 8)), bsz = (uint16_t)(bdb[20] | (bdb[21] << 8));
    uint32_t idx = bhs;
    while (idx + 3 < bsz) {
        uint8_t id = bdb[idx]; uint32_t sz = bdb[idx + 1] | ((uint32_t)bdb[idx + 2] << 8);
        if (idx + 3 + sz > bsz) break;
        if (id == 2 && sz >= 5) {
            const uint8_t *g = bdb + idx + 3; uint32_t csz = g[4];
            if (csz < 27 || csz > 64) return 0;
            for (uint32_t off = 5; off + csz <= sz; off += csz) {
                const uint8_t *c = g + off;
                if ((c[2] | (c[3] << 8)) && c[16] == 1) {
                    for (uint32_t i = 0; i < 64; i++) child[i] = i < csz ? c[i] : 0;
                    return (int)csz;
                }
            }
            return 0;
        }
        idx += 3 + sz;
    }
    return 0;
}

/* HDMI 버퍼 변환표 (skl_trans_hdmi) */
static const uint32_t hd_lo[11] = { 0x18, 0x5012, 0x7011, 0x18, 0x18, 0x4013, 0x80006012, 0x18, 0x80003015, 0x80003015, 0x80000018 };
static const uint32_t hd_hi[11] = { 0xAC, 0x9D, 0x88, 0xA1, 0x98, 0x88, 0xCD, 0xDF, 0xCD, 0xC0, 0xC0 };
static const uint8_t  hd_ib[11] = { 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1 };

static struct {
    int valid, well_was_on, on;
    uint32_t pwctl2, ctrl1, ctrl2, func, linetime, wm0, bmu, chicken_b;
    int stage;
} hs;

/* ---- 내장 패널 끄기 (4.0.61): 백라이트만 끈다. i915 lpt_disable_backlight와 같은 순서 (PP_CONTROL의 BLC 허용 비트 -> PCH PWM 켜짐 비트).
 * 파이프 A(패널 화면 신호)는 그대로라 되돌리기 쉽고, HDMI가 뽑히면 tick()이 자동으로 되돌린다 ---- */
#define PP_CONTROL 0xC7204u
#define BLC_PWM_PCH_CTL1 0xC8250u
static struct { int req, off, lost; uint32_t pp, c1, last; } pn;
static void panel_off(void)
{
    pn.pp = rd32(PP_CONTROL); pn.c1 = rd32(BLC_PWM_PCH_CTL1);
    sp("\r\n[panel off] backlight\r\n");
    hw("PP_CONTROL(BLC off)", PP_CONTROL, pn.pp & ~4u);
    hw("BLC_PWM_PCH_CTL1(PWM off)", BLC_PWM_PCH_CTL1, pn.c1 & ~0x80000000u);
    pn.off = 1; pn.lost = 0; flush();
}
static void panel_on(void)
{
    if (!pn.off) return;
    sp("\r\n[panel on] restore backlight\r\n");
    hw("BLC_PWM_PCH_CTL1(restore)", BLC_PWM_PCH_CTL1, pn.c1);
    hw("PP_CONTROL(restore)", PP_CONTROL, (rd32(PP_CONTROL) & ~4u) | (pn.pp & 4u));
    pn.off = 0; flush();
}

void intelhdmi_init(void (*stall_us)(uint32_t), void (*logw_)(const char *, const uint8_t *, uint32_t), uint64_t gop_fb)
{
    stall = stall_us; logw = logw_; gop_base = gop_fb;
}

static int find_gpu(uint32_t *devid)
{
    uint32_t id = pci_rd(0, 2, 0, 0), cmd = pci_rd(0, 2, 0, 4);
    if ((id & 0xFFFF) != 0x8086 || (pci_rd(0, 2, 0, 8) >> 24) != 0x03) return 0;
    uint32_t b0 = pci_rd(0, 2, 0, 0x10), b1 = pci_rd(0, 2, 0, 0x14);
    uint64_t base = (b0 & 6) == 4 ? (((uint64_t)b1 << 32) | (b0 & ~0xFu)) : (uint64_t)(b0 & ~0xFu);
    if (!(cmd & 2) || !base) return 0;
    gm = (volatile uint8_t *)(uintptr_t)base;
    *devid = id >> 16;
    return 1;
}

static int enable(void)
{
    uint32_t devid = 0;
    for (uint32_t i = 0; i < sizeof hs; i++) ((volatile uint8_t *)&hs)[i] = 0;
    ln = 0;
    sp("\xEF\xBB\xBF" "X86MSX Intel HDMI mirror (4.0.58)\r\n");
    if (!find_gpu(&devid)) FAIL("no usable Intel graphics at PCI 00:02.0");
    sp("device 8086:"); sh(devid, 4); sp("  GOP fb "); sh((uint32_t)(gop_base >> 32), 0); sh((uint32_t)gop_base, 8);
    sp("  BDSM "); sh(pci_rd(0, 2, 0, 0x5C), 8); nl();
    { uint32_t hi = devid >> 8;
      if (!(hi == 0x19 || hi == 0x59 || hi == 0x3E || hi == 0x87 || hi == 0x9B)) FAIL("not a Gen9 Intel graphics (Skylake/Kaby Lake/Coffee Lake) - not supported"); }

    sp("\r\n[stage 0] read-only checks\r\n");
    if (!well_on(PW_PW1) || !well_on(PW_PW2)) FAIL("PW1/PW2 off");
    uint32_t sde = rd32(0xC4000);
    sp("  hotplug port B: "); sp((sde >> 21) & 1 ? "connected" : "none"); nl();
    if (!((sde >> 21) & 1)) FAIL("no HDMI sink on port B - plug HDMI and retry");

    uint8_t e[128]; for (int i = 0; i < 128; i++) e[i] = 0;
    find_pch();
    int spt = pch_is_spt();
    uint32_t gate = rd32(SOUTH_DSPCLK_GATE_D);
    if (spt) wr32(SOUTH_DSPCLK_GATE_D, gate | (1u << 31));
    int rc = gm_read_edid_block(5, 0, e, 128);
    if (spt) wr32(SOUTH_DSPCLK_GATE_D, gate);
    if (rc || !edid_ok(e)) FAIL("cannot read monitor EDID (GMBUS pin 5)");
    Dtd t;
    if (!dtd_parse(e + 54, &t)) FAIL("no EDID detailed timing");
    sp("  timing "); sn(t.ha); sp("x"); sn(t.va); sp(" pclk "); sn(t.pclk); sp("kHz H "); sn(t.hso); sp("/"); sn(t.hsw); sp("/"); sn(t.hb);
    sp(" V "); sn(t.vso); sp("/"); sn(t.vsw); sp("/"); sn(t.vb); sp(t.hpos ? " H+" : " H-"); sp(t.vpos ? " V+" : " V-"); sp(t.il ? " interlaced" : " progressive"); nl();
    if (t.ha != 1920 || t.va != 1080) FAIL("only 1920x1080 monitors supported (same buffer as the panel)");
    if (t.il || !t.sep) FAIL("interlaced / non-separate sync not supported");
    if (t.pclk < 100000 || t.pclk > 165000) FAIL("pixel clock outside 100-165 MHz");

    uint32_t cfg1 = 0, cfg2 = 0, pdiv = 0;
    if (!skl_wrpll(t.pclk, 24000, &cfg1, &cfg2, &pdiv)) FAIL("no PLL divider found");
    sp("  PLL ref 24MHz  P "); sn(pdiv); sp("  CFGCR1 "); sh(cfg1, 8); sp("  CFGCR2 "); sh(cfg2, 8); nl();

    uint8_t vc[64]; uint32_t vver = 0;
    int vcs = vbt_hdmi_b(vc, &vver);
    int level = 8, vboost = 0, lanerev = 0;
    if (vcs) {
        if (vver >= 158) level = vc[7] & 0x1F;
        if (level > 10) level = 10;
        if (vver >= 196 && (vc[23] & 0x08) && vcs >= 38) { static const uint8_t mp[3] = { 1, 3, 7 }; uint8_t v = vc[37] >> 4; vboost = v < 3 ? mp[v] : 0; }
        lanerev = (vc[23] >> 1) & 1;
    }
    sp("  VBT HDMI-B child "); sp(vcs ? "found" : "not found (defaults)"); sp("  level "); sn((uint32_t)level); sp("  iboost "); sn((uint32_t)vboost); sp("  lane_rev "); sn((uint32_t)lanerev); nl();

    uint32_t pa_ctl = rd32(0x70180), pa_stride = rd32(0x70188), pa_size = rd32(0x70190), pa_surf = rd32(0x7019C), pa_wm0 = rd32(0x70240);
    if (!(pa_ctl & 0x80000000u) || ((pa_ctl >> 24) & 0xF) != 4) FAIL("pipe A plane is not XRGB8888");
    if (pa_surf != 0 || pa_stride != 0x78 || pa_size != 0x0437077F) FAIL("pipe A buffer is not (GGTT 0, 1920x1080)");
    if (!(pa_wm0 & 0x80000000u)) FAIL("pipe A watermark 0 not enabled");
    uint32_t pb_buf = rd32(0x7127C);
    if (pb_buf == 0) FAIL("no DDB allocated for pipe B");
    if (rd32(0x71008) & 0x80000000u) FAIL("transcoder B already on");
    if (rd32(0x61400) & 0x80000000u) FAIL("transcoder B DDI function already on");
    if (rd32(0x71180) & 0x80000000u) FAIL("pipe B plane already on");
    if (rd32(0x46014) & 0x80000000u) FAIL("DPLL1 already on");
    hs.pwctl2 = rd32(0x45404); hs.ctrl1 = rd32(0x6C058); hs.ctrl2 = rd32(0x6C05C); hs.func = rd32(0x61400);
    hs.linetime = rd32(0x45274); hs.wm0 = rd32(0x71240); hs.bmu = rd32(0x6C00C); hs.chicken_b = rd32(0x420C4);
    hs.well_was_on = well_on(PW_DDI_B);
    hs.valid = 1;
    sp("  checks passed. writing now.\r\n");
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    uint32_t ht = t.ha + t.hb, vt = t.va + t.vb;
    uint32_t hss = t.ha + t.hso, hse = hss + t.hsw, vss = t.va + t.vso, vse = vss + t.vsw;

    pa_ctl_sv = rd32(0x70180); pa_surf_sv = rd32(0x7019C);
    hs.stage = 1; sp("\r\n[stage 1] DDI B power well\r\n");
    hw("PWR_WELL_CTL2", 0x45404, rd32(0x45404) | (2u << (PW_DDI_B * 2)));
    if (!wait_bits(0x45404, 1u << (PW_DDI_B * 2), 1u << (PW_DDI_B * 2), 10)) FAIL("DDI B power well did not turn on");
    if (rd32(0x64100) & 0x80000000u) FAIL("DDI B buffer already on");
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 2; sp("\r\n[stage 2] DPLL1 (HDMI WRPLL)\r\n");
    hw("DPLL_CTRL1", 0x6C058, (rd32(0x6C058) & ~(0x3Fu << 6)) | ((1u | (1u << 5)) << 6));
    hw("DPLL1_CFGCR1", 0x6C040, cfg1);
    hw("DPLL1_CFGCR2", 0x6C044, cfg2);
    hw("LCPLL2_CTL(DPLL1)", 0x46014, rd32(0x46014) | 0x80000000u);
    if (!wait_bits(0x6C060, 1u << 8, 1u << 8, 20)) { reg("DPLL_STATUS", 0x6C060, rd32(0x6C060)); FAIL("DPLL1 did not lock"); }
    reg("DPLL_STATUS", 0x6C060, rd32(0x6C060));
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 3; sp("\r\n[stage 3] DPLL_CTRL2: port B clock = DPLL1\r\n");
    hw("DPLL_CTRL2", 0x6C05C, (rd32(0x6C05C) & ~((1u << 16) | (3u << 4))) | (1u << 4) | (1u << 3));
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 4; sp("\r\n[stage 4] DDI B buffer translation entry 9 + iboost\r\n");
    hw("DDI_BUF_TRANS_B[9].LO", 0x64EA8, hd_lo[level] | (vboost ? 0x80000000u : 0));
    hw("DDI_BUF_TRANS_B[9].HI", 0x64EAC, hd_hi[level]);
    {
        uint32_t ib = vboost ? (uint32_t)vboost : hd_ib[level];
        uint32_t v = rd32(0x6C00C) & ~((7u << 11) | (1u << 24));
        if (ib == 1 || ib == 3 || ib == 7) v |= ib << 11; else v |= 1u << 24;
        hw("DISPIO_CR_TX_BMU_CR0", 0x6C00C, v);
    }
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 5; sp("\r\n[stage 5] TRANS_CLK_SEL B\r\n");
    hw("TRANS_CLK_SEL_B", 0x46144, 2u << 29);
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 6; sp("\r\n[stage 6] pipe B / transcoder B timing\r\n");
    hw("PIPESRC_B", 0x6101C, ((t.ha - 1) << 16) | (t.va - 1));
    hw("PIPE_MISC_B", 0x71030, 0);
    hw("VSYNCSHIFT_B", 0x61028, 0);
    hw("HTOTAL_B", 0x61000, (t.ha - 1) | ((ht - 1) << 16));
    hw("HBLANK_B", 0x61004, (t.ha - 1) | ((ht - 1) << 16));
    hw("HSYNC_B", 0x61008, (hss - 1) | ((hse - 1) << 16));
    hw("VTOTAL_B", 0x6100C, (t.va - 1) | ((vt - 1) << 16));
    hw("VBLANK_B", 0x61010, (t.va - 1) | ((vt - 1) << 16));
    hw("VSYNC_B", 0x61014, (vss - 1) | ((vse - 1) << 16));
    hw("TRANS_MULT_B", 0x6102C, 0);
    hw("CHICKEN_TRANS_B", 0x420C4, rd32(0x420C4) & ~(3u << 27));
    hw("TRANSCONF_B(off)", 0x71008, 0);
    hw("WM_LINETIME_B", 0x45274, (t.pclk ? (ht * 8000 + t.pclk - 1) / t.pclk : 0));
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 7; sp("\r\n[stage 7] plane B buffers / watermark\r\n");
    hw("PLANE_BUF_CFG_B", 0x7127C, pb_buf);
    hw("PLANE_WM0_B", 0x71240, pa_wm0);
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 8; sp("\r\n[stage 8] TRANS_DDI_FUNC_CTL B (HDMI, port B, 8bpc) + TRANSCONF B\r\n");
    dip_avi(1);                                          /* 4.0.88: v6처럼 처음부터 HDMI 모드 + AVI 인포프레임 (DVI로 켠 뒤 소리 때 HDMI로 바꾸면 코덱이 핀 출력을 받지 않았음) */
    hw("TRANS_DDI_FUNC_CTL_B", 0x61400, 0x80000000u | (1u << 28) | (t.vpos ? (1u << 17) : 0) | (t.hpos ? (1u << 16) : 0));
    hw("TRANSCONF_B", 0x71008, 0x80000000u);
    if (!wait_bits(0x71008, 0x40000000u, 0x40000000u, 100)) { reg("TRANSCONF_B", 0x71008, rd32(0x71008)); FAIL("pipe B did not start"); }
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 9; sp("\r\n[stage 9] DDI B buffer on (WA #1143)\r\n");
    hw("CHICKEN_TRANS_A(WA on)", 0x420C0, rd32(0x420C0) | (3u << 18));
    stall(2);
    hw("CHICKEN_TRANS_A(WA off)", 0x420C0, rd32(0x420C0) & ~(3u << 18));
    hw("DDI_BUF_CTL_B", 0x64100, 0x80000000u | (lanerev ? (1u << 16) : 0));
    stall(1000);
    if (!wait_bits(0x64100, 0x80, 0, 10)) { reg("DDI_BUF_CTL_B", 0x64100, rd32(0x64100)); FAIL("DDI B buffer did not become active"); }
    /* 4.0.73: 단계마다 USB에 쓰지 않음 (느렸음) — 실패·마지막에만 */

    hs.stage = 10; sp("\r\n[stage 10] plane B on (same buffer, SURF=0)\r\n");
    hw("PLANE_OFFSET_B", 0x711A4, 0);
    hw("PLANE_STRIDE_B", 0x71188, pa_stride);
    hw("PLANE_POS_B", 0x7118C, 0);
    hw("PLANE_SIZE_B", 0x71190, pa_size);
    hw("PLANE_CTL_B", 0x71180, pa_ctl);
    hw("PLANE_SURF_B", 0x7119C, pa_surf);
    stall(60000);

    hs.stage = 11;
    sp("\r\ndone. HDMI monitor should show the same screen.\r\n");
    flush();
    if (pn.req) panel_off();
    hs.on = 1;
    return 1;
}

/* ---- HDMI 소리 (4.0.65): 그래픽 쪽 준비. i915 intel_audio.c (hsw_audio_codec_enable) + intel_hdmi.c (AVI 인포프레임) 순서.
 * HDMI 모드로 바꾸고 AVI 인포프레임(RGB 풀레인지 1080p)을 보내고, 모니터 EDID로 ELD를 만들어 오디오 쪽에 넣는다. HD Audio 쪽은 kernel/hda.c(hda_select 2) ---- */
static int au_on, au_pend, ih_lost, au_fresh;
static uint32_t au_t0;
static uint32_t au_save_func;
#define AU_CFG_B   0x65100u
#define AU_ELDCP   0x650C0u
#define AU_ELDCTL  0x651B4u
#define AU_EDID_B  0x65150u
#define DIP_CTL_B  0x61200u
#define DIP_AVI_B  0x61220u
static void dip_avi(int on)
{
    uint32_t v = rd32(DIP_CTL_B) & ~(1u << 12);
    wr32(DIP_CTL_B, v);
    if (!on) { sp("  AVI infoframe off\r\n"); return; }
    uint8_t pb[14] = { 0 };
    pb[1] = 0x00; pb[2] = 0x08; pb[3] = 0x08; pb[4] = 16;       /* RGB, 화면 비율 같음, 풀레인지, VIC 16 (1080p60) */
    uint32_t sum = 0x82 + 0x02 + 0x0D;
    for (int i = 1; i <= 13; i++) sum += pb[i];
    pb[0] = (uint8_t)(0x100 - (sum & 0xFF));
    uint32_t d[9] = { 0 };
    d[0] = 0x82u | (2u << 8) | (13u << 16);
    for (int i = 0; i < 14; i++) d[1 + i / 4] |= (uint32_t)pb[i] << ((i % 4) * 8);
    for (int i = 0; i < 9; i++) wr32(DIP_AVI_B + 4u * (uint32_t)i, d[i]);
    wr32(DIP_CTL_B, v | (1u << 12));
    reg("VIDEO_DIP_CTL_B", DIP_CTL_B, rd32(DIP_CTL_B));
}
static int build_eld(uint8_t *eld)
{
    uint8_t e1[128];
    for (int i = 0; i < 24; i++) eld[i] = 0;
    int nsad = 0;
    uint8_t sad[3 * 15];
    uint8_t spk = 0x01, e0[128];
    int mnl = 0;
    if (gm_read_edid_block(5, 0, e0, 128) == 0 && edid_ok(e0) && e0[126] >= 1 && gm_read_edid_block(5, 128, e1, 128) == 0 && e1[0] == 0x02) {
        uint32_t end = e1[2]; if (end > 128) end = 128;
        for (uint32_t i = 4; i < end;) {
            uint8_t tag = e1[i] >> 5, n = e1[i] & 31;
            if (tag == 1) for (int k = 0; k + 2 < n && nsad < 15; k += 3) { sad[nsad * 3] = e1[i + 1 + k]; sad[nsad * 3 + 1] = e1[i + 2 + k]; sad[nsad * 3 + 2] = e1[i + 3 + k]; nsad++; }
            if (tag == 4 && n >= 3) spk = e1[i + 1];
            i += (uint32_t)n + 1;
        }
        for (int i = 0; i < 18 && 0; i++) {}
    }
    if (!nsad) { sad[0] = 0x09; sad[1] = 0x07; sad[2] = 0x07; nsad = 1; }   /* LPCM 2ch, 32/44.1/48 kHz, 16/20/24bit */
    eld[0] = 2 << 3;                       /* ELD 버전 2 */
    eld[4] = (uint8_t)((3 << 5) | mnl);    /* 4.0.79: 위 3비트 = CEA-861 버전 3, 아래 5비트 = 모니터 이름 길이 */
    eld[5] = (uint8_t)(nsad << 4);         /* SAD 수, 연결 종류 0 = HDMI */
    eld[7] = spk;
    int len = 20 + mnl + nsad * 3;
    for (int i = 0; i < nsad * 3; i++) eld[20 + mnl + i] = sad[i];
    eld[2] = (uint8_t)((len - 4 + 3) / 4);   /* 4.0.79: 기본 블록 길이(4바이트 묶음), 머리 4바이트 제외 */
    return (len + 3) / 4;                  /* 4바이트 묶음 수 */
}
int intelhdmi_audio(int on)
{
    uint32_t devid = 0;
    if (!stall || !find_gpu(&devid) || !hs.on) return 0;
    if (on && !au_on) {
        ln = 0; sp("\xEF\xBB\xBF" "X86MSX Intel HDMI audio (graphics side)\r\n\r\n[audio] enable\r\n");
        au_save_func = rd32(0x61400);
        reg("TRANS_DDI_FUNC_CTL_B(before)", 0x61400, au_save_func);
        hw("TRANS_DDI_FUNC_CTL_B(HDMI mode)", 0x61400, au_save_func & ~0x07000000u);
        stall(20000);
        dip_avi(1);
        uint32_t v = rd32(0x65F10);                     /* 4.0.79: i915 codec_wake_override — 비트를 한 번 내렸다 올려야 코덱이 깨어남 */
        wr32(0x65F10, v & ~(1u << 15)); stall(1500);
        hw("AUD_CHICKENBIT(codec wake)", 0x65F10, v | (1u << 15)); stall(1500);
        v = rd32(AU_ELDCP); hw("AUD_PIN_ELD_CP_VLD(out enable, eld invalid)", AU_ELDCP, (v | 0x40u) & ~0x10u);
        v = rd32(AU_CFG_B); v &= ~((1u << 29) | (1u << 28) | (0xFu << 16)); v |= (9u << 16);      /* 148.5 MHz */
        hw("AUD_CONFIG_B(148500)", AU_CFG_B, v);
        uint8_t eld[128]; int dw = build_eld(eld);
        sp("  ELD:"); for (int i = 0; i < dw * 4; i++) { sp(" "); sh(eld[i], 2); } nl();
        v = rd32(AU_ELDCTL); reg("AUD_DIP_ELD_CTRL_ST_B(before)", AU_ELDCTL, v);
        wr32(AU_ELDCTL, v & ~(0x1Fu << 5));
        for (int i = 0; i < dw; i++) wr32(AU_EDID_B, (uint32_t)eld[i * 4] | ((uint32_t)eld[i * 4 + 1] << 8) | ((uint32_t)eld[i * 4 + 2] << 16) | ((uint32_t)eld[i * 4 + 3] << 24));
        reg("AUD_DIP_ELD_CTRL_ST_B(after)", AU_ELDCTL, rd32(AU_ELDCTL));
        v = rd32(AU_ELDCP); hw("AUD_PIN_ELD_CP_VLD(eld valid)", AU_ELDCP, v | 0x10u);
        stall(50000);
        reg("AUD_CONFIG_B", AU_CFG_B, rd32(AU_CFG_B));
        reg("AUD_MISC_CTRL_B", 0x65110, rd32(0x65110));
        reg("TRANS_DDI_FUNC_CTL_B(after)", 0x61400, rd32(0x61400));
        reg("PIPEDSL_B", 0x71000, rd32(0x71000)); stall(2000); reg("PIPEDSL_B", 0x71000, rd32(0x71000));
        sp("\r\ndone. Now pick HDMI in sound output (HDA).\r\n");
        au_on = 1; flush();
    } else if (!on && au_on) {
        ln = 0; sp("\xEF\xBB\xBF" "X86MSX Intel HDMI audio: off\r\n");
        uint32_t v = rd32(AU_ELDCP); hw("AUD_PIN_ELD_CP_VLD(off)", AU_ELDCP, v & ~0x50u);
        dip_avi(0);
        hw("TRANS_DDI_FUNC_CTL_B(DVI mode)", 0x61400, au_save_func);
        au_on = 0; flush();
    }
    return au_on;
}

static void undo(void)
{
    au_pend = 0;
    if (au_on) { uint32_t v = rd32(AU_ELDCP); wr32(AU_ELDCP, v & ~0x50u); dip_avi(0); au_on = 0; }
    panel_on();
    sp("\r\n[undo]\r\n");
    if (!hs.valid || hs.stage == 0) { sp("  nothing to undo\r\n"); flush(); return; }
    if (hs.stage >= 10) { hw("PLANE_CTL_B", 0x71180, 0); hw("PLANE_SURF_B", 0x7119C, 0); stall(40000); }
    if (hs.stage >= 9)  { hw("DDI_BUF_CTL_B", 0x64100, rd32(0x64100) & ~0x80000000u); wait_bits(0x64100, 0x80, 0x80, 10); }
    if (hs.stage >= 8)  { hw("TRANSCONF_B", 0x71008, rd32(0x71008) & ~0x80000000u); wait_bits(0x71008, 0x40000000u, 0, 100); hw("TRANS_DDI_FUNC_CTL_B", 0x61400, hs.func); }
    if (hs.stage >= 7)  hw("PLANE_WM0_B", 0x71240, hs.wm0);
    if (hs.stage >= 6)  { hw("WM_LINETIME_B", 0x45274, hs.linetime); hw("CHICKEN_TRANS_B", 0x420C4, hs.chicken_b); }
    if (hs.stage >= 5)  hw("TRANS_CLK_SEL_B", 0x46144, 0);
    if (hs.stage >= 4)  hw("DISPIO_CR_TX_BMU_CR0", 0x6C00C, hs.bmu);
    if (hs.stage >= 3)  hw("DPLL_CTRL2", 0x6C05C, hs.ctrl2);
    if (hs.stage >= 2)  { hw("LCPLL2_CTL(DPLL1)", 0x46014, rd32(0x46014) & ~0x80000000u); hw("DPLL_CTRL1", 0x6C058, hs.ctrl1); }
    if (hs.stage >= 1 && !hs.well_was_on) hw("PWR_WELL_CTL2", 0x45404, hs.pwctl2);
    if (pa_ctl_sv & 0x80000000u) { hw("PLANE_SURF_A(re-arm)", 0x7019C, pa_surf_sv); hw("PLANE_CTL_A(re-arm)", 0x70180, pa_ctl_sv); }   /* 4.0.90 */
    sp("  undo done.\r\n");
    reg("TRANSCONF_EDP(panel)", 0x7F008, rd32(0x7F008));
    flush();
    hs.stage = 0; hs.on = 0;
}

int intelhdmi_try(int run)
{
    uint32_t devid = 0;
    if (!stall) return -1;
    if (!find_gpu(&devid)) return -1;
    if (!run) {                                     /* Video 탭 열 때: 읽기 점검만 (쓰기 없음) */
        if (hs.on) return 1;
        ln = 0; sp("\xEF\xBB\xBF" "X86MSX Intel HDMI read-only check\r\n");
        sp("device 8086:"); sh(devid, 4); sp("  BDSM "); sh(pci_rd(0, 2, 0, 0x5C), 8); sp("  GOP fb "); sh((uint32_t)gop_base, 8); nl();
        if (well_on(PW_PW1)) {
            reg("SDEISR(hotplug)", 0xC4000, rd32(0xC4000));
            reg("pipeA PLANE_CTL", 0x70180, rd32(0x70180)); reg("pipeA PLANE_SURF", 0x7019C, rd32(0x7019C));
            reg("pipeB PLANE_BUF_CFG", 0x7127C, rd32(0x7127C)); reg("TRANSCONF_B", 0x71008, rd32(0x71008));
        } else sp("PW1 off\r\n");
        flush();
        return 0;
    }
    if (hs.on) { ln = 0; sp("\xEF\xBB\xBF" "X86MSX Intel HDMI mirror: turning off\r\n"); undo(); return 0; }
    pn.req = 0;
    { int r = enable(); if (!r && hs.valid && hs.stage > 0) undo(); return r; }
}

/* 4.0.59: 설정 > 비디오 > 화면 출력 목록용. 반환 = 장치 ID(0이면 이 PC에서는 목록에 올리지 않음). 읽기만 한다 */
int intelhdmi_avail(void)
{
    uint32_t devid = 0;
    if (!stall || !find_gpu(&devid)) return 0;
    { uint32_t hi = devid >> 8; if (!(hi == 0x19 || hi == 0x59 || hi == 0x3E || hi == 0x87 || hi == 0x9B)) return 0; }
    if (!well_on(PW_PW1) || !well_on(PW_PW2)) return 0;
    if (hs.on) return (int)devid;
    return ((rd32(0xC4000) >> 21) & 1) ? (int)devid : 0;            /* 포트 B(HDMI)에 모니터가 꽂혀 있을 때만 */
}
/* on=1 켜기 / on=0 끄기. 반환 = 지금 켜져 있으면 1 */
int intelhdmi_set(int mode)           /* 0 = 끄기, 1 = 복사 (내부+HDMI), 2 = HDMI만 (내장 패널 백라이트 끔) */
{
    uint32_t devid = 0;
    if (!stall || !find_gpu(&devid)) return 0;
    if (mode > 0 && !hs.on) { ln = 0; pn.req = 0; { int r = enable(); if (!r && hs.valid && hs.stage > 0) undo(); }
        if (hs.on) { au_pend = 1; au_t0 = 0; if (mode == 2 && !pn.off) { sp("\r\nmode -> HDMI only\r\n"); panel_off(); } } }   /* 4.0.66: 소리 쪽도 같이 켜고, 그 다음에 패널을 끔 */
    else if (mode > 0 && hs.on) {
        if (mode == 2 && !pn.off) { sp("\r\nmode -> HDMI only\r\n"); panel_off(); }
        else if (mode == 1 && pn.off) { sp("\r\nmode -> copy\r\n"); panel_on(); }
    }
    else if (!mode && hs.on) { ln = 0; sp("\xEF\xBB\xBF" "X86MSX Intel HDMI mirror: turning off\r\n"); undo(); }
    return hs.on;
}

/* 1초마다 호출: 패널을 꺼 둔 채 HDMI 모니터가 3초 넘게 안 보이면 내장 패널을 다시 켠다 (화면이 모두 꺼지는 것 방지) */
void intelhdmi_tick(uint32_t now_ms)
{
    if (au_pend && hs.on) {                                 /* 4.0.68: 소리 쪽은 켠 지 3초 뒤에 (부팅 중 화면이 먼저 안정되도록) */
        if (!au_t0) au_t0 = now_ms ? now_ms : 1;
        else if (now_ms - au_t0 >= 3000) { au_pend = 0; intelhdmi_audio(1); au_fresh = 1; }
    }
    if (!hs.on) return;
    if (now_ms - pn.last < 1000) return;
    pn.last = now_ms;
    if ((rd32(0xC4000) >> 21) & 1) { pn.lost = 0; return; }
    if (++pn.lost >= 3) { sp("\r\nHDMI sink lost -> back to internal\r\n"); panel_on(); ih_lost = 1; pn.lost = 0; }
}

int intelhdmi_lost(void) { int r = ih_lost; ih_lost = 0; return r; }   /* 4.0.74: HDMI 모니터가 뽑혔는지 (한 번 읽으면 지워짐) */

int intelhdmi_audio_fresh(void) { int f = au_fresh; au_fresh = 0; return f; }   /* 4.0.76: 그래픽 소리 쪽이 켜진 직후 1 (읽으면 지워짐) */

void intelhdmi_wake(void)     /* 4.0.84: 코덱 깨우기 신호를 내렸다 올림 (i915 codec_wake_override) */
{
    uint32_t devid = 0;
    if (!stall || !find_gpu(&devid)) return;
    uint32_t v = rd32(0x65F10);
    wr32(0x65F10, v & ~(1u << 15)); stall(1500);
    wr32(0x65F10, v | (1u << 15)); stall(1500);
}
