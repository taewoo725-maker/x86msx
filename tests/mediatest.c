/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 5.2.34 미디어 종류 표 시험 (src/media.c) — 예전 fe.c dir_ok/wrong_kind 식과 모든 이름·창 조합이 같아야 함 */
#include "../src/media.h"
#include <stdio.h>
#include <string.h>
static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int ext_is(const char *n, const char *e1, const char *e2, const char *e3)   /* 예전 fe.c 그대로 */
{
    const char *dot = 0;
    for (const char *p = n; *p; p++) if (*p == '.') dot = p;
    if (!dot) return 0;
    const char *e[3] = { e1, e2, e3 };
    for (int k = 0; k < 3; k++) { if (!e[k]) continue; int i = 0; while (e[k][i] && lc(dot[1 + i]) == e[k][i]) i++; if (!e[k][i] && !dot[1 + i]) return 1; }
    return 0;
}
enum { W_ROM = 1, W_DISK, W_HD, W_TAPE, W_OTHER };
/* 예전 식: 창 종류 bas/tape/hd/disk/rom */
static int old_dir_ok(int win, int bsave, const char *nm)
{
    return win == MK_BASIC ? ext_is(nm, "txt", "asc", "bas") || (!bsave && ext_is(nm, "zip", 0, 0)) : win == MK_TAPE ? ext_is(nm, "cas", 0, 0) || ext_is(nm, "zip", 0, 0) : win == MK_HD ? ext_is(nm, "dsk", "hdd", "ide") || ext_is(nm, "hdi", 0, 0) : win == MK_DISK ? ext_is(nm, "dsk", "di1", "di2") || ext_is(nm, "360", "720", 0) || ext_is(nm, "zip", 0, 0) : ext_is(nm, "rom", "mx1", "mx2") || ext_is(nm, "ri", 0, 0) || ext_is(nm, "zip", 0, 0);
}
static int old_wrong(int win, const char *nm)
{
    int rom = ext_is(nm, "rom", "mx1", "mx2") || ext_is(nm, "ri", 0, 0), dsk = ext_is(nm, "dsk", "di1", "di2") || ext_is(nm, "360", "720", 0);
    int hd = ext_is(nm, "hdd", "ide", "hdi") || ext_is(nm, "dsk", 0, 0), cas = ext_is(nm, "cas", 0, 0), zip = ext_is(nm, "zip", 0, 0);
    if (win == MK_BASIC || (zip && win != MK_HD)) return -1;
    int ok = win == MK_TAPE ? cas : win == MK_HD ? hd : win == MK_DISK ? dsk : (rom || ext_is(nm, "bin", 0, 0));
    if (ok) return -1;
    return rom ? W_ROM : dsk ? W_DISK : hd ? W_HD : cas ? W_TAPE : W_OTHER;
}
static int new_wrong(int win, const char *nm)
{
    if (media_accepts(win, nm)) return -1;
    int g = media_kind_guess(nm);
    return g == MK_ROM ? W_ROM : g == MK_DISK ? W_DISK : g == MK_HD ? W_HD : g == MK_TAPE ? W_TAPE : W_OTHER;
}
int main(void)
{
    static const char *const names[] = { "GAME.ROM", "a.rom", "b.mx1", "c.MX2", "d.ri", "e.bin", "f.dsk", "g.DI1", "h.di2", "i.360", "j.720", "k.hdd", "l.ide", "m.HDI",
        "n.cas", "o.zip", "p.ZIP", "q.txt", "r.asc", "s.bas", "t.png", "noext", "x.romx", "y.ro", "z.", ".dsk", "a.b.rom", "w.sta", "v.dskx", "u.zi" };
    int fails = 0, checks = 0;
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
        for (int win = 0; win < MK_N; win++) {
            for (int bs = 0; bs < 2; bs++) { checks++; int o = old_dir_ok(win, bs, names[i]), n = media_shows(win, names[i], bs); if (o != n) { fails++; printf("FAIL shows %s win %d bsave %d: old %d new %d\n", names[i], win, bs, o, n); } }
            checks++; int o = old_wrong(win, names[i]), n = new_wrong(win, names[i]); if (o != n) { fails++; printf("FAIL wrong %s win %d: old %d new %d\n", names[i], win, o, n); }
        }
    printf("mediatest: %d/%d passed\n", checks - fails, checks);
    return fails != 0;
}
