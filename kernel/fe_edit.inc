/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_edit.inc - 4.0.29 텍스트 편집기 (창 17). fe.c가 포함한다.
 * 파일 창(F4)·이미지 파일 관리(F4)에서 .txt .doc .bat .bas .ini 같은 글 파일을 열어 고치고 저장한다.
 * 파일 전체를 메모리(256 KB까지)에 두고 바이트 단위로 고침. 줄 끝 \n / \r\n은 파일에 있던 것을 그대로 쓴다.
 * 글자 쓰기는 영문·숫자·기호와 한글(Shift+Space로 한/영, 두벌식). 글은 UTF-8. */

#define ED_MAX (256 * 1024)
static char ed_buf[ED_MAX + 8];
static int ed_len, ed_cur, ed_topb, ed_hcol, ed_want, ed_dirty, ed_crlf, ed_ask, ed_from, ed_saved, ed_rows = 20;
static char ed_path[FE_PATH_MAX], ed_name[FE_KEY_MAX];

/* 4.0.32: 한글 입력 (두벌식). Shift+Space = 한/영. 글자는 UTF-8(3바이트)로 넣고, 만드는 중인 글자는 바로 앞 3바이트를 바꿔 씀 */
static void ed_ins(const char *s, int n);
static void ed_del(int a, int b);
static int ed_han, ed_hs, ed_hL, ed_hV, ed_hT;               /* hs: 0 없음 1 초성 2 초+중 3 초+중+종 4 홀로 모음 */
static const uint16_t hg_cho_c[19] = { 0x3131,0x3132,0x3134,0x3137,0x3138,0x3139,0x3141,0x3142,0x3143,0x3145,0x3146,0x3147,0x3148,0x3149,0x314A,0x314B,0x314C,0x314D,0x314E };
static const signed char hg_cho_jong[19] = { 1,2,4,7,-1,8,16,17,-1,19,20,21,22,-1,23,24,25,26,27 };
static int hg_cho_of_jong(int j) { static const signed char t[28] = { -1,0,1,-1,2,-1,-1,3,5,-1,-1,-1,-1,-1,-1,-1,6,7,-1,9,10,11,12,14,15,16,17,18 }; return t[j]; }
static int hg_dbl_jong(int t, int c)                          /* 받침 + 자음 = 겹받침 (없으면 -1) */
{
    static const struct { signed char t, c, r; } d[] = { {1,9,3},{4,12,5},{4,18,6},{8,0,9},{8,6,10},{8,7,11},{8,9,12},{8,16,13},{8,17,14},{8,18,15},{17,9,18} };
    for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) if (d[i].t == t && d[i].c == c) return d[i].r;
    return -1;
}
static void hg_split_jong(int j, int *first, int *second_cho)   /* 겹받침 → 앞 받침 + 뒤 초성, 홑받침이면 first=0 */
{
    static const struct { signed char j, a, c; } d[] = { {3,1,9},{5,4,12},{6,4,18},{9,8,0},{10,8,6},{11,8,7},{12,8,9},{13,8,16},{14,8,17},{15,8,18},{18,17,9} };
    for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) if (d[i].j == j) { *first = d[i].a; *second_cho = d[i].c; return; }
    *first = 0; *second_cho = hg_cho_of_jong(j);
}
static int hg_dbl_vowel(int a, int b)
{
    static const struct { signed char a, b, r; } d[] = { {8,0,9},{8,1,10},{8,20,11},{13,4,14},{13,5,15},{13,20,16},{18,20,19} };
    for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) if (d[i].a == a && d[i].b == b) return d[i].r;
    return -1;
}
static int hg_key(char c, int *kind)                           /* 영문 글쇠 → 자음(0)/모음(1)과 번호, 아니면 -1 */
{
    static const char *const cons = "rRseEfaqQtTdwWczxvg";    /* 초성 번호 0..18 */
    for (int i = 0; i < 19; i++) if (cons[i] == c) { *kind = 0; return i; }
    static const struct { char c; signed char v; } vw[] = { {'k',0},{'o',1},{'i',2},{'O',3},{'j',4},{'p',5},{'u',6},{'P',7},{'h',8},{'y',12},{'n',13},{'b',17},{'m',18},{'l',20} };
    for (unsigned i = 0; i < sizeof vw / sizeof vw[0]; i++) if (vw[i].c == c) { *kind = 1; return vw[i].v; }
    return -1;
}
static void hg_put(const char *s3, int replace)                /* 만드는 글자를 쓰거나(replace) 새로 넣음 */
{
    if (replace && ed_cur >= 3) ed_del(ed_cur - 3, ed_cur);
    ed_ins(s3, 3);
}
static void hg_cp(unsigned cp, char *o) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); }
static void hg_show(int replace)
{
    char o[3]; unsigned cp;
    if (ed_hs == 1) cp = hg_cho_c[ed_hL];
    else if (ed_hs == 4) cp = 0x314F + (unsigned)ed_hV;
    else cp = 0xAC00 + ((unsigned)ed_hL * 21 + (unsigned)ed_hV) * 28 + (unsigned)ed_hT;
    hg_cp(cp, o); hg_put(o, replace);
}
static void hg_letter(char c)
{
    int kind, v = hg_key(c, &kind);
    if (kind == 0) {                                            /* 자음 */
        if (ed_hs == 2 && hg_cho_jong[v] > 0) { ed_hT = hg_cho_jong[v]; ed_hs = 3; hg_show(1); return; }
        if (ed_hs == 3) { int r = hg_dbl_jong(ed_hT, v); if (r > 0) { ed_hT = r; hg_show(1); return; } }
        ed_hL = v; ed_hT = 0; ed_hs = 1; hg_show(0); return;
    }
    if (ed_hs == 1) { ed_hV = v; ed_hs = 2; hg_show(1); return; }
    if (ed_hs == 4 || ed_hs == 2) { int r = hg_dbl_vowel(ed_hV, v); if (r >= 0) { ed_hV = r; hg_show(1); return; } }
    if (ed_hs == 3) {                                           /* 받침이 다음 글자의 초성으로 넘어감 */
        int first, c2; hg_split_jong(ed_hT, &first, &c2);
        ed_hT = first; hg_show(1);
        ed_hL = c2; ed_hV = v; ed_hT = 0; ed_hs = 2; hg_show(0); return;
    }
    ed_hV = v; ed_hT = 0; ed_hs = 4; hg_show(0);
}
static int hg_back(void)                                        /* 만드는 글자가 있으면 한 단계 지움 (1 = 처리함) */
{
    if (!ed_hs) return 0;
    if (ed_hs == 3) { ed_hT = 0; ed_hs = 2; hg_show(1); }
    else if (ed_hs == 2) { ed_hs = 1; hg_show(1); }
    else { ed_del(ed_cur - 3, ed_cur); ed_hs = 0; }
    return 1;
}

