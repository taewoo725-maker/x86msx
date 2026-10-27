/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_medit_ui.inc - 머신 편집기 화면 (2.16, X86MSX 공통 "키보드 카드" 디자인 — fe_ui.inc)
 *
 * blueMSX 머신 설정 편집기의 기능을 모두 담되, 모양과 조작은 다른 창(속성·파일 선택 등)과 같다:
 *   카드 머리: 제목 + 머신 이름(바뀌면 ●) + 오른쪽 칩(실행 중 / 세대)
 *   탭 [1] 슬롯 배치 [2] 메모리 [3] 칩 에뮬레이션 [4] 기타  (숫자 키, PgUp/PgDn, Ctrl+Tab)
 *   줄: 이름 ‹ 값 › — ←/→ 바로 바꾸기, Enter = 고를 수 있는 값 목록(글자 키로 이동), 둘 중 하나는 나뉜 단추, 켜기/끄기는 스위치
 *   메모리: 표 + Ins 추가 / Enter 편집 / Del 삭제 → "매퍼" 카드(파일·형식·슬롯·주소·크기), 파일은 "ROM 파일 열기" 카드
 *   아래 명령 줄: F2 저장  F3 머신  F4 다른 이름으로  F5 실행  Esc 닫기 (단추 대신 키)
 * 층: 기본 카드 → 매퍼 카드(me_det) → 작은 카드(me_pop: 이름·문제·확인·파일 열기) → 값 목록(me_drop) */

/* 줄 종류 */
enum { MW_SECTION, MW_ROW, MW_LIST, MW_MAP, MW_NOTE };
typedef struct {
    uint8_t kind, en, on, uv;           /* uv = UV_CHOICE / UV_SEG2 / UV_TOGGLE / UV_ACTION */
    int16_t id;
    uint32_t x, y, w, h;
    const char *text;
} MeW;
#define MW_MAX 48
static MeW mw_m[MW_MAX], mw_d[MW_MAX], mw_p[MW_MAX];
static int nmw_m, nmw_d, nmw_p;
static UiRect me_card, me_body;                        /* 기본 카드 */
static UiRect me_dcard, me_dbody;                      /* 매퍼 카드 */
static UiRect me_pcard, me_pbody;                      /* 작은 카드 */

static MeW *mw_add(MeW *a, int *n, int kind, int id, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *text, int en, int on, int uv)
{
    if (*n >= MW_MAX) return &a[MW_MAX - 1];
    MeW *q = &a[(*n)++];
    q->kind = (uint8_t)kind; q->id = (int16_t)id; q->x = x; q->y = y; q->w = w; q->h = h; q->text = text;
    q->en = (uint8_t)en; q->on = (uint8_t)on; q->uv = (uint8_t)uv;
    return q;
}
static int mw_focusable(const MeW *q) { return q->id > 0 && q->en && (q->kind == MW_ROW || q->kind == MW_LIST); }
static MeW *mw_find(MeW *a, int n, int id) { for (int i = 0; i < n; i++) if (a[i].id == id) return &a[i]; return 0; }

/* ================================================================== */
/* 콤보 값과 값 목록                                                    */
/* ================================================================== */
#define ME_OPT_MAX 400
static char me_opt[ME_OPT_MAX][72];
static int me_optv[ME_OPT_MAX];
static uint8_t me_optg[ME_OPT_MAX];                    /* 회색 (X86MSX 미지원 형식) */
static int me_nopt;

static void opt_add(const char *t, int v, int grey)
{
    if (me_nopt >= ME_OPT_MAX) return;
    copy_str(me_opt[me_nopt], sizeof me_opt[0], t);
    me_optv[me_nopt] = v; me_optg[me_nopt] = (uint8_t)grey;
    me_nopt++;
}

/* 머신 목록 (USB\Machines의 MSX… 폴더, 이름 순) */
static void me_opts_machines(void)
{
    int n = P.list_dir ? P.list_dir("0:\\Machines", entries, DIR_MAX) : -1;
    for (int i = 0; i < n; i++) {
        const char *nm = entries[i].key[0] ? entries[i].key : entries[i].name;
        if (!entries[i].is_dir || strncmp(nm, "MSX", 3) || me_nopt >= ME_OPT_MAX) continue;
        int j = me_nopt++;
        while (j > 0 && name_cmp(me_opt[j - 1], nm) > 0) { memcpy(me_opt[j], me_opt[j - 1], sizeof me_opt[0]); me_optv[j] = j; me_optg[j] = 0; j--; }
        copy_str(me_opt[j], sizeof me_opt[0], nm); me_optv[j] = j; me_optg[j] = 0;
    }
}

/* id의 고를 수 있는 값들을 me_opt에 채우고 지금 값의 번호를 돌려줌 */
static int me_opts(int id)
{
    me_nopt = 0;
    int cur = 0;
    char t[80];
    switch (id) {
    case 1:
        me_opts_machines();
        for (int i = 0; i < me_nopt; i++) if (!strcmp(me_opt[i], me_src)) cur = i;
        break;
    case 30:
        for (int i = 0; i < 6; i++) { opt_add(me_board_names[i], i, 0); if (!strcmp(me_boards[i], me.board[0] ? me.board : "MSX")) cur = i; }
        break;
    case 14: case 15: case 16: case 17: case 18: case 103: {
        int ps[16], ss[16], n = me_slot_opts(ps, ss), base = 0;
        int wp = id == 103 ? me_ed.ps : me.ext_ps[id - 14], ws = id == 103 ? me_ed.ss : me.ext_ss[id - 14];
        if (id >= 16 && id <= 18) { opt_add(T(S_NONE), -1, 0); base = 1; if (wp < 0) cur = 0; }   /* 3.3.1: C·D는 없음 가능 */
        for (int i = 0; i < n; i++) {
            me_slot_text(ps[i], ss[i], t, sizeof t, 1);
            opt_add(t, ps[i] * 4 + ss[i], 0);
            if (ps[i] == wp && ss[i] == (me.subslotted[ps[i]] ? ws : 0)) cur = i + base;
        }
        break;
    }
    case 20: for (int i = 0; i < 4; i++) { opt_add(me_vdps[i], i, 0); if (!strcmp(me_vdps[i], me.vdp)) cur = i; } break;
    case 21:
        for (int i = 0; i < 4; i++) { t[0] = 0; num_append(t, sizeof t, (uint32_t)me_vrams[i], 0, ' '); str_append(t, sizeof t, " kB"); opt_add(t, me_vrams[i], 0); if (me_vrams[i] == me.vram) cur = i; }
        break;
    case 5: case 9: {
        uint32_t f = id == 5 ? me.z80hz : (me.r800hz ? me.r800hz : 7159090);
        cur = -1;
        for (int i = 0; i < 10; i++) { me_freq_text(me_freqs[i], t, sizeof t); opt_add(t, i, 0); if (me_freqs[i] == f) cur = i; }
        if (cur < 0) cur = 4;
        break;
    }
    case 6:
        for (int i = 0; i < 3; i++) { t[0] = 0; num_append(t, sizeof t, (uint32_t)i, 0, ' '); opt_add(t, i, 0); }
        cur = me.fdc < 0 ? 2 : me.fdc;
        break;
    case 102:
        for (int i = 0; i < me_type_count(); i++) {
            const MeTypeInfo *ti = me_type_at(i);
            copy_str(t, sizeof t, ti->name);
            if (!me_type_supported(ti->t)) { str_append(t, sizeof t, "  "); str_append(t, sizeof t, T(S_ME_UNSUP)); }
            opt_add(t, ti->t, !me_type_supported(ti->t));
            if (ti->t == me_ed.type) cur = i;
        }
        break;
    case 104: {
        int w = me_window(me_ed.type, me_ed.count);
        if (!w || w == 8) { me_range_text(0, w ? 8 : 0, t, sizeof t); if (!w) t[0] = 0; opt_add(t, 0, 0); break; }
        for (int s = 0; s + w <= 8; s++) { me_range_text(s, w, t, sizeof t); opt_add(t, s, 0); if (s == me_ed.start) cur = me_nopt - 1; }
        break;
    }
    case 105: {
        int sizes[8], n = me_size_options(me_ed.type, sizes, 8);
        for (int i = 0; i < n; i++) { me_size_text(sizes[i], t, sizeof t); opt_add(t, sizes[i], 0); if (sizes[i] == me_ed.count) cur = i; }
        break;
    }
    }
    return cur;
}

