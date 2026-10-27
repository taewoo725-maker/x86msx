/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_media.inc - 5.2.141 오른쪽 여백의 미디어 상태 판 (카트리지 슬롯·디스크 드라이브·하드디스크)
 * 디자인: 프로젝트 문서 X86MSX_미디어상태아이콘_디자인명세_20261025.md
 * 그림은 설계 단위(폭 280)의 좌표로 그리고, 화면 폭에 맞게 2배 크기 캔버스에 그린 뒤 반으로 줄여(2x2 평균) 내보낸다.
 * 좌표는 설계 단위 × 10 (0.5·101.4 같은 소수 자리). 상태 값(SS_*)만 보고 그림을 고른다 — 장치 이름으로 갈라지지 않음 */

#define MDP_CW_MAX 960                                  /* 캔버스 최대 폭 (화면 480 픽셀) */
#define MDP_CH_MAX 320
static uint8_t mdp_cv[MDP_CW_MAX * MDP_CH_MAX * 3];
static int mdp_cw, mdp_ch;                              /* 캔버스 크기 (화면의 2배) */

static void mdp_begin(int cw, int dh10)                 /* dh10 = 설계 높이 × 10 */
{
    if (cw > MDP_CW_MAX) cw = MDP_CW_MAX;
    mdp_cw = cw;
    mdp_ch = (int)((int64_t)cw * dh10 / 2800);
    if (mdp_ch > MDP_CH_MAX) mdp_ch = MDP_CH_MAX;
    memset(mdp_cv, 0, (size_t)mdp_cw * (size_t)mdp_ch * 3);
}
/* 캔버스 화소 ↔ 설계 단위×10 */
static inline int mdp_d(int px) { return (int)(((int64_t)(2 * px + 1) * 2800) / (2 * mdp_cw)); }   /* 화소 가운데 */
static inline int mdp_p(int d10) { return (int)((int64_t)d10 * mdp_cw / 2800); }
static void mdp_put(int x, int y, uint32_t rgb, int a)
{
    if (x < 0 || y < 0 || x >= mdp_cw || y >= mdp_ch || a <= 0) return;
    uint8_t *p = &mdp_cv[((size_t)y * (size_t)mdp_cw + (size_t)x) * 3];
    if (a >= 255) { p[0] = (uint8_t)(rgb >> 16); p[1] = (uint8_t)(rgb >> 8); p[2] = (uint8_t)rgb; return; }
    p[0] = (uint8_t)((p[0] * (255 - a) + ((rgb >> 16) & 255) * a) / 255);
    p[1] = (uint8_t)((p[1] * (255 - a) + ((rgb >> 8) & 255) * a) / 255);
    p[2] = (uint8_t)((p[2] * (255 - a) + (rgb & 255) * a) / 255);
}
/* 모양 하나: 테두리 상자(설계×10) 안의 화소마다 시험 함수 */
typedef int (*MdpTest)(int x, int y, const int *a);
static void mdp_shape(int x0, int y0, int x1, int y1, MdpTest t, const int *a, uint32_t rgb, int alpha)
{
    int px0 = mdp_p(x0) - 1, px1 = mdp_p(x1) + 1, py0 = mdp_p(y0) - 1, py1 = mdp_p(y1) + 1;
    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 >= mdp_cw) px1 = mdp_cw - 1;
    if (py1 >= mdp_ch) py1 = mdp_ch - 1;
    for (int py = py0; py <= py1; py++) {
        int dy = mdp_d(py);
        for (int px = px0; px <= px1; px++) if (t(mdp_d(px), dy, a)) mdp_put(px, py, rgb, alpha);
    }
}
static int mdp_t_rr(int x, int y, const int *a)        /* 둥근 사각형: x y w h r */
{
    int L = a[0], T = a[1], R = a[0] + a[2], B = a[1] + a[3], r = a[4];
    if (x < L || x > R || y < T || y > B) return 0;
    int cx = x < L + r ? L + r : x > R - r ? R - r : x, cy = y < T + r ? T + r : y > B - r ? B - r : y;
    int64_t dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= (int64_t)r * r;
}
static void mdp_rr(int x, int y, int w, int h, int r, uint32_t rgb, int alpha)
{
    int a[5] = { x, y, w, h, r };
    mdp_shape(x, y, x + w, y + h, mdp_t_rr, a, rgb, alpha);
}
static void mdp_rframe(int x, int y, int w, int h, int r, uint32_t fill, uint32_t line)   /* 테두리 굵기 1 단위 */
{
    mdp_rr(x, y, w, h, r, line, 255);
    mdp_rr(x + 10, y + 10, w - 20, h - 20, r > 10 ? r - 10 : 0, fill, 255);
}
static int mdp_t_circ(int x, int y, const int *a)      /* 원: cx cy r */
{
    int64_t dx = x - a[0], dy = y - a[1];
    return dx * dx + dy * dy <= (int64_t)a[2] * a[2];
}
static void mdp_circ(int cx, int cy, int r, uint32_t rgb, int alpha)
{
    int a[3] = { cx, cy, r };
    mdp_shape(cx - r, cy - r, cx + r, cy + r, mdp_t_circ, a, rgb, alpha);
}
static int mdp_t_ring(int x, int y, const int *a)      /* 고리: cx cy r 굵기 */
{
    int64_t dx = x - a[0], dy = y - a[1], d2 = dx * dx + dy * dy, ro = a[2] + a[3] / 2, ri = a[2] - a[3] / 2;
    return d2 <= ro * ro && d2 >= (ri > 0 ? ri * ri : 0);
}
static void mdp_ring(int cx, int cy, int r, int w, uint32_t rgb)
{
    int a[4] = { cx, cy, r, w };
    mdp_shape(cx - r - w, cy - r - w, cx + r + w, cy + r + w, mdp_t_ring, a, rgb, 255);
}
static int mdp_t_line(int x, int y, const int *a)      /* 굵은 선: x1 y1 x2 y2 굵기 점선켜짐 점선꺼짐 (0 = 실선) */
{
    int64_t vx = a[2] - a[0], vy = a[3] - a[1], wx = x - a[0], wy = y - a[1];
    int64_t L2 = vx * vx + vy * vy, t = L2 ? wx * vx + wy * vy : 0;
    if (t < 0) t = 0;
    if (t > L2) t = L2;
    int64_t qx = L2 ? a[0] + vx * t / L2 : a[0], qy = L2 ? a[1] + vy * t / L2 : a[1];
    int64_t dx = x - qx, dy = y - qy, hw = a[4] / 2;
    if (dx * dx + dy * dy > hw * hw) return 0;
    if (a[5] && L2) {                                   /* 점선: 선을 따라 간 거리 */
        int64_t len = 0, s = (int64_t)(t);              /* t/√L2 = 거리 → 근사: 축 방향 선만 쓰므로 |vx|+|vy| 로 */
        int64_t ax = vx < 0 ? -vx : vx, ay = vy < 0 ? -vy : vy, Ln = ax > ay ? ax : ay;
        len = s * Ln / L2;
        if (len % (a[5] + a[6]) >= a[5]) return 0;
    }
    return 1;
}
static void mdp_line(int x1, int y1, int x2, int y2, int w, uint32_t rgb, int on, int off, int alpha)
{
    int a[7] = { x1, y1, x2, y2, w, on, off };
    int lx = x1 < x2 ? x1 : x2, rx = x1 < x2 ? x2 : x1, ty = y1 < y2 ? y1 : y2, by = y1 < y2 ? y2 : y1;
    mdp_shape(lx - w, ty - w, rx + w, by + w, mdp_t_line, a, rgb, alpha);
}
static int mdp_t_quad(int x, int y, const int *a)      /* 볼록 사각형: 꼭짓점 4개 */
{
    int pos = 0, neg = 0;
    for (int i = 0; i < 4; i++) {
        int x1 = a[i * 2], y1 = a[i * 2 + 1], x2 = a[(i * 2 + 2) & 7], y2 = a[(i * 2 + 3) & 7];
        int64_t c = (int64_t)(x2 - x1) * (y - y1) - (int64_t)(y2 - y1) * (x - x1);
        if (c > 0) pos = 1; else if (c < 0) neg = 1;
    }
    return !(pos && neg);
}
static int mdp_t_sector(int x, int y, const int *a)    /* 부채꼴: cx cy r  시작 방향(ux uy) 끝 방향(vx vy) ×1000 (90도 안쪽) */
{
    int64_t dx = x - a[0], dy = y - a[1];
    if (dx * dx + dy * dy > (int64_t)a[2] * a[2]) return 0;
    int64_t c1 = (int64_t)a[3] * dy - (int64_t)a[4] * dx, c2 = (int64_t)dx * a[6] - (int64_t)dy * a[5];
    return c1 >= 0 && c2 >= 0;
}
/* 회전 (설계×10, 축 (ox,oy), cos·sin ×1000, 화면 좌표라 y 아래가 +) */
static void mdp_rot(int *x, int *y, int ox, int oy, int c, int s)
{
    int64_t dx = *x - ox, dy = *y - oy;
    *x = ox + (int)((dx * c - dy * s) / 1000);
    *y = oy + (int)((dx * s + dy * c) / 1000);
}

