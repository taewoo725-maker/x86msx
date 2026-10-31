/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* fe_sysview.inc - 5.3.19 시스템 보기 창 (dialog 21): 책상 위에 본체와 지금 꽂힌 주변기기를 그림으로.
 * 보기만 하는 창이다. 상태는 미디어 상태판·MIDI 탭·진단이 이미 쓰는 것만 읽고, 아무것도 바꾸지 않는다.
 * 본체 그림: USB  Icons\Machines\<기종 이름>.png (투명 PNG) 가 있으면 그 사진, 없으면 내장 그림.
 *            같은 이름 .png.txt 에 "slot1=x,y" "slot2=x,y" "led=x,y" (사진 픽셀 기준) 가 있으면 꽂힌 카트리지·램프를 그 자리에 겹친다.

 * 5.3.48: 주변기기는 USB  Icons\Sysview\*.png (monitor drive drivefd hd midi gm joystick expander cart, 투명 PNG) 가 모두 있으면 그 그림으로, 없으면 아래 예전 코드 그림.
 *         같은 이름 .png.txt: body=몸체 범위(그림자 뺀)  screen=모니터 화면 구멍  label=카트리지 라벨  slotN=확장 박스 칸 N 의 카트리지 자리.
 * 좌표는 시안(1200x626)을 창 크기에 맞춰 비례. 사진은 창을 열 때 한 번 읽어 그릴 크기로 줄여 P.scratch 앞쪽에 둔다. */

extern const uint8_t gt_photo_png[]; extern const uint32_t gt_photo_png_len;
static int sv_k;                                        /* 시안 → 화면 배율 (1/1024) */
static uint32_t sv_x0, sv_y0, sv_top;                           /* 장면 왼쪽 위 */
static uint8_t *sv_img;                                 /* 줄인 사진 RGBA (작업 메모리 앞쪽), 없으면 0 */
static uint32_t sv_iw, sv_ih;                           /* 줄인 사진 크기 */
static int sv_ov_n; static int sv_ov[6][3];             /* 겹침 자리: 종류(0 slot1 1 slot2 2 led) x y (사진 픽셀) */
static uint32_t sv_src_w, sv_src_h;                     /* 원본 사진 크기 (겹침 좌표 환산용) */
static char sv_img_for[128];                            /* 어느 기종 사진인지 */
static uint32_t sv_crc;                                 /* 사진 검사값: 스크린샷·상태 저장이 작업 메모리 앞쪽을 쓰면 달라짐 → 다시 읽음 */
static uint32_t sv_under(uint32_t x, uint32_t y);
static uint32_t sv_sum(void)
{
    uint32_t h = 2166136261u; uint32_t n = sv_iw * sv_ih;
    for (uint32_t i = 0; i < n; i += 61) h = (h ^ ((const uint32_t *)sv_img)[i]) * 16777619u;
    return h;
}

#define SX(v) (sv_x0 + (uint32_t)(((int)(v)) * sv_k / 1024))
#define SY(v) (sv_y0 + (uint32_t)(((int)(v)) * sv_k / 1024))
#define SL(v) ((uint32_t)(((int)(v)) * sv_k / 1024))

static void sv_rgb(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) { if (w && h) fe_fill_rgb(x, y, w, h, rgb >> 16, (rgb >> 8) & 255, rgb & 255); }
/* 세로 그라데이션 둥근 칸 */
static void sv_box(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t top, uint32_t bot, uint32_t line)
{
    if (!w || !h) return;
    g_rrect(x, y, w, h, r, line);
    if (w > 2 && h > 2) {
        for (uint32_t i = 1; i + 1 < h; i++) {
            uint32_t c = g_mix(top, bot, i, h);
            uint32_t in = 1;
            if (i < r) { uint32_t dy = r - i; for (uint32_t k = r + 1; k-- > 0; ) if (k * k + dy * dy <= r * r + r) { in = r - k + 1; break; } }
            else if (i + r >= h) { uint32_t dy = i + r + 1 - h; for (uint32_t k = r + 1; k-- > 0; ) if (k * k + dy * dy <= r * r + r) { in = r - k + 1; break; } }
            if (w > 2 * in) sv_rgb(x + in, y + i, w - 2 * in, 1, c);
        }
    }
}
static void sv_text(uint32_t x, uint32_t y, const char *t, uint32_t rgb, uint32_t maxw, const UiFont *f)
{
    /* RGB 글자: 가까운 UI 색이 없어 텍스트는 UI 팔레트 색 중 고른다 (짙음 C_BACK, 보통 C_CHIP, 흐림 C_MUTED) */
    uint8_t c = rgb == 0x2a2f36 ? C_BACK : rgb == 0x4a4f58 ? C_CHIP : rgb == 0xff7a1a ? C_WARN : rgb == 0xe6e9ed ? C_TEXT : C_MUTED;
    text_f(f, 1, x, y, t, c, 0xFF, maxw, 1);
}
static void sv_led(uint32_t cx, uint32_t cy, int st) { g_disc((int)cx, (int)cy, (int)(3 * S + S / 2), st == 2 ? 0xf2c230 : st == 1 ? 0x3ecf6e : 0xa9a59d); }

/* 글자 크기: 시안의 글자 크기(px) x 장면 배율 에 가장 가까운 글꼴을 고름 (배수 키우기는 안 씀 — 화면이 커져도 시안과 같은 비례) */
static const UiFont *sv_pick(int base, int bold)
{
    const UiFont *rg[5] = { &font_mini, &font_tiny, &font_small, &font_btn, &font_large };
    static const int rp[5] = { 9, 11, 12, 13, 14 };
    const UiFont *bd[3] = { &font_head, &font_bold, &font_title };
    static const int bp[3] = { 16, 17, 20 };
    int t = base * sv_k / 1024 * (S >= 2 ? 3 : 2) / 2, best = 0, bd_ = 1 << 20;   /* 큰 화면(S=2)은 다른 창과 맞춰 1.5배 */
    int n = bold ? 3 : 5;
    for (int i = 0; i < n; i++) { int v = bold ? bp[i] : rp[i]; int d = v > t ? v - t : t - v; if (d < bd_) { bd_ = d; best = i; } }
    return bold ? bd[best] : rg[best];
}

