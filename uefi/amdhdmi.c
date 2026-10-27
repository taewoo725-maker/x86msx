/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* amdhdmi.c - AMD Renoir HDMI 켜기/끄기 연결부: PCI 찾기, HPD 감지, ACPI VFCT 에서 VBIOS 얻기, 켜기 뒤 소리 시점/뽑힘 감시.
 * 레지스터 쓰기 순서는 amddisp.c (DisplaySelect 1번 v53 과 같은 순서). */
#include "amdhdmi.h"
#include "amddisp.h"

static EFI_SYSTEM_TABLE *ST;
static void (*stall)(uint32_t);
static void (*logw)(const char *, const uint8_t *, uint32_t);
static volatile uint32_t *mm;
static int mapped, state, fresh, lostf;
static uint32_t on_ms, last_ms;

static uint32_t pr(int b, int d, int f, int off)
{
    __asm__ volatile("outl %0, %1" : : "a"(0x80000000u | ((uint32_t)b << 16) | ((uint32_t)d << 11) | ((uint32_t)f << 8) | (uint32_t)(off & 0xFC)), "Nd"((uint16_t)0xCF8));
    uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)0xCFC)); return v;
}
void amdhdmi_init(EFI_SYSTEM_TABLE *st, void (*s)(uint32_t), void (*l)(const char *, const uint8_t *, uint32_t)) { ST = st; stall = s; logw = l; }

static int map(void)
{
    if (mapped) return mapped > 0;
    mapped = -1;
    for (int b = 0; b < 16; b++) for (int d = 0; d < 32; d++) {
        uint32_t id = pr(b, d, 0, 0);
        if ((id & 0xFFFF) != 0x1002 || (id >> 16) != 0x1636 || (pr(b, d, 0, 8) >> 24) != 0x03) continue;   /* Renoir만 (다른 DCN 2.1 계열은 미검증) */
        if (!(pr(b, d, 0, 4) & 2)) continue;
        uint32_t bar = pr(b, d, 0, 0x24);
        if ((bar & 1) || !(bar & 0xFFFFFFF0u)) continue;
        mm = (volatile uint32_t *)(uintptr_t)(bar & 0xFFFFFFF0u);
        if (mm[0x34C0u + 0x1f1cu] == 0xFFFFFFFFu) { mm = 0; continue; }
        mapped = 1; return 1;
    }
    return 0;
}
static int hpd1(void) { return mm && (mm[0x34C0u + 0x1f1cu] & 2u) != 0; }       /* HPD1 = HDMI 단자 (실측: 케이블 있음 0x12) */
int amdhdmi_avail(void) { return map() && hpd1() ? 0x1636 : 0; }

/* ---- ACPI VFCT에서 1002:1636 VBIOS 이미지 (v53 amd_hdmi.inc ah_vbios) ---- */
static int atom_ok(const uint8_t *b, uint32_t len)
{
    if (len < 0x4C || b[0] != 0x55 || b[1] != 0xAA) return 0;
    uint32_t h = (uint32_t)b[0x48] | ((uint32_t)b[0x49] << 8);
    if (h + 8 > len) return 0;
    const uint8_t *a = b + h + 4;
    return (a[0] == 'A' && a[1] == 'T' && a[2] == 'O' && a[3] == 'M') || (a[0] == 'M' && a[1] == 'O' && a[2] == 'T' && a[3] == 'A');
}
static int vbios(const uint8_t **img, uint32_t *len)
{
    static const EFI_GUID acpi2 = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
    const uint8_t *rsdp = 0, *vfct = 0; uint32_t vl = 0;
    struct { EFI_GUID g; void *t; } *ct = (void *)ST->ConfigurationTable;
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        const uint8_t *a = (const uint8_t *)&ct[i].g, *b = (const uint8_t *)&acpi2; int same = 1;
        for (int k = 0; k < 16; k++) if (a[k] != b[k]) same = 0;
        if (same) { rsdp = (const uint8_t *)ct[i].t; break; }
    }
    if (!rsdp) return 0;
    uint64_t xs = 0; for (int k = 0; k < 8; k++) xs |= (uint64_t)rsdp[24 + k] << (8 * k);
    const uint8_t *x = (const uint8_t *)(uintptr_t)xs;
    if (!x || x[0] != 'X' || x[1] != 'S' || x[2] != 'D' || x[3] != 'T') return 0;
    uint32_t xl = (uint32_t)x[4] | (uint32_t)x[5] << 8 | (uint32_t)x[6] << 16 | (uint32_t)x[7] << 24;
    if (xl < 36 || xl > 0x10000) return 0;
    for (uint32_t o = 36; o + 8 <= xl && !vfct; o += 8) {
        uint64_t tp = 0; for (int k = 0; k < 8; k++) tp |= (uint64_t)x[o + k] << (8 * k);
        const uint8_t *t = (const uint8_t *)(uintptr_t)tp;
        if (t && t[0] == 'V' && t[1] == 'F' && t[2] == 'C' && t[3] == 'T') { vfct = t; vl = (uint32_t)t[4] | (uint32_t)t[5] << 8 | (uint32_t)t[6] << 16 | (uint32_t)t[7] << 24; }
    }
    if (!vfct || vl < 0x4C + 28 || vl > 0x200000) return 0;
    uint32_t off = (uint32_t)vfct[0x34] | (uint32_t)vfct[0x35] << 8 | (uint32_t)vfct[0x36] << 16 | (uint32_t)vfct[0x37] << 24;
    while (off + 28 <= vl) {
        const uint8_t *h = vfct + off;
        uint16_t ven = (uint16_t)(h[12] | h[13] << 8), did = (uint16_t)(h[14] | h[15] << 8);
        uint32_t ilen = (uint32_t)h[24] | (uint32_t)h[25] << 8 | (uint32_t)h[26] << 16 | (uint32_t)h[27] << 24;
        if (!ilen || off + 28 + ilen > vl) break;
        if (ven == 0x1002 && did == 0x1636 && atom_ok(h + 28, ilen)) { *img = h + 28; *len = ilen; return 1; }
        off += 28 + ilen;
    }
    return 0;
}

