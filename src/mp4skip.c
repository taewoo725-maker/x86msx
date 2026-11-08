/* 만든이: KimTaeWoo / 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * mp4skip.c (8.0.139) - 설명은 mp4skip.h */
#include "mp4skip.h"

int mp4skip_nal_type(uint8_t nal_header) { return nal_header & 31; }
int mp4skip_nal_ref_idc(uint8_t nal_header) { return (nal_header >> 5) & 3; }

int mp4skip_can_skip(int enabled, uint8_t first_nal_header, int later_in_window)
{
    if (!enabled || !later_in_window) return 0;
    return mp4skip_nal_type(first_nal_header) == MP4SKIP_NAL_NONIDR_SLICE && mp4skip_nal_ref_idc(first_nal_header) == 0;
}