/* 펼치지 않은 콤보에 보이는 값 (me_opt를 건드리지 않음: 펼친 목록이 그대로 남게) */
static void me_combo_val(int id, char *out, int max)
{
    out[0] = 0;
    switch (id) {
    case 1: copy_str(out, max, me.name); break;
    case 30: {
        const char *bn = me.board[0] ? me.board : "MSX";
        for (int i = 0; i < 6; i++) if (!strcmp(me_boards[i], bn)) bn = me_board_names[i];
        copy_str(out, max, bn);
        break;
    }
    case 14: case 15: case 16: case 17: case 18:
        if (me.ext_ps[id - 14] < 0) { copy_str(out, max, T(S_NONE)); break; }
        me_slot_text(me.ext_ps[id - 14], me.subslotted[me.ext_ps[id - 14] & 3] ? me.ext_ss[id - 14] : 0, out, max, 1); break;
    case 103: me_slot_text(me_ed.ps, me_ed.ss, out, max, 1); break;
    case 20: copy_str(out, max, me.vdp); break;
    case 21: num_append(out, max, (uint32_t)me.vram, 0, ' '); str_append(out, max, " kB"); break;
    case 5: me_freq_text(me.z80hz, out, max); break;
    case 9: me_freq_text(me.r800hz ? me.r800hz : 7159090, out, max); break;
    case 6: num_append(out, max, (uint32_t)(me.fdc < 0 ? 2 : me.fdc), 0, ' '); break;
    case 102: copy_str(out, max, me_type_name(me_ed.type) ? me_type_name(me_ed.type) : "?"); break;
    case 104: {
        int w = me_window(me_ed.type, me_ed.count);
        if (w) me_range_text(w == 8 ? 0 : me_ed.start, w, out, max);
        break;
    }
    case 105: {
        const MeTypeInfo *ti = me_type_info(me_ed.type);
        if (me_ed_size_sel()) me_size_text(me_ed.count, out, max);
        else if (me_ed_bytes) me_size_text((int)((me_ed_bytes + 0x1FFF) / 0x2000), out, max);
        else if (ti && (ti->kind == ME_K_RAMFIX || ti->kind == ME_K_NOFILE) && me_ed.count) me_size_text(me_ed.count, out, max);
        else copy_str(out, max, T(S_ME_UNKNOWN));
        break;
    }
    }
}

static void me_opt_apply(int id, int i)
{
    if (i < 0 || i >= me_nopt) return;
    int v = me_optv[i];
    switch (id) {
    case 1:
        if (!strcmp(me_opt[i], me_src)) return;
        if (me_dirty) { copy_str(me_switch_to, sizeof me_switch_to, me_opt[i]); me_q = ME_Q_SWITCH; me_pop = ME_POP_CONFIRM; me_pfocus = 130; return; }
        { char nm[72]; copy_str(nm, sizeof nm, me_opt[i]); me_load(nm); }
        return;
    case 30: copy_str(me.board, sizeof me.board, me_boards[v]); break;
    case 14: case 15: case 16: case 17: case 18: if (v < 0) { me.ext_ps[id - 14] = -1; me.ext_ss[id - 14] = 0; } else { me.ext_ps[id - 14] = v / 4; me.ext_ss[id - 14] = v % 4; } break;
    case 20: copy_str(me.vdp, sizeof me.vdp, me_vdps[v]); break;
    case 21: me.vram = v; break;
    case 5: me.z80hz = me_freqs[v]; break;
    case 9: me.r800hz = me_freqs[v]; break;
    case 6: me.fdc = v; break;
    case 102: me_ed.type = v; me_ed_fix(); return;
    case 103: me_ed.ps = v / 4; me_ed.ss = v % 4; me_ed_fix(); return;
    case 104: me_ed.start = v; me_ed_fix(); return;
    case 105: me_ed.count = v; me_ed_fix(); return;
    default: return;
    }
    me_dirty = 1;
}

static void me_set_sub(int p, int on)
{
    if (me.subslotted[p] == on) return;
    me.subslotted[p] = on;
    int had = -1;                                                   /* 3.6.7: 이 슬롯에 꽂힌 카트리지 자리 */
    for (int x = 0; x < 5; x++) if (me.ext_ps[x] == p && had < 0) had = x;
    if (on && had >= 0) {                                           /* 카트리지 슬롯을 넷으로: 카트리지 1~4 = p-0~p-3, 카트리지 5 = 다른 카트리지 슬롯 */
        int other = -1, oss = 0;
        for (int x = 0; x < 5; x++) if (me.ext_ps[x] >= 0 && me.ext_ps[x] <= 3 && me.ext_ps[x] != p) { other = me.ext_ps[x]; oss = me.ext_ss[x]; break; }
        for (int x = 0; x < 4; x++) { me.ext_ps[x] = p; me.ext_ss[x] = x; }
        me.ext_ps[4] = other; me.ext_ss[4] = other >= 0 ? oss : 0;
    }
    if (!on) {                                                      /* 서브슬롯이 없어지면 그 슬롯 줄은 서브슬롯 0으로 */
        for (int i = 0; i < me.nslot; i++) if (me.slot[i].ps == p) me.slot[i].ss = 0;
        if (had == 0 && me.ext_ps[1] == p && me.ext_ps[2] == p && me.ext_ps[3] == p) {   /* 3.6.7: 넷으로 나눈 것을 되돌림 → 카트리지 1 = p, 2 = 카트리지 5 자리 */
            int o = me.ext_ps[4], oss = me.ext_ss[4];
            me.ext_ps[0] = p; me.ext_ss[0] = 0;
            me.ext_ps[1] = o >= 0 ? o : -1; me.ext_ss[1] = o >= 0 ? oss : 0;
            if (me.ext_ps[1] < 0) me.ext_ps[1] = p == 2 ? 1 : 2;   /* 카트리지 2는 늘 있어야 함 */
            me.ext_ps[2] = me.ext_ps[3] = me.ext_ps[4] = -1; me.ext_ss[2] = me.ext_ss[3] = me.ext_ss[4] = 0;
        }
        for (int x = 0; x < 5; x++) if (me.ext_ps[x] == p) me.ext_ss[x] = 0;
    }
    me_sort();
    me_dirty = 1;
}

