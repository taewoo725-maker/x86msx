/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* iltest.c - 3.4.2 VID-06: SCREEN 5에서 쓴 바이트가 SCREEN 8에서 끼워 넣은 자리로 보이고, 되돌리면 그대로 */
#include "../src/vdp.h"
#include <stdio.h>
static VDP v;
static void reg(int r, int x) { vdp_write_ctrl(&v, (uint8_t)x); vdp_write_ctrl(&v, (uint8_t)(0x80 | r)); }
int main(void)
{
    int fails = 0;
    vdp_init(&v, 1); vdp_reset(&v);
    reg(1, 0x40); reg(0, 0x06);                         /* SCREEN 5 */
    reg(14, 0); vdp_write_ctrl(&v, 0x00); vdp_write_ctrl(&v, 0x40);
    vdp_write_data(&v, 0xAA); vdp_write_data(&v, 0xBB);
    reg(14, 4); vdp_write_ctrl(&v, 0x00); vdp_write_ctrl(&v, 0x40);      /* 0x10000 */
    vdp_write_data(&v, 0xCC);
    reg(0, 0x0E);                                       /* SCREEN 8 */
    int ok1 = v.vram[0] == 0xAA && v.vram[2] == 0xBB && v.vram[1] == 0xCC;
    printf("%s G7 view: [0]=%02X [1]=%02X [2]=%02X (AA CC BB)\n", ok1 ? "PASS" : "FAIL", v.vram[0], v.vram[1], v.vram[2]); fails += !ok1;
    reg(0, 0x06);
    int ok2 = v.vram[0] == 0xAA && v.vram[1] == 0xBB && v.vram[0x10000] == 0xCC;
    printf("%s back to SCREEN 5: [0]=%02X [1]=%02X [10000]=%02X\n", ok2 ? "PASS" : "FAIL", v.vram[0], v.vram[1], v.vram[0x10000]); fails += !ok2;
    printf("iltest: %d/2 passed\n", 2 - fails);
    return fails;
}
