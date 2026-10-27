/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fe_ov.inc - 3.5.0 떠 있는 도구 막대 · 미디어 판 · 막대 팝업 (fe.c가 포함, 설계서 §51)
 *
 * X86APPLEII 2.7.1의 떠 있는 막대를 MSX에 맞게 옮김 (디자인 시안 "X86MSX 화면 디자인 시안 v3.5").
 *   위 도구 막대: 기종 배지 | 모니터 · 스캔라인 · 조이스틱 · MSX 마우스 | 저장 · 불러오기 · 되감기 · 멈춤 · 화면 저장 |
 *                 속도 네 칸 | 음량 + 음원 계기 | 메뉴 · 리셋 · 전원
 *   오른쪽 미디어 판: 카트리지 슬롯, 디스크 A·B, 카세트(위치 막대), 하드 디스크. 누르면 그 장치 메뉴가 카드 옆에 팝업, 오른쪽 단추 = 꺼내기
 * 보이기: 포인터를 화면 위끝으로 = 막대 + 판, 오른쪽 끝 = 판만. 포인터가 떠나면 0.5초 뒤 사라짐.
 * MSX는 막대가 보이는 동안에도 계속 돈다 (팝업·창이 열리면 지금처럼 멈춤).
 * 그리기: present()가 막대 자리의 줄을 다시 그렸거나 여백을 다시 칠했으면 ov_dirty → 막대를 다시 그림.
 */

#define OV_MS 500                           /* 포인터가 떠나면 0.5초 뒤 사라짐 */
#define OV_KEY_MS 1500                      /* 미디어 팝업(RCtrl+RShift+D)으로 열 때 */
/* 색 (fe.c ui_rgb 32~47) */
enum { T_BG = 32, T_HOT, T_LINE, T_OFF, T_LBL, T_CHIP, T_CARD, T_POP, T_ACC, T_ACC2, T_WHITE, T_PWR, T_BR, T_BY, T_BG2, T_BB };

static uint32_t ov_until;
static uint32_t ov_sig;
enum { OB_NONE, OB_MACH, OB_MON, OB_SCAN, OB_JOY, OB_MOUSE, OB_SAVE, OB_LOAD, OB_PAUSE, OB_SHOT, OB_SPEED, OB_VOL, OB_MIX,
       OB_MENU, OB_RESET, OB_POWER, OB_CARD };
typedef struct { uint32_t x, y, w, h; int kind, arg; } ObHit;
static ObHit ob_hits[48];
static int ob_n, ob_hover = -1;
static void ob_add(int kind, int arg, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (ob_n >= (int)(sizeof ob_hits / sizeof ob_hits[0])) return;
    ObHit *q = &ob_hits[ob_n++];
    q->x = x; q->y = y; q->w = w; q->h = h; q->kind = kind; q->arg = arg;
}
static int ob_at(uint32_t x, uint32_t y)
{
    if (!(ov_on & 3)) return -1;
    for (int i = ob_n - 1; i >= 0; i--) if (x >= ob_hits[i].x && x < ob_hits[i].x + ob_hits[i].w && y >= ob_hits[i].y && y < ob_hits[i].y + ob_hits[i].h) return i;
    return -1;
}