/* ================================================================== */
/* 배치                                                                */
/* ================================================================== */
static const char *me_tab_names[4];
static void me_tabs_init(void)
{
    me_tab_names[0] = T(S_ME_TAB_SLOT); me_tab_names[1] = T(S_ME_TAB_MEM); me_tab_names[2] = T(S_ME_TAB_CHIP); me_tab_names[3] = T(S_ME_TAB_ETC);
}

/* 기본 카드 (dlg_outer와 같은 자리) */
static void me_layout_main(void)
{
    MeW *a = mw_m; int *n = &nmw_m; *n = 0;
    me_card = dlg_outer();
    UiRect b = { me_card.x + S, dlg_body_y(), me_card.w - 2 * S, 0 };
    uint32_t top = b.y + 4 * S + ui_tabs_h() + 10 * S, bottom = dlg_cmd_y() - 8 * S;
    me_body.x = b.x; me_body.y = top; me_body.w = b.w; me_body.h = bottom - top;
    uint32_t pad = 14 * S, X = b.x + pad, W = b.w - 2 * pad, rh = ui_row_h(), sh = SLH + 12 * S, y = top;
#define SEC(t) do { mw_add(a, n, MW_SECTION, 0, X, y, W, sh, (t), 1, 0, 0); y += sh; } while (0)
#define ROW(id, t, en, on, uv) do { mw_add(a, n, MW_ROW, (id), X, y, W, rh, (t), (en), (on), (uv)); y += rh; } while (0)
    switch (me_tab) {
    case 0: {
        SEC(T(S_ME_BOARDG));
        ROW(1, T(S_ME_NAMEL), 1, 0, UV_CHOICE);
        ROW(30, T(S_ME_BOARD), 1, 0, UV_CHOICE);
        y += 6 * S; SEC(T(S_ME_SLOTLAYOUT));
        for (int p = 0; p < 4; p++) {
            static char sl[4][32];
            copy_str(sl[p], sizeof sl[p], T(S_ME_SLOTN)); str_append(sl[p], sizeof sl[p], " "); num_append(sl[p], sizeof sl[p], (uint32_t)p, 0, ' ');
            ROW(10 + p, sl[p], 1, me.subslotted[p], UV_SEG2);
        }
        y += 6 * S; SEC(T(S_ME_EXTSLOTS));
        ROW(14, T(S_ME_CART1), 1, 0, UV_CHOICE);
        ROW(15, T(S_ME_CART2), 1, 0, UV_CHOICE);
        ROW(16, T(S_ME_CART3), 1, 0, UV_CHOICE);           /* 3.3.1 MENU3-07 */
        ROW(17, T(S_ME_CART4), 1, 0, UV_CHOICE);
        ROW(18, T(S_ME_CART5), 1, 0, UV_CHOICE);           /* 3.6.7 */
        uint32_t mh = 2 * (SLH + 10 * S);               /* 슬롯 그림 (편집 중인 값 그대로) */
        if (y + 12 * S + sh + mh <= bottom) mw_add(a, n, MW_MAP, 0, X, bottom - mh - sh, W, mh + sh, T(S_SLOT_MAP), 1, 0, 0);
        break;
    }
    case 1:
        mw_add(a, n, MW_LIST, 40, X, y, W, bottom - y, 0, 1, 0, 0);
        break;
    case 2:
        SEC(T(S_ME_VIDEOG));
        ROW(20, T(S_ME_VDP), 1, 0, UV_CHOICE);
        ROW(21, T(S_ME_VRAM), 1, 0, UV_CHOICE);
        break;
    case 3: {
        int r800 = me_has_type(58);
        SEC(T(S_ME_CPUG));
        ROW(5, T(S_ME_Z80), 1, 0, UV_CHOICE);
        ROW(9, T(S_ME_R800), r800, 0, UV_CHOICE);
        y += 6 * S; SEC(T(S_ME_FDCG));
        ROW(6, T(S_ME_DRIVES), 1, 0, UV_CHOICE);
        y += 6 * S; SEC(T(S_ME_CMOSG));
        ROW(7, T(S_ME_CMOS_ON), 1, me.cmos != 0, UV_TOGGLE);
        ROW(8, T(S_ME_BATT_ON), 1, me.battery != 0, UV_TOGGLE);
        break;
    }
    }
#undef SEC
#undef ROW
}

/* 매퍼 카드 (가운데) */
static void me_layout_details(void)
{
    MeW *a = mw_d; int *n = &nmw_d; *n = 0;
    uint32_t rh = ui_row_h(), w = me_card.w * 80 / 100, h = ui_head_h() + 16 * S + 5 * rh + 14 * S + ui_cmdbar_h() + 4 * S;
    if (w < out_w * 6 / 10) w = out_w * 6 / 10;
    if (w > out_w - 24 * S) w = out_w - 24 * S;
    me_dcard.w = w; me_dcard.h = h; me_dcard.x = x0 + (out_w - w) / 2; me_dcard.y = (D.h > h ? (D.h - h) / 2 : 0);
    uint32_t X = me_dcard.x + 14 * S, W = w - 28 * S, y = me_dcard.y + ui_head_h() + 14 * S;
    me_dbody.x = X; me_dbody.y = y; me_dbody.w = W; me_dbody.h = 5 * rh;
    static const StrId lab[5] = { S_ME_FILE, S_ME_TYPE, S_ME_SLOTF, S_ME_ADDR, S_ME_SIZEF };
    static const int ids[5] = { 100, 102, 103, 104, 105 };
    for (int f = 0; f < 5; f++) {
        int en = f == 3 ? !me_ed_fixed_addr() : f == 4 ? me_ed_size_sel() : 1;
        mw_add(a, n, MW_ROW, ids[f], X, y, W, rh, T(lab[f]), en, 0, f == 0 ? UV_ACTION : UV_CHOICE);
        y += rh;
    }
}

static const char *me_confirm_text(void)
{
    return T(me_q == ME_Q_REBOOT ? S_ME_REBOOT_Q : me_q == ME_Q_SAVE ? S_ME_SAVE_Q : S_ME_SWITCH_Q);
}

