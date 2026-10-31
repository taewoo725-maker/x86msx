/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_ui.inc - 팝업·메뉴 공통 디자인 ("키보드 카드", 2.17)
 *
 * 모든 창은 같은 틀을 쓰고, 모든 조작은 키보드로 된다 (아래 명령 줄에 키를 보여 줌):
 *   카드: 어두운 바탕 위의 둥근 카드 + 그림자. 머리(왼쪽 MSX 네 색 띠 + 제목 + 작은 부제, 오른쪽 상태 칩)
 *   탭: 번호 칩 [1] [2] … 가 붙은 글자, 고른 탭은 밝은 글자 + 아래 강조 줄 (Tab/Shift+Tab, 숫자 키, PgUp/PgDn)
 *   줄: 왼쪽 이름, 오른쪽 값. 고른 줄은 바탕이 밝아지고 왼쪽에 강조 띠, 값은 ‹ 값 › (←/→로 바뀜을 보여 줌)
 *       켜기/끄기는 스위치, 둘 중 하나는 나뉜 단추, 수치는 막대, 실행 항목은 "Enter ›"
 *   목록: 머리 줄 + 줄무늬, 고른 줄은 강조색, 오른쪽 스크롤 막대
 *   아래: 명령 줄 — 키 칩 [Enter] 고르기  [Esc] 닫기 … (문자열 "키 설명   키 설명"을 나눠서 그림)
 * fe.c 안에 포함되어 그 정적 함수(fe_fill, text_f …)를 쓴다. */

enum { C_BACK = 16, C_CARD, C_HEAD, C_ROWSEL, C_LINE, C_TEXT, C_MUTED, C_ACC, C_ACC2, C_OK, C_WARN, C_BAD, C_FIELD, C_SHADOW, C_CHIP, C_FAINT, C_TRACK = 48, C_OKBG, C_OKLN, C_RACK, C_RACKHI, C_RACKEAR, C_RACKIN, C_LCD, C_LCDTX, C_LCDSUB, C_KNOB, C_KNOBIN, C_SCREW, C_RACKBTN, C_RACKNUM };

/* ------------------------------------------------------------------ */
/* 3.0 마우스: 그린 자리 목록 (hit). 그릴 때 자리와 할 일을 남기고, 포인터가 가리키면(ev 0) 선택,
 * 왼쪽 클릭(ev 1)이면 동작. 키보드와 같은 선택 변수를 바꾸므로 키 안내·그림이 그대로 맞는다.
 * 같은 (fn, a, b)는 자리만 고치고, 겹치면 나중에 그린 것(stamp가 큰 것)이 이긴다 — 작은 창이 아래 창을 가림. */
