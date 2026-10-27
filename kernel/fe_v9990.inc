/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_v9990.inc - 5.2.85: Gfx9000 (V9990) 프런트엔드 부품. fe.c 에는 화면 소스 표(src/vsrc.c)와 아이콘 선언(src/status.c)을 읽는 공용 코드만 두고,
 * 이 장치만의 그리기·꽂기·빼기는 여기 모은다 (구조 개편: 새 영상 장치 = 소스 표 한 줄 + 그리기 함수 하나 + 이런 부품 파일 하나).
 * fe.c 의 그리기 층(present_impl)이 vsrc_draw 표로 present_v9990 을 부르고, 특수 카트리지 꽂기·빼기 표(fe_extdev.inc)가 SPK_V9990 으로 v9_cart_insert·v9_cart_eject 를 부른다. */

/* V9990 아이콘: 칩이 화면을 내보내기 시작하면 3초 보이고 사라짐. 메뉴·창이 열렸거나 멈췄을 때는 계속 (F6·ROM 게임 중엔 숨김) */
static void v9_tick(uint32_t now)
{
    static int was;
    static uint32_t act;
    int on = run_state != RUN_STOPPED && ms_st[MS_V9990] == SS_ON;   /* 장치 선언 (src/status.c) */
    if (on && !was) act = now | 1;
    was = on;
    uint8_t v = (uint8_t)(on && badge_vis() && ((act && now - act < 3000) || status_show_all()));
    if (v != v9_vis_now) { v9_vis_now = v; margin_bd = -1; }
}

/* 4.0.29: V9990(Gfx9000) 화면. 줄이 바뀐 곳만 확대해서 그림. 그림자(이전 프레임)는 V9990 상태 메모리 뒤쪽에 둠 */
/* mode 0: V9990 만 보임 (그림 뒤 토스트·음량 표시까지). 1: 겹치기 — MSX 를 그린 뒤 그 위에 반반. 2: 나란히 — 오른쪽 반에 그림 */
static void present_v9990_draw(int mode)
{
    const int side = mode == 2;
    const uint32_t vx0 = side ? v9x : x0, vy0 = side ? v9y : y0, vw = side ? v9w : out_w, vh = side ? v9h : out_h;
    V9990 *v = M->v9;
    uint32_t (*sh)[V9_FBW] = (uint32_t (*)[V9_FBW])(void *)((uint8_t *)v + sizeof(V9990));
    uint32_t *line32 = (uint32_t *)(void *)((uint8_t *)sh + sizeof(uint32_t) * V9_FBH * V9_FBW);   /* 그림자 뒤 남는 자리 (BSS 절약) */
    uint16_t *xm = (uint16_t *)(void *)(line32 + 4096);
    uint32_t rows = (uint32_t)v->rows;
    if (rows > V9_FBH) rows = V9_FBH;
    if (vw > 4096 || !vh) return;
    for (uint32_t x = 0; x < vw; x++) xm[x] = (uint16_t)(x * V9_FBW / vw);
    uint32_t bp = (uint32_t)D.bpp;
    int last_sy = -1;
    for (uint32_t y = 0; y < vh; y++) {
        uint32_t ry = vy0 + y, sy = y * rows / vh;
        if (!pres_clip_w && ry >= pres_clip_y) break;                 /* 믹서 팝업 자리 아래는 그리지 않음 */
        if (!mode && !force_redraw && !memcmp(v->fb[sy], sh[sy], sizeof v->fb[sy])) { last_sy = -2; continue; }   /* 겹치기·나란히는 MSX 쪽 그리기가 여백·바탕을 다시 칠할 수 있어 줄 건너뛰기 없이 매번 그림 */
        uint8_t *dr = D.fb + (uintptr_t)ry * D.pitch + vx0 * bp;
        if ((int)sy != last_sy || mode == 1) {
            int tint = monitor_kind || vid_bright || vid_contrast != 100 || vid_satur != 100 || vid_gamma != 100;   /* VDP 와 같은 모니터 종류·화면 조절을 V9990 에도 */
            for (uint32_t x = 0; x < vw; x++) {
                uint32_t c = v->fb[sy][xm[x]];
                if (tint) { uint8_t r8 = (uint8_t)(c >> 16), g8 = (uint8_t)(c >> 8), b8 = (uint8_t)c; monitor_tint(&r8, &g8, &b8); c = ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8; }
                if (bp == 1) line32[x] = 16 + (((c >> 16 & 255) * 5 / 255) * 7 + (c >> 8 & 255) * 6 / 255) * 5 + (c & 255) * 4 / 255;
                else if (!D.bgr) line32[x] = ((c & 0xFF) << 16) | (c & 0xFF00) | (c >> 16 & 255);
                else line32[x] = c;
            }
            if (mode == 1 && bp == 4) {            /* 겹치기: 방금 그린 MSX 화면과 반반 (고정 50 %, 8비트 화면은 섞을 수 없어 V9990 만) */
                const uint32_t *under = (const uint32_t *)(const void *)dr;
                for (uint32_t x = 0; x < vw; x++) line32[x] = ((line32[x] >> 1) & 0x7F7F7F7Fu) + ((under[x] >> 1) & 0x7F7F7F7Fu);
            }
            if (bp == 1) for (uint32_t x = 0; x < vw; x++) ((uint8_t *)line32)[x] = (uint8_t)line32[x];
            last_sy = mode == 1 ? -1 : (int)sy;
        }
        if (pres_clip_w && ry >= pres_clip_y && ry < pres_clip_y2) {
            uint32_t cx0 = pres_clip_x > vx0 ? pres_clip_x - vx0 : 0, cx1 = cx0 + pres_clip_w;
            if (cx0 > vw) cx0 = vw;
            if (cx1 > vw) cx1 = vw;
            memcpy(dr, line32, cx0 * bp);
            memcpy(dr + cx1 * bp, (const uint8_t *)line32 + cx1 * bp, (vw - cx1) * bp);
        } else memcpy(dr, line32, vw * bp);
        mark_dirty_rect(vx0, ry, vw, 1);
    }
    for (uint32_t sy = 0; sy < rows; sy++) memcpy(sh[sy], v->fb[sy], sizeof v->fb[sy]);
    if (ov_on) ov_dirty = 1;
    if (mode) return;                          /* 겹치기·나란히: 뒷일은 MSX 쪽 그리기 끝에서 */
    force_redraw = 0;
    draw_toast_center();
    draw_vol_osd();
    last_present = P.ms();
}
static void present_v9990(void) { present_v9990_draw(0); }

/* Gfx9000 꽂기 (4.0.29: 포트 60h~6Fh). 슬롯은 표시만, 상태 메모리는 플랫폼이 줌. 돌려주는 값: 0 성공, 그 밖 msx_insert_cart 실패값 */
static int v9_cart_insert(int n, int quiet, uint32_t *size, int *abort)   /* *abort = 1 이면 이미 알렸으니 공용 알림 없이 끝냄 */
{
    *abort = 0;
    if (!P.v9990) { if (!quiet) toast(T(S_V9_NOMEM), 0); *abort = 1; return -1; }
    *size = 16384; memset(cart_buf[n], 0xFF, *size);
    int r = msx_insert_cart(M, n, cart_buf[n], *size, MAPPER_PLAIN);
    if (!r) msx_v9990_enable(M, (V9990 *)P.v9990);
    return r;
}
static void v9_cart_eject(int n) { if (cart_special[n] == 158) msx_v9990_enable(M, 0); }