/* 작은 카드 자리 */
static void me_layout_pop(void)
{
    MeW *a = mw_p; int *n = &nmw_p; *n = 0;
    uint32_t w = out_w * 3 / 4, h = 0, ch = ui_cmdbar_h() + 4 * S, hh = ui_head_h();
    switch (me_pop) {
    case ME_POP_NAME: h = hh + 24 * S + SLH + LH + 30 * S + ch; break;
    case ME_POP_PROBLEMS: h = hh + 20 * S + (uint32_t)me_nmsg * (LH + 8 * S) + ch; w = out_w * 85 / 100; break;
    case ME_POP_CONFIRM: h = hh + 24 * S + 2 * (LH + 8 * S) + 16 * S + LH + 8 * S + 20 * S; break;
    case ME_POP_BROWSE: w = out_w * 90 / 100; h = D.h * 86 / 100; break;
    }
    if (w > out_w - 24 * S) w = out_w - 24 * S;
    if (h > D.h - 24 * S) h = D.h - 24 * S;
    me_pcard.w = w; me_pcard.h = h; me_pcard.x = x0 + (out_w - w) / 2; me_pcard.y = (D.h - h) / 2;
    me_pbody.x = me_pcard.x + 14 * S; me_pbody.w = w - 28 * S; me_pbody.y = me_pcard.y + hh + 12 * S;
    me_pbody.h = h > hh + 12 * S + ch ? h - hh - 12 * S - ch : 0;
    if (me_pop == ME_POP_BROWSE) {
        uint32_t fy = me_pbody.y + LH + 18 * S;
        mw_add(a, n, MW_LIST, 150, me_pbody.x, fy, me_pbody.w, me_pbody.y + me_pbody.h - fy - 6 * S, 0, 1, 0, 0);
    }
}

/* ================================================================== */
/* 그리기                                                              */
/* ================================================================== */
static uint32_t me_list_rh(void) { return LH + 8 * S; }
static int me_list_rows(const MeW *q)
{
    uint32_t hh = q->id == 40 ? SLH + 14 * S : 0, h = q->h > hh ? q->h - hh : 0;
    int r = (int)(h / me_list_rh());
    return r < 1 ? 1 : r;
}

/* 메모리 표, 파일 열기 목록 */
/* 3.0 마우스: 지금 그리는 층의 초점 변수 (기본 me_focus, 매퍼 카드 me_dfocus, 작은 카드 me_pfocus) */
static int *mw_fv;
/* 표·파일 목록 줄: a = 목록 번호(40, 150), b = 줄 */
static void hc_melist(const UiHit *h, int ev)
{
    int *sel = h->a == 40 ? &me_lsel : &me_bsel, was = *sel == h->b && mw_fv && *mw_fv == (int)h->a;
    if (mw_fv && *mw_fv != (int)h->a) { *mw_fv = (int)h->a; menu_dirty = 1; }
    if (*sel != h->b) { *sel = h->b; menu_dirty = 1; }
    if (ev == 1 && was) ui_press(0x1C, 0);                  /* 고른 줄을 한 번 더 누르면 편집·열기 */
}

static void me_draw_list(const MeW *q, int focused)
{
    uint32_t rh = me_list_rh(), y = q->y;
    int vis = me_list_rows(q);
    uint32_t ix = q->x, iw = q->w - 12 * S;
    if (q->id == 40) {
        uint32_t c[5] = { ix + 14 * S, ix + iw * 11 / 100, ix + iw * 28 / 100, ix + iw * 62 / 100, ix + iw };
        static const StrId hd[4] = { S_ME_COL_SLOT, S_ME_COL_ADDR, S_ME_COL_TYPE, S_ME_COL_FILE };
        for (int k = 0; k < 4; k++) text_f(SF, 1, c[k], y + 4 * S, T(hd[k]), C_MUTED, 0xFF, 0, 1);
        y += SLH + 14 * S;
        if (me_lsel >= me.nslot) me_lsel = me.nslot ? me.nslot - 1 : 0;
        if (me_lsel < me_ltop) me_ltop = me_lsel;
        if (me_lsel >= me_ltop + vis) me_ltop = me_lsel - vis + 1;
        if (!me.nslot) text_clip(c[0], y, "-", C_FAINT, 0xFF, 0);
        for (int r = 0; r < vis && me_ltop + r < me.nslot; r++) {
            int i = me_ltop + r, sel = i == me_lsel;
            uint32_t ry = y + (uint32_t)r * rh;
            ui_list_row_bg(ix, ry, iw, rh, sel, i & 1, focused);
            hit_add(hc_melist, 40, i, 0, 0, ix, ry, iw, rh);
            char s[48], ad[48], ty[96], fi[96];
            me_row_text(&me.slot[i], s, ad, ty, fi, 96);
            uint8_t fg = sel && focused ? 15 : C_TEXT, mc = sel && focused ? 15 : C_MUTED;
            uint32_t t0 = ui_ty(ry, rh);
            text_clip(c[0], t0, s[0] ? s : "-", s[0] ? fg : C_FAINT, 0xFF, c[1] - c[0] - 8 * S);
            text_clip(c[1], t0, ad, mc, 0xFF, c[2] - c[1] - 8 * S);
            int sup = me_type_supported(me.slot[i].type);
            uint32_t tw = text_clip(c[2], t0, ty, sup ? fg : C_WARN, 0xFF, c[3] - c[2] - 8 * S);
            if (!sup && c[2] + tw + 20 * S < c[3]) text_clip(c[2] + tw + 6 * S, t0, "!", C_WARN, 0xFF, 0);
            small_ell(c[3], ui_sty(ry, rh), fi, mc, 0xFF, c[4] - c[3] - 10 * S);
        }
        ui_sbar(q->x + q->w - 5 * S, y, (uint32_t)vis * rh, &me_ltop, &me_lsel, vis, me.nslot);
        return;
    }
    /* 파일 열기 */
    if (me_bsel < me_btop) me_btop = me_bsel;
    if (me_bsel >= me_btop + vis) me_btop = me_bsel - vis + 1;
    for (int r = 0; r < vis && me_btop + r < me_bn; r++) {
        int i = me_btop + r, sel = i == me_bsel, e = me_bidx[i];
        uint32_t ry = y + (uint32_t)r * rh;
        ui_list_row_bg(ix, ry, iw, rh, sel, i & 1, 1);
        hit_add(hc_melist, 150, i, 0, 0, ix, ry, iw, rh);
        char t[200], sz[24] = "";
        int dir = e < 0 || entries[e].is_dir;
        if (e < 0) copy_str(t, sizeof t, "..");
        else {
            copy_str(t, sizeof t, entries[e].name);
            if (!dir) {
                if (entries[e].size < 0x2000) { num_append(sz, sizeof sz, entries[e].size, 0, ' '); str_append(sz, sizeof sz, " B"); }
                else me_size_text((int)((entries[e].size + 0x1FFF) / 0x2000), sz, sizeof sz);
            }
        }
        draw_icon(ix + 10 * S, ry + (rh - LH) / 2 + LH / 8, dir, sel ? C_ACC2 : (i & 1) ? C_HEAD : C_CARD);
        uint32_t t0 = ui_ty(ry, rh), szw = text_w("0000 kB") + 16 * S;
        text_clip(ix + LH + 20 * S, t0, t, sel ? 15 : dir ? C_WARN : C_TEXT, 0xFF, iw - szw - LH - 30 * S);
        if (sz[0]) text_clip(ix + iw - 10 * S - text_w(sz), t0, sz, sel ? 15 : C_MUTED, 0xFF, szw);
    }
    ui_sbar(q->x + q->w - 5 * S, y, (uint32_t)vis * rh, &me_btop, &me_bsel, vis, me_bn);
}