struct UiHit;
typedef void (*HitFn)(const struct UiHit *h, int ev);
typedef struct UiHit { uint32_t x, y, w, h, stamp; HitFn fn; intptr_t a; int b, c; uint16_t k; } UiHit;
#define HIT_MAX 256
static UiHit hits[HIT_MAX];
static int hit_n;
static uint32_t hit_stamp;
static void hit_clear(void) { hit_n = 0; }
static UiHit *hit_add(HitFn fn, intptr_t a, int b, int c, int k, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    UiHit *q = 0;
    for (int i = 0; i < hit_n; i++) if (hits[i].fn == fn && hits[i].a == a && hits[i].b == b) { q = &hits[i]; break; }
    if (!q) {
        if (hit_n >= HIT_MAX) {                         /* 가장 오래된 것을 버림 */
            int o = 0;
            for (int i = 1; i < hit_n; i++) if (hits[i].stamp < hits[o].stamp) o = i;
            q = &hits[o];
        } else q = &hits[hit_n++];
    }
    q->fn = fn; q->a = a; q->b = b; q->c = c; q->k = (uint16_t)k;
    q->x = x; q->y = y; q->w = w; q->h = h; q->stamp = ++hit_stamp;
    return q;
}
static const UiHit *hit_at(uint32_t x, uint32_t y)
{
    const UiHit *best = 0;
    for (int i = 0; i < hit_n; i++) {
        const UiHit *q = &hits[i];
        if (x >= q->x && x < q->x + q->w && y >= q->y && y < q->y + q->h && (!best || q->stamp > best->stamp)) best = q;
    }
    return best;
}
static void ui_press(int key, int shift);               /* fe.c: 키를 누른 것처럼 */
static uint32_t ptr_x, ptr_y;                           /* 포인터 위치 */
static int menu_dirty;
/* 아무것도 안 함 (작은 창이 아래 창 클릭을 막음) */
static void hc_none(const UiHit *h, int ev) { (void)h; (void)ev; }
/* 키 칩: 누르면 그 키 (b & 1 = Shift와 함께) */
static void hc_key(const UiHit *h, int ev) { if (ev == 1 && h->k) ui_press(h->k, h->b & 1); }
/* 목록 줄: 가리키면 *a = b, 고른 줄을 한 번 더 누르면 k(없으면 Enter). c = 1이면 첫 클릭에 바로 */
static void hc_sel(const UiHit *h, int ev)
{
    int *v = (int *)h->a, was = v && *v == h->b;
    if (v && *v != h->b) { *v = h->b; menu_dirty = 1; }
    if (ev == 1 && (was || h->c)) ui_press(h->k ? h->k : 0x1C, 0);
}
/* 값 줄: 가리키면 *a = b. 누르면 값 자리(x ≥ c)의 왼쪽 반 = ←, 오른쪽 반 = →, 이름 자리 = k(없으면 Enter) */
static void hc_val(const UiHit *h, int ev)
{
    int *v = (int *)h->a;
    if (v && *v != h->b) { *v = h->b; menu_dirty = 1; }
    if (ev != 1) return;
    uint32_t vx = (uint32_t)h->c;
    if (RTL && vx) {                                    /* 3.4.4: 값 자리가 왼쪽 [x, 2x + w - vx) */
        uint32_t vr = 2 * h->x + h->w - vx;
        if (ptr_x < vr) ui_press(ptr_x < h->x + (vr - h->x) / 2 ? 0xCB : 0xCD, 0);
        else ui_press(h->k ? h->k : 0x1C, 0);
        return;
    }
    if (vx && ptr_x >= vx) ui_press(ptr_x < vx + (h->x + h->w - vx) / 2 ? 0xCB : 0xCD, 0);
    else ui_press(h->k ? h->k : 0x1C, 0);
}
/* 명령 줄의 키 이름 → 스캔 코드 (묶음 "↑/↓", "1~4" 등은 0 = 누를 수 없음) */
static int key_of_name(const char *k, int *shift)
{
    static const struct { const char *n; int k; } t[] = {
        { "Enter", 0x1C }, { "Esc", 0x01 }, { "Tab", 0x0F }, { "Space", 0x39 }, { "Backspace", 0x0E }, { "Del", 0xD3 }, { "Ins", 0xD2 },
        { "Home", 0xC7 }, { "End", 0xCF }, { "PgUp", 0xC9 }, { "PgDn", 0xD1 }, { "F1", 0x3B }, { "F2", 0x3C }, { "F3", 0x3D }, { "F4", 0x3E },
        { "F5", 0x3F }, { "F6", 0x40 }, { "F7", 0x41 }, { "F8", 0x42 }, { "F9", 0x43 }, { "F10", 0x44 }, { "F11", 0x57 }, { "F12", 0x58 },
        { "←", 0xCB }, { "→", 0xCD }, { "↑", 0xC8 }, { "↓", 0xD0 },
    };
    *shift = 0;
    if (!strncmp(k, "Shift+", 6)) { *shift = 1; k += 6; }
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(k, t[i].n)) return t[i].k;
    if (k[0] && !k[1]) {                                /* 글자·숫자 하나 */
        static const char row[] = "1234567890";
        static const char let[] = "qwertyuiopasdfghjklzxcvbnm";
        static const uint8_t lsc[] = { 0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32 };
        char c = (char)(k[0] | 32);
        for (int i = 0; row[i]; i++) if (k[0] == row[i]) return 0x02 + i;
        for (int i = 0; let[i]; i++) if (c == let[i]) return lsc[i];
    }
    return 0;
}

/* 둥근 사각형 (반지름 r픽셀) */
static void ui_rrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint8_t c)
{
    if (!w || !h) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (uint32_t i = 0; i < r; i++) {                  /* 위·아래 모서리 줄 */
        uint32_t dy = r - i, in = r;
        for (uint32_t k = r + 1; k-- > 0; ) if (k * k + dy * dy <= r * r + r) { in = r - k; break; }   /* 가장 넓은 가로 폭 */
        fe_fill(x + in, y + i, w - 2 * in, 1, c);
        fe_fill(x + in, y + h - 1 - i, w - 2 * in, 1, c);
    }
    fe_fill(x, y + r, w, h - 2 * r, c);
}
static void ui_rframe(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint8_t line, uint8_t fill)
{
    ui_rrect(x, y, w, h, r, line);
    if (w > 2 * S && h > 2 * S) ui_rrect(x + S, y + S, w - 2 * S, h - 2 * S, r > S ? r - S : 0, fill);
}
static uint32_t ui_ty(uint32_t y, uint32_t h) { return y + (h > LH ? (h - LH) / 2 : 0); }
static uint32_t ui_sty(uint32_t y, uint32_t h) { return y + (h > SLH ? (h - SLH) / 2 : 0); }
/* 5.2.190 글씨 7단계 (시안 8번): 이름표·칸 값 11, 설명 12, 단추 13, 본문 14, 소제목 16, 이름 17, 창 제목 20. 큰 화면(S>=2)은 한 단계씩 낮은 글꼴을 2배로 */
static const UiFont *ft_cap(void) { return S >= 2 ? SF : &font_tiny; }
static const UiFont *ft_btn(void) { return S >= 2 ? SF : &font_btn; }
static const UiFont *ft_title(void) { return S >= 2 ? &font_bold : &font_title; }
static uint32_t ui_sw(const char *s) { return text_f(SF, 1, 0, 0, s, 0, 0, 0, 0); }

/* 키 칩: 둥근 상자 안 작은 글자. 그린 폭 반환 */
static uint32_t ui_keychip(uint32_t x, uint32_t y, uint32_t h, const char *key, int hot)
{
    uint32_t w = ui_sw(key) + 12 * S;
    ui_rrect(x, y, w, h, 3 * S, hot ? C_ACC2 : C_CHIP);
    text_f(SF, 1, x + 6 * S, ui_sty(y, h), key, hot ? 15 : C_TEXT, 0xFF, 0, 1);
    return w;
}

