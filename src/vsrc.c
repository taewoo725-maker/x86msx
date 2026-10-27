/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vsrc.c - 5.2.19: 화면 소스 표 (vsrc.h) */
#include "vsrc.h"

static int vdp_present(const MSX *m) { (void)m; return 1; }
static int v9_present(const MSX *m) { return m->v9 != 0; }
static int v9_active(const MSX *m) { return m->v9 && v9990_visible(m->v9); }

const VSrcInfo msx_vsrc[VSRC_N] = {
    [VSRC_VDP]   = { "VDP",   "MSX (VDP)",       vdp_present, vdp_present, 0, 0 },
    [VSRC_V9990] = { "V9990", "V9990 (Gfx9000)", v9_present,  v9_active,   0, 1 },
};

int msx_vsrc_pick(const MSX *m, int pref)
{
    if (pref >= 0 && pref < VSRC_N && msx_vsrc[pref].present(m)) return pref;
    int best = VSRC_VDP;
    for (int i = 0; i < VSRC_N; i++)
        if (msx_vsrc[i].present(m) && msx_vsrc[i].active(m) && msx_vsrc[i].prio > msx_vsrc[best].prio) best = i;
    return best;
}
