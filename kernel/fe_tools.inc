/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fe_tools.inc - 2.9 도구: 단축키 편집기, 조이스틱 키 정하기, 머신 편집기.
 * fe.c 안에서 include 한다 (fe.c의 정적 함수·변수를 그대로 쓴다).
 */

/* ================================================================== */
/* 단축키 편집기 (대화 창 8)                                           */
/* ================================================================== */
static int sced_sel, sced_top, sced_cap;
static int sced_ask = -1, sced_ask_vk, sced_ask_mods;   /* 3.2.12: 겹치는 단축키를 물을 때 (그 키를 쓰던 줄 번호) */

/* 메뉴에서 동작 이름 찾기: 하위 메뉴면 "위 메뉴 - 항목" */
static int menu_label_of(const Menu *m, int a, char *out, int max)
{
    for (int i = 0; i < m->n; i++) {
        const MenuItem *it = &m->items[i];
        if (it->label < 0) continue;
        if (it->action == a && !it->sub) { copy_str(out, max, T((StrId)it->label)); return 1; }
        if (it->sub) {
            char t[96];
            if (menu_label_of(it->sub, a, t, sizeof t)) {
                copy_str(out, max, T((StrId)it->label));
                str_append(out, max, " - ");
                str_append(out, max, t);
                return 1;
            }
        }
    }
    return 0;
}

static void action_name(int a, char *out, int max)
{
    static const struct { int a; StrId s; } fixed[] = {
        { A_MENU, S_SC_MENU }, { A_VOL_UP, S_VOL_UP }, { A_VOL_DOWN, S_VOL_DOWN }, { A_MUTE, S_MUTE },
        { A_TURBO_HOLD, S_SC_TURBO },
        { A_SPEED_NORMAL, S_SPEED_NORMAL }, { A_SPEED_UP, S_SPEED_UP }, { A_SPEED_DOWN, S_SPEED_DOWN }, { A_SPEED_MAX, S_SPEED_MAX },
        { A_PANA_FRONT, S_PANA_FRONT }, { A_PANA_PAUSE, S_PANA_PAUSE }, { A_ICONS_TOGGLE, S_ICONS_MENU },
        { A_LANG_MENU, S_LANGUAGE }, { A_SPR_TOGGLE, S_SPR_SHOW }, { A_STEREO, S_STEREO },   /* 3.3.1 */
    };
    if (a == A_MEDIA) { copy_str(out, max, "미디어(마우스)"); return; }   /* 4.0.34: #126 이름 */
    if (a == A_TAPE_POSMENU) { copy_str(out, max, "카셋트 삽입"); return; }   /* 4.0.34: #109 이름 */
    for (unsigned i = 0; i < sizeof fixed / sizeof fixed[0]; i++) if (fixed[i].a == a) { copy_str(out, max, T(fixed[i].s)); return; }
    out[0] = 0;
    for (int b = 0; b < bar_n; b++) {
        char t[96];
        if (menu_label_of(bar_menus[b], a, t, sizeof t)) { copy_str(out, max, t); break; }
    }
    if (!out[0]) { copy_str(out, max, "#"); num_append(out, max, (uint32_t)a, 0, ' '); }
    int n = (int)strlen(out);                                  /* 끝의 "..." 빼기 */
    while (n > 0 && out[n - 1] == '.') out[--n] = 0;
}

static void vk_name(int vk, char *out, int max)
{
    out[0] = 0;
    if (vk >= 0x70 && vk <= 0x7B) { copy_str(out, max, "F"); num_append(out, max, (uint32_t)(vk - 0x6F), 0, ' '); return; }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { char t[2] = { (char)vk, 0 }; copy_str(out, max, t); return; }
    switch (vk) {
    case 0x13: copy_str(out, max, "Pause"); return;
    case 0x03: copy_str(out, max, "Pause"); return;          /* Ctrl+Pause = Break */
    case 0x91: copy_str(out, max, "Scroll Lock"); return;
    case 0x2C: copy_str(out, max, "PrtSc"); return;
    case 0x0D: copy_str(out, max, "Enter"); return;
    case 0x5D: copy_str(out, max, "Menu"); return;
    case 0x26: copy_str(out, max, "↑"); return;
    case 0x28: copy_str(out, max, "↓"); return;
    case 0x25: copy_str(out, max, "←"); return;
    case 0x27: copy_str(out, max, "→"); return;
    case 0x20: copy_str(out, max, "Space"); return;
    case 0x2D: copy_str(out, max, "Ins"); return;
    case 0x2E: copy_str(out, max, "Del"); return;
    case 0x24: copy_str(out, max, "Home"); return;
    case 0x23: copy_str(out, max, "End"); return;
    case 0x21: copy_str(out, max, "PgUp"); return;
    case 0x22: copy_str(out, max, "PgDn"); return;
    case 0xBD: copy_str(out, max, "-"); return;            /* 5.2.80 */
    case 0xBB: copy_str(out, max, "+"); return;
    }
    copy_str(out, max, "VK ");
    num_append(out, max, (uint32_t)vk, 0, ' ');
}

