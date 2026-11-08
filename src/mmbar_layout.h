/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* mmbar_layout.h - made by X86MV/mvbar_spec.py (--emu).  Do not edit: change mvbar_spec.py. */
#ifndef MMBAR_LAYOUT_H
#define MMBAR_LAYOUT_H
enum { XPREV = 3, XPILL = 16, PILLW = 24, XPICON = 22, XNEXT = 44, XCUR = 60, XTRK = 93, TRKW = 63, XREM = 159, XVOL = 198, XSTEP = 212, XSET = 239,
       H1 = (TRKW + 1) / 2, H2 = TRKW - H1, CLW = (TRKW + 10) / 2 };
static const char *const IC_PREV [6] = { "##....#....#", "##...##...##", "##.####.####", "##.####.####", "##...##...##", "##....#....#" };
static const char *const IC_NEXT [6] = { "#....#....##", "##...##...##", "####.####.##", "####.####.##", "##...##...##", "#....#....##" };
static const char *const IC_PAUSE[6] = { "..###..###..", "..###..###..", "..###..###..", "..###..###..", "..###..###..", "..###..###.." };
static const char *const IC_PLAY [6] = { "...#........", "...###......", "...######...", "...######...", "...###......", "...#........" };
static const char *const IC_VOL  [6] = { "...#........", "..##..#.....", "####...#....", "####...#....", "..##..#.....", "...#........" };
static const char *const IC_SET  [6] = { "...##.......", "############", "...##.......", "........##..", "############", "........##.." };
#endif