/* ---- 미디어 판에 들어갈 장치 ---- */
enum { MD_SLOT1, MD_SLOT2, MD_SLOT3, MD_SLOT4, MD_SLOT5, MD_DISKA, MD_DISKB, MD_HD, MD_TAPE, MD_N };   /* 3.7.3: 하드 디스크 → 카세트 (맨 아래, 앞에 줄) */
static int md_has(int k)
{
    if (k <= MD_SLOT2) return 1;
    if (k <= MD_SLOT4) return slot_expand || M->cart_slot[k] >= 0;      /* 3.3.1: 기종의 C·D */
    if (k == MD_SLOT5) return M->cart_slot[4] >= 0;                     /* 3.6.7 */
    if (k == MD_DISKA) return disk_drives() >= 1;
    if (k == MD_DISKB) return disk_drives() >= 2;
    return 1;                                                            /* 카세트, 3.7.3: 하드 디스크도 늘 (가상 하드 C~H 메뉴) */
}
static int md_busy(int k)
{
    if (k == MD_DISKA || k == MD_DISKB) { int d = k - MD_DISKA; return M->fdc.motor && M->cpu.cycles - M->fdc.last_access[d] < 3579545u / 6; }
    if (k == MD_TAPE) return M->tape && M->tape_motor;
    if (k == MD_HD) return (hd_secs || hd2_secs) && P.ms() - hd_last_ms < 200;
    return 0;
}
static const char *md_name(int k)
{
    if (k <= MD_SLOT5) return cart_name[k][0] ? cart_name[k] : 0;
    if (k <= MD_DISKB) return disk_size[k - MD_DISKA] && disk_name[k - MD_DISKA][0] ? disk_name[k - MD_DISKA] : 0;
    if (k == MD_TAPE) return M->tape && tape_name[0] ? tape_name : 0;
    if (hd_secs && hd_name[0]) return hd_name;
    if (hd2_secs && hd2_name[0]) return hd2_name;
    static char vn[80];                                                  /* 3.7.3: 이미지가 없으면 연결한 가상 하드 */
    for (int i = 0; i < 6; i++) if (vhd_dir[i][0]) { char dl[5] = { (char)('C' + i), ':', ' ', 0, 0 }; copy_str(vn, sizeof vn, dl); str_append(vn, sizeof vn, vhd_dir[i]); return vn; }
    return 0;
}
static int md_icon(int k) { return k <= MD_SLOT5 ? ICON_CART : k <= MD_DISKB ? ICON_FDD : k == MD_TAPE ? ICON_CAS : ICON_HD; }
static const int md_insert[MD_N] = { A_CART1_INSERT, A_CART2_INSERT, A_CART3_INSERT, A_CART4_INSERT, A_CART5_INSERT, A_DISKA_INSERT, A_DISKB_INSERT, A_HD_INSERT, A_TAPE_INSERT };
static const int md_eject[MD_N] = { A_CART1_EJECT, A_CART2_EJECT, A_CART3_EJECT, A_CART4_EJECT, A_CART5_EJECT, A_DISKA_EJECT, A_DISKB_EJECT, A_HD_EJECT, A_TAPE_EJECT };
static const Menu *const md_menu[MD_N] = { &m_cart1, &m_cart2, &m_cart3, &m_cart4, &m_cart5, &m_diska, &m_diskb, &m_hd, &m_tape };

/* ---- 자리 ---- */
#define RP_CH (SLH + LH + 20 * S)
static int tb_nokeys;                       /* 막대가 좁으면 단추의 단축키 칩을 뺌 (이름은 남김) */
static uint32_t tb_h(void) { return 5 * S + LH + 2 * S + SLH + 3 * S + (tb_nokeys ? 0 : SLH + 2 * S) + 6 * S; }
static void rp_layout(void)
{
    uint32_t m = D.w > x0 + out_w ? D.w - (x0 + out_w) : 0;
    int n = 0, g2 = 0;
    for (int k = 0; k < MD_N; k++) if (md_has(k)) { n++; if (k >= MD_DISKA) g2 = 1; }
    if (m >= 150 * S) { rp_fixed = 1; rp_w = m - 16 * S; if (rp_w > 300 * S) rp_w = 300 * S; rp_x = x0 + out_w + (m - rp_w) / 2; }
    else { rp_fixed = 0; rp_w = 260 * S; if (rp_w > D.w / 2) rp_w = D.w / 2; rp_x = D.w - rp_w - 8 * S; }
    rp_y = tb_h() + 10 * S;
    if (M->tape) n++;                                                    /* 카세트 카드는 위치 막대만큼 더 (반 칸) */
    rp_h = 8 * S + SLH + 6 * S + 9 * S + (uint32_t)(1 + g2) * (SLH + 4 * S) + (uint32_t)n * (RP_CH + 6 * S) - (M->tape ? RP_CH / 2 : 0) + 8 * S + 3 * (SLH + 3 * S) + 6 * S;
    if (rp_y + rp_h > D.h) rp_y = D.h > rp_h ? D.h - rp_h : 0;
}
static int ov_contains(uint32_t x, uint32_t y)
{
    return ((ov_on & 1) && y < tb_h()) || ((ov_on & 2) && x >= rp_x && x < rp_x + rp_w && y >= rp_y && y < rp_y + rp_h);
}

static void ov_show_part(int m, uint32_t ms)
{
    if (!D.fb || ui_active()) return;
    uint32_t until = P.ms() + ms;
    if (!until) until = 1;
    if ((int32_t)(until - ov_until) > 0 || !(ov_on & 3)) ov_until = until;
    if ((ov_on | m) != ov_on) { if ((m & 1) && !(ov_on & 1)) margin_bd = -1; ov_on |= m; ov_dirty = 1; rp_layout(); }   /* 도구 막대가 나오면 여백의 기종 카드를 지움 */
}
static void ov_hide(void)
{
    if (!(ov_on & 3)) return;
    ov_on &= ~3; ob_n = 0; ob_hover = -1;
    full_redraw();                                                       /* 막대 밑 MSX 화면·여백을 다시 */
}

