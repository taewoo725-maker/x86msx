/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* mp4play_fast.inc - 빠른 화면 처리 (8.0.123, ini Mp4Fast=1). 기본(0)은 mp4play.c 의 기존 코드가 그대로 맡는다.
 * mp4play.c 안에 #include 되며, 기존 scale_to_rgb·make_pic·quantize 는 한 줄도 고치지 않는다 (레고 원칙).
 *  - 줄이기: 출력 점마다 원본 한 점만 읽음 (평균 안 냄)
 *  - 디더: 오차 확산(Floyd-Steinberg) 대신 순서 디더(Bayer 4x4) — 점마다 독립, 앞 점에 의존 안 함
 *  - SCREEN 5·6·7: 팔레트는 8프레임에 한 번만 갱신(히스토그램도 1/4 만 셈) + 3비트 RGB 조회표
 *  - SCREEN 8: 고정 3-3-2 + 순서 디더, SCREEN 12: Y 에 순서 디더, J·K 는 4점 평균 (기존과 같음) */

static const uint8_t bayer4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
static uint8_t fq3[256], fq2[256], fq5[256], fq_init;          /* 8비트 → 3·2·5비트 (반올림) */
static uint8_t flut[512];                                      /* 3비트 RGB 칸 → 팔레트 칸 */
static uint16_t fxs[MAXW];                                     /* 출력 열 → 원본 열 */

static void fast_init(void)
{
    if (fq_init) return;
    for (int v = 0; v < 256; v++) { fq3[v] = (uint8_t)((v * 7 + 127) / 255); fq2[v] = (uint8_t)((v * 3 + 127) / 255); fq5[v] = (uint8_t)((v * 31 + 127) / 255); }
    fq_init = 1;
}

