/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* mmbar.c - 영상 아래 파란 표시줄 그림 (8.0.149). 배치·모양은 X86MV V1.11.30 의 하단바(m8bar6.inc)와 같다: 256 x 20 점, 역할 4가지 */
#include "mmbar.h"
#include <string.h>
#include "mmbar_font.h"
#include "mmbar_layout.h"   /* 숫자·아이콘 원본: X86MV/mvbar_spec.py (--emu 로 생성) */
typedef uint8_t Canvas[MMBAR_H][MMBAR_W];
static void rect(Canvas cv, int x, int y, int w, int h, int role)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (y + j >= 0 && y + j < MMBAR_H && x + i >= 0 && x + i < MMBAR_W) cv[y + j][x + i] = (uint8_t)role;
}
static void cut_rect(Canvas cv, int x, int y, int w, int h, int cut, int role)   /* 네 모서리를 cut 점씩 단계로 깎은 사각형 */
{
    for (int j = 0; j < h; j++) {
        int depth = j < h - 1 - j ? j : h - 1 - j, c = cut > depth ? cut - depth : 0;
        rect(cv, x + c, y + j, w - 2 * c, 1, role);
    }
}
static void icon(Canvas cv, const char *const rows[6], int x, int y, int role)
{
    for (int j = 0; j < 6; j++) for (int i = 0; i < 12; i++) if (rows[j][i] == '#') rect(cv, x + i, y + j, 1, 1, role);
}
static void text(Canvas cv, const char *s, int x, int y, int role)           /* 6 x 8 글꼴, 6점 간격 */
{
    for (; *s; s++, x += 6) {
        int c = (unsigned char)*s; if (c < 0x20 || c > 0x7F) c = 0x20;
        const unsigned char *g = mmbar_font + (c - 0x20) * 8;
        for (int j = 0; j < 8; j++) for (int i = 0; i < 6; i++) if (g[j] & (0x80 >> i)) rect(cv, x + i, y + j, 1, 1, role);
    }
}
static void mmss(char *out, uint32_t sec)                                    /* "MM:SS" (99:59 까지) */
{
    uint32_t m = sec / 60, s = sec % 60; if (m > 99) { m = 99; s = 59; }
    out[0] = (char)('0' + m / 10); out[1] = (char)('0' + m % 10); out[2] = ':'; out[3] = (char)('0' + s / 10); out[4] = (char)('0' + s % 10); out[5] = 0;
}
int mmbar_progress(uint32_t cur, uint32_t total)
{
    if (!total) return TRKW;
    uint64_t p = (uint64_t)TRKW * cur / total; return p > TRKW ? TRKW : (int)p;
}
int mmbar_level_of_percent(int pct)
{
    static const int pc[6] = { 100, 71, 50, 35, 25, 0 };
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 6; i++) { int d = pct > pc[i] ? pct - pc[i] : pc[i] - pct; if (d < bd) { bd = d; best = i; } }
    return best;
}
void mmbar_render(const MmBarState *st, uint8_t roles[MMBAR_H][MMBAR_W])
{
    uint8_t (*cv)[MMBAR_W] = roles;
    memset(roles, st->hidden ? MMBAR_DARK : MMBAR_BAR, (size_t)MMBAR_H * MMBAR_W);
    if (st->hidden) return;
    icon(cv, IC_PREV, XPREV, 6, MMBAR_TEXT); icon(cv, IC_NEXT, XNEXT, 6, MMBAR_TEXT);
    icon(cv, IC_VOL, XVOL, 7, MMBAR_TEXT);   icon(cv, IC_SET, XSET, 7, MMBAR_TEXT);
    char t[8]; uint32_t now = st->cur / 15, tot = st->total / 15;
    mmss(t, now); text(cv, t, XCUR, 6, MMBAR_TEXT);
    cut_rect(cv, XPILL, 2, PILLW, 16, 2, MMBAR_LIGHT);                         /* 재생 / 일시정지 알약 */
    icon(cv, st->paused ? IC_PLAY : IC_PAUSE, XPICON, 7, MMBAR_BAR);
    t[0] = '-'; mmss(t + 1, tot > now ? tot - now : 0); text(cv, t, XREM, 6, MMBAR_LIGHT);
    for (int i = 0; i < 5; i++) {                                              /* 볼륨 5칸: 높이 2 4 6 8 10 */
        int x = XSTEP + 5 * i, h = 2 * (i + 1), top = 16 - h;
        if (i + st->level < 5) rect(cv, x, top, 4, h, MMBAR_LIGHT);
        else for (int j = 0; j <= i; j++) rect(cv, x + ((j & 1) ? 2 : 0), top + 2 * j, 2, 2, MMBAR_DARK);   /* 꺼진 칸: 2 x 2 바둑판 */
    }
    int p = mmbar_progress(st->cur, st->total);
    rect(cv, XTRK, 8, H1, 4, MMBAR_DARK); rect(cv, XTRK + H1, 8, H2, 4, MMBAR_DARK);   /* 줄 */
    rect(cv, XTRK, 8, p < H1 ? p : H1, 4, MMBAR_LIGHT);                                /* 지나온 부분 */
    if (p > H1) rect(cv, XTRK + H1, 8, p - H1, 4, MMBAR_LIGHT);
    rect(cv, p + XTRK - 3, 5, 6, 10, MMBAR_DARK); rect(cv, p + XTRK - 2, 6, 4, 8, MMBAR_LIGHT);   /* 손잡이 */
}

