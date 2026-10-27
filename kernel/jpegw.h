/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* jpegw.h - 3.2.13 영상 녹화(MJPEG AVI)용 JPEG 부호기. 플랫폼과 무관, 부동소수 없음.
 * rgb = R,G,B 바이트, stride = 한 줄 바이트 수. 돌려줌: JPEG 바이트 수, 0 = out이 모자람 */
#ifndef X86MSX_JPEGW_H
#define X86MSX_JPEGW_H
#include <stdint.h>
int jpeg_encode_rgb(const uint8_t *rgb, int w, int h, int stride, int quality, uint8_t *out, int max);
#endif