/* 캔버스를 2x2 평균으로 줄여 화면 (x,y)에. dim = 흐리게(%) — 0 이면 그대로 */
static void mdp_flush(uint32_t x, uint32_t y, int dim, uint32_t bg)
{
    int ow = mdp_cw / 2, oh = mdp_ch / 2;
    for (int oy = 0; oy < oh; oy++) {
        uint32_t run_c = 0xFFFFFFFFu; int run_x = 0;
        for (int ox = 0; ox <= ow; ox++) {
            uint32_t col = 0xFFFFFFFFu;
            if (ox < ow) {
                int c[3];
                for (int ch = 0; ch < 3; ch++) {
                    const uint8_t *p = &mdp_cv[((size_t)(2 * oy) * (size_t)mdp_cw + (size_t)(2 * ox)) * 3 + ch];
                    int v = (p[0] + p[3] + p[(size_t)mdp_cw * 3] + p[(size_t)mdp_cw * 3 + 3] + 2) / 4;
                    if (dim) { int b = (int)((bg >> (16 - ch * 8)) & 255); v = (v * (100 - dim) + b * dim) / 100; }
                    c[ch] = v;
                }
                col = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
            }
            if (col != run_c) {
                if (run_c != 0xFFFFFFFFu && ox > run_x) fe_fill_rgb(x + (uint32_t)run_x, y + (uint32_t)oy, (uint32_t)(ox - run_x), 1, run_c >> 16, (run_c >> 8) & 255, run_c & 255);
                run_c = col; run_x = ox;
            }
        }
    }
}
static uint32_t mdp_bg = 0x14171Bu;                    /* 판 바탕 (그릴 때 판 색으로) */
#define MDP_BG mdp_bg
static uint32_t mdp_sx(uint32_t x, int d10) { return x + (uint32_t)(mdp_p(d10) / 2); }   /* 설계 좌표 → 화면 */

