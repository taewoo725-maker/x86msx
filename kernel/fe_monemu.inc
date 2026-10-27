/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_monemu.inc - 3.2.11 모니터 에뮬레이션과 화면 효과 (다른 에뮬레이터와 같은 방식으로 다시 만듦)
 * fe.c의 present()가 포함해 쓴다. 32비트 색 화면에서만 (8비트 색 화면은 스캔라인만).
 *
 * 속성 → 비디오 → 모니터 에뮬레이션 (blueMSX의 같은 목록):
 *   0 없음            : 그대로
 *   1 모니터 (RGB)    : RGB 입력 모니터 — 빔이 옆 점으로 조금 번짐(1:6:1), 색은 그대로
 *   2 Yc (S-비디오)   : 밝기(Y)는 또렷하게, 색(C)만 옆으로 조금 번짐(세모 5점) — S단자
 *   3 Yc + 잡음
 *   4 컴포지트        : 밝기도 조금 번지고([1 2 6 2 1]), 색은 더 넓게(세모 7점) 번지며 채도 95 % — 일반 TV 비디오 단자
 *   5 컴포지트 + 잡음
 *   6 Scale 2x        : 2배 확대 (Scale2x/EPX 규칙, blueMSX·openMSX ScaleNx·fMSX와 같은 규칙)
 *   7 Hq2x            : 2배 확대, YUV로 닮은 색을 가려 대각선을 섞음 (hq2x 방식)
 * 속성 → 효과 (openMSX의 세기 설정과 같은 뜻, 0~100):
 *   스캔라인 scanline : 각 줄 마지막 화면 줄을 그만큼 어둡게
 *   흐림 blur         : 가로로 옆 점과 섞음 (100 = 절반)
 *   잔상 glow         : 앞 프레임이 남아 서서히 사라짐 (100 = 80 %씩 남음)
 *   잡음 noise        : 매 프레임 바뀌는 점 잡음 (100 = ±40), "+ 잡음" 방식은 기본 ±14를 더함
 * 줄마다 RGB(화면 형식 32비트)를 emu_rgb에 모으고(잔상은 glow_buf), 바뀐 줄(2배 방식은 위·아래 줄도)만 다시 그린다.
 * 잡음이 있으면 매 프레임 모든 줄을 다시 그린다. */

static uint32_t emu_rgb[VDP_H][VDP_W];         /* 줄 RGB (화면 픽셀 형식) */
static uint32_t glow_buf[VDP_H][VDP_W];        /* 잔상을 섞은 줄 */
static uint8_t emu_chg[VDP_H + 2], emu_low[VDP_H], glow_left[VDP_H];
static uint16_t emu_xs[4096];                  /* 출력 열 → 원본 열 (방식마다) */
static int emu_last_mode = -1, emu_last_gw, emu_last_w, glow_on_last;
static uint32_t emu_seed = 12345;

/* 화면 형식 픽셀 ↔ R·G·B (D.bgr이면 R이 비트 16) */
static inline int ch_r(uint32_t c) { return D.bgr ? (int)(c >> 16) & 255 : (int)c & 255; }
static inline int ch_g(uint32_t c) { return (int)(c >> 8) & 255; }
static inline int ch_b(uint32_t c) { return D.bgr ? (int)c & 255 : (int)(c >> 16) & 255; }
static inline uint32_t mk_px(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return D.bgr ? ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b : ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}
/* 세 채널을 같은 비율(f/256, f ≤ 256)로 */
static inline uint32_t px_scale(uint32_t c, uint32_t f)
{
    return ((((c & 0xFF00FFu) * f) >> 8) & 0xFF00FFu) | ((((c & 0x00FF00u) * f) >> 8) & 0x00FF00u);
}
/* 섞기: (a*wa + b*wb) / (wa+wb), 채널마다 */
static inline uint32_t px_mix3(uint32_t a, uint32_t b, uint32_t c, uint32_t wa, uint32_t wb, uint32_t wc)
{
    uint32_t s = wa + wb + wc;
    uint32_t r = ((uint32_t)ch_r(a) * wa + (uint32_t)ch_r(b) * wb + (uint32_t)ch_r(c) * wc) / s;
    uint32_t g = ((uint32_t)ch_g(a) * wa + (uint32_t)ch_g(b) * wb + (uint32_t)ch_g(c) * wc) / s;
    uint32_t bb = ((uint32_t)ch_b(a) * wa + (uint32_t)ch_b(b) * wb + (uint32_t)ch_b(c) * wc) / s;
    return mk_px((int)r, (int)g, (int)bb);
}