/* ---- 기종 사진 읽기 (창을 열 때 한 번) ---- */
static void sv_parse_txt(const char *t, uint32_t n)
{
    sv_ov_n = 0;
    const char *e = t + n;
    while (t < e && sv_ov_n < 6) {
        while (t < e && (*t == ' ' || *t == '\r' || *t == '\n' || *t == '\t')) t++;
        int kind = -1;
        if (e - t > 6 && !memcmp(t, "slot1=", 6)) { kind = 0; t += 6; }
        else if (e - t > 6 && !memcmp(t, "slot2=", 6)) { kind = 1; t += 6; }
        else if (e - t > 4 && !memcmp(t, "led=", 4)) { kind = 2; t += 4; }
        else { while (t < e && *t != '\n') t++; continue; }
        int v[2] = { 0, 0 }, neg = 0;
        for (int i = 0; i < 2 && t < e; i++) {
            neg = 0; if (*t == '-') { neg = 1; t++; }
            while (t < e && *t >= '0' && *t <= '9') { v[i] = v[i] * 10 + (*t - '0'); t++; }
            if (neg) v[i] = -v[i];
            if (t < e && *t == ',') t++;
        }
        sv_ov[sv_ov_n][0] = kind; sv_ov[sv_ov_n][1] = v[0]; sv_ov[sv_ov_n][2] = v[1]; sv_ov_n++;
        while (t < e && *t != '\n') t++;
    }
}
static void sv_load_img(uint32_t dw)
{
    sv_img = 0; sv_ov_n = 0;
    if (!P.scratch || !P.read_file || P.scratch_size < (16u << 20) || !dw) return;   /* 5.3.24: 사진은 작업 메모리 앞쪽 (녹화 버퍼를 쓰면 영상 녹화가 덮어써서 지직거림) */
    char path[FE_PATH_MAX]; copy_str(path, sizeof path, "0:\\Icons\\Machines\\"); str_append(path, sizeof path, machine_name); str_append(path, sizeof path, ".png");
    uint32_t n = 0, w = 0, h = 0; int builtin = 0;
    const uint8_t *png = dc_pv_mem();
    if (P.read_file(path, dc_pv_mem(), 2u << 20, &n) || !n) {
        copy_str(path, sizeof path, "0:\\Icons\\Machines\\default.png");
        if (P.read_file(path, dc_pv_mem(), 2u << 20, &n) || !n) { png = gt_photo_png; n = gt_photo_png_len; builtin = 1; }   /* USB 에 사진이 없으면 내장 GT 사진 */
    }
    if (png_load_mem(png, n, &w, &h, dc_pv_mem() + (2u << 20), 6u << 20, 2048, 1024) || !w || !h) return;   /* 원본 그대로 (최대 2048x1024, 파일 2 MB) */
    int alpha = png_has_alpha();
    uint32_t dh = (uint32_t)((uint64_t)h * dw / w);
    if (dh > 400 * S) { dh = 400 * S; dw = (uint32_t)((uint64_t)w * dh / h); }
    if ((uint64_t)dw * dh * 4 > (3u << 20)) return;
    uint8_t *out = P.scratch;
    for (uint32_t y = 0; y < dh; y++) {                 /* 면적 평균으로 줄임 */
        uint32_t sy0 = (uint32_t)((uint64_t)y * h / dh), sy1 = (uint32_t)((uint64_t)(y + 1) * h / dh); if (sy1 <= sy0) sy1 = sy0 + 1;
        for (uint32_t x = 0; x < dw; x++) {
            uint32_t sx0 = (uint32_t)((uint64_t)x * w / dw), sx1 = (uint32_t)((uint64_t)(x + 1) * w / dw); if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t a[4] = { 0, 0, 0, 0 }, cnt = 0, asum = 0;
            for (uint32_t yy = sy0; yy < sy1 && yy < h; yy++)
                for (uint32_t xx = sx0; xx < sx1 && xx < w; xx++) {
                    int c[4]; png_rgba(xx, yy, c);
                    uint32_t al = alpha ? (uint32_t)c[3] : 255;
                    a[0] += (uint32_t)c[0] * al; a[1] += (uint32_t)c[1] * al; a[2] += (uint32_t)c[2] * al; asum += al; cnt++;
                }
            uint8_t *d = out + (y * dw + x) * 4;
            if (asum) { d[0] = (uint8_t)(a[0] / asum); d[1] = (uint8_t)(a[1] / asum); d[2] = (uint8_t)(a[2] / asum); } else d[0] = d[1] = d[2] = 0;
            d[3] = (uint8_t)(cnt ? asum / cnt : 0);
        }
    }
    sv_img = out; sv_iw = dw; sv_ih = dh; sv_src_w = w; sv_src_h = h; sv_crc = sv_sum();
    /* 겹침 위치표 */
    str_append(path, sizeof path, ".txt");
    if (builtin) { static const char ov[] = "slot1=340,-36\nslot2=790,-36\nled=747,58\nled=790,58\nled=911,58\n"; sv_parse_txt(ov, sizeof ov - 1); }
    else if (!P.read_file(path, dc_pv_mem(), 4096, &n) && n) sv_parse_txt((const char *)dc_pv_mem(), n);
    copy_str(sv_img_for, sizeof sv_img_for, machine_name);
}
static void sv_blit_img(uint32_t x, uint32_t y)         /* 줄인 사진을 1:1 로 (알파 섞기). 뒷배경은 책상색 그라데이션이라 점마다 섞음 */
{
    for (uint32_t yy = 0; yy < sv_ih; yy++) {
        const uint8_t *p = sv_img + yy * sv_iw * 4;
        uint32_t run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t xx = 0; xx <= sv_iw; xx++) {
            uint32_t c = 0xFFFFFFFFu;
            if (xx < sv_iw) {
                uint32_t al = p[xx * 4 + 3];
                if (al >= 24) {
                    uint32_t bg = sv_under(x + xx, y + yy);
                    uint32_t r = (p[xx * 4] * al + ((bg >> 16) & 255) * (255 - al)) / 255, g = (p[xx * 4 + 1] * al + ((bg >> 8) & 255) * (255 - al)) / 255, b = (p[xx * 4 + 2] * al + (bg & 255) * (255 - al)) / 255;
                    c = r << 16 | g << 8 | b;
                }
            }
            if (c != run_c) { if (run_c != 0xFFFFFFFFu && xx > run_x) sv_rgb(x + run_x, y + yy, xx - run_x, 1, run_c); run_c = c; run_x = xx; }
        }
    }
}