/* 불빛 + 번짐 */
static void mdp_lamp(int cx, int cy, int r, int rg, uint32_t col, int glow, int ring)
{
    if (glow) mdp_circ(cx, cy, rg, col, glow);
    if (ring) mdp_circ(cx, cy, r + 10, 0x0A0B0D, 255);
    mdp_circ(cx, cy, r, col, 255);
}

/* ---- 카트리지 슬롯 (280×64) ---- */
/* 라벨에 넣을 이름 — 경로·확장자 뺀 파일 이름 (길면 text_fit 이 ... 로 줄임) */
static const char *mdp_fname(const char *n, char *o, int max)
{
    const char *b = n; int m = 0, dot = -1;
    for (const char *c = n; *c; c++) if (*c == '\\' || *c == '/') b = c + 1;
    for (; b[m] && m < max - 1; m++) { o[m] = b[m]; if (b[m] == '.') dot = m; }
    if (dot > 0 && m - dot <= 5) m = dot;
    o[m] = 0;
    return o;
}
/* 이름을 폭 max_w 안에 쓰기 — 넘치면 끝을 잘라 ... (UTF-8 글자 단위) */
static void mdp_label(uint32_t x, uint32_t y, const char *str, uint32_t max_w, uint8_t fg, int center)
{
    /* 작은 글씨: 작은 글꼴(높이 16)을 3/4 로 줄여 그림 (원래 획이 끊기지 않게 겹치는 점은 합침) */
    const UiFont *f = &font_small;
    char b[72]; int n = 0;
    while (str[n] && n < 64) { b[n] = str[n]; n++; }
    b[n] = 0;
    if (text_f(f, 1, 0, 0, b, 0, 0, 0, 0) * 3 / 4 > max_w) {
        for (;;) {
            if (n > 0) { n--; while (n > 0 && ((uint8_t)b[n] & 0xC0) == 0x80) n--; }
            b[n] = '.'; b[n + 1] = '.'; b[n + 2] = '.'; b[n + 3] = 0;
            if (n == 0 || text_f(f, 1, 0, 0, b, 0, 0, 0, 0) * 3 / 4 <= max_w) break;
        }
    }
    uint32_t tw = text_f(f, 1, 0, 0, b, 0, 0, 0, 0) * 3 / 4;
    if (center && tw < max_w) x += (max_w - tw) / 2;
    uint32_t th = (uint32_t)f->height * 3 / 4;
    if (x + tw > D.w || y + th + 1 > D.h) return;
    mark_dirty_rect(x, y, tw + 1, th + 1);
    uint32_t gc = 0;
    for (const char *q = b; *q; ) {
        uint32_t cp = utf8_next(&q);
        const FontGlyph *g = font_glyph(f, cp);
        if (!g) g = font_glyph(f, '?');
        if (!g) continue;
        const uint8_t *bits = f->bits + g->off;
        for (int c = 0; c < g->adv; c++, gc++) {
            uint32_t px = x + gc * 3 / 4;
            for (int r = 0; r < f->height; r++) {
                int a = glyph_a(f, g, bits, r, c);
                if (!a) continue;
                put_aa(D.fb + (uintptr_t)(y + (uint32_t)r * 3 / 4) * D.pitch, px, a, fg, 0xFF);
            }
        }
    }
}
/* 라벨 글자 줄 높이 (설계 단위) — 화면 글꼴이 들어가도록 */
static int mdp_tl(uint32_t iw) { int t = (int)((font_small.height * 3 / 4 + 4) * 2800 / (iw ? iw : 1)); return t < 200 ? 200 : t > 280 ? 280 : t; }
static void mdp_slot(uint32_t sx, uint32_t sy, int cw, int k, int st)
{
    mdp_begin(cw, 640);
    mdp_rr(0, 0, 2800, 640, 0, MDP_BG, 255);
    mdp_rframe(5, 5, 2790, 630, 90, 0x171B20, 0x2B323B);
    static const uint32_t lamp[5] = { 0x4A515B, 0, 0xF2C230, 0xF2C230, 0x2A2F36 };
    int on = st == SS_ON || st == SS_BUSY;
    if (on) mdp_lamp(620, 340, 50, 90, lamp[st], 89, 1); else mdp_lamp(620, 340, 50, 90, lamp[st & 7], 0, 1);
    mdp_rframe(800, 160, 1840, 400, 50, 0x0B0D10, 0x39414C);
    mdp_rr(920, 280, 1600, 90, 0, 0x000000, 255);
    mdp_line(960, 325, 2480, 325, 40, 0xC9A24A, 24, 20, 255);
    if (on) {
        mdp_rframe(960, 40, 1520, 440, 40, 0x444C58, 0x20252C);
        mdp_rr(980, 50, 1480, 30, 0, 0x6B7787, 255);
        static const int gx[6] = { 1040, 1080, 1120, 2320, 2360, 2400 };
        for (int i = 0; i < 6; i++) mdp_line(gx[i], 140, gx[i], 400, 20, 0x2B313A, 0, 0, 255);
        int tl = mdp_tl((uint32_t)mdp_cw / 2);
        mdp_rr(1200, 120, 1040, 60 + tl, 0, 0xECE6D3, 255);
        int m = M->cart[k].mapper;
        uint32_t band = cart_special[k] ? 0x2F7FC2 : (m == MAPPER_KONAMI_SCC || m == MAPPER_SCCPLUS || m == MAPPER_YAMANOOTO || m == MAPPER_MFRSCC || m == MAPPER_MFRSCCP) ? 0x2F7FC2 : 0xC2362F;
        mdp_rr(1200, 120, 1040, 60, 0, band, 255);
    }
    mdp_rr(810, 420, 1820, 140, 0, 0x1D2228, 255);
    mdp_line(800, 420, 2640, 420, 10, 0x39414C, 0, 0, 255);
    mdp_flush(sx, sy, st == SS_NONE ? 55 : 0, MDP_BG);
    /* 글자 (화면 글꼴) */
    char num[2] = { (char)('1' + k), 0 };
    uint32_t ih = (uint32_t)(mdp_ch / 2);
    text_f(SF, 1, mdp_sx(sx, 140), sy + (ih > SLH ? (ih - SLH) / 2 : 0), num, st == SS_NONE ? C_FAINT : C_TEXT, 0xFF, 0, 1);
    if (on) {
        char lb[64];
        mdp_fname(cart_name[k][0] ? cart_name[k] : cart_mapper_name(M->cart[k].mapper), lb, sizeof lb);
        mdp_label(mdp_sx(sx, 1200) + 3 * S, mdp_sx(sy, 180) + 4 - 2 * S, lb, (uint32_t)(mdp_p(1040) / 2) - 6 * S, C_BACK, 1);
    }
}

