/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
#include "../src/vdp.h"
#include <stdio.h>
int main(void)
{
    uint8_t raw[256] = {0}, rgb[256][3], pi[256]; int fails = 0;
    for (int i = 0; i < 4; i++) raw[i] = 0xF8;                                  /* Y=31, J=K=0 → 흰색 */
    /* 두 번째 묶음: Y=16, K=+10 (0b001010: 아래 3비트 2, 위 3비트 1), J=0 → 초록이 셈 */
    raw[4] = (16 << 3) | 2; raw[5] = (16 << 3) | 1; raw[6] = 16 << 3; raw[7] = 16 << 3;
    /* 세 번째 묶음: Y=16, J=+10, K=0 → 빨강이 셈 */
    raw[8] = 16 << 3; raw[9] = 16 << 3; raw[10] = (16 << 3) | 2; raw[11] = (16 << 3) | 1;
    /* YAE: A 비트 → 팔레트 번호 */
    raw[12] = (5 << 4) | 0x08;
    vdp_yjk_line(raw, 0, rgb, pi);
    printf("white  %3d %3d %3d\n", rgb[0][0], rgb[0][1], rgb[0][2]); fails += !(rgb[0][0] == 255 && rgb[0][1] == 255 && rgb[0][2] == 255);
    printf("K+10   %3d %3d %3d\n", rgb[4][0], rgb[4][1], rgb[4][2]); fails += !(rgb[4][1] > rgb[4][0] && rgb[4][1] > rgb[4][2]);
    printf("J+10   %3d %3d %3d\n", rgb[8][0], rgb[8][1], rgb[8][2]); fails += !(rgb[8][0] > rgb[8][1] && rgb[8][0] > rgb[8][2]);
    vdp_yjk_line(raw, 1, rgb, pi);
    printf("YAE palette index %d\n", pi[12]); fails += pi[12] != 5;
    printf("yjk: %s\n", fails ? "FAILED" : "all passed");
    return fails;
}