/* ---- 그림 도우미 ---- */
static void key_chip(uint32_t x, uint32_t y, const char *k, uint8_t fg)
{
    uint32_t kw = ui_sw(k) + 10 * S;
    ui_rrect(x, y, kw, SLH + 2 * S, 3 * S, T_CHIP);
    text_f(SF, 1, x + 5 * S, y + S, k, fg, 0xFF, 0, 1);
}
static uint32_t chip_w(const char *k) { return k && k[0] ? ui_sw(k) + 10 * S : 0; }
static void band4(uint32_t x, uint32_t y, uint32_t h)
{
    static const uint8_t c[4] = { T_BR, T_BY, T_BG2, T_BB };
    for (int i = 0; i < 4; i++) ui_rrect(x + (uint32_t)i * 4 * S, y, 3 * S, h, S, c[i]);
}

typedef struct { int kind, arg, icon; const char *label; int action; int on, dim; uint8_t acc; } ObBtn;
static uint32_t btn_w(const ObBtn *b, int compact)
{
    char kb[48];
    const char *k = b->action && !tb_nokeys ? menu_keys_for(b->action, kb, sizeof kb) : 0;
    uint32_t w = LH, q = chip_w(k);
    if (!compact && ui_sw(b->label) > w) w = ui_sw(b->label);
    if (q > w) w = q;
    return w + 12 * S;
}
static void btn_draw(const ObBtn *b, uint32_t x, uint32_t w, int compact)
{
    uint32_t th = tb_h(), h = th - 8 * S, y = 4 * S;
    int hot = ob_hover >= 0 && ob_hits[ob_hover].kind == b->kind && ob_hits[ob_hover].x == x;
    uint8_t bg = hot ? T_HOT : b->on ? T_ACC : T_BG;
    ui_rrect(x, y, w, h, 7 * S, bg);
    uint32_t iy = y + 4 * S;
    draw_icon_hd(x + (w - LH) / 2, iy, LH, b->icon, b->dim ? T_OFF : T_WHITE, b->dim ? T_OFF : b->acc, bg);
    uint32_t ly = iy + LH + 2 * S;
    if (!compact) { uint32_t lw = ui_sw(b->label); text_f(SF, 1, x + (w > lw ? (w - lw) / 2 : 0), ly, b->label, b->dim ? T_OFF : T_WHITE, 0xFF, w, 1); ly += SLH + 3 * S; }
    char kb[48];
    const char *k = b->action && !tb_nokeys ? menu_keys_for(b->action, kb, sizeof kb) : 0;
    if (k) key_chip(x + (w - chip_w(k)) / 2, ly, k, T_WHITE);
    ob_add(b->kind, b->arg, x, 0, w, th);                                /* 누를 자리는 막대 위끝까지 */
}

/* 음원 계기: PSG · SCC · FM(MSX-MUSIC) · PCM 막대 (믹서 계기 값 0~1000) */
static int mix_meter_of(int c);
static void meters_draw(uint32_t x, uint32_t y, uint32_t h)
{
    for (int i = 0; i < 4; i++) {
        int v = mix_meter_of(i);
        uint32_t bh = (uint32_t)v * h / 1000;
        if (bh > h) bh = h;
        fe_fill(x + (uint32_t)i * 5 * S, y, 4 * S, h, T_CHIP);
        if (bh) fe_fill(x + (uint32_t)i * 5 * S, y + h - bh, 4 * S, bh, v > 800 ? C_WARN : C_OK);
    }
}