/* 편집 중인 슬롯 배치 그림 (속성 창의 슬롯 구성과 같은 모양) */
static void me_draw_map(const MeW *q)
{
    ui_section(q->x + 6 * S, q->y, q->w - 12 * S, q->text);
    uint32_t y = q->y + SLH + 12 * S, rh = SLH + 10 * S, units = 0;
    for (int ps = 0; ps < 4; ps++) units += me.subslotted[ps] ? 4 : 2;
    uint32_t unit = q->w / units, px = q->x;
    for (int ps = 0; ps < 4; ps++) {
        int sub = me.subslotted[ps], nc = sub ? 4 : 1;
        uint32_t sw = sub ? unit : 2 * unit;
        for (int ss = 0; ss < nc; ss++) {
            uint32_t cx = px + (uint32_t)ss * sw;
            char head[24], lab[160] = "";
            head[0] = (char)('0' + ps); head[1] = 0;
            if (sub) { head[1] = '-'; head[2] = (char)('0' + ss); head[3] = 0; }
            int kind = 0;
            for (int xs = 0; xs < 5; xs++)
                if (me.ext_ps[xs] == ps && (!sub || me.ext_ss[xs] == ss)) { static const char *const sn[5] = { " SLOT1", " SLOT2", " SLOT3", " SLOT4", " SLOT5" }; str_append(head, sizeof head, sn[xs]); copy_str(lab, sizeof lab, "Blank"); kind = 2; }
            for (int i = 0; i < me.nslot && kind != 2; i++) {
                const MeSlot *s = &me.slot[i];
                if (!me_window(s->type, s->count) || s->ps != ps || (sub && s->ss != ss)) continue;
                const MeTypeInfo *ti = me_type_info(s->type);
                const char *nm = ti && (ti->kind == ME_K_RAM || ti->kind == ME_K_MAPRAM || ti->kind == ME_K_RAMFIX) ? "RAM" : s->file[0] ? path_base(s->file) : ti ? ti->name : "ROM";
                if (kind && strstr_ci(lab, nm)) continue;
                if (kind) str_append(lab, sizeof lab, " + ");
                str_append(lab, sizeof lab, nm);
                kind = 1;
            }
            if (!kind) copy_str(lab, sizeof lab, "Blank");
            uint8_t bg = kind == 2 ? C_FIELD : kind == 1 ? C_ROWSEL : C_HEAD;
            if (kind == 2) ui_rframe(cx + 2 * S, y, sw - 4 * S, 2 * rh, 5 * S, C_WARN, C_FIELD);
            else ui_rrect(cx + 2 * S, y, sw - 4 * S, 2 * rh, 5 * S, bg);
            small_ell(cx + 8 * S, ui_sty(y, rh) + 2 * S, head, C_MUTED, 0xFF, sw - 14 * S);
            small_ell(cx + 8 * S, ui_sty(y + rh, rh) - 2 * S, lab, kind == 2 ? C_WARN : kind ? C_TEXT : C_FAINT, 0xFF, sw - 14 * S);
        }
        px += (uint32_t)nc * sw;
    }
}

static void mw_draw(const MeW *a, int n, int focus, int *fv)
{
    mw_fv = fv;
    for (int i = 0; i < n; i++) {
        const MeW *q = &a[i];
        int f = q->id && q->id == focus && mw_focusable(q);
        if (fv && q->kind == MW_ROW && mw_focusable(q)) {  /* 3.0 마우스: 값 자리 왼쪽 반 ←, 오른쪽 반 →, 이름·스위치는 Enter */
            uint32_t vx = q->x + q->w * (q->id >= 100 ? 22 : 40) / 100;
            hit_add(hc_val, (intptr_t)fv, q->id, (q->uv == UV_TOGGLE || q->uv == UV_ACTION || q->uv == UV_CHOICE) ? 0 : (int)vx, 0x1C, q->x, q->y, q->w, q->h);
        }
        switch (q->kind) {
        case MW_SECTION: ui_section(q->x + 6 * S, q->y + 2 * S, q->w - 12 * S, q->text); break;
        case MW_NOTE: small_ell(q->x + 20 * S, q->y, q->text, C_MUTED, 0xFF, q->w - 40 * S); break;
        case MW_MAP: me_draw_map(q); break;
        case MW_LIST: me_draw_list(q, f); break;
        case MW_ROW: {
            char v[220] = "";
            uint32_t vx = q->x + q->w * 40 / 100;
            if (q->uv == UV_SEG2) { ui_row(q->x, q->y, q->w, vx, q->text, T(S_ME_PRIMARY), T(S_ME_EXP4), UV_SEG2, q->on, f, q->en); break; }
            if (q->uv == UV_TOGGLE) { ui_row(q->x, q->y, q->w, vx, q->text, "", 0, UV_TOGGLE, q->on, f, q->en); break; }
            if (q->id == 100) {
                copy_str(v, sizeof v, me_ed.file[0] ? me_ed.file : "-");
                ui_row(q->x, q->y, q->w, q->x + q->w * 22 / 100, q->text, v, 0, UV_ACTION, 0, f, q->en);
                break;
            }
            me_combo_val(q->id, v, sizeof v);
            if (q->id == 1 && me_dirty) str_append(v, sizeof v, "  ●");
            if (q->id == 102 && !me_type_supported(me_ed.type)) { str_append(v, sizeof v, "  "); str_append(v, sizeof v, T(S_ME_UNSUP)); }
            if (q->id >= 100) vx = q->x + q->w * 22 / 100;
            ui_row(q->x, q->y, q->w, vx, q->text, v, 0, UV_DROP, 0, f, q->en);   /* 값 ‹ › 대신 밑줄, 누르면 목록 */
            break;
        }
        }
    }
}