static void savelog(const char *tag)
{
    static char fb[48000]; uint32_t n = 0;
    static char hl[400][64];
    while (*tag && n < sizeof fb - 4) fb[n++] = *tag++;
    fb[n++] = '\r'; fb[n++] = '\n';
    int cnt = amddisp_hist(hl, 400);
    for (int i = 0; i < cnt; i++) { for (int c = 0; hl[i][c] && n < sizeof fb - 4; c++) fb[n++] = hl[i][c]; fb[n++] = '\r'; fb[n++] = '\n'; }
    if (logw) logw("0:\\LOG\\AMD_HDMI.TXT", (const uint8_t *)fb, n);
}

static int pan_off;      /* 내장 패널 백라이트를 우리가 꺼 둠 */
static void panel_sync(int mode)   /* mode 2 = HDMI만(내장 패널 끔), 1 = 복사 */
{
    if (mode == 2 && !pan_off) { int r = amddisp_panel((volatile uint32_t *)mm, 1); pan_off = (r == 0); savelog(r == 0 ? "panel off" : "panel off FAILED (internal stays on)"); }
    else if (mode != 2 && pan_off) { amddisp_panel((volatile uint32_t *)mm, 0); pan_off = 0; savelog("panel on"); }
}
int amdhdmi_set(int mode)
{
    if (!map()) return 0;
    if (!mode) {
        if (!state) return 0;
        if (pan_off) { amddisp_panel((volatile uint32_t *)mm, 0); pan_off = 0; }   /* 내장 패널 먼저 되살림 */
        amddisp_off((volatile uint32_t *)mm);                    /* 원래대로 */
        savelog("off");
        state = fresh = 0;
        return 0;
    }
    if (state) { panel_sync(mode); return 1; }
    if (!hpd1()) return 0;
    const uint8_t *img = 0; uint32_t il = 0;
    if (!vbios(&img, &il)) { if (logw) logw("0:\\LOG\\AMD_HDMI.TXT", (const uint8_t *)"VFCT: no 1002:1636 VBIOS\r\n", 26); return 0; }
    amddisp_setbios(img, il);
    amddisp_timing(stall);
    int rc = amddisp_on((volatile uint32_t *)mm);
    savelog(rc ? (rc == 1 ? "FAIL: internal screen state bad" : rc == 2 ? "FAIL: VBIOS parse" : "FAIL: bus/edp bad during write") : "ON");
    if (rc) { amddisp_off((volatile uint32_t *)mm); return 0; }
    state = 1; fresh = 0; lostf = 0; on_ms = 0;
    panel_sync(mode);
    return 1;
}
int amdhdmi_audio_fresh(void) { int f = fresh; fresh = 0; return f; }
int amdhdmi_lost(void) { int r = lostf; lostf = 0; return r; }
void amdhdmi_tick(uint32_t now_ms)
{
    if (!state) return;
    if (!on_ms) on_ms = now_ms ? now_ms : 1;
    if (!fresh && now_ms - on_ms >= 3000 && on_ms != 0xFFFFFFFFu) { fresh = 1; on_ms = 0xFFFFFFFFu; }   /* 3초 뒤 1회 (Intel과 같은 시점) */
    if (now_ms - last_ms < 1000) return;
    last_ms = now_ms;
    if (!hpd1()) { lostf = 1; }
}