static void tb_draw(void)
{
    static const StrId mk[4] = { S_MON_COLOR, S_MON_BW, S_MON_GREEN, S_MON_AMBER };
    int paused = run_state != RUN_ON;
    int r800 = msx_dev(M, MDEV_S1990) && !(M->s1990_cpu & 0x20);
    const ObBtn left[] = {
        { OB_MON, 0, ICON_MONITOR, T(mk[monitor_kind & 3]), 0, monitor_kind != 0, 0, T_ACC2 },
        { OB_SCAN, 0, ICON_SCAN, T(S_OV_SCAN), 0, scanlines != 0, 0, T_ACC2 },
        { OB_NONE, 0, -1, 0, 0, 0, 0, 0 },
        { OB_SAVE, 0, ICON_SAVE, T(S_OV_SAVE), A_QUICK_SAVE, 0, 0, T_ACC2 },
        { OB_LOAD, 0, ICON_LOAD, T(S_OV_LOAD), A_QUICK_LOAD, 0, 0, T_ACC2 },
        { OB_PAUSE, 0, ICON_PAUSE, T(paused ? S_OV_CONT : S_OV_PAUSE), A_RUN_PAUSE, paused, 0, T_ACC2 },
        { OB_SHOT, 0, ICON_CAMERA, T(S_OV_SHOT), A_SCREENSHOT, 0, 0, T_ACC2 },
    };
    const ObBtn right[] = {
        { OB_MENU, 0, ICON_MENU, T(S_OV_MENU), A_MENU, 0, 0, T_ACC2 },
        { OB_RESET, 0, ICON_RESET, T(S_OV_RESET), A_HARD_RESET, 0, 0, T_ACC2 },
        { OB_POWER, 0, ICON_POWER, T(S_OV_POWER), 0, 0, 0, T_PWR },
    };
    int nl = (int)(sizeof left / sizeof left[0]), nr = (int)(sizeof right / sizeof right[0]);
    const char *mn = machine_name;                                       /* "MSX2 - " 같은 앞부분은 빼고 */
    if ((mn[0] | 32) == 'm' && (mn[1] | 32) == 's' && (mn[2] | 32) == 'x')
        for (const char *q = mn; q[0] && q[1] && q[2]; q++) if (q[0] == ' ' && q[1] == '-' && q[2] == ' ') { mn = q + 3; break; }
    static const char *const gen_n[4] = { "MSX", "MSX2", "MSX2+", "turbo R" };
    char sub[64];
    copy_str(sub, sizeof sub, gen_n[machine_gen() & 3]);
    str_append(sub, sizeof sub, r800 ? " · R800" : " · Z80");
    if (M->mram_size) { str_append(sub, sizeof sub, " · "); num_append(sub, sizeof sub, M->mram_size >> 10, 0, ' '); str_append(sub, sizeof sub, "K"); }   /* 매퍼가 없으면 RAM 크기는 모름 */
    const char *spl[4] = { T(S_OV_X1), T(S_OV_X2), T(S_OV_X4), T(S_OV_XMAX) };
    uint32_t gap = 2 * S, sepw = 12 * S, badge_w = 0, spw = 0, vw = 0, need = 0;
    int compact = 0, narrow = 0, small_badge = 0;
    for (int pass = 0; pass < 5; pass++) {                               /* 폭이 모자라면: 배지 작게 → 단축키 칩 빼기 → 속도 한 칸·음량 짧게 → 이름 빼기 → 배지 빼기 */
        small_badge = pass >= 1; tb_nokeys = pass >= 2; narrow = pass >= 3; compact = pass >= 4;
        uint32_t nw = small_badge ? ui_sw(mn) : text_w(mn);
        badge_w = 22 * S + (nw > ui_sw(sub) ? nw : ui_sw(sub)) + 12 * S;
        spw = 0;
        if (narrow) spw = ui_sw(spl[3]) + 16 * S;
        else for (int i = 0; i < 4; i++) spw += ui_sw(spl[i]) + 16 * S + 3 * S;
        vw = narrow ? 90 * S : 150 * S;
        need = 10 * S + badge_w + sepw;
        for (int i = 0; i < nl; i++) need += left[i].kind ? btn_w(&left[i], compact) + gap : sepw;
        need += sepw + spw + sepw + vw + sepw;
        for (int i = 0; i < nr; i++) need += btn_w(&right[i], compact) + gap;
        if (need <= D.w) break;
    }
    if (need > D.w) badge_w = 0;
    uint32_t th = tb_h();
    fe_fill(0, 0, D.w, th, T_BG);
    fe_fill(0, th - S, D.w, S, T_LINE);
    uint32_t x = 10 * S;
    if (badge_w) {
        band4(x, 10 * S, th - 20 * S);
        uint32_t tx = x + 22 * S, tw = badge_w - 22 * S;
        if (small_badge) small_ell(tx, th / 2 - SLH - S, mn, T_WHITE, 0xFF, tw);
        else text_ell(tx, th / 2 - LH, mn, T_WHITE, 0xFF, tw);
        small_ell(tx, th / 2 + 2 * S, sub, T_LBL, 0xFF, tw);
        ob_add(OB_MACH, 0, x, 0, badge_w, th);
        x += badge_w;
        fe_fill(x + sepw / 2, 12 * S, S, th - 24 * S, T_LINE); x += sepw;
    }
    for (int i = 0; i < nl; i++) {
        if (!left[i].kind) { fe_fill(x + sepw / 2, 12 * S, S, th - 24 * S, T_LINE); x += sepw; continue; }
        uint32_t w = btn_w(&left[i], compact);
        btn_draw(&left[i], x, w, compact);
        x += w + gap;
    }
    fe_fill(x + sepw / 2, 12 * S, S, th - 24 * S, T_LINE); x += sepw;
    {                                                                    /* 속도: 네 칸 */
        uint32_t ty = 8 * S;
        small_ell(x, ty, T(S_OV_SPEED), T_LBL, 0xFF, spw);
        char kb[48];
        const char *k = menu_keys_for(A_SPEED_UP, kb, sizeof kb);
        if (k && !narrow && !tb_nokeys && chip_w(k) + ui_sw(T(S_OV_SPEED)) + 6 * S <= spw) key_chip(x + spw - chip_w(k), ty, k, T_WHITE);
        uint32_t py = ty + SLH + 5 * S, ph = th > py + 6 * S + SLH ? th - py - 6 * S : SLH, px = x;
        int cur = speed_max ? 3 : speed >= 400 ? 2 : speed >= 200 ? 1 : 0;
        int exact = speed_max || speed == 100 || speed == 200 || speed == 400;
        for (int i = 0; i < 4; i++) {
            if (narrow && i != cur) continue;                            /* 좁으면 지금 것만 (누르면 다음) */
            uint32_t w = narrow ? spw : ui_sw(spl[i]) + 16 * S;
            int on = exact && cur == i;
            int hot = ob_hover >= 0 && ob_hits[ob_hover].kind == OB_SPEED && ob_hits[ob_hover].x == px;
            ui_rrect(px, py, w, ph, 6 * S, hot ? T_HOT : on ? T_ACC : T_CHIP);
            text_f(SF, 1, px + 8 * S, ui_sty(py, ph), spl[i], T_WHITE, 0xFF, 0, 1);
            ob_add(OB_SPEED, narrow ? (i + 1) & 3 : i, px, py, w, th - py);
            px += w + 3 * S;
        }
        x += spw;
    }
    fe_fill(x + sepw / 2, 12 * S, S, th - 24 * S, T_LINE); x += sepw;
    {                                                                    /* 음량: 글 + 음원 계기 (누르면 믹서) + 막대 */
        uint32_t ty = 8 * S;
        char v[32] = "";
        if (!narrow) { copy_str(v, sizeof v, T(S_OV_VOL)); str_append(v, sizeof v, " "); }
        if (muted) str_append(v, sizeof v, T(S_OV_MUTE)); else { num_append(v, sizeof v, (uint32_t)volume, 0, ' '); str_append(v, sizeof v, "%"); }
        int hot = ob_hover >= 0 && ob_hits[ob_hover].kind == OB_MIX;
        if (hot) ui_rrect(x - 4 * S, ty - 3 * S, vw + 8 * S, SLH + 6 * S, 5 * S, T_HOT);
        small_ell(x, ty, v, muted ? C_BAD : T_LBL, 0xFF, vw);
        if (!narrow) meters_draw(x + vw - 20 * S, ty + 2 * S, SLH - 2 * S);
        ob_add(OB_MIX, 0, x - 4 * S, 0, vw + 8 * S, ty + SLH + 4 * S);
        uint32_t by = ty + SLH + 5 * S, bh = th > by + 6 * S ? th - by - 6 * S : 16 * S, ty2 = by + bh / 2 - 3 * S;
        ui_rrect(x, ty2, vw, 6 * S, 3 * S, T_CHIP);
        uint32_t fw = vw * (uint32_t)volume / 100;
        if (fw) ui_rrect(x, ty2, fw, 6 * S, 3 * S, muted ? T_OFF : T_ACC2);
        ui_rrect(x + (fw > 8 * S ? fw - 8 * S : 0), ty2 - 5 * S, 16 * S, 16 * S, 8 * S, muted ? T_LBL : T_WHITE);
        ob_add(OB_VOL, 0, x, by, vw, th - by);
        x += vw;
    }
    {                                                                    /* 오른쪽 끝: 메뉴 · 리셋 · 전원 */
        uint32_t rw = 0;
        for (int i = 0; i < nr; i++) rw += btn_w(&right[i], compact) + gap;
        uint32_t rx = D.w > rw + 8 * S ? D.w - rw - 8 * S : x;
        if (rx < x + sepw) rx = x + sepw;
        fe_fill(rx - sepw / 2, 12 * S, S, th - 24 * S, T_LINE);
        for (int i = 0; i < nr; i++) { uint32_t w = btn_w(&right[i], compact); btn_draw(&right[i], rx, w, compact); rx += w + gap; }
    }
}