static inline uint32_t px_mix(uint32_t a, uint32_t b, uint32_t wa, uint32_t wb) { return px_mix3(a, b, b, wa, wb, 0); }

/* 한 줄의 RGB (present의 32비트 길과 같은 색 계산) */
static int emu_ly = -1;     /* 4.0.31: 줄마다 팔레트를 쓸 줄 번호 (-1이면 안 씀) */
static void emu_line_rgb(const uint8_t *s0, int dr, const uint8_t *ov, uint32_t *out)
{
    if (dr >= 2) {                                      /* YJK */
        uint8_t raw[256], rgb[256][3], pi[256];
        for (int x = 0; x < 256; x++) raw[x] = s0[2 * x];
        vdp_yjk_line(raw, dr == 3, rgb, pi);
        for (int sx = 0; sx < 256; sx++) {
            uint8_t r8 = rgb[sx][0], g8 = rgb[sx][1], b8 = rgb[sx][2];
            monitor_tint(&r8, &g8, &b8);
            uint32_t c = ov[sx] != 0xFF ? g7_32[ov[sx]] : pi[sx] != 0xFF ? px32(pi[sx]) : mk_px(r8, g8, b8);
            out[2 * sx] = out[2 * sx + 1] = c;
        }
    } else if (dr) {
        for (int x = 0; x < VDP_W; x++) out[x] = g7_32[s0[x]];
    } else {
        if (emu_ly >= 0) for (int x = 0; x < VDP_W; x++) out[x] = snap32[M->vdp.line_pal[emu_ly]][s0[x] & 15];
        else for (int x = 0; x < VDP_W; x++) out[x] = px32(s0[x]);
    }
}

/* YUV로 닮았는지 (hq2x 문턱: Y 48, U 7, V 6) */
static inline int px_like(uint32_t a, uint32_t b)
{
    if (a == b) return 1;
    int ra = ch_r(a), ga = ch_g(a), ba = ch_b(a), rb = ch_r(b), gb = ch_g(b), bb = ch_b(b);
    int dy = ((ra - rb) * 299 + (ga - gb) * 587 + (ba - bb) * 114) / 1000;
    int du = (-(ra - rb) * 169 - (ga - gb) * 331 + (ba - bb) * 500) / 1000;
    int dv = ((ra - rb) * 500 - (ga - gb) * 419 - (ba - bb) * 81) / 1000;
    return (dy < 0 ? -dy : dy) <= 48 && (du < 0 ? -du : du) <= 7 && (dv < 0 ? -dv : dv) <= 6;
}

/* hq2x 방식 한 귀퉁이: e 가운데, b 위/아래, d 왼/오른, a 대각 */
static inline uint32_t hq_corner(uint32_t e, uint32_t b, uint32_t d, uint32_t a)
{
    int sb = px_like(e, b), sd = px_like(e, d);
    if (sb && sd) return px_like(e, a) ? e : px_mix(e, a, 7, 1);       /* 안쪽 (대각만 다르면 아주 조금) */
    if (!sb && !sd) {
        if (px_like(b, d)) return px_mix3(e, b, d, 2, 3, 3);            /* 대각선 경계: 이웃 쪽으로 */
        return px_mix3(e, b, d, 6, 1, 1);                               /* 홀로 선 점: 조금만 */
    }
    return e;                                                           /* 곧은 경계: 또렷하게 */
}

/* 2배 방식: 줄 y의 위·아래 두 줄(폭 2*gw)을 만든다 */
static void emu_2x_rows(uint32_t (*src)[VDP_W], uint32_t y, int gw, uint32_t *o0, uint32_t *o1)
{
    uint32_t n = disp_lines;
    const uint32_t *up = src[y ? y - 1 : 0], *cu = src[y], *dn = src[y + 1 < n ? y + 1 : y];
    int st = VDP_W / gw;                                                /* 256 격자면 2칸씩 */
    for (int x = 0; x < gw; x++) {
        int xl = x ? x - 1 : 0, xr = x + 1 < gw ? x + 1 : x;
        uint32_t E = cu[x * st], B = up[x * st], H = dn[x * st], D_ = cu[xl * st], F = cu[xr * st];
        uint32_t e0, e1, e2, e3;
        if (mon_emu == 6) {                                             /* Scale2x */
            e0 = (D_ == B && B != F && D_ != H) ? D_ : E;
            e1 = (B == F && B != D_ && F != H) ? F : E;
            e2 = (D_ == H && D_ != B && H != F) ? D_ : E;
            e3 = (H == F && D_ != H && B != F) ? F : E;
        } else {                                                        /* Hq2x */
            uint32_t A = up[xl * st], C = up[xr * st], G = dn[xl * st], I = dn[xr * st];
            e0 = hq_corner(E, B, D_, A);
            e1 = hq_corner(E, B, F, C);
            e2 = hq_corner(E, H, D_, G);
            e3 = hq_corner(E, H, F, I);
        }
        o0[2 * x] = e0; o0[2 * x + 1] = e1;
        o1[2 * x] = e2; o1[2 * x + 1] = e3;
    }
}