static void combo_name(int vk, int md, char *out, int max)
{
    out[0] = 0;
    if (!vk) { copy_str(out, max, T(S_SC_NONE)); return; }
    static const struct { int bit; const char *n; } mb[] = { { 0x04, "LCtrl" }, { 0x08, "RCtrl" }, { 0x10, "LAlt" }, { 0x20, "RAlt" }, { 0x01, "LShift" }, { 0x02, "RShift" } };
    for (int i = 0; i < 6; i++) if (md & mb[i].bit) { str_append(out, max, mb[i].n); str_append(out, max, "+"); }
    char t[24];
    vk_name(vk, t, sizeof t);
    str_append(out, max, t);
}

static const char *menu_keys_for(int action, char *out, int max)
{
    if (action <= A_NOT_YET) return 0;
    sc_ensure();
    for (int i = 0; i < sc_n; i++)
        if (sc_tab[i].vk && sc_tab[i].action == action) { combo_name(sc_tab[i].vk, sc_tab[i].mods, out, max); return out; }
    return 0;
}

/* 편집기에 보이는 줄 = 아직 없는 기능(A_NOT_YET)을 뺀 단축키 */
/* 4.0.34: 편집기에서 같은 종류끼리 묶어 보임 (저장 순서는 그대로 — 보이는 차례만 바꿈) */
static int sced_cat(int a)
{
    switch (a) {
    case A_RUN_PAUSE: case A_HARD_RESET: case A_SOFT_RESET: case A_FULL_RESET: case A_RESET_CTRL: return 0;
    case A_SPEED_NORMAL: case A_SPEED_UP: case A_SPEED_DOWN: case A_SPEED_MAX: case A_TURBO_HOLD: return 1;
    case A_VOL_UP: case A_VOL_DOWN: case A_MUTE: case A_STEREO: case A_AUDIO_REC: return 2;
    case A_CART1_INSERT: case A_CART2_INSERT: case A_CART1_EJECT: case A_CART2_EJECT: case A_RESET_AFTER_INSERT: return 3;
    case A_DISKA_INSERT: case A_DISKB_INSERT: case A_DISKA_EJECT: case A_DISKB_EJECT: case A_DISKA_NEXT: case A_DISKB_NEXT:
    case A_DISK_RESET_AFTER: case A_DISK_COPY: return 4;
    case A_TAPE_INSERT: case A_TAPE_EJECT: case A_TAPE_REWIND: case A_TAPE_NEW: case A_TAPE_POSMENU: return 5;
    case A_QUICK_LOAD: case A_QUICK_SAVE: case A_STATE_LOAD: case A_STATE_SAVE: return 6;
    case A_SCREENSHOT: case A_VID_REC: case A_ICONS_TOGGLE: case A_SPR_TOGGLE: return 7;
    case A_PRN_OFF: case A_PRN_TEXT: case A_PRN_RAW: case A_PRN_END: case A_PRN_POP0: case A_PRN_POP1: case A_PRN_POP2: case A_PRN_TEAR: case A_PRN_VIEW: return 8;
    case A_MENU: case A_PROPS_EMU: case A_PROPS_MIX: case A_PROPS_KB: case A_PROPS_VIDEO: case A_PROPS_EFFECTS: case A_PROPS_SOUND: case A_PROPS_MIDI:
    case A_DIAG: case A_ABOUT: case A_LANG_MENU: case A_MACHINE_EDIT: case A_SC_EDIT: case A_POP_JOY: case A_MEDIA: return 9;
    case A_EXIT: case A_REBOOT: return 11;
    }
    return 10;
}
/* 편집기에 보이는 줄 = 아직 없는 기능(A_NOT_YET)을 뺀 단축키, 같은 종류끼리 모아서 */
/* 키가 없는 줄은 목록에서 지움 (보기 켜는 키도 없음). 표 자리는 저장된 순서를 지키려고 남기되 편집기에는 안 나옴 */
static int sced_rows(int *idx, int max)
{
    sc_ensure();
    int n = 0;
    for (int c = 0; c <= 11; c++)
        for (int i = 0; i < sc_n && n < max; i++)
            if (sc_def[i].action != A_NOT_YET && sc_def[i].action != A_NONE && sc_tab[i].vk && sced_cat(sc_def[i].action) == c) idx[n++] = i;
    return n;
}

