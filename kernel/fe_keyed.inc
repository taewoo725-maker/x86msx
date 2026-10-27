/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_keyed.inc - 3.2.12 KBD-02 키 편집기 (대화 창 14). fe.c가 fe_tools.inc 다음에 포함한다.
 * 3.7.0 새 디자인: MSX 키보드 그림에서 키를 고르고(방향 키·마우스) Enter → PC 키를 눌러 그 MSX 키로 정함.
 * 아래 판에 기본 PC 키와 바꾼 PC 키. 그 PC 키가 이미 다른 MSX 키로 정해져 있으면 묻는다(Enter 예 / Esc 아니오).
 * Del = 이 MSX 키로 정한 PC 키를 기본으로, F5 = 모두 기본. 바꾼 키는 노란 테두리, PC로 누를 수 없는 키는 흐린 글자.
 * 설정 파일 KeyRemap = PC 키(2자리 16진) + 값(2자리: FF 안 보냄, 그 밖 1 + 행*8 + 비트) 이어 붙임. Shift 두 개는 바꿀 수 없음. */

static const char *const km_names[11][8] = {   /* 일본식(대우 포함) MSX 키 이름 */
    { "0", "1", "2", "3", "4", "5", "6", "7" },
    { "8", "9", "-", "^", "\\", "@", "[", ";" },
    { ":", "]", ",", ".", "/", "_", "A", "B" },
    { "C", "D", "E", "F", "G", "H", "I", "J" },
    { "K", "L", "M", "N", "O", "P", "Q", "R" },
    { "S", "T", "U", "V", "W", "X", "Y", "Z" },
    { "SHIFT", "CTRL", "GRAPH", "CAPS", "CODE (한글/かな)", "F1", "F2", "F3" },
    { "F4", "F5", "ESC", "TAB", "STOP", "BS", "SELECT", "RETURN" },
    { "SPACE", "HOME", "INS", "DEL", "←", "↑", "↓", "→" },
    { "Num *", "Num +", "Num /", "Num 0", "Num 1", "Num 2", "Num 3", "Num 4" },
    { "Num 5", "Num 6", "Num 7", "Num 8", "Num 9", "Num -", "Num ,", "Num ." },
};
/* 기종 키보드 그림 7종. 그림 = 도안(tools/kbd_design)을 그대로 구운 PNG(964x358), 키 목록 = 고르기·강조용 (tools/gen_kbdlay.js 가 만듦).
 * 키 = { MSX 키 코드, 모양, 강조 상자 x y w h, 누르는 자리 x y w h } (도안 화소). 모양 0 = 사각형, 그 밖은 km_poly 의 다각형 */