/* MSX 화면 자리 전체를 바탕색으로 (창이 열린 동안) */
static int live_mixer(void);
static void ui_backdrop(void)
{
    if (live_mixer()) return;                           /* 4.0: 팝업 뒤는 MSX 화면 그대로 */
    fe_fill(x0, 0, out_w, D.h, C_BACK);
}

typedef struct { uint32_t x, y, w, h; } UiRect;

static uint32_t ui_head_h(void) { return LH + SLH + 22 * S; }

/* 카드 하나 (제목, 부제, 오른쪽 칩). 안쪽(머리 아래) 자리를 돌려줌 */
static UiRect ui_card(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *title, const char *sub, const char *chip)
{
    uint32_t r = 8 * S;
    hit_add(hc_none, (intptr_t)x, (int)y, 0, 0, x, y, w, h);  /* 3.0: 아래 창 클릭 막기 */
    ui_rrect(x + 4 * S, y + 6 * S, w, h, r, C_SHADOW);   /* 그림자 */
    ui_rframe(x, y, w, h, r, C_LINE, C_CARD);
    uint32_t hh = ui_head_h();
    ui_rrect(x + S, y + S, w - 2 * S, hh, r, C_HEAD);
    fe_fill(x + S, y + hh - r + S, w - 2 * S, r, C_HEAD);
    fe_fill(x + S, y + hh + S, w - 2 * S, S, C_LINE);
    /* 왼쪽 강조 띠: MSX 네 색 (빨강·노랑·초록·파랑) */
    static const uint8_t band[4] = { C_BAD, C_WARN, C_OK, C_ACC };
    uint32_t bx = x + 14 * S, bh = hh - 24 * S, by = y + 12 * S + S;
    if (RTL) {                                          /* 3.4.4: 띠는 오른쪽, 제목은 오른쪽 맞춤, 칩은 왼쪽 */
        bx = x + w - 25 * S;
        for (int i = 0; i < 4; i++) fe_fill(bx + (uint32_t)i * 3 * S, by, 2 * S, bh, band[i]);
        uint32_t cw = 0;
        if (chip && chip[0]) {
            cw = ui_sw(chip) + 20 * S;
            if (cw > w / 3) cw = w / 3;
            uint32_t ch = SLH + 8 * S;
            ui_rrect(x + 14 * S, y + (hh - ch) / 2 + S, cw, ch, ch / 2, C_CHIP);
            small_ell(x + 24 * S, ui_sty(y + (hh - ch) / 2 + S, ch), chip, C_TEXT, 0xFF, cw - 20 * S);
        }
        uint32_t tr = bx - 7 * S, tw = tr > x + cw + 30 * S ? tr - x - cw - 30 * S : 0;
        text_rf(ft_title(), S >= 2 ? 2 : 1, tr, y + 10 * S, title, C_TEXT, tw);
        if (sub && sub[0]) text_rf(SF, 1, tr, y + 12 * S + LH + 2 * S, sub, C_MUTED, tw);
        UiRect b = { x + S, y + hh + 2 * S, w - 2 * S, h > hh + 3 * S ? h - hh - 3 * S : 0 };
        return b;
    }
    for (int i = 0; i < 4; i++) fe_fill(bx + (uint32_t)i * 3 * S, by, 2 * S, bh, band[i]);
    uint32_t tx = bx + 18 * S, cw = 0;
    if (chip && chip[0]) {
        cw = ui_sw(chip) + 20 * S;
        if (cw > w / 3) cw = w / 3;
        uint32_t ch = SLH + 8 * S;
        ui_rrect(x + w - cw - 14 * S, y + (hh - ch) / 2 + S, cw, ch, ch / 2, C_CHIP);
        small_ell(x + w - cw - 4 * S, ui_sty(y + (hh - ch) / 2 + S, ch), chip, C_TEXT, 0xFF, cw - 20 * S);
    }
    uint32_t tw = w - (tx - x) - cw - 30 * S;
    text_f(ft_title(), S >= 2 ? 2 : 1, tx, y + 10 * S, title, C_TEXT, 0xFF, tw, 1);
    if (sub && sub[0]) small_ell(tx, y + 12 * S + LH + 2 * S, sub, C_MUTED, 0xFF, tw);
    UiRect b = { x + S, y + hh + 2 * S, w - 2 * S, h > hh + 3 * S ? h - hh - 3 * S : 0 };
    return b;
}

/* 화면 가득한 창: MSX 자리 안쪽 여백만 두고. 4.0: 작은 창은 내용에 맞는 크기로 줄여 가운데에 (card_caps) */
static void card_caps(uint32_t *w, uint32_t *h);
static UiRect card_rect(void)
{
    uint32_t m = 10 * S, w = out_w > 2 * m ? out_w - 2 * m : out_w, h = D.h > 2 * m + 6 * S ? D.h - 2 * m - 6 * S : D.h;
    uint32_t cw = 0, ch = 0;
    card_caps(&cw, &ch);
    UiRect o = { x0 + m, m, w, h };
    if (cw && cw < w) { o.x += (w - cw) / 2; o.w = cw; }
    if (ch && ch < h) { o.y += (h - ch) / 2; o.h = ch; }
    return o;
}
static UiRect ui_full_card(const char *title, const char *sub, const char *chip, UiRect *outer)
{
    ui_backdrop();
    UiRect o = card_rect();
    if (outer) *outer = o;
    return ui_card(o.x, o.y, o.w, o.h, title, sub, chip);
}