static void draw_sced(void)
{
    UiRect o;
    char title[96] = "";
    copy_str(title, sizeof title, T(S_SHORTCUT_EDITOR));
    int tl = (int)strlen(title); while (tl > 0 && title[tl - 1] == '.') title[--tl] = 0;
    static int idx[SC_MAX];
    int n = sced_rows(idx, SC_MAX);
    if (n && sced_sel >= n) sced_sel = n - 1;
    char chip[24] = ""; num_append(chip, sizeof chip, (uint32_t)n, 0, ' '); str_append(chip, sizeof chip, " keys");
    UiRect b = ui_full_card(title, "blueMSX shortcuts", chip, &o);
    uint32_t pad = 14 * S, X = b.x + pad, W = b.w - 2 * pad, rh = list_rh();
    uint32_t hy = b.y + 10 * S, y0 = hy + SLH + 14 * S, by = dlg_cmd_y() - 8 * S;
    uint32_t vx = X + W * 11 / 20;
    text_f(SF, 1, X + 16 * S, hy, T(S_SC_COL_ACTION), C_MUTED, 0xFF, 0, 1);
    text_f(SF, 1, vx, hy, T(S_SC_COL_KEY), C_MUTED, 0xFF, 0, 1);
    int vis = (int)((by - y0) / rh);
    if (vis < 1) vis = 1;
    if (sced_sel < sced_top) sced_top = sced_sel;
    if (sced_sel >= sced_top + vis) sced_top = sced_sel - vis + 1;
    for (int r = 0; r < vis && sced_top + r < n; r++) {
        int k = sced_top + r, i = idx[k];
        uint32_t y = y0 + (uint32_t)r * rh;
        int sel = k == sced_sel;
        int grp = 0, newgrp = 0;                        /* 종류가 바뀌는 줄: 줄무늬 바꾸고 위에 선 */
        for (int q = 0; q <= k; q++) if (q == 0 || sced_cat(sc_def[idx[q]].action) != sced_cat(sc_def[idx[q - 1]].action)) { grp++; newgrp = (q == k); }
        ui_list_row_bg(X, y, W - 10 * S, rh, sel, grp & 1, 1);
        if (newgrp && k > 0) ui_rrect(X, y, W - 10 * S, 2 * S, 0, C_ACC);
        hit_add(hc_sel, (intptr_t)&sced_sel, k, 0, 0, X, y, W - 10 * S, rh);   /* 3.0 마우스 */
        char a[128], c[64];
        action_name(sc_tab[i].action, a, sizeof a);
        text_clip(X + 16 * S, ui_ty(y, rh), a, sel ? 15 : C_TEXT, 0xFF, vx - X - 28 * S);
        uint32_t ch = SLH + 8 * S, cyy = y + (rh - ch) / 2;
        if (sel && sced_ask >= 0) {                     /* 3.2.12: 이미 쓰는 키 — 바꿀지 묻기 */
            char q[192], an[96], kn[48];
            action_name(sc_tab[sced_ask].action, an, sizeof an);
            combo_name(sced_ask_vk, sced_ask_mods, kn, sizeof kn);
            copy_str(q, sizeof q, T(S_ASK_USED)); str_append(q, sizeof q, " "); str_append(q, sizeof q, kn);
            str_append(q, sizeof q, " = "); str_append(q, sizeof q, an); str_append(q, sizeof q, "   "); str_append(q, sizeof q, T(S_ASK_YESNO));
            ui_rrect(vx, cyy, W - (vx - X) - 20 * S, ch, ch / 2, C_WARN);
            text_f(SF, 1, vx + 8 * S, ui_sty(cyy, ch), q, 1, 0xFF, 0, 1);
        } else if (sel && sced_cap) {
            ui_rrect(vx, cyy, ui_sw(T(S_SC_PRESS)) + 16 * S, ch, ch / 2, C_WARN);
            text_f(SF, 1, vx + 8 * S, ui_sty(cyy, ch), T(S_SC_PRESS), 1, 0xFF, 0, 1);
        } else if (sc_tab[i].vk) {
            combo_name(sc_tab[i].vk, sc_tab[i].mods, c, sizeof c);
            int changed = sc_tab[i].vk != sc_def[i].vk || sc_tab[i].mods != sc_def[i].mods;
            uint32_t cx = vx;                           /* "LCtrl+F7" → 칩 여러 개 */
            const char *p = c;
            while (*p) {
                char part[24]; int q = 0;
                while (*p && *p != '+' && q < (int)sizeof part - 1) part[q++] = *p++;
                part[q] = 0;
                if (*p == '+') p++;
                cx += ui_keychip(cx, cyy, ch, part, sel) + 4 * S;
            }
            if (changed) text_f(SF, 1, cx + 6 * S, ui_sty(cyy, ch), "*", C_WARN, 0xFF, 0, 1);
        } else text_f(SF, 1, vx, ui_sty(cyy, ch), "-", C_FAINT, 0xFF, 0, 1);
    }
    ui_sbar(X + W - 5 * S, y0, (uint32_t)vis * rh, &sced_top, &sced_sel, vis, n);
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, T(S_SC_KEYS));
}

