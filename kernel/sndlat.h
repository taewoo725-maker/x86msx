/* 만든이: KimTaeWoo / 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * sndlat.h (8.0.138) - 소리 목표 지연(tgt_lat)을 올리고 내리는 규칙. 하드웨어를 모르는 순수 계산이라 시험하기 쉽다.
 *   예전 문제: 목표 지연이 2800 까지 올라가면 한 프레임(800)을 쓴 뒤 3600 이 되어 넘침 기준(3840)까지 여유가 5 ms 뿐이었다.
 *   새 규칙: 상한 = 넘침 기준 - 한 프레임 - 여유(10 ms). 넘침이 나면 목표 지연을 한 단계 내린다. */
#ifndef SNDLAT_H
#define SNDLAT_H

#define SNDLAT_STEP          400    /* 모자람 한 번에 올리는 양 / 넘침 한 번에 내리는 양 */
#define SNDLAT_OLD_MAX       2800   /* 상한의 절대 한계 */
#define SNDLAT_FRAME_NOMINAL 800    /* 한 프레임에 쓰는 소리 칸 수 (60 Hz, 48 kHz) */
#define SNDLAT_SAFETY        480    /* 넘침 기준 앞에 남길 여유 (약 10 ms) */
#define SNDLAT_OVER_MARGIN   256    /* 넘침 기준 = 링 크기 - 이 값 (hda.c 와 같은 값) */

/* 목표 지연의 상한 */
int sndlat_max_target(int ring_frames);
/* 소리가 모자랐을 때 새 목표 지연 (상한을 넘지 않음) */
int sndlat_after_underrun(int target, int ring_frames);
/* 소리가 넘쳤을 때 새 목표 지연: 한 단계 내림 (base 아래로는 안 내려감) */
int sndlat_after_overflow(int target, int base);

#endif