static void rp_draw(void)
{
    rp_layout();
    ui_rrect(rp_x + 3 * S, rp_y + 5 * S, rp_w, rp_h, 12 * S, C_SHADOW);
    ui_rframe(rp_x, rp_y, rp_w, rp_h, 12 * S, T_LINE, T_BG);
    uint32_t cx = rp_x + 8 * S, cw = rp_w - 16 * S, cy = rp_y + 8 * S;
    small_ell(cx + 2 * S, cy, T(S_OV_MEDIA), T_LBL, 0xFF, cw / 2);
    { char kb[48]; const char *k = menu_keys_for(A_MEDIA, kb, sizeof kb); if (k && chip_w(k) < cw / 2) key_chip(cx + cw - chip_w(k), cy, k, C_WARN); }
    cy += SLH + 6 * S;
    int g2 = 0;
    for (int k = 0; k < MD_N; k++) {
        if (!md_has(k)) continue;
        if (k == MD_SLOT1 || (k >= MD_DISKA && !g2)) {                  /* 묶음 이름: 카트리지 / 디스크 · 테이프 · 하드 */
            if (k >= MD_DISKA) g2 = 1;
            small_ell(cx + 2 * S, cy, T(k == MD_SLOT1 ? S_OV_GRP_CART : S_OV_GRP_MEDIA), T_OFF, 0xFF, cw);
            cy += SLH + 4 * S;
        }
        if (k == MD_TAPE) { fe_fill(cx + 6 * S, cy + 2 * S, cw - 12 * S, S, T_LINE); cy += 9 * S; }   /* 3.7.3: 카세트 앞 줄 */
        const char *nm = md_name(k);
        int busy = md_busy(k);
        int hot = ob_hover >= 0 && ob_hits[ob_hover].kind == OB_CARD && ob_hits[ob_hover].arg == k;
        uint32_t ch = RP_CH + (k == MD_TAPE && M->tape ? RP_CH / 2 : 0);
        uint8_t bg = hot ? T_HOT : T_CARD;
        ui_rframe(cx, cy, cw, ch, 9 * S, busy ? C_WARN : T_LINE, bg);
        uint32_t isz = RP_CH - 18 * S;
        if (isz > cw / 5) isz = cw / 5;
        uint32_t iy = cy + (RP_CH - isz) / 2;
        draw_icon_hd(cx + 8 * S, iy, isz, md_icon(k), busy ? C_WARN : nm ? T_WHITE : T_OFF, busy ? C_WARN : nm ? C_WARN : T_OFF, bg);
        uint32_t tx = cx + 14 * S + isz, tw = cw > tx - cx + 6 * S ? cw - (tx - cx) - 6 * S : 0;
        char kb[48];
        const char *key = menu_keys_for(md_insert[k], kb, sizeof kb);
        uint32_t kw = key ? chip_w(key) : 0;
        if (kw + 30 * S > tw) { key = 0; kw = 0; }
        char lb[80] = "";
        if (k <= MD_SLOT5) {
            copy_str(lb, sizeof lb, T(S_OV_SLOT)); str_append(lb, sizeof lb, " "); num_append(lb, sizeof lb, (uint32_t)k + 1, 0, ' ');
            if (M->cart[k].present) { const char *mt = cart_special[k] ? 0 : cart_mapper_name((MapperType)M->cart[k].mapper); if (mt && mt[0]) { str_append(lb, sizeof lb, " · "); str_append(lb, sizeof lb, mt); } }
        } else if (k <= MD_DISKB) {
            copy_str(lb, sizeof lb, T(S_OV_DRIVE)); str_append(lb, sizeof lb, k == MD_DISKA ? " A" : " B");
            if (disk_wp[k - MD_DISKA] && nm) { str_append(lb, sizeof lb, " · "); str_append(lb, sizeof lb, T(S_OV_WP)); }
        } else if (k == MD_TAPE) {
            copy_str(lb, sizeof lb, T(S_CASSETTE));
            if (M->tape) {
                tape_scan();
                int cur = 0;
                for (int i = 0; i < tape_nfile; i++) if (M->tape_pos >= tape_fpos[i]) cur = i + 1;
                if (tape_nfile) { str_append(lb, sizeof lb, " · "); num_append(lb, sizeof lb, (uint32_t)cur, 0, ' '); str_append(lb, sizeof lb, "/"); num_append(lb, sizeof lb, (uint32_t)tape_nfile, 0, ' '); }
            }
        } else copy_str(lb, sizeof lb, T(S_HARDDISK));
        small_ell(tx, cy + 6 * S, lb, busy ? C_WARN : T_LBL, 0xFF, tw > kw + 4 * S ? tw - kw - 4 * S : tw);
        if (key) key_chip(cx + cw - kw - 6 * S, cy + 5 * S, key, T_WHITE);
        uint32_t ny = cy + 8 * S + SLH;
        if (nm && text_w(nm) <= tw) text_f(F, S, tx, ny, nm, T_WHITE, 0xFF, tw, 1);
        else if (nm) small_ell(tx, ny + 2 * S, nm, T_WHITE, 0xFF, tw);
        else small_ell(tx, ny + 2 * S, ui_sw(T(S_OV_EMPTY)) <= tw ? T(S_OV_EMPTY) : T(S_OV_EMPTY_S), T_OFF, 0xFF, tw);
        if (k == MD_TAPE && M->tape) {                                   /* 위치 막대 + 카운터 */
            char cnt[16] = "";
            num_append(cnt, sizeof cnt, M->tape_pos, 0, ' ');
            uint32_t cwid = ui_sw(cnt), by = cy + RP_CH + 2 * S, bw = tw > cwid + 10 * S ? tw - cwid - 10 * S : 0;
            ui_rrect(tx, by + SLH / 2 - 2 * S, bw, 4 * S, 2 * S, T_CHIP);
            uint32_t f = M->tape_size ? (uint32_t)((uint64_t)bw * M->tape_pos / M->tape_size) : 0;
            if (f) ui_rrect(tx, by + SLH / 2 - 2 * S, f > bw ? bw : f, 4 * S, 2 * S, T_ACC2);
            text_f(SF, 1, tx + bw + 8 * S, by, cnt, T_LBL, 0xFF, 0, 1);
        }
        ob_add(OB_CARD, k, cx, cy, cw, ch);
        cy += ch + 6 * S;
    }
    cy += 2 * S;
    fe_fill(cx, cy, cw, S, T_LINE); cy += 6 * S;
    small_ell(cx + 2 * S, cy, T(S_OV_HINT1), T_LBL, 0xFF, cw - 4 * S); cy += SLH + 3 * S;
    small_ell(cx + 2 * S, cy, T(S_OV_HINT2), T_LBL, 0xFF, cw - 4 * S); cy += SLH + 3 * S;
    char t[64] = "";
    int r800 = msx_dev(M, MDEV_S1990) && !(M->s1990_cpu & 0x20);
    if (run_state != RUN_ON) copy_str(t, sizeof t, T(S_OV_PAUSE));
    else {
        copy_str(t, sizeof t, T(S_OV_SPEED)); str_append(t, sizeof t, " ");
        if (speed_max || turbo_hold) str_append(t, sizeof t, T(S_OV_XMAX)); else { num_append(t, sizeof t, (uint32_t)speed, 0, ' '); str_append(t, sizeof t, "%"); }
        str_append(t, sizeof t, r800 ? " · R800" : " · Z80");
    }
    small_ell(cx + 2 * S, cy, t, run_state != RUN_ON ? C_WARN : C_OK, 0xFF, cw - 4 * S);
}