/* 펼친 값 목록: 그 줄의 값 자리 아래(모자라면 위)에 작은 카드 */
static int me_drop_vis = 11;
static void me_draw_drop(const MeW *q)
{
    uint32_t rh = LH + 8 * S;
    int vis = me_nopt < 12 ? me_nopt : 12;
    if (vis < 1) vis = 1;
    uint32_t vx = q->x + q->w * (q->id >= 100 ? 22 : 40) / 100, w = q->x + q->w - vx, h, y = q->y + q->h;
    {   /* 화면 아래로 넘치면 위로, 위도 모자라면 넓은 쪽에 맞춰 줄 수를 줄임 (스크롤) */
        int below = (int)D.h - 8 * (int)S - (int)y, above = (int)q->y - 8 * (int)S;
        int need = vis * (int)rh + 16 * (int)S;
        int up = need > below && above > below;
        int room = (up ? above : below) - 16 * (int)S;
        if (room < need - 16 * (int)S) { vis = room / (int)rh; if (vis < 3) vis = me_nopt < 3 ? me_nopt : 3; }
        h = (uint32_t)vis * rh + 16 * S;
        if (up) y = q->y - h;
    }
    me_drop_vis = vis;
    if (me_dsel < me_dtop) me_dtop = me_dsel;
    if (me_dsel >= me_dtop + vis) me_dtop = me_dsel - vis + 1;
    hit_add(hc_key, 0x01, 0, 0, 0x01, 0, 0, D.w, D.h);     /* 3.0: 목록 밖을 누르면 닫기 */
    hit_add(hc_none, 1, 0, 0, 0, vx, y, w, h);
    ui_rrect(vx + 4 * S, y + 6 * S, w, h, 8 * S, C_SHADOW);
    ui_rframe(vx, y, w, h, 8 * S, C_ACC, C_CARD);
    uint32_t iw = w - 24 * S;
    for (int r = 0; r < vis && me_dtop + r < me_nopt; r++) {
        int i = me_dtop + r, sel = i == me_dsel;
        uint32_t ry = y + 8 * S + (uint32_t)r * rh;
        ui_list_row_bg(vx + 8 * S, ry, iw, rh, sel, 0, 1);
        hit_add(hc_sel, (intptr_t)&me_dsel, i, 1, 0x1C, vx + 8 * S, ry, iw, rh);   /* 누르면 바로 고름 */
        text_clip(vx + 20 * S, ui_ty(ry, rh), me_opt[i], sel ? 15 : me_optg[i] ? C_FAINT : C_TEXT, 0xFF, iw - 24 * S);
    }
    ui_sbar(vx + w - 12 * S, y + 8 * S, (uint32_t)vis * rh, &me_dtop, &me_dsel, vis, me_nopt);
}

static void me_draw_details(void)
{
    me_layout_details();
    hit_add(hc_none, 2, 0, 0, 0, 0, 0, D.w, D.h);           /* 3.0: 아래 창 막기 */
    UiRect b = ui_card(me_dcard.x, me_dcard.y, me_dcard.w, me_dcard.h, me_ed_idx >= 0 ? T(S_ME_EDITT) : T(S_ME_ADDT), T(S_ME_DETAILS), me_src);
    (void)b;
    mw_draw(mw_d, nmw_d, me_pop ? 0 : me_dfocus, me_pop ? 0 : &me_dfocus);
    uint32_t cy = me_dcard.y + me_dcard.h - ui_cmdbar_h() - 2 * S;
    ui_cmdbar(me_dcard.x + S, cy, me_dcard.w - 2 * S, T(me_drop && !me_pop ? S_ME_KEYS_LIST : S_ME_KEYS_DLG2));
}

static void me_draw_pop(void)
{
    me_layout_pop();
    const char *pt = me_pop == ME_POP_NAME ? T(S_ME_SAVEAS_T) : me_pop == ME_POP_PROBLEMS ? T(S_ME_PROBLEMS) : me_pop == ME_POP_BROWSE ? T(S_ME_OPENT) : T(S_ME_TITLE);
    const char *sub = me_pop == ME_POP_BROWSE ? 0 : me.name;
    hit_add(hc_none, 3, 0, 0, 0, 0, 0, D.w, D.h);           /* 3.0: 아래 창 막기 */
    ui_card(me_pcard.x, me_pcard.y, me_pcard.w, me_pcard.h, pt, sub, 0);
    uint32_t X = me_pbody.x, W = me_pbody.w, y = me_pbody.y, cy = me_pcard.y + me_pcard.h - ui_cmdbar_h() - 2 * S;
    switch (me_pop) {
    case ME_POP_NAME: {
        text_f(SF, 1, X + 4 * S, y, T(S_ME_NAMEQ), C_MUTED, 0xFF, 0, 1);
        y += SLH + 10 * S;
        uint32_t fh = LH + 14 * S;
        ui_rframe(X, y, W, fh, 6 * S, C_ACC, C_FIELD);
        uint32_t tw = text_clip(X + 12 * S, ui_ty(y, fh), me_name_buf, C_TEXT, 0xFF, W - 30 * S);
        fe_fill(X + 13 * S + tw, ui_ty(y, fh), 2 * S, LH, C_ACC);    /* 글자 커서 */
        ui_cmdbar(me_pcard.x + S, cy, me_pcard.w - 2 * S, T(S_ME_KEYS_NAME));
        break;
    }
    case ME_POP_PROBLEMS:
        for (int i = 0; i < me_nmsg; i++) {
            uint32_t ry = y + (uint32_t)i * (LH + 8 * S), d = LH / 2;
            ui_rrect(X + 6 * S, ry + (LH - d) / 2 + 4 * S, d, d, d / 2, C_BAD);
            text_clip(X + 6 * S + d + 12 * S, ry + 4 * S, me_msgs[i], C_TEXT, 0xFF, W - d - 24 * S);
        }
        ui_cmdbar(me_pcard.x + S, cy, me_pcard.w - 2 * S, T(S_ME_KEYS_OK));
        break;
    case ME_POP_CONFIRM: {
        text_clip(X + 8 * S, y + 4 * S, me_confirm_text(), C_TEXT, 0xFF, W - 16 * S);
        y += LH + 8 * S;
        small_ell(X + 8 * S, y + 4 * S, T(me_q == ME_Q_REBOOT ? S_ME_REBOOT_Q2 : me_q == ME_Q_SAVE ? S_ME_SAVE_Q2 : S_ME_SWITCH_Q2), C_MUTED, 0xFF, W - 16 * S);
        y += LH + 24 * S;
        const char *k3[3] = { "Y", "N", "Esc" };
        const char *d3[3] = { T(me_q == ME_Q_SAVE ? S_ME_SAVEY : S_ME_YES), T(me_q == ME_Q_SAVE ? S_ME_DONTSAVE : S_ME_NO),
                              me_q == ME_Q_REBOOT ? T(S_ME_CLOSEONLY) : T(S_ME_CANCEL) };
        ui_keys_center(X, y, W, k3, d3, me_q == ME_Q_SWITCH ? 2 : 3, me_pfocus - 130);
        break;
    }
    case ME_POP_BROWSE: {
        uint32_t fh = LH + 8 * S;
        ui_rrect(X, y, W, fh, 5 * S, C_FIELD);
        text_clip(X + 10 * S, ui_ty(y, fh), me_bdir, C_TEXT, 0xFF, W - 20 * S);
        mw_draw(mw_p, nmw_p, 150, &me_pfocus);
        ui_cmdbar(me_pcard.x + S, cy, me_pcard.w - 2 * S, T(S_ME_KEYS_BROWSE));
        break;
    }
    }
}