/* 명령 줄: "키 설명   키 설명 …" (두 칸 이상 띄어 나눔). 첫 낱말 = 키. 아래쪽 y에 그리고 높이 반환 */
static uint32_t ui_cmdbar_h(void) { return SLH + 22 * S; }
static void ui_cmdbar(uint32_t x, uint32_t y, uint32_t w, const char *hints)
{
    uint32_t h = ui_cmdbar_h();
    fe_fill(x, y, w, S, C_LINE);
    uint32_t cx = x + 14 * S, ch = SLH + 8 * S, cy = y + (h - ch) / 2, rx = 0;
    const char *p = hints;
    while (p && *p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char seg[96]; int n = 0;
        while (*p && n < (int)sizeof seg - 1) {
            if (p[0] == ' ' && p[1] == ' ') break;
            seg[n++] = *p++;
        }
        seg[n] = 0;
        char key[40]; int k = 0;
        const char *d = seg;
        while (*d && *d != ' ' && k < (int)sizeof key - 1) key[k++] = *d++;
        key[k] = 0;
        if (k > 1 && key[k - 1] == ':') key[k - 1] = 0;  /* "Esc: 닫기" 모양도 */
        while (*d == ' ') d++;
        uint32_t need = ui_sw(key) + 12 * S + (d[0] ? ui_sw(d) + 8 * S : 0) + 18 * S;
        if (RTL) {                                      /* 3.4.4: 오른쪽부터 [설명][키] 차례로 */
            if (!rx) rx = x + w - 14 * S;
            if (rx < x + 8 * S + need) break;
            uint32_t kw = ui_sw(key) + 12 * S, dw = d[0] ? ui_sw(d) : 0, kx = rx - kw;
            ui_keychip(kx, cy, ch, key, 0);
            uint32_t dx = d[0] ? kx - 6 * S - dw : kx;
            if (d[0]) text_f(SF, 1, dx, ui_sty(cy, ch), d, C_MUTED, 0xFF, 0, 1);
            { int sh, kc = key_of_name(key, &sh); if (kc) hit_add(hc_key, kc, sh, 0, kc, dx, cy, rx - dx, ch); }
            rx = dx - 18 * S;
            continue;
        }
        if (cx + need > x + w - 8 * S) break;           /* 넘치면 여기까지 */
        uint32_t x0c = cx;
        cx += ui_keychip(cx, cy, ch, key, 0);
        if (d[0]) { cx += 6 * S; cx += text_f(SF, 1, cx, ui_sty(cy, ch), d, C_MUTED, 0xFF, 0, 1); }
        {   /* 3.0: 칩을 누르면 그 키 */
            int sh, kc = key_of_name(key, &sh);
            if (kc) hit_add(hc_key, kc, sh, 0, kc, x0c, cy, cx - x0c, ch);
        }
        cx += 18 * S;
    }
}