static int ed_ls(int p) { while (p > 0 && ed_buf[p - 1] != '\n') p--; return p; }                         /* 줄 처음 */
static int ed_le(int p) { int q = p; while (q < ed_len && ed_buf[q] != '\n') q++; if (q > p && ed_buf[q - 1] == '\r') q--; return q; }   /* 줄 끝 (\r\n 앞) */
static int ed_nl(int p) { while (p < ed_len && ed_buf[p] != '\n') p++; return p < ed_len ? p + 1 : ed_len; }   /* 다음 줄 처음 (없으면 끝) */
static int ed_nc(int p) { if (p >= ed_len) return ed_len; p++; while (p < ed_len && ((uint8_t)ed_buf[p] & 0xC0) == 0x80) p++; return p; }
static int ed_pc(int p) { if (p <= 0) return 0; p--; while (p > 0 && ((uint8_t)ed_buf[p] & 0xC0) == 0x80) p--; return p; }
static int ed_chars(int a, int b) { int n = 0; for (int p = a; p < b; p = ed_nc(p)) n++; return n; }
static int ed_advance(int p, int n, int lim) { while (n-- > 0 && p < lim) p = ed_nc(p); return p > lim ? lim : p; }

static int ed_ro;                                   /* zip 안 파일 = 보기만 (저장 안 함) */
static void ed_start(const char *path, const char *name, uint32_t size)
{
    if (size > ED_MAX) { toast(T(S_ED_BIG), 0); return; }
    ed_len = (int)size; ed_buf[ed_len] = 0;
    ed_cur = ed_topb = ed_hcol = ed_want = 0; ed_dirty = ed_ask = ed_saved = 0;
    ed_crlf = 0;
    for (int i = 0; i < ed_len; i++) if (ed_buf[i] == '\n') { ed_crlf = i > 0 && ed_buf[i - 1] == '\r'; break; }
    copy_str(ed_path, sizeof ed_path, path); copy_str(ed_name, sizeof ed_name, name);
    ed_from = dialog;
    open_dialog(17);
}
static void ed_open(const char *path, const char *name)
{
    uint32_t size = 0;
    ed_ro = 0;
    if (!P.read_file || P.read_file(path, (uint8_t *)ed_buf, ED_MAX, &size)) { toast(T(S_ED_READFAIL), name); return; }
    ed_start(path, name, size);
}
static void ed_open_zip(const uint8_t *zip, uint32_t zlen, const char *zpath, const char *name)   /* 5.0.37 */
{
    uint32_t size = 0;
    int r = zip_read(zip, zlen, zpath, (uint8_t *)ed_buf, ED_MAX, &size);
    if (r) { toast(r == ZIPX_BIG ? T(S_ED_BIG) : T(S_ED_READFAIL), name); return; }
    ed_start(name, name, size);
    ed_ro = 1;
}
static void ed_close(void)
{
    int back = ed_from == 13 ? 13 : 1;
    open_dialog(back);
    if (back == 1) { if (ed_saved) load_dir(ed_name); browse_full = 1; }
    else if (ed_saved) { int p = dc_pane; dc_load_usb(p, ed_name); dc_refresh_peer(p); }
    full_redraw();
}
static int ed_save(void)
{
    if (ed_ro) { toast(lang == 0 ? "zip 안의 파일은 저장할 수 없습니다" : lang == 2 ? "zip内のファイルは保存できません" : "Files inside a zip cannot be saved", ed_name); return -1; }
    if (!P.write_file || P.write_file(ed_path, (const uint8_t *)ed_buf, (uint32_t)ed_len)) { toast(T(S_ED_SAVEFAIL), ed_name); return -1; }
    ed_dirty = 0; ed_saved = 1;
    toast(T(S_ED_SAVED), ed_name);
    return 0;
}
static void ed_ins(const char *s, int n)
{
    if (ed_len + n > ED_MAX) return;
    memmove(ed_buf + ed_cur + n, ed_buf + ed_cur, (size_t)(ed_len - ed_cur));
    memcpy(ed_buf + ed_cur, s, (size_t)n);
    ed_len += n; ed_cur += n; ed_buf[ed_len] = 0; ed_dirty = 1;
}
static void ed_del(int a, int b)                                    /* [a, b) 지우기 */
{
    if (a >= b) return;
    memmove(ed_buf + a, ed_buf + b, (size_t)(ed_len - b));
    ed_len -= b - a; ed_buf[ed_len] = 0; ed_cur = a; ed_dirty = 1;
}
static void ed_vert(int dir)
{
    int ls = ed_ls(ed_cur);
    if (dir < 0) {
        if (!ls) return;
        int pl = ed_ls(ls - 1);
        ed_cur = ed_advance(pl, ed_want, ed_le(pl));
    } else {
        int q = ed_cur;
        while (q < ed_len && ed_buf[q] != '\n') q++;
        if (q >= ed_len) return;                                    /* 마지막 줄 */
        int nl = q + 1;
        ed_cur = ed_advance(nl, ed_want, ed_le(nl));
    }
}