static void draw_medit(void)
{
    me_tabs_init();
    me_layout_main();
    char sub[160];
    copy_str(sub, sizeof sub, me.name);
    if (me_dirty) { str_append(sub, sizeof sub, "   ● "); str_append(sub, sizeof sub, T(S_ME_CHANGED)); }
    const char *chip = me_is_running() ? T(S_ME_RUNNING) : 0;
    UiRect o;
    ui_full_card(T(S_ME_TITLE), sub, chip, &o);
    ui_tabs(o.x + S, dlg_body_y() + 4 * S, o.w - 2 * S, me_tab_names, 4, me_tab, 0);
    mw_draw(mw_m, nmw_m, (me_det || me_pop) ? 0 : me_focus, (me_det || me_pop) ? 0 : &me_focus);
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, T(me_drop && !me_det && !me_pop ? S_ME_KEYS_LIST : me_tab == 1 ? S_ME_KEYS_MEM3 : S_ME_KEYS3));
    if (!me_det && !me_pop && me_drop) { MeW *q = mw_find(mw_m, nmw_m, me_drop); if (q) me_draw_drop(q); }
    if (me_det) {
        me_draw_details();
        if (!me_pop && me_drop) { MeW *q = mw_find(mw_d, nmw_d, me_drop); if (q) me_draw_drop(q); }
    }
    if (me_pop) me_draw_pop();
}

/* ================================================================== */
/* 키                                                                  */
/* ================================================================== */
static void mw_move(MeW *a, int n, int *focus, int dir)
{
    int cur = -1;
    for (int i = 0; i < n; i++) if (a[i].id == *focus && mw_focusable(&a[i])) cur = i;
    for (int k = 1; k <= n; k++) {
        int i = cur < 0 ? (dir > 0 ? k - 1 : n - k) : cur + dir * k;
        if (i < 0 || i >= n) return;                    /* 끝에서 멈춤 */
        if (mw_focusable(&a[i])) { *focus = a[i].id; return; }
    }
}
static void mw_fix_focus(MeW *a, int n, int *focus)
{
    for (int i = 0; i < n; i++) if (a[i].id == *focus && mw_focusable(&a[i])) return;
    *focus = 0;
    mw_move(a, n, focus, 1);
}

static void me_open_drop(int id)
{
    int cur = me_opts(id);
    if (me_nopt <= 0) return;
    me_drop = id; me_dsel = cur; me_dtop = cur > 5 ? cur - 5 : 0;
}
static void me_step_combo(int id, int dir)
{
    int cur = me_opts(id);
    if (me_nopt <= 1) return;
    int i = cur + dir;
    if (i < 0) i = 0;
    if (i >= me_nopt) i = me_nopt - 1;
    if (i != cur) me_opt_apply(id, i);
}

static void me_tab_step(int dir) { me_tab = (me_tab + (dir > 0 ? 1 : 3)) % 4; }

static void me_remove_row(void)
{
    if (!me.nslot) return;
    for (int i = me_lsel; i < me.nslot - 1; i++) me.slot[i] = me.slot[i + 1];
    me.nslot--;
    if (me_lsel >= me.nslot && me_lsel > 0) me_lsel--;
    me_dirty = 1;
}

static void me_saveas_open(void)
{
    copy_str(me_name_buf, sizeof me_name_buf, me.name);
    me_pop = ME_POP_NAME; me_pfocus = 120;
}

/* 단추·체크 누르기 */
static void me_press(int id)
{
    switch (id) {
    case 2: me_tab_step(1); break;
    case 7: me.cmos = !me.cmos; me_dirty = 1; break;
    case 8: me.battery = !me.battery; me_dirty = 1; break;
    case 10: case 11: case 12: case 13: me_set_sub(id - 10, !me.subslotted[id - 10]); break;
    case 40: case 42: if (me.nslot) me_ed_open(me_lsel); break;
    case 41: if (me.nslot < ME_SLOTS) me_ed_open(-1); break;
    case 43: me_remove_row(); break;
    case 90: me_save(); break;
    case 91: me_saveas_open(); break;
    case 92: me_run(); break;
    case 93: me_try_close(); break;
    case 100: case 101: me_browse_open(); break;
    case 106: me_ed_ok(); break;
    case 107: me_det = 0; break;
    case 121: {
        char old[96];
        copy_str(old, sizeof old, me.name);
        copy_str(me.name, sizeof me.name, me_name_buf);
        me_pop = 0;
        if (!strcmp(me.name, me_src)) { copy_str(me_msgs[0], ME_MSG, T(S_ME_E_EXISTS)); me_nmsg = 1; me_pop = ME_POP_PROBLEMS; me_pfocus = 140; copy_str(me.name, sizeof me.name, old); break; }
        if (me_save()) copy_str(me.name, sizeof me.name, old);      /* 실패하면 이름 그대로 */
        break;
    }
    case 122: me_pop = 0; break;
    case 140: me_pop = 0; break;
    case 130:                                                       /* 예 / 저장 */
        me_pop = 0;
        if (me_q == ME_Q_REBOOT) me_run();
        else if (me_q == ME_Q_SAVE) { if (!me_save()) me_close_now(); }
        else { char nm[96]; copy_str(nm, sizeof nm, me_switch_to); me_load(nm); }
        break;
    case 131:                                                       /* 아니요 / 저장 안 함 */
        me_pop = 0;
        if (me_q == ME_Q_SAVE) me_close_now();
        break;
    case 132:                                                       /* 적용 안 하고 닫기 / 취소 */
        me_pop = 0;
        if (me_q == ME_Q_REBOOT) me_close_now();
        break;
    case 150: case 151: me_browse_enter(); break;
    case 152: me_pop = 0; break;
    case 153: me_browse_up(); break;
    }
}

/* 목록에서 글자로 이동 */
static int me_letter_jump(int key, int cur, int n, const char *(*name)(int))
{
    if (key >= 0x40 || !sc_ascii[key] || n <= 0) return cur;
    for (int k = 1; k <= n; k++) {
        int i = (cur + k) % n;
        const char *s = name(i);
        if (s && lower_c((uint8_t)s[0]) == sc_ascii[key]) return i;
    }
    return cur;
}
static const char *opt_name(int i) { return me_opt[i]; }
static const char *browse_name(int i) { int e = me_bidx[i]; return e < 0 ? ".." : entries[e].name; }

static int me_list_key(int *sel, int n, int page, int key)
{
    switch (key) {
    case 0xC8: if (*sel > 0) (*sel)--; return 1;
    case 0xD0: if (*sel < n - 1) (*sel)++; return 1;
    case 0xC9: *sel = *sel > page ? *sel - page : 0; return 1;
    case 0xD1: *sel = *sel + page < n ? *sel + page : (n ? n - 1 : 0); return 1;
    case 0xC7: *sel = 0; return 1;
    case 0xCF: *sel = n ? n - 1 : 0; return 1;
    }
    return 0;
}

