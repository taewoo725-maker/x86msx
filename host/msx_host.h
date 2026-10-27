/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
#ifndef MSX_HOST_H
#define MSX_HOST_H
#include "../src/msx.h"
#include <stddef.h>
int  msx_host_init(MSX *m, const char *profile_dir, char *err, size_t errlen);
void msx_host_free(MSX *m);
#endif