/* ---- 디스크 드라이브 (280×84) ---- */
static void mdp_fdd(uint32_t sx, uint32_t sy, int cw, int d, int st, int lit)
{
    mdp_begin(cw, 840);
    mdp_rr(0, 0, 2800, 840, 0, MDP_BG, 255);
    mdp_rframe(5, 5, 2790, 830, 70, 0xCDC7B5, 0x8A846F);
    mdp_rr(20, 20, 2760, 80, 50, 0xDDD8C8, 255);
    mdp_rr(600, 520, 1400, 160, 20, 0x14120D, 255);
    mdp_rr(640, 570, 1320, 40, 0, 0x000000, 255);
    int in = st == SS_ON || st == SS_BUSY;
    if (in) {
        mdp_rframe(720, 100, 1160, 500, 30, 0x202328, 0x0C0D10);
        mdp_rr(1020, 100, 360, 100, 0, 0xB4BAC2, 255);
        mdp_rr(1260, 115, 80, 70, 0, 0x4A4F57, 255);
        mdp_rr(780, 230, 1040, 350, 0, 0xEEEADC, 255);
        mdp_rr(780, 230, 1040, 30, 0, 0x2F7FC2, 255);
        mdp_rr(580, 580, 1440, 120, 0, 0x14120D, 255);
    }
    mdp_rr(2120, 520, 200, 100, 0, 0x8F8975, 255);
    uint32_t lc = st == SS_NONE ? 0x8A846F : st == SS_BUSY && lit ? 0xFF3D1F : in ? 0xE0A800 : 0x6B6757;
    if (st == SS_BUSY && lit) { mdp_rr(2120, 500, 200, 20, 0, lc, 179); mdp_rr(2100, 520, 240, 100, 30, lc, 179); }
    else if (in) mdp_rr(2100, 520, 240, 100, 30, lc, 89);
    mdp_rr(2140, 550, 160, 40, 0, lc, 255);
    mdp_rframe(2400, 480, 240, 160, 30, 0xBDB6A1, 0x8A846F);
    mdp_line(2460, 560, 2580, 560, 20, 0x6C664F, 0, 0, 255);
    mdp_flush(sx, sy, st == SS_NONE ? 55 : 0, MDP_BG);
    char nm[2] = { (char)('A' + d), 0 };
    uint32_t ih = (uint32_t)(mdp_ch / 2);
    text_f(SF, 1, mdp_sx(sx, 140), sy + (ih > SLH ? (ih - SLH) / 2 : 0), nm, C_BACK, 0xFF, 0, 1);
    if (in && d < 2 && disk_name[d][0]) {
        char db[64]; mdp_fname(disk_name[d], db, sizeof db);
        mdp_label(mdp_sx(sx, 780) + 3 * S, mdp_sx(sy, 260) + 5 - 2 * S, db, (uint32_t)(mdp_p(1040) / 2) - 6 * S, C_BACK, 1);
    }
}