/* 한 점의 채널마다 (Y, R−Y, B−Y) */
static inline void px_ycc(uint32_t c, int *y, int *cr, int *cb)
{
    int r = ch_r(c), g = ch_g(c), b = ch_b(c);
    *y = (77 * r + 150 * g + 29 * b) >> 8; *cr = r - *y; *cb = b - *y;
}
static inline uint32_t ycc_px(int y, int cr, int cb)
{
    int r = y + cr, b = y + cb, g = (256 * y - 77 * r - 29 * b) / 150;
    return mk_px(r, g, b);
}

/* 가로 처리 (모니터·Yc·컴포지트·없음): 512칸 한 줄 + 흐림 */
static void emu_hline(const uint32_t *s, uint32_t *p)
{
    int m = mon_emu;
    if (m == 1) {                                                       /* 모니터: 빔 번짐 1:6:1 */
        for (int x = 0; x < VDP_W; x++) p[x] = px_mix3(s[x ? x - 1 : 0], s[x], s[x + 1 < VDP_W ? x + 1 : x], 1, 6, 1);
    } else if (m >= 2 && m <= 5) {                                      /* Yc·컴포지트 */
        static int Y[VDP_W], CR[VDP_W], CB[VDP_W];
        for (int x = 0; x < VDP_W; x++) px_ycc(s[x], &Y[x], &CR[x], &CB[x]);
        /* 색(C): 세모 무게 — Yc [1 2 3 2 1]/9, 컴포지트 [1 2 3 4 3 2 1]/16. 밝기(Y): Yc 그대로, 컴포지트 [1 2 6 2 1]/12, 채도 95 % */
        static const int kc_yc[5] = { 1, 2, 3, 2, 1 }, kc_cp[7] = { 1, 2, 3, 4, 3, 2, 1 };
        int comp = m >= 4, hw = comp ? 3 : 2, ks = comp ? 16 : 9, sat = comp ? 95 : 100;
        const int *kc = comp ? kc_cp : kc_yc;
        for (int x = 0; x < VDP_W; x++) {
            int y = Y[x], cr = 0, cb = 0;
            for (int k = -hw; k <= hw; k++) { int q = x + k; q = q < 0 ? 0 : q >= VDP_W ? VDP_W - 1 : q; cr += CR[q] * kc[k + hw]; cb += CB[q] * kc[k + hw]; }
            if (comp) {
                int a = Y[x > 1 ? x - 2 : 0], bb = Y[x ? x - 1 : 0], c = Y[x + 1 < VDP_W ? x + 1 : x], d = Y[x + 2 < VDP_W ? x + 2 : VDP_W - 1];
                y = (a + 2 * bb + 6 * y + 2 * c + d) / 12;
            }
            p[x] = ycc_px(y, cr * sat / (ks * 100), cb * sat / (ks * 100));
        }
    } else memcpy(p, s, VDP_W * 4);
}

/* 흐림: 옆 두 점의 평균과 w/256만큼 섞기 */
static void emu_blur(uint32_t *p, int n)
{
    if (!fx_blur) return;
    uint32_t w = (uint32_t)fx_blur * 128 / 100, prev = p[0];
    for (int x = 0; x < n; x++) {
        uint32_t cur = p[x], nx = p[x + 1 < n ? x + 1 : x];
        p[x] = px_mix3(cur, prev, nx, 512 - 2 * w, w, w);
        prev = cur;
    }
}

/* 잔상: glow_buf[y]를 emu_rgb[y] 쪽으로 (남는 비율 k %). 아직 다르면 1 */
static int glow_step(uint32_t y)
{
    int k = fx_glow * 80 / 100, left = 0;
    uint32_t *g = glow_buf[y];
    const uint32_t *c = emu_rgb[y];
    for (int x = 0; x < VDP_W; x++) {
        if (g[x] == c[x]) continue;
        int ch[3] = { ch_r(g[x]), ch_g(g[x]), ch_b(g[x]) }, to[3] = { ch_r(c[x]), ch_g(c[x]), ch_b(c[x]) };
        for (int i = 0; i < 3; i++) {
            int d = to[i] - ch[i], st = d * (100 - k) / 100;
            if (!st) st = d > 0 ? 1 : -1;
            ch[i] += st;
        }
        g[x] = mk_px(ch[0], ch[1], ch[2]);
        if (g[x] != c[x]) left = 1;
    }
    return left;
}