/* 대화 창 8의 키. 잡는 중이면 수식키가 아닌 다음 키를 새 조합으로 */
static void sced_key(int key)
{
    menu_dirty = 1;
    static int idx[SC_MAX];
    int n = sced_rows(idx, SC_MAX);
    if (sced_ask >= 0) {                                        /* 3.2.12: 묻는 중 — Enter·Y 예, Esc·N 아니오 */
        if (mod_bit(key)) return;
        if (key == 0x1C || key == 0x9C || key == 0x15) {
            sc_tab[sced_ask].vk = 0;
            int i = idx[sced_sel];
            sc_tab[i].vk = (uint8_t)sced_ask_vk; sc_tab[i].mods = (uint8_t)sced_ask_mods;
            settings_changed();
        } else if (key != 0x01 && key != 0x31) return;
        sced_ask = -1;
        return;
    }
    if (sced_cap) {
        if (mod_bit(key)) return;                               /* 수식키만 눌린 동안은 기다림 */
        if (key == 0x01 && !mods) { sced_cap = 0; return; }
        int vk = key_vk(key);
        if (!vk) return;
        int i = idx[sced_sel];
        for (int j = 0; j < sc_n; j++)                          /* 3.2.12: 같은 조합을 쓰는 다른 기능이 있으면 묻기 (빈 자리·아직 없는 기능은 그냥 지움) */
            if (j != i && sc_tab[j].vk == vk && sc_tab[j].mods == mods) {
                if (sc_tab[j].action == A_NOT_YET || sc_tab[j].action == A_NONE) { sc_tab[j].vk = 0; continue; }
                sced_ask = j; sced_ask_vk = vk; sced_ask_mods = mods; sced_cap = 0;
                return;
            }
        sc_tab[i].vk = (uint8_t)vk; sc_tab[i].mods = mods;
        sced_cap = 0;
        settings_changed();
        return;
    }
    switch (key) {
    case 0x01: dialog_back(); return;
    case 0xC8: if (sced_sel > 0) sced_sel--; break;
    case 0xD0: if (sced_sel < n - 1) sced_sel++; break;
    case 0xC9: sced_sel = sced_sel > 10 ? sced_sel - 10 : 0; break;
    case 0xD1: sced_sel = sced_sel + 10 < n ? sced_sel + 10 : n - 1; break;
    case 0x1C: case 0x9C: if (n) sced_cap = 1; break;
    case 0xD3: if (n) { sc_tab[idx[sced_sel]].vk = 0; settings_changed(); if (sced_sel >= n - 1 && sced_sel > 0) sced_sel--; } break;    /* Del (키를 지우면 그 줄도 목록에서 사라짐) */
    case 0x3F: sc_reset(); settings_changed(); toast(T(S_SC_DEFAULTS), 0); break;       /* F5 */
    default: break;
    }
}

/* 설정 파일: 표 순서대로 vk·mods를 16진 4글자씩.
 * 3.2.9: 키 이름을 Shortcuts2로 바꿈 — 3.2.4~3.2.8의 Shortcuts 읽기가 마우스 잡기 자리를 잘못 건너뛰어(저장한 줄에는 없는데도)
 * 끝쪽 단축키(테이프 되감기·F6 아이콘·RCtrl+F12 …)가 끌 때마다 한 칸씩 밀렸음. 옛 Shortcuts는 밀리지 않은 앞부분만 읽음 */
static void sc_save(char *b)
{
    sc_ensure();
    char t[SC_MAX * 4 + 1];
    int k = 0;
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < sc_n; i++) {
        t[k++] = hx[sc_tab[i].vk >> 4]; t[k++] = hx[sc_tab[i].vk & 15];
        t[k++] = hx[sc_tab[i].mods >> 4]; t[k++] = hx[sc_tab[i].mods & 15];
    }
    t[k] = 0;
    ini_put(b, "Shortcuts2", t);
}
static int hexd(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
static void sc_migrate(void)                               /* 3.7.2: 음량 = LCtrl+↑/↓ (옛 RAlt+F8/F7에서 옮김), 옛 2% 자리 비움 */
{
    for (int i = 0; i < sc_n; i++) {
        int vk = sc_tab[i].vk, md = sc_tab[i].mods;
        int old = md == 0x20 && (vk == 0x76 || vk == 0x77 || vk == 0x26 || vk == 0x28);   /* LCtrl+↑/↓는 더 이상 옛 키 아님 */
        if (sc_def[i].action == A_VOL_UP && old) { sc_tab[i].vk = 0x26; sc_tab[i].mods = 0x10; }
        if (sc_def[i].action == A_VOL_DOWN && old) { sc_tab[i].vk = 0x28; sc_tab[i].mods = 0x10; }
        if (sc_def[i].action == A_NONE) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; }
    }
    for (int i = 0; i < sc_n; i++)                         /* LCtrl+↑/↓는 음량만 (다른 동작에 들어간 것은 지움) */
        if (sc_def[i].action != A_VOL_UP && sc_def[i].action != A_VOL_DOWN && sc_tab[i].mods == 0x10 && (sc_tab[i].vk == 0x26 || sc_tab[i].vk == 0x28)) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; }
}
/* 3.7.2: 저장된 단축키가 무엇이든(다른 키로 바뀌어 있어도) 한 번은 음량 올리기 = LCtrl+↑, 내리기 = LCtrl+↓로. 그 뒤 사용자가 바꾼 것은 둠 */
static int sc_ver;
static void sc_force_vol(void)
{
    if (sc_ver >= 373) return;                          /* 373 = LCtrl+↑/↓로 맞춘 뒤 */
    sc_ensure();
    for (int i = 0; i < sc_n; i++) {
        if (sc_def[i].action == A_VOL_UP) { sc_tab[i].vk = 0x26; sc_tab[i].mods = 0x10; }
        else if (sc_def[i].action == A_VOL_DOWN) { sc_tab[i].vk = 0x28; sc_tab[i].mods = 0x10; }
        else if ((sc_tab[i].mods == 0x10 || sc_tab[i].mods == 0x20) && (sc_tab[i].vk == 0x26 || sc_tab[i].vk == 0x28)) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; }
    }
    sc_ver = 373;
    settings_changed();
}
/* 4.0.34: 저장된 단축키가 있어도 한 번은 새 기본값으로 (Pause 실행/정지, F2 소프트 리셋, LCtrl+F12 하드 리셋, LShift+F9 끝내기,
 * LCtrl+F9 PC 재부팅, F9 파일 관리자, F10 키보드, F12 조이스틱, LShift+F6 .prn). 설정 창·에 대하여·언어·머신 편집기·단축키 편집기 키는 없앰.
 * 그 뒤 사용자가 바꾼 것은 둠. 새 키를 쓰던 다른 줄은 비움 */