/* 탭 줄: [n] 이름 … 고른 탭은 밝게 + 강조 밑줄. 높이 반환 */
static uint32_t ui_tabs_h(void) { return LH + 18 * S; }
static void ui_tabs(uint32_t x, uint32_t y, uint32_t w, const char *const *labels, int n, int sel, int focused)
{
    uint32_t h = ui_tabs_h(), tx = x + 14 * S, ch = SLH + 6 * S;
    fe_fill(x, y + h - S, w, S, C_LINE);
    if (RTL) {                                          /* 3.4.4: 오른쪽부터, 탭 안은 [이름][번호] */
        uint32_t tr = x + w - 14 * S;
        for (int t = 0; t < n; t++) {
            char num[4] = { (char)(t == 9 ? '0' : t == 10 ? '-' : '1' + t), 0 };
            int on = t == sel;
            uint32_t lw = text_w(labels[t]), cw = ui_sw(num) + 10 * S, tw = cw + 8 * S + lw + 20 * S;
            if (tr < x + tw) break;
            uint32_t tl = tr - tw;
            if (on) ui_rrect(tl, y + 4 * S, tw, h - 6 * S, 5 * S, focused ? C_ROWSEL : C_HEAD);
            ui_rrect(tr - 8 * S - cw, y + (h - ch) / 2, cw, ch, 3 * S, on ? C_ACC : C_CHIP);
            text_f(SF, 1, tr - 8 * S - cw + 5 * S, ui_sty(y + (h - ch) / 2, ch), num, on ? 15 : C_MUTED, 0xFF, 0, 1);
            text_r(tr - 16 * S - cw, ui_ty(y, h), labels[t], on ? C_TEXT : C_MUTED, lw + 4 * S);
            if (on) fe_fill(tl + 6 * S, y + h - 3 * S, tw - 12 * S, 3 * S, C_ACC);
            hit_add(hc_key, 0x02 + t, 0, 0, 0x02 + t, tl, y, tw, h);
            tr = tl - 6 * S;
        }
        return;
    }
    /* 탭이 다 안 들어가면 (큰 글자·좁은 화면, 진단 10탭) 1) 여백을 줄이고 2) 이름을 작은 글자로 3) 그래도 넘치면 고른 탭이 보이게 밀어서 그림 */
    uint32_t avail = w > 28 * S ? w - 28 * S : w, pad = 20 * S, gap = 6 * S, total = 0;
    int small = 0, first = 0;
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 1) { pad = 8 * S; gap = 2 * S; }
        if (pass == 2) small = 1;
        total = 0;
        for (int t = 0; t < n; t++) { char num[4] = { (char)(t == 9 ? '0' : t == 10 ? '-' : '1' + t), 0 }; total += ui_sw(num) + 10 * S + 8 * S + (small ? ui_sw(labels[t]) : text_w(labels[t])) + pad + gap; }
        if (total <= avail) break;
    }
    if (total > avail && sel > 0) {                      /* 고른 탭까지 들어가도록 앞을 건너뜀 */
        for (first = 0; first < sel; first++) {
            uint32_t need = 0;
            for (int t = first; t <= sel; t++) { char num[4] = { (char)(t == 9 ? '0' : t == 10 ? '-' : '1' + t), 0 }; need += ui_sw(num) + 18 * S + (small ? ui_sw(labels[t]) : text_w(labels[t])) + pad + gap; }
            if (need + 16 * S <= avail) break;
        }
        if (first > 0) { text_f(SF, 1, x + 2 * S, ui_sty(y + (h - ch) / 2, ch), "<", C_MUTED, 0xFF, 0, 1); tx += 8 * S; }
    }
    for (int t = first; t < n; t++) {
        char num[4] = { (char)(t == 9 ? '0' : t == 10 ? '-' : '1' + t), 0 };
        int on = t == sel;
        uint32_t lw = small ? ui_sw(labels[t]) : text_w(labels[t]), cw = ui_sw(num) + 10 * S, tw = cw + 8 * S + lw + pad;
        if (tx + tw > x + w) { text_f(SF, 1, x + w - 10 * S, ui_sty(y + (h - ch) / 2, ch), ">", C_MUTED, 0xFF, 0, 1); break; }
        if (on) ui_rrect(tx, y + 4 * S, tw, h - 6 * S, 5 * S, focused ? C_ROWSEL : C_HEAD);
        ui_rrect(tx + pad / 2 - 2 * S, y + (h - ch) / 2, cw, ch, 3 * S, on ? C_ACC : C_CHIP);
        text_f(SF, 1, tx + pad / 2 + 3 * S, ui_sty(y + (h - ch) / 2, ch), num, on ? 15 : C_MUTED, 0xFF, 0, 1);
        if (small) text_f(SF, 1, tx + pad / 2 - 2 * S + cw + 6 * S, ui_sty(y + (h - ch) / 2, ch), labels[t], on ? C_TEXT : C_MUTED, 0xFF, lw + 4 * S, 1);
        else text_clip(tx + pad / 2 - 2 * S + cw + 8 * S, ui_ty(y, h), labels[t], on ? C_TEXT : C_MUTED, 0xFF, lw + 4 * S);
        if (on) fe_fill(tx + 6 * S, y + h - 3 * S, tw - 12 * S, 3 * S, C_ACC);
        hit_add(hc_key, 0x02 + t, 0, 0, 0x02 + t, tx, y, tw, h);   /* 3.0: 탭을 누르면 숫자 키 */
        tx += tw + gap;
    }
}

/* 구역 머리: 강조색 작은 제목 + 가는 선. 높이 반환 */
static uint32_t ui_section(uint32_t x, uint32_t y, uint32_t w, const char *label)
{
    uint32_t tw = ui_sw(label);
    if (RTL) {                                          /* 3.4.4: 제목은 오른쪽, 선은 왼쪽으로 */
        text_rf(SF, 1, x + w - 4 * S, y + 4 * S, label, C_ACC, 0);
        if (w > tw + 18 * S) fe_fill(x + 4 * S, y + 4 * S + SLH / 2, w - tw - 18 * S, S, C_LINE);
        return SLH + 12 * S;
    }
    text_f(SF, 1, x + 4 * S, y + 4 * S, label, C_ACC, 0xFF, 0, 1);
    fe_fill(x + tw + 14 * S, y + 4 * S + SLH / 2, w > tw + 18 * S ? w - tw - 18 * S : 0, S, C_LINE);
    return SLH + 12 * S;
}

/* 값의 모양 */
enum { UV_CHOICE, UV_TOGGLE, UV_SLIDER, UV_ACTION, UV_TEXT, UV_SEG2, UV_DROP };   /* UV_DROP = 값 ▾ (누르면 풀다운) */
static int ui_tight;                                    /* 5.3.32 출력장치 창: 한 화면에 안 들어가면 줄 사이를 좁힘 (그 창을 그리는 동안만 1) */
static uint32_t ui_row_h(void) { return LH + (ui_tight ? 7 : 12) * S; }

/* 스위치 (켜기/끄기) */
static void ui_switch(uint32_t x, uint32_t y, uint32_t h, int on, int en)
{
    uint32_t sh = h * 3 / 5, sw = sh * 2, sy = y + (h - sh) / 2;
    ui_rrect(x, sy, sw, sh, sh / 2, !en ? C_LINE : on ? C_ACC : C_FAINT);
    uint32_t k = sh - 4 * S;
    ui_rrect(on ? x + sw - k - 2 * S : x + 2 * S, sy + 2 * S, k, k, k / 2, en ? 15 : C_MUTED);
}

