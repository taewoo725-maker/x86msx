/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vdpktest_ms.c - UEFI(Microsoft x64 호출 규칙)에서 쓰는 레지스터 배치를 호스트에서 시험: 같은 어셈블러를 FORCE_MSABI 로 만들어
 * ms_abi 함수로 부르고 C판과 견줌. 인자를 받는 레지스터가 맞는지, 상위 32비트가 쓰레기인 int 인자(n)를 제대로 다루는지, (XMM6 이상·rbx·rsi·rdi 는 어셈블러 소스가 아예 안 씀) */
#include "../src/vdpk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MS __attribute__((ms_abi))
extern MS void vdpk_ms_put256(uint8_t *o, const uint8_t *l);
extern MS void vdpk_ms_nib16(uint8_t *o, const uint8_t *src, int n, const uint8_t *map16);
extern MS void vdpk_ms_crumb4(uint8_t *o, const uint8_t *src, int n, const uint8_t *map4);
extern MS void vdpk_ms_glyph8(uint8_t *o, const uint8_t *pat, const uint8_t *fg, const uint8_t *bg, int n);
static uint32_t rs = 777;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
int main(void)
{
    static uint8_t src[600], oa[1100], ob[1100];
    int fails = 0;
    for (int it = 0; it < 20000; it++) {
        for (int i = 0; i < 600; i++) src[i] = (uint8_t)rnd();
        uint8_t m16[16], m4[4];
        for (int i = 0; i < 16; i++) m16[i] = (uint8_t)rnd();
        for (int i = 0; i < 4; i++) m4[i] = (uint8_t)rnd();
        int n = 16 * (1 + (int)(rnd() % 16));
        memset(oa, 0xA5, sizeof oa); memset(ob, 0xA5, sizeof ob);
        vdpk_c.put256(oa, src); vdpk_ms_put256(ob, src);
        vdpk_c.nib16(oa + 512, src, n, m16); vdpk_ms_nib16(ob + 512, src, n, m16);
        if (memcmp(oa, ob, sizeof oa)) fails++;
        memset(oa, 0xA5, sizeof oa); memset(ob, 0xA5, sizeof ob);
        vdpk_c.crumb4(oa, src, n, m4); vdpk_ms_crumb4(ob, src, n, m4);
        if (memcmp(oa, ob, sizeof oa)) fails++;
        memset(oa, 0xA5, sizeof oa); memset(ob, 0xA5, sizeof ob);
        n = 2 * (1 + (int)(rnd() % 32));
        vdpk_c.glyph8(oa + 8, src, src + 100, src + 200, n); vdpk_ms_glyph8(ob + 8, src, src + 100, src + 200, n);   /* 다섯째 인자(n)는 스택으로 */
        if (memcmp(oa, ob, sizeof oa)) fails++;
    }
    printf("vdpktest_ms: %s\n", fails ? "실패" : "OK (MS x64 레지스터 배치에서도 C판과 같음)");
    return fails ? 1 : 0;
}
