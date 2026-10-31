/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * vdpk.h - VDP 그리기 점 커널 (7단계: 자주 도는 곳을 어셈블러로)
 *
 * 레고 방식: 같은 일을 하는 C판(기준)과 어셈블러판(x86-64 NASM, SSSE3)을 함수표로 바꿔 끼움.
 * 기본은 C판 (이미 검증된 루틴). 어셈블러판은 ini `VdpAsm=1` 일 때만 쓰고, 시험(tests/vdpktest.c)이 두 판의 결과가
 * 모든 입력에서 바이트까지 같음을 확인한다. C판은 호스트 시험·레거시 빌드·어셈블러가 없는 곳의 기준으로 남는다.
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
extern const VdpKernels vdpk_x64;      /* 어셈블러판 — VDPK_ASM 으로 빌드했을 때만 있음 */
extern const VdpKernels *vdpk;         /* 지금 쓰는 표 (기본 vdpk_c) */
extern int vdpk_asm_on;                /* ini VdpAsm — vdpk_apply() 가 표를 고름 */
int vdpk_have_asm(void);               /* 어셈블러판이 빌드에 들어 있고 CPU 가 SSSE3 를 지원하면 1 */
void vdpk_apply(void);                 /* vdpk_asm_on 에 따라 vdpk 를 바꿈 */

#endif