/* 한 층의 공통 키: 칸 이동·값 바꾸기·누르기. 처리했으면 1 */
static int mw_key(MeW *a, int n, int *focus, int key)
{
    mw_fix_focus(a, n, focus);
    MeW *q = mw_find(a, n, *focus);
    if (key == 0x0F) return 1;                                           /* 2.17: Tab은 탭만 바꾼다 (줄 이동은 ↑/↓) */
    if (q && q->kind == MW_LIST) {
        int page = me_list_rows(q) - 1;
        if (q->id == 40) {
            if (me_list_key(&me_lsel, me.nslot, page, key)) return 1;
            if (key == 0x1C || key == 0x9C) { me_press(40); return 1; }
        } else {
            if (me_list_key(&me_bsel, me_bn, page, key)) return 1;
            if (key == 0x1C || key == 0x9C) { me_press(150); return 1; }
            if (key == 0x0E) { me_browse_up(); return 1; }
            int j = me_letter_jump(key, me_bsel, me_bn, browse_name);
            if (j != me_bsel) { me_bsel = j; return 1; }
        }
        return 0;
    }
    int uv = q ? q->uv : -1;
    switch (key) {
    case 0xC8: mw_move(a, n, focus, -1); return 1;
    case 0xD0: mw_move(a, n, focus, 1); return 1;
    case 0xC7: *focus = 0; mw_move(a, n, focus, 1); return 1;        /* Home */
    case 0xCF: *focus = 0; mw_move(a, n, focus, -1); return 1;       /* End */
    case 0xCB: case 0xCD: {
        int d = key == 0xCD ? 1 : -1;
        if (!q) return 1;
        if (uv == UV_CHOICE) { }                          /* 목록으로 고르는 값은 ←/→로 바꾸지 않음 */
        else if (uv == UV_SEG2) me_set_sub(*focus - 10, d > 0);
        else if (uv == UV_TOGGLE) { if ((d > 0) != (q->on != 0)) me_press(*focus); }
        return 1;
    }
    case 0x1C: case 0x9C: case 0x39:
        if (!q) return 1;
        if (uv == UV_CHOICE) me_open_drop(*focus);
        else me_press(*focus);
        return 1;
    }
    return 0;
}

static void me_goto_tab(int t)
{
    if (t == me_tab) return;
    me_tab = t; me_drop = 0; me_focus = 0;
    me_layout_main();
    mw_fix_focus(mw_m, nmw_m, &me_focus);
}

static void medit_key(int key)
{
    menu_dirty = 1;
    if (mod_bit(key)) return;
    int shift = (mods & 0x03) != 0;
    if (me_drop) {                                                  /* 펼친 값 목록 */
        switch (key) {
        case 0x01: case 0xCB: me_drop = 0; return;
        case 0x1C: case 0x9C: case 0x39: case 0xCD: { int id = me_drop; me_drop = 0; me_opt_apply(id, me_dsel); return; }
        case 0x0F: me_drop = 0; break;                                  /* Tab: 목록을 닫고 탭 바꾸기로 */
        default:
            if (me_list_key(&me_dsel, me_nopt, me_drop_vis, key)) return;
            me_dsel = me_letter_jump(key, me_dsel, me_nopt, opt_name);
            return;
        }
    }
    if (me_pop) {
        me_layout_main(); me_layout_pop();
        if (me_pop == ME_POP_NAME) {                                /* 이름 입력 */
            if (key == 0x1C || key == 0x9C) { me_press(121); return; }
            if (key == 0x01) { me_pop = 0; return; }
            int l = (int)strlen(me_name_buf);
            if (key == 0x0E) { if (l > 0) me_name_buf[l - 1] = 0; return; }
            char c = key_char(key, shift);
            if (c && l < (int)sizeof me_name_buf - 1) { me_name_buf[l] = c; me_name_buf[l + 1] = 0; }
            return;
        }
        if (me_pop == ME_POP_PROBLEMS) { if (key == 0x01 || key == 0x1C || key == 0x9C || key == 0x39) me_pop = 0; return; }
        if (me_pop == ME_POP_CONFIRM) {                             /* Y / N / C 바로, ←/→ + Enter 도 */
            int nk = me_q == ME_Q_SWITCH ? 2 : 3;
            if (me_pfocus < 130 || me_pfocus >= 130 + nk) me_pfocus = 130;
            switch (key) {
            case 0x15: me_press(130); return;
            case 0x31: me_press(131); return;
            case 0x01: if (me_q == ME_Q_REBOOT) me_press(132); else me_pop = 0; return;   /* 4.0.28: Esc = 적용 안 하고 닫기 (다시 시작 물음) / 그 밖엔 돌아가기 */
            case 0xCB: if (me_pfocus > 130) me_pfocus--; return;
            case 0xCD: if (me_pfocus < 129 + nk) me_pfocus++; return;
            case 0x1C: case 0x9C: case 0x39: me_press(me_pfocus); return;
            }
            return;
        }
        if (key == 0x01) { me_pop = 0; return; }
        mw_key(mw_p, nmw_p, &me_pfocus, key);
        return;
    }
    me_layout_main();
    if (me_det) {                                                   /* 매퍼 카드 */
        me_layout_details();
        if (key == 0x01) { me_det = 0; return; }
        if (key == 0x3C) { me_ed_ok(); return; }                    /* F2 = 확인 */
        if (key == 0xD3 && me_dfocus == 100) { me_ed.file[0] = 0; me_ed_bytes = 0; return; }
        mw_key(mw_d, nmw_d, &me_dfocus, key);
        return;
    }
    /* 기본 카드 */
    switch (key) {
    case 0x01: me_try_close(); return;
    case 0x3C: me_save(); return;                                   /* F2 저장 (다시 켜지 않음) */
    case 0x3D: me_goto_tab(0); me_focus = 1; me_open_drop(1); return;   /* F3 머신 고르기 */
    case 0x3E: me_saveas_open(); return;                            /* F4 다른 이름으로 저장 */
    case 0x3F: me_run(); return;                                    /* F5 실행 */
    case 0x02: case 0x03: case 0x04: case 0x05: me_goto_tab(key - 0x02); return;   /* 1~4 탭 */
    case 0xC9: if (me_tab != 1) { me_goto_tab((me_tab + 3) % 4); return; } break;
    case 0xD1: if (me_tab != 1) { me_goto_tab((me_tab + 1) % 4); return; } break;
    case 0x0F: me_goto_tab((me_tab + (shift ? 3 : 1)) % 4); return;               /* 2.17: Tab 다음 탭, Shift+Tab 앞 탭 */
    }
    if (me_tab == 1) {
        if (key == 0xD2) { me_press(41); return; }                  /* Ins 추가 */
        if (key == 0xD3) { me_remove_row(); return; }               /* Del 삭제 */
        if (key == 0x39) { me_press(40); return; }
    }
    mw_fix_focus(mw_m, nmw_m, &me_focus);
    mw_key(mw_m, nmw_m, &me_focus, key);
}