/* ---- 5.3.48 주변기기 그림 (USB Icons\Sysview\*.png) ---- */
enum { SP_MON, SP_DRV, SP_DRVFD, SP_HD, SP_MT32, SP_SC55, SP_JOY, SP_MOU, SP_TAPE, SP_EXP, SP_CART, SP_CX0, SP_CX1, SP_CX2, SP_CX3, SP_STK, SP_N };
typedef struct { uint8_t *px; uint32_t w, h; int bx, by, bw, bh, sx, sy, sw, sh, lx, ly, lw, lh; int slot[4][3]; } SvSpr;
static SvSpr sv_sp[SP_N];
static const char *const sv_sp_nm[SP_N] = { "monitor", "drive", "drivefd", "hd", "midi", "gm", "joystick", "mouse", "tape", "expander", "cart", "cart", "cart", "cart", "cart", 0 };
static uint8_t *sv_ar_base, *sv_ar_cur, *sv_ar_end;     /* 그림 보관 자리: 작업 메모리 3 MiB 뒤 */
static uint32_t sv_spr_crc;
static int sv_art, sv_art_exp, sv_art_cart;              /* 1 = 그림으로 그림 (없으면 예전 코드 그림) */
static int sv_stk_ca, sv_stk_cb;                         /* 드라이브 두 대를 쌓은 그림 안에서 위·아래 몸체 가운데 줄 */
static uint32_t sv_dsk_y, sv_dsk_h;                      /* 책상 위 줄·높이 (그림 뒷배경 계산) */
static uint32_t sv_hx, sv_hy, sv_hw, sv_hh; static int sv_hole;    /* 모니터 화면 구멍 (그 자리는 실제 MSX 화면이 비침) */
static uint32_t sv_spr_sum(void) { uint32_t h = 2166136261u; for (const uint8_t *p = sv_ar_base; p && p < sv_ar_cur; p += 4093) h = (h ^ *p) * 16777619u; return h; }
static int sv_txt_get(const char *t, uint32_t n, const char *key, int v[4])
{
    uint32_t kl = (uint32_t)strlen(key); const char *e = t + n;
    while (t < e) {
        while (t < e && (*t == ' ' || *t == '\r' || *t == '\n' || *t == '\t')) t++;
        if ((uint32_t)(e - t) > kl && !memcmp(t, key, kl) && t[kl] == '=') {
            t += kl + 1;
            for (int i = 0; i < 4; i++) { int neg = 0; v[i] = 0; if (t < e && *t == '-') { neg = 1; t++; } while (t < e && *t >= '0' && *t <= '9') { v[i] = v[i] * 10 + (*t - '0'); t++; } if (neg) v[i] = -v[i]; if (t < e && *t == ',') t++; }
            return 1;
        }
        while (t < e && *t != '\n') t++;
    }
    return 0;
}
static int sv_spr_load(int id, uint32_t wd)             /* 파일을 읽어 폭 wd 로 줄여 보관 (원본보다 크게는 안 함). 성공 1 */
{
    SvSpr *s = &sv_sp[id]; memset(s, 0, sizeof *s);
    if (!P.read_file || !wd || !sv_ar_base) return 0;
    char path[FE_PATH_MAX]; copy_str(path, sizeof path, "0:\\Icons\\Sysview\\"); str_append(path, sizeof path, sv_sp_nm[id]); str_append(path, sizeof path, ".png");
    uint32_t n = 0, w = 0, h = 0;
    if (P.read_file(path, dc_pv_mem(), 2u << 20, &n) || !n) return 0;
    if (png_load_mem(dc_pv_mem(), n, &w, &h, dc_pv_mem() + (2u << 20), 6u << 20, 2048, 1024) || !w || !h) return 0;
    int alpha = png_has_alpha();
    if (wd > w) wd = w;
    uint32_t dh = (uint32_t)((uint64_t)h * wd / w); if (!dh) return 0;
    uint32_t bytes = wd * dh * 4;
    if ((uint64_t)(sv_ar_end - sv_ar_cur) < bytes + 16) return 0;
    uint8_t *out = sv_ar_cur;
    for (uint32_t y = 0; y < dh; y++) {                 /* 면적 평균 (알파를 곱해서 섞음) */
        uint32_t sy0 = (uint32_t)((uint64_t)y * h / dh), sy1 = (uint32_t)((uint64_t)(y + 1) * h / dh); if (sy1 <= sy0) sy1 = sy0 + 1;
        for (uint32_t x = 0; x < wd; x++) {
            uint32_t sx0 = (uint32_t)((uint64_t)x * w / wd), sx1 = (uint32_t)((uint64_t)(x + 1) * w / wd); if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t a[3] = { 0, 0, 0 }, cnt = 0, asum = 0;
            for (uint32_t yy = sy0; yy < sy1 && yy < h; yy++)
                for (uint32_t xx = sx0; xx < sx1 && xx < w; xx++) {
                    int c[4]; png_rgba(xx, yy, c);
                    uint32_t al = alpha ? (uint32_t)c[3] : 255;
                    a[0] += (uint32_t)c[0] * al; a[1] += (uint32_t)c[1] * al; a[2] += (uint32_t)c[2] * al; asum += al; cnt++;
                }
            uint8_t *d = out + (y * wd + x) * 4;
            if (asum) { d[0] = (uint8_t)(a[0] / asum); d[1] = (uint8_t)(a[1] / asum); d[2] = (uint8_t)(a[2] / asum); } else d[0] = d[1] = d[2] = 0;
            d[3] = (uint8_t)(cnt ? asum / cnt : 0);
        }
    }
    s->px = out; s->w = wd; s->h = dh; s->bx = 0; s->by = 0; s->bw = (int)wd; s->bh = (int)dh;
    str_append(path, sizeof path, ".txt");
    uint32_t tn = 0; int v[4];
    if (!P.read_file(path, dc_pv_mem(), 4096, &tn) && tn) {
        const char *t = (const char *)dc_pv_mem();
        #define SVK(x) ((int)((int64_t)(x) * (int64_t)wd / (int64_t)w))
        if (sv_txt_get(t, tn, "body", v)) { s->bx = SVK(v[0]); s->by = SVK(v[1]); s->bw = SVK(v[2]); s->bh = SVK(v[3]); }
        if (sv_txt_get(t, tn, "screen", v)) { s->sx = SVK(v[0]); s->sy = SVK(v[1]); s->sw = SVK(v[2]); s->sh = SVK(v[3]); }
        if (sv_txt_get(t, tn, "label", v)) { s->lx = SVK(v[0]); s->ly = SVK(v[1]); s->lw = SVK(v[2]); s->lh = SVK(v[3]); }
        for (int j = 0; j < 4; j++) { char k[8] = { 's', 'l', 'o', 't', (char)('0' + j), 0 }; if (sv_txt_get(t, tn, k, v)) { s->slot[j][0] = SVK(v[0]); s->slot[j][1] = SVK(v[1]); s->slot[j][2] = SVK(v[2]); } }
        #undef SVK
    }
    sv_ar_cur += (bytes + 15) & ~15u;
    return 1;
}
static void sv_over(uint8_t *dst, uint32_t dw, uint32_t dh, const SvSpr *s, int ox, int oy)   /* 그림을 다른 그림 위에 얹음 (알파 합성) */
{
    for (uint32_t y = 0; y < s->h; y++) {
        int dy = oy + (int)y; if (dy < 0 || (uint32_t)dy >= dh) continue;
        for (uint32_t x = 0; x < s->w; x++) {
            int dx = ox + (int)x; if (dx < 0 || (uint32_t)dx >= dw) continue;
            const uint8_t *sp = s->px + (y * s->w + x) * 4; uint8_t *d = dst + ((uint32_t)dy * dw + (uint32_t)dx) * 4;
            uint32_t sa = sp[3]; if (!sa) continue;
            uint32_t da = d[3];
            if (sa == 255 || !da) { d[0] = sp[0]; d[1] = sp[1]; d[2] = sp[2]; d[3] = (uint8_t)sa; continue; }
            uint32_t oa = sa + da * (255 - sa) / 255;
            for (int c = 0; c < 3; c++) d[c] = (uint8_t)((sp[c] * sa + d[c] * da * (255 - sa) / 255) / (oa ? oa : 1));
            d[3] = (uint8_t)oa;
        }
    }
}
static int sv_exp(void);
static int sv_drv(void);
static const char *sv_md(int k);
static void sv_sprites_load(void)                       /* 창을 열 때 (그림이 덮였으면 다시) */
{
    sv_art = sv_art_exp = sv_art_cart = 0; memset(sv_sp, 0, sizeof sv_sp);
    if (!P.scratch || !P.read_file || P.scratch_size < (16u << 20)) return;
    sv_ar_base = sv_ar_cur = P.scratch + (3u << 20); sv_ar_end = dc_pv_mem();
    if (sv_ar_end <= sv_ar_base + (1u << 20)) { sv_ar_base = 0; return; }
    int ok = sv_spr_load(SP_MON, SL(300)) && sv_spr_load(SP_DRV, SL(186)) && sv_spr_load(SP_DRVFD, SL(186)) && sv_spr_load(SP_HD, SL(186))
          && sv_spr_load(SP_MT32, SL(262)) && sv_spr_load(SP_SC55, SL(262)) && sv_spr_load(SP_JOY, SL(88));
    sv_spr_load(SP_MOU, SL(46)); sv_spr_load(SP_TAPE, SL(186) * 611 / 616);   /* 몸체 가로가 드라이브와 같게 */
    if (ok && sv_sp[SP_MON].sw && sv_sp[SP_DRV].bh && sv_sp[SP_DRVFD].bh) sv_art = 1;
    if (sv_art) {
        sv_art_cart = sv_spr_load(SP_CART, SL(68));
        if (sv_spr_load(SP_EXP, SL(173)) && sv_sp[SP_EXP].slot[0][2] > 8) {
            int g = 1;
            for (int j = 0; j < 4; j++) if (!sv_spr_load(SP_CX0 + j, (uint32_t)sv_sp[SP_EXP].slot[j][2])) g = 0;
            sv_art_exp = g;
        }
        if (sv_drv() >= 2) {                            /* 드라이브 두 대: 위(A)가 아래(B) 위에 3 mm 간격으로 쌓임 */
            const SvSpr *a = &sv_sp[sv_md(MD_DISKA) ? SP_DRVFD : SP_DRV], *b = &sv_sp[sv_md(MD_DISKB) ? SP_DRVFD : SP_DRV];
            SvSpr *t = &sv_sp[SP_STK];
            int gap = (int)SL(5), yb = a->by + a->bh + gap - b->by;
            uint32_t tw = a->w, th = (uint32_t)(yb + (int)b->h);
            if (tw * th * 4 + 16 <= (uint32_t)(sv_ar_end - sv_ar_cur)) {
                memset(sv_ar_cur, 0, tw * th * 4);
                sv_over(sv_ar_cur, tw, th, b, 0, yb); sv_over(sv_ar_cur, tw, th, a, 0, 0);
                t->px = sv_ar_cur; t->w = tw; t->h = th; t->bx = a->bx; t->by = a->by; t->bw = a->bw; t->bh = a->bh + gap + b->bh;
                sv_stk_ca = a->by + a->bh / 2; sv_stk_cb = yb + b->by + b->bh / 2;
                sv_ar_cur += (tw * th * 4 + 15) & ~15u;
            } else sv_art = 0;
        }
    }
    sv_spr_crc = sv_spr_sum();
}

