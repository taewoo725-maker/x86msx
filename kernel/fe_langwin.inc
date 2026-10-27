/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_langwin.inc - 5.2.197 언어 · 글꼴 창 (창 20). 시안: 언어 11개를 한 줄에 3칸씩, 한국어 칸에만 글꼴 풀다운 (내장 점글꼴 + USB fonts 폴더의 .x8f). 고른 글꼴은 모든 언어가 따름 */
typedef struct { const char *code, *name; int lang; } LgRow;
static const LgRow lg_rows[11] = {
    { "KO", "한국어", 0 }, { "EN", "English", 1 }, { "JA", "日本語", 2 }, { "DE", "Deutsch", 3 }, { "FR", "Français", 4 }, { "ES", "Español", 5 },
    { "SV", "Svenska", 6 }, { "RU", "Русский", 7 }, { "PT", "Português", 8 }, { "ET", "Eesti", 9 }, { "AR", "ﺔﻴﺑﺮﻌﻟﺍ", 10 } };
static int lg_sel, lg_nf, lg_open, lg_dd, lg_dummy;
static char lg_fn[12][64];                              /* 글꼴 파일 이름 (.x8f) */
static void langwin_scan(void)
{
    static FeDirEntry fl[24];
    lg_nf = 0;
    int n = P.list_dir ? P.list_dir("0:\\fonts", fl, 24) : 0;
    for (int i = 0; i < n && lg_nf < 12; i++) {
        const char *nm = fl[i].key[0] ? fl[i].key : fl[i].name; int l = (int)strlen(nm);
        if (fl[i].is_dir || l <= 4 || nm[l - 4] != '.' || (nm[l - 3] | 32) != 'x' || nm[l - 2] != '8' || (nm[l - 1] | 32) != 'f') continue;
        copy_str(lg_fn[lg_nf++], sizeof lg_fn[0], nm);
    }
    for (int a = 0; a < lg_nf; a++) for (int b2 = a + 1; b2 < lg_nf; b2++) {       /* 순서: Noto Sans 먼저, 나머지는 이름순 */
        int ka = !strcmp(lg_fn[a], "NotoSansKR.x8f") ? 0 : 1, kb = !strcmp(lg_fn[b2], "NotoSansKR.x8f") ? 0 : 1;
        if (kb < ka || (kb == ka && strcmp(lg_fn[b2], lg_fn[a]) < 0)) { char t[64]; copy_str(t, sizeof t, lg_fn[a]); copy_str(lg_fn[a], sizeof lg_fn[0], lg_fn[b2]); copy_str(lg_fn[b2], sizeof lg_fn[0], t); }
    }
}
static int lg_cur_idx(void) { for (int i = 0; i < lg_nf; i++) if (!strcmp(fx_pref, lg_fn[i])) return i + 1; return 0; }
static void open_langwin(void)
{
    langwin_scan();
    lg_sel = lang; lg_open = 0; lg_dd = 0;
    open_dialog(20);
}
static void langwin_test_open(void) { open_langwin(); lg_open = 1; lg_sel = 0; lg_dd = lg_cur_idx(); }   /* 시험: 풀다운을 연 채로 */
static const char *lg_fname(const char *f)
{
    if (!f[0]) return nt("내장 점글꼴", "Built-in pixel", "内蔵ドット");
    if (!strcmp(f, "NotoSansKR.x8f")) return "Noto Sans";
    if (!strcmp(f, "GothicKR.x8f")) return nt("고딕", "Gothic", "ゴシック");
    if (!strcmp(f, "HappyKR.x8f")) return nt("해피니스 산스", "Happiness Sans", "ハピネスサンス");
    return f;
}
static void langwin_pick_lang(void) { lang = lg_rows[lg_sel].lang; fx_apply_lang(); full_redraw(); settings_changed(); }
static void langwin_pick_font(void)
{
    if (lg_dd > lg_nf) lg_dd = lg_nf;
    copy_str(fx_pref, sizeof fx_pref, lg_dd == 0 ? "" : lg_fn[lg_dd - 1]);
    fx_seen = 1; fx_apply_lang(); lg_open = 0;
    if (fx_name[0] ? strcmp(fx_name, fx_pref) : fx_pref[0]) toast(nt("글꼴 파일을 읽지 못했습니다 (내장 글꼴로 돌아감)", "Could not load the font file (back to built-in)", "フォントを読めません (内蔵に戻ります)"), 0);
    full_redraw(); settings_changed();
}
static void langwin_key(int key)
{
    if (lg_open) {
        if (key == 0x01) lg_open = 0;
        else if (key == 0xC8 && lg_dd > 0) lg_dd--;
        else if (key == 0xD0 && lg_dd < lg_nf) lg_dd++;
        else if (key == 0xC7) lg_dd = 0;
        else if (key == 0xCF) lg_dd = lg_nf;
        else if (key == 0x1C || key == 0x9C || key == 0x39) langwin_pick_font();
        menu_dirty = 1; return;
    }
    if (key == 0x01) { dialog_back(); return; }
    if (key == 0xCB && lg_sel > 0) lg_sel--;
    else if (key == 0xCD && lg_sel < 10) lg_sel++;
    else if (key == 0xC8) lg_sel = lg_sel >= 3 ? lg_sel - 3 : lg_sel;
    else if (key == 0xD0) lg_sel = lg_sel + 3 <= 10 ? lg_sel + 3 : 10;
    else if (key == 0xC7) lg_sel = 0;
    else if (key == 0xCF) lg_sel = 10;
    else if (key == 0x39 && lg_sel == 0) { lg_open = 1; lg_dd = lg_cur_idx(); }
    else if (key == 0x1C || key == 0x9C) { if (lg_sel == 0 && lang == 0) { lg_open = 1; lg_dd = lg_cur_idx(); } else langwin_pick_lang(); }
    menu_dirty = 1;
}
static void draw_langwin(void)
{
    UiRect o;
    UiRect b = ui_full_card(nt("언어 · 글꼴", "Language · Font", "言語・フォント"), nt("화면에 쓰는 말과 글꼴", "Language and font", "画面の言語とフォント"), "X86MSX", &o);
    hit_add(hc_none, 0, 0, 0, 0, o.x, o.y, o.w, o.h);
    uint32_t pad = 14 * S, x = b.x + pad, w = b.w - 2 * pad, cy = dlg_cmd_y(), y = b.y + 10 * S, gap = 8 * S, rh = 32 * S;
    uint32_t cw3 = (w - 2 * gap) / 3, fx0 = 0, fy0 = 0, fw0 = 0, fh0 = 0;
    for (int i = 0; i < 11; i++) {
        uint32_t cx = x + (uint32_t)(i % 3) * (cw3 + gap), ry = y + (uint32_t)(i / 3) * (rh + gap);
        int sel = lg_sel == i && !lg_open, on = lang == lg_rows[i].lang;
        ui_rframe(cx, ry, cw3, rh, 9 * S, sel ? C_ACC : on ? C_OK : C_LINE, sel ? C_ROWSEL : C_CARD);
        uint32_t cs = rh - 10 * S, chw = cs + 6 * S, nx = cx + 8 * S + chw + 8 * S;
        ui_rrect(cx + 8 * S, ry + 5 * S, chw, cs, 6 * S, C_CHIP);
        text_f(SF, 1, cx + 8 * S + (chw - ui_sw(lg_rows[i].code)) / 2, ui_sty(ry + 5 * S, cs), lg_rows[i].code, C_MUTED, 0xFF, 0, 1);
        uint32_t nw = i == 0 ? text_f(F, S, 0, 0, lg_rows[0].name, 0, 0, 0, 0) + 6 * S : cx + cw3 > nx + 14 * S ? cx + cw3 - nx - 14 * S : 0;
        text_clip(nx, ui_ty(ry, rh), lg_rows[i].name, C_TEXT, 0xFF, nw);
        hit_add(hc_sel, (intptr_t)&lg_sel, i, 1, 0x1C, cx, ry, cw3, rh);
        if (on && i) { ui_rrect(cx + cw3 - 14 * S, ry + rh / 2 - 3 * S, 6 * S, 6 * S, 3 * S, C_OK); }
        if (i == 0) {                                   /* 한국어 칸: 글꼴 풀다운 */
            uint32_t px = nx + nw + 8 * S, pw = cx + cw3 > px + 6 * S ? cx + cw3 - px - 6 * S : 0, ph = rh - 10 * S;
            fx0 = px; fy0 = ry + 5 * S; fw0 = pw; fh0 = ph;
            ui_rframe(px, fy0, pw, ph, 6 * S, lg_open ? C_ACC : C_LINE, C_TRACK);
            text_f(SF, 1, px + 8 * S, ui_sty(fy0, ph), lg_fname(fx_pref), C_TEXT, 0xFF, pw > 30 * S ? pw - 30 * S : 0, 1);
            text_f(SF, 1, px + pw - 18 * S, ui_sty(fy0, ph), "▼", C_MUTED, 0xFF, 0, 1);
            hit_add(hc_sel, (intptr_t)&lg_sel, 0, 1, 0x39, px, fy0, pw, ph);
        }
    }
    small_ell(x + 4 * S, y + 4 * (rh + gap) + 2 * S, nt("새 글꼴은 fonts 폴더에 .x8f 로 넣으면 나타납니다 (자유 글꼴만)", "Put new .x8f files in the fonts folder (free fonts only)", "新しいフォントは fonts フォルダに .x8f で入れてください"), C_MUTED, 0xFF, w - 8 * S);
    if (lg_open) {                                      /* 풀다운 목록 (맨 마지막에 그려 위에 덮음) */
        uint32_t ih = SLH + 8 * S, n = (uint32_t)lg_nf + 1, lh = n * ih + 6 * S, ly = fy0 + fh0 + 3 * S; if (fw0 < 150 * S) fw0 = 150 * S;
        hit_add(hc_sel, (intptr_t)&lg_dummy, 0, 1, 0x01, o.x, o.y, o.w, o.h);       /* 밖을 누르면 닫기 */
        ui_rframe(fx0, ly, fw0, lh, 8 * S, C_ACC, C_CARD);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t iy = ly + 3 * S + k * ih;
            const char *nm = lg_fname(k == 0 ? "" : lg_fn[k - 1]);
            int cur = (int)k == lg_cur_idx();
            if ((int)k == lg_dd) ui_rrect(fx0 + 3 * S, iy, fw0 - 6 * S, ih, 6 * S, C_ROWSEL);
            text_f(SF, 1, fx0 + 10 * S, ui_sty(iy, ih), nm, C_TEXT, 0xFF, fw0 > 36 * S ? fw0 - 36 * S : 0, 1);
            if (cur) ui_rrect(fx0 + fw0 - 16 * S, iy + ih / 2 - 3 * S, 6 * S, 6 * S, 3 * S, C_OK);
            hit_add(hc_sel, (intptr_t)&lg_dd, (int)k, 1, 0x1C, fx0 + 3 * S, iy, fw0 - 6 * S, ih);
        }
    }
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, lg_open ? nt("↑/↓ 고르기  Enter 적용  Esc 닫기", "Up/Down choose  Enter apply  Esc close", "↑/↓ 選択  Enter 適用  Esc 閉じる") : nt("←→↑↓ 고르기  Enter 언어  Space 글꼴  Esc 닫기", "Arrows choose  Enter language  Space font  Esc close", "矢印 選択  Enter 言語  Space フォント  Esc 閉じる"));
}