/* 3.4.4: 오른쪽 → 왼쪽 줄. 이름은 오른쪽 끝에 맞추고, 값은 왼쪽 자리 [x + 14, vr)의 오른쪽 끝(vr)에 맞춤 (vr = vx를 좌우로 뒤집은 자리) */
static void ui_row_rtl(uint32_t x, uint32_t y, uint32_t w, uint32_t vx, const char *label, const char *value, const char *value2,
                       int kind, int on, int focused, int en)
{
    uint32_t h = ui_row_h(), xr = x + w;
    if (focused) {
        ui_rrect(x, y + S, w, h - 2 * S, 6 * S, C_ROWSEL);
        ui_rrect(xr - 4 * S, y + 4 * S, 4 * S, h - 8 * S, 2 * S, C_ACC);
    }
    if (kind == UV_TOGGLE) {                            /* [이름][스위치] */
        uint32_t sw = (h * 3 / 5) * 2;
        ui_switch(xr - 16 * S - sw, y, h, on, en);
        text_r(xr - 16 * S - sw - 14 * S, ui_ty(y, h), label, !en ? C_FAINT : C_TEXT, w > sw + 40 * S ? w - sw - 40 * S : 0);
        return;
    }
    char lb[96];
    copy_str(lb, sizeof lb, label);
    { size_t l = strlen(lb); while (l && (lb[l - 1] == ':' || lb[l - 1] == ' ')) lb[--l] = 0; }
    { size_t l = 0; while (lb[l] == ':' || lb[l] == ' ') l++; if (l) memmove(lb, lb + l, strlen(lb + l) + 1); }   /* 뒤집힌 ':'는 앞에 */
    uint32_t vr = 2 * x + w - vx, vl = x + 14 * S, vw = vr > vl ? vr - vl : 0;
    text_r(xr - 18 * S, ui_ty(y, h), lb, focused && en ? C_TEXT : en ? C_MUTED : C_FAINT, xr > vr + 26 * S ? xr - vr - 26 * S : 0);
    switch (kind) {
    case UV_CHOICE: {
        uint8_t vc = en ? C_TEXT : C_FAINT;
        uint32_t aw = text_w("‹"), tw = text_w(value), mx = vw > 2 * aw + 24 * S ? vw - 2 * aw - 24 * S : 0;
        if (tw > mx) tw = mx;
        uint32_t bx = vr > tw + 2 * aw + 16 * S ? vr - tw - 2 * aw - 16 * S : vl;
        if (focused && en) text_clip(bx, ui_ty(y, h), "‹", C_ACC, 0xFF, 0);
        text_r(bx + aw + 8 * S + tw, ui_ty(y, h), value, vc, mx);
        if (focused && en) text_clip(bx + aw + 8 * S + tw + 8 * S, ui_ty(y, h), "›", C_ACC, 0xFF, 0);
        break;
    }
    case UV_SLIDER: {
        uint32_t bw = vw * 55 / 100, bx = vr - bw, by = y + h / 2 - 2 * S, k = bw * (uint32_t)(on < 0 ? 0 : on > 1000 ? 1000 : on) / 1000;
        ui_rrect(bx, by, bw, 4 * S, 2 * S, C_LINE);
        if (k) ui_rrect(bx, by, k, 4 * S, 2 * S, en ? C_ACC : C_FAINT);
        uint32_t kd = LH * 2 / 3;
        ui_rrect(bx + (k > kd / 2 ? k - kd / 2 : 0), y + (h - kd) / 2, kd, kd, kd / 2, focused ? 15 : C_TEXT);
        text_r(bx > 16 * S ? bx - 16 * S : 0, ui_ty(y, h), value, en ? C_TEXT : C_FAINT, vw > bw + 16 * S ? vw - bw - 16 * S : 0);
        break;
    }
    case UV_ACTION: {
        uint32_t ch = SLH + 8 * S, cy = y + (h - ch) / 2, kw = ui_sw("Enter ›") + 12 * S, cx = vr > kw ? vr - kw : vl;
        if (en) ui_keychip(cx, cy, ch, "Enter ›", focused);
        if (value && value[0]) text_r(cx > 12 * S ? cx - 12 * S : 0, ui_ty(y, h), value, en ? C_TEXT : C_FAINT, vw > 90 * S ? vw - 90 * S : vw);
        break;
    }
    case UV_SEG2: {
        uint32_t a = text_w(value) + 28 * S, b = text_w(value2) + 28 * S, sh = h - 12 * S, sy = y + 6 * S, bx = vr > a + b ? vr - a - b : vl;
        ui_rrect(bx, sy, a + b, sh, 5 * S, C_FIELD);
        ui_rrect(on ? bx + a : bx, sy, on ? b : a, sh, 5 * S, focused ? C_ACC : C_ACC2);
        text_clip(bx + 14 * S, ui_ty(sy, sh), value, !on ? 15 : C_MUTED, 0xFF, 0);
        text_clip(bx + a + 14 * S, ui_ty(sy, sh), value2, on ? 15 : C_MUTED, 0xFF, 0);
        break;
    }
    default:
        text_r(vr, ui_ty(y, h), value, en ? C_TEXT : C_FAINT, vw);
        break;
    }
}

/* 한 줄. label 왼쪽, value 오른쪽(vx부터). kind = UV_*. on = 스위치 상태 / 막대 0~1000 / 나뉜 단추 고른 쪽.
 * value2 = 나뉜 단추의 둘째 글자 */