/* ---- MSX 화면 작은 그림 (모니터 안) ---- */
static void sv_thumb(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!M || run_state == RUN_STOPPED) { sv_rgb(x, y, w, h, 0x101216); return; }
    uint32_t lines = disp_lines;
    for (uint32_t yy = 0; yy < h; yy++) {
        uint32_t sy = (uint32_t)((uint64_t)yy * lines / h); if (sy >= VDP_H) sy = VDP_H - 1;
        const uint8_t *src = M->vdp.fb[sy]; int direct = M->vdp.line_direct[sy];
        uint32_t run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t xx = 0; xx <= w; xx++) {
            uint32_t c = 0xFFFFFFFFu;
            if (xx < w) {
                uint32_t sx = (uint32_t)((uint64_t)xx * VDP_W / w);
                uint32_t v = direct ? g7_32[src[sx]] : px32(src[sx]);
                c = D.bgr ? v : ((v & 255) << 16) | (v & 0xFF00) | ((v >> 16) & 255);   /* fe_fill_rgb 는 R,G,B 순 */
            }
            if (c != run_c) { if (run_c != 0xFFFFFFFFu && xx > run_x) sv_rgb(x + run_x, y + yy, xx - run_x, 1, run_c); run_c = c; run_x = xx; }
        }
    }
}

static uint32_t sv_thumb_px(uint32_t xx, uint32_t yy)   /* 모니터 구멍 안 한 점의 MSX 화면 색 (0xRRGGBB) */
{
    if (!M || run_state == RUN_STOPPED || !sv_hw || !sv_hh) return 0x101216;
    uint32_t sy = (uint32_t)((uint64_t)yy * disp_lines / sv_hh); if (sy >= VDP_H) sy = VDP_H - 1;
    uint32_t sx = (uint32_t)((uint64_t)xx * VDP_W / sv_hw); if (sx >= VDP_W) sx = VDP_W - 1;
    uint32_t v = M->vdp.line_direct[sy] ? g7_32[M->vdp.fb[sy][sx]] : px32(M->vdp.fb[sy][sx]);
    return D.bgr ? v : ((v & 255) << 16) | (v & 0xFF00) | ((v >> 16) & 255);
}
static uint32_t sv_under(uint32_t x, uint32_t y)        /* 그림 뒤에 깔린 벽·책상 색 */
{
    (void)x;
    if (y < sv_dsk_y) return 0xeef0f2;
    if (y < sv_dsk_y + S) return 0xa88e68;
    return g_mix(0xd9c7a6, 0xb89e78, y - sv_dsk_y, sv_dsk_h ? sv_dsk_h : 1);
}
static void sv_spr_blit(const SvSpr *s, uint32_t x0, int y0)   /* 그림 한 장을 (x0, y0) 에. 투명한 곳은 벽·책상(모니터 구멍은 MSX 화면)과 섞음 */
{
    for (uint32_t yy = 0; yy < s->h; yy++) {
        int py = y0 + (int)yy; if (py < 0) continue;
        const uint8_t *p = s->px + yy * s->w * 4;
        uint32_t run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t xx = 0; xx <= s->w; xx++) {
            uint32_t c = 0xFFFFFFFFu;
            if (xx < s->w) {
                uint32_t px = x0 + xx, al = p[xx * 4 + 3];
                int inh = sv_hole && px >= sv_hx && px < sv_hx + sv_hw && (uint32_t)py >= sv_hy && (uint32_t)py < sv_hy + sv_hh;
                if (al >= 6 || inh) {
                    if (al == 255) c = (uint32_t)p[xx * 4] << 16 | (uint32_t)p[xx * 4 + 1] << 8 | p[xx * 4 + 2];
                    else {
                        uint32_t bg = inh ? sv_thumb_px(px - sv_hx, (uint32_t)py - sv_hy) : sv_under(px, (uint32_t)py);
                        uint32_t r = (p[xx * 4] * al + ((bg >> 16) & 255) * (255 - al)) / 255, g = (p[xx * 4 + 1] * al + ((bg >> 8) & 255) * (255 - al)) / 255, b = (p[xx * 4 + 2] * al + (bg & 255) * (255 - al)) / 255;
                        c = r << 16 | g << 8 | b;
                    }
                }
            }
            if (c != run_c) { if (run_c != 0xFFFFFFFFu && xx > run_x) sv_rgb(x0 + run_x, (uint32_t)py, xx - run_x, 1, run_c); run_c = c; run_x = xx; }
        }
    }
}

/* ---- 내장 본체 그림 (사진이 없을 때): 밝은 키보드 일체형 ---- */
static void sv_body_builtin(uint32_t x, uint32_t y, uint32_t w)
{
    uint32_t h = w * 169 / 560;
    sv_box(x, y + h * 22 / 100, w, h * 78 / 100, SL(6), 0xecebe6, 0xc9c7bf, 0xa9a69e);          /* 몸체 */
    sv_box(x + w * 4 / 100, y, w * 92 / 100, h * 24 / 100, SL(4), 0xd4d2cb, 0xc5c3bb, 0xa9a69e); /* 뒤 데크 */
    sv_rgb(x + w * 17 / 100, y + h * 5 / 100, w * 14 / 100, h * 12 / 100, 0x1b1f25);          /* 슬롯 1 */
    sv_rgb(x + w * 37 / 100, y + h * 5 / 100, w * 14 / 100, h * 12 / 100, 0xbdbab2);          /* 슬롯 2 */
    sv_rgb(x + w * 66 / 100, y + h * 7 / 100, w * 26 / 100, h * 8 / 100, 0x2a2f36);           /* 램프 띠 */
    for (int r = 0; r < 5; r++) sv_rgb(x + w * 4 / 100 + (uint32_t)r * SL(2), y + h * (30 + r * 12) / 100, w * 58 / 100, h * 8 / 100, 0xf3f1ec);   /* 키 줄 */
    sv_rgb(x + w * 55 / 100, y + h * 42 / 100, w * 5 / 100, h * 20 / 100, 0xff7a1a);          /* RETURN */
    sv_box(x + w * 74 / 100, y + h * 34 / 100, w * 22 / 100, h * 40 / 100, SL(2), 0xd4d2cb, 0xc5c3bb, 0xa9a69e);   /* 드라이브 */
    sv_rgb(x + w * 76 / 100, y + h * 44 / 100, w * 18 / 100, h * 5 / 100, 0x1b1f25);
}