/* ---- 하드디스크 (280×92, 위에서 본 모양) ---- */
static void mdp_hd(uint32_t sx, uint32_t sy, int cw, int st, int phase)
{
    mdp_begin(cw, 920);
    mdp_rr(0, 0, 2800, 920, 0, MDP_BG, 255);
    mdp_rframe(5, 5, 2790, 910, 90, 0x262C33, 0x434C58);
    mdp_rframe(80, 60, 1500, 800, 60, 0x1A1E23, 0x3A424D);
    int has = st == SS_ON || st == SS_BUSY;
    mdp_circ(580, 460, 370, 0x6A7482, 255);
    mdp_circ(580, 460, 350, has ? 0xB9C0C9 : 0x262C33, 255);
    mdp_ring(580, 460, 290, 12, has ? 0x8F99A5 : 0x3A424D);
    mdp_ring(580, 460, 210, 12, has ? 0x8F99A5 : 0x3A424D);
    if (st == SS_BUSY) {                                /* 플래터 빛 부채꼴 50도, 90도씩 돌림 */
        static const int ux[4] = { 1000, 0, -1000, 0 }, uy[4] = { 0, 1000, 0, -1000 };
        int p = phase & 3, vx = (ux[p] * 643 - uy[p] * 766) / 1000, vy = (ux[p] * 766 + uy[p] * 643) / 1000;
        int a[7] = { 580, 460, 340, ux[p], uy[p], vx, vy };
        mdp_shape(240, 120, 920, 800, mdp_t_sector, a, 0xFFFFFF, 56);
    }
    mdp_circ(580, 460, 80, 0x7D8794, 255);
    mdp_circ(580, 460, 70, 0xC9CFD7, 255);
    mdp_circ(580, 460, 25, 0x1A1E23, 255);
    /* 암: 쉬는 자리 0도, 일하는 중 28·36·44·36도 */
    static const int ac[4] = { 883, 809, 719, 809 }, as[4] = { 469, 588, 695, 588 };
    int c = 1000, s = 0;
    if (st == SS_BUSY) { c = ac[phase & 3]; s = as[phase & 3]; }
    int q[8] = { 1316, 198, 1014, 741, 988, 727, 1244, 162 };
    for (int i = 0; i < 4; i++) mdp_rot(&q[i * 2], &q[i * 2 + 1], 1280, 180, c, s);
    int hx = 1001, hy = 734, kx = 1340, ky = 90;
    mdp_rot(&hx, &hy, 1280, 180, c, s);
    mdp_rot(&kx, &ky, 1280, 180, c, s);
    mdp_circ(kx, ky, 60, 0xC0693A, 255);
    mdp_shape(q[2] - 20 < q[4] ? q[2] - 20 : q[4] - 20, q[7] < q[1] ? q[7] - 20 : q[1] - 20, (q[0] > q[6] ? q[0] : q[6]) + 20, (q[3] > q[5] ? q[3] : q[5]) + 20, mdp_t_quad, q, 0xC8CFD8, 255);
    mdp_circ(hx, hy, 30, 0xF1F4F7, 255);
    mdp_circ(1280, 180, 60, 0x98A1AE, 255);
    mdp_circ(1280, 180, 20, 0x22272D, 255);
    uint32_t lc = st == SS_NONE ? 0x2A2F36 : st == SS_BUSY ? 0xFF7A1A : has ? 0xF2C230 : 0x4A515B;
    mdp_lamp(2520, 240, 60, 110, lc, has ? 89 : 0, 0);
    mdp_rframe(1780, 600, 860, 180, 0, 0x14171B, 0x39414C);
    mdp_line(1820, 690, 2600, 690, 80, 0xC9A24A, 24, 20, 255);
    mdp_flush(sx, sy, st == SS_NONE ? 55 : 0, MDP_BG);
    uint32_t tx = mdp_sx(sx, 1780), ty = mdp_sx(sy, 140);
    text_f(SF, 1, tx, ty, "HDD", st == SS_NONE ? C_FAINT : C_TEXT, 0xFF, 0, 1);
    if (mdp_p(380) / 2 >= (int)SLH * 2) text_f(SF, 1, tx, ty + SLH, "IDE \xC2\xB7 SCSI", C_MUTED, 0xFF, 0, 1);
}

