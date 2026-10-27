/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* acpi.c - 3.3.2 PLAT-01: 레거시 부팅에서 ACPI S5(전원 끄기).
 * RSDP(EBDA 첫 1 KiB, E0000~FFFFF) → RSDT/XSDT → FADT(FACP) → DSDT의 \_S5_ 꾸러미에서 SLP_TYPa/b →
 * (필요하면 SMI_CMD에 ACPI_ENABLE) → PM1a/PM1b_CNT에 SLP_TYP << 10 | SLP_EN. 4 GiB 아래는 같은 주소로 보임(페이징 항등). */
#include <stdint.h>
#include "acpi.h"

static inline void outb_(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline void outw_(uint16_t p, uint16_t v) { __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint16_t inw_(uint16_t p) { uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int sig_is(const uint8_t *p, const char *s, int n) { for (int i = 0; i < n; i++) if (p[i] != (uint8_t)s[i]) return 0; return 1; }
static int sum_ok(const uint8_t *p, uint32_t n) { uint8_t s = 0; for (uint32_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]); return s == 0; }

static const uint8_t *find_rsdp(void)
{
    uint32_t ebda = (uint32_t)(*(volatile uint16_t *)0x40E) << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000)
        for (uint32_t a = ebda; a < ebda + 1024; a += 16) if (sig_is((const uint8_t *)a, "RSD PTR ", 8) && sum_ok((const uint8_t *)a, 20)) return (const uint8_t *)a;
    for (uint32_t a = 0xE0000; a < 0x100000; a += 16) if (sig_is((const uint8_t *)a, "RSD PTR ", 8) && sum_ok((const uint8_t *)a, 20)) return (const uint8_t *)a;
    return 0;
}

static const uint8_t *find_fadt(const uint8_t *rsdp)
{
    const uint8_t *sdt = 0;
    int ent = 4;
    if (rsdp[15] >= 2 && !rd32(rsdp + 28) && rd32(rsdp + 24)) { sdt = (const uint8_t *)rd32(rsdp + 24); ent = 8; }   /* XSDT (4 GiB 아래) */
    if (!sdt || !sig_is(sdt, "XSDT", 4)) { sdt = (const uint8_t *)rd32(rsdp + 16); ent = 4; }
    if (!sdt || (!sig_is(sdt, "RSDT", 4) && !sig_is(sdt, "XSDT", 4))) return 0;
    uint32_t len = rd32(sdt + 4);
    if (len < 36 || len > 65536) return 0;
    for (uint32_t o = 36; o + ent <= len; o += ent) {
        if (ent == 8 && rd32(sdt + o + 4)) continue;
        const uint8_t *t = (const uint8_t *)rd32(sdt + o);
        if (t && sig_is(t, "FACP", 4)) return t;
    }
    return 0;
}

/* AML 꾸러미 길이 바이트 수 */
static int pkg_len_bytes(const uint8_t *p) { return (p[0] >> 6) + 1; }
static int aml_int(const uint8_t **pp, int *v)
{
    const uint8_t *p = *pp;
    if (*p == 0x0A) { *v = p[1]; *pp = p + 2; return 0; }          /* BytePrefix */
    if (*p == 0x00 || *p == 0x01) { *v = *p; *pp = p + 1; return 0; }   /* Zero/One */
    if (*p == 0xFF) { *v = 0xFF; *pp = p + 1; return 0; }          /* Ones */
    if (*p == 0x0B) { *v = p[1] | p[2] << 8; *pp = p + 3; return 0; }   /* WordPrefix */
    if (*p == 0x0C) { *v = (int)rd32(p + 1); *pp = p + 5; return 0; }   /* DWordPrefix */
    return -1;
}

static uint16_t pm1a, pm1b;
static int slp_a = -1, slp_b = -1;
static const char *why = "not tried";

int acpi_init(void)
{
    const uint8_t *rsdp = find_rsdp();
    if (!rsdp) { why = "no RSDP"; return -1; }
    const uint8_t *f = find_fadt(rsdp);
    if (!f) { why = "no FADT"; return -1; }
    uint32_t flen = rd32(f + 4);
    pm1a = (uint16_t)rd32(f + 64); pm1b = (uint16_t)rd32(f + 68);
    uint32_t smi = rd32(f + 48);
    uint8_t en = f[52];
    const uint8_t *dsdt = (const uint8_t *)rd32(f + 40);
    if (flen >= 148 && rd32(f + 140) && !rd32(f + 144)) dsdt = (const uint8_t *)rd32(f + 140);
    if (!pm1a) { why = "no PM1a"; return -1; }
    if (!dsdt || !sig_is(dsdt, "DSDT", 4)) { why = "no DSDT"; return -1; }
    uint32_t dlen = rd32(dsdt + 4);
    if (dlen > (8u << 20)) dlen = 8u << 20;
    for (uint32_t i = 36; i + 8 < dlen; i++) {
        const uint8_t *p = dsdt + i;
        if (!sig_is(p, "_S5_", 4)) continue;
        if (!((i >= 1 && p[-1] == 0x08) || (i >= 2 && p[-2] == 0x08 && p[-1] == '\\'))) continue;   /* NameOp */
        p += 4;
        if (*p != 0x12) continue;                                    /* PackageOp */
        p++;
        p += pkg_len_bytes(p);
        p++;                                                         /* NumElements */
        int a, b;
        if (aml_int(&p, &a)) continue;
        if (aml_int(&p, &b)) b = 0;
        slp_a = a & 7; slp_b = b & 7;
        break;
    }
    if (slp_a < 0) { why = "no _S5_"; return -1; }
    if (!(inw_(pm1a) & 1) && smi && en) {                            /* SCI_EN 꺼짐 → ACPI 켜기 */
        outb_((uint16_t)smi, en);
        for (int k = 0; k < 3000000 && !(inw_(pm1a) & 1); k++) __asm__ volatile("pause");
    }
    why = "ready";
    return 0;
}

void acpi_poweroff(void)
{
    if (slp_a < 0 && acpi_init()) return;
    __asm__ volatile("cli");
    outw_(pm1a, (uint16_t)((inw_(pm1a) & 0xE3FF) | slp_a << 10 | 1 << 13));
    if (pm1b) outw_(pm1b, (uint16_t)((inw_(pm1b) & 0xE3FF) | slp_b << 10 | 1 << 13));
    for (int k = 0; k < 10000000; k++) __asm__ volatile("pause");   /* 꺼지지 않으면 돌아옴 */
    why = "S5 write did not power off";
}

const char *acpi_status(void) { return why; }