/* ---- 장치 상태 (읽기만) ---- */
static char sv_mn[48];
static const char *sv_midi_nm(int kind)                 /* 긴 이름에서 " /" · " (" 뒤를 뺌 */
{
    const char *nm = midi_dev_class[kind].name; int n = 0;
    while (nm[n] && n < (int)sizeof sv_mn - 1 && !(nm[n] == ' ' && (nm[n + 1] == '/' || nm[n + 1] == '('))) { sv_mn[n] = nm[n]; n++; }
    sv_mn[n] = 0; return sv_mn;
}
static int sv_demo, sv_demo_req;                                     /* 시험용: 주변기기를 모두 꽂은 것처럼 그림 (fe_test_action "svdemo") */
static const char *sv_demo_carts[5] = { "SCC+", "FM-PAC", "MOONSOUND", "SCC", "ROM" };
static int sv_cart_in(int n) { return sv_demo ? 1 : n >= 0 && n < MSX_CART_SLOTS && M->cart_slot[n] >= 0 && M->cart[n].present; }
static const char *sv_cart_nm(int n) { return sv_demo ? sv_demo_carts[n % 5] : cart_name[n][0] ? cart_name[n] : "ROM"; }
static void sv_cart_art(const SvSpr *c, uint32_t x, int y, const char *name, const UiFont *f)   /* 카트리지 그림 + 라벨 아래쪽에 이름 */
{
    sv_spr_blit(c, x, y);
    if (!c->lw || !name) return;
    uint32_t px = x + (uint32_t)c->lx + c->lw / 12, pw = c->lw - c->lw / 6, ph = (uint32_t)c->lh * 55 / 100;
    int py = y + c->ly + (int)((uint32_t)c->lh * 42 / 100);
    if (py < 0 || ph < (uint32_t)f->height + 2) return;
    sv_rgb(px, (uint32_t)py, pw, ph, 0xeee6d2);
    uint32_t tw = text_f(f, 1, 0, 0, name, 0, 0, 0, 0); if (tw > pw) tw = pw;
    sv_text(px + (pw - tw) / 2, (uint32_t)py + (ph - (uint32_t)f->height) / 2, name, 0x2a2f36, pw, f);
}
static int sv_drv(void) { return sv_demo ? 2 : disk_drives(); }
static int sv_exp(void) { return sv_demo ? 2 : slot_expand; }
static const char *sv_md(int k) { if (sv_demo) return k == MD_HD ? "NEXTOR.HDD" : k == MD_DISKA ? "SDSNATCH.DSK" : k == MD_DISKB ? "GAME2.DSK" : "TAPE01.CAS"; return md_name(k); }