static void emu_render(void)
{
    uint32_t n = disp_lines;
    int glow_on = fx_glow > 0;
    if (glow_on && !glow_on_last) { memcpy(glow_buf, emu_rgb, sizeof glow_buf); memset(glow_left, 0, sizeof glow_left); }
    glow_on_last = glow_on;
    for (uint32_t y = 0; y < n; y++) {
        if (emu_chg[y]) {
            const uint32_t *s = emu_rgb[y];
            int lo = 1;
            for (int x = 0; x < VDP_W; x += 2) if (s[x] != s[x + 1]) { lo = 0; break; }
            emu_low[y] = (uint8_t)lo;
        }
        if (glow_on && (emu_chg[y] || glow_left[y])) { glow_left[y] = (uint8_t)glow_step(y); emu_chg[y] = 1; }
    }
    uint32_t (*src)[VDP_W] = glow_on ? glow_buf : emu_rgb;
    int hi = 0;
    for (uint32_t y = 0; y < n; y++) hi |= !emu_low[y];
    int is2x = mon_emu >= 6, gw = hi ? 512 : 256;
    int noise = fx_noise * 40 / 100 + (mon_emu == 3 || mon_emu == 5 ? 14 : 0);
    if (mon_emu != emu_last_mode || gw != emu_last_gw || (int)out_w != emu_last_w) {
        emu_last_mode = mon_emu; emu_last_gw = gw; emu_last_w = (int)out_w;
        uint32_t ow = out_w > 4096 ? 4096 : out_w, srcw = is2x ? 2u * (uint32_t)gw : VDP_W;
        for (uint32_t X = 0; X < ow; X++) emu_xs[X] = (uint16_t)(X * srcw / ow);
        for (uint32_t y = 0; y < n; y++) emu_chg[y] = 1;
    }
    static uint8_t need[VDP_H];
    for (uint32_t y = 0; y < n; y++)
        need[y] = noise || emu_chg[y] || (is2x && ((y && emu_chg[y - 1]) || (y + 1 < n && emu_chg[y + 1])));
    memset(emu_chg, 0, sizeof emu_chg);
    static uint32_t row[4096], p0[1024], p1[1024], ph[VDP_W];
    uint32_t ow = out_w > 4096 ? 4096 : out_w;
    for (uint32_t y = 0; y < n; y++) {
        if (!need[y]) continue;
        uint32_t ry = row_y[y + voff], rh_ = row_y[y + voff + 1] - ry;
        if (is2x) { emu_2x_rows(src, y, gw, p0, p1); emu_blur(p0, 2 * gw); emu_blur(p1, 2 * gw); }
        else { emu_hline(src[y], ph); emu_blur(ph, VDP_W); }
        if (ry >= pres_clip_y) continue;                    /* 3.7.0: 믹서 팝업 자리 */
        if (ry + rh_ > pres_clip_y) rh_ = pres_clip_y - ry;
        for (uint32_t r = 0; r < rh_; r++) {
            uint32_t *dst = (uint32_t *)(D.fb + (uintptr_t)(ry + r) * D.pitch + x0 * 4u);
            const uint32_t *sp = is2x ? ((r * 2 < rh_) ? p0 : p1) : ph;
            for (uint32_t X = 0; X < ow; X++) row[X] = sp[emu_xs[X]];
            if (noise) {
                for (uint32_t X = 0; X < ow; X++) {
                    emu_seed = emu_seed * 1103515245u + 12345u;
                    int nz = (int)((emu_seed >> 16) % (uint32_t)(2 * noise + 1)) - noise;
                    uint32_t c = row[X];
                    row[X] = mk_px(ch_r(c) + nz, ch_g(c) + nz, ch_b(c) + nz);
                }
            }
            if (scanlines && rh_ >= 2 && r == rh_ - 1) {        /* 스캔라인 세기 */
                uint32_t f = (uint32_t)(100 - scanlines) * 256 / 100;
                for (uint32_t X = 0; X < ow; X++) row[X] = px_scale(row[X], f);
            }
            memcpy(dst, row, ow * 4);
        }
        mark_dirty(ry, rh_);
    }
}