static void ov_draw(void)
{
    if (!D.fb || !(ov_on & 3) || !M) return;
    ob_n = 0;
    if (ov_on & 1) tb_draw();
    if (ov_on & 2) rp_draw();
    ov_dirty = 0;
}

/* 바뀐 것이 있나 (장치 이름·불·속도·음량·가리킨 단추 …): 있으면 다시 그림 */
static void ov_check(void)
{
    uint32_t h = 2166136261u;
#define OVH(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    for (int k = 0; k < MD_N; k++) {
        OVH(md_has(k)); OVH(md_busy(k));
        const char *n = md_name(k);
        if (n) for (const char *q = n; *q; q++) OVH(*q);
        if (k <= MD_SLOT5) OVH(M->cart[k].mapper);
    }
    OVH(M->tape ? M->tape_pos / 256 : 0); OVH(disk_wp[0]); OVH(disk_wp[1]);
    OVH(monitor_kind); OVH(scanlines != 0); OVH(joy_mode[0]); OVH(joy_mode[1]); OVH(mouse_port);
    OVH(speed); OVH(speed_max); OVH(turbo_hold); OVH(volume); OVH(muted); OVH(run_state);
    OVH(ob_hover); OVH(M->s1990_cpu); OVH(lang);
    for (int i = 0; i < 4; i++) OVH(mix_meter_of(i) / 125);