static void fast_scale(void)                                   /* scale_to_rgb 와 같은 자리·크기, 점마다 원본 한 점 */
{
    int W = P.pic.w, H = P.pic.h, wx = W / (W > 256 ? 2 : 1);
    uint32_t sw = P.cw, sh = P.ch, cl = P.cl, ct = P.ct, dw, dh;
    if (P.fit == 2) { dw = 256; dh = (uint32_t)H; }
    else if (P.fit == 0) {
        uint32_t nw = sh * 256u / (uint32_t)H;
        if (nw > sw) { uint32_t nh = sw * (uint32_t)H / 256u; ct += (sh - nh) / 2; sh = nh; }
        else { cl += (sw - nw) / 2; sw = nw; }
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        dw = 256; dh = (uint32_t)H;
    } else {
        uint32_t a = 256u * sh, b = (uint32_t)H * sw;
        if (a <= b) { dw = 256; dh = 256u * sh / sw; } else { dh = (uint32_t)H; dw = (uint32_t)H * sw / sh; }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    int mul = W / wx;
    uint32_t DW = dw * (uint32_t)mul, ox = ((uint32_t)W - DW) / 2, oy = ((uint32_t)H - dh) / 2;
    memset(s_rgb, 0, (size_t)W * (size_t)H * 3);
    int hd = sh >= 720;
    const uint8_t *Y = P.yuv, *U = Y + P.mbw * P.mbh, *V = U + (P.mbw / 2) * (P.mbh / 2);
    uint32_t cwid = P.mbw / 2, sx_step = (sw << 16) / DW, sy_step = (sh << 16) / dh;
    for (uint32_t x = 0; x < DW; x++) { uint32_t sx = cl + ((x * sx_step + sx_step / 2) >> 16); fxs[x] = (uint16_t)(sx >= P.mbw ? P.mbw - 1 : sx); }
    for (uint32_t y = 0; y < dh; y++) {
        uint8_t *row = s_rgb + (((size_t)(oy + y)) * (size_t)W + ox) * 3;
        uint32_t sy = ct + ((y * sy_step + sy_step / 2) >> 16); if (sy >= P.mbh) sy = P.mbh - 1;
        const uint8_t *yr = Y + sy * P.mbw, *ur = U + (sy >> 1) * cwid, *vr = V + (sy >> 1) * cwid;
        for (uint32_t x = 0; x < DW; x++) { uint32_t sx = fxs[x]; yuv_to_rgb(yr[sx], ur[sx >> 1], vr[sx >> 1], hd, row + x * 3); }
    }
}

static void fast_quant8(void)                                  /* SCREEN 8: 3-3-2 + 순서 디더 */
{
    int W = P.pic.w, H = P.pic.h, ag = 18 * P.dither / 100, ab = 42 * P.dither / 100;
    memset(s_img, 0, sizeof s_img);
    for (int y = 0; y < H; y++) {
        const uint8_t *px = s_rgb + (size_t)y * (size_t)W * 3; uint8_t *dst = s_img + (size_t)y * (size_t)P.pic.stride;
        for (int x = 0; x < W; x++) {
            int t = bayer4[y & 3][x & 3] * 2 - 15, og = (t * ag) >> 4, ob = (t * ab) >> 4;
            int r = clamp8(px[x * 3] + og), g = clamp8(px[x * 3 + 1] + og), b = clamp8(px[x * 3 + 2] + ob);
            dst[x] = (uint8_t)(fq3[g] << 5 | fq3[r] << 2 | fq2[b]);
        }
    }
}

static void fast_quant_pal(void)                               /* SCREEN 5·6·7: 팔레트 조회표 + 순서 디더 */
{
    int W = P.pic.w, H = P.pic.h, amp = 40 * P.dither / 100, scr = P.scr;
    memset(s_img, 0, sizeof s_img);
    for (int y = 0; y < H; y++) {
        const uint8_t *px = s_rgb + (size_t)y * (size_t)W * 3; uint8_t *dst = s_img + (size_t)y * (size_t)P.pic.stride;
        for (int x = 0; x < W; x++) {
            int o = ((bayer4[y & 3][x & 3] * 2 - 15) * amp) >> 4;
            int r = clamp8(px[x * 3] + o), g = clamp8(px[x * 3 + 1] + o), b = clamp8(px[x * 3 + 2] + o);
            int code = flut[(r >> 5) << 6 | (g >> 5) << 3 | (b >> 5)];
            if (scr == 6) dst[x >> 2] |= (uint8_t)(code << (6 - 2 * (x & 3)));
            else if (x & 1) dst[x >> 1] |= (uint8_t)code; else dst[x >> 1] = (uint8_t)(code << 4);
        }
    }
}

static void fast_quant_yjk(void)                               /* SCREEN 12: Y 에 순서 디더, J·K 는 4점 평균 */
{
    int W = P.pic.w, H = P.pic.h, amp = 33 * P.dither / 100;
    memset(s_img, 0, sizeof s_img);
    for (int y = 0; y < H; y++) {
        const uint8_t *px = s_rgb + (size_t)y * (size_t)W * 3; uint8_t *dst = s_img + (size_t)y * (size_t)P.pic.stride;
        for (int x0 = 0; x0 < W; x0 += 4) {
            int yq[4], sj = 0, sk = 0;
            for (int i = 0; i < 4; i++) {
                int x = x0 + i, r = px[x * 3], g = px[x * 3 + 1], b = px[x * 3 + 2];
                int yn = 4 * b + 2 * r + g + (((bayer4[y & 3][x & 3] * 2 - 15) * amp) >> 4);       /* 0..1785 = 8 x Y8 */
                int y5 = (yn * 31 + 1020) / 2040; y5 = y5 < 0 ? 0 : y5 > 31 ? 31 : y5;
                yq[i] = y5; sj += fq5[r] - y5; sk += fq5[g] - y5;
            }
            int j = (sj + (sj >= 0 ? 2 : -2)) / 4, k = (sk + (sk >= 0 ? 2 : -2)) / 4;
            j = j < -32 ? -32 : j > 31 ? 31 : j; k = k < -32 ? -32 : k > 31 ? 31 : k;
            j &= 63; k &= 63;
            dst[x0] = (uint8_t)(yq[0] << 3 | (k & 7)); dst[x0 + 1] = (uint8_t)(yq[1] << 3 | (k >> 3));
            dst[x0 + 2] = (uint8_t)(yq[2] << 3 | (j & 7)); dst[x0 + 3] = (uint8_t)(yq[3] << 3 | (j >> 3));
        }
    }
}

static void fast_make_pic(void)
{
    static uint32_t fn;
    fast_init();
    P.pic.pal_changed = 0;
    if (P.scr == 12) { fast_quant_yjk(); return; }
    if (P.scr == 8) { fast_quant8(); return; }
    int n = P.npal, first = !P.pal_valid;
    if (first) fn = 0;
    if (first || (++fn & 7) == 0) {                            /* 8프레임에 한 번만 팔레트 계산 */
        memset(hist, 0, sizeof hist);
        int W = P.pic.w, H = P.pic.h, sx = W > 256 ? 4 : 2;
        for (int y = 0; y < H; y += 2) {
            const uint8_t *px = s_rgb + (size_t)y * (size_t)W * 3;
            for (int x = 0; x < W; x += sx) hist[((px[x * 3] * 7 + 127) / 255) << 6 | ((px[x * 3 + 1] * 7 + 127) / 255) << 3 | ((px[x * 3 + 2] * 7 + 127) / 255)]++;
        }
        uint8_t np[16][3]; make_palette(np, n);
        if (first) { memcpy(P.pal, np, sizeof P.pal); P.pal_valid = 1; P.pic.pal_changed = 1; }
        else { uint64_t eo = pal_error(P.pal, n), en = pal_error(np, n); if (en * 100 < eo * 85) { memcpy(P.pal, np, sizeof P.pal); P.pic.pal_changed = 1; } }
        if (P.pic.pal_changed) {
            memset(nn_lut, 0xFF, sizeof nn_lut);
            for (int c = 0; c < 512; c++) flut[c] = (uint8_t)nn_find(((c >> 6) << 5) | 16, (((c >> 3) & 7) << 5) | 16, ((c & 7) << 5) | 16);
        }
    }
    memcpy(P.pic.pal, P.pal, sizeof P.pic.pal);
    fast_quant_pal();
}