static void sc_move_4034(void)
{
    if (sc_ver >= 4041) return;
    sc_ensure();
    const int stage = sc_ver >= 4040 ? 7 : sc_ver >= 4039 ? 6 : sc_ver >= 4038 ? 5 : sc_ver >= 4037 ? 4 : sc_ver >= 4036 ? 3 : sc_ver >= 4035 ? 2 : sc_ver >= 4034 ? 1 : 0;   /* 이미 옮긴 단계: 1이면 하드·소프트 리셋·끝내기·다음 디스크만, 2면 끝내기·다음 디스크만 다시 */
    static const int acts[] = { A_RUN_PAUSE, A_PANA_PAUSE, A_HARD_RESET, A_SOFT_RESET, A_FULL_RESET, A_MUTE, A_TURBO_HOLD, A_EXIT, A_REBOOT, A_DISKA_INSERT, A_DISKA_NEXT, A_ABOUT,
        A_PROPS_EMU, A_PROPS_VIDEO, A_PROPS_SOUND, A_PROPS_EFFECTS, A_PROPS_MIDI, A_PROPS_KB, A_LANG_MENU, A_MACHINE_EDIT, A_SC_EDIT, A_POP_JOY, A_DISK_COPY, A_PRN_RAW };
    uint8_t moved[SC_MAX];
    for (int i = 0; i < sc_n; i++) {
        moved[i] = 0;
        for (unsigned k = 0; k < sizeof acts / sizeof acts[0]; k++) if (sc_def[i].action == acts[k] && (!stage || sc_def[i].action == A_MUTE || (stage < 7 && sc_def[i].action == A_FULL_RESET) || (stage < 6 && (sc_def[i].action == A_SOFT_RESET || sc_def[i].action == A_HARD_RESET)) || (stage < 4 && (sc_def[i].action == A_EXIT || sc_def[i].action == A_DISKA_NEXT || sc_def[i].action == A_HARD_RESET)))) { sc_tab[i] = sc_def[i]; moved[i] = 1; }
    }
    for (int i = 0; i < sc_n; i++) {
        if (moved[i] || !sc_tab[i].vk) continue;
        for (int j = 0; j < sc_n; j++) if (moved[j] && sc_tab[j].vk == sc_tab[i].vk && sc_tab[j].mods == sc_tab[i].mods) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; break; }
    }
    sc_ver = 4041;
    settings_changed();
}
/* 4.0.34 (2차): 믹서 F12, 입력장치 F5, 스테레오 RAlt+F12, 프린터 원본 .prn F10. 특수 카트리지 키(LCtrl+LAlt+F7/F8)·조이스틱 팝업(F12)은 없앰.
 * 한 번만 옮기고, 새 키를 쓰던 다른 줄은 비움 */
static void sc_move_4035(void)
{
    if (sc_ver >= 4051) return;
    sc_ensure();
    static const int acts[] = { A_PRN_END, A_PROPS_MIX, A_PROPS_KB, A_STEREO, A_PRN_RAW };
    const unsigned k0 = sc_ver >= 4050 ? 0 : 0, kn = sc_ver >= 4050 ? 1 : 5;   /* 4050을 이미 거쳤으면 인쇄 끝내기만 */
    uint8_t moved[SC_MAX];
    for (int i = 0; i < sc_n; i++) {
        moved[i] = 0;
        for (unsigned k = k0; k < kn; k++) if (sc_def[i].action == acts[k]) { sc_tab[i].vk = sc_def[i].vk; sc_tab[i].mods = sc_def[i].mods; moved[i] = 1; }
    }
    for (int i = 0; i < sc_n; i++) {
        if (moved[i] || !sc_tab[i].vk) continue;
        for (int j = 0; j < sc_n; j++) if (moved[j] && sc_tab[j].vk == sc_tab[i].vk && sc_tab[j].mods == sc_tab[i].mods) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; break; }
    }
    for (int i = 0; i < sc_n; i++) if (sc_def[i].action == A_NONE) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; }
    sc_ver = 4051;
    settings_changed();
}
/* 마스터님이 없애라 하신 단축키 15개를 저장된 표에서도 한 번 지움 (어느 동작에 걸려 있든 그 키 조합이면). 그 뒤 사용자가 다시 정한 것은 둠.
 * F5, LCtrl+F6, LCtrl+F9, LAlt+F9, LCtrl+LShift+F8, LCtrl+LAlt+F9, LAlt+F12, LCtrl+LAlt+F10, LAlt+F7, LAlt+F8, Scroll Lock, RAlt+Pause, RCtrl+Pause, RCtrl+RShift+M, LCtrl+LAlt+M */