/* 판에 넣을 항목 (k: 0~4 슬롯, 5·6 디스크, 8 하드) — 상태 아이콘 기준(상태아이콘표 2절)을 그대로 따름:
 *   이 기종에 없음(SS_NONE)은 안 보임, 넣은 것만(빈 칸 안 보임), 넣음·읽고 쓰기 뒤 3초(md_vis_now), 메뉴·멈춤·계속 보이기면 넣은 것 모두, F6 숨김 */
static int mdp_items(int *it)
{
    int n = 0;
    if (icons_off) return 0;
    for (int k = 0; k < 5; k++)                         /* 슬롯: 예전처럼 메뉴·멈춤·계속 보이기일 때 꽂힌 것만 */
        if (ms_st[MS_SLOT1 + k] != SS_NONE && M->cart[k].present && status_show_all()) it[n++] = k;
    for (int k = 5; k <= 6; k++) if (led_now[LED_FDD1 + k - 5] != 4 && (md_vis_now >> k & 1)) it[n++] = k;
    if (ms_st[MS_HD] != SS_NONE && (md_vis_now >> 8 & 1)) it[n++] = 8;
    return n;
}
static uint32_t mdp_item_h(int k, uint32_t iw) { return iw * (uint32_t)(k <= 4 ? 64 : k <= 6 ? 84 : 92) / 280; }
/* 판 높이 (그리지 않음) */
static uint32_t mdp_height(const int *it, int n, uint32_t iw, uint32_t ip)
{
    if (!n) return 0;
    uint32_t h = ip + SLH + 8 * S, g5 = 3 * S, g14 = 7 * S;
    int grp = -1;
    for (int i = 0; i < n; i++) {
        int gk = it[i] <= 4 ? 0 : it[i] <= 6 ? 1 : 2;
        if (gk != grp) { if (grp >= 0) h += g14; h += SLH + g5; grp = gk; }
        else h += g5;
        h += mdp_item_h(it[i], iw);
    }
    return h + ip;
}
/* 판 그리기. (x,y) 왼쪽 위, 폭 pw */
static void mdp_draw(uint32_t x, uint32_t y, uint32_t pw, const int *it, int n, uint32_t ip)
{
    uint32_t iw = pw - 2 * ip, h = mdp_height(it, n, iw, ip);
    ui_rrect(x + 2 * S, y + 3 * S, pw, h, 12 * S, C_SHADOW);
    ui_rframe(x, y, pw, h, 12 * S, C_LINE, C_CARD);
    { const uint8_t *c = ui_rgb[UIC(C_CARD)]; mdp_bg = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2]; }   /* 그림 바탕 = 판 색 */
    uint32_t cy = y + ip;
    text_f(SF, 1, x + ip, cy, "MEDIA", C_TEXT, 0xFF, 0, 1);
    cy += SLH + 8 * S;
    static const char *const gname[3] = { "CARTRIDGE", "DISK", "HARD DISK" };
    int grp = -1, blink = (int)((M->frame >> 2) & 1), phase = (int)((M->frame >> 3) & 3);
    for (int i = 0; i < n; i++) {
        int k = it[i], gk = k <= 4 ? 0 : k <= 6 ? 1 : 2;
        if (gk != grp) { if (grp >= 0) cy += 7 * S; text_f(SF, 1, x + ip, cy, gname[gk], C_MUTED, 0xFF, 0, 1);
            if (gk == 2 && (hd_secs || hd2_secs) && (hd_secs ? hd_name : hd2_name)[0]) {   /* 하드디스크는 제목 줄 오른쪽에 이름 */
                char hb[64]; (void)0; uint32_t gw = text_f(SF, 1, 0, 0, gname[gk], 0, 0, 0, 0) + 10 * S;
                mdp_fname(hd_secs ? hd_name : hd2_name, hb, sizeof hb);
                if (iw > gw + 20 * S) mdp_label(x + ip + gw, cy, hb, iw - gw, C_TEXT, 0);
            }
            cy += SLH + 3 * S; grp = gk; }
        else cy += 3 * S;
        int cw = (int)iw * 2;
        if (k <= 4) mdp_slot(x + ip, cy, cw, k, ms_st[MS_SLOT1 + k] == SS_NONE ? SS_NONE : M->cart[k].present ? SS_ON : SS_OFF);
        else if (k <= 6) {
            int d = k - 5, st = d >= disk_drives() ? SS_NONE : !disk_size[d] ? SS_OFF : (ms_st[MS_FDD1 + d] == SS_BUSY && M->fdc.motor) ? SS_BUSY : SS_ON;
            mdp_fdd(x + ip, cy, cw, d, st, blink);
        } else {
            int st = (hd_secs || hd2_secs) ? (P.ms() - hd_last_ms < 200 ? SS_BUSY : SS_ON) : ms_st[MS_HD] == SS_NONE ? SS_NONE : SS_OFF;
            mdp_hd(x + ip, cy, cw, st, phase);
        }
        cy += mdp_item_h(k, iw);
    }
}