#undef OVH
    if (h != ov_sig) { ov_sig = h; ov_dirty = 1; }
}

/* 매 프레임 (화면을 그린 뒤) */
static void ov_tick(int need)
{
    if (!(ov_on & 3)) return;
    if (ui_active()) return;
    uint32_t now = P.ms();
    if ((int32_t)(now - ov_until) >= 0 && !(ptr_used && ov_contains(ptr_x, ptr_y))) { ov_hide(); return; }
    rp_layout();
    ov_check();
    if (ov_dirty || need) ov_draw();
}

/* ---- 팝업 메뉴 (메뉴 막대 없이 그 자리에) ---- */
static void pop_open(const Menu *m, uint32_t x, uint32_t y, int ralign)
{
    input_init(&in); mq_head = mq_tail = 0;
    snap_valid = 0;
    bar_visible = 0; depth = 0; dialog = 0; menu_under = 0; popup_act = -1; ui_ret_valid = 0;
    pop_mode = 1; pop_x = x; pop_y = y; pop_ralign = ralign;
    last_menu_key = P.ms();
    open_level(m);
    menu_dirty = 1;
}
static void pop_media(void)                                              /* RCtrl+RShift+D, 파일 → 미디어 */
{
    ov_on |= 3; ov_until = P.ms() + OV_KEY_MS; rp_layout(); ov_dirty = 1;
    pop_open(&m_media, rp_x > 6 * S ? rp_x - 6 * S : 0, rp_y, 1);
}

