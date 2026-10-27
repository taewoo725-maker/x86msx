/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* tinf_all.c - tinf(압축 해제, zlib 라이선스, third_party/tinf)를 경고 없이 빌드하기 위한 감싸개 */
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../third_party/tinf/tinflate.c"
#include "../third_party/tinf/tinfzlib.c"
#include "../third_party/tinf/adler32.c"