static void ed_key(int key)
{
    menu_dirty = 1;
    int ctrl = (mods & 0x0C) != 0;
    if (!ed_ask && key == 0x39 && (mods & 0x03)) { ed_han ^= 1; ed_hs = 0; return; }       /* Shift+Space = 한/영 */
    if (!ed_ask && ed_han && !ctrl && !(key & 0x80)) {
        char c = key_char(key, (mods & 0x03) != 0);
        int kind;
        if (c && hg_key(c, &kind) >= 0) { hg_letter(c); ed_want = ed_chars(ed_ls(ed_cur), ed_cur); return; }
    }
    if (key == 0x0E && ed_hs) { if (hg_back()) { ed_want = ed_chars(ed_ls(ed_cur), ed_cur); return; } }
    if (!mod_bit(key)) ed_hs = 0;
    if (ed_ask) {                                                   /* 닫기 전에 저장할까요? */
        if (key == 0x15) { if (!ed_save()) { ed_ask = 0; ed_close(); } }                  /* Y */
        else if (key == 0x31) { ed_ask = 0; ed_dirty = 0; ed_close(); }                  /* N */
        else if (key == 0x01) ed_ask = 0;
        return;
    }
    switch (key) {
    case 0x01: ed_hs = 0; if (ed_dirty) ed_ask = 1; else ed_close(); return;
    case 0x3C: ed_save(); return;                                   /* F2 */
    case 0xC8: ed_vert(-1); return;
    case 0xD0: ed_vert(1); return;
    case 0xCB: ed_cur = ed_pc(ed_cur); if (ed_cur > 0 && ed_buf[ed_cur] == '\n' && ed_buf[ed_cur - 1] == '\r') ed_cur--; ed_want = ed_chars(ed_ls(ed_cur), ed_cur); return;
    case 0xCD: if (ed_buf[ed_cur] == '\r' && ed_cur + 1 < ed_len && ed_buf[ed_cur + 1] == '\n') ed_cur += 2; else ed_cur = ed_nc(ed_cur); ed_want = ed_chars(ed_ls(ed_cur), ed_cur); return;
    case 0xC7: if (ctrl) ed_cur = 0; else ed_cur = ed_ls(ed_cur); ed_want = 0; return;
    case 0xCF: if (ctrl) ed_cur = ed_len; else ed_cur = ed_le(ed_cur); ed_want = ed_chars(ed_ls(ed_cur), ed_cur); return;
    case 0xC9: for (int i = 0; i < ed_rows - 1; i++) ed_vert(-1); return;
    case 0xD1: for (int i = 0; i < ed_rows - 1; i++) ed_vert(1); return;
    case 0x0E: {                                                    /* Backspace */
        if (ed_cur <= 0) return;
        int a = ed_pc(ed_cur);
        if (ed_buf[ed_cur - 1] == '\n' && ed_cur > 1 && ed_buf[ed_cur - 2] == '\r') a = ed_cur - 2;
        ed_del(a, ed_cur); ed_want = ed_chars(ed_ls(ed_cur), ed_cur); return;
    }
    case 0xD3: {                                                    /* Del */
        if (ed_cur >= ed_len) return;
        int b = ed_nc(ed_cur);
        if (ed_buf[ed_cur] == '\r' && ed_cur + 1 < ed_len && ed_buf[ed_cur + 1] == '\n') b = ed_cur + 2;
        ed_del(ed_cur, b); return;
    }
    case 0x1C: case 0x9C: if (ed_crlf) ed_ins("\r\n", 2); else ed_ins("\n", 1); ed_want = 0; return;
    case 0x0F: ed_ins("    ", 4); ed_want = ed_chars(ed_ls(ed_cur), ed_cur); return;
    }
    if (key == 0x1F && ctrl) { ed_save(); return; }                 /* Ctrl+S */
    if (ctrl || (key & 0x80)) return;
    char c = key_char(key, (mods & 0x03) != 0);
    if (c && (uint8_t)c >= 32) { ed_ins(&c, 1); ed_want = ed_chars(ed_ls(ed_cur), ed_cur); }
}

