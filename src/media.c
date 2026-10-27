/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* media.c - 5.2.34: 미디어 종류 표 (media.h 설명). 확장자 목록은 4.0.34~5.2.33 fe.c dir_ok/wrong_kind 와 같음 */
#include "media.h"

const MediaKindInfo msx_media_kind[MK_N] = {
    [MK_ROM]   = { "rom",   "rom mx1 mx2 ri",          "bin", 1 },
    [MK_DISK]  = { "disk",  "dsk di1 di2 360 720",     "",    1 },
    [MK_HD]    = { "hd",    "dsk hdd ide hdi",         "",    0 },
    [MK_TAPE]  = { "tape",  "cas",                     "",    1 },
    [MK_BASIC] = { "basic", "txt asc bas",             "",    1 },
};

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int media_ext_in(const char *list, const char *name)
{
    const char *dot = 0;
    if (!list || !name) return 0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (!dot || !dot[1]) return 0;
    const char *e = dot + 1;
    for (const char *w = list; *w; ) {
        while (*w == ' ') w++;
        if (!*w) break;
        int i = 0;
        while (w[i] && w[i] != ' ' && e[i] && lc((unsigned char)e[i]) == w[i]) i++;
        if ((!w[i] || w[i] == ' ') && !e[i]) return 1;
        while (*w && *w != ' ') w++;
    }
    return 0;
}

int media_shows(int kind, const char *name, int no_zip)
{
    if (kind < 0 || kind >= MK_N) return 0;
    const MediaKindInfo *k = &msx_media_kind[kind];
    return media_ext_in(k->exts, name) || (k->zip && !(no_zip && kind == MK_BASIC) && media_ext_in("zip", name));
}

int media_accepts(int kind, const char *name)
{
    if (kind < 0 || kind >= MK_N) return 0;
    const MediaKindInfo *k = &msx_media_kind[kind];
    if (kind == MK_BASIC) return 1;                             /* BASIC 글 창은 무엇이든 (글로 읽어 봄) */
    if (kind != MK_HD && media_ext_in("zip", name)) return 1;   /* zip 은 안을 보고 다시 고름 */
    return media_ext_in(k->exts, name) || media_ext_in(k->extra, name);
}

int media_kind_guess(const char *name)
{
    static const int order[4] = { MK_ROM, MK_DISK, MK_HD, MK_TAPE };
    for (int i = 0; i < 4; i++) if (media_ext_in(msx_media_kind[order[i]].exts, name)) return order[i];
    return -1;
}

void msx_media_slots(const MSX *m, MsxMediaSlots *s)
{
    for (int k = 0; k < MSX_CART_SLOTS; k++) s->cart[k] = m->cart_slot[k] >= 0;
    int n = m->fdc_kind ? (m->prof.fdc_count > 0 ? m->prof.fdc_count : 2) : 0;   /* 기종 파일에 수가 없으면 2대 (디스크 인터페이스 기본) */
    s->disk = (unsigned char)(n > 2 ? 2 : n);
    s->tape = !msx_dev(m, MDEV_S1990);                                   /* turbo R (S1990) 은 카세트 단자가 없음 */
}