static void ui_row(uint32_t x, uint32_t y, uint32_t w, uint32_t vx, const char *label, const char *value, const char *value2,
                   int kind, int on, int focused, int en)
{
    uint32_t h = ui_row_h();
    if (RTL) { ui_row_rtl(x, y, w, vx, label, value, value2, kind == UV_DROP ? UV_CHOICE : kind, on, focused, en); return; }
    if (focused) {
        ui_rrect(x, y + S, w, h - 2 * S, 6 * S, C_ROWSEL);
        ui_rrect(x, y + 4 * S, 4 * S, h - 8 * S, 2 * S, C_ACC);
    }
    uint8_t lc = !en ? C_FAINT : focused ? C_TEXT : C_TEXT;
    if (kind == UV_TOGGLE) {                            /* 스위치 + 이름 */
        ui_switch(x + 16 * S, y, h, on, en);
        uint32_t sw = (h * 3 / 5) * 2;
        text_clip(x + 16 * S + sw + 14 * S, ui_ty(y, h), label, lc, 0xFF, w - sw - 40 * S);
        return;
    }
    char lb[96];                                        /* 줄 이름 끝의 ':'는 떼어 냄 */
    copy_str(lb, sizeof lb, label);
    { size_t l = strlen(lb); while (l && (lb[l - 1] == ':' || lb[l - 1] == ' ')) lb[--l] = 0; }
    label = lb;
    text_clip(x + 18 * S, ui_ty(y, h), label, focused && en ? C_TEXT : en ? C_MUTED : C_FAINT, 0xFF, vx > x + 26 * S ? vx - x - 26 * S : 0);
    uint32_t vw = x + w > vx + 14 * S ? x + w - vx - 14 * S : 0;
    switch (kind) {
    case UV_DROP: {                                     /* 값은 다른 글자색(C_ACC), 선택한 줄은 오른쪽에 'Enter ▼' 칩 (누르거나 Enter = 풀다운) */
        uint8_t vc = en ? C_ACC : C_FAINT;
        uint32_t ch = SLH + 6 * S, cw = ui_sw("Enter ▼") + 26 * S, lim = vw > cw + 20 * S ? vw - cw - 20 * S : vw;
        text_clip(vx, ui_ty(y, h), value, vc, 0xFF, lim);
        if (focused && en && value[0] && x + w > cw + 8 * S) ui_keychip(x + w - cw, y + (h - ch) / 2, ch, "Enter ▼", 0);
        break;
    }
    case UV_CHOICE: {
        uint8_t vc = en ? C_TEXT : C_FAINT;
        if (focused && en) {
            uint32_t aw = text_w("‹");
            text_clip(vx, ui_ty(y, h), "‹", C_ACC, 0xFF, 0);
            uint32_t tw = text_clip(vx + aw + 8 * S, ui_ty(y, h), value, vc, 0xFF, vw > 2 * aw + 24 * S ? vw - 2 * aw - 24 * S : 0);
            text_clip(vx + aw + 8 * S + tw + 8 * S, ui_ty(y, h), "›", C_ACC, 0xFF, 0);
        } else text_clip(vx + text_w("‹") + 8 * S, ui_ty(y, h), value, vc, 0xFF, vw);
        break;
    }
    case UV_SLIDER: {
        uint32_t bw = vw * 55 / 100, by = y + h / 2 - 2 * S, k = bw * (uint32_t)(on < 0 ? 0 : on > 1000 ? 1000 : on) / 1000;
        ui_rrect(vx, by, bw, 4 * S, 2 * S, C_LINE);
        if (k) ui_rrect(vx, by, k, 4 * S, 2 * S, en ? C_ACC : C_FAINT);
        uint32_t kd = LH * 2 / 3;
        ui_rrect(vx + (k > kd / 2 ? k - kd / 2 : 0), y + (h - kd) / 2, kd, kd, kd / 2, focused ? 15 : C_TEXT);
        text_clip(vx + bw + 16 * S, ui_ty(y, h), value, en ? C_TEXT : C_FAINT, 0xFF, vw > bw + 16 * S ? vw - bw - 16 * S : 0);
        break;
    }
    case UV_ACTION: {
        uint32_t ch = SLH + 8 * S, cy = y + (h - ch) / 2, cx = vx;
        if (value && value[0]) cx += text_clip(vx, ui_ty(y, h), value, en ? C_TEXT : C_FAINT, 0xFF, vw > 90 * S ? vw - 90 * S : vw) + 12 * S;
        if (en) ui_keychip(cx, cy, ch, "Enter ›", focused);
        break;
    }
    case UV_SEG2: {                                     /* [ 가 | 나 ] */
        uint32_t a = text_w(value) + 28 * S, b = text_w(value2) + 28 * S, sh = h - 12 * S, sy = y + 6 * S;
        ui_rrect(vx, sy, a + b, sh, 5 * S, C_FIELD);
        ui_rrect(on ? vx + a : vx, sy, on ? b : a, sh, 5 * S, focused ? C_ACC : C_ACC2);
        text_clip(vx + 14 * S, ui_ty(sy, sh), value, !on ? 15 : C_MUTED, 0xFF, 0);
        text_clip(vx + a + 14 * S, ui_ty(sy, sh), value2, on ? 15 : C_MUTED, 0xFF, 0);
        break;
    }
    default:
        text_clip(vx, ui_ty(y, h), value, en ? C_TEXT : C_FAINT, 0xFF, vw);
        break;
    }
}