/* ---- 8.0.152 화면 번호 이름표와 일시정지 큰 표시 (작은 캔버스에 같은 도우미로) ---- */
static void put_cut(uint8_t *cv, int stride, int w, int h, int x0, int y0, int rw, int rh, int cut, int role)   /* 모서리를 cut 점씩 깎은 사각형 */
{
    for (int j = 0; j < rh; j++) {
        int depth = j < rh - 1 - j ? j : rh - 1 - j, c = cut > depth ? cut - depth : 0;
        for (int i = c; i < rw - c; i++) if (y0 + j < h && x0 + i < w) cv[(size_t)(y0 + j) * stride + x0 + i] = (uint8_t)role;
    }
}
void mmbar_badge(int scr, uint8_t out[MMBAR_BADGE_H][MMBAR_BADGE_W])
{
    char name[5] = "SCR0";
    if (scr >= 10) { name[0] = 'S'; name[1] = 'C'; name[2] = (char)('0' + scr / 10); name[3] = (char)('0' + scr % 10); } else name[3] = (char)('0' + scr);
    memset(out, MMBAR_NONE, (size_t)MMBAR_BADGE_H * MMBAR_BADGE_W);
    put_cut(&out[0][0], MMBAR_BADGE_W, MMBAR_BADGE_W, MMBAR_BADGE_H, 0, 0, MMBAR_BADGE_W, MMBAR_BADGE_H, 2, MMBAR_BAR);
    for (int k = 0; k < 4; k++) {
        int c = (unsigned char)name[k]; const unsigned char *g = mmbar_font + (c - 0x20) * 8;
        for (int j = 0; j < 8; j++) for (int i = 0; i < 6; i++) if (g[j] & (0x80 >> i)) out[3 + j][4 + 6 * k + i] = MMBAR_TEXT;
    }
}
void mmbar_sign(uint8_t out[MMBAR_SIGN_H][MMBAR_SIGN_W])
{
    memset(out, MMBAR_NONE, (size_t)MMBAR_SIGN_H * MMBAR_SIGN_W);
    uint8_t *cv = &out[0][0];
    put_cut(cv, MMBAR_SIGN_W, MMBAR_SIGN_W, MMBAR_SIGN_H, 0, 0, 32, 40, 3, MMBAR_LIGHT);
    put_cut(cv, MMBAR_SIGN_W, MMBAR_SIGN_W, MMBAR_SIGN_H, 2, 2, 28, 36, 2, MMBAR_DARK);
    for (int j = 12; j < 28; j++) for (int i = 0; i < 4; i++) { out[j][9 + i] = MMBAR_LIGHT; out[j][20 + i] = MMBAR_LIGHT; }
}
