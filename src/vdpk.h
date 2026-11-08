/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * vdpk.h - VDP 그리기 점 커널 (7단계: 자주 도는 곳을 어셈블러로)
 *
 * 레고 방식: 같은 일을 하는 C판(기준)과 어셈블러판(x86-64 NASM, SSSE3)을 함수표로 바꿔 끼움.
 * 8.0.159: 비교용 어셈블러판(VdpAsm)을 삭제해 C판 하나만 쓴다.
 */
#ifndef MSX_VDPK_H
#define MSX_VDPK_H

#include <stdint.h>

typedef struct {
    /* o[2x] = o[2x+1] = l[x], x < 256 */
    void (*put256)(uint8_t *o, const uint8_t *l);
    /* 4비트 점 펼치기: o[2i] = map[src[i] >> 4], o[2i+1] = map[src[i] & 15], i < n (G4: n=128 → 256점, G6: n=256 → 512점) */
    void (*nib16)(uint8_t *o, const uint8_t *src, int n, const uint8_t *map16);
    /* 2비트 점 펼치기: o[4i+k] = map[(src[i] >> (6-2k)) & 3], k < 4 (G5: n=128 → 512점) */
    void (*crumb4)(uint8_t *o, const uint8_t *src, int n, const uint8_t *map4);
    /* 글자 칸 펼치기 (SCREEN 1·2·3): o[8i+k] = (pat[i] & (0x80 >> k)) ? fg[i] : bg[i], i < n, k < 8 (n = 32, 짝수) */
    void (*glyph8)(uint8_t *o, const uint8_t *pat, const uint8_t *fg, const uint8_t *bg, int n);
} VdpKernels;

extern const VdpKernels vdpk_c;        /* C판 (기준) */
extern const VdpKernels *vdpk;         /* 지금 쓰는 표 (기본 vdpk_c) */

#endif