/* ---- 막대 단추 누름 ---- */
static void ob_click(int i, uint32_t px, int button)
{
    const ObHit h = ob_hits[i];
    ov_until = P.ms() + OV_MS; if (!ov_until) ov_until = 1;
    switch (h.kind) {
    case OB_MACH: do_action(A_MACHINE); return;
    case OB_MON: monitor_kind = (monitor_kind + 1) & 3; recompute_palette(); if (P.set_palette) P.set_palette(); full_redraw(); settings_changed(); break;
    case OB_SCAN: scanlines = scanlines ? 0 : scan_last; if (scanlines) scan_last = scanlines; full_redraw(); settings_changed(); break;
    case OB_JOY: do_action(A_POP_JOY); return;
    case OB_MOUSE: do_action(A_POP_MOUSE); return;
    case OB_SAVE: pop_open(&m_save, h.x, tb_h() + 2 * S, 0); return;
    case OB_LOAD: pop_open(&m_load, h.x, tb_h() + 2 * S, 0); return;
    case OB_PAUSE: do_action(A_RUN_PAUSE); break;
    case OB_SHOT: do_action(A_SCREENSHOT); break;
    case OB_SPEED:
        speed_max = h.arg == 3; turbo_hold = 0;
        if (h.arg < 3) speed = h.arg == 0 ? 100 : h.arg == 1 ? 200 : 400;
        settings_changed(); break;
    case OB_MIX: if (button == 2) { muted = !muted; settings_changed(); break; } do_action(A_PROPS_MIX); return;
    case OB_VOL: { int v = (int)((px - h.x) * 100 / (h.w ? h.w : 1)); v = (v + 2) / 5 * 5; volume = v < 0 ? 0 : v > 100 ? 100 : v; muted = 0; mix_vol[MIX_MASTER] = volume; mix_mute[MIX_MASTER] = 0; settings_changed(); break; }
    case OB_MENU: ov_hide(); input_init(&in); mq_head = mq_tail = 0; menu_show(); return;
    case OB_RESET: do_action(A_HARD_RESET); break;
    case OB_POWER: pop_open(&m_power, h.x + h.w, tb_h() + 2 * S, 1); return;
    case OB_CARD:
        if (button == 2) { do_action(md_eject[h.arg]); break; }      /* 오른쪽 단추 = 꺼내기 */
        pop_open(md_menu[h.arg], h.x > 6 * S ? h.x - 6 * S : 0, h.y, 1);
        return;
    }
    ov_dirty = 1;
}

/* ---- 마우스 (메뉴·창이 닫혀 있을 때). 처리했으면 1 ---- */
static int ov_vdrag;                                                     /* 4.0.29: 음량 막대를 누른 채 끌기 */
static int ov_mouse(int moved, int press, int dz, int b)
{
    if (!D.fb || !M) return 0;
    if (!(b & 1)) ov_vdrag = 0;
    if (ov_vdrag) {                                                      /* 막대 밖으로 나가도 끝까지 (좌우는 막대 안으로 맞춤) */
        for (int i = 0; i < ob_n; i++) if (ob_hits[i].kind == OB_VOL) {
            if (moved) ob_click(i, ptr_x, 1);
            uint32_t u = P.ms() + OV_MS; ov_until = u ? u : 1;
            break;
        }
        return 1;
    }
    if (moved) {
        if (ptr_y < 3 * S) ov_show_part(3, OV_MS);                       /* 위끝 = 도구 막대 + 미디어 판 */
        else if (ptr_x >= D.w - 3 * S) ov_show_part(2, OV_MS);           /* 오른쪽 끝 = 미디어 판 */
        else if (ov_contains(ptr_x, ptr_y)) { uint32_t u = P.ms() + OV_MS; ov_until = u ? u : 1; }
        int hv = ob_at(ptr_x, ptr_y);
        if (hv != ob_hover) { ob_hover = hv; ov_dirty = 1; }
    }
    if (!(ov_on & 3) || !ov_contains(ptr_x, ptr_y)) return 0;
    int hv = ob_at(ptr_x, ptr_y);
    if (dz && hv >= 0 && (ob_hits[hv].kind == OB_VOL || ob_hits[hv].kind == OB_MIX)) {   /* 음량 위 휠 = ±5 */
        volume += dz > 0 ? 5 : -5; if (volume < 0) volume = 0; if (volume > 100) volume = 100;
        muted = 0; mix_vol[MIX_MASTER] = volume; mix_mute[MIX_MASTER] = 0; settings_changed(); ov_dirty = 1;
    }
    if ((press & 1) && hv >= 0) { if (ob_hits[hv].kind == OB_VOL) ov_vdrag = 1; ob_click(hv, ptr_x, 1); }
    else if ((press & 2) && hv >= 0 && (ob_hits[hv].kind == OB_CARD || ob_hits[hv].kind == OB_MIX)) ob_click(hv, ptr_x, 2);
    return 1;                                                            /* 막대·판 위: MSX로 보내지 않음 */
}
