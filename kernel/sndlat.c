/* 만든이: KimTaeWoo / 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * sndlat.c (8.0.138) - 소리 목표 지연 규칙. 설명은 sndlat.h */
#include "sndlat.h"

int sndlat_max_target(int ring_frames)
{
    int cap = ring_frames - SNDLAT_OVER_MARGIN - SNDLAT_FRAME_NOMINAL - SNDLAT_SAFETY;
    return cap < SNDLAT_OLD_MAX ? cap : SNDLAT_OLD_MAX;
}

int sndlat_after_underrun(int target, int ring_frames)
{
    int cap = sndlat_max_target(ring_frames);
    if (target >= cap) return target > cap ? cap : target;
    target += SNDLAT_STEP;
    return target > cap ? cap : target;
}

int sndlat_after_overflow(int target, int base)
{
    target -= SNDLAT_STEP;
    return target < base ? base : target;
}