static void sv_layout(void)                             /* 장면 배율: 시안 1200x626 이 창 안(b) x (명령줄 위) 에 */
{
    UiRect o = card_rect();
    UiRect b = { o.x + S, o.y + ui_head_h() + 2 * S + 4 * S + ui_tabs_h(), o.w - 2 * S, 0 };   /* 8.0.34: 위에 탭 줄 */
    uint32_t cmd_y = o.y + o.h - ui_cmdbar_h() - 2 * S;
    uint32_t aw = b.w, ah = cmd_y > b.y + 8 * S ? cmd_y - b.y - 8 * S : 1;
    int kx = (int)(aw * 1024 / 1200), ky = (int)(ah * 1024 / 626);
    sv_k = kx < ky ? kx : ky;
    sv_x0 = b.x + (aw - 1200 * (uint32_t)sv_k / 1024) / 2; sv_top = b.y;
    { uint32_t sh = 626 * (uint32_t)sv_k / 1024; sv_y0 = b.y + (ah > sh ? (ah - sh) / 2 : 0); }   /* 아래 여백이 남으면 위아래 가운데로 */
}
static void draw_sysview(void)
{
    UiRect o;
    char chip[48]; chip[0] = 0; num_append(chip, sizeof chip, D.w, 0, ' '); str_append(chip, sizeof chip, "x"); num_append(chip, sizeof chip, D.h, 0, ' ');
    UiRect b0 = ui_full_card(grow_t("시스템 보기", "System view", "システム表示"), grow_t("책상 위 · 지금 꽂힌 주변기기", "On the desk · what is plugged in now", "机の上 · 今つないでいる周辺機器"), chip, &o);
    { const char *tl[2] = { grow_t("성능", "Performance", "性能"), grow_t("배치", "Layout", "配置") };
      ui_tabs(b0.x, b0.y + 4 * S, b0.w, tl, 2, sv_tab, 1); }                 /* 8.0.34 */
    if (sv_tab == 0) {
        uint32_t ptop = b0.y + 4 * S + ui_tabs_h() + 12 * S, pbot = dlg_cmd_y() - 8 * S;
        if (P.sys_sample && sp_have) draw_sysperf(b0.x, b0.w, ptop, pbot);
        else text_clip(b0.x + 24 * S, ptop, grow_t("이 환경에서는 성능 수치를 읽을 수 없습니다", "Performance figures are not available here", "この環境では性能値を取得できません"), C_MUTED, 0xFF, b0.w - 48 * S);
        ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, grow_t("1 성능   2 배치   ↑/↓ 항목   Tab 탭 바꾸기   Esc 닫기", "1 Performance   2 Layout   ↑/↓ Item   Tab Switch tab   Esc Close", "1 性能   2 配置   ↑/↓ 項目   Tab タブ切替   Esc 閉じる"));
        return;
    }
    uint32_t cmd_y = dlg_cmd_y();
    sv_layout();
    if (sv_img && sv_sum() != sv_crc) sv_load_img(SL(560));   /* 스크린샷 등이 사진 자리를 덮어썼으면 다시 읽음 */
    if (sv_ar_base && sv_spr_sum() != sv_spr_crc) sv_sprites_load();   /* 5.3.48: 주변기기 그림도 */
    sv_dsk_y = SY(330); sv_dsk_h = cmd_y > SY(330) ? cmd_y - SY(330) : 1;
    uint32_t sw = SL(1200), sh = SL(626);
    /* 벽·책상 */
    sv_rgb(o.x + S, sv_top, o.w - 2 * S, SY(330) - sv_top, 0xeef0f2);
    { uint32_t dh = cmd_y > SY(330) ? cmd_y - SY(330) : 1; for (uint32_t i = 0; i < dh; i++) sv_rgb(o.x + S, SY(330) + i, o.w - 2 * S, 1, g_mix(0xd9c7a6, 0xb89e78, i, dh)); }
    sv_rgb(o.x + S, SY(330), o.w - 2 * S, S, 0xa88e68);
    (void)sw; (void)sh;
    const UiFont *FS = sv_pick(11, 0), *FM = sv_pick(9, 0), *FH = sv_pick(14, 1);
    uint32_t CH = (uint32_t)FS->height;

    /* ---- 모니터 (본체 위) ---- */
    if (sv_art) {                                          /* 5.3.48: 그림 (화면 자리는 투명, 실제 MSX 화면이 비침) */
      const SvSpr *m = &sv_sp[SP_MON]; uint32_t mx = SX(610) - m->w / 2; int my = (int)SY(402) - (int)m->h;                     /* 5.3.48: 마스터님 배치 — 모니터·본체·마우스·조이스틱을 아래로 */
      sv_hole = 1; sv_hx = mx + (uint32_t)m->sx; sv_hy = (uint32_t)(my + m->sy); sv_hw = (uint32_t)m->sw; sv_hh = (uint32_t)m->sh;
      sv_spr_blit(m, mx, my); sv_hole = 0;
    } else
    { uint32_t mx = SX(445), my = SY(30), mw = SL(330), mh = SL(262);
      sv_box(mx, my, mw, mh, SL(14), 0xe8e6e0, 0xcbc8c0, 0xa9a69e);
      sv_rgb(mx + SL(22) - S, my + SL(20) - S, SL(286) + 2 * S, SL(206) + 2 * S, 0x0d0f12);
      sv_thumb(mx + SL(22), my + SL(20), SL(286), SL(206));
      sv_led(mx + SL(300), my + SL(244), 1);
      sv_box(mx + SL(100), my + mh, SL(130), SL(18), SL(4), 0xcfccc4, 0xcfccc4, 0xcfccc4);
      sv_box(mx + SL(80), my + mh + SL(18), SL(170), SL(8), SL(4), 0xb9b6ae, 0xb9b6ae, 0xb9b6ae); }

    /* ---- 본체 (사진 또는 내장 그림) ---- */
    { uint32_t bx = SX(330), by = SY(sv_art ? 410 : 334), bw = SL(560);
      if (sv_img && strcmp(sv_img_for, machine_name) == 0 && sv_iw == bw) sv_blit_img(bx, by);
      else sv_body_builtin(bx, by, bw);
      /* 겹침: 꽂힌 카트리지·램프 (위치표가 있을 때만) */
      for (int i = 0; sv_img && i < sv_ov_n; i++) {
          uint32_t ox = bx + (uint32_t)(sv_ov[i][1] * (int)sv_iw / (int)sv_src_w), oy = (uint32_t)((int)by + sv_ov[i][2] * (int)sv_iw / (int)sv_src_w);
          if (sv_ov[i][0] == 2) { sv_led(ox, oy, 1); continue; }
          int n = sv_ov[i][0] == 0 ? 0 : 1;
          if (!sv_cart_in(n)) continue;
          if (sv_art_cart) { sv_cart_art(&sv_sp[SP_CART], ox, (int)oy, sv_cart_nm(n), FM); continue; }
          uint32_t cw = SL(68), chh = SL(34);
          sv_box(ox, oy, cw, chh, SL(3), 0x3a3f47, 0x2a2f36, 0x555b64);
          sv_text(ox + 3 * S, oy + (chh - (uint32_t)FM->height) / 2, sv_cart_nm(n), 0xe6e9ed, cw - 6 * S, FM);
      }
      char t[160]; t[0] = 0; str_append(t, sizeof t, machine_name);
      sv_text(bx + bw / 2 - text_f(FH, 1, 0, 0, t, 0, 0, 0, 0) / 2, by + (sv_img ? sv_ih : SL(169)) + SL(10), t, 0x2a2f36, bw, FH);   /* 본체 이름: 굵게 크게 */ }

    /* ---- 왼쪽 열: 있는 것만 위에서부터 차례로 (자리를 비워 두지 않음) ---- */
    uint32_t lx = SX(30), ly = SY(272);
    { int nl = (sv_md(MD_HD) ? 1 : 0) + (sv_drv() >= 1) + (sv_drv() >= 2) + (sv_exp() ? 1 : 0) + (sv_md(MD_TAPE) ? 1 : 0);   /* 있는 장치 수만큼 위로 올려 아래에 넘치지 않게 */
      if (nl > 2) ly = SY(272 - (nl - 2) * 62); }
    int nleft = 0;
    if (sv_art) {                                          /* 5.3.48: 그림 — 외장 하드, 드라이브(두 대면 A 가 B 위에 3 mm 간격으로 쌓음) */
      const UiFont *FSx = FS; (void)FSx;
      const char *nH = sv_md(MD_HD), *nA = sv_drv() >= 1 ? sv_md(MD_DISKA) : 0, *nB = sv_drv() >= 2 ? sv_md(MD_DISKB) : 0;
      int nd = sv_drv() > 2 ? 2 : sv_drv();
      const SvSpr *hd = &sv_sp[SP_HD], *dr = &sv_sp[SP_DRV];
      int gap = (int)SL(5), g14 = (int)SL(14);
      int tot = (nH ? hd->bh + g14 : 0) + (nd == 1 ? dr->bh + g14 : nd == 2 ? 2 * dr->bh + gap + g14 : 0)
              + ((sv_exp() && !sv_art_exp) ? (int)(SL(14 + 56 + 20) + CH + SL(14)) : 0) + (sv_md(MD_TAPE) ? (int)(SL(20 + 60 + 6) + CH) : 0);
      int cur = (int)SY(516) - tot; if (cur > (int)SY(300)) cur = (int)SY(300); if (cur < (int)SY(24)) cur = (int)SY(24);
      uint32_t ax = SX(42), cx = ax + SL(186) + SL(8);
      uint32_t capw = SX(346) > cx ? SX(346) - cx : SL(90);
      if (nH) {
          sv_spr_blit(hd, ax, cur - hd->by);
          uint32_t yc = (uint32_t)(cur + hd->bh / 2);
          char t[96]; t[0] = 0; str_append(t, sizeof t, grow_t("외장 하드", "External HD", "外付けHD"));
          sv_text(cx, yc - CH - 1, t, 0x4a4f58, capw, FS); sv_text(cx, yc + 2, nH, 0x7a7f88, capw, FM);
          cur += hd->bh + g14; nleft++;
      }
      if (nd == 1) {
          const SvSpr *d1 = &sv_sp[nA ? SP_DRVFD : SP_DRV];
          sv_spr_blit(d1, ax, cur - d1->by);
          uint32_t yc = (uint32_t)(cur + d1->bh / 2);
          char t[96]; t[0] = 0; str_append(t, sizeof t, grow_t("드라이브 A", "Drive A", "ドライブA"));
          sv_text(cx, nA ? yc - CH - 1 : yc - CH / 2, t, nA ? 0x4a4f58 : 0x7a7f88, capw, FS); if (nA) sv_text(cx, yc + 2, nA, 0x7a7f88, capw, FM);
          cur += d1->bh + g14; nleft++;
      } else if (nd == 2 && sv_sp[SP_STK].px) {
          const SvSpr *st = &sv_sp[SP_STK];
          sv_spr_blit(st, ax, cur - st->by);
          for (int q = 0; q < 2; q++) {
              const char *nm = q ? nB : nA; uint32_t yc = (uint32_t)(cur - st->by + (q ? sv_stk_cb : sv_stk_ca));
              char t[96]; t[0] = 0; str_append(t, sizeof t, grow_t(q ? "드라이브 B" : "드라이브 A", q ? "Drive B" : "Drive A", q ? "ドライブB" : "ドライブA"));
              sv_text(cx, nm ? yc - CH - 1 : yc - CH / 2, t, nm ? 0x4a4f58 : 0x7a7f88, capw, FS); if (nm) sv_text(cx, yc + 2, nm, 0x7a7f88, capw, FM);
          }
          cur += st->bh + g14; nleft += 2;
      }
      ly = (uint32_t)cur;
    }
    if (!sv_art) { /* 외장 하드 */
      const char *nm = sv_md(MD_HD);
      if (nm) { uint32_t y = ly, w = SL(190), h = SL(56);
                sv_box(lx, y, w, h, SL(4), 0xe6e4de, 0xcfccc4, 0xa9a69e); sv_rgb(lx + SL(12), y + SL(14), SL(110), SL(28), 0x2a2f36);
                sv_text(lx + SL(18), y + SL(14) + (SL(28) - (uint32_t)FM->height) / 2, nm, 0xe6e9ed, SL(100), FM); sv_led(lx + SL(160), y + SL(28), md_busy(MD_HD) ? 2 : 1);
                char t[96]; t[0] = 0; str_append(t, sizeof t, grow_t("외장 하드", "External HD", "外付けHD"));
                sv_text(lx, y + h + SL(8), t, 0x4a4f58, SL(230), FS); ly += h + SL(8) + CH + SL(14); nleft++; } }
    if (!sv_art) { /* 드라이브 A — 기종에 드라이브가 있을 때 */
      int has = sv_drv() >= 1; const char *nm = has ? sv_md(MD_DISKA) : 0;
      if (has) { uint32_t y = ly, w = SL(190), h = SL(56);
                sv_box(lx, y, w, h, SL(4), 0xe6e4de, 0xcfccc4, 0xa9a69e); sv_rgb(lx + SL(16), y + SL(18), SL(150), SL(8), 0x1b1f25); sv_led(lx + SL(30), y + SL(42), nm ? (md_busy(MD_DISKA) ? 2 : 1) : 0);
                sv_text(lx + SL(44), y + SL(38), nm ? nm : "DRIVE A", 0x4a4f58, SL(140), FM);   /* 드라이브 등 옆: 디스크 이름 */
                char t[96]; t[0] = 0; str_append(t, sizeof t, grow_t("드라이브 A", "Drive A", "ドライブA"));
                sv_text(lx, y + h + SL(8), t, nm ? 0x4a4f58 : 0x7a7f88, SL(230), FS); ly += h + SL(8) + CH + SL(14); nleft++; } }
    if (!sv_art) { /* 드라이브 B (외장) — 기종에 2대째가 있을 때만 */
      int has = sv_drv() >= 2; const char *nm = has ? sv_md(MD_DISKB) : 0;
      if (has) { uint32_t y = ly, w = SL(190), h = SL(56);
                sv_box(lx, y, w, h, SL(4), 0xe6e4de, 0xcfccc4, 0xa9a69e); sv_rgb(lx + SL(16), y + SL(18), SL(150), SL(8), 0x1b1f25); sv_led(lx + SL(30), y + SL(42), nm ? (md_busy(MD_DISKB) ? 2 : 1) : 0);
                sv_text(lx + SL(44), y + SL(38), nm ? nm : "DRIVE B", 0x4a4f58, SL(140), FM);   /* 드라이브 등 옆: 디스크 이름 */
                char t[96]; t[0] = 0; str_append(t, sizeof t, grow_t("드라이브 B", "Drive B", "ドライブB"));
                sv_text(lx, y + h + SL(8), t, nm ? 0x4a4f58 : 0x7a7f88, SL(230), FS); ly += h + SL(8) + CH + SL(14); nleft++; } }
    { /* 4슬롯 확장 박스 — 슬롯 확장이 켜져 있을 때만 (그림이 있으면 컴퓨터 오른쪽에) */
      if (sv_exp() && !sv_art_exp) {
          uint32_t y = ly + SL(14), w = SL(292), h = SL(56);
          sv_box(lx, y + SL(14), w, h, SL(5), 0xe6e4de, 0xcfccc4, 0xa9a69e);
          for (int i = 0; i < 4; i++) {
              int n = i + 1;                                   /* 확장 자리: 카트리지 2~5 */
              uint32_t sx = lx + SL(12 + 68 * i);
              sv_rgb(sx, y + SL(20), SL(62), SL(12), sv_cart_in(n) ? 0x1b1f25 : 0xbdbab2);
              if (sv_cart_in(n)) { sv_box(sx, y - SL(10), SL(62), SL(30), SL(2), 0x3a3f47, 0x2a2f36, 0x555b64); sv_text(sx + S, y - SL(10) + (SL(30) - (uint32_t)FM->height) / 2, sv_cart_nm(n), 0xe6e9ed, SL(62) - 2 * S, FM); }
              char l[8] = { (char)('0' + sv_exp()), '-', (char)('0' + i), 0 }; sv_text(sx + SL(23), y + SL(36), l, 0x4a4f58, SL(30), FM);
          }
          sv_led(lx + SL(274), y + SL(60), 1);
          sv_text(lx + SL(12), y + SL(54), "EXPANDER x4", 0x2a2f36, SL(120), FM);
          char t[128]; t[0] = 0; str_append(t, sizeof t, grow_t("4슬롯 확장 박스", "4-slot expander", "4スロット拡張"));
                    sv_text(lx, y + h + SL(20), t, 0x4a4f58, SL(240), FS); ly += SL(14) + h + SL(20) + CH + SL(14); nleft++;
      } }
    { /* 데이터 레코더 — 테이프를 넣었을 때만 */
      const char *nm = sv_md(MD_TAPE);
      if (nm && sv_art && sv_sp[SP_TAPE].px) {                 /* 5.3.48: 데이터 레코더 그림 */
          const SvSpr *tp = &sv_sp[SP_TAPE]; uint32_t y = ly + SL(8);
          sv_spr_blit(tp, SX(42), (int)y - tp->by);
          char t[96]; t[0] = 0; str_append(t, sizeof t, grow_t("데이터 레코더", "Data recorder", "データレコーダ"));
          sv_text(SX(42) + (uint32_t)tp->bx, y + (uint32_t)tp->bh + SL(4), t, 0x4a4f58, SL(240), FS);
          ly += SL(8) + (uint32_t)tp->bh + SL(4) + CH; nleft++;
      } else
      if (nm) { uint32_t y = ly + SL(20), w = SL(180), h = SL(60);
      sv_box(lx + SL(5), y, w, h, SL(6), 0xe6e4de, 0xcfccc4, 0xa9a69e);
      sv_box(lx + SL(19), y + SL(10), SL(100), SL(40), SL(3), 0x3a3f47, 0x2a2f36, 0x2a2f36);
      g_disc((int)(lx + SL(45)), (int)(y + SL(30)), (int)SL(9), 0x1b1f25); g_disc((int)(lx + SL(93)), (int)(y + SL(30)), (int)SL(9), 0x1b1f25);
      g_disc((int)(lx + SL(45)), (int)(y + SL(30)), (int)SL(3), 0x8a929c); g_disc((int)(lx + SL(93)), (int)(y + SL(30)), (int)SL(3), 0x8a929c);
      for (int i = 0; i < 3; i++) sv_rgb(lx + SL(129 + 14 * i), y + SL(16), SL(10), SL(26), 0xa9a59d);
      sv_led(lx + SL(133), y + SL(50), md_busy(MD_TAPE) ? 2 : 1);
      char t[96]; t[0] = 0; str_append(t, sizeof t, grow_t("데이터 레코더", "Data recorder", "データレコーダ"));
      sv_text(lx, y + h + SL(6), t, 0x4a4f58, SL(240), FS); ly += SL(20) + h + SL(6) + CH; nleft++; } }

    /* ---- 오른쪽 열: 있는 것만 ---- */
    uint32_t ry = SY(290);
    { int km = 0; for (int i = 0; i < (sv_demo ? 3 : midi_bus.n); i++) if (sv_demo || (midi_bus.dev[i].kind != MDK_NONE && midi_bus.dev[i].kind != MDK_FILE)) km++; if (km > 3) km = 3; if (km > 1) ry = SY(290 - (km - 1) * 45); }
    uint32_t spk_top = 0;
    int nright = 0;
    if (sv_art) {                                          /* 5.3.48: MIDI 그림 (MT-32 는 MT-32 그림, SC-55·GM 은 SC-55 그림), 이름은 그림 아래 */
      int kinds[3], km = 0;
      for (int i = 0; i < (sv_demo ? 3 : midi_bus.n) && km < 3; i++) {
          static const int dk[3] = { MDK_MT32, MDK_GS, MDK_GM };
          int kd = sv_demo ? dk[i] : midi_bus.dev[i].kind; if (kd == MDK_NONE || kd == MDK_FILE) continue;
          kinds[km++] = kd;
      }
      if (km) {
          const SvSpr *u0 = &sv_sp[SP_MT32];
          int unit = u0->bh + (int)(CH + SL(10)), cur = (int)SY(416) - km * unit; if (cur < (int)SY(14)) cur = (int)SY(14);
          uint32_t x = SX(910);
          for (int k = 0; k < km; k++) {
              const SvSpr *u = &sv_sp[kinds[k] == MDK_MT32 ? SP_MT32 : SP_SC55];
              sv_spr_blit(u, x, cur - u->by);
              sv_text(x + (uint32_t)u->bx, (uint32_t)(cur + u->bh) + SL(2), sv_midi_nm(kinds[k]), 0x4a4f58, (uint32_t)u->bw, FS);
              cur += unit;
          }
          ry = (uint32_t)cur + SL(6); nright++;
      } else ry = SY(300);
    }
    if (!sv_art) { /* MIDI 모듈 (꽂힌 대수만큼, 최대 3단) */
      uint32_t x = SX(905), w = SL(262), h = SL(40); int k = 0;
      static const MidiDev dm[3] = { { .kind = MDK_MT32 }, { .kind = MDK_GS }, { .kind = MDK_GM } };
      for (int i = 0; i < (sv_demo ? 3 : midi_bus.n) && k < 3; i++) {
          const MidiDev *d = sv_demo ? &dm[i] : &midi_bus.dev[i]; if (d->kind == MDK_NONE || d->kind == MDK_FILE) continue;
          uint32_t yy = ry + (uint32_t)k * SL(48);
          sv_box(x, yy, w, h, SL(4), 0xdcdad3, 0xcfccc4, 0xa9a69e);
          sv_rgb(x + SL(14), yy + SL(10), SL(86), SL(20), 0x0b3d1e);
          sv_text(x + SL(20), yy + SL(10) + (SL(20) - (uint32_t)FM->height) / 2, midi_dev_class[d->kind].short_name, 0x3ecf6e, SL(80), FM);
          sv_text(x + SL(106), yy + SL(10) + (SL(20) - (uint32_t)FM->height) / 2, sv_midi_nm(d->kind), 0x4a4f58, SL(134), FM);   /* 까만 칸 옆: 어떤 MIDI 인지 */
          sv_led(x + SL(246), yy + SL(20), 1);
          k++;
      }
      if (k) { char t[96]; t[0] = 0; str_append(t, sizeof t, "MIDI");
              sv_text(x, ry + (uint32_t)k * SL(48) + SL(2), t, 0x4a4f58, SL(260), FS); ry += (uint32_t)k * SL(48) + SL(2) + CH + SL(18); nright++; } }
    int exp_art = sv_art && sv_art_exp && sv_exp();
    if (exp_art) {                                         /* 5.3.48: 4슬롯 확장 박스 — 컴퓨터 오른쪽 책상 위, 꽂힌 카트리지는 칸 0(아래)~3(위) */
      const SvSpr *e = &sv_sp[SP_EXP]; uint32_t ex = SX(910); int ey = (int)SY(442);
      sv_spr_blit(e, ex, ey);
      for (int j = 3; j >= 0; j--) {                       /* 뒤(위)쪽 칸부터 그려 앞(아래) 카트리지가 위에 */
          if (!sv_cart_in(j + 1)) continue;
          const SvSpr *c = &sv_sp[SP_CX0 + j];
          sv_cart_art(c, ex + (uint32_t)e->slot[j][0] - c->w / 2, ey + e->slot[j][1] - (int)c->h, sv_cart_nm(j + 1), FM);
      }
    }
    /* 5.3.48: 스피커 그림은 뺌(마스터님 요청) */
    { /* 조이스틱 · 마우스 — 포트에 꽂힌 것만 */
      uint32_t x = SX(1085), y = ry > SY(430) ? ry : SY(430);
      uint32_t px = sv_art ? SX(620) : SX(690), py = sv_art ? SY(544) : SY(540);                   /* 마우스·조이스틱: 본체 앞, 가운데에서 조금 오른쪽부터 나란히 */
      (void)x; (void)y; (void)spk_top;
      for (int q = 0; q < 2; q++) {                         /* 5.3.48: 포트 1 → 포트 2 순서로, 그 포트에 꽂힌 것만 */
          char t[64]; t[0] = 0;
          int pn = q + 1;
          int is_mouse = sv_demo ? pn == 1 : (mouse_port == pn && mouse_on);
          int jm = is_mouse ? 0 : sv_demo ? (pn == 2 ? 2 : 0) : joy_mode[q];
          if (is_mouse) copy_str(t, sizeof t, grow_t("MSX 마우스", "MSX mouse", "MSXマウス"));
          else if (sv_demo && jm) copy_str(t, sizeof t, "8BitDo");
          else if (jm >= 2) { char nm[24] = ""; int st = P.pad_read ? P.pad_read(jm - 2, nm, sizeof nm) : -1; copy_str(t, sizeof t, st >= 0 && nm[0] ? nm : T(jm == 2 ? S_JOY_PAD1 : S_JOY_PAD2)); }   /* 패드를 못 읽어도 포트에 꽂혀 있으니 보임 */
          else if (jm == 1) copy_str(t, sizeof t, grow_t("키보드 조이스틱", "keyboard joystick", "キーボード"));
          if (t[0]) { char t2[96]; copy_str(t2, sizeof t2, pn == 1 ? "1: " : "2: "); str_append(t2, sizeof t2, t); copy_str(t, sizeof t, t2); }
          if (!t[0]) continue;
          if (is_mouse) {                                   /* 마우스: 본체 앞, 가운데에서 조금 오른쪽에 붙여 놓음 */
              uint32_t mx = sv_art ? SX(808) : px, my = sv_art ? SY(500) : py; if (!sv_art) px += SL(80);   /* 마스터님 배치: 마우스는 본체 오른쪽 끝 */
              uint32_t lw = text_f(FS, 1, 0, 0, t, 0, 0, 0, 0), mh = SL(50);
              if (sv_art && sv_sp[SP_MOU].px) { sv_spr_blit(&sv_sp[SP_MOU], mx, (int)my); mh = sv_sp[SP_MOU].h; lw = lw; }
              else { mh = SL(50); sv_box(mx, my, SL(34), SL(50), SL(14), 0xe6e4de, 0xcfccc4, 0xa9a69e); sv_rgb(mx + SL(17), my, S, SL(14), 0xa9a69e); }
              sv_text(mx + (sv_art && sv_sp[SP_MOU].px ? sv_sp[SP_MOU].w / 2 : SL(17)) - lw / 2, my + mh + SL(4), t, 0x4a4f58, SL(140), FS);
              continue;
          }
          x = sv_art ? SX(q == 0 ? 409 : 705) : px; y = sv_art ? SY(525) : py; px += SL(130);   /* 조이스틱은 본체 앞 양쪽: 포트 1 왼쪽, 포트 2 오른쪽 */
          if (sv_art) sv_spr_blit(&sv_sp[SP_JOY], x, (int)y);       /* 5.3.48: 조이스틱 그림 */
          else { sv_box(x, y, SL(110), SL(56), SL(12), 0xdcdad3, 0xcfccc4, 0xa9a69e); g_disc((int)(x + SL(28)), (int)(y + SL(28)), (int)SL(11), 0x3a3f47); sv_rgb(x + SL(24), y + SL(6), SL(8), SL(22), 0x5a5f68); g_disc((int)(x + SL(74)), (int)(y + SL(22)), (int)SL(6), 0xe8453c); g_disc((int)(x + SL(92)), (int)(y + SL(32)), (int)SL(6), 0xf2c230); }
          char l[80]; copy_str(l, sizeof l, t);
          { uint32_t lw = text_f(FS, 1, 0, 0, l, 0, 0, 0, 0), lx2 = x, lim = o.x + o.w - 10 * S; if (lx2 + lw > lim) lx2 = lim > lw ? lim - lw : x; sv_text(lx2, sv_art ? y + sv_sp[SP_JOY].h + SL(4) : y + SL(62), l, 0x4a4f58, SL(140), FS); }
      } }
    (void)nleft;
    /* 케이블(점선)은 그리지 않음 */
    ui_cmdbar(o.x + S, cmd_y, o.w - 2 * S, grow_t("1 성능   2 배치   Tab 탭 바꾸기   Esc 닫기", "1 Performance   2 Layout   Tab Switch tab   Esc Close", "1 性能   2 配置   Tab タブ切替   Esc 閉じる"));
}
static void sysview_open(void)
{
    sv_demo = sv_demo_req; sv_demo_req = 0;
    open_dialog(21);
    sv_tab = 0; sv_perf_sel = 0; sp_reset(); sysperf_tick(P.ms());      /* 8.0.34: 처음 값만 읽어 둠 */
    sv_layout();
    sv_load_img(SL(560));
    sv_sprites_load();                                  /* 5.3.48: 주변기기 그림 */
}
static void sysview_key(int key)
{
    if (key == 0x01) { dialog_back(); return; }
    if (key == 0x02 || key == 0x03) { sv_tab = key - 0x02; menu_dirty = 1; full_redraw(); return; }       /* 8.0.36: 1 성능 · 2 배치 */
    if (key == 0x0F) { sv_tab ^= 1; menu_dirty = 1; full_redraw(); return; }
    if (sv_tab == 0) {
        if (key == 0xC8 && sv_perf_sel > 0) { sv_perf_sel--; menu_dirty = 1; }
        else if (key == 0xD0 && sv_perf_sel < 2) { sv_perf_sel++; menu_dirty = 1; }
        return;
    }
    if (key == 0x1C) dialog_back();
}
