/* 만든이: KimTaeWoo / 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * mp4limits.h (8.0.140) - MP4 재생이 받는 영상 크기 한계를 한 곳에 모음 (순수 계산, 시험은 tests/mp4limitstest.c).
 *   예전: 1280x720 을 넘으면 거절. 이후: 1920x1080 까지 받고 MSX 화면 크기로 줄여서 보여줌. 4K 는 그대로 거절. */
#ifndef MP4LIMITS_H
#define MP4LIMITS_H
#include <stdint.h>

#define MP4LIM_MAX_WIDTH      1920u
#define MP4LIM_MAX_HEIGHT     1088u                 /* 1080 줄 영상은 16줄 단위로 1088 로 해독됨 */
#define MP4LIM_SOURCE_COLUMNS 2048u                 /* 가로 표본 위치 표(colx·cx·yb…) 크기: 폭 한계보다 커야 함 */

/* 크기를 받을 수 있는가 (잘라 낸 뒤의 크기 기준) */
static inline int mp4lim_size_ok(uint32_t width, uint32_t height)
{
    return width != 0 && height != 0 && width <= MP4LIM_MAX_WIDTH && height <= MP4LIM_MAX_HEIGHT;
}
/* 해독 그림 한 장(YUV 4:2:0)을 담는 데 필요한 바이트 (짝수로 올림 + 여유) */
static inline uint32_t mp4lim_picture_bytes(uint32_t width, uint32_t height)
{
    uint32_t w = (width + 1) & ~1u, h = (height + 1) & ~1u;
    return w * h * 3u / 2u + 4096u;
}
/* 한계 크기 그림 담는 자리 (버퍼를 한 번에 잡을 때) */
#define MP4LIM_PICTURE_BYTES_MAX (MP4LIM_MAX_WIDTH * MP4LIM_MAX_HEIGHT * 3u / 2u + 4096u)


/* 8.0.141: mp4p_open 이 코드 4 를 돌려준 "이유 번호"(mp4play.c pwhy)를 사용자에게 보일 오류 코드로 바꿈.
 * 2·10 = 크기 한도 초과 → 13, 5~9 = 해독기를 못 열었거나 첫 화면을 못 만듦 → 14, 그 밖(3·4 avcC 문제 등) = 4 그대로 */
static inline int mp4lim_error_from_why(int why)
{
    if (why == 2 || why == 10) return 13;
    if (why >= 5 && why <= 9) return 14;
    return 4;
}
#endif
