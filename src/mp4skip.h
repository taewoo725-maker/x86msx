/* 만든이: KimTaeWoo / 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * mp4skip.h (8.0.139) - 화면에 안 나올 프레임 중 해독을 건너뛰어도 되는 것을 가리는 규칙 (순수 계산).
 *   다른 프레임이 참조하지 않는 일반 조각(nal_unit_type 1, nal_ref_idc 0)은 건너뛰어도 뒤 프레임 해독에 영향이 없다. */
#ifndef MP4SKIP_H
#define MP4SKIP_H
#include <stdint.h>

#define MP4SKIP_NAL_NONIDR_SLICE 1

/* NAL 첫 바이트에서 종류(0~31)와 참조 등급(0~3)을 뽑음 */
int mp4skip_nal_type(uint8_t nal_header);
int mp4skip_nal_ref_idc(uint8_t nal_header);

/* 이 샘플(NAL 첫 바이트 = first_nal_header)의 해독을 건너뛰어도 되는가.
 *   enabled        : ini Mp4SkipNonRef
 *   later_in_window: 이번 호출에서 이 샘플 뒤에 더 해독할 샘플이 있음 (이 샘플이 마지막이면 화면에 나올 수 있으므로 건너뛰지 않음) */
int mp4skip_can_skip(int enabled, uint8_t first_nal_header, int later_in_window);

#endif