typedef struct { uint8_t code, shape; uint16_t x, y, w, h, hx, hy, hw, hh; } KmKey;
typedef struct { const KmKey *k; int n, w, h; const char *file; } KmLay;      /* file = Icons\Keyboard\<file> */
typedef struct { int n; uint8_t p[8][2]; } KmPoly;
#define KC(r, b) (1 + (r) * 8 + (b))
#define KL(c) (23 + ((c) - 'A'))                        /* 글자 키 (A = 2행 6비트) */
static const KmPoly km_poly[] = {   /* 모양 1~10 (퍼센트), 11 = 타원 */
    { 0, {{0}} },
    { 5, { {0,0}, {100,0}, {100,38}, {50,100}, {0,38} } },                                  /* 1 GT 위 */
    { 5, { {0,63}, {50,0}, {100,63}, {100,100}, {0,100} } },                                /* 2 GT 아래 */
    { 5, { {0,0}, {52,0}, {100,50}, {52,100}, {0,100} } },                                  /* 3 GT 왼쪽 */
    { 5, { {48,0}, {100,0}, {100,100}, {48,100}, {0,50} } },                                /* 4 GT 오른쪽 */
    { 7, { {4,0}, {96,0}, {100,12}, {100,35}, {50,100}, {0,35}, {0,12} } },                 /* 5 WSX 위 */
    { 7, { {0,65}, {50,0}, {100,65}, {100,88}, {96,100}, {4,100}, {0,88} } },               /* 6 WSX 아래 */
    { 7, { {0,12}, {8,0}, {50,0}, {100,50}, {50,100}, {8,100}, {0,88} } },                  /* 7 WSX 왼쪽 */
    { 7, { {50,0}, {92,0}, {100,12}, {100,88}, {92,100}, {50,100}, {0,50} } },              /* 8 WSX 오른쪽 */
    { 6, { {0,0}, {100,0}, {100,100}, {33,100}, {33,50}, {0,50} } },                        /* 9 ㄱ자 RETURN */
    { 4, { {50,0}, {100,50}, {50,100}, {0,50} } },                                          /* 10 마름모 */
};
#include "fe_kbdlay.inc"
enum { KMV_JP1, KMV_DPC, KMV_CPC, KMV_A1, KMV_WSX, KMV_CF, KMV_GT };    /* km_lays[] 순서 (JP1 = 기본 키보드) */
/* 기종 → 그림 표: 전용 그림이 있는 기종만 적는다 (이름에 들어 있는 글자). 없으면 아래 분류 기본 그림 */
static const struct { const char *name; int lay; } km_mach_lay[] = {
    { "FS-A1GT", KMV_GT },
};
static char km_defset[512];                             /* 기본 그림을 쓰기로 한 기종들: "|기종이름|기종이름|" (없으면 기종 그림) */
static int km_view_def;                                 /* 지금 기종: 1 = 기본 그림, 0 = 기종 그림 */
static char *km_find(const char *t)                     /* 맨몸 PC에는 strstr 가 없어 직접 찾는다 */
{
    size_t n = strlen(t);
    for (char *p = km_defset; *p; p++) if (!strncmp(p, t, n)) return p;
    return 0;
}
static int km_view_get(void)
{
    char t[160];
    copy_str(t, sizeof t, "|"); str_append(t, sizeof t, machine_name); str_append(t, sizeof t, "|");
    return km_find(t) != 0;
}
static void km_view_set(int on)                         /* 기종마다 따로 기억 */
{
    char t[160], *q;
    copy_str(t, sizeof t, "|"); str_append(t, sizeof t, machine_name); str_append(t, sizeof t, "|");
    if ((q = km_find(t)) != 0) {              /* 있으면 지움 (앞 "|"는 남김) */
        size_t tl = strlen(t) - 1;
        memmove(q + 1, q + tl + 1, strlen(q + tl + 1) + 1);
        if (!km_defset[1]) km_defset[0] = 0;
    }
    if (on) {
        if (!km_defset[0]) copy_str(km_defset, sizeof km_defset, "|");
        if (strlen(km_defset) + strlen(machine_name) + 2 < sizeof km_defset) { str_append(km_defset, sizeof km_defset, machine_name); str_append(km_defset, sizeof km_defset, "|"); }
    }
}
static const KmKey *km_k = km_lay_JP1;
static int km_n, km_gw = 1, km_gh = 1, km_lay_cur = -1;
#define KM_N km_n
#define KM_GW km_gw
#define KM_GH km_gh
static int km_sel = 0, km_cap, km_ask_key = -1;
static int kb_is_kr(void)                               /* 한국 기종 (대우·삼성·금성) */
{
    return name_has(machine_name, "Daewoo") || name_has(machine_name, "Samsung") || name_has(machine_name, "Goldstar") ||
           name_has(machine_name, "Gold Star") || name_has(machine_name, "Zemmix") || name_has(machine_name, "SPC-");
}
static int km_machine_lay(void)                         /* 이 기종의 그림: 전용 → 분류 기본 → 기본 키보드 */
{
    for (unsigned i = 0; i < sizeof km_mach_lay / sizeof km_mach_lay[0]; i++)
        if (name_has(machine_name, km_mach_lay[i].name)) return km_mach_lay[i].lay;
#ifdef KM_TEST_LAY
    return KM_TEST_LAY;
#endif
    int g = machine_gen();
    if (g == 3) return KMV_GT;                          /* turbo R */
    if (kb_is_kr()) return g == 0 ? KMV_DPC : KMV_CPC;   /* 한국 MSX1 = DPC-200, MSX2 = CPC-300 */
    if (keymap_machine_get() == KBD_JP) return g == 0 ? KMV_CF : g == 1 ? KMV_A1 : KMV_WSX;   /* 일본 MSX1 = CF-2000, MSX2 = FS-A1, MSX2+ = FS-A1WSX */
    return KMV_JP1;                                     /* 한국·일본 밖 = 기본 키보드 */
}
/* 그림 풀기: 구운 PNG → 작은 보관 자리 (RGBA 964x358). 작업 자리는 디스크 복사 미리보기와 같은 곳(그 창과 같이 열리지 않음) */
#define KM_IW 964
#define KM_IH 358
static uint8_t km_pix[KM_IW * KM_IH * 4];
static int km_pix_lay = -1;
static uint8_t *dc_pv_mem(void);                        /* fe_sys.inc */
static void km_img_load(int v)
{
    if (v == km_pix_lay) return;
    km_pix_lay = -1;
    uint32_t w = 0, h = 0, n = 0;
    char path[64] = "0:\\Icons\\Keyboard\\";
    str_append(path, sizeof path, km_lays[v].file);
    memset(km_pix, 0, sizeof km_pix);
    if (!P.scratch || !P.read_file || P.read_file(path, dc_pv_mem(), 2u << 20, &n) || !n || png_load_big(dc_pv_mem(), n, &w, &h, dc_pv_mem() + (2u << 20)) || w != KM_IW || h != KM_IH) return;   /* 파일이 없으면 빈 그림 + 아래 간단 그림 */
    int alpha = png_has_alpha();
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            int c[4];
            png_rgba(x, y, c);
            uint8_t *d = &km_pix[(y * w + x) * 4];
            d[0] = (uint8_t)c[0]; d[1] = (uint8_t)c[1]; d[2] = (uint8_t)c[2]; d[3] = alpha ? (uint8_t)c[3] : 255;
        }
    km_pix_lay = v;
}
static void km_pick(void)
{
    km_view_def = km_view_get();
    int v = km_view_def ? KMV_JP1 : km_machine_lay();
    if (v == km_lay_cur) return;
    km_lay_cur = v;
    const KmLay *L = &km_lays[v];
    km_k = L->k; km_n = L->n; km_gw = L->w; km_gh = L->h;
    km_img_load(v);
    km_sel = 0;
    for (int i = 0; i < km_n; i++) if (km_k[i].code == KC(0,1)) km_sel = i;   /* 처음 = "1" 키 */
}

