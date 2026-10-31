/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vdpk.c - VDP 그리기 점 커널 C판 (기준) + 표 고르기. 어셈블러판은 src/asm/vdpk_x64.asm (vdpk.h 참고) */
#include "vdpk.h"

static void c_put256(uint8_t *o, const uint8_t *l)
{
    for (int x = 0; x < 256; x++) o[2 * x] = o[2 * x + 1] = l[x];
}

static void c_nib16(uint8_t *o, const uint8_t *src, int n, const uint8_t *map16)
{
    for (int i = 0; i < n; i++) {
        uint8_t b = src[i];
        o[2 * i] = map16[b >> 4];
        o[2 * i + 1] = map16[b & 15];
    }
}

static void c_crumb4(uint8_t *o, const uint8_t *src, int n, const uint8_t *map4)
{
    for (int i = 0; i < n; i++) {
        uint8_t b = src[i];
        for (int k = 0; k < 4; k++) o[4 * i + k] = map4[(b >> (6 - 2 * k)) & 3];
    }
}

static void c_glyph8(uint8_t *o, const uint8_t *pat, const uint8_t *fg, const uint8_t *bg, int n)
{
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 8; k++) o[8 * i + k] = (pat[i] & (0x80 >> k)) ? fg[i] : bg[i];
}

const VdpKernels vdpk_c = { c_put256, c_nib16, c_crumb4, c_glyph8 };
const VdpKernels *vdpk = &vdpk_c;
int vdpk_asm_on;

#ifdef VDPK_ASM
extern void vdpk_asm_put256(uint8_t *o, const uint8_t *l);
extern void vdpk_asm_nib16(uint8_t *o, const uint8_t *src, int n, const uint8_t *map16);
extern void vdpk_asm_crumb4(uint8_t *o, const uint8_t *src, int n, const uint8_t *map4);
extern void vdpk_asm_glyph8(uint8_t *o, const uint8_t *pat, const uint8_t *fg, const uint8_t *bg, int n);
const VdpKernels vdpk_x64 = { vdpk_asm_put256, vdpk_asm_nib16, vdpk_asm_crumb4, vdpk_asm_glyph8 };
static int has_ssse3(void)
{
    uint32_t a = 1, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return (int)((c >> 9) & 1);
}
int vdpk_have_asm(void) { return has_ssse3(); }
void vdpk_apply(void) { vdpk = (vdpk_asm_on && has_ssse3()) ? &vdpk_x64 : &vdpk_c; }
#else
int vdpk_have_asm(void) { return 0; }
void vdpk_apply(void) { vdpk = &vdpk_c; }
#endif