/* 한 줄을 보이는 글로: 탭 → 공백 4개, \r 뺌. ci = 커서가 있는 바이트 자리 (줄 안), 돌려주는 값은 그 자리가 만든 글 안 위치 */
static int ed_expand(int ls, int le, int cur, char *out, int max, int *cpos)
{
    int n = 0;
    *cpos = -1;
    for (int p = ls; p < le && n < max - 5; p++) {
        if (p == cur) *cpos = n;
        char c = ed_buf[p];
        if (c == '\r') continue;
        if (c == '\t') { for (int k = 0; k < 4; k++) out[n++] = ' '; continue; }
        out[n++] = c;
    }
    if (cur >= le) *cpos = n;
    out[n] = 0;
    return n;
}
static int ed_align(const char *s, int i) { while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) i--; return i; }

static void draw_edit(void)
{
    UiRect o;
    char chip[64] = "", num[12];
    int ls0 = ed_ls(ed_cur), line = 1;
    for (int i = 0; i < ls0; i++) if (ed_buf[i] == '\n') line++;
    int col = ed_chars(ls0, ed_cur) + 1;
    int_to_str(num, sizeof num, line); str_append(chip, sizeof chip, num); str_append(chip, sizeof chip, " : ");
    int_to_str(num, sizeof num, col); str_append(chip, sizeof chip, num);
    if (ed_han) { str_append(chip, sizeof chip, "  "); str_append(chip, sizeof chip, T(S_ED_HAN)); }
    if (ed_dirty) { str_append(chip, sizeof chip, "  "); str_append(chip, sizeof chip, T(S_ED_MOD)); }
    UiRect b = ui_full_card(T(S_ED_TITLE), ed_name, chip, &o);
    hit_add(hc_none, 0, 0, 0, 0, o.x, o.y, o.w, o.h);
    uint32_t cy = dlg_cmd_y();
    uint32_t ax = b.x + 12 * S, ay = b.y + 8 * S, aw = b.w > 24 * S ? b.w - 24 * S : 1, ah = cy > ay + 12 * S ? cy - ay - 10 * S : 1;
    uint32_t lh = LH + 2 * S;
    int rows = (int)(ah / lh); if (rows < 1) rows = 1;
    ed_rows = rows;
    ui_rrect(ax, ay, aw, ah, 6 * S, C_FIELD);
    uint32_t gw = text_w("00000") + 14 * S, tx = ax + gw + 6 * S, tw = aw > gw + 16 * S ? aw - gw - 16 * S : 1;
    ui_rrect(ax, ay, gw, ah, 6 * S, C_HEAD);
    /* 커서가 보이게 (세로) */
    if (ed_topb > ls0) ed_topb = ls0;
    ed_topb = ed_ls(ed_topb);
    { int n = 0; for (int p = ed_topb; p < ls0; p = ed_nl(p)) n++; while (n > rows - 1) { ed_topb = ed_nl(ed_topb); n--; } }
    int topline = 1; for (int i = 0; i < ed_topb; i++) if (ed_buf[i] == '\n') topline++;
    /* 가로: 커서 줄로 ed_hcol 맞춤 */
    {
        static char ex[1100]; int cp;
        ed_expand(ls0, ed_le(ls0), ed_cur, ex, sizeof ex, &cp);
        if (cp < 0) cp = 0;
        if (ed_hcol > cp) ed_hcol = ed_align(ex, cp);
        for (;;) {
            char sv = ex[cp]; ex[cp] = 0;
            uint32_t w = text_w(ex + ed_hcol);
            ex[cp] = sv;
            if (w + 12 * S <= tw || ed_hcol >= cp) break;
            int nx = ed_hcol + 1; while (ex[nx] && ((uint8_t)ex[nx] & 0xC0) == 0x80) nx++;
            ed_hcol = nx;
        }
    }
    int p = ed_topb;
    for (int r = 0; r < rows; r++) {
        uint32_t y = ay + (uint32_t)r * lh;
        static char ex[1100]; int cp;
        int le = ed_le(p), q = p;
        while (q < ed_len && ed_buf[q] != '\n') q++;               /* q = 줄 끝 바이트 ('\n' 자리 또는 끝) */
        ed_expand(p, le, ed_cur, ex, sizeof ex, &cp);
        int sk = ed_hcol, el = (int)strlen(ex); if (sk > el) sk = ed_align(ex, el);
        char ln[12]; ln[0] = 0; int_to_str(num, sizeof num, topline + r); str_append(ln, sizeof ln, num);
        int onrow = ed_cur >= p && ed_cur <= q;
        text_f(SF, 1, ax + gw - 8 * S - ui_sw(ln), ui_sty(y, lh), ln, onrow ? C_WARN : C_FAINT, 0xFF, 0, 1);
        text_clip(tx, ui_ty(y, lh), ex + sk, C_TEXT, 0xFF, tw);
        if (onrow && !ed_ask) {
            if (cp < 0) cp = 0;
            char sv = ex[cp]; ex[cp] = 0;
            uint32_t cx = tx + (cp >= sk ? text_w(ex + sk) : 0);
            ex[cp] = sv;
            fe_fill(cx, y + 2 * S, 2 * S, lh - 4 * S, C_WARN);
        }
        if (q >= ed_len) break;
        p = q + 1;
    }
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, T(S_ED_KEYS));
    if (ed_ask) {
        uint32_t pw = 560 * S, ph = 2 * LH + ui_cmdbar_h() + 40 * S;
        if (pw > b.w) pw = b.w;
        uint32_t px = b.x + (b.w - pw) / 2, py = ay + ah / 4;
        hit_add(hc_none, 0, 0, 0, 0, px, py, pw, ph);
        ui_rrect(px + 5 * S, py + 6 * S, pw, ph, 10 * S, C_SHADOW);
        ui_rframe(px, py, pw, ph, 10 * S, C_ACC, C_CARD);
        text_f(F, S, px + 16 * S, py + 12 * S, T(S_ED_ASK), C_TEXT, 0xFF, 0, 1);
        small_ell(px + 16 * S, py + 12 * S + LH + 6 * S, ed_name, C_MUTED, C_CARD, pw - 32 * S);
        ui_cmdbar(px + S, py + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(S_ED_ASK_KEYS));
    }
}