static void sc_remove_5277(void)
{
    if (sc_ver >= 5277) return;
    sc_ensure();
    static const uint8_t rm[][2] = {
        { 0x74, 0x00 }, { 0x75, 0x04 }, { 0x78, 0x04 }, { 0x78, 0x10 }, { 0x77, 0x05 }, { 0x78, 0x14 }, { 0x7B, 0x10 }, { 0x79, 0x14 },
        { 0x76, 0x10 }, { 0x77, 0x10 }, { 0x91, 0x00 }, { 0x13, 0x20 }, { 0x03, 0x20 }, { 0x13, 0x08 }, { 0x03, 0x08 }, { 'M', 0x0A }, { 'M', 0x14 } };
    for (int i = 0; i < sc_n; i++)
        for (unsigned k = 0; k < sizeof rm / sizeof rm[0]; k++)
            if (sc_tab[i].vk == rm[k][0] && sc_tab[i].mods == rm[k][1]) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; }
    sc_ver = 5277;
    settings_changed();
}
/* 마스터님 새 단축키 배치. 목록에 있는 기능만 새 키로 바꾸고, 목록에 없는 기능은 사용자가 정해 둔 키를 그대로 둠
   (다만 새 키와 같은 조합을 쓰던 다른 기능은 겹치지 않게 그 키만 지움) */
static void sc_apply_list(const uint8_t *act, unsigned na);
static void sc_apply_5280(void)
{
    if (sc_ver >= 5280) return;
    static const uint8_t act[] = {
        A_RUN_PAUSE, A_TURBO_HOLD, A_SPEED_DOWN, A_SPEED_UP, A_SPEED_MAX, A_SPR_TOGGLE, A_AUDIO_REC, A_QUICK_LOAD, A_QUICK_SAVE,
        A_PRN_RAW, A_PRN_END, A_SOFT_RESET, A_HARD_RESET, A_FULL_RESET, A_RESET_CTRL,
        A_CART1_INSERT, A_CART1_EJECT, A_CART2_INSERT, A_CART2_EJECT,
        A_DISKA_INSERT, A_DISKA_EJECT, A_DISKA_NEXT, A_DISKB_INSERT, A_DISKB_EJECT, A_DISKB_NEXT,
        A_TAPE_INSERT, A_TAPE_EJECT, A_TAPE_NEW, A_TAPE_REWIND,
        A_MENU, A_DISK_COPY, A_ICONS_TOGGLE, A_PROPS_MIX, A_MIDI_MIX, A_STEREO };
    sc_apply_list(act, sizeof act);
    sc_ver = 5280;
}
/* 목록의 기능만 기본 키로 맞추고, 그 키와 겹치던 다른 기능의 키만 지움 (다른 기능의 사용자 키는 그대로) */
static void sc_apply_list(const uint8_t *act, unsigned na)
{
    sc_ensure();
    uint8_t mine[SC_MAX] = { 0 };
    for (int i = 0; i < sc_n; i++)
        for (unsigned k = 0; k < na; k++)
            if (sc_def[i].action == act[k]) { sc_tab[i] = sc_def[i]; mine[i] = 1; break; }
    for (int i = 0; i < sc_n; i++) {
        if (mine[i] || !sc_tab[i].vk) continue;
        for (int j = 0; j < sc_n; j++)
            if (mine[j] && sc_tab[j].vk == sc_tab[i].vk && sc_tab[j].mods == sc_tab[i].mods) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; break; }
    }
    settings_changed();
}
/* 소리 저장 RCtrl+F6, 스프라이트 RCtrl+F7, 상태 불러오기 RCtrl+F8, 저장 RCtrl+F9, .prn RAlt+F6, 인쇄 끝내기 RAlt+F7.
   카세트 삽입(옛 RCtrl+F9)은 키 없음. RCtrl+RShift+D(미디어 팝업) 없앰 */
static void sc_apply_5281(void)
{
    if (sc_ver >= 5281) return;
    static const uint8_t act[] = { A_AUDIO_REC, A_SPR_TOGGLE, A_QUICK_LOAD, A_QUICK_SAVE, A_PRN_RAW, A_PRN_END, A_TAPE_INSERT, A_MEDIA };
    sc_apply_list(act, sizeof act);
    sc_ver = 5281;
}
/* 음량 올리기·내리기 = LAlt+↑/↓, 속도 느리게·빠르게 = LAlt+-/+ (옛 LCtrl). 최대 속도 켜기/끄기(LCtrl+LShift++)는 그대로 */
static void sc_apply_5282(void)
{
    if (sc_ver >= 5282) return;
    static const uint8_t act[] = { A_VOL_UP, A_VOL_DOWN, A_SPEED_DOWN, A_SPEED_UP };
    sc_apply_list(act, sizeof act);
    sc_ver = 5282;
}
static int sc_load_n(const char *v, int limit)
{
    int len = (int)strlen(v), cnt = len / 4;
    if (len % 4) return -1;
    for (int i = 0; i < len; i++) if (hexd(v[i]) < 0) return -1;
    if (cnt > limit) cnt = limit;                          /* 표 끝에 새 단축키가 붙었으면 앞부분만, 표가 줄었으면 남는 것은 버림 */
    for (int i = 0; i < cnt; i++) {
        sc_tab[i].vk = (uint8_t)(hexd(v[i * 4]) << 4 | hexd(v[i * 4 + 1]));
        sc_tab[i].mods = (uint8_t)(hexd(v[i * 4 + 2]) << 4 | hexd(v[i * 4 + 3]));
    }
    sc_migrate();
    return 0;
}
static int sc_new_seen;
static void sc_load(const char *v)                         /* Shortcuts2 */
{
    sc_ensure();
    sc_reset();
    sc_new_seen = 1;
    sc_load_n(v, sc_n);
}
static void sc_load_old(const char *v)                     /* 3.2.8까지의 Shortcuts: 3.2.x에서 붙은 자리(A_TAPE_REWIND부터)는 기본값 */
{
    sc_ensure();
    if (sc_new_seen) return;
    int keep = 0;
    while (keep < sc_n && sc_def[keep].action != A_TAPE_REWIND) keep++;
    sc_load_n(v, keep);
}

