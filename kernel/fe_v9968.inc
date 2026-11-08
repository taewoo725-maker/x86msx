/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_v9968.inc - 8.0.94: V9968 카트리지 (두 번째 VDP) 프런트엔드 부품. fe_v9990.inc 와 같은 구조:
 * 화면 소스 표(src/vsrc.c)의 VSRC_V9968 이 present_v9968 을 부르고, 특수 카트리지 표(fe_extdev.inc)가 SPK_V9968 으로 v2_cart_insert·v2_cart_eject 를 부른다.
 * 카트리지 VDP 는 본체 VDP 와 별개 칩이라 자기 프레임 버퍼(v2->fb)를 가짐 — 여기서 그 줄들을 RGB 로 바꿔 화면 크기에 맞춰 그림. */

static uint32_t v2_line[4096];
static uint16_t v2_xm[4096];

static void v2_rgb(const VDP *v, uint8_t c, uint8_t *r, uint8_t *g, uint8_t *b)       /* 팔레트 번호 → RGB (확장 팔레트면 256색 표) */
{
    if (vdp_epal(v)) { *r = v->pal256[c][0]; *g = v->pal256[c][1]; *b = v->pal256[c][2]; }
    else { *r = v->palette[c & 15][0]; *g = v->palette[c & 15][1]; *b = v->palette[c & 15][2]; }
}
static uint32_t v2_pack(uint8_t r8, uint8_t g8, uint8_t b8)
{
    monitor_tint(&r8, &g8, &b8);
    if (D.bpp == 1) return 16 + (((uint32_t)r8 * 5 / 255) * 7 + (uint32_t)g8 * 6 / 255) * 5 + (uint32_t)b8 * 4 / 255;
    return D.bgr ? ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8 : ((uint32_t)b8 << 16) | ((uint32_t)g8 << 8) | r8;
}
/* 카트리지 VDP 한 줄 → 화면 폭 vw 만큼의 점 (v2_line) */
static void v2_make_line(const VDP *v, int sy, uint32_t vw)
{
    const uint8_t *src = v->fb[sy];
    int direct = v->line_direct[sy];
    uint32_t bgcol[1];
    (void)bgcol;
    if (direct >= 2) {
        uint8_t raw[256], rgb[256][3], pi[256];
        for (int x = 0; x < 256; x++) raw[x] = src[2 * x];
        vdp_yjk_line(raw, direct == 3, rgb, pi);
        for (uint32_t x = 0; x < vw; x++) {
            int sx = v2_xm[x] >> 1;
            uint8_t r8, g8, b8, ov = v->spr_ov[sy][sx];
            if (ov != 0xFF) vdp_g7_rgb(v, ov, &r8, &g8, &b8);
            else if (pi[sx] != 0xFF) v2_rgb(v, pi[sx], &r8, &g8, &b8);
            else { r8 = rgb[sx][0]; g8 = rgb[sx][1]; b8 = rgb[sx][2]; }
            v2_line[x] = v2_pack(r8, g8, b8);
        }
    } else if (direct == 1) {
        uint32_t cc[256];
        for (int c = 0; c < 256; c++) { uint8_t r8, g8, b8; vdp_g7_rgb(v, (uint8_t)c, &r8, &g8, &b8); cc[c] = v2_pack(r8, g8, b8); }
        for (uint32_t x = 0; x < vw; x++) v2_line[x] = cc[src[v2_xm[x]]];
    } else {
        uint32_t cc[256];
        int n = vdp_epal(v) ? 256 : 16;
        for (int c = 0; c < n; c++) {
            uint8_t r8, g8, b8;
            if (!vdp_epal(v) && v->pal_nsnap > 1) { const uint8_t *p = v->pal_snap[v->line_pal[sy] % VDP_PAL_SNAPS][c]; r8 = p[0]; g8 = p[1]; b8 = p[2]; }
            else v2_rgb(v, (uint8_t)c, &r8, &g8, &b8);
            cc[c] = v2_pack(r8, g8, b8);
        }
        for (uint32_t x = 0; x < vw; x++) v2_line[x] = cc[src[v2_xm[x]] & (n - 1)];
    }
    if (D.bpp == 1) for (uint32_t x = 0; x < vw; x++) ((uint8_t *)v2_line)[x] = (uint8_t)v2_line[x];
}
static void v2_put_row(uint32_t ry, uint32_t vx0, uint32_t vw, int blend)
{
    uint32_t bp = (uint32_t)D.bpp;
    uint8_t *dr = D.fb + (uintptr_t)ry * D.pitch + vx0 * bp;
    if (blend && bp == 4) {                                                       /* 겹치기: 방금 그린 MSX 화면과 반반 */
        const uint32_t *under = (const uint32_t *)(const void *)dr;
        for (uint32_t x = 0; x < vw; x++) v2_line[x] = ((v2_line[x] >> 1) & 0x7F7F7F7Fu) + ((under[x] >> 1) & 0x7F7F7F7Fu);
    }
    if (pres_clip_w && ry >= pres_clip_y && ry < pres_clip_y2) {                 /* 믹서 팝업 칸만 비움 */
        uint32_t cx0 = pres_clip_x > vx0 ? pres_clip_x - vx0 : 0, cx1 = cx0 + pres_clip_w;
        if (cx0 > vw) cx0 = vw;
        if (cx1 > vw) cx1 = vw;
        memcpy(dr, v2_line, cx0 * bp);
        memcpy(dr + cx1 * bp, (const uint8_t *)v2_line + cx1 * bp, (vw - cx1) * bp);
    } else memcpy(dr, v2_line, vw * bp);
    mark_dirty_rect(vx0, ry, vw, 1);
}
/* mode 0: V9968 만 보임 (뒤에 토스트·음량 표시). 1: 겹치기 — MSX 위에 반반. 2: 나란히 — 오른쪽 반 (V9990 과 같은 자리 v9x·v9y·v9w·v9h) */
static void present_v9968_draw(int mode)
{
    const VDP *v = M->v2;
    const int side = mode == 2;
    const uint32_t vx0 = side ? v9x : x0, vy0 = side ? v9y : y0, vw = side ? v9w : out_w, vh = side ? v9h : out_h;
    if (!v || vw > 4096 || vw < 2 || !vh) return;
    uint32_t lines = v->lines == 212 ? 212 : 192;
    uint32_t ph = vh * lines / FRAME_LINES, top = (vh - ph) / 2;                  /* 192줄 모드는 위아래에 테두리 */
    for (uint32_t x = 0; x < vw; x++) v2_xm[x] = (uint16_t)(x * VDP_W / vw);
    uint8_t br, bg, bb;
    v2_rgb(v, vdp_backdrop(v), &br, &bg, &bb);
    uint32_t bd = v2_pack(br, bg, bb);
    int last = -1;
    for (uint32_t y = 0; y < vh; y++) {
        uint32_t ry = vy0 + y;
        if (!pres_clip_w && ry >= pres_clip_y) break;
        if (y < top || y >= top + ph) {
            for (uint32_t x = 0; x < vw; x++) v2_line[x] = bd;
            if (D.bpp == 1) for (uint32_t x = 0; x < vw; x++) ((uint8_t *)v2_line)[x] = (uint8_t)bd;
            last = -1;
        } else {
            int sy = (int)((y - top) * lines / (ph ? ph : 1));
            if (sy >= (int)lines) sy = (int)lines - 1;
            if (sy != last || mode == 1) { v2_make_line(v, sy, vw); last = mode == 1 ? -1 : sy; }
        }
        v2_put_row(ry, vx0, vw, mode == 1 && y >= top && y < top + ph);
    }
    if (ov_on) ov_dirty = 1;
    if (mode) return;                                                             /* 겹치기·나란히: 뒷일은 MSX 쪽 그리기 끝에서 */
    force_redraw = 0;
    draw_toast_center();
    draw_vol_osd();
    last_present = P.ms();
}
static void present_v9968(void) { present_v9968_draw(0); }

/* V9968 카트리지 꽂기: 슬롯은 표시만, 두 번째 VDP 메모리는 플랫폼이 줌. 돌려주는 값: 0 성공, 그 밖 msx_insert_cart 실패값 */
static int v2_cart_insert(int n, int quiet, uint32_t *size, int *abort)
{
    *abort = 0;
    if (!P.v9968) { if (!quiet) toast("V9968: no memory", 0); *abort = 1; return -1; }
    *size = 16384; memset(cart_buf[n], 0xFF, *size);
    int r = msx_insert_cart(M, n, cart_buf[n], *size, MAPPER_PLAIN);
    if (!r) msx_v9968_enable(M, (VDP *)P.v9968);
    return r;
}
static void v2_cart_eject(int n) { if (cart_special[n] == 171) { msx_v9968_enable(M, 0); if (vsrc_pref == VSRC_V9968) vsrc_pref = -1; } }