static int km_def_of(int p)                            /* PC 키 p가 기본으로 누르는 MSX 키 (Shift 두 개 = SHIFT) */
{
    return p == 0x2A || p == 0x36 ? KC(6,0) : keymap_default_get(p);
}
static void km_row_name(int code, char *out, int max)  /* code: 0xFF 또는 1 + 행*8 + 비트 */
{
    if (code == 0xFF || code < 1 || code > 88) { copy_str(out, max, T(S_KM_NONE_ROW)); return; }
    copy_str(out, max, km_names[(code - 1) >> 3][(code - 1) & 7]);
}
static void km_cap_name(int code, char *out, int max)  /* 키 위에 쓰는 짧은 이름: 글자·숫자·기호는 그 기종 키보드의 글자 */
{
    km_row_name(code, out, max);
    if (code == KC(6,4)) copy_str(out, max, kb_is_kr() ? "한글" : "CODE");
    else if (!strncmp(out, "Num ", 4)) { char t[16]; copy_str(t, sizeof t, out + 4); copy_str(out, max, t); }
    else if (code >= 1 && code <= 48) {
        int kd = keymap_machine_get(), g = keymap_legend(kd, code);
        if (code == KC(1,4)) copy_str(out, max, kb_is_kr() ? "₩" : kd == KBD_JP ? "¥" : "\\");
        else if (code == KC(2,5) && kd == KBD_UK) copy_str(out, max, "£");
        else if (code == KC(1,7) && kd == KBD_ES) copy_str(out, max, "Ñ");
        else if (g) { out[0] = (char)g; out[1] = 0; }
    }
}
/* 방향 키: 그 쪽으로 가장 가까운 키 (위·아래는 줄 먼저, 좌우는 같은 줄 높이에서) */
/* 점 (u,v) = 상자 안 위치 0~1000 이 키 모양 안인가 */
static int km_in(int shape, int u, int v)
{
    if (!shape) return 1;
    if (shape == 11) { int du = u - 500, dv = v - 500; return du * du + dv * dv <= 250000; }
    const KmPoly *q = &km_poly[shape];
    int in = 0;
    for (int i = 0, j = q->n - 1; i < q->n; j = i++) {
        int xi = q->p[i][0] * 10, yi = q->p[i][1] * 10, xj = q->p[j][0] * 10, yj = q->p[j][1] * 10;
        if ((yi > v) != (yj > v) && u < (xj - xi) * (v - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}
/* 키 하나를 그림 위에 칠함. mode: 0 고름(도안의 노란 강조), 1 바꾼 키(주황), 2 같은 키(파랑), 3 누를 PC 키 없음(어둡게) */
static void km_paint(const KmKey *k, int mode, uint32_t kx0, uint32_t ky0, uint32_t ux)
{
    uint32_t bx = k->x * ux / 16, by = k->y * ux / 16, ex = (k->x + k->w) * ux / 16, ey = (k->y + k->h) * ux / 16;
    const uint8_t *tint = mode == 1 ? ui_rgb[UIC(C_WARN)] : ui_rgb[UIC(C_ACC)];
    const uint8_t *r0 = &km_pix[((k->y + k->h / 2) * KM_IW + k->x + k->w * 3 / 4) * 4];     /* 바탕 색 견본 */
    int ref = (r0[0] * 3 + r0[1] * 6 + r0[2]) / 10;
    for (uint32_t oy = by; oy < ey; oy++) {
        uint32_t sy = oy * 16 / ux; if (sy >= KM_IH) break;
        uint32_t run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t ox = bx; ox <= ex; ox++) {
            uint32_t col = 0xFFFFFFFFu;
            uint32_t sx = ox * 16 / ux;
            if (ox < ex && sx < KM_IW && km_in(k->shape, (int)((sx - k->x) * 1000 / k->w), (int)((sy - k->y) * 1000 / k->h))) {
                const uint8_t *s = &km_pix[(sy * KM_IW + sx) * 4];
                uint32_t c[3];
                if (mode == 0) {                        /* 노란 바탕 + 어두운 글자 (바탕과 많이 다른 점은 글자·테두리) */
                    int lum = (s[0] * 3 + s[1] * 6 + s[2]) / 10, d = lum > ref ? lum - ref : ref - lum;
                    uint32_t t = d * 255 / 90; if (t > 255) t = 255;
                    uint32_t v = (sy - k->y) * 255 / k->h, yr = 255 - (255 - 242) * v / 255, yg = 217 - (217 - 180) * v / 255, yb = 111 - (111 - 42) * v / 255;
                    c[0] = (yr * (255 - t) + 42 * t) / 255; c[1] = (yg * (255 - t) + 28 * t) / 255; c[2] = (yb * (255 - t) + 0 * t) / 255;
                } else if (mode == 3) {
                    c[0] = s[0] * 40 / 100 + 6; c[1] = s[1] * 40 / 100 + 7; c[2] = s[2] * 40 / 100 + 10;
                } else {
                    for (int q = 0; q < 3; q++) c[q] = (s[q] * 55 + tint[q] * 45) / 100;
                }
                col = c[0] << 16 | c[1] << 8 | c[2];
            }
            if (col != run_c) {
                if (run_c != 0xFFFFFFFFu && ox > run_x) fe_fill_rgb(kx0 + run_x, ky0 + oy, ox - run_x, 1, run_c >> 16, (run_c >> 8) & 255, run_c & 255);
                run_c = col; run_x = ox;
            }
        }
    }
}
static int km_first_sel(void) { for (int i = 0; i < km_n; i++) if (km_k[i].code) return i; return 0; }
static int km_move(int cur, int dx, int dy)
{
    const KmKey *c = &km_k[cur];
    int cx = c->x * 2 + c->w, cy = c->y * 2 + c->h, best = -1, bd = 0x7FFFFFFF;
    for (int i = 0; i < KM_N; i++) {
        if (i == cur || !km_k[i].code || km_k[i].code == 0xFF) continue;      /* '보내지 않음' 칸은 End 키로만 */
        const KmKey *k = &km_k[i];
        int kx = k->x * 2 + k->w, ky = k->y * 2 + k->h, d;
        if (dx) {
            if (k->y >= c->y + c->h || k->y + k->h <= c->y) continue;   /* 줄이 겹쳐야 */
            if ((kx - cx) * dx <= 0) continue;
            d = (kx - cx) * dx * 4 + (ky > cy ? ky - cy : cy - ky);
        } else {
            if ((ky - cy) * dy <= 0) continue;
            if ((k->y - c->y) * dy <= 0 && !(dy > 0 ? k->y >= c->y + c->h : k->y + k->h <= c->y)) continue;
            int ady = (ky - cy) * dy, adx = kx > cx ? kx - cx : cx - kx;
            d = ady * 64 + adx;
        }
        if (d < bd) { bd = d; best = i; }
    }
    return best >= 0 ? best : cur;
}
static void km_toggle_view(void) { if (km_machine_lay() == KMV_JP1) return; km_view_set(!km_view_get()); km_pick(); settings_changed(); menu_dirty = 1; }   /* 기본 그림 <-> 기종 그림 */
static void hc_km_view(const UiHit *h, int ev) { (void)h; if (ev == 1) km_toggle_view(); }
static void km_apply(void) { input_init(&in); settings_changed(); menu_dirty = 1; }

static void km_save(char *b)
{
    static const char hx[] = "0123456789ABCDEF";
    char t[256 * 4 + 1];
    int k = 0;
    for (int key = 0; key < 256; key++) {
        int c = keymap_remap_get(key);
        if (!c) continue;
        t[k++] = hx[key >> 4]; t[k++] = hx[key & 15]; t[k++] = hx[c >> 4]; t[k++] = hx[c & 15];
    }
    t[k] = 0;
    ini_put(b, "KeyRemap", t);
    ini_put(b, "KeyViewDef", km_defset);
}
static void km_load(const char *v)
{
    keymap_remap_clear();
    int len = (int)strlen(v);
    if (len % 4) return;
    for (int i = 0; i < len; i++) if (hexd(v[i]) < 0) return;
    for (int i = 0; i < len; i += 4) {
        int key = hexd(v[i]) << 4 | hexd(v[i + 1]), c = hexd(v[i + 2]) << 4 | hexd(v[i + 3]);
        if (key == 0x2A || key == 0x36) continue;
        if (c == 0xFF || (c >= 1 && c <= 88)) keymap_remap_set(key, c);
    }
}

static void draw_keyed(void)
{
    UiRect o;
    km_pick();
    int n = 0;
    for (int k = 0; k < 256; k++) if (keymap_remap_get(k)) n++;
    char chip[24] = ""; num_append(chip, sizeof chip, (uint32_t)n, 0, ' '); str_append(chip, sizeof chip, " changed");
    char ttl[96];                                       /* 제목 옆에 지금 기종 이름 */
    copy_str(ttl, sizeof ttl, T(S_KM_TITLE)); str_append(ttl, sizeof ttl, " - "); str_append(ttl, sizeof ttl, machine_name);
    UiRect b = ui_full_card(ttl, T(S_KM_SUB), chip, &o);
    uint32_t pad = 18 * S, X = b.x + pad, W = b.w - 2 * pad;
    uint32_t top = b.y + 14 * S, bot = dlg_cmd_y() - 10 * S;
    uint32_t ph = LH + 3 * (SLH + 14 * S) + 24 * S;     /* 아래 정보 판 높이 */
    uint32_t avh = bot > top + ph + 20 * S ? bot - top - ph - 20 * S : 40 * S;
    uint32_t ux = W * 16 / KM_GW, uy = avh * 16 / KM_GH; /* 1/4 키 크기 (1/16 픽셀 단위) */
    if (uy < ux) ux = uy; else uy = ux;                 /* 그림은 가로세로 같은 비율로 */
    if (!ux) ux = 1;
    if (!uy) uy = 1;
    uint32_t kw = ux * KM_GW / 16, kh = uy * KM_GH / 16, kx0 = X + (W - kw) / 2;
    uint32_t grp = kh + 24 * S + ph, ky0 = top + (bot - top > grp ? (bot - top - grp) / 2 : 0);
    int cur = km_k[km_sel].code;
    uint32_t pw_ = KM_IW * ux / 16, ph_ = KM_IH * ux / 16;
    draw_rgba_scaled(kx0, ky0, pw_, ph_, km_pix, KM_IW, KM_IH, 0, C_BACK, C_BACK, 0);      /* 도안 그림 그대로 */
    uint32_t sux = ux;
    for (int pass = 0; pass < 4; pass++)                /* 칠하기 순서: 어둡게 → 같은 키 → 바꾼 키 → 고른 키 (위에 놓이게) */
        for (int i = 0; i < KM_N; i++) {
            const KmKey *k = &km_k[i];
            if (k->code == 0xFF) continue;
            int chg = 0, def = 0;
            for (int p = 0; p < 256; p++) {
                int r = keymap_remap_get(p);
                if (r == k->code) chg = 1;
                else if (!r && km_def_of(p) == k->code) def = 1;
            }
            int sel = i == km_sel, same = !sel && k->code == cur;
            if (pass == 0 && !def && !chg) km_paint(k, 3, kx0, ky0, sux);
            else if (pass == 1 && same) km_paint(k, 2, kx0, ky0, sux);
            else if (pass == 2 && chg && !sel) km_paint(k, 1, kx0, ky0, sux);
            else if (pass == 3 && sel) km_paint(k, 0, kx0, ky0, sux);
        }
    for (int i = 0; i < KM_N; i++) {
        const KmKey *k = &km_k[i];
        uint32_t x = kx0 + k->hx * ux / 16, y = ky0 + k->hy * ux / 16;
        uint32_t w = (k->hx + k->hw) * ux / 16 - k->hx * ux / 16, h = (k->hy + k->hh) * ux / 16 - k->hy * ux / 16;
        if (k->code == 0xFF) {                          /* MSX로 보내지 않음: 그림 밖 아래 칸 (5.2.147: 고르거나 쓰는 중일 때만 보임) */
            int chg = 0, sel = i == km_sel;
            for (int p = 0; p < 256; p++) if (keymap_remap_get(p) == 0xFF) chg = 1;
            if (!sel && !chg) continue;
            ui_rframe(x, y, w, h, 4 * S, sel ? C_ACC : chg ? C_WARN : C_LINE, sel ? C_ACC : C_CHIP);
            char a[48];
            km_cap_name(0xFF, a, sizeof a);
            uint32_t tw = ui_sw(a);
            if (tw > w) { copy_str(a, sizeof a, "OFF"); tw = ui_sw(a); }
            text_f(SF, 1, x + (w > tw ? (w - tw) / 2 : 0), y + (h > SLH ? (h - SLH) / 2 : 0), a, sel ? 15 : chg ? C_WARN : C_TEXT, 0xFF, w, 1);
        }
        hit_add(hc_sel, (intptr_t)&km_sel, i, 0, 0, x, y, w, h);
    }
    /* 아래 정보 판: 고른 MSX 키, 기본 PC 키, 바꾼 PC 키 */
    uint32_t py = ky0 + kh + 24 * S, pw = kw;
    X = kx0;
    ui_rframe(X, py, pw, ph, 8 * S, C_LINE, C_HEAD);
    uint32_t ix = X + 16 * S, iy = py + 12 * S, ch = SLH + 8 * S;
    char nm[64];
    km_row_name(cur, nm, sizeof nm);
    uint32_t nw = text_f(F, S, ix, iy, nm, C_TEXT, 0xFF, 0, 1);
    /* 오른쪽 위: 범례 */
    {
        uint32_t lx = X + pw - 16 * S, sw = 10 * S;
        const char *l2 = T(S_KM_LEG_NONE), *l1 = T(S_KM_LEG_CHG);
        lx -= ui_sw(l2); text_f(SF, 1, lx, ui_sty(iy, LH), l2, C_FAINT, 0xFF, 0, 1);
        lx -= sw + 6 * S; ui_rframe(lx, ui_sty(iy, LH) + (SLH - sw) / 2, sw, sw, 2 * S, C_LINE, C_CHIP);
        lx -= 18 * S + ui_sw(l1); text_f(SF, 1, lx, ui_sty(iy, LH), l1, C_WARN, 0xFF, 0, 1);
        lx -= sw + 6 * S; ui_rframe(lx, ui_sty(iy, LH) + (SLH - sw) / 2, sw, sw, 2 * S, C_WARN, C_CHIP);
    }
    (void)nw;
    uint32_t lw = 0;
    { uint32_t a1 = ui_sw(T(S_KM_DEFKEYS)), a2 = ui_sw(T(S_KM_MYKEYS)); lw = (a1 > a2 ? a1 : a2) + 18 * S; }
    uint32_t r1 = iy + LH + 10 * S, r2 = r1 + ch + 8 * S, r3 = r2 + ch + 8 * S, lim = X + pw - 20 * S;
    text_f(SF, 1, ix, ui_sty(r1, ch), T(S_KM_DEFKEYS), C_MUTED, 0xFF, 0, 1);
    text_f(SF, 1, ix, ui_sty(r2, ch), T(S_KM_MYKEYS), C_MUTED, 0xFF, 0, 1);
    uint32_t cx = ix + lw; int any = 0;
    for (int p = 0; p < 256 && cur != 0xFF; p++) {
        if (km_def_of(p) != cur) continue;
        char kn[32]; scan_name(p, kn, sizeof kn);
        int moved = keymap_remap_get(p) != 0;           /* 다른 키로 바꿔 둠 = 흐리게 */
        uint32_t w = ui_sw(kn) + 12 * S;
        if (cx + w > lim) break;
        ui_rrect(cx, r1, w, ch, 3 * S, C_CHIP);
        text_f(SF, 1, cx + 6 * S, ui_sty(r1, ch), kn, moved ? C_FAINT : C_TEXT, 0xFF, 0, 1);
        if (moved) fe_fill(cx + 4 * S, r1 + ch / 2, w - 8 * S, S, C_FAINT);
        cx += w + 4 * S; any = 1;
    }
    if (!any) text_f(SF, 1, ix + lw, ui_sty(r1, ch), T(S_KM_NOKEY), C_FAINT, 0xFF, 0, 1);
    cx = ix + lw; any = 0;
    for (int p = 0; p < 256; p++) {
        if (keymap_remap_get(p) != cur) continue;
        char kn[32]; scan_name(p, kn, sizeof kn);
        uint32_t w = ui_sw(kn) + 12 * S;
        if (cx + w > lim) break;
        ui_rrect(cx, r2, w, ch, 3 * S, C_WARN);
        text_f(SF, 1, cx + 6 * S, ui_sty(r2, ch), kn, 1, 0xFF, 0, 1);
        cx += w + 4 * S; any = 1;
    }
    if (!any) text_f(SF, 1, ix + lw, ui_sty(r2, ch), T(S_KM_NOKEY), C_FAINT, 0xFF, 0, 1);
    if (km_ask_key >= 0) {                              /* 이미 쓰는 키: 묻기 */
        char q[160], kn[32], to[64];
        scan_name(km_ask_key, kn, sizeof kn);
        km_row_name(keymap_remap_get(km_ask_key), to, sizeof to);
        copy_str(q, sizeof q, T(S_ASK_USED)); str_append(q, sizeof q, " "); str_append(q, sizeof q, kn);
        str_append(q, sizeof q, " → "); str_append(q, sizeof q, to); str_append(q, sizeof q, "   "); str_append(q, sizeof q, T(S_ASK_YESNO));
        ui_rrect(ix, r3, ui_sw(q) + 16 * S, ch, ch / 2, C_WARN);
        text_f(SF, 1, ix + 8 * S, ui_sty(r3, ch), q, 1, 0xFF, 0, 1);
    } else if (km_cap) {
        ui_rrect(ix, r3, ui_sw(T(S_KM_PRESS)) + 16 * S, ch, ch / 2, C_WARN);
        text_f(SF, 1, ix + 8 * S, ui_sty(r3, ch), T(S_KM_PRESS), 1, 0xFF, 0, 1);
    }
    if (km_machine_lay() != KMV_JP1) {                  /* 그림 전환 칩 (Tab) — 아래 판 오른쪽 아래. 기종 그림이 기본 키보드면 고를 게 없음 */
        const char *a = T(S_KM_VIEW_MACH), *d = T(S_KM_VIEW_DEF);
        uint32_t wa = ui_sw(a), wd = ui_sw(d), cw = (wa > wd ? wa : wd) + 16 * S, gx = lim - 2 * cw - 4 * S;
        const char *lab[2] = { a, d };
        for (int q = 0; q < 2; q++) {
            int on = (q == 1) == (km_view_def != 0);
            uint32_t x = gx + (uint32_t)q * (cw + 4 * S);
            ui_rrect(x, r3, cw, ch, ch / 2, on ? C_ACC : C_CHIP);
            text_f(SF, 1, x + (cw - ui_sw(lab[q])) / 2, ui_sty(r3, ch), lab[q], on ? 15 : C_MUTED, 0xFF, 0, 1);
            hit_add(hc_km_view, 0, q, 0, 0, x, r3, cw, ch);
        }
    }
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, T(S_KM_KEYS));
}

/* 대화 창 14의 키 (누름만). 잡는 중이면 그 키를 고른 MSX 키로 */
static void keyed_key(int key)
{
    km_pick();
    menu_dirty = 1;
    int code = km_k[km_sel].code;
    if (km_ask_key >= 0) {                              /* 묻는 중: Enter·Y 예, Esc·N 아니오 */
        if (key == 0x1C || key == 0x9C || key == 0x15) { keymap_remap_set(km_ask_key, code); km_apply(); }
        else if (key != 0x01 && key != 0x31) return;
        km_ask_key = -1;
        return;
    }
    if (km_cap) {
        if (key == 0x01) { km_cap = 0; return; }
        if (key == 0x2A || key == 0x36) return;         /* Shift는 바꿀 수 없음 */
        km_cap = 0;
        int cur = keymap_remap_get(key);
        if (cur && cur != code) { km_ask_key = key; return; }   /* 다른 MSX 키로 정해 둔 PC 키 */
        if (keymap_default_get(key) == code) {                  /* 3.8.4: 기본 키로 되돌림 = 이 MSX 키에 따로 붙여 둔 다른 PC 키도 모두 원래대로 */
            for (int k = 0; k < 256; k++) if (keymap_remap_get(k) == code) keymap_remap_set(k, 0);
            keymap_remap_set(key, 0);
        } else keymap_remap_set(key, code);
        km_apply();
        return;
    }
    switch (key) {
    case 0x01: dialog_back(); return;
    case 0xCB: km_sel = km_move(km_sel, -1, 0); break;
    case 0xCD: km_sel = km_move(km_sel, 1, 0); break;
    case 0xC8: km_sel = km_move(km_sel, 0, -1); break;
    case 0xD0: km_sel = km_move(km_sel, 0, 1); break;
    case 0xC7: km_sel = km_first_sel(); break;
    case 0xCF: km_sel = KM_N - 1; break;
    case 0x1C: case 0x9C: km_cap = 1; break;
    case 0xD3: {                                        /* Del: 이 MSX 키로 정한 PC 키를 기본으로 */
        int ch = 0;
        for (int k = 0; k < 256; k++) if (keymap_remap_get(k) == code) { keymap_remap_set(k, 0); ch = 1; }
        if (ch) km_apply();
        break;
    }
    case 0x0F: km_toggle_view(); break;                 /* Tab: 기본 그림 <-> 기종 그림 */
    case 0x3F: keymap_remap_clear(); km_apply(); toast(T(S_KM_RESET), 0); break;   /* F5 */
    default: break;
    }
}