/* 3.3.1 KBD-03: 단축키 파일 Keyboard Config\X86MSX.shortcuts (사람이 읽고 고칠 수 있는 글).
 * 한 줄에 "SC번호=키 조합   ; 동작 이름" (예: SC17=LCtrl+F7). 빈 값 = 지움. 켤 때 있으면 ini보다 먼저 이것을 따르고, 바뀌면 ini와 함께 다시 씀 */
#define SC_FILE "0:\\Keyboard Config\\X86MSX.shortcuts"
static uint32_t sc_file_sum;
static int sc_file_text(char *t, int max)
{
    sc_ensure();
    t[0] = 0;
    str_append(t, max, "; X86MSX shortcuts. SCnn=Key (LCtrl RCtrl LAlt RAlt LShift RShift + F1..F12, A..Z, 0..9, Pause, PrtSc, Enter, Space, ...). Empty = none\r\n");
    for (int i = 0; i < sc_n; i++) {
        if (sc_def[i].action == A_NONE || sc_def[i].action == A_NOT_YET) continue;
        char c[48], an[96];
        str_append(t, max, "SC"); num_append(t, max, (uint32_t)i, 2, '0'); str_append(t, max, "=");
        if (sc_tab[i].vk) { combo_name(sc_tab[i].vk, sc_tab[i].mods, c, sizeof c); str_append(t, max, c); }
        action_name(sc_def[i].action, an, sizeof an);
        str_append(t, max, "   ; "); str_append(t, max, an); str_append(t, max, "\r\n");
    }
    return (int)strlen(t);
}
static uint32_t sc_sum(const char *t) { uint32_t h = 2166136261u; while (*t) h = (h ^ (uint8_t)*t++) * 16777619u; return h; }
static void sc_file_save(void)
{
    static char t[SC_MAX * 160];
    int n = sc_file_text(t, sizeof t);
    uint32_t h = sc_sum(t);
    if (h == sc_file_sum || !P.create || !P.write_file) return;
    P.create("0:\\Keyboard Config", 1);
    if (!P.create(SC_FILE, 0) && !P.write_file(SC_FILE, (const uint8_t *)t, (uint32_t)n)) sc_file_sum = h;
}
/* "LCtrl+LShift+F7" → vk, mods. 실패 -1 */
static int sc_parse_combo(const char *s, int *vk, int *md)
{
    static const struct { int bit; const char *n; } mb[] = { { 0x04, "LCtrl" }, { 0x08, "RCtrl" }, { 0x10, "LAlt" }, { 0x20, "RAlt" }, { 0x01, "LShift" }, { 0x02, "RShift" } };
    *md = 0; *vk = 0;
    for (;;) {
        int l = 0;
        while (s[l] && s[l] != '+') l++;
        if (!s[l] || !s[l + 1]) break;                  /* 마지막 조각 = 키 ("+" 자체는 키로 쓰지 않음) */
        int f = 0;
        for (int i = 0; i < 6; i++) if ((int)strlen(mb[i].n) == l && !memcmp(s, mb[i].n, (size_t)l)) { *md |= mb[i].bit; f = 1; }
        if (!f) return -1;
        s += l + 1;
    }
    for (int v = 1; v < 256; v++) {
        char t[24];
        vk_name(v, t, sizeof t);
        if (!strcmp(t, s) && strncmp(t, "VK ", 3)) { *vk = v; return 0; }
    }
    return -1;
}
static void sc_file_load(void)
{
    static char t[SC_MAX * 160];
    uint32_t n = 0;
    if (!P.read_file || P.read_file(SC_FILE, (uint8_t *)t, sizeof t - 1, &n) || !n) return;
    t[n < sizeof t ? n : sizeof t - 1] = 0;
    sc_ensure();
    int got = 0;
    for (char *line = t; *line; ) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char save = *end; *end = 0;
        if (line[0] == 'S' && line[1] == 'C' && line[2] >= '0' && line[2] <= '9') {
            int i = 0; char *q = line + 2;
            while (*q >= '0' && *q <= '9') i = i * 10 + (*q++ - '0');
            if (*q == '=' && i < sc_n && sc_def[i].action != A_NONE && sc_def[i].action != A_NOT_YET) {
                q++;
                char v[48]; int k = 0;
                while (*q && *q != ';' && *q != '\r' && k < 47) v[k++] = *q++;
                while (k > 0 && v[k - 1] == ' ') k--;
                v[k] = 0;
                int vk, md;
                if (!v[0]) { sc_tab[i].vk = 0; sc_tab[i].mods = 0; got++; }
                else if (!sc_parse_combo(v, &vk, &md)) { sc_tab[i].vk = (uint8_t)vk; sc_tab[i].mods = (uint8_t)md; got++; }
            }
        }
        *end = save;
        line = *end ? end + 1 : end;
    }
    sc_migrate();
    static char tt[SC_MAX * 160];
    sc_file_text(tt, sizeof tt);
    sc_file_sum = sc_sum(tt);                           /* 읽은 그대로면 다시 쓰지 않음 */
    (void)got;
}