/* 목록 줄 바탕: 고른 줄 / 줄무늬 */
static void ui_list_row_bg(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int sel, int zebra, int focused)
{
    if (sel) {
        ui_rrect(x, y, w, h, 4 * S, focused ? C_ACC2 : C_ROWSEL);
        if (focused) ui_rrect(x, y + 3 * S, 3 * S, h > 6 * S ? h - 6 * S : h, S, C_ACC);
    } else if (zebra) fe_fill(x, y, w, h, C_HEAD);
}

/* 오른쪽 스크롤 막대 */
static void ui_scrollbar(uint32_t x, uint32_t y, uint32_t h, int top, int vis, int n)
{
    if (n <= vis || n <= 0) return;
    ui_rrect(x, y, 4 * S, h, 2 * S, C_LINE);
    uint32_t th = h * (uint32_t)vis / (uint32_t)n, ty = h * (uint32_t)top / (uint32_t)n;
    if (th < 12 * S) th = 12 * S;
    if (ty + th > h) ty = h - th;
    ui_rrect(x, y + ty, 4 * S, th, 2 * S, C_MUTED);
}

/* 3.4.4 MOUSE: 끌 수 있는 스크롤 막대. top(맨 위 줄 변수)을 끌어 바꾸고, sel(고른 줄, 없으면 NULL)은 보이는 범위 안으로 당긴다.
 * 막대를 누르면 그 자리로 (손잡이를 누르면 잡은 자리 그대로), 누른 채 움직이면 따라감 (mouse_frame의 끌기) */
#define SBAR_MAX 12
static struct { int *top, *sel; int vis, n; } sbars[SBAR_MAX];
static int sbar_grab = -1;                              /* 손잡이 안 잡은 자리 (픽셀), -1 = 가운데 */
static void hc_sbar(const UiHit *h, int ev)
{
    if (ev == 0 || h->b < 0 || h->b >= SBAR_MAX) return;
    int *top = sbars[h->b].top, *sel = sbars[h->b].sel, vis = sbars[h->b].vis, n = sbars[h->b].n;
    if (!top || n <= vis || !h->h) return;
    uint32_t H = h->h, th = H * (uint32_t)vis / (uint32_t)n, ty = H * (uint32_t)*top / (uint32_t)n;
    if (th < 12 * S) th = 12 * S;
    if (ty + th > H) ty = H - th;
    int rel = (int)ptr_y - (int)h->y;
    if (ev == 1) sbar_grab = rel >= (int)ty && rel < (int)(ty + th) ? rel - (int)ty : (int)th / 2;
    int g = sbar_grab >= 0 ? sbar_grab : (int)th / 2;
    int room = (int)(H - th), pos = rel - g;
    if (pos < 0) pos = 0;
    if (pos > room) pos = room;
    int nt = room > 0 ? (int)((uint32_t)pos * (uint32_t)(n - vis) / (uint32_t)room) : 0;
    if (nt != *top) { *top = nt; menu_dirty = 1; }
    if (sel && *sel < nt) { *sel = nt; menu_dirty = 1; }
    if (sel && *sel >= nt + vis) { *sel = nt + vis - 1; menu_dirty = 1; }
}
static void ui_sbar(uint32_t x, uint32_t y, uint32_t h, int *top, int *sel, int vis, int n)
{
    ui_scrollbar(x, y, h, *top, vis, n);
    if (n <= vis || n <= 0) return;
    int k = -1;
    for (int i = 0; i < SBAR_MAX && k < 0; i++) if (sbars[i].top == top) k = i;
    for (int i = 0; i < SBAR_MAX && k < 0; i++) if (!sbars[i].top) k = i;
    if (k < 0) k = (int)((uintptr_t)top / sizeof(int) % SBAR_MAX);
    sbars[k].top = top; sbars[k].sel = sel; sbars[k].vis = vis; sbars[k].n = n;
    hit_add(hc_sbar, (intptr_t)top, k, 0, 0, x > 4 * S ? x - 4 * S : 0, y, 12 * S, h);   /* 막대보다 조금 넓게 */
}

/* 메시지 상자 안의 버튼 대신 키 안내 ("[Y] 예  [N] 아니요") — 가운데 정렬 */
static void ui_keys_center(uint32_t x, uint32_t y, uint32_t w, const char *const *keys, const char *const *desc, int n, int hot)
{
    uint32_t ch = LH + 8 * S, tot = 0;
    for (int i = 0; i < n; i++) tot += text_w(keys[i]) + 16 * S + 8 * S + text_w(desc[i]) + 28 * S;
    uint32_t cx = x + (w > tot ? (w - tot) / 2 : 0);
    for (int i = 0; i < n; i++) {
        uint32_t kw = text_w(keys[i]) + 16 * S;
        { int sh, kc = key_of_name(keys[i], &sh); if (kc) hit_add(hc_key, kc, sh, 0, kc, cx, y, kw + 8 * S + text_w(desc[i]), ch); }
        ui_rrect(cx, y, kw, ch, 4 * S, i == hot ? C_ACC : C_CHIP);
        text_clip(cx + 8 * S, ui_ty(y, ch), keys[i], i == hot ? 15 : C_TEXT, 0xFF, 0);
        cx += kw + 8 * S;
        cx += text_clip(cx, ui_ty(y, ch), desc[i], i == hot ? C_TEXT : C_MUTED, 0xFF, 0) + 28 * S;
    }
}