/* ================================================================== */
/* 조이스틱 키 (속성 창 탭 7)                                          */
/* ================================================================== */
static void joy_save(char *b)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int p = 0; p < 2; p++) {
        char t[16];
        for (int k = 0; k < 6; k++) { t[k * 2] = hx[joy_keys[p][k] >> 4]; t[k * 2 + 1] = hx[joy_keys[p][k] & 15]; }
        t[12] = 0;
        ini_put(b, p ? "Joy2Keys" : "Joy1Keys", t);
    }
}
static void joy_load(int p, const char *v)
{
    if (strlen(v) != 12) return;
    for (int i = 0; i < 12; i++) if (hexd(v[i]) < 0) return;
    for (int k = 0; k < 6; k++) joy_keys[p][k] = (uint8_t)(hexd(v[k * 2]) << 4 | hexd(v[k * 2 + 1]));
}

/* 스캔 코드 이름 (조이스틱 키 표시) */
static void scan_name(int key, char *out, int max)
{
    out[0] = 0;
    if (key < 0x40 && sc_ascii[key]) { char t[2] = { sc_ascii[key], 0 }; if (t[0] >= 'a') t[0] = (char)(t[0] - 32); copy_str(out, max, t); return; }
    switch (key) {
    case 0x39: copy_str(out, max, "Space"); return;
    case 0x1C: copy_str(out, max, "Enter"); return;
    case 0x0F: copy_str(out, max, "Tab"); return;
    case 0x0E: copy_str(out, max, "Backspace"); return;
    case 0x2A: copy_str(out, max, "LShift"); return;
    case 0x36: copy_str(out, max, "RShift"); return;
    case 0x1D: copy_str(out, max, "LCtrl"); return;
    case 0x9D: copy_str(out, max, "RCtrl"); return;
    case 0x38: copy_str(out, max, "LAlt"); return;
    case 0xB8: copy_str(out, max, "RAlt"); return;
    case 0xC8: copy_str(out, max, "↑"); return;
    case 0xD0: copy_str(out, max, "↓"); return;
    case 0xCB: copy_str(out, max, "←"); return;
    case 0xCD: copy_str(out, max, "→"); return;
    case 0xD2: copy_str(out, max, "Ins"); return;
    case 0xD3: copy_str(out, max, "Del"); return;
    case 0xC7: copy_str(out, max, "Home"); return;
    case 0xCF: copy_str(out, max, "End"); return;
    case 0xC9: copy_str(out, max, "PgUp"); return;
    case 0xD1: copy_str(out, max, "PgDn"); return;
    case 0x0C: copy_str(out, max, "-"); return;
    case 0x0D: copy_str(out, max, "="); return;
    case 0x1A: copy_str(out, max, "["); return;
    case 0x1B: copy_str(out, max, "]"); return;
    case 0x27: copy_str(out, max, ";"); return;
    case 0x28: copy_str(out, max, "'"); return;
    case 0x33: copy_str(out, max, ","); return;
    case 0x34: copy_str(out, max, "."); return;
    case 0x35: copy_str(out, max, "/"); return;
    case 0x01: copy_str(out, max, "Esc"); return;       /* 3.7.0: 키 편집기에 나오는 키 더 */
    case 0x29: copy_str(out, max, "`"); return;
    case 0x2B: copy_str(out, max, "\\"); return;
    case 0x3A: copy_str(out, max, "Caps Lock"); return;
    case 0x45: copy_str(out, max, "Num Lock"); return;
    case 0x46: copy_str(out, max, "Scroll Lock"); return;
    case 0x37: copy_str(out, max, "Num *"); return;
    case 0xB5: copy_str(out, max, "Num /"); return;
    case 0x9C: copy_str(out, max, "Num Enter"); return;
    case 0x56: copy_str(out, max, "\\ (102)"); return;
    case 0x57: copy_str(out, max, "F11"); return;
    case 0x58: copy_str(out, max, "F12"); return;
    case 0x70: copy_str(out, max, "かな"); return;
    case 0x73: copy_str(out, max, "\\ (JP)"); return;
    case 0x79: copy_str(out, max, "変換"); return;
    case 0x7B: copy_str(out, max, "無変換"); return;
    case 0x7D: copy_str(out, max, "¥"); return;
    case 0xDB: copy_str(out, max, "LWin"); return;
    case 0xDC: copy_str(out, max, "RWin"); return;
    case 0xDD: copy_str(out, max, "Menu"); return;
    }
    if (key >= 0x3B && key <= 0x44) { char t[8] = "F"; num_append(t, sizeof t, (uint32_t)(key - 0x3A), 0, ' '); copy_str(out, max, t); return; }
    if (key >= 0x47 && key <= 0x53) { static const char kp[] = "789-456+1230."; char t[8] = "Num "; t[4] = kp[key - 0x47]; t[5] = 0; copy_str(out, max, t); return; }
    copy_str(out, max, "Key ");
    num_append(out, max, (uint32_t)key, 0, ' ');
}

/* ================================================================== */
/* 머신 편집기 (대화 창 9)                                             */
/* ================================================================== */
#include "fe_medit.inc"
