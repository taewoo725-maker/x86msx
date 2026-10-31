/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_sys.inc - 3.0.2 도움말 → 시스템 → 이미지 만들기 (대화 창 11), 도움말 (대화 창 12).
 * fe.c 안에 포함되어 그 정적 함수·변수를 쓴다. 이미지 형식은 dskimg.c, 도움말 본문은 help.h */

/* ================================================================== */
/* 이미지 만들기                                                        */
/* ================================================================== */
static int img_kind, img_fmt, img_hdk, img_size = 3, img_ins = 1, img_sel, img_busy, img_pct, img_ok;
static char img_name[40] = "NEWDISK";
static char img_msg[200];
enum { IMG_KIND = 1, IMG_FMT, IMG_SIZE, IMG_NAME, IMG_DIR, IMG_AFTER, IMG_GO };

static int img_rows(int *ids)
{
    int n = 0;
    ids[n++] = IMG_KIND; ids[n++] = IMG_FMT;
    if (img_kind) ids[n++] = IMG_SIZE;
    ids[n++] = IMG_NAME; ids[n++] = IMG_DIR; ids[n++] = IMG_AFTER; ids[n++] = IMG_GO;
    return n;
}
static void img_path(char *out, int max)
{
    copy_str(out, max, img_dir);
    str_append(out, max, "\\");
    str_append(out, max, img_name);
    str_append(out, max, img_kind ? ".hdd" : ".dsk");
}
static const char *img_hdk_name(int k) { return T(k == HDI_NEXTOR_FAT16 ? S_IMG_HDK_NEXTOR : k == HDI_DOS2_FAT12 ? S_IMG_HDK_DOS2 : S_IMG_HDK_BLANK); }
static void img_fix_size(void)
{
    if (!hd_size_ok(img_hdk, hd_sizes_mb[img_size])) {   /* 이 종류에 맞는 크기로 */
        for (int i = HD_SIZES - 1; i >= 0; i--) if (hd_size_ok(img_hdk, hd_sizes_mb[i]) && hd_sizes_mb[i] <= 512) { img_size = i; break; }
    }
}
static void img_value(int id, char *v, int max)
{
    v[0] = 0;
    switch (id) {
    case IMG_KIND: copy_str(v, max, T(img_kind ? S_IMG_HD : S_IMG_DISK)); break;
    case IMG_FMT: copy_str(v, max, img_kind ? img_hdk_name(img_hdk) : dsk_formats[img_fmt].name); break;
    case IMG_SIZE: {
        uint32_t mb = hd_sizes_mb[img_size];
        if (mb >= 1024) { num_append(v, max, (mb + 1) / 1024, 0, ' '); str_append(v, max, " GB"); }
        else { num_append(v, max, mb, 0, ' '); str_append(v, max, " MB"); }
        break;
    }
    case IMG_DIR: copy_str(v, max, img_dir[0] && img_dir[1] == ':' ? img_dir + 3 : img_dir); break;
    case IMG_NAME: copy_str(v, max, img_name); str_append(v, max, img_kind ? ".hdd" : ".dsk"); break;
    case IMG_AFTER: copy_str(v, max, T(img_ins == 0 ? S_IMG_NOINS : img_kind ? S_IMG_INS_HD : img_ins == 1 ? S_IMG_INS_A : S_IMG_INS_B)); break;
    case IMG_GO: copy_str(v, max, ""); break;
    }
}

static int img_dd_val[40], img_dd_y;
static void img_dd_open(int id)                         /* 이미지 만들기 값은 풀다운으로 고름 */
{
    int n = 0, cur = 0; char v[48];
    switch (id) {
    case IMG_KIND:
        for (int k = 0; k < 2; k++) { img_dd_val[n] = k; copy_str(dd_txt[n++], sizeof dd_txt[0], T(k ? S_IMG_HD : S_IMG_DISK)); }
        cur = img_kind; break;
    case IMG_FMT:
        if (img_kind) { for (int k = 0; k < HDI_KINDS && n < 40 && n < DD_MAX; k++) { img_dd_val[n] = k; copy_str(dd_txt[n++], sizeof dd_txt[0], img_hdk_name(k)); } cur = img_hdk; }
        else { for (int k = 0; k < DSK_FORMATS && n < 40 && n < DD_MAX; k++) { img_dd_val[n] = k; copy_str(dd_txt[n++], sizeof dd_txt[0], dsk_formats[k].name); } cur = img_fmt; }
        break;
    case IMG_SIZE:
        for (int k = 0; k < HD_SIZES && n < 40 && n < DD_MAX; k++) {
            if (!hd_size_ok(img_hdk, hd_sizes_mb[k])) continue;
            uint32_t mb = hd_sizes_mb[k]; v[0] = 0;
            if (mb >= 1024) { num_append(v, sizeof v, (mb + 1) / 1024, 0, ' '); str_append(v, sizeof v, " GB"); } else { num_append(v, sizeof v, mb, 0, ' '); str_append(v, sizeof v, " MB"); }
            if (k == img_size) cur = n;
            img_dd_val[n] = k; copy_str(dd_txt[n++], sizeof dd_txt[0], v);
        }
        break;
    case IMG_AFTER:
        for (int k = 0; k < (img_kind ? 2 : 3); k++) { img_dd_val[n] = k; copy_str(dd_txt[n++], sizeof dd_txt[0], T(k == 0 ? S_IMG_NOINS : img_kind ? S_IMG_INS_HD : k == 1 ? S_IMG_INS_A : S_IMG_INS_B)); }
        cur = img_ins; break;
    default: return;
    }
    dd_n = n; dd_sel = cur < n ? cur : 0; dd_top = 0; dd_id = 2000 + id; dd_on = n > 0;
    if (dd_sel >= 10) dd_top = dd_sel - 9;
}
static void img_dd_apply(int i)
{
    int id = dd_id - 2000, v = img_dd_val[i];
    switch (id) {
    case IMG_KIND: if (v != img_kind) { img_kind = v; img_ins = img_kind ? 0 : 1; if (img_kind) img_fix_size(); } break;
    case IMG_FMT: if (img_kind) { img_hdk = v; img_fix_size(); } else img_fmt = v; break;
    case IMG_SIZE: img_size = v; break;
    case IMG_AFTER: img_ins = v; break;
    }
    menu_dirty = 1;
}

static void img_draw_progress(int pct);
static void img_progress(int pct)
{
    img_pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    img_draw_progress(img_pct);
    fe_flush_now();
}

static void img_make(void)
{
    char path[FE_PATH_MAX + 64], dir[FE_PATH_MAX];
    img_path(path, sizeof path);
    copy_str(dir, sizeof dir, img_dir);
    img_ok = 0;
    if (!img_name[0]) { copy_str(img_msg, sizeof img_msg, T(S_IMG_FAIL)); return; }
    static uint8_t tiny[8];
    uint32_t n0 = 0;
    if (!P.read_file || !P.create || !P.read_file(path, tiny, sizeof tiny, &n0)) {   /* 이미 있으면 만들지 않음 */
        copy_str(img_msg, sizeof img_msg, T(S_IMG_EXISTS)); str_append(img_msg, sizeof img_msg, ": "); str_append(img_msg, sizeof img_msg, path + 3);
        return;
    }
    P.create(dir, 1);
    int rc = -1;
    if (!img_kind) {                                    /* 디스크: 이미지 전체를 만들어 씀 */
        static uint8_t ibuf[2880 * 512];
        uint32_t len = dsk_build(img_fmt, ibuf, img_name);
        if (len && !P.create(path, 0) && P.write_file && !P.write_file(path, ibuf, len)) rc = 0;
    } else {                                            /* 하드 디스크: 앞부분만 만들고 나머지는 자리만 */
        static uint8_t pbuf[320 * 1024];
        uint32_t mb = hd_sizes_mb[img_size], total = mb * 2048u;
        uint32_t plen = hd_build_prefix(img_hdk, total, pbuf, sizeof pbuf, img_name);
        if (plen && P.make_file) {
            img_busy = 1; img_pct = 0;
            img_progress(0);
            rc = P.make_file(path, total * 512u, pbuf, plen, img_progress);
            img_busy = 0;
        }
    }
    full_redraw();
    menu_dirty = 1;
    if (rc) { copy_str(img_msg, sizeof img_msg, T(img_kind ? S_IMG_NOSPACE : S_IMG_FAIL)); str_append(img_msg, sizeof img_msg, ": "); str_append(img_msg, sizeof img_msg, path + 3); return; }
    img_ok = 1;
    copy_str(img_msg, sizeof img_msg, T(S_IMG_DONE)); str_append(img_msg, sizeof img_msg, ": "); str_append(img_msg, sizeof img_msg, path + 3);
    char disp[64];
    copy_str(disp, sizeof disp, img_name); str_append(disp, sizeof disp, img_kind ? ".hdd" : ".dsk");
    if (img_ins && !img_kind) {
        if (disk_drives() >= img_ins) insert_disk_file(img_ins - 1, path, disp, 0);
    } else if (img_ins && img_kind) hd_insert(path, disp, 0);
    /* 다음 이름: 끝의 숫자를 하나 올림 */
    int l = (int)strlen(img_name), k = l;
    while (k > 0 && img_name[k - 1] >= '0' && img_name[k - 1] <= '9') k--;
    uint32_t num = 0;
    for (int i = k; i < l; i++) num = num * 10 + (uint32_t)(img_name[i] - '0');
    img_name[k] = 0;
    num_append(img_name, sizeof img_name, num + 1, l - k > 0 ? (uint32_t)(l - k) : 2, '0');
}

static void img_change(int id, int dir)
{
    switch (id) {
    case IMG_KIND: img_kind = !img_kind; img_ins = img_kind ? 0 : 1; if (img_kind) img_fix_size(); break;
    case IMG_FMT:
        if (img_kind) { img_hdk = (img_hdk + (dir < 0 ? HDI_KINDS - 1 : 1)) % HDI_KINDS; img_fix_size(); }
        else img_fmt = (img_fmt + (dir < 0 ? DSK_FORMATS - 1 : 1)) % DSK_FORMATS;
        break;
    case IMG_SIZE:
        for (int k = 1; k <= HD_SIZES; k++) {
            int i = (img_size + (dir < 0 ? HD_SIZES - k : k)) % HD_SIZES;
            if (hd_size_ok(img_hdk, hd_sizes_mb[i])) { img_size = i; break; }
        }
        break;
    case IMG_AFTER: img_ins = img_kind ? !img_ins : (img_ins + (dir < 0 ? 2 : 1)) % 3; break;
    case IMG_GO: if (dir == 0) img_make(); break;
    }
    menu_dirty = 1;
}

static void open_image_dialog(void)
{
    img_sel = 0; img_msg[0] = 0; img_busy = 0;
    open_dialog(11);
}

static void draw_image(void)
{
    UiRect o;
    UiRect b = ui_full_card(T(S_IMG_TITLE), T(S_IMG_SUB), 0, &o);
    uint32_t pad = 14 * S, X = b.x + pad, W = b.w - 2 * pad, y = b.y + 12 * S, rh = ui_row_h(), vx = X + W * 30 / 100;
    int ids[9], n = img_rows(ids);
    if (img_sel >= n) img_sel = n - 1;
    static const StrId lab[8] = { 0, S_IMG_KIND, S_IMG_FMT, S_IMG_SIZE, S_IMG_NAME, S_IMG_DIR, S_IMG_AFTER, S_IMG_MAKE };
    for (int i = 0; i < n; i++) {
        int id = ids[i], f = i == img_sel;
        char v[96];
        img_value(id, v, sizeof v);
        if (id == IMG_GO) {                             /* 만들기 = 실행 줄 */
            y += 6 * S;
            ui_row(X, y, W, vx, T(lab[id]), "", 0, UV_ACTION, 0, f, 1);
            hit_add(hc_val, (intptr_t)&img_sel, i, 0, 0x1C, X, y, W, rh);
        } else if (id == IMG_NAME) {
            ui_row(X, y, W, vx, T(lab[id]), "", 0, UV_TEXT, 0, f, 1);
            {                                           /* 3.0.2: 입력 칸 + 이름 끝의 커서 + 흐린 확장자 */
                uint32_t fw = text_w("WWWWWWWWWWWWWWWWWWWWWWWW") * 2 / 3 + 20 * S, fh = LH + 6 * S, fy = y + (rh - fh) / 2;
                ui_rrect(vx, fy, fw, fh, 4 * S, f ? C_FIELD : C_HEAD);
                uint32_t tw = text_clip(vx + 10 * S, ui_ty(y, rh), img_name, C_TEXT, 0xFF, fw - 20 * S);
                if (f) fe_fill(vx + 10 * S + tw + S, ui_ty(y, rh), 2 * S, LH, C_ACC);
                text_clip(vx + fw + 10 * S, ui_ty(y, rh), img_kind ? ".hdd" : ".dsk", C_MUTED, 0xFF, 0);
            }
            hit_add(hc_val, (intptr_t)&img_sel, i, 0, 0x1C, X, y, W, rh);
        } else {
            ui_row(X, y, W, vx, T(lab[id]), v, 0, UV_DROP, 0, f, 1);
            hit_add(hc_val, (intptr_t)&img_sel, i, 0, 0x1C, X, y, W, rh);
            if (f) img_dd_y = y + rh;
        }
        y += rh;
    }
    y += 10 * S;
    char path[160];
    img_path(path, sizeof path);
    small_ell(X + 18 * S, y, T(S_IMG_WHERE), C_MUTED, 0xFF, W / 4);
    small_ell(vx + 14 * S, y, path + 3, C_TEXT, 0xFF, W - (vx - X) - 20 * S);
    y += SLH + 10 * S;
    if (!img_kind) {                                    /* 형식 설명: 크기 */
        const DskFormat *f = &dsk_formats[img_fmt];
        char t[96] = "";
        num_append(t, sizeof t, (uint32_t)f->sectors / 2, 0, ' '); str_append(t, sizeof t, " KB  ·  ");
        num_append(t, sizeof t, f->tracks, 0, ' '); str_append(t, sizeof t, " x ");
        num_append(t, sizeof t, f->heads, 0, ' '); str_append(t, sizeof t, " x ");
        num_append(t, sizeof t, f->spt, 0, ' '); str_append(t, sizeof t, " x 512");
        small_ell(vx + 14 * S, y, t, C_MUTED, 0xFF, W - (vx - X) - 20 * S);
        y += SLH + 10 * S;
    }
    y += 6 * S;
    small_ell(X + 18 * S, y, T(S_IMG_NOTE), C_MUTED, 0xFF, W - 36 * S);
    y += SLH + 16 * S;
    if (img_msg[0]) {
        uint32_t mh = LH + 16 * S;
        ui_rrect(X, y, W, mh, 6 * S, C_HEAD);
        ui_rrect(X + 10 * S, y + (mh - LH / 2) / 2, LH / 2, LH / 2, LH / 4, img_ok ? C_OK : C_BAD);
        text_clip(X + 20 * S + LH / 2, ui_ty(y, mh), img_msg, C_TEXT, 0xFF, W - LH - 30 * S);
    }
    if (dd_on && dd_id >= 2000) dd_draw_at(vx, img_dd_y, W * 45 / 100, D.h);
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, T(S_IMG_KEYS));
}

static void img_draw_progress(int pct)
{
    uint32_t w = out_w * 2 / 3, h = ui_head_h() + LH * 3 + 40 * S;
    uint32_t x = x0 + (out_w - w) / 2, y = (D.h - h) / 2;
    UiRect b = ui_card(x, y, w, h, T(S_IMG_BUSY), img_name, 0);
    uint32_t bx = b.x + 20 * S, bw = b.w - 40 * S, by = b.y + 20 * S;
    char t[16] = "";
    num_append(t, sizeof t, (uint32_t)pct, 0, ' '); str_append(t, sizeof t, " %");
    fe_fill(bx, by, bw, LH, C_CARD);
    text_clip(bx, by, t, C_TEXT, 0xFF, bw);
    by += LH + 10 * S;
    ui_rrect(bx, by, bw, LH, LH / 2, C_FIELD);
    uint32_t k = (bw - 4 * S) * (uint32_t)pct / 100;
    if (k) ui_rrect(bx + 2 * S, by + 2 * S, k, LH - 4 * S, (LH - 4 * S) / 2, C_OK);
}

static void image_key(int key)
{
    menu_dirty = 1;
    if (img_busy) return;
    if (dd_on && dd_id >= 2000) { dd_key_event(key); return; }
    int ids[9], n = img_rows(ids), shift = (mods & 0x03) != 0;
    if (img_sel >= n) img_sel = n - 1;
    int id = ids[img_sel];
    switch (key) {
    case 0x01: dialog_back(); return;
    case 0xC8: if (img_sel > 0) img_sel--; return;
    case 0xD0: case 0x0F: if (img_sel < n - 1) img_sel++; else if (key == 0x0F) img_sel = 0; return;
    case 0xC7: img_sel = 0; return;
    case 0xCF: img_sel = n - 1; return;
    case 0xCB: case 0xCD: return;                       /* 값은 풀다운으로 고름 (←/→ 없음) */
    case 0x1C: case 0x9C:
        if (id == IMG_DIR) open_browser(26);            /* 3.8.1: 저장 폴더 찾아보기 */
        else if (id == IMG_KIND || id == IMG_FMT || id == IMG_SIZE || id == IMG_AFTER) img_dd_open(id);
        else img_make();
        return;
    }
    if (id != IMG_NAME) return;
    int l = (int)strlen(img_name);
    if (key == 0x0E) { if (l) img_name[l - 1] = 0; return; }
    char c = key_char(key, shift);                      /* 파일 이름에 쓸 수 있는 글자만 */
    if (c && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ') && l < 24) {
        img_name[l] = c; img_name[l + 1] = 0;
    }
}

/* ================================================================== */
/* 도움말                                                               */
/* ================================================================== */
static int help_sel, help_scroll, help_total;
static const StrId help_topic[HELP_TOPICS] = { S_HT_START, S_HT_KEYS, S_HT_MOUSE, S_HT_ROM, S_HT_MEDIA, S_HT_IMAGE, S_HT_MACHINE,
                                               S_HT_STATE, S_HT_AV, S_HT_JOY, S_HT_USB, S_HT_SHORTCUTS, S_HT_TROUBLE };
static char help_sc[12288];                             /* 단축키 주제 본문 (열 때 지금 단축키 표로 만듦) */

static void help_build_shortcuts(void)
{
    help_sc[0] = 0;
    sc_ensure();
    for (int i = 0; i < sc_n; i++) {
        if (sc_def[i].action == A_NOT_YET || !sc_tab[i].vk) continue;
        char a[128], k[64];
        action_name(sc_def[i].action, a, sizeof a);
        combo_name(sc_tab[i].vk, sc_tab[i].mods, k, sizeof k);
        str_append(help_sc, sizeof help_sc, "- [");
        str_append(help_sc, sizeof help_sc, k);
        str_append(help_sc, sizeof help_sc, "]  ");
        str_append(help_sc, sizeof help_sc, a);
        str_append(help_sc, sizeof help_sc, "\n");
    }
}
static const char *help_text(int t)
{
    if (t == HELP_SHORTCUTS) return help_sc;
    return lang == 0 ? help_ko[t] : help_en[t];
}

/* 본문 그리기 (draw = 0이면 높이만). 위에서 scroll 점만큼 올려, [top, bottom) 안에만 그림 */
static uint32_t help_render(uint32_t X, uint32_t W, uint32_t top, uint32_t bottom, int scroll, const char *s, int draw)
{
    int32_t y = (int32_t)top - scroll;
    uint32_t lh = LH + 6 * S, ch = LH + 2 * S;
    while (*s) {
        const char *e = s;
        while (*e && *e != '\n') e++;
        char line[1200];
        int len = (int)(e - s) < (int)sizeof line - 1 ? (int)(e - s) : (int)sizeof line - 1;
        memcpy(line, s, (size_t)len); line[len] = 0;
        s = *e ? e + 1 : e;
        if (!len) { y += (int32_t)(lh / 2); continue; }
        if (line[0] == '#' && line[1] == ' ') {          /* 소제목 */
            y += (int32_t)(6 * S);
            if (draw && y >= (int32_t)top && y + (int32_t)(SLH + 12 * S) <= (int32_t)bottom) ui_section(X, (uint32_t)y, W, line + 2);
            y += (int32_t)(SLH + 12 * S);
            continue;
        }
        int bullet = line[0] == '-' && line[1] == ' ';
        const char *p = bullet ? line + 2 : line;
        uint32_t indent = bullet ? 22 * S : 4 * S, x = X + indent;
        if (bullet && draw && y >= (int32_t)top && y + (int32_t)lh <= (int32_t)bottom)
            ui_rrect(X + 8 * S, (uint32_t)y + LH / 2 - 2 * S, 5 * S, 5 * S, 2 * S, C_ACC);
        while (*p) {                                    /* 낱말·[키] 단위로 줄 바꿈 */
            char word[128]; int wl = 0, chip = 0;
            if (*p == '[') {
                const char *q = p + 1;
                while (*q && *q != ']' && wl < 120) word[wl++] = *q++;
                if (*q == ']') { chip = 1; p = q + 1; } else { wl = 0; }
            }
            if (!chip) {
                while (*p && *p != ' ' && *p != '[' && wl < 120) word[wl++] = *p++;
                if (!wl && *p == '[') word[wl++] = *p++;
            }
            word[wl] = 0;
            uint32_t ww = chip ? ui_sw(word) + 12 * S : text_w(word);
            if (x + ww > X + W && x > X + indent) { x = X + indent; y += (int32_t)lh; }
            if (draw && y >= (int32_t)top && y + (int32_t)lh <= (int32_t)bottom) {
                if (chip) ui_keychip(x, (uint32_t)y - (ch - LH) / 2 + S, ch, word, 0);
                else text_f(F, S, x, (uint32_t)y, word, C_TEXT, 0xFF, 0, 1);
            }
            x += ww;
            if (*p == ' ') { x += text_w(" "); while (*p == ' ') p++; }
        }
        y += (int32_t)lh;
    }
    return (uint32_t)(y + scroll - (int32_t)top);
}

static void open_help(void)
{
    help_build_shortcuts();
    help_scroll = 0;
    open_dialog(12);
}

static void draw_help(void)
{
    UiRect o;
    char sub[64] = "X86MSX ";
    str_append(sub, sizeof sub, MSXEMU_VERSION);
    UiRect b = ui_full_card(T(S_HELP_TITLE), sub, 0, &o);
    uint32_t pad = 14 * S, top = b.y + 12 * S, bottom = dlg_cmd_y() - 8 * S;
    uint32_t lw = b.w * 26 / 100, LX = b.x + pad, rh = list_rh();
    /* 왼쪽: 주제 */
    for (int i = 0; i < HELP_TOPICS; i++) {
        uint32_t y = top + (uint32_t)i * rh;
        if (y + rh > bottom) break;
        int sel = i == help_sel;
        ui_list_row_bg(LX, y, lw, rh, sel, 0, 1);
        text_clip(LX + 12 * S, ui_ty(y, rh), T(help_topic[i]), sel ? 15 : C_TEXT, 0xFF, lw - 20 * S);
        hit_add(hc_sel, (intptr_t)&help_sel, i, 1, 0x1C, LX, y, lw, rh);
    }
    fe_fill(LX + lw + 12 * S, top, S, bottom - top, C_LINE);
    /* 오른쪽: 본문 */
    uint32_t TX = LX + lw + 30 * S, TW = b.x + b.w - pad - TX - 10 * S;
    const char *s = help_text(help_sel);
    help_total = (int)help_render(TX, TW, top, bottom, 0, s, 0);
    int maxs = help_total > (int)(bottom - top) ? help_total - (int)(bottom - top) : 0;
    if (help_scroll > maxs) help_scroll = maxs;
    if (help_scroll < 0) help_scroll = 0;
    text_clip(TX, top, T(help_topic[help_sel]), C_ACC, 0xFF, TW);
    uint32_t t2 = top + LH + 14 * S;
    help_render(TX, TW, t2, bottom, help_scroll, s, 1);
    if (maxs) ui_sbar(b.x + b.w - pad - 4 * S, t2, bottom - t2, &help_scroll, 0, (int)(bottom - t2), maxs + (int)(bottom - t2));   /* 3.4.4: 끌기 (픽셀 단위) */
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, T(S_HELP_KEYS));
}

static void help_key(int key)
{
    menu_dirty = 1;
    int page = (int)(D.h / 2);
    switch (key) {
    case 0x01: dialog_back(); return;
    case 0xC8: if (help_sel > 0) { help_sel--; help_scroll = 0; } return;
    case 0xD0: case 0x0F: if (help_sel < HELP_TOPICS - 1) { help_sel++; help_scroll = 0; } return;
    case 0xC9: help_scroll -= page; return;
    case 0xD1: case 0x39: help_scroll += page; return;
    case 0xC7: help_scroll = 0; return;
    case 0xCF: help_scroll = 1 << 20; return;
    case 0xCB: help_scroll -= (int)(LH * 2); return;
    case 0xCD: help_scroll += (int)(LH * 2); return;
    }
}

/* ================================================================== */
/* 디스크 파일 복사 (3.0.2): 왼쪽 = 디스크 이미지(.dsk) 안, 오른쪽 = USB 폴더   */
/* ================================================================== */
#define DC_MAX 256
#ifdef FE_UEFI
#define DCU_MAX 8192                                    /* 4.0.126: UEFI는 USB 폴더 창에 8192개까지 한 쪽에 (ROM 선택 창과 같은 이름 색인 방식) */
#else
#define DCU_MAX 256
#endif
/* 3.7.0: 이미지 파일 관리 — 왼쪽 = 디스크 이미지(FAT12, 메모리) 또는 하드 디스크 이미지(FAT16 = fat.c로 섹터 단위,
 * FAT12 파티션 = 작업 메모리에 올려 msxfat), 오른쪽 = USB 폴더 (처음 0:\DISK\DirFolder). 양쪽 복사·지우기(묻고)·이름 바꾸기 */
enum { DCK_NONE, DCK_MF, DCK_FAT, DCK_PART };          /* DCK_PART = 하드 디스크 파티션 목록 */
static int dc_pane, dc_sel[2], dc_top[2];
static uint8_t dc_mark[2][DCU_MAX + 1];
static char dc_upath[2][PATH_MAX_FE], dc_udisp[2][PATH_MAX_FE];   /* [0] 이미지 고르는 폴더, [1] USB 폴더 */
static FeDirEntry dc_ue[2][DCU_MAX];
static int dc_un[2], dc_uidx[2][DCU_MAX];
/* 4.0.34: 파일이 DC_MAX(256)개보다 많은 폴더는 '창'으로 나눠 읽음 — 끝에서 ↓ 하면 다음 창, 처음에서 ↑ 하면 앞 창 (창 안은 폴더 먼저·이름순) */
#define DC_WIN_HIST 64
static int dc_pbase[2], dc_pnext[2] = { -1, -1 }, dc_phist[2][DC_WIN_HIST], dc_pdepth[2];
static char dc_ploaded[2][PATH_MAX_FE];
static int dc_init;
static int dc_mem_usb;                                  /* 지난번에 왼쪽 창을 USB 폴더(zip 포함)로 보다 닫았음 → 끼운 디스크를 자동으로 열지 않음 */
static int dc_inside;                                   /* 이미지 파일 관리 창에 들어와 있음 */
static char dc_mem_sel[2][FE_KEY_MAX];                  /* 4.0.31: 닫을 때 커서가 있던 이름 (zip 안이면 그 zip) — 다시 열면 그 자리 */
static char dc_ini_path[2][PATH_MAX_FE], dc_ini_disp[2][PATH_MAX_FE];   /* 4.0.31: 지난번(껐다 켜도) 두 창의 폴더 */
/* 이미지 열기 상태는 창마다 따로 (양쪽 창이 모두 디스크·하드 이미지를 열 수 있음). dcc = 지금 다루는 창의 이미지.
 * 기존 이름(dc_open·dc_kind …)은 dcc 의 칸으로 바꿔 씀. DCI(p) = 창 p가 이미지를 열었으면 1이고 dcc 를 그 창으로 돌림 */
typedef struct DcImg {
    int open, kind, drive, unit, hd, pane;              /* 이미지 열림·종류, 드라이브 번호, 하드 디스크 유닛, 하드 디스크 이미지, 창 번호 */
    char path[PATH_MAX_FE], name[64];
    uint8_t *ip;
    uint32_t size;
    MsxFat fs;
    uint16_t dstack[8];
    char dname[8][13];
    int depth;
    MfEntry me[DC_MAX];
    int mn;
    FatVol vols[1];                                     /* 지금 연 FAT16 파티션 */
    int vol;                                            /* 지금 연 파티션 번호 (-1 = 파티션 목록) */
    int nparts, pf16[4];                                /* 하드 디스크 파티션: 수, FAT16(1)/FAT12(0) */
    uint32_t plba[4], psecs[4];
    char plabel[4][12];
    char fpath[PATH_MAX_FE];                            /* 파티션 안 폴더 ("" = 맨 위) */
    FatEntry fe[DC_MAX];
    uint32_t part_lba, part_secs;                       /* FAT12 하드 디스크: 작업 메모리에 올린 파티션 */
} DcImg;
static DcImg dci[2] = { { .drive = -1, .unit = -1, .vol = -1 }, { .drive = -1, .unit = -1, .vol = -1, .pane = 1 } };
static DcImg *dcc = &dci[0];
#define dc_open (dcc->open)
#define dc_kind (dcc->kind)
#define dc_drive (dcc->drive)
#define dc_unit (dcc->unit)
#define dc_hd (dcc->hd)
#define dc_img_path (dcc->path)
#define dc_img_name (dcc->name)
#define dc_ip (dcc->ip)
#define dc_img_size (dcc->size)
#define dc_fs (dcc->fs)
#define dc_dstack (dcc->dstack)
#define dc_dname (dcc->dname)
#define dc_depth (dcc->depth)
#define dc_me (dcc->me)
#define dc_mn (dcc->mn)
#define dc_vols (dcc->vols)
#define dc_vol (dcc->vol)
#define dc_nparts (dcc->nparts)
#define dc_pf16 (dcc->pf16)
#define dc_plba (dcc->plba)
#define dc_psecs (dcc->psecs)
#define dc_plabel (dcc->plabel)
#define dc_fpath (dcc->fpath)
#define dc_fe (dcc->fe)
#define dc_part_lba (dcc->part_lba)
#define dc_part_secs (dcc->part_secs)
static inline int dc_img_on(int p) { if (!dci[p].open) return 0; dcc = &dci[p]; return 1; }
#define DCI(p) dc_img_on(p)
static uint8_t dc_img[DISK_MAX], dc_buf[DISK_MAX];     /* 디스크 이미지 자리: 왼쪽 창 = dc_img, 오른쪽 창 = dc_buf */
static uint8_t *dc_fbuf(int p) { return p ? dc_buf : dc_img; }
static uint32_t dc_sum[65536];                          /* FAT12 하드 파티션(작업 메모리) 섹터별 검사합 — 한 번에 한 창만 작업 메모리를 씀 */
static int dc_scr_owner(void) { for (int i = 0; i < 2; i++) if (dci[i].open && dci[i].hd && dci[i].kind == DCK_MF) return i; return -1; }
static char dc_msg[240];
static int dc_ok;
/* 4.0.3: zip을 폴더처럼 열기 — zip 파일은 작업 메모리 맨 위 8 MiB 자리에 올림 (복사 버퍼는 그 아래) */
#define DC_ZRES (8u << 20)
static int dc_zip[2];                                   /* 4.0.31: 창마다 zip 열림 (양쪽 창이 똑같이 zip을 열 수 있음) */
static char dc_zname[2][FE_KEY_MAX], dc_zdir[2][PATH_MAX_FE]; /* zip 파일 이름, zip 안 폴더 ('\\'로 이음, "" = 맨 위) */
static uint32_t dc_zlen[2];
static uint8_t *dc_zbuf(int p) { return P.scratch + P.scratch_size - DC_ZRES * (uint32_t)(p + 1); }   /* 왼쪽 = 맨 뒤 8 MiB, 오른쪽 = 그 앞 8 MiB */
static uint32_t dc_zres(void) { return dc_zip[1] ? DC_ZRES * 2 : dc_zip[0] ? DC_ZRES : 0; }
static uint32_t dc_smax(void) { return P.scratch_size - dc_zres(); }
static int dc_ask, dc_ask_n;                            /* dc_ask: 1 = 지울까요?, 2 = 덮어쓸까요? (4.0.28) */
static char dc_ovr_name[64];                            /* 지울까요? 팝업 */
static int dc_ren;                                      /* 이름 바꾸기 팝업 */
static char dc_ren_name[64], dc_ren_old[64];

static int dc_img_count(void) { return dc_kind == DCK_PART ? dc_nparts : dc_mn; }
/* 창 p의 줄 수 (맨 위 ".." 포함) */
static int dc_has_up(int p) { return dc_zip[p] || dci[p].open || dc_upath[p][0] != 0; }
static int dc_count(int p) { return dc_has_up(p) + (DCI(p) ? dc_img_count() : dc_un[p]); }
/* 줄 → 항목 번호 (-1 = "..") */
static int dc_item(int p, int row) { int u = dc_has_up(p); if (row < u) return -1; row -= u; return DCI(p) ? row : dc_uidx[p][row]; }
static int dc_img_isdir(int i) { return dc_kind == DCK_PART ? 1 : dc_kind == DCK_FAT ? dc_fe[i].is_dir : dc_me[i].is_dir; }
static const char *dc_img_iname(int i)
{
    static char t[48];
    if (dc_kind == DCK_PART) {                          /* 파티션 n  FAT16 (라벨) */
        copy_str(t, sizeof t, T(S_DC_PART)); str_append(t, sizeof t, " "); num_append(t, sizeof t, (uint32_t)i + 1, 0, ' ');
        str_append(t, sizeof t, dc_pf16[i] ? "  FAT16" : "  FAT12");
        if (dc_plabel[i][0]) { str_append(t, sizeof t, "  "); str_append(t, sizeof t, dc_plabel[i]); }
        return t;
    }
    return dc_kind == DCK_FAT ? (dc_fe[i].key[0] ? dc_fe[i].key : dc_fe[i].disp) : dc_me[i].name;
}
static uint32_t dc_img_isize(int i) { return dc_kind == DCK_PART ? 0 : dc_kind == DCK_FAT ? dc_fe[i].size : dc_me[i].size; }
static int dc_row_dir(int p, int row)
{
    int it = dc_item(p, row);
    if (it < 0) return 1;
    return DCI(p) ? dc_img_isdir(it) : dc_ue[p][it].is_dir;
}
static const char *dc_row_name(int p, int row)
{
    int it = dc_item(p, row);
    if (it < 0) return "..";
    return DCI(p) ? dc_img_iname(it) : dc_ue[p][it].name;
}

static int dc_is_image_name(const char *nm) { return ext_is(nm, "dsk", "di1", "di2") || ext_is(nm, "360", "720", "hdd") || ext_is(nm, "ide", "hdi", 0); }   /* 4.0.30: .hdi 추가 (아이콘은 없음) */
static void dc_sort_usb(int p, int n)
{
    dc_un[p] = n;
    for (int i = 0; i < n; i++) dc_uidx[p][i] = i;
    for (int gap = n / 2; gap > 0; gap /= 2)            /* 폴더 먼저, 이름 순 (4.0.126: 셸 정렬) */
        for (int i = gap; i < n; i++) {
            int t = dc_uidx[p][i], j = i;
            while (j >= gap) {
                FeDirEntry *x = &dc_ue[p][dc_uidx[p][j - gap]], *y = &dc_ue[p][t];
                if (!(x->is_dir != y->is_dir ? (!x->is_dir && y->is_dir) : name_cmp(x->name, y->name) > 0)) break;
                dc_uidx[p][j] = dc_uidx[p][j - gap]; j -= gap;
            }
            dc_uidx[p][j] = t;
        }
}
typedef struct { FeDirEntry *ue; int n; } DcZFill;
static int dc_zfill_ue(void *c, const ZipEnt *z)        /* 4.0.3: zip 항목 → 창 목록 */
{
    DcZFill *f = c;
    for (int j = 0; j < f->n; j++) if (f->ue[j].is_dir == z->dir && !strcmp(f->ue[j].key, z->name)) return 0;   /* 같은 이름은 한 번만 */
    if (f->n >= DCU_MAX) return 1;
    FeDirEntry *x = &f->ue[f->n++];
    memset(x, 0, sizeof *x);
    int L = (int)strlen(z->name), k = L < 63 ? L : 63;
    while (k > 0 && k < L && ((uint8_t)z->name[k] & 0xC0) == 0x80) k--;       /* 글자 중간에서 자르지 않음 */
    memcpy(x->name, z->name, (size_t)k); x->name[k] = 0;
    copy_str(x->key, sizeof x->key, z->name);
    x->is_dir = z->dir; x->size = z->size;
    x->year = z->year; x->mon = z->mon; x->day = z->day; x->hour = z->hour; x->min = z->min;
    return 0;
}
static void dc_msg_set(int ok, const char *a, const char *b);
#ifdef FE_UEFI
/* 4.0.126: 큰 폴더 = 이름 색인(dcx)을 만들어 정렬하고, 정렬 순위 쪽(dc_pbase = 순위)을 자세히 읽음 */
static CxItem dcx[2][CX_MAX];
static uint16_t dcx_ord[2][CX_MAX];
static int dcx_n[2], dcx_big[2], dcx_cut[2];
static char dcx_path[2][PATH_MAX_FE];
static int dcx_less(int p, int a, int b) { return ent_cmp(dcx[p][a].is_dir, dcx[p][a].name, dcx[p][b].is_dir, dcx[p][b].name) < 0; }
static int dc_raw(int p, int skip, int k)               /* dc_ue[p]+k 에 원본 목록을 skip번째부터 읽음 (읽은 수, 오류 -1), 남았으면 fe_list_next */
{
    fe_list_skip = skip; fe_list_next = -1;
    int n = P.list_dir_all ? P.list_dir_all(dc_upath[p], dc_ue[p] + k, DCU_MAX - k) : P.list_dir ? P.list_dir(dc_upath[p], dc_ue[p] + k, DCU_MAX - k) : -1;
    fe_list_skip = 0;
    return n;
}
static void dcx_build(int p)
{
    dcx_n[p] = 0; dcx_cut[p] = 0;
    int skip = 0;
    for (int g = 0; g < 100000; g++) {
        int n = dc_raw(p, skip, 0), nx = n >= DCU_MAX ? fe_list_next : -1;
        if (n < 0) break;
        for (int i = 0; i < n; i++) {
            if (dcx_n[p] >= CX_MAX) { dcx_cut[p] = 1; break; }
            copy_str(dcx[p][dcx_n[p]].name, sizeof dcx[p][0].name, dc_ue[p][i].name);
            dcx[p][dcx_n[p]].is_dir = dc_ue[p][i].is_dir;
            dcx_ord[p][dcx_n[p]] = (uint16_t)dcx_n[p]; dcx_n[p]++;
        }
        if (nx < 0 || dcx_cut[p]) break;
        skip = nx;
    }
    int n = dcx_n[p];
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; i++) {
            uint16_t t = dcx_ord[p][i]; int j = i;
            while (j >= gap && dcx_less(p, t, dcx_ord[p][j - gap])) { dcx_ord[p][j] = dcx_ord[p][j - gap]; j -= gap; }
            dcx_ord[p][j] = t;
        }
    copy_str(dcx_path[p], sizeof dcx_path[p], dc_upath[p]);
}
static int dcx_page(int p, int pb)
{
    int pe = pb + (DCU_MAX - 8); if (pe > dcx_n[p]) pe = dcx_n[p];
    if (pb >= pe) return 0;
    const CxItem *lo = &dcx[p][dcx_ord[p][pb]], *hi = &dcx[p][dcx_ord[p][pe - 1]];
    int skip = 0, k = 0;
    for (int g = 0; g < 100000 && k < pe - pb; g++) {
        int n = dc_raw(p, skip, k), nx = n >= DCU_MAX - k ? fe_list_next : -1;
        if (n < 0) break;
        int kk = k;
        for (int i = k; i < k + n; i++) {
            if (ent_cmp(dc_ue[p][i].is_dir, dc_ue[p][i].name, lo->is_dir, lo->name) < 0 || ent_cmp(dc_ue[p][i].is_dir, dc_ue[p][i].name, hi->is_dir, hi->name) > 0) continue;
            if (kk != i) dc_ue[p][kk] = dc_ue[p][i];
            kk++;
        }
        k = kk;
        if (nx < 0) break;
        skip = nx;
    }
    return k;
}
#endif
static int dc_win_read(int p)                            /* 지금 창(dc_pbase)을 읽어 dc_ue에 (줄 수) */
{
    int n;
#ifdef FE_UEFI
    if (dcx_big[p] && dc_pbase[p] > 0 && dc_pbase[p] < dcx_n[p] && !strcmp(dcx_path[p], dc_upath[p])) {   /* 큰 폴더의 둘째 쪽부터 */
        n = dcx_page(p, dc_pbase[p]);
        dc_pnext[p] = dc_pbase[p] + (DCU_MAX - 8) < dcx_n[p] ? dc_pbase[p] + (DCU_MAX - 8) : -1;
        return n;
    }
    if (dcx_big[p] && dc_pbase[p] > 0) { dc_pbase[p] = 0; dc_pdepth[p] = 0; }
    dcx_big[p] = 0;
    n = dc_raw(p, dc_pbase[p], 0);
    dc_pnext[p] = n >= DCU_MAX ? fe_list_next : -1;
    if (n >= 0 && dc_pbase[p] == 0 && dc_pnext[p] >= 0) {   /* 한 쪽에 안 들어가는 폴더: 색인을 만들어 순위 순서로 */
        dcx_build(p);
        n = dcx_page(p, 0);
        dcx_big[p] = dcx_n[p] > DCU_MAX - 8;
        dc_pnext[p] = dcx_big[p] ? DCU_MAX - 8 : -1;
    }
    return n;
#else
    fe_list_skip = dc_pbase[p]; fe_list_next = -1;
    n = P.list_dir_all ? P.list_dir_all(dc_upath[p], dc_ue[p], DC_MAX) : P.list_dir ? P.list_dir(dc_upath[p], dc_ue[p], DC_MAX) : -1;
    fe_list_skip = 0;
    dc_pnext[p] = n >= DC_MAX ? fe_list_next : -1;
    return n;
#endif
}
static void dc_load_usb(int p, const char *sel_key)
{
    int n;
    if (strcmp(dc_ploaded[p], dc_upath[p]) || dc_zip[p]) { dc_pbase[p] = 0; dc_pdepth[p] = 0; }   /* 다른 폴더면 처음 창부터 */
    copy_str(dc_ploaded[p], sizeof dc_ploaded[p], dc_upath[p]);
    dc_pnext[p] = -1;
    if (dc_zip[p]) {                                    /* 4.0.3: zip 안 폴더의 바로 아래 항목 */
        char zd[PATH_MAX_FE];
        copy_str(zd, sizeof zd, dc_zdir[p]);
        for (char *c = zd; *c; c++) if (*c == '\\') *c = '/';
        DcZFill f = { dc_ue[p], 0 };
        n = zip_list(dc_zbuf(p), dc_zlen[p], zd, dc_zfill_ue, &f) < 0 ? 0 : f.n;
    } else {
        n = dc_win_read(p);
        if (n < 0 && dc_pbase[p]) { dc_pbase[p] = 0; dc_pdepth[p] = 0; n = dc_win_read(p); }
    }
    if (n < 0) n = 0;
    /* 3.7.3: 왼쪽도 USB 폴더 (모든 파일). 이미지 파일은 Enter로 열기 */
    dc_sort_usb(p, n);
    dc_sel[p] = 0; dc_top[p] = 0;
    memset(dc_mark[p], 0, sizeof dc_mark[p]);
    for (int guard = 0; sel_key; guard++) {              /* 찾는 이름이 이 창에 없으면 다음 창들을 차례로 */
        int found = 0;
        for (int r = 0; r < dc_count(p); r++) {
            int it = dc_item(p, r);
            if (it >= 0 && !name_cmp(dc_ue[p][it].key[0] ? dc_ue[p][it].key : dc_ue[p][it].name, sel_key)) { dc_sel[p] = r; found = 1; }
        }
        if (found || dc_pnext[p] < 0 || guard >= 256 || dc_pdepth[p] >= DC_WIN_HIST) break;
        dc_phist[p][dc_pdepth[p]++] = dc_pbase[p];
        dc_pbase[p] = dc_pnext[p];
        n = dc_win_read(p); if (n < 0) n = 0;
        dc_sort_usb(p, n);
        dc_sel[p] = 0; dc_top[p] = 0;
        memset(dc_mark[p], 0, sizeof dc_mark[p]);
    }
}
/* 끝에서 ↓ → 다음 창, 처음에서 ↑ → 앞 창 (창을 옮기면 1, 아니면 0) */
static int dc_win_move(int p, int dir)
{
    if (DCI(p)) return 0;
    if (dc_zip[p]) return 0;
    int save_base = dc_pbase[p], save_depth = dc_pdepth[p];
    if (dir > 0) {
        if (dc_pnext[p] < 0 || dc_pdepth[p] >= DC_WIN_HIST) return 0;
        dc_phist[p][dc_pdepth[p]++] = dc_pbase[p];
        dc_pbase[p] = dc_pnext[p];
    } else {
        if (dc_pdepth[p] <= 0) return 0;
        dc_pbase[p] = dc_phist[p][--dc_pdepth[p]];
    }
    int n = dc_win_read(p);
    if (n <= 0) { dc_pbase[p] = save_base; dc_pdepth[p] = save_depth; n = dc_win_read(p); if (n < 0) n = 0; dc_sort_usb(p, n); return 0; }
    dc_sort_usb(p, n);
    memset(dc_mark[p], 0, sizeof dc_mark[p]);
    int u = dc_has_up(p), rows = dc_count(p);
    dc_sel[p] = dir > 0 ? (u < rows ? u : 0) : rows - 1;
    dc_top[p] = 0;
    char t[48]; t[0] = 0;
    num_append(t, sizeof t, (uint32_t)dc_pdepth[p] + 1, 0, ' '); str_append(t, sizeof t, " "); str_append(t, sizeof t, T(S_DC_PAGE));
    dc_msg_set(1, t, 0);
    return 1;
}

/* 3.8.3: 두 창이 같은 USB 폴더를 보고 있으면, 한쪽에서 지우거나 이름을 바꾸거나 만든 뒤 다른 쪽도 다시 읽음 (고른 줄은 그대로) */
static int dc_same_path(const char *a, const char *b);
static void dc_refresh_peer(int p)
{
    int q = !p;
    if (dci[q].open || dci[p].open || !dc_upath[0][0] || !dc_same_path(dc_upath[0], dc_upath[1])) return;
    int it = dc_item(q, dc_sel[q]), top = dc_top[q], s = dc_sel[q];
    char key[FE_KEY_MAX]; key[0] = 0;
    if (it >= 0) copy_str(key, sizeof key, dc_ue[q][it].key[0] ? dc_ue[q][it].key : dc_ue[q][it].name);
    dc_load_usb(q, 0);
    int found = 0;
    for (int r = 0; r < dc_count(q) && key[0]; r++) {
        int i2 = dc_item(q, r);
        if (i2 >= 0 && !name_cmp(dc_ue[q][i2].key[0] ? dc_ue[q][i2].key : dc_ue[q][i2].name, key)) { dc_sel[q] = r; found = 1; break; }
    }
    if (!found) dc_sel[q] = s < dc_count(q) ? s : dc_count(q) > 0 ? dc_count(q) - 1 : 0;   /* 지워진 항목이면 같은 자리 */
    dc_top[q] = top < dc_count(q) ? top : 0;
}

static uint16_t dc_cur_dir(void) { return dc_depth ? dc_dstack[dc_depth - 1] : 0; }
static void dc_load_image_dir(const char *sel)
{
    if (dc_kind == DCK_PART) dc_mn = 0;
    else if (dc_kind == DCK_FAT) {
        dc_mn = 0;
        {
            int n = fat_list(&dc_vols[0], dc_fpath[0] ? dc_fpath : "\\", dc_fe, DC_MAX);
            int k = 0;
            for (int i = 0; i < n; i++) if (strcmp(dc_fe[i].key, ".") && strcmp(dc_fe[i].key, "..")) dc_fe[k++] = dc_fe[i];
            for (int i = 1; i < k; i++) {               /* 폴더 먼저, 이름 순 */
                FatEntry t = dc_fe[i];
                int j = i - 1;
                while (j >= 0 && (dc_fe[j].is_dir != t.is_dir ? (!dc_fe[j].is_dir && t.is_dir) : name_cmp(dc_fe[j].key, t.key) > 0)) { dc_fe[j + 1] = dc_fe[j]; j--; }
                dc_fe[j + 1] = t;
            }
            dc_mn = k;
        }
    } else {
        dc_mn = mf_list(&dc_fs, dc_cur_dir(), dc_me, DC_MAX);
        for (int i = 1; i < dc_mn; i++) {               /* 폴더 먼저, 이름 순 */
            MfEntry t = dc_me[i];
            int j = i - 1;
            while (j >= 0 && (dc_me[j].is_dir != t.is_dir ? (!dc_me[j].is_dir && t.is_dir) : name_cmp(dc_me[j].name, t.name) > 0)) { dc_me[j + 1] = dc_me[j]; j--; }
            dc_me[j + 1] = t;
        }
    }
    int ip_ = dcc->pane;
    dc_sel[ip_] = 0; dc_top[ip_] = 0;
    memset(dc_mark[ip_], 0, sizeof dc_mark[ip_]);
    if (sel) for (int i = 0; i < dc_img_count(); i++) if (!name_cmp(dc_img_iname(i), sel)) dc_sel[ip_] = i + 1;
}

static void dc_msg_set(int ok, const char *a, const char *b)
{
    dc_ok = ok;
    copy_str(dc_msg, sizeof dc_msg, a);
    if (b && b[0]) { str_append(dc_msg, sizeof dc_msg, ": "); str_append(dc_msg, sizeof dc_msg, b); }
}

static int dc_same_path(const char *a, const char *b) { return !name_cmp(a, b); }
static int dc_fat_rd(void *ctx, uint64_t lba, uint32_t n, uint8_t *b)       /* ctx = 그 창의 DcImg (하드 디스크 유닛이 창마다 다름) */
{
    int u = ((DcImg *)ctx)->unit;
    for (uint32_t i = 0; i < n; i++) if (!P.hd_read || P.hd_read(u, (uint32_t)lba + i, b + i * 512)) return -1;
    return 0;
}
static int dc_fat_wr(void *ctx, uint64_t lba, uint32_t n, const uint8_t *b)
{
    int u = ((DcImg *)ctx)->unit;
    for (uint32_t i = 0; i < n; i++) if (!P.hd_write || P.hd_write(u, (uint32_t)lba + i, b + i * 512)) return -1;
    return 0;
}
static uint32_t dc_secsum(const uint8_t *s) { uint32_t h = 2166136261u; for (int i = 0; i < 512; i++) h = (h ^ s[i]) * 16777619u; return h; }
static void dc_close_image(int p)
{
    dcc = &dci[p];
    if (dc_open && dc_unit >= 2 && P.hd_close) P.hd_close(dc_unit);
    dc_open = 0; dc_kind = DCK_NONE; dc_drive = -1; dc_unit = -1; dc_hd = 0; dc_vol = -1; dc_nparts = 0; dc_mn = 0;
}

/* 하드 디스크 파티션 i 열기: FAT16은 fat.c로 섹터 단위, FAT12는 작업 메모리에 올려 msxfat (바뀐 섹터만 다시 씀) */
static int dc_enter_part(int i)
{
    dc_vol = i; dc_depth = 0; dc_fpath[0] = 0;
    if (dc_pf16[i]) {
        if (fat_mount(&dc_vols[0], dc_fat_rd, dcc, dc_plba[i])) return -1;
        dc_vols[0].write = dc_fat_wr;
        dc_kind = DCK_FAT;
        return 0;
    }
    dc_part_lba = dc_plba[i]; dc_part_secs = dc_psecs[i];
    if (!P.scratch || (uint64_t)dc_part_secs * 512 > dc_smax() || dc_part_secs > 65536 || rec_on || vrec_on) return -1;
    { int ow = dc_scr_owner(); if (ow >= 0 && ow != dcc->pane) return -1; }      /* 작업 메모리는 한 창만 (다른 창이 FAT12 하드 파티션을 열어 둠) */
    for (uint32_t s = 0; s < dc_part_secs; s++) {
        if (dc_fat_rd(dcc, dc_part_lba + s, 1, P.scratch + s * 512)) return -1;
        dc_sum[s] = dc_secsum(P.scratch + s * 512);
    }
    dc_ip = P.scratch; dc_img_size = dc_part_secs * 512;
    if (mf_open(&dc_fs, dc_ip, dc_img_size)) return -1;
    dc_kind = DCK_MF;
    return 0;
}
/* 하드 디스크 이미지 열기: MBR의 기본 파티션들 (없으면 디스크 전체). 하나면 바로 그 안으로 */
static int dc_open_hd(const char *path, const char *name)
{
    uint32_t secs = 0;
    dc_unit = hd_secs && dc_same_path(hd_path, path) ? 0 : hd2_secs && dc_same_path(hd2_path, path) ? 1 : dcc->pane ? 3 : 2;   /* 유닛 2 = 왼쪽 창, 3 = 오른쪽 창 */
    if (dc_unit >= 2) { if (!P.hd_open || P.hd_open(dc_unit, path, &secs)) { dc_unit = -1; dc_msg_set(0, T(S_DC_READFAIL), name); return -1; } }
    else secs = dc_unit ? hd2_secs : hd_secs;
    dc_hd = 1; dc_nparts = 0;
    uint8_t mbr[512];
    if (!dc_fat_rd(dcc, 0, 1, mbr) && mbr[510] == 0x55 && mbr[511] == 0xAA)
        for (int i = 0; i < 4; i++) {
            const uint8_t *e = mbr + 446 + i * 16;
            uint8_t t = e[4];
            uint32_t st = e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24, n = e[12] | e[13] << 8 | e[14] << 16 | (uint32_t)e[15] << 24;
            if ((e[0] & 0x7F) || !st || !n || st >= secs) continue;
            if (t != 0x01 && t != 0x04 && t != 0x06 && t != 0x0B && t != 0x0C && t != 0x0E) continue;
            if (st + n > secs) n = secs - st;
            dc_plba[dc_nparts] = st; dc_psecs[dc_nparts] = n; dc_nparts++;
        }
    if (!dc_nparts) { dc_plba[0] = 0; dc_psecs[0] = secs; dc_nparts = 1; }
    for (int i = 1; i < dc_nparts; i++) {               /* 디스크 안 차례대로 */
        uint32_t a = dc_plba[i], b = dc_psecs[i]; int j = i - 1;
        while (j >= 0 && dc_plba[j] > a) { dc_plba[j + 1] = dc_plba[j]; dc_psecs[j + 1] = dc_psecs[j]; j--; }
        dc_plba[j + 1] = a; dc_psecs[j + 1] = b;
    }
    for (int i = 0; i < dc_nparts; i++) {               /* FAT16인가 (fat.c가 붙이면) + 라벨 */
        FatVol v;
        dc_pf16[i] = !fat_mount(&v, dc_fat_rd, dcc, dc_plba[i]);
        dc_plabel[i][0] = 0;
        if (dc_pf16[i]) copy_str(dc_plabel[i], sizeof dc_plabel[i], v.label);
    }
    if (dc_nparts == 1) { if (dc_enter_part(0)) { dc_close_image(dcc->pane); dc_msg_set(0, T(S_DC_BADIMG), name); return -1; } }
    else { dc_kind = DCK_PART; dc_vol = -1; }
    return 0;
}

static int dc_open_image(int pn, const char *path, const char *name, uint32_t fsize)
{
    if (dci[!pn].open && dc_same_path(dci[!pn].path, path)) { dc_msg_set(0, T(S_DC_INUSE), name); return -1; }   /* 같은 이미지를 양쪽에서 열지 않음 */
    dc_close_image(pn);
    dc_msg[0] = 0;
    for (int d = 0; d < 2; d++) if (disk_size[d] && dc_same_path(disk_path[d], path)) dc_drive = d;
    int hd = dc_drive < 0 && (fsize > DISK_MAX || ext_is(name, "hdd", "ide", "hdi") || (hd_secs && dc_same_path(hd_path, path)) || (hd2_secs && dc_same_path(hd2_path, path)));
    if (hd) { if (dc_open_hd(path, name)) return -1; }
    else {
        if (dc_drive >= 0) {                            /* 넣어 둔 디스크: 그 버퍼를 그대로 (MSX가 바꾼 것 포함) */
            dc_ip = disk_buf[dc_drive]; dc_img_size = disk_size[dc_drive];
        } else {
            uint32_t sz = 0;
            if (!P.read_file || P.read_file(path, dc_fbuf(pn), DISK_MAX, &sz) || !sz || sz > DISK_MAX) { dc_msg_set(0, T(S_DC_READFAIL), name); return -1; }
            dc_ip = dc_fbuf(pn); dc_img_size = sz;
        }
        if (mf_open(&dc_fs, dc_ip, dc_img_size)) { dc_msg_set(0, T(S_DC_BADIMG), name); return -1; }
        dc_kind = DCK_MF;
    }
    dc_open = 1; dc_depth = 0;
    copy_str(dc_img_path, sizeof dc_img_path, path);
    copy_str(dc_img_name, sizeof dc_img_name, name);
    dc_load_image_dir(0);
    if (dc_drive >= 0) dc_msg_set(1, T(S_DC_INUSE), dc_drive ? "B" : "A");
    else if (dc_hd && dc_unit < 2) dc_msg_set(1, T(S_DC_INUSE), "HD");
    return 0;
}

/* 바뀐 이미지를 저장: 디스크 = 파일 전체, FAT12 하드 디스크 = 바뀐 섹터만, FAT16 = 이미 섹터로 썼음 */
static int dc_save_image(void)
{
    if (dc_kind == DCK_FAT) { if (P.hd_flush) P.hd_flush(); return 0; }
    if (dc_hd) {
        for (uint32_t s = 0; s < dc_part_secs; s++) {
            uint32_t h = dc_secsum(dc_ip + s * 512);
            if (h == dc_sum[s]) continue;
            if (dc_fat_wr(0, dc_part_lba + s, 1, dc_ip + s * 512)) return -1;
            dc_sum[s] = h;
        }
        if (P.hd_flush) P.hd_flush();
        return 0;
    }
    if (!P.write_file || P.write_file(dc_img_path, dc_ip, dc_img_size)) return -1;
    if (dc_drive >= 0) {
        M->fdc.drv[dc_drive].dirty = 0;
        fdc_insert(&M->fdc, dc_drive, disk_buf[dc_drive], disk_size[dc_drive], disk_wp[dc_drive]);
    }
    return 0;
}

static void dc_path_child(char *out, int max, int p, const FeDirEntry *x)
{
    out[0] = 0;
    if (dc_upath[p][0]) { str_append(out, max, dc_upath[p]); str_append(out, max, "\\"); }
    str_append(out, max, x->key[0] ? x->key : x->name);
}
static void dc_fat_child(char *out, int max, const char *name)
{
    copy_str(out, max, dc_fpath); str_append(out, max, "\\"); str_append(out, max, name);
}

static void dc_disp_pop(int p)                          /* 위 경로 표시에서 맨 끝 " > 이름" 지우기 */
{
    int dn = 0, dl = 0;
    while (dc_udisp[p][dn]) { if (dc_udisp[p][dn] == '>' && dn > 0) dl = dn - 1; dn++; }
    dc_udisp[p][dl] = 0;
}
/* 4.0.3: zip */
static int dc_zcheck(void *c, const ZipEnt *z) { (void)c; (void)z; return 1; }
static void dc_zip_close_pane(int p)                    /* 표시 경로만 되돌리고 그 창의 zip을 닫음 */
{
    if (!dc_zip[p]) return;
    int lv = 1;                                         /* zip 이름 한 층 + 안 폴더 층들 */
    if (dc_zdir[p][0]) { lv++; for (const char *c = dc_zdir[p]; *c; c++) if (*c == '\\') lv++; }
    for (int i = 0; i < lv; i++) dc_disp_pop(p);
    dc_zip[p] = 0; dc_zdir[p][0] = 0;
}
static void dc_zip_close_quiet(void) { dc_zip_close_pane(0); dc_zip_close_pane(1); }   /* 창을 닫을 때 */
static void dc_zip_open(int p, const FeDirEntry *x)
{
    const char *key = x->key[0] ? x->key : x->name;
    if (!P.scratch || P.scratch_size < (16u << 20) || rec_on || vrec_on) { dc_msg_set(0, T(S_DC_TOOBIG), x->name); return; }
    { int ow = dc_scr_owner(); if (ow >= 0 && dci[ow].size > P.scratch_size - DC_ZRES * (uint32_t)(p + 1)) { dc_msg_set(0, T(S_DC_TOOBIG), x->name); return; } }   /* 열린 하드 디스크 파티션이 그 자리를 씀 */
    if (x->size > DC_ZRES) { dc_msg_set(0, T(S_DC_TOOBIG), x->name); return; }
    dc_zip_close_pane(p);                               /* 이 창에서 다른 zip으로 (지난번 zip 표시를 정리) */
    char np[PATH_MAX_FE]; dc_path_child(np, sizeof np, p, x);
    uint32_t n = 0;
    uint8_t *zb = dc_zbuf(p);
    if (!P.read_file || P.read_file(np, zb, DC_ZRES, &n) || !n || n > DC_ZRES || zip_list(zb, n, "", dc_zcheck, 0) < 0) { dc_msg_set(0, T(S_DC_READFAIL), x->name); return; }
    dc_zip[p] = 1; dc_zlen[p] = n; dc_zdir[p][0] = 0;
    copy_str(dc_zname[p], sizeof dc_zname[p], key);
    if (dc_udisp[p][0]) str_append(dc_udisp[p], sizeof dc_udisp[p], " > ");
    str_append(dc_udisp[p], sizeof dc_udisp[p], x->name);
    dc_msg[0] = 0;
    dc_load_usb(p, 0);
}
static void dc_zip_push(int p, const FeDirEntry *x)
{
    if (dc_zdir[p][0]) str_append(dc_zdir[p], sizeof dc_zdir[p], "\\");
    str_append(dc_zdir[p], sizeof dc_zdir[p], x->key[0] ? x->key : x->name);
    str_append(dc_udisp[p], sizeof dc_udisp[p], " > ");
    str_append(dc_udisp[p], sizeof dc_udisp[p], x->name);
    dc_load_usb(p, 0);
}
static void dc_zip_up(int p)
{
    char from[FE_KEY_MAX];
    dc_disp_pop(p);
    if (dc_zdir[p][0]) {
        int n = (int)strlen(dc_zdir[p]), k = n;
        while (k > 0 && dc_zdir[p][k - 1] != '\\') k--;
        copy_str(from, sizeof from, dc_zdir[p] + k);
        dc_zdir[p][k ? k - 1 : 0] = 0;
    } else {
        copy_str(from, sizeof from, dc_zname[p]);          /* zip 밖으로: 그 zip 파일에 커서 */
        dc_zip[p] = 0;
    }
    dc_load_usb(p, from);
}

static void dc_usb_up(int p)
{
    if (!dc_upath[p][0]) return;
    int n = 0, last = 0;
    while (dc_upath[p][n]) { if (dc_upath[p][n] == '\\') last = n + 1; n++; }
    char from[FE_KEY_MAX];
    copy_str(from, sizeof from, dc_upath[p] + last);
    dc_upath[p][last ? last - 1 : 0] = 0;
    int dn = 0, dl = 0;
    while (dc_udisp[p][dn]) { if (dc_udisp[p][dn] == '>' && dn > 0) dl = dn - 1; dn++; }
    dc_udisp[p][dl] = 0;
    dc_load_usb(p, from);
}

static void dc_up(int p)
{
    if (dc_zip[p]) { dc_zip_up(p); return; }       /* 4.0.3: zip 안 ".." */
    if (DCI(p)) {
        if (dc_kind == DCK_MF && dc_depth) { char from[13]; copy_str(from, sizeof from, dc_dname[--dc_depth]); dc_load_image_dir(from); return; }
        if (dc_kind == DCK_FAT && dc_fpath[0]) {
            int n = (int)strlen(dc_fpath), k = n;
            while (k > 0 && dc_fpath[k - 1] != '\\') k--;
            char from[FE_KEY_MAX]; copy_str(from, sizeof from, dc_fpath + k);
            dc_fpath[k ? k - 1 : 0] = 0;
            dc_load_image_dir(from);
            return;
        }
        if (dc_hd && dc_nparts > 1 && dc_kind != DCK_PART) { int v = dc_vol; dc_kind = DCK_PART; dc_vol = -1; dc_depth = 0; dc_load_image_dir(0); dc_sel[p] = v + 1; return; }
        { char nm_[64]; copy_str(nm_, sizeof nm_, dc_img_name); dc_close_image(p); dc_load_usb(p, nm_); dc_msg[0] = 0; }   /* 이미지 닫기 */
        return;
    }
    dc_usb_up(p);
}

static void dc_edit_start(void);
static int dc_is_text(const char *n)                    /* 4.0.31: Enter로 편집기를 여는 글 파일 */
{
    static const char *const e[] = { "txt", "ini", "asc", "bas", "doc", "bat", "md", "log", "cfg" };
    for (unsigned i = 0; i < sizeof e / sizeof e[0]; i++) if (ext_is(n, e[i], 0, 0)) return 1;
    return 0;
}
/* 4.0.31: PNG 미리보기 팝업 (Enter) */
static int dc_pv;
static const char *dc_pv_note;                           /* 팝업 아래 알림 (처음·마지막 그림) */
static uint32_t dc_pv_w, dc_pv_h;
static char dc_pv_name[64];
#define DC_PV_FILE (2u << 20)
/* 작업 메모리에서 zip 자리(맨 뒤 16 MiB) 바로 앞 8 MiB: 파일 2 MiB + 해독 자리 */
static uint8_t *dc_pv_mem(void) { return P.scratch + P.scratch_size - 2 * DC_ZRES - (8u << 20); }
static void dc_preview(int p, FeDirEntry *x)
{
    char np[PATH_MAX_FE]; uint32_t n = 0;
    if (x->is_dir || dc_zip[p] || (DCI(p))) return;
    if (!ext_is(x->key[0] ? x->key : x->name, "png", 0, 0)) return;
    dc_path_child(np, sizeof np, p, x);
    if (!P.read_file || P.read_file(np, dc_pv_mem(), DC_PV_FILE, &n) || !n || n > DC_PV_FILE || png_load_big(dc_pv_mem(), n, &dc_pv_w, &dc_pv_h, dc_pv_mem() + DC_PV_FILE)) { dc_msg_set(0, T(S_DC_READFAIL), x->name); return; }
    copy_str(dc_pv_name, sizeof dc_pv_name, x->name);
    dc_pv = 1; dc_pv_note = 0;
}
static void dc_pv_step(int dir)                         /* 4.0.32: > 다음 그림, < 앞 그림 (같은 폴더, 목록 차례) */
{
    int p = dc_pane, n = dc_count(p);
    dc_pv_note = 0;
    for (int r = dc_sel[p] + dir; r >= 0 && r < n; r += dir) {
        int it = dc_item(p, r);
        if (it < 0) continue;
        FeDirEntry *x = &dc_ue[p][it];
        if (x->is_dir || !ext_is(x->key[0] ? x->key : x->name, "png", 0, 0)) continue;
        dc_sel[p] = r;
        dc_preview(p, x);
        return;
    }
    dc_pv_note = T(dir > 0 ? S_PV_LAST : S_PV_FIRST);
}
static void dc_pv_draw(UiRect *b)
{
    uint32_t pw = b->w * 9 / 10, ph = b->h * 9 / 10;
    uint32_t hh = LH + 14 * S, ix = 12 * S;
    uint32_t aw = pw - 2 * ix, ah = ph - hh - 12 * S;
    uint32_t dw = aw, dh = dc_pv_h * aw / dc_pv_w;
    if (dh > ah) { dh = ah; dw = dc_pv_w * ah / dc_pv_h; }
    if (!dw) dw = 1;
    if (!dh) dh = 1;
    uint32_t px = b->x + (b->w - pw) / 2, py = b->y + (b->h - ph) / 2;
    hit_add(hc_none, 0, 0, 0, 0, px, py, pw, ph);
    ui_rrect(px + 5 * S, py + 6 * S, pw, ph, 10 * S, C_SHADOW);
    ui_rframe(px, py, pw, ph, 10 * S, C_ACC, C_CARD);
    char t[96]; copy_str(t, sizeof t, dc_pv_name); str_append(t, sizeof t, "  ");
    num_append(t, sizeof t, dc_pv_w, 0, ' '); str_append(t, sizeof t, "x"); num_append(t, sizeof t, dc_pv_h, 0, ' ');
    text_ell(px + ix, py + 8 * S, t, C_TEXT, 0xFF, pw - 2 * ix - 150 * S);
    text_clip(px + pw - ix - 130 * S, py + 8 * S, "< >   Esc", C_MUTED, 0xFF, 130 * S);
    uint32_t x0 = px + (pw - dw) / 2, y0 = py + hh + (ah - dh) / 2;
    char key_hint[8]; (void)key_hint;
    for (uint32_t oy = 0; oy < dh; oy++) {
        uint32_t sy = oy * dc_pv_h / dh, run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t ox = 0; ox <= dw; ox++) {
            uint32_t col = 0xFFFFFFFFu;
            if (ox < dw) {
                int c[4]; png_rgba(ox * dc_pv_w / dw, sy, c);
                if (c[3] < 255) { int bgc = ((ox >> 3) + (oy >> 3)) & 1 ? 90 : 60; for (int k = 0; k < 3; k++) c[k] = (c[k] * c[3] + bgc * (255 - c[3])) / 255; }
                col = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
            }
            if (col != run_c) {
                if (run_c != 0xFFFFFFFFu && ox > run_x) fe_fill_rgb(x0 + run_x, y0 + oy, ox - run_x, 1, run_c >> 16, (run_c >> 8) & 255, run_c & 255);
                run_c = col; run_x = ox;
            }
        }
    }
    if (dc_pv_note) {
        uint32_t nw = text_w(dc_pv_note) + 40 * S, nh = LH + 16 * S;
        uint32_t nx = px + (pw - nw) / 2, ny = py + ph - nh - 18 * S;
        ui_rrect(nx, ny, nw, nh, 8 * S, C_HEAD);
        ui_rframe(nx, ny, nw, nh, 8 * S, C_WARN, C_HEAD);
        text_clip(nx + 20 * S, ui_ty(ny, nh), dc_pv_note, C_WARN, 0xFF, nw - 40 * S);
    }
}

static void dc_enter(int p)
{
    int row = dc_sel[p], it = dc_item(p, row);
    if (it < 0) { dc_up(p); return; }
    if (DCI(p)) {
        if (dc_kind == DCK_PART) {
            if (dc_enter_part(it)) { dc_kind = DCK_PART; dc_vol = -1; dc_msg_set(0, T(S_DC_BADIMG), dc_img_iname(it)); return; }
            dc_load_image_dir(0);
            return;
        }
        if (dc_kind == DCK_FAT) {
            if (!dc_fe[it].is_dir) return;
            char np[PATH_MAX_FE]; dc_fat_child(np, sizeof np, dc_fe[it].key);
            copy_str(dc_fpath, sizeof dc_fpath, np);
            dc_load_image_dir(0);
            return;
        }
        MfEntry *e = &dc_me[it];
        if (!e->is_dir || dc_depth >= 8) return;
        copy_str(dc_dname[dc_depth], sizeof dc_dname[0], e->name);
        dc_dstack[dc_depth++] = e->clus;
        dc_load_image_dir(0);
        return;
    }
    FeDirEntry *x = &dc_ue[p][it];
    if (dc_zip[p]) { if (x->is_dir) dc_zip_push(p, x); else if (dc_is_text(x->key[0] ? x->key : x->name)) dc_edit_start(); return; }   /* 4.0.3: zip 안 폴더는 들어감, 5.0.37: 글 파일은 편집기로 (보기만) */
    if (!x->is_dir && ext_is(x->key[0] ? x->key : x->name, "zip", 0, 0)) { dc_zip_open(p, x); return; }
    if (!x->is_dir && ext_is(x->key[0] ? x->key : x->name, "png", 0, 0)) { dc_preview(p, x); return; }
    if (!x->is_dir && dc_is_text(x->key[0] ? x->key : x->name)) { dc_edit_start(); return; }
    char np[PATH_MAX_FE];
    dc_path_child(np, sizeof np, p, x);
    if (x->is_dir) {
        copy_str(dc_upath[p], sizeof dc_upath[p], np);
        if (dc_udisp[p][0]) str_append(dc_udisp[p], sizeof dc_udisp[p], " > ");
        str_append(dc_udisp[p], sizeof dc_udisp[p], x->name);
        dc_load_usb(p, 0);
    } else if (dc_is_image_name(x->key[0] ? x->key : x->name)) dc_open_image(p, np, x->name, x->size);
}

static int dc_count_marked(int p) { int n = 0; for (int i = 0; i <= DCU_MAX; i++) n += dc_mark[p][i]; return n; }
static int dc_row_on(int p, int r, int any) { return any ? dc_mark[p][r] : r == dc_sel[p]; }
/* 복사 버퍼: FAT12 하드 디스크가 작업 메모리를 쓰면 1.44 MB, 아니면 작업 메모리 전체 */
static uint8_t *dc_xbuf(uint32_t *max)
{
    int ow = dc_scr_owner();
    if (P.scratch && ow < 0 && !rec_on && !vrec_on) { *max = dc_smax(); return P.scratch; }
    if (P.scratch && ow >= 0 && !rec_on && !vrec_on) {  /* FAT12 하드 파티션이 작업 메모리 앞을 씀 → 그 뒤를 복사 버퍼로 */
        uint32_t used = (dci[ow].size + 4095u) & ~4095u, sm = dc_smax();
        if (used < sm && sm - used >= DISK_MAX) { *max = sm - used; return P.scratch + used; }
    }
    if (dci[1].open && dci[1].ip == dc_buf) { *max = 0; return dc_buf; }   /* 오른쪽 창의 디스크 이미지가 dc_buf를 쓰고 있음 */
    *max = sizeof dc_buf; return dc_buf;
}
static void dc_dos_date(uint16_t *date, uint16_t *time, const FeDirEntry *x)
{
    *date = 0x21; *time = 0;                            /* 날짜를 모르면 1980-01-01 */
    if (x && x->year >= 1980) { *date = (uint16_t)((x->year - 1980) << 9 | x->mon << 5 | x->day); *time = (uint16_t)(x->hour << 11 | x->min << 5); }
    else if (P.now) { int t[6]; if (!P.now(t) && t[0] >= 1980) { *date = (uint16_t)((t[0] - 1980) << 9 | t[1] << 5 | t[2]); *time = (uint16_t)(t[3] << 11 | t[4] << 5 | t[5] / 2); } }
}
static void dc_raw_name(const uint8_t raw[11], char *out)   /* 8+3 → "NAME.EXT" */
{
    int k = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[k++] = (char)raw[i];
    if (raw[8] != ' ') { out[k++] = '.'; for (int i = 8; i < 11 && raw[i] != ' '; i++) out[k++] = (char)raw[i]; }
    out[k] = 0;
}

/* 3.7.3: 복사 — 고른 줄(없으면 지금 줄)의 파일과 폴더(안의 것 모두)를 반대쪽으로.
 * 양쪽 다 USB 폴더일 수도, 한쪽이 이미지(디스크 FAT12 / 하드 FAT16)일 수도 있다. */
typedef struct { int k; int zp; int ip; char path[PATH_MAX_FE]; uint16_t clus; } DcLoc;     /* k: 0 USB, 1 FAT16 이미지(경로), 2 디스크 이미지(폴더 클러스터), 3 zip 안(폴더 경로) */
typedef struct { char name[256]; uint8_t dir; uint32_t size; uint16_t year; uint8_t mon, day, hour, min; MfEntry me; } DcEnt;
#define DC_DEPTH 8
static DcEnt dc_lv[DC_DEPTH + 1][DC_MAX];
typedef struct { int files, dirs, skip, renamed, failed, err; char err_name[64]; uint8_t *buf; uint32_t bmax; } DcStat;

static void dc_loc_pane(int p, DcLoc *l)
{
    memset(l, 0, sizeof *l);
    if (DCI(p)) {
        l->ip = p;
        if (dc_kind == DCK_FAT) { l->k = 1; copy_str(l->path, sizeof l->path, dc_fpath); }
        else { l->k = 2; l->clus = dc_cur_dir(); }
    } else if (dc_zip[p]) { l->k = 3; l->zp = p; copy_str(l->path, sizeof l->path, dc_zdir[p]); }
    else copy_str(l->path, sizeof l->path, dc_upath[p]);
}
static void dc_loc_join(const DcLoc *d, const char *name, char *out, int max)
{
    if (d->k == 3 && !d->path[0]) { copy_str(out, max, name); return; }   /* zip 맨 위는 앞 빗금 없이 */
    copy_str(out, max, d->path); str_append(out, max, "\\"); str_append(out, max, name);
}
static void dc_ent_from_fe(DcEnt *e, const FeDirEntry *x)
{
    memset(e, 0, sizeof *e);
    copy_str(e->name, sizeof e->name, x->key[0] ? x->key : x->name);
    e->dir = x->is_dir; e->size = x->size; e->year = x->year; e->mon = x->mon; e->day = x->day; e->hour = x->hour; e->min = x->min;
}
static void dc_ent_from_fat(DcEnt *e, const FatEntry *x)
{
    memset(e, 0, sizeof *e);
    copy_str(e->name, sizeof e->name, x->key);
    e->dir = x->is_dir != 0; e->size = x->size; e->year = x->year; e->mon = x->mon; e->day = x->day; e->hour = x->hour; e->min = x->min;
}
static void dc_ent_from_mf(DcEnt *e, const MfEntry *x)
{
    memset(e, 0, sizeof *e);
    copy_str(e->name, sizeof e->name, x->name);
    e->dir = x->is_dir; e->size = x->size; e->me = *x;
    if (x->date) { e->year = (uint16_t)(1980 + (x->date >> 9)); e->mon = (uint8_t)(x->date >> 5 & 15); e->day = (uint8_t)(x->date & 31); e->hour = (uint8_t)(x->time >> 11); e->min = (uint8_t)(x->time >> 5 & 63); }
}
typedef struct { DcEnt *out; int n, max; } DcZTree;
static int dc_ztree(void *c, const ZipEnt *z)           /* 4.0.3: zip 항목 → 복사 목록 */
{
    DcZTree *f = c;
    for (int j = 0; j < f->n; j++) if (f->out[j].dir == z->dir && !strcmp(f->out[j].name, z->name)) return 0;
    if (f->n >= f->max) return 1;
    DcEnt *e = &f->out[f->n++];
    memset(e, 0, sizeof *e);
    copy_str(e->name, sizeof e->name, z->name);
    e->dir = z->dir; e->size = z->size; e->year = z->year; e->mon = z->mon; e->day = z->day; e->hour = z->hour; e->min = z->min;
    return 0;
}
/* 목록 (임시로 dc_ue[0]·dc_fe·dc_me를 씀 — 복사 뒤 두 창을 다시 읽음) */
static int dc_tl_skip, dc_tl_next = -1;                 /* 4.0.34: 복사할 폴더 안 목록도 창으로 (256개 넘어도 빠짐없이) */
static int dc_tree_list(const DcLoc *d, DcEnt *out, int max)
{
    int n = 0, skip = dc_tl_skip;
    dc_tl_skip = 0; dc_tl_next = -1;
    if (d->k == 0) {
        fe_list_skip = skip; fe_list_next = -1;
        int m = P.list_dir_all ? P.list_dir_all(d->path, dc_ue[0], DC_MAX) : P.list_dir ? P.list_dir(d->path, dc_ue[0], DC_MAX) : -1;
        fe_list_skip = 0;
        if (m >= DC_MAX) dc_tl_next = fe_list_next;
        for (int i = 0; i < m && n < max; i++) {
            const char *nm = dc_ue[0][i].key[0] ? dc_ue[0][i].key : dc_ue[0][i].name;
            if (!strcmp(nm, ".") || !strcmp(nm, "..")) continue;
            dc_ent_from_fe(&out[n++], &dc_ue[0][i]);
        }
    } else if (d->k == 3) {                              /* 4.0.3: zip 안 폴더 */
        char zd[PATH_MAX_FE];
        copy_str(zd, sizeof zd, d->path);
        for (char *c = zd; *c; c++) if (*c == '\\') *c = '/';
        DcZTree f = { out, 0, max };
        if (zip_list(dc_zbuf(d->zp), dc_zlen[d->zp], zd, dc_ztree, &f) >= 0) n = f.n;
    } else if (d->k == 1) {
        DcImg *im = &dci[d->ip];
        int m = fat_list(&im->vols[0], d->path[0] ? d->path : "\\", im->fe, DC_MAX);
        for (int i = 0; i < m && n < max; i++) {
            if (!strcmp(im->fe[i].key, ".") || !strcmp(im->fe[i].key, "..")) continue;
            dc_ent_from_fat(&out[n++], &im->fe[i]);
        }
    } else {
        DcImg *im = &dci[d->ip];
        int m = mf_list(&im->fs, d->clus, im->me, DC_MAX);
        for (int i = 0; i < m && n < max; i++) dc_ent_from_mf(&out[n++], &im->me[i]);
    }
    return n;
}
/* ---- 4.0: 복사·지우기 진행 창 (무엇을 하는지 이름과 움직임으로 보여 줌) ---- */
static struct { int mode, total, done, nrecent; uint32_t size; char cur[96], from[64], to[64], recent[5][64]; int stage, ask; } dcp;
static int dc_cancel;                                   /* 4.0.34: 복사 도중 ESC → 취소 확인에서 '예' */   /* mode: 0 없음, 1 복사, 2 지우기 */
static void dc_size_str(char *t, int max, uint32_t n);
static void dc_prog_draw(void)
{
    if (!dcp.mode) return;
    uint32_t w = out_w * 3 / 4;
    if (w > 860 * S) w = 860 * S;
    uint32_t vis_h = 92 * S, rl = SLH + 3 * S;
    uint32_t h = ui_head_h() + 18 * S + vis_h + 14 * S + LH + 8 * S + LH + 8 * S + 14 * S + 5 * rl + 18 * S;
    if (h > D.h - 20 * S) h = D.h - 20 * S;
    uint32_t x = x0 + (out_w - w) / 2, y = D.h > h ? (D.h - h) / 2 : 0;
    char sub[24] = "";
    num_append(sub, sizeof sub, (uint32_t)dcp.done, 0, ' '); str_append(sub, sizeof sub, " / "); num_append(sub, sizeof sub, (uint32_t)dcp.total, 0, ' ');
    UiRect b = ui_card(x, y, w, h, T(dcp.mode == 1 ? S_DC_BUSY_COPY : S_DC_BUSY_DEL), sub, 0);
    hit_add(hc_none, 0, 0, 0, 0, x, y, w, h);
    uint32_t pad = 22 * S, bx = b.x + pad, bw = b.w - 2 * pad, vy = b.y + 14 * S;
    /* 그림: 왼쪽 상자(원본) → 파일 조각들이 흘러감 → 오른쪽 상자(대상) 또는 휴지통 */
    uint32_t boxw = 130 * S, boxh = vis_h - 24 * S;
    uint32_t lx = bx, rx = bx + bw - boxw, by_ = vy;
    ui_rrect(lx, by_, boxw, boxh, 10 * S, C_HEAD);
    ui_rframe(lx, by_, boxw, boxh, 10 * S, C_LINE, C_HEAD);
    ui_rrect(lx + 14 * S, by_ + 12 * S, boxw / 2, 8 * S, 3 * S, C_ACC);                  /* 폴더 귀 */
    ui_rrect(lx + 14 * S, by_ + 20 * S, boxw - 28 * S, boxh - 36 * S, 5 * S, dcp.mode == 1 ? C_ACC : C_MUTED);
    uint8_t rc = dcp.mode == 1 ? C_OK : C_BAD;
    ui_rrect(rx, by_, boxw, boxh, 10 * S, C_HEAD);
    ui_rframe(rx, by_, boxw, boxh, 10 * S, rc, C_HEAD);
    if (dcp.mode == 1) {
        ui_rrect(rx + 14 * S, by_ + 12 * S, boxw / 2, 8 * S, 3 * S, rc);
        ui_rrect(rx + 14 * S, by_ + 20 * S, boxw - 28 * S, boxh - 36 * S, 5 * S, rc);
    } else {                                                                            /* 휴지통: 뚜껑이 들썩임 */
        uint32_t lift = ((P.ms() / 90) & 1) ? 4 * S : 0;
        ui_rrect(rx + 20 * S, by_ + 12 * S - lift, boxw - 40 * S, 6 * S, 3 * S, rc);
        ui_rrect(rx + 28 * S, by_ + 20 * S, boxw - 56 * S, boxh - 34 * S, 5 * S, rc);
    }
    uint32_t t0x = lx + boxw + 14 * S, t1x = rx - 14 * S, span = t1x > t0x ? t1x - t0x : 1;
    uint32_t ph = (P.ms() / 11) % 100, cy_ = by_ + boxh / 2;
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t q = (ph + i * 20) % 100, e = q < 50 ? q : 100 - q;                   /* 가운데에서 가장 큼 */
        uint32_t cw = (8 + e * 16 / 50) * S, ch = (10 + e * 20 / 50) * S;
        uint32_t cx = t0x + span * q / 100 - cw / 2;
        ui_rrect(cx, cy_ - ch / 2, cw, ch, 3 * S, dcp.mode == 1 ? C_WARN : C_BAD);
        if (cw > 12 * S) fe_fill(cx + 3 * S, cy_ - ch / 2 + 4 * S, cw - 6 * S, S, C_BACK);   /* 쪽 줄 */
    }
    uint32_t lab_y = by_ + boxh + 4 * S;
    small_ell(lx, lab_y, dcp.from, C_MUTED, 0xFF, boxw + 40 * S);
    uint32_t tl = ui_sw(dcp.to); if (tl > boxw + 40 * S) tl = boxw + 40 * S;
    if (dcp.mode == 1) small_ell(rx + boxw - tl, lab_y, dcp.to, C_MUTED, 0xFF, boxw + 40 * S);
    /* 지금 하는 것 */
    uint32_t y2 = vy + vis_h + 6 * S;
    text_ell(bx, y2, dcp.cur[0] ? dcp.cur : T(S_DC_P_WAIT), C_TEXT, 0xFF, bw);
    y2 += LH + 8 * S;
    uint32_t pct = dcp.total ? (uint32_t)(dcp.done * 100 + (dcp.stage ? 50 : 0)) / (uint32_t)dcp.total : 0;
    if (pct > 100) pct = 100;
    char t[32] = "";
    num_append(t, sizeof t, pct, 0, ' '); str_append(t, sizeof t, " %");
    if (dcp.size) { str_append(t, sizeof t, "   "); dc_size_str(t + strlen(t), (int)(sizeof t - strlen(t)), dcp.size); }
    fe_fill(bx, y2, bw, LH, C_CARD);
    text_clip(bx, y2, t, C_MUTED, 0xFF, bw);
    y2 += LH + 6 * S;
    ui_rrect(bx, y2, bw, 14 * S, 7 * S, C_FIELD);
    uint32_t k = (bw - 4 * S) * pct / 100;
    if (k) ui_rrect(bx + 2 * S, y2 + 2 * S, k < 10 * S ? 10 * S : k, 10 * S, 5 * S, dcp.mode == 1 ? C_OK : C_BAD);
    y2 += 14 * S + 12 * S;
    /* 끝난 것: 최근 5개 */
    for (int i = 0; i < dcp.nrecent && i < 5; i++) {
        ui_rrect(bx, y2 + (rl - 8 * S) / 2, 8 * S, 8 * S, 4 * S, rc);
        small_ell(bx + 16 * S, y2, dcp.recent[i], i == 0 ? C_TEXT : C_MUTED, 0xFF, bw - 16 * S);
        y2 += rl;
    }
    if (dcp.ask) {                                      /* ESC: 취소할까요? */
        uint32_t pw = 520 * S, ph = LH + ui_cmdbar_h() + 44 * S;
        if (pw > w) pw = w;
        uint32_t px = x + (w - pw) / 2, py = y + (h - ph) / 2;
        ui_rrect(px + 5 * S, py + 6 * S, pw, ph, 10 * S, C_SHADOW);
        ui_rframe(px, py, pw, ph, 10 * S, C_BAD, C_CARD);
        text_f(F, S, px + 16 * S, py + 14 * S, T(S_DC_CANCEL_Q), C_TEXT, 0xFF, 0, 1);
        ui_cmdbar(px + S, py + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(S_DC_CANCEL_KEYS));
    }
}
static void dc_prog_frame(void)
{
    dc_prog_draw();
    fe_flush_now();
}
static void dc_prog_hold(uint32_t ms)
{
    uint32_t t0 = P.ms();
    do {
        dc_prog_frame();
        uint32_t t1 = P.ms();
        while (P.ms() - t1 < 14) __asm__ volatile("pause");
    } while (P.ms() - t0 < ms);
}
/* 복사·삭제 도중 ESC를 눌렀는지 보고, 눌렀으면 취소할지 물음 (막힌 반복 안이라 키를 직접 읽음) */
static void dc_poll_esc(void)
{
    if (!dcp.mode || dc_cancel || dcp.ask) return;
    if (P.key_poll) P.key_poll();
    int esc = 0;
    while (kq_tail != kq_head) { if (kq[kq_tail++] == 0x01) esc = 1; }
    if (!esc) return;
    dcp.ask = 1;
    for (;;) {
        dc_prog_frame();
        if (P.key_poll) P.key_poll();
        int ans = 0;
        while (kq_tail != kq_head) {
            uint8_t c = kq[kq_tail++];
            if (c == 0x1C || c == 0x15) ans = 1;        /* Enter, Y */
            else if (c == 0x01 || c == 0x31) ans = 2;   /* Esc, N */
        }
        if (ans) { dc_cancel = (ans == 1); break; }
        uint32_t t1 = P.ms();
        while (P.ms() - t1 < 14) __asm__ volatile("pause");
    }
    dcp.ask = 0;
    dc_prog_frame();
}
static uint32_t dc_prog_ms(void) { return dcp.total <= 6 ? 420 : dcp.total <= 30 ? 160 : dcp.total <= 120 ? 45 : 0; }
static void dc_prog_begin(int mode, int total, const char *from, const char *to)
{
    memset(&dcp, 0, sizeof dcp); dc_cancel = 0;
    dcp.mode = mode; dcp.total = total > 0 ? total : 1;
    copy_str(dcp.from, sizeof dcp.from, from ? from : "");
    copy_str(dcp.to, sizeof dcp.to, to ? to : "");
    dc_prog_frame();
}
static void dc_prog_item(const char *name, uint32_t size)
{
    if (!dcp.mode) return;
    dc_poll_esc();
    copy_str(dcp.cur, sizeof dcp.cur, name); dcp.size = size; dcp.stage = 1;
    dc_prog_frame();
}
static void dc_prog_done(void)
{
    if (!dcp.mode) return;
    for (int i = 4; i > 0; i--) memcpy(dcp.recent[i], dcp.recent[i - 1], sizeof dcp.recent[0]);
    copy_str(dcp.recent[0], sizeof dcp.recent[0], dcp.cur);
    if (dcp.nrecent < 5) dcp.nrecent++;
    dcp.done++; dcp.stage = 0;
    uint32_t ms = dc_prog_ms();
    if (ms) dc_prog_hold(ms); else if ((dcp.done & 7) == 0) dc_prog_frame();
    dc_poll_esc();
}
static void dc_prog_end(void)
{
    if (!dcp.mode) return;
    dcp.done = dcp.total; dcp.cur[0] = 0; dcp.size = 0; dcp.stage = 0;
    dc_prog_hold(160);
    dcp.mode = 0;
    menu_dirty = 1; full_redraw();
}
static int dc_count_tree(const DcLoc *src, const DcEnt *e, int depth);
static void dc_err(DcStat *st, int e, const char *name)
{
    st->failed++;
    if (!st->err) { st->err = e; copy_str(st->err_name, sizeof st->err_name, name); }
    if (dcp.mode) { dcp.done++; dcp.stage = 0; dc_prog_frame(); }
}
static void dc_tree(const DcLoc *src, const DcEnt *e, const DcLoc *dst, int depth, DcStat *st)
{
    if (dc_cancel) return;
    uint8_t raw[11]; char n83[16];
    int lossy = mf_make83(e->name, raw); dc_raw_name(raw, n83);
    FeDirEntry fx; memset(&fx, 0, sizeof fx); fx.year = e->year; fx.mon = e->mon; fx.day = e->day; fx.hour = e->hour; fx.min = e->min;
    uint16_t date, time; dc_dos_date(&date, &time, e->year ? &fx : 0);
    if (!e->dir) {                                      /* 파일 */
        uint32_t n = 0;
        dc_prog_item(e->name, e->size);
        if (e->size > st->bmax) { dc_err(st, S_DC_TOOBIG, e->name); return; }
        if (src->k == 0) { char sp[PATH_MAX_FE]; dc_loc_join(src, e->name, sp, sizeof sp); if (!P.read_file || P.read_file(sp, st->buf, st->bmax, &n) || n > st->bmax) { dc_err(st, S_DC_READFAIL, e->name); return; } }
        else if (src->k == 1) { char sp[PATH_MAX_FE]; dc_loc_join(src, e->name, sp, sizeof sp); if (fat_read_file(&dci[src->ip].vols[0], sp, st->buf, st->bmax, &n)) { dc_err(st, S_DC_READFAIL, e->name); return; } }
        else if (src->k == 3) {                         /* 4.0.3: zip 안 파일 */
            char sp[PATH_MAX_FE]; dc_loc_join(src, e->name, sp, sizeof sp);
            for (char *c = sp; *c; c++) if (*c == '\\') *c = '/';
            int zr = zip_read(dc_zbuf(src->zp), dc_zlen[src->zp], sp, st->buf, st->bmax, &n);
            if (zr) { dc_err(st, zr == ZIPX_BIG ? S_DC_TOOBIG : S_DC_READFAIL, e->name); return; }
        }
        else n = mf_read(&dci[src->ip].fs, &e->me, st->buf, st->bmax);
        if (dst->k == 0) {
            char dp[PATH_MAX_FE]; dc_loc_join(dst, e->name, dp, sizeof dp);
            if (!P.write_file || (P.write_file(dp, st->buf, n) && (!P.create || P.create(dp, 0) || P.write_file(dp, st->buf, n)))) { dc_err(st, S_DC_WRITEFAIL, e->name); return; }
        } else if (dst->k == 1) {
            char dp[PATH_MAX_FE]; dc_loc_join(dst, n83, dp, sizeof dp);
            fat_create(&dci[dst->ip].vols[0], dp, 0);
            if (fat_write_file(&dci[dst->ip].vols[0], dp, st->buf, n)) { dc_err(st, S_DC_NOSPACE, e->name); return; }
            st->renamed += lossy;
        } else {
            int rc = mf_write(&dci[dst->ip].fs, dst->clus, raw, st->buf, n, date, time);
            if (rc) { dc_err(st, rc == MF_EDIRFULL ? S_DC_DIRFULL : S_DC_NOSPACE, e->name); return; }
            st->renamed += lossy;
        }
        st->files++;
        dc_prog_done();
        return;
    }
    /* 폴더: 반대쪽에 만들고 안의 것을 모두 */
    if (depth >= DC_DEPTH) { st->skip++; return; }
    dc_prog_item(e->name, 0);
    DcLoc s2 = *src, d2 = *dst;
    if (src->k == 2) s2.clus = e->me.clus; else dc_loc_join(src, e->name, s2.path, sizeof s2.path);
    if (dst->k == 0) {
        dc_loc_join(dst, e->name, d2.path, sizeof d2.path);
        if (src->k == 0) {                              /* 자기 안으로는 복사하지 않음 */
            size_t L = strlen(s2.path);
            if (!name_cmp(d2.path, s2.path) || (strlen(d2.path) > L && d2.path[L] == '\\' && !strncmp(d2.path, s2.path, L))) { dc_err(st, S_DC_INSIDE, e->name); return; }
            char dpath[PATH_MAX_FE]; copy_str(dpath, sizeof dpath, dst->path);
            size_t L2 = strlen(dpath);
            if (L2 >= L && !strncmp(dpath, s2.path, L) && (dpath[L] == 0 || dpath[L] == '\\')) { dc_err(st, S_DC_INSIDE, e->name); return; }
        }
        if (P.create) P.create(d2.path, 1);
    } else if (dst->k == 1) {
        dc_loc_join(dst, n83, d2.path, sizeof d2.path);
        fat_create(&dci[dst->ip].vols[0], d2.path, 1);
        st->renamed += lossy;
    } else {
        int rc = mf_mkdir(&dci[dst->ip].fs, dst->clus, raw, date, time, &d2.clus);
        if (rc) { dc_err(st, rc == MF_EDIRFULL ? S_DC_DIRFULL : rc == MF_EBAD ? S_DC_EXISTS : S_DC_NOSPACE, e->name); return; }
        st->renamed += lossy;
    }
    st->dirs++;
    dc_prog_done();
    DcEnt *lv = dc_lv[depth + 1];
    for (int skip = 0;;) {
        dc_tl_skip = skip;
        int n = dc_tree_list(&s2, lv, DC_MAX), nxt = dc_tl_next;
        for (int i = 0; i < n && !dc_cancel; i++) dc_tree(&s2, &lv[i], &d2, depth + 1, st);
        if (nxt <= skip || n <= 0 || dc_cancel) break;
        skip = nxt;
    }
}
static int dc_count_tree(const DcLoc *src, const DcEnt *e, int depth)
{
    if (!e->dir) return 1;
    if (depth >= DC_DEPTH) return 0;
    DcLoc s2 = *src;
    if (src->k == 2) s2.clus = e->me.clus; else dc_loc_join(src, e->name, s2.path, sizeof s2.path);
    DcEnt *lv = dc_lv[depth + 1];
    int c = 1;
    for (int skip = 0;;) {
        dc_tl_skip = skip;
        int n = dc_tree_list(&s2, lv, DC_MAX), nxt = dc_tl_next;
        for (int i = 0; i < n; i++) c += dc_count_tree(&s2, &lv[i], depth + 1);
        if (nxt <= skip || n <= 0) break;
        skip = nxt;
    }
    return c;
}
static void dc_copy_go(void);
/* 4.0.28: 받는 쪽에 같은 이름이 이미 있는지 (맨 위 항목만 — 폴더는 합쳐지므로 파일·폴더 이름 모두) */
static int dc_dest_has(int q, const char *name)
{
    uint8_t raw[11]; char n83[16];
    mf_make83(name, raw); dc_raw_name(raw, n83);
    if (DCI(q)) {
        for (int i = 0; i < dc_img_count(); i++) {
            const char *in = dc_img_iname(i);
            if (!name_cmp(in, name)) return 1;
            if (dc_kind == DCK_FAT && !name_cmp(dc_fe[i].key, n83)) return 1;
        }
        return 0;
    }
    for (int i = 0; i < dc_un[q]; i++) {
        const char *k = dc_ue[q][i].key[0] ? dc_ue[q][i].key : dc_ue[q][i].name;
        if (!name_cmp(dc_ue[q][i].name, name) || !name_cmp(k, name)) return 1;
    }
    return 0;
}
/* 복사할 수 있는 창 조합인가 (원본 p → 대상 q). 이미지는 파티션 목록이 아닐 때, USB는 폴더 안일 때 (대상은 zip 안이 아닐 때) */
static int dc_copy_ok(int p, int q)
{
    if ((DCI(p) && dc_kind == DCK_PART) || (DCI(q) && dc_kind == DCK_PART)) { dc_msg_set(0, T(S_DC_NOIMG), 0); return 0; }
    if (!dci[q].open && (dc_zip[q] || !dc_upath[q][0])) { dc_msg_set(0, T(S_DC_NOFOLDER), 0); return 0; }   /* 4.0.3: zip 안으로는 쓰지 않음 */
    if (!dci[p].open && !dc_zip[p] && !dc_upath[p][0]) return 0;
    if (!dci[0].open && !dci[1].open && !dc_zip[0] && !dc_zip[1] && dc_same_path(dc_upath[0], dc_upath[1])) { dc_msg_set(0, T(S_DC_SAMEDIR), 0); return 0; }
    return 1;
}
static void dc_copy(void)
{
    int p = dc_pane, q = !p, rows = dc_count(p);
    if (!dc_copy_ok(p, q)) return;
    int any = dc_count_marked(p), nover = 0;
    dc_ovr_name[0] = 0;
    for (int r = 0; r < rows; r++) {
        if (!dc_row_on(p, r, any)) continue;
        int it = dc_item(p, r);
        if (it < 0) continue;
        const char *nm = (DCI(p)) ? dc_img_iname(it) : (dc_ue[p][it].name);
        if (dc_dest_has(q, nm)) { if (!nover) copy_str(dc_ovr_name, sizeof dc_ovr_name, nm); nover++; }
    }
    if (nover) { dc_ask = 2; dc_ask_n = nover; return; }   /* 같은 이름이 있으면 먼저 물어봄 */
    { int n = 0; for (int r = 0; r < rows; r++) if (dc_row_on(p, r, any) && dc_item(p, r) >= 0) { n++; if (n == 1) { int it = dc_item(p, r); copy_str(dc_ovr_name, sizeof dc_ovr_name, DCI(p) ? dc_img_iname(it) : dc_ue[p][it].name); } }
      if (!n) return; dc_ask = 3; dc_ask_n = n; }   /* 8.0.40: 없으면 "복사할까요?" */
}
static void dc_copy_go(void)
{
    int p = dc_pane, q = !p, rows = dc_count(p);
    if (!dc_copy_ok(p, q)) return;
    DcLoc src, dst; dc_loc_pane(p, &src); dc_loc_pane(q, &dst);
    DcStat st; memset(&st, 0, sizeof st);
    st.buf = dc_xbuf(&st.bmax);
    int any = dc_count_marked(p), n = 0;
    for (int r = 0; r < rows && n < DC_MAX; r++) {      /* 맨 위 항목 (목록을 다른 데 쓰기 전에 모아 둠) */
        if (!dc_row_on(p, r, any)) continue;
        int it = dc_item(p, r);
        if (it < 0) continue;
        if (DCI(p)) { if (dc_kind == DCK_FAT) dc_ent_from_fat(&dc_lv[0][n++], &dc_fe[it]); else dc_ent_from_mf(&dc_lv[0][n++], &dc_me[it]); }
        else dc_ent_from_fe(&dc_lv[0][n++], &dc_ue[p][it]);
    }
    {
        int total = 0;
        for (int i = 0; i < n; i++) total += dc_count_tree(&src, &dc_lv[0][i], 0);
        dc_prog_begin(1, total, DCI(p) ? dc_img_name : dc_udisp[p], DCI(q) ? dc_img_name : dc_udisp[q]);
    }
    for (int i = 0; i < n; i++) dc_tree(&src, &dc_lv[0][i], &dst, 0, &st);
    dc_prog_end();
    int wrote_img = DCI(q) && (st.files || st.dirs);
    if (wrote_img && dc_save_image()) { dc_msg_set(0, T(S_DC_SAVEFAIL), dc_img_name); }
    for (int k = 0; k < 2; k++) {                       /* 두 창 다시 읽기 (임시로 목록 자리를 썼음) */
        int s = dc_sel[k];
        if (DCI(k)) dc_load_image_dir(0); else dc_load_usb(k, 0);
        dc_sel[k] = s < dc_count(k) ? s : 0;
    }
    if (wrote_img && dc_msg[0] && !dc_ok) return;
    char t[200] = "";
    num_append(t, sizeof t, (uint32_t)st.files, 0, ' '); str_append(t, sizeof t, T(S_DC_FILES));
    if (st.dirs) { str_append(t, sizeof t, ", "); num_append(t, sizeof t, (uint32_t)st.dirs, 0, ' '); str_append(t, sizeof t, T(S_DC_FOLDERS)); }
    if (st.skip) { str_append(t, sizeof t, ", "); num_append(t, sizeof t, (uint32_t)st.skip, 0, ' '); str_append(t, sizeof t, " "); str_append(t, sizeof t, T(S_DC_SKIPPED)); }
    if (st.renamed) { str_append(t, sizeof t, ", "); str_append(t, sizeof t, T(S_DC_RENAMED)); }
    if (st.err) {
        dc_msg_set(0, T((StrId)st.err), st.err_name);
        if (st.failed > 1) { str_append(dc_msg, sizeof dc_msg, " +"); num_append(dc_msg, sizeof dc_msg, (uint32_t)(st.failed - 1), 0, ' '); }
        if (st.files || st.dirs) { str_append(dc_msg, sizeof dc_msg, "  ("); str_append(dc_msg, sizeof dc_msg, T(S_DC_COPIED)); str_append(dc_msg, sizeof dc_msg, ": "); str_append(dc_msg, sizeof dc_msg, t); str_append(dc_msg, sizeof dc_msg, ")"); }
    } else if (dc_cancel) dc_msg_set(1, T(S_DC_CANCELED), t);
    else dc_msg_set(1, T(S_DC_COPIED), t);
}

/* 지울 수 있는 줄인가: 이미지 안 = 파일·빈 폴더(디스크), USB·이미지 고르기 = 파일만 (넣어 둔 이미지는 안 됨) */
static int dc_can_delete(int p, int r)
{
    int it = dc_item(p, r);
    if (it < 0 || dc_zip[p]) return 0;              /* 4.0.3: zip 안은 지우지 않음 */
    if (DCI(p)) return dc_kind == DCK_PART ? 0 : dc_kind == DCK_FAT ? !dc_fe[it].is_dir : 1;
    return 1;                                           /* 4.0.29: USB 폴더도 (안의 것까지) */
}
static void dc_delete_ask(void)
{
    int p = dc_pane, rows = dc_count(p), any = dc_count_marked(p), n = 0;
    for (int r = 0; r < rows; r++) if (dc_row_on(p, r, any) && dc_can_delete(p, r)) n++;
    if (!n) return;
    dc_ask = 1; dc_ask_n = n;
}
static void dc_delete(void)
{
    int p = dc_pane, rows = dc_count(p), any = dc_count_marked(p), done = 0, busy = 0, pend_ = 0;
    dc_ask = 0;
    {
        int total = 0;
        for (int r = 0; r < rows; r++) if (dc_row_on(p, r, any) && dc_can_delete(p, r)) total++;
        dc_prog_begin(2, total, DCI(p) ? dc_img_name : dc_udisp[p], "");
    }
    for (int r = 0; r < rows; r++) {
        if (!dc_row_on(p, r, any) || !dc_can_delete(p, r)) continue;
        int it = dc_item(p, r);
        if (pend_) dc_prog_done();
        pend_ = 1;
        dc_prog_item(dc_row_name(p, r), 0);
        if (DCI(p)) {
            if (dc_kind == DCK_FAT) { char ip[PATH_MAX_FE]; dc_fat_child(ip, sizeof ip, dc_fe[it].key); if (!fat_delete(&dc_vols[0], ip)) done++; }
            else if (!mf_delete(&dc_fs, &dc_me[it])) done++;
            continue;
        }
        char path[PATH_MAX_FE];
        dc_path_child(path, sizeof path, p, &dc_ue[p][it]);
        if (dc_ue[p][it].is_dir) { if (path_in_use(path)) busy++; else if (P.delete_file && !del_tree(path, 0)) done++; continue; }   /* 4.0.29 */
        int inuse = 0;
        for (int d = 0; d < 2; d++) if (disk_size[d] && dc_same_path(disk_path[d], path)) inuse = 1;
        if ((hd_secs && dc_same_path(hd_path, path)) || (hd2_secs && dc_same_path(hd2_path, path))) inuse = 1;
        if (inuse) { busy++; continue; }
        if (P.delete_file && !P.delete_file(path)) done++;
    }
    if (pend_) dc_prog_done();
    dc_prog_end();
    if (DCI(p)) {
        if (done && dc_save_image()) { dc_msg_set(0, T(S_DC_SAVEFAIL), dc_img_name); return; }
        int s = dc_sel[p]; dc_load_image_dir(0); dc_sel[p] = s < dc_count(p) ? s : dc_count(p) - 1;
    } else {
        int s = dc_sel[p]; dc_load_usb(p, 0); dc_sel[p] = s < dc_count(p) ? s : dc_count(p) - 1;
        dc_refresh_peer(p);   /* 3.8.3 */
    }
    char t[48] = ""; num_append(t, sizeof t, (uint32_t)done, 0, ' '); str_append(t, sizeof t, T(S_DC_FILES));
    if (busy) dc_msg_set(0, T(S_DC_INUSE_DEL), t);
    else dc_msg_set(done > 0, T(done ? S_DC_DELETED : S_DC_DEL_FAIL), done ? t : 0);
}

/* 이름 바꾸기 */
static void dc_rename_start(void)
{
    int p = dc_pane, r = dc_sel[p], it = dc_item(p, r);
    if (it < 0 || dc_zip[p] || (DCI(p) && (dc_kind == DCK_PART || (dc_kind == DCK_FAT && dc_fe[it].is_dir)))) return;
    if (!DCI(p) && dc_ue[p][it].is_dir) return;   /* USB 폴더는 바꾸지 않음 */
    copy_str(dc_ren_old, sizeof dc_ren_old, dc_row_name(p, r));
    copy_str(dc_ren_name, sizeof dc_ren_name, dc_ren_old);
    dc_ren = 1;
}
/* USB(또는 FAT16 이미지) 파일: 새 이름으로 복사한 뒤 옛 파일 지우기 */
static int dc_rename_copy(int p, int it, const char *newname)
{
    uint32_t bmax, n = 0; uint8_t *buf = dc_xbuf(&bmax);
    if (DCI(p)) {                            /* FAT16 이미지 */
        char op[PATH_MAX_FE], np[PATH_MAX_FE];
        uint8_t raw[11]; char n83[16];
        mf_make83(newname, raw); dc_raw_name(raw, n83);
        dc_fat_child(op, sizeof op, dc_fe[it].key); dc_fat_child(np, sizeof np, n83);
        for (int i = 0; i < dc_mn; i++) if (!name_cmp(dc_fe[i].key, n83)) return S_DC_EXISTS;
        if (dc_fe[it].size > bmax) return S_DC_TOOBIG;
        if (fat_read_file(&dc_vols[0], op, buf, bmax, &n)) return S_DC_READFAIL;
        if (fat_create(&dc_vols[0], np, 0) || fat_write_file(&dc_vols[0], np, buf, n)) return S_DC_NOSPACE;
        fat_delete(&dc_vols[0], op);
        return 0;
    }
    FeDirEntry *x = &dc_ue[p][it];
    char op[PATH_MAX_FE], np[PATH_MAX_FE];
    dc_path_child(op, sizeof op, p, x);
    copy_str(np, sizeof np, dc_upath[p]); str_append(np, sizeof np, "\\"); str_append(np, sizeof np, newname);
    for (int i = 0; i < dc_un[p]; i++) if (!name_cmp(dc_ue[p][i].key[0] ? dc_ue[p][i].key : dc_ue[p][i].name, newname)) return S_DC_EXISTS;
    for (int d = 0; d < 2; d++) if (disk_size[d] && dc_same_path(disk_path[d], op)) return S_DC_INUSE_DEL;
    if ((hd_secs && dc_same_path(hd_path, op)) || (hd2_secs && dc_same_path(hd2_path, op))) return S_DC_INUSE_DEL;
    if (x->size > bmax) return S_DC_TOOBIG;
    if (!P.read_file || P.read_file(op, buf, bmax, &n) || n > bmax) return S_DC_READFAIL;
    if (!P.create || !P.write_file || P.create(np, 0) || P.write_file(np, buf, n)) return S_DC_WRITEFAIL;
    if (P.delete_file) P.delete_file(op);
    return 0;
}
static void dc_rename_do(void)
{
    int p = dc_pane, r = dc_sel[p], it = dc_item(p, r), e = 0;
    dc_ren = 0;
    if (it < 0 || !dc_ren_name[0] || !strcmp(dc_ren_name, dc_ren_old)) return;
    char shown[64]; copy_str(shown, sizeof shown, dc_ren_name);
    if (DCI(p) && dc_kind == DCK_MF) {       /* 디스크 이미지: 디렉터리 항목 이름만 */
        uint8_t raw[11];
        mf_make83(dc_ren_name, raw); dc_raw_name(raw, shown);
        if (mf_rename(&dc_fs, dc_cur_dir(), &dc_me[it], raw)) e = S_DC_EXISTS;
        else if (dc_save_image()) e = S_DC_SAVEFAIL;
    } else {
        e = dc_rename_copy(p, it, dc_ren_name);
        if (!e && DCI(p)) { uint8_t raw[11]; mf_make83(dc_ren_name, raw); dc_raw_name(raw, shown); if (dc_save_image()) e = S_DC_SAVEFAIL; }
    }
    if (DCI(p)) dc_load_image_dir(shown); else { dc_load_usb(p, shown); dc_refresh_peer(p); }   /* 3.8.3 */
    if (e) dc_msg_set(0, T((StrId)e), dc_ren_old); else dc_msg_set(1, T(S_DC_REN_OK), shown);
}

/* 4.0.29: 새 폴더 (Ins) */
static void dc_mkdir_start(void)
{
    int p = dc_pane;
    if (dc_zip[p]) return;                          /* zip 안에는 못 만듦 */
    if (DCI(p) && dc_kind == DCK_PART) return;
    if (!DCI(p) && !dc_upath[p][0]) return;             /* 드라이브 목록 */
    dc_ren_old[0] = 0;
    copy_str(dc_ren_name, sizeof dc_ren_name, "NEWFOLDER");
    dc_ren = 2;
}
static void dc_mkdir_do(void)
{
    int p = dc_pane, e = 0;
    dc_ren = 0;
    if (!dc_ren_name[0]) return;
    char shown[64]; copy_str(shown, sizeof shown, dc_ren_name);
    if (DCI(p)) {
        if (dc_kind == DCK_MF) {
            uint8_t raw[11]; uint16_t date, time, out = 0;
            mf_make83(dc_ren_name, raw); dc_raw_name(raw, shown);
            dc_dos_date(&date, &time, 0);
            if (mf_mkdir(&dc_fs, dc_cur_dir(), raw, date, time, &out)) e = S_DC_NOSPACE;
            else if (dc_save_image()) e = S_DC_SAVEFAIL;
        } else {
            char np[PATH_MAX_FE]; dc_fat_child(np, sizeof np, dc_ren_name);
            if (fat_create(&dc_vols[0], np, 1)) e = S_DC_NOSPACE;
            else if (dc_save_image()) e = S_DC_SAVEFAIL;
        }
        dc_load_image_dir(shown);
    } else {
        char np[PATH_MAX_FE];
        copy_str(np, sizeof np, dc_upath[p]); str_append(np, sizeof np, "\\"); str_append(np, sizeof np, dc_ren_name);
        for (int i = 0; i < dc_un[p]; i++) if (!name_cmp(dc_ue[p][i].key[0] ? dc_ue[p][i].key : dc_ue[p][i].name, dc_ren_name)) e = S_DC_EXISTS;
        if (!e && (!P.create || P.create(np, 1))) e = S_DIR_MKFAIL;
        dc_load_usb(p, shown); dc_refresh_peer(p);
    }
    if (e) dc_msg_set(0, T((StrId)e), shown); else dc_msg_set(1, T(S_DIR_MADE), shown);
}
/* 4.0.29: F4 = 고른 USB 파일을 텍스트 편집기로 */
static void dc_edit_start(void)
{
    int p = dc_pane, r = dc_sel[p], it = dc_item(p, r);
    if (it < 0 || (DCI(p))) return;
    FeDirEntry *x = &dc_ue[p][it];
    if (x->is_dir) return;
    if (dc_zip[p]) {                                    /* zip 안 글 파일 = 보기 */
        char zp[PATH_MAX_FE];
        if (dc_zdir[p][0]) { copy_str(zp, sizeof zp, dc_zdir[p]); str_append(zp, sizeof zp, "\\"); } else zp[0] = 0;
        str_append(zp, sizeof zp, x->key[0] ? x->key : x->name);
        for (char *c = zp; *c; c++) if (*c == '\\') *c = '/';
        ed_open_zip(dc_zbuf(p), dc_zlen[p], zp, x->key[0] ? x->key : x->name);
        return;
    }
    char path[PATH_MAX_FE];
    dc_path_child(path, sizeof path, p, x);
    ed_open(path, x->key[0] ? x->key : x->name);
}

static void open_disk_copy(void)
{
    if (!dc_init) {
        dc_init = 1;
        if (P.create) { P.create(FE_STORE, 1); P.create(FE_DISK, 1); }
        for (int q = 0; q < 2; q++) {
            if (dc_ini_path[q][0] && P.list_dir_all && P.list_dir_all(dc_ini_path[q], dc_ue[q], 1) >= 0) { copy_str(dc_upath[q], sizeof dc_upath[q], dc_ini_path[q]); copy_str(dc_udisp[q], sizeof dc_udisp[q], dc_ini_disp[q]); }   /* 4.0.31: 지난번 폴더 */
            else { copy_str(dc_upath[q], sizeof dc_upath[q], FE_DISK); copy_str(dc_udisp[q], sizeof dc_udisp[q], FE_DISP_STORE " > DISK"); }
        }
    }
    dc_inside = 1;
    dc_zip_close_quiet();                               /* 4.0.3: 지난번에 열어 둔 zip */
    dc_msg[0] = 0; dc_ask = 0; dc_ren = 0; dc_pane = 0;
    dc_close_image(0); dc_close_image(1);
    dc_load_usb(0, dc_mem_sel[0][0] ? dc_mem_sel[0] : 0);
    dc_load_usb(1, dc_mem_sel[1][0] ? dc_mem_sel[1] : 0);
    for (int d = 0; d < 2 && !dc_mem_usb; d++)         /* 드라이브 A(없으면 B)에 넣은 디스크를 바로 엶 (지난번 USB 폴더를 보다 닫았으면 그 자리 유지) */
        if (disk_size[d] && disk_path[d][0]) { dc_open_image(0, disk_path[d], disk_name[d], disk_size[d]); break; }
    open_dialog(13);
}

/* 마우스: 가리키면 그 창·줄, 고른 줄을 다시 누르면 Enter, 표시 칸(x < c)을 누르면 Space */
static void hc_dc(const UiHit *h, int ev)
{
    if (dc_ask || dc_ren) return;
    int p = (int)h->a, row = h->b, was = dc_pane == p && dc_sel[p] == row;
    if (dc_pane != p || dc_sel[p] != row) { dc_pane = p; dc_sel[p] = row; menu_dirty = 1; }
    if (ev != 1) return;
    if (ptr_x < (uint32_t)h->c) ui_press(0x39, 0);
    else if (was) ui_press(0x1C, 0);
}

static void dc_size_str(char *t, int max, uint32_t n)
{
    t[0] = 0;
    if (n < 10000) { num_append(t, max, n, 0, ' '); str_append(t, max, " B"); }
    else if (n < 10000u * 1024) { num_append(t, max, (n + 1023) / 1024, 0, ' '); str_append(t, max, " KB"); }
    else { num_append(t, max, (n + (1u << 19)) >> 20, 0, ' '); str_append(t, max, " MB"); }
}

static void dc_draw_pane(int p, uint32_t X, uint32_t top, uint32_t W, uint32_t bottom)
{
    int focus = dc_pane == p;
    uint32_t rh = list_rh(), hh = LH * 2 + 20 * S;
    ui_rframe(X, top, W, bottom - top, 8 * S, focus ? C_ACC : C_LINE, C_CARD);
    /* 머리: 제목 + 위치 */
    const char *title = DCI(p) ? T(S_DC_IMAGE) : p == 1 ? T(S_DC_USB) : T(S_DC_PICK);
    uint32_t tx = X + 14 * S, tw = W - 28 * S;
    text_clip(tx, top + 8 * S, title, focus ? C_WARN : C_MUTED, 0xFF, tw / 2);
    char info[96] = "";
    if (DCI(p)) {
        if (dc_kind == DCK_MF) {
            char fr[24]; dc_size_str(fr, sizeof fr, mf_free(&dc_fs));
            str_append(info, sizeof info, dc_hd ? "FAT12  ·  " : ""); str_append(info, sizeof info, T(S_DC_FREE)); str_append(info, sizeof info, " "); str_append(info, sizeof info, fr);
        } else if (dc_kind == DCK_FAT) {
            char sz[24]; dc_size_str(sz, sizeof sz, (uint32_t)(dc_vols[0].bytes > 0xFFFFFFFFull ? 0xFFFFFFFFu : dc_vols[0].bytes));
            str_append(info, sizeof info, dc_vols[0].fat32 ? "FAT32  ·  " : "FAT16  ·  "); str_append(info, sizeof info, sz);
        }
    }
    int nm = dc_count_marked(p);
    if (nm) { if (info[0]) str_append(info, sizeof info, "  ·  "); num_append(info, sizeof info, (uint32_t)nm, 0, ' '); str_append(info, sizeof info, T(S_DC_MARKED)); }
    if (info[0]) small_ell(X + W - 14 * S - ui_sw(info), top + 8 * S + (LH - SLH) / 2, info, C_TEXT, 0xFF, tw / 2);
    char where[PATH_MAX_FE + 96] = "";
    if (DCI(p)) {
        str_append(where, sizeof where, dc_img_name);
        if (dc_hd && dc_vol >= 0) {
            if (dc_nparts > 1) { str_append(where, sizeof where, " \\ "); str_append(where, sizeof where, T(S_DC_PART)); str_append(where, sizeof where, " "); num_append(where, sizeof where, (uint32_t)dc_vol + 1, 0, ' '); }
            for (const char *q = dc_fpath; *q; q++) { if (*q == '\\') str_append(where, sizeof where, " \\ "); else { char c[2] = { *q, 0 }; str_append(where, sizeof where, c); } }
        }
        for (int i = 0; dc_kind == DCK_MF && i < dc_depth; i++) { str_append(where, sizeof where, " \\ "); str_append(where, sizeof where, dc_dname[i]); }
    } else {
        str_append(where, sizeof where, T(S_DRIVES));
        if (dc_udisp[p][0]) { str_append(where, sizeof where, "  ›  "); str_append(where, sizeof where, dc_udisp[p]); }
    }
    ui_rrect(tx - 4 * S, top + 12 * S + LH, tw + 8 * S, LH + 4 * S, 4 * S, C_FIELD);
    small_ell(tx + 4 * S, top + 14 * S + LH + (LH - SLH) / 2, where, C_TEXT, 0xFF, tw - 8 * S);
    /* 목록 */
    uint32_t fh = SLH + 10 * S;                         /* 4.0.29: 아래 칸 = 가리키는 파일 이름 (작은 글씨) */
    uint32_t ly = top + hh + 8 * S, lh = bottom - 8 * S - fh > ly ? bottom - 8 * S - fh - ly : 0;
    int vis = (int)(lh / rh), n = dc_count(p);
    if (vis < 1) vis = 1;
    if (dc_sel[p] >= n) dc_sel[p] = n ? n - 1 : 0;
    if (dc_sel[p] < dc_top[p]) dc_top[p] = dc_sel[p];
    if (dc_sel[p] >= dc_top[p] + vis) dc_top[p] = dc_sel[p] - vis + 1;
    uint32_t LX = X + 8 * S, LW = W - 22 * S, mk = LX + 8 * S, ix = mk + LH, nx = ix + LH + 8 * S;
    uint32_t szw = text_w("0000 KB") + 10 * S;
    for (int r = 0; r < vis && dc_top[p] + r < n; r++) {
        int row = dc_top[p] + r, it = dc_item(p, row), sel = row == dc_sel[p], dir = dc_row_dir(p, row);
        uint32_t y = ly + (uint32_t)r * rh;
        ui_list_row_bg(LX, y, LW, rh, sel, 0, focus);
        hit_add(hc_dc, p, row, (int)ix, 0, LX, y, LW, rh);
        uint8_t bg = sel ? (focus ? C_ACC2 : C_ROWSEL) : C_CARD;
        if (dc_mark[p][row]) ui_rrect(mk, y + (rh - LH / 2) / 2, LH / 2, LH / 2, LH / 4, C_WARN);
        const char *name = dc_row_name(p, row);
        uint32_t size = 0;
        if (it >= 0) size = DCI(p) ? dc_img_isize(it) : dc_ue[p][it].size;
        int isf = !dir && !(DCI(p));         /* 4.0.30: 파일 선택창과 같은 file_type_sz / file_look 를 씀 (아이콘·색이 같음) */
        int ftk = 0, fic = ICON_FILE; uint8_t icol = C_OK;
        if (isf) { ftk = file_type_sz(it >= 0 && dc_ue[p][it].key[0] ? dc_ue[p][it].key : name, size);   /* 4.0.30: 긴 이름은 name이 잘려 확장자가 없으므로 key(전체 이름)로 */
            file_look(ftk, &fic, &icol); draw_icon_hd(ix, y + (rh - LH) / 2, LH, fic, icol, 15, bg); }
        else draw_icon(ix, y + (rh - LH) / 2 + LH / 8, dir, bg);
        uint8_t fg = dc_mark[p][row] ? C_WARN : sel ? 15 : dir ? C_WARN : ftk ? icol : C_TEXT;
        text_clip(nx, ui_ty(y, rh), name, fg, 0xFF, LX + LW - nx - szw - 8 * S);
        if (it >= 0 && !dir) {
            char t[24]; dc_size_str(t, sizeof t, size);
            text_clip(LX + LW - 10 * S - text_w(t), ui_ty(y, rh), t, sel ? 15 : C_MUTED, 0xFF, szw);
        }
    }
    ui_sbar(X + W - 12 * S, ly, (uint32_t)vis * rh, &dc_top[p], &dc_sel[p], vis, n);
    {
        int it = n ? dc_item(p, dc_sel[p]) : -1;
        const char *fn = it < 0 ? (n ? ".." : "") : (DCI(p)) ? dc_img_iname(it) : dc_ue[p][it].key[0] ? dc_ue[p][it].key : dc_ue[p][it].name;
        uint32_t fy = bottom - 6 * S - fh;
        ui_rrect(tx - 4 * S, fy, tw + 8 * S, fh, 4 * S, C_FIELD);
        char cb[64] = "";                               /* 4.0.125: 항목 수 / 고른(Space) 수 — 이름 칸 오른쪽 끝 */
        int total = n - dc_has_up(p); if (total < 0) total = 0;
#ifdef FE_UEFI
        if (!(DCI(p)) && !dc_zip[p] && dcx_big[p]) total = dcx_n[p];
#endif
        num_append(cb, sizeof cb, (uint32_t)total, 0, ' ');
        int bigp = 0;
#ifdef FE_UEFI
        bigp = !(DCI(p)) && !dc_zip[p] && dcx_big[p];
#endif
        if (!(DCI(p)) && dc_pnext[p] >= 0 && !bigp) str_append(cb, sizeof cb, "+");
#ifdef FE_UEFI
        if (!(DCI(p)) && !dc_zip[p] && dcx_big[p] && dcx_cut[p]) str_append(cb, sizeof cb, "+");
#endif
        str_append(cb, sizeof cb, lang == 0 ? "개 항목 / " : lang == 2 ? " 項目 / " : " items / ");
        num_append(cb, sizeof cb, (uint32_t)dc_count_marked(p), 0, ' ');
        str_append(cb, sizeof cb, lang == 0 ? "개 선택" : lang == 2 ? " 選択" : " selected");
        uint32_t cw = ui_sw(cb);
        small_ell(tx + tw + 4 * S - cw - 8 * S, fy + (fh - SLH) / 2, cb, C_MUTED, C_FIELD, cw);
        small_ell(tx + 4 * S, fy + (fh - SLH) / 2, fn, C_TEXT, 0xFF, tw - 8 * S - cw - 16 * S);
    }
}

static void draw_disk_copy(void)
{
    UiRect o;
    UiRect b = ui_full_card(T(S_DC_TITLE), T(S_DC_SUB), 0, &o);
    uint32_t pad = 14 * S, top = b.y + 12 * S, gap = 34 * S;   /* 4.0.31: 가운데 화살표 아이콘 자리 */
    uint32_t mh = dc_msg[0] ? LH + 16 * S : 0;
    uint32_t bottom = dlg_cmd_y() - 10 * S - (mh ? mh + 10 * S : 0);
    uint32_t W = (b.w - 2 * pad - gap) / 2;
    dc_draw_pane(0, b.x + pad, top, W, bottom);
    dc_draw_pane(1, b.x + pad + W + gap, top, W, bottom);
    /* 가운데 화살표: 복사 방향 */
    uint32_t ax = b.x + pad + W;
    { uint32_t as = gap - 4 * S; draw_icon_hd(ax + (gap - as) / 2, (top + bottom) / 2 - as / 2, as, dc_pane ? ICON_ARROWL : ICON_ARROWR, C_WARN, 15, C_CARD); }   /* 4.0.31: 복사 방향 화살표 (아이콘) */
    if (mh) {
        uint32_t y = bottom + 10 * S, X = b.x + pad, MW = b.w - 2 * pad;
        ui_rrect(X, y, MW, mh, 6 * S, C_HEAD);
        ui_rrect(X + 10 * S, y + (mh - LH / 2) / 2, LH / 2, LH / 2, LH / 4, dc_ok ? C_OK : C_BAD);
        text_clip(X + 20 * S + LH / 2, ui_ty(y, mh), dc_msg, C_TEXT, 0xFF, MW - LH - 30 * S);
    }
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, T(S_DC_KEYS));
    if (dc_pv) dc_pv_draw(&b);
    if (dc_ask || dc_ren) {                             /* 가운데 팝업: 지울까요? / 이름 바꾸기 */
        uint32_t pw = 620 * S, ph = 3 * LH + 2 * SLH + ui_cmdbar_h() + 52 * S;
        if (pw > b.w) pw = b.w;
        uint32_t px = b.x + (b.w - pw) / 2, py = top + (bottom - top) / 4;
        hit_add(hc_none, 0, 0, 0, 0, px, py, pw, ph);
        ui_rrect(px + 5 * S, py + 6 * S, pw, ph, 10 * S, C_SHADOW);
        ui_rframe(px, py, pw, ph, 10 * S, dc_ask ? C_BAD : C_ACC, C_CARD);
        uint32_t ty = py + 12 * S;
        text_f(F, S, px + 16 * S, ty, dc_ask == 3 ? grow_t("복사할까요?", "Copy?", "コピーしますか？") : T(dc_ask == 2 ? S_DC_OVR_Q : dc_ask ? S_DC_DEL_Q : dc_ren == 2 ? S_DIR_NEWDIR : S_DC_REN_TITLE), C_TEXT, 0xFF, 0, 1);
        ty += LH + 8 * S;
        if (dc_ask) {
            char t[96] = "";
            if (dc_ask >= 2) { if (dc_ask_n == 1) copy_str(t, sizeof t, dc_ovr_name); else { num_append(t, sizeof t, (uint32_t)dc_ask_n, 0, ' '); str_append(t, sizeof t, " "); str_append(t, sizeof t, dc_ask == 3 ? grow_t("개 항목을 복사합니다", " items will be copied", "件をコピーします") : T(S_DC_OVR_N)); } }
            else if (dc_ask_n == 1) { int r = dc_sel[dc_pane], any = dc_count_marked(dc_pane); for (int k = 0; k < dc_count(dc_pane); k++) if (dc_row_on(dc_pane, k, any) && dc_can_delete(dc_pane, k)) r = k; copy_str(t, sizeof t, dc_row_name(dc_pane, r)); }
            else { num_append(t, sizeof t, (uint32_t)dc_ask_n, 0, ' '); str_append(t, sizeof t, " "); str_append(t, sizeof t, T(S_DC_DEL_FILES)); }
            text_ell(px + 16 * S, ty, t, C_WARN, 0xFF, pw - 32 * S);
            ty += LH + 6 * S;
            small_ell(px + 16 * S, ty, dc_ask >= 2 ? (DCI(!dc_pane) ? dc_img_name : dc_udisp[!dc_pane]) : (DCI(dc_pane) ? dc_img_name : dc_udisp[dc_pane]), C_MUTED, C_CARD, pw - 32 * S);
            ui_cmdbar(px + S, py + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, dc_ask == 3 ? grow_t("Enter 복사   Esc 취소", "Enter Copy   Esc Cancel", "Enter コピー   Esc 取消") : T(dc_ask == 2 ? S_DC_OVR_KEYS : S_DEL_KEYS));
        } else {
            small_ell(px + 16 * S, ty, dc_ren == 2 ? (DCI(dc_pane) ? dc_img_name : dc_udisp[dc_pane]) : dc_ren_old, C_MUTED, C_CARD, pw - 32 * S);
            ty += SLH + 10 * S;
            uint32_t ih = LH + 10 * S;
            ui_rrect(px + 16 * S, ty, pw - 32 * S, ih, 5 * S, C_FIELD);
            ui_rrect(px + 16 * S, ty, 3 * S, ih, 1 * S, C_ACC);
            char nb[72]; copy_str(nb, sizeof nb, dc_ren_name); str_append(nb, sizeof nb, "_");
            text_ell(px + 28 * S, ui_ty(ty, ih), nb, C_TEXT, 0xFF, pw - 56 * S);
            ui_cmdbar(px + S, py + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(dc_ren == 2 ? S_DIR_NEW_KEYS : S_DC_REN_KEYS));
        }
    }
}

static void dc_remember(void)                           /* 4.0.31: 닫을 때 두 창의 폴더·커서 이름을 기억 */
{
    if (!dc_inside) return;
    dc_inside = 0;
    dc_mem_usb = !dci[0].open;
    for (int p = 0; p < 2; p++) {
        if (DCI(p)) continue;                /* 이미지를 열어 둔 왼쪽은 지난 기억을 그대로 */
        dc_mem_sel[p][0] = 0;
        if (dc_zip[p]) copy_str(dc_mem_sel[p], sizeof dc_mem_sel[p], dc_zname[p]);          /* zip 안이었으면 그 zip 파일에 */
        else if (!(DCI(p))) { int it = dc_item(p, dc_sel[p]); if (it >= 0 && it < DCU_MAX) copy_str(dc_mem_sel[p], sizeof dc_mem_sel[p], dc_ue[p][it].key[0] ? dc_ue[p][it].key : dc_ue[p][it].name); }
    }
    dc_zip_close_quiet();
    for (int p = 0; p < 2; p++) { if (DCI(p)) continue; copy_str(dc_ini_path[p], sizeof dc_ini_path[p], dc_upath[p]); copy_str(dc_ini_disp[p], sizeof dc_ini_disp[p], dc_udisp[p]); }
    settings_changed();
}

static void disk_copy_key(int key)
{
    menu_dirty = 1;
    if (dc_pv) {                                        /* 미리보기: > < 로 넘김, 그 밖의 키는 닫기 */
        if (key == 0x34) dc_pv_step(1);
        else if (key == 0x33) dc_pv_step(-1);
        else if (!mod_bit(key)) dc_pv = 0;
        return;
    }
    if (dc_ask) {                                       /* 지울까요? */
        if (key == 0x1C || key == 0x9C) { if (dc_ask >= 2) { dc_ask = 0; dc_copy_go(); } else dc_delete(); }
        else if (key == 0x01 || key == 0x0E) dc_ask = 0;
        return;
    }
    if (dc_ren) {                                       /* 이름 바꾸기 */
        int l = (int)strlen(dc_ren_name);
        if (key == 0x01) { dc_ren = 0; return; }
        if (key == 0x0E) { if (l) dc_ren_name[l - 1] = 0; return; }
        if (key == 0x1C || key == 0x9C) { if (dc_ren == 2) dc_mkdir_do(); else dc_rename_do(); return; }
        char c = key_char(key, (mods & 0x03) != 0);
        if (c && c != '\\' && c != '/' && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|' && l < 40) { dc_ren_name[l] = c; dc_ren_name[l + 1] = 0; }
        return;
    }
    int p = dc_pane, n = dc_count(p);
    switch (key) {
    case 0x01: dc_remember(); dc_close_image(0); dc_close_image(1); dialog_back(); return;
    case 0x0F: case 0xCB: case 0xCD:
        dc_pane = key == 0xCB ? 0 : key == 0xCD ? 1 : !dc_pane; return;
    case 0xC8: if (dc_sel[p] > 0) dc_sel[p]--; else dc_win_move(p, -1); return;
    case 0xD0: if (dc_sel[p] < n - 1) dc_sel[p]++; else dc_win_move(p, 1); return;
    case 0xC9: if (dc_sel[p] == 0 && dc_win_move(p, -1)) return; dc_sel[p] -= 10; if (dc_sel[p] < 0) dc_sel[p] = 0; return;
    case 0xD1: if (dc_sel[p] >= n - 1 && dc_win_move(p, 1)) return; dc_sel[p] += 10; if (dc_sel[p] >= n) dc_sel[p] = n - 1; return;
    case 0xC7: dc_sel[p] = 0; return;
    case 0xCF: dc_sel[p] = n - 1; return;
    case 0x1C: case 0x9C: dc_enter(p); return;
    case 0x0E: dc_up(p); return;
    case 0xD2: dc_mkdir_start(); return;                /* 4.0.29: Ins = 새 폴더 */
    case 0x39:                                          /* 고르기 (3.7.3: 폴더도) */
        if (dc_item(p, dc_sel[p]) >= 0 && dc_sel[p] <= DCU_MAX && !(DCI(p) && dc_kind == DCK_PART)) dc_mark[p][dc_sel[p]] ^= 1;
        if (dc_sel[p] < n - 1) dc_sel[p]++;
        return;
    case 0x3F: dc_copy(); return;                       /* F5 */
    case 0x3C: dc_rename_start(); return;               /* F2 */
    case 0xD3: case 0x53: dc_delete_ask(); return;      /* Del */
    }
    if (key == 0x1E && (mods & 0x0C)) {                 /* Ctrl+A: 모두 고르기 */
        for (int r = 0; r < n && r <= DCU_MAX; r++) if (dc_item(p, r) >= 0 && !(DCI(p) && dc_kind == DCK_PART)) dc_mark[p][r] = 1;
    }
}

/* ================================================================== */
/* 진단 (3.0.2): 탭 USB · 키보드 · 소리 · 마우스 (소리 진단을 합침)        */
/* ================================================================== */
static int diag_tab;
#define DG_TABS 11                                       /* 6 = 화면 (4.0.29: 작은 화면에서 USB 탭 아래 줄이 잘려 따로 뺌) */
#define DG_SW 10
#define DG_SUM 9                                        /* 요약 탭 (보이는 차례는 맨 앞) */
static const uint8_t dg_order[DG_TABS] = { DG_SUM, 0, 1, 2, 3, 4, 5, 6, 7, 8, DG_SW };   /* 화면 차례 → 탭 번호 */
static int dg_pos(int id) { for (int i = 0; i < DG_TABS; i++) if (dg_order[i] == id) return i; return 0; }
/* 5.2.26 진단 판정 (kernel/diag.c): 1초마다 다시 — 요약 탭·각 탭 맨 위 판정 줄·DIAG.TXT 가 같이 씀 */
static int diag_pc_sel = -1;                            /* -1 자동, 0~ 시험 PC 번호 (기본 표 + TESTPC.TXT), 그 밖 = 목록에 없음 (ini DiagPC) */
static DiagSum dg_sum[DA_N];
static char dg_sl[160][64];
static int dg_sn, dg_pc_i = -1, dg_pc_isauto;
static uint32_t dg_sum_at;
static uint32_t gap_show_pub(void);
static int dg_upc_done;
static void dg_upc_load(void)                           /* 사용자가 더한 시험 PC (USB TESTPC.TXT) — 처음 한 번 */
{
    if (dg_upc_done || !P.read_file) return;
    dg_upc_done = 1;
    static uint8_t tb[8192]; uint32_t n = 0;
    if (!P.read_file(DIAG_TESTPC_PATH, tb, sizeof tb - 1, &n) && n) diag_pc_user_load((const char *)tb, (int)n);
}
/* 8.0.22: 화면 맞추기 시점(줄)을 시험 PC 번호마다 ini(VsyncLinesPC<번호>)에 저장 — USB 하나를 여러 PC 에서 쓰므로 PC 마다 따로.
 * Lenovo V14(AMD): 기본 -94 에서 아래쪽 흐르는 글씨가 비뚤게 보였고 -126 에서 똑바로 (마스터님 확인 2026-10-30) */
static void dg_refresh(int force);
static int vs_pc_num(void)
{
    dg_refresh(0);
    const DiagPc *pc = dg_pc_i >= 0 ? diag_pc(dg_pc_i) : 0;
    return pc && pc->pc >= 0 && pc->pc < VS_PC_N ? pc->pc : -1;
}
static void vs_pc_store(int v)
{
    int n = vs_pc_num();
    if (n < 0) return;
    vs_pc_lines[n] = v; settings_changed();
}
static void vs_pc_apply(void)
{
    int n = vs_pc_num();
    if (n >= 0 && vs_pc_lines[n] != 0x7FFF && P.vs_adj) P.vs_adj(vs_pc_lines[n], 1);
}
static void dg_refresh(int force)
{
    if (!force && dg_sum_at && P.ms() - dg_sum_at < 1000) return;
    dg_sn = P.info ? P.info(dg_sl, 160) : 0;
    DiagCtx c = { snd_status, wc_status, vs_status, usb_status, machine_name, measured_speed, measured_busy, (int)gap_show_pub() };
    diag_judge(&c, dg_sl, dg_sn, dg_sum);
    dg_upc_load();
    if (diag_pc_sel < 0) { dg_pc_i = diag_pc_auto(dg_sl, dg_sn); dg_pc_isauto = 1; }
    else { dg_pc_i = diag_pc_sel < diag_pc_count() ? diag_pc_sel : -1; dg_pc_isauto = 0; }
    diag_expect(diag_pc(dg_pc_i), dg_sl, dg_sn, dg_sum);
    dg_sum_at = P.ms() | 1;
}
static const char *dg_area_name(int a)
{
    static const char *const an[3][DA_N] = {
        { "시스템", "화면", "소리", "키보드", "마우스", "터치패드", "조이스틱", "USB", "네트워크", "속도" },
        { "System", "Video", "Sound", "Keyboard", "Mouse", "Touchpad", "Joystick", "USB", "Network", "Speed" },
        { "システム", "画面", "サウンド", "キーボード", "マウス", "タッチパッド", "ジョイスティック", "USB", "ネットワーク", "速度" } };
    return an[lang == 0 ? 0 : lang == 2 ? 2 : 1][a];
}
static const char *dg_state_name(int st)
{
    static const char *const sn[3][5] = { { "정보", "정상", "주의", "실패", "진단 안 됨" }, { "info", "OK", "warn", "FAIL", "not diagnosed" }, { "情報", "正常", "要チェック", "失敗", "診断できず" } };
    return sn[lang == 0 ? 0 : lang == 2 ? 2 : 1][st];
}
static uint8_t dg_state_col(int st) { return st == DS_OK ? C_OK : st == DS_WARN ? C_WARN : st == DS_FAIL ? C_BAD : st == DS_NA ? C_BAD : C_MUTED; }
/* 판정 한 줄 (점 · 영역 · 상태 · [이유] 설명). 돌려줌: 높이 */
static uint32_t dg_verdict_row(uint32_t x, uint32_t y, uint32_t w, int a)
{
    uint32_t rh = LH + 10 * S, kw = w / 5, sw = w / 7;
    const DiagSum *d = &dg_sum[a];
    uint8_t col = dg_state_col(d->st);
    ui_rrect(x, y + (rh - LH / 2) / 2, LH / 2, LH / 2, LH / 4, col);
    text_clip(x + LH, ui_ty(y, rh), dg_area_name(a), C_TEXT, 0xFF, kw - LH);
    text_clip(x + kw, ui_ty(y, rh), dg_state_name(d->st), col, 0xFF, sw);
    char v[140] = "";
    if (d->why[0]) { str_append(v, sizeof v, "["); str_append(v, sizeof v, d->why); str_append(v, sizeof v, "] "); }
    str_append(v, sizeof v, d->text);
    small_ell(x + kw + sw, y + (rh - SLH) / 2, v, C_TEXT, 0xFF, w - kw - sw);
    return rh;
}                                    /* 0 USB 1 키보드 2 소리 3 마우스 4 조이스틱 5 터치패드 */
static int dg_jpad;                                     /* 3.8.1: 시험 중인 조이스틱 번호 */
static PadView dg_pv, dg_pv_prev;
static int dg_pv_ok = -1, dg_pv_ok_prev = -2;
static uint32_t dg_jbtn_seen;                           /* 한 번이라도 눌린 버튼 */
static int dg_jmin_x, dg_jmax_x, dg_jmin_y, dg_jmax_y;  /* 본 축 값의 범위 (범위를 못 읽는 장치용) */
static int dg_key = -1, dg_key_mods;
static uint32_t dg_key_n;
static int dg_mx, dg_my, dg_btn, dg_wheel;
static uint32_t dg_events, dg_last_ev;

static int gpu_sel = -1;                                   /* 4.0.58: 0 = AMD, 1 = Intel (Video 탭 V 키) */
/* 멈춤 추적 — 프레임 안 단계·코덱 명령을 메모리 고리(64줄)에만 남김 (USB 쓰기 없음, 거의 공짜).
 * UEFI 는 펌웨어 타이머가 프레임이 1.5초 넘게 안 바뀌면 이 고리를 LOG\HANG.TXT 로 씀 (fe_hang_dump). 진단 저장(DIAG.TXT)에도 붙임 */
static int tr_sync;                                     /* (5.2.22~23 의 바로 쓰기 — 이제 안 씀, 표시만) */
#define TR_N 128
static struct { uint32_t t; const char *s; char x[24]; uint8_t tab; } tr_ring[TR_N];
static volatile uint32_t tr_pos;
volatile uint32_t fe_frame_count;                       /* fe_frame 이 끝날 때마다 +1 (플랫폼 멈춤 감시) */
static void fe_trace(const char *what)
{
    uint32_t k = tr_pos % TR_N;
    tr_ring[k].t = P.ms ? P.ms() : 0; tr_ring[k].s = what; tr_ring[k].x[0] = 0; tr_ring[k].tab = (uint8_t)(dialog == 2 ? diag_tab : 255);
    tr_pos++;
}
static void fe_trace_str(const char *what, const char *x)   /* 문자열을 복사해 둠 (hda 명령 줄 등 임시 버퍼) */
{
    uint32_t k = tr_pos % TR_N;
    fe_trace(what);
    int i = 0; for (; x && x[i] && i < 23; i++) tr_ring[k].x[i] = x[i]; tr_ring[k].x[i] = 0;
}
int fe_hang_dump(char *b, int max)                      /* 고리를 글로 (오래된 것부터) */
{
    int q = 0;
#define HP(str) do { const char *s_ = (str); while (*s_ && q < max - 1) b[q++] = *s_++; } while (0)
    HP("X86MSX " MSXEMU_VERSION " HANG/TRACE (last steps, oldest first)\r\n");
    uint32_t n = tr_pos < TR_N ? tr_pos : TR_N;
    for (uint32_t i = tr_pos - n; i != tr_pos; i++) {
        uint32_t k = i % TR_N; char nb[16]; int m = 0; uint32_t v = tr_ring[k].t;
        char t[12]; int tn = 0; do { t[tn++] = (char)('0' + v % 10); v /= 10; } while (v); while (tn) nb[m++] = t[--tn]; nb[m] = 0;
        HP("t="); HP(nb);
        if (tr_ring[k].tab != 255) { char tb[8] = " tab 0"; tb[5] = (char)('0' + tr_ring[k].tab % 10); HP(tb); }
        HP(" "); HP(tr_ring[k].s ? tr_ring[k].s : "?");
        if (tr_ring[k].x[0]) { HP(" "); HP(tr_ring[k].x); }
        HP("\r\n");
    }
    b[q] = 0;
    return q;
}

/* 멈췄다가 다시 돈 경우 (HANG.TXT 는 다음 멈춤이 덮어씀) — 멈춘 길이와 그때 고리를 LOG\STALL.TXT 에 모아 둠 (최대 4번) */
static char stall_buf[4][2600]; static int stall_n, stall_due;
static void stall_note(uint32_t ms)
{
    int k = stall_n < 4 ? stall_n : 3;
    char *b = stall_buf[k]; int q = 0;
#define SP(str) do { const char *s_ = (str); while (*s_ && q < (int)sizeof stall_buf[0] - 1) b[q++] = *s_++; } while (0)
    char nb[16]; { int m = 0; uint32_t v = ms; char t[12]; int tn = 0; do { t[tn++] = (char)('0' + v % 10); v /= 10; } while (v); while (tn) nb[m++] = t[--tn]; nb[m] = 0; }
    SP("== stall "); SP(nb); SP(" ms (frames stopped, then resumed) ==\r\n");
    b[q] = 0;
    { int r = fe_hang_dump(b + q, (int)sizeof stall_buf[0] - q); q += r; }
    b[q] = 0;
#undef SP
    if (stall_n < 4) stall_n++;
    stall_due = 1;
}
static void stall_flush(void)
{
    if (!stall_due || !P.write_file) return;
    stall_due = 0;
    if (!log_stall) return;
    static char ob[4 * 2600 + 200]; int q = 0;
    const char *h = "#LOG 1\r\n#prog=X86MSX ver=" MSXEMU_VERSION "\r\n#what=frames stopped >1 s then resumed; trace ring at that moment (oldest first)\r\n";
    while (*h) ob[q++] = *h++;
    for (int i = 0; i < stall_n; i++) for (const char *c = stall_buf[i]; *c && q < (int)sizeof ob - 1; c++) ob[q++] = *c;
    if (P.create) { P.create("0:\\LOG", 1); P.create("0:\\LOG\\STALL.TXT", 0); }
    P.write_file("0:\\LOG\\STALL.TXT", (const uint8_t *)ob, (uint32_t)q);
}
static uint32_t dg_t_info, dg_t_snd, dg_save_due;   /* dg_save_due: 이 시각에 DIAG.TXT 다시 저장 (0 = 없음) */                 /* 진단 창 그리기에서 P.info·P.sound_diag 가 걸린 ms (최대) */
static void diag_save_now(void)                      /* 4.0.37 → 5.2.26: LOG\DIAG.TXT = 공용 로그 규약 머리 + 영역 요약(SUM) + 영역별 줄 + 소리 코덱 + 멈춤 추적 */
{
    static char hb[24000];
    if (!P.diag_save || !log_diag) return;
    dg_refresh(1);
    DiagCtx c = { snd_status, wc_status, vs_status, usb_status, machine_name, measured_speed, measured_busy, (int)gap_show };
    int q = diag_format(hb, sizeof hb - 9000, MSXEMU_VERSION_FULL " (" MSXEMU_BUILD ")", &c, dg_sl, dg_sn, dg_sum, diag_pc(dg_pc_i), dg_pc_isauto);
    hb[q] = 0;
    str_append(hb, sizeof hb, "== PERF ==\r\nspeed ");
    num_append(hb, sizeof hb, (uint32_t)measured_speed, 0, ' ');
    str_append(hb, sizeof hb, " %  busy ");
    num_append(hb, sizeof hb, (uint32_t)measured_busy, 0, ' ');
    str_append(hb, sizeof hb, " %  frame gap max ");
    num_append(hb, sizeof hb, gap_show, 0, ' ');
    str_append(hb, sizeof hb, " ms\r\n");
    { char pt[160]; pf_text(pt, sizeof pt, 0); str_append(hb, sizeof hb, "time share "); str_append(hb, sizeof hb, pt); str_append(hb, sizeof hb, "\r\n"); pf_text(pt, sizeof pt, 1); str_append(hb, sizeof hb, "worst/s "); str_append(hb, sizeof hb, pt); str_append(hb, sizeof hb, "\r\n"); }
    { static const char *const gmn[] = { "off", "FM", "SF2", "MT32", "SC55" }; char ml[96]; ml[0] = 0;   /* MIDI 음원별 소리 나는 동안 평균 크기 (RMS, 목표 4000 ≈ PSG 3음 음량 13) */
      for (int gq = 0; gq < GM_INST_MAX; gq++) if (gmi_mode(gq) != GM_OFF && gmi_level(gq)) { if (ml[0]) str_append(ml, sizeof ml, "  "); num_append(ml, sizeof ml, (uint32_t)gq, 0, ' '); str_append(ml, sizeof ml, ":"); str_append(ml, sizeof ml, gmn[gmi_mode(gq) & 7]); str_append(ml, sizeof ml, " "); num_append(ml, sizeof ml, (uint32_t)gmi_level(gq), 0, ' '); }
      if (ml[0]) { str_append(hb, sizeof hb, "midi rms "); str_append(hb, sizeof hb, ml); str_append(hb, sizeof hb, "\r\n"); } }
    str_append(hb, sizeof hb, "midi bytes "); num_append(hb, sizeof hb, msx_midi_int_bytes, 0, ' '); str_append(hb, sizeof hb, "  guard "); num_append(hb, sizeof hb, gm_guard_hits, 0, ' '); str_append(hb, sizeof hb, "  dropped "); num_append(hb, sizeof hb, (uint32_t)msx_midi_drop, 0, ' '); str_append(hb, sizeof hb, "\r\n");
    { char mt[768]; mt[0] = 0;   /* 8.0.1 다른 코어 시험 결과 (8.0.25: 512 → 768) */
      if (P.mp_text) P.mp_text(mt, sizeof mt);
      str_append(hb, sizeof hb, "mp test "); str_append(hb, sizeof hb, mt[0] ? mt : "-"); str_append(hb, sizeof hb, "\r\n"); }
    { int ct = 0, ce = 0, has = P.cpu_cores ? P.cpu_cores(&ct, &ce) : -1;   /* 5.3.56: CPU 코어 수 (읽기 전용 조회) */
      str_append(hb, sizeof hb, "cpu cores ");
      if (has > 0) { num_append(hb, sizeof hb, (uint32_t)ct, 0, ' '); str_append(hb, sizeof hb, " enabled "); num_append(hb, sizeof hb, (uint32_t)ce, 0, ' '); str_append(hb, sizeof hb, "  MP Services yes\r\n"); }
      else str_append(hb, sizeof hb, has == 0 ? "? MP Services no\r\n" : "? (legacy build)\r\n"); }
    { char cw[480]; cw[0] = 0; if (P.cpu_why) P.cpu_why(cw, sizeof cw);   /* 8.0.4 */
      str_append(hb, sizeof hb, "cpu why "); str_append(hb, sizeof hb, cw[0] ? cw : "-"); str_append(hb, sizeof hb, "\r\n"); }
    str_append(hb, sizeof hb, "acpi mode "); str_append(hb, sizeof hb, acpi_mode ? (acpi_line[0] ? acpi_line : "requested") : "off (ini AcpiMode=1)"); str_append(hb, sizeof hb, "\r\n");   /* 5.2.162 */
    str_append(hb, sizeof hb, "video "); str_append(hb, sizeof hb, wc_status);
    str_append(hb, sizeof hb, "\r\nvsync "); str_append(hb, sizeof hb, vs_status[0] ? vs_status : "-");
    str_append(hb, sizeof hb, "\r\n== EMU ==\r\nmachine "); str_append(hb, sizeof hb, machine_name);
    str_append(hb, sizeof hb, "\r\nsound "); str_append(hb, sizeof hb, snd_status);
    str_append(hb, sizeof hb, "  volume "); num_append(hb, sizeof hb, (uint32_t)volume, 0, ' '); str_append(hb, sizeof hb, muted ? " % mute" : " %");
    str_append(hb, sizeof hb, "\r\nROM DB msxromdb "); str_append(hb, sizeof hb, romdb_len ? "OK" : "-");
    str_append(hb, sizeof hb, " msxsysromdb "); str_append(hb, sizeof hb, sysdb_len ? "OK" : "-");
    str_append(hb, sizeof hb, "\r\ndiag draw ms: info "); num_append(hb, sizeof hb, dg_t_info, 0, ' ');
    str_append(hb, sizeof hb, "  sound_diag "); num_append(hb, sizeof hb, dg_t_snd, 0, ' ');
    str_append(hb, sizeof hb, "\r\n== SND codec ==\r\n");
    if (P.sound_diag) {
        static char cl[40][64];
        uint32_t t0 = P.ms();
        int n = P.sound_diag(cl, 40);
        uint32_t dt = P.ms() - t0;
        for (int i = 0; i < n; i++) { str_append(hb, sizeof hb, cl[i]); str_append(hb, sizeof hb, "\r\n"); }
        str_append(hb, sizeof hb, "(sound_diag "); num_append(hb, sizeof hb, dt, 0, ' '); str_append(hb, sizeof hb, " ms)\r\n");
    }
    { static char tb[4096]; fe_hang_dump(tb, sizeof tb); str_append(hb, sizeof hb, "== TRACE ==\r\n"); str_append(hb, sizeof hb, tb); }
    P.diag_save(hb);
}

static void open_diag(int tab)
{
    fe_trace("open diag");
    diag_save_now();
    diag_tab = tab;
    dg_key = -1; dg_key_n = 0;
    dg_mx = dg_my = dg_wheel = 0; dg_events = 0;
    dg_jpad = 0; dg_jbtn_seen = 0; dg_pv_ok_prev = -2;
    open_dialog(2);
}

/* 마우스 탭에서 받은 움직임 (mouse_frame이 부름) */
static void dg_feed(int dx, int dy, int dz, int b)
{
    if (dx || dy || dz || b != dg_btn) { dg_events++; dg_last_ev = P.ms(); menu_dirty = 1; }
    dg_mx += dx; dg_my += dy; dg_wheel += dz; dg_btn = b;
}
static int dg_mouse_test(void) { return dialog == 2 && diag_tab == 3; }
static int diag_kbd_tab(void) { return diag_tab == 1; }
static int dg_joy_tab(void) { return dialog == 2 && diag_tab == 4; }
/* 조이스틱 탭: 한 프레임마다 보고를 읽어 바뀌면 다시 그림 (3.8.1) */
static void dg_joy_poll(void)
{
    if (!dg_joy_tab() || !P.pad_view) return;
    char nm[32];
    dg_pv_ok = P.pad_view(dg_jpad, &dg_pv, nm, sizeof nm);
    if (dg_pv_ok < 0 && dg_jpad) { dg_jpad = 0; dg_pv_ok = P.pad_view(0, &dg_pv, nm, sizeof nm); }
    if (dg_pv_ok != dg_pv_ok_prev || memcmp(&dg_pv, &dg_pv_prev, sizeof dg_pv)) { menu_dirty = 1; dg_pv_ok_prev = dg_pv_ok; dg_pv_prev = dg_pv; }
    if (dg_pv_ok > 0) dg_jbtn_seen |= dg_pv.btn;
}

/* 3.8.8 터치패드 탭: 입력 보고가 계속 바뀌므로 0.15초마다 다시 그림 */
static uint32_t dg_tp_t;
static void dg_tp_poll(void)
{
    if (dialog != 2 || diag_tab != 5 || !P.ms) return;
    uint32_t t = P.ms();
    if (t - dg_tp_t >= 150) { dg_tp_t = t; menu_dirty = 1; }
}

static int dg_has(const char *s, const char *w)
{
    for (; *s; s++) {
        int i = 0;
        while (w[i] && lower_c(s[i]) == w[i]) i++;
        if (!w[i]) return 1;
    }
    return 0;
}
/* 플랫폼 정보 줄을 탭으로 나눔 */
static int dg_col_page;                           /* Color 탭 0 = 개요, 1~ = 화면 가득 한 색 */
static int dg_vs_off;                             /* 4.0.34: Video 탭 줄 스크롤 */
static int dg_kind(const char *l)                       /* 분류는 kernel/diag.c 한 곳 (옛 탭 번호로 바꿈: 0 USB 1 키보드 2 소리 3 마우스 4 화면·시스템 5 터치패드 7 무선) */
{
    static const int8_t map[DA_N] = { 4, 4, 2, 1, 3, 5, 1, 0, 7, 4 };   /* SYS VID SND KBD MOUSE TP PAD USB NET PERF */
    return map[diag_area_of(l)];
}
/* 3.4.4: 진단 마우스 탭의 플랫폼 줄(영문)을 한국어·일본어로. 분류(dg_kind)는 영문으로 한 뒤 그릴 때만 바꾼다. 긴 말부터 */
static const char *const dg_tr_tab[][3] = {
    { "PS/2 mouse: on (wheel)", "PS/2 마우스: 켜짐 (휠)", "PS/2マウス: オン (ホイール)" },
    { "PS/2 mouse: none", "PS/2 마우스: 없음", "PS/2マウス: なし" },
    { "PS/2 mouse: on", "PS/2 마우스: 켜짐", "PS/2マウス: オン" },
    { "PS/2 mouse: ", "PS/2 마우스: ", "PS/2マウス: " },
    { "USB mouse via firmware pointer", "USB 마우스 (펌웨어 포인터)", "USBマウス (ファームウェアポインタ)" },
    { "USB mouse (boot) if", "USB 마우스 (부트) 인터페이스 ", "USBマウス (ブート) IF " },
    { "USB mouse if", "USB 마우스 인터페이스 ", "USBマウス IF " },
    { " via firmware driver", " (펌웨어 드라이버)", " (ファームウェアドライバ)" },
    { "USB mice: ", "USB 마우스 수: ", "USBマウス数: " },
    { "Logitech DJ if", "Logitech DJ 인터페이스 ", "Logitech DJ IF " },
    { " no mouse paired", " 짝지은 마우스 없음", " ペアのマウスなし" },
    { " -> mouse (firmware)", " → 마우스 (펌웨어)", " → マウス (ファームウェア)" },
    { " -> not a mouse", " → 마우스 아님", " → マウスではない" },
    { " -> keyboard", " → 키보드", " → キーボード" },
    { " -> mouse", " → 마우스", " → マウス" },
    { " -> ", " → ", " → " },
    { ", mouse reports ", ", 마우스 보고 ", ", マウス報告 " },
    { "  mouse last:", "  마지막 보고:", "  最後の報告:" },
    { "  packets ", "  받은 묶음 ", "  受信パケット " },
    { ", init step ", ", 초기화 단계 ", ", 初期化段階 " },
    { " MB, bytes ", " MB, 바이트 ", " MB, バイト " },
    { ", bytes ", ", 바이트 ", ", バイト " },
    { ", memory ", ", 메모리 ", ", メモリ " },
    { ", sync err ", ", 동기 오류 ", ", 同期エラー " },
    { ", error", ", 오류", ", エラー" },
    { " (boot ", " (부트 ", " (ブート " },
    { " paired ", " 짝 ", " ペア " },
    { " asking", " 묻는 중", " 問い合わせ中" },
    { " DJ on", " DJ 켜짐", " DJ オン" },
    { " iface ", " 인터페이스 ", " IF " },
    { " wheel", " 휠", " ホイール" },
    { " sync", " 동기", " 同期" },
    { " res ", " 해상도 ", " 解像度 " },
    { " mouse ", " 마우스 ", " マウス " },
};
static const char *dg_tr(const char *in, char *out, int max)
{
    int col = lang == 0 ? 1 : lang == 2 ? 2 : 0;
    if (!col) return in;
    int k = 0;
    while (*in && k < max - 1) {
        int hit = 0;
        for (unsigned i = 0; i < sizeof dg_tr_tab / sizeof dg_tr_tab[0] && !hit; i++) {
            const char *e = dg_tr_tab[i][0];
            int n = 0;
            while (e[n] && in[n] == e[n]) n++;
            if (e[n]) continue;
            for (const char *t = dg_tr_tab[i][col]; *t && k < max - 1; t++) out[k++] = *t;
            in += n; hit = 1;
        }
        if (!hit) out[k++] = *in++;
    }
    out[k] = 0;
    return out;
}
static int dg_small;                                    /* 1 = 장치 줄을 작은 글꼴로 (줄이 많은 탭) */
static uint32_t dg_lines(uint32_t x, uint32_t y, uint32_t w, uint32_t bottom, int kind, const char *title)
{
    static char lines[80][64];
    int n = P.info ? P.info(lines, 80) : 0, any = 0;
    for (int i = 0; i < n && y + LH < bottom; i++) {
        if (!lines[i][0] || dg_kind(lines[i]) != kind) continue;
        if (!any) { y += 6 * S + ui_section(x, y + 6 * S, w, title); any = 1; }
        char tr[192];
        const char *l = kind == 3 ? dg_tr(lines[i], tr, sizeof tr) : lines[i];   /* 3.4.4: 마우스 줄은 한국어·일본어로 */
        if (dg_small) { small_ell(x + 4 * S, y, l, C_TEXT, 0xFF, w - 8 * S); y += SLH + 6 * S; }
        else y += info_line(x, y, w, 0, l, C_TEXT);
    }
    return y;
}

/* 색 시험 탭. 컬러 바 시험 화면: ① MSX 팔레트 16색(0~15) 막대 ② 표준 컬러 바 7색 (75 %) + 역순 띠 ③ 회색 계단·어두운/밝은 끝 ④ RGB 그러데이션.
 * Enter·화면 누르기 = 화면 가득 한 색 (흰·검정·회색·빨강·초록·파랑·청록·자홍·노랑) */
#define DG_COL_PAGES 10
static void dg_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t g, uint32_t b);
static uint8_t dg_pal[224][3]; static int dg_pal_n, dg_pal_used;
static void dg_color_full(void)
{
    static const uint8_t fc[DG_COL_PAGES - 1][3] = { {255,255,255}, {0,0,0}, {128,128,128}, {255,0,0}, {0,255,0}, {0,0,255}, {0,255,255}, {255,0,255}, {255,255,0} };
    const uint8_t *c = fc[dg_col_page - 1];
    dg_pal_n = 0;
    dg_fill(0, 0, D.w, D.h, c[0], c[1], c[2]);
    if (D.bpp == 1 && P.dac_set) { P.dac_set(16, dg_pal_n, &dg_pal[0][0]); dg_pal_used = 1; }
    hit_add(hc_key, 0x1C, 0, 0, 0x1C, 0, 0, D.w, D.h);          /* 화면을 누르면 다음 색 (Enter) */
}
/* 8비트(팔레트) 화면: 시험 화면에 쓰는 색을 DAC 16~239 에 직접 올려서 근사색(6x7x5) 없이 정확히 보이게 한다 (5.0.85) */
static void dg_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t g, uint32_t b)
{
    if (D.bpp != 1 || !P.dac_set) { fe_fill_rgb(x, y, w, h, r, g, b); return; }
    int k = -1;
    for (int i = 0; i < dg_pal_n; i++) if (dg_pal[i][0] == r && dg_pal[i][1] == g && dg_pal[i][2] == b) { k = i; break; }
    if (k < 0 && dg_pal_n < 224) { k = dg_pal_n++; dg_pal[k][0] = (uint8_t)r; dg_pal[k][1] = (uint8_t)g; dg_pal[k][2] = (uint8_t)b; }
    if (k < 0) k = 223;
    fill_any(x, y, w, h, (uint8_t)(16 + k), 0);
}
static void dg_color_restore(void)
{
    if (dg_pal_used && P.dac_set) { dg_pal_used = 0; P.dac_set(-1, 0, 0); }
}
static void dg_color_overview(uint32_t x, uint32_t y, uint32_t w, uint32_t bottom)
{
    static const uint8_t bar[7][3] = { {191,191,191}, {191,191,0}, {0,191,191}, {0,191,0}, {191,0,191}, {191,0,0}, {0,0,191} };      /* 표준 75 % 컬러 바: 회·노·청록·초록·자홍·빨강·파랑 */
    static const uint8_t rev[7][3] = { {0,0,191}, {19,19,19}, {191,0,191}, {19,19,19}, {0,191,191}, {19,19,19}, {191,191,191} };    /* 역순 띠 */
    if (D.bpp != 1) dg_pal_n = 0;
    text_clip(x, y, "Color bars: MSX 16 colors (0-15) / standard / gray.  Enter or tap: full-screen colors   Tab: next tab   Esc: close", D.bpp == 1 ? 15 : C_MUTED, 0xFF, w);
    y += LH + 6 * S;
    if (bottom <= y + 40 * S) return;
    uint32_t H = bottom - y, h1 = H * 40 / 100, h2 = H * 8 / 100, h3 = H * 12 / 100, h4 = H * 14 / 100, h5 = H * 12 / 100, gap = 3 * S;
    hit_add(hc_key, 0x1C, 0, 0, 0x1C, x, y, w, H);               /* 누르면 전체 화면 색 */
    for (int i = 0; i < 16; i++) {                              /* ① MSX 팔레트 0~15: 8비트 화면은 DAC 0~15 그대로(MSX 화면과 같은 색) */
        uint32_t x0 = x + (uint32_t)i * w / 16, x1 = x + (uint32_t)(i + 1) * w / 16;
        if (D.bpp == 1) fill_msx(x0, y, x1 - x0, h1, (uint8_t)i); else fe_fill_rgb(x0, y, x1 - x0, h1, mon_rgb[i][0], mon_rgb[i][1], mon_rgb[i][2]);
        char t[8] = ""; num_append(t, sizeof t, (uint32_t)i, 0, ' ');
        uint32_t lum = ((uint32_t)mon_rgb[i][0] * 30 + (uint32_t)mon_rgb[i][1] * 59 + (uint32_t)mon_rgb[i][2] * 11) / 100;
        small_ell(x0 + 3 * S, y + h1 - SLH - 3 * S, t, lum > 110 ? 1 : 15, 0xFF, x1 - x0 - 4 * S);
    }
    y += h1 + gap;
    for (int i = 0; i < 7; i++) {                               /* ② 표준 컬러 바 (위 MSX 막대와 밝기·색감 비교) + 역순 띠 */
        uint32_t x0 = x + (uint32_t)i * w / 7, x1 = x + (uint32_t)(i + 1) * w / 7;
        dg_fill(x0, y, x1 - x0, h2 + h3, bar[i][0], bar[i][1], bar[i][2]);
        dg_fill(x0, y + h2 + h3 - h3 / 2, x1 - x0, h3 / 2, rev[i][0], rev[i][1], rev[i][2]);
    }
    y += h2 + h3 + gap;
    for (int i = 0; i < 16; i++) {                              /* ③ 위: 회색 0~255 (17씩), 아래 왼쪽: 어두운 끝 0~15, 아래 오른쪽: 밝은 끝 240~255 (뭉개지면 출력 범위·감마 문제) */
        uint32_t x0 = x + (uint32_t)i * w / 16, x1 = x + (uint32_t)(i + 1) * w / 16, v = (uint32_t)i * 17;
        uint32_t lx0 = x + (uint32_t)i * (w / 2) / 16, lx1 = x + (uint32_t)(i + 1) * (w / 2) / 16, hx0 = x + w / 2 + (uint32_t)i * (w / 2) / 16, hx1 = x + w / 2 + (uint32_t)(i + 1) * (w / 2) / 16;
        uint32_t lo = (uint32_t)i, hi = 240 + (uint32_t)i;
        dg_fill(x0, y, x1 - x0, h4 / 2, v, v, v);
        dg_fill(lx0, y + h4 / 2, lx1 - lx0, h4 - h4 / 2, lo, lo, lo);
        dg_fill(hx0, y + h4 / 2, hx1 - hx0, h4 - h4 / 2, hi, hi, hi);
    }
    y += h4 + gap;
    {                                                           /* ④ RGB 그러데이션 (줄무늬가 보이면 색 깊이가 낮음). 8비트 화면은 48단계 */
        uint32_t bh = h5 / 3, n = D.bpp == 1 ? 48 : w / (4 * S);
        if (bh && n > 1) for (uint32_t i = 0; i < n; i++) {
            uint32_t v = i * 255 / (n - 1), x0 = x + i * w / n, x1 = x + (i + 1) * w / n;
            dg_fill(x0, y, x1 - x0, bh, v, 0, 0);
            dg_fill(x0, y + bh, x1 - x0, bh, 0, v, 0);
            dg_fill(x0, y + 2 * bh, x1 - x0, bh, 0, 0, v);
        }
    }
    if (D.bpp == 1 && P.dac_set && dg_pal_n) {                  /* 모은 색을 DAC 에 올림 */
        P.dac_set(16, dg_pal_n, &dg_pal[0][0]); dg_pal_used = 1;
    }
}

static uint32_t dg_pc_dd_x, dg_pc_dd_y, dg_pc_dd_w;
static void dg_pc_dd_open(void)                          /* 시험 PC 풀다운 — 자동 · 표의 PC들 · 목록에 없음 */
{
    int n = 0;
    copy_str(dd_txt[n++], sizeof dd_txt[0], lang == 1 ? "Auto (detect from SMBIOS / GPU)" : lang == 2 ? "自動 (SMBIOS・GPU で判別)" : "자동 (SMBIOS·영상칩으로 찾기)");
    dg_upc_load();
    int cnt = diag_pc_count();
    for (int i = 0; i < cnt && n < DD_MAX - 2; i++) {
        const DiagPc *pc = diag_pc(i); char v[64] = "";
        { str_append(v, sizeof v, "#"); num_append(v, sizeof v, (uint32_t)pc->pc, 0, ' '); str_append(v, sizeof v, " "); }
        str_append(v, sizeof v, pc->name);
        if (diag_pc_is_user(i)) str_append(v, sizeof v, lang == 1 ? "  (added)" : lang == 2 ? "  (追加)" : "  (추가함)");
        copy_str(dd_txt[n++], sizeof dd_txt[0], v);
    }
    copy_str(dd_txt[n++], sizeof dd_txt[0], lang == 1 ? "Not in list" : lang == 2 ? "リストにない" : "목록에 없음");
    copy_str(dd_txt[n++], sizeof dd_txt[0], lang == 1 ? "+ Add this PC as a test PC (TESTPC.TXT)" : lang == 2 ? "+ このPCをテストPCに追加 (TESTPC.TXT)" : "+ 이 PC를 시험 PC로 추가 (TESTPC.TXT)");
    for (int i = 0; i < n; i++) dd_hd[i] = 0;
    dd_n = n; dd_sel = diag_pc_sel + 1; if (dd_sel < 0 || dd_sel >= n) dd_sel = 0;
    dd_top = dd_sel >= 10 ? dd_sel - 9 : 0; dd_cur = dd_sel; dd_id = 4000; dd_on = 1; menu_dirty = 1;
    fe_trace("pc dd open");
}
static void dg_pc_apply(int i)
{
    int cnt = diag_pc_count();
    if (i == cnt + 2) {                                   /* + 이 PC 추가: 지금 판정을 기준으로 TESTPC.TXT 에 씀 */
        dg_refresh(1);
        DiagSum raw[DA_N]; DiagCtx c = { snd_status, wc_status, vs_status, usb_status, machine_name, measured_speed, measured_busy, (int)gap_show_pub() };
        diag_judge(&c, dg_sl, dg_sn, raw);                /* 예상을 반영하기 전 판정 */
        int k = diag_pc_user_add(dg_sl, dg_sn, raw, MSXEMU_VERSION);
        static char tb[8192]; int tl = k >= 0 ? diag_pc_user_save(tb, sizeof tb) : 0;
        int bad = k < 0 || !P.write_file;
        if (!bad) { if (P.create) P.create(DIAG_TESTPC_PATH, 0); bad = P.write_file(DIAG_TESTPC_PATH, (const uint8_t *)tb, (uint32_t)tl) != 0; }
        if (k >= 0) diag_pc_sel = k;
        toast(bad ? (lang == 1 ? "Could not add this PC (no SMBIOS / GPU id, or USB write failed)" : lang == 2 ? "このPCを追加できません" : "이 PC를 추가하지 못했습니다 (SMBIOS·영상칩 없음 또는 USB 쓰기 실패)")
                  : (lang == 1 ? "Added to USB TESTPC.TXT" : lang == 2 ? "USB TESTPC.TXT に追加しました" : "USB TESTPC.TXT 에 추가했습니다"), 0);
        dg_refresh(1); menu_dirty = 1; settings_changed(); dg_save_due = P.ms() + 500; fe_trace("pc add");
        return;
    }
    diag_pc_sel = i - 1;                                  /* 0 자동, 1.. 표, cnt+1 = 목록에 없음 */
    dg_refresh(1); menu_dirty = 1; settings_changed(); dg_save_due = P.ms() + 1500; fe_trace("pc select");
}
static void hc_dgpc(const UiHit *h, int ev) { (void)h; if (ev == 1) dg_pc_dd_open(); }

/* 8.0.30: 스위치 탭 — 진단용으로 USB 에 쓰는 파일마다 켜기/끄기. 새 진단 저장을 만들면 이 표에 한 줄 더하고 fe.c 의 log_* 와 ini(Log*) 도 같이 */
typedef struct { int *flag; const char *file; const char *ko, *en; } DgSw;
static const DgSw dg_sw[] = {
    { &log_diag,      "LOG\\DIAG.TXT",      "진단 요약 (시작 10초 뒤, 설정 바꾼 뒤, 1분마다 · 연주 중엔 미룸)", "Diagnostic summary (10 s after boot, on change, every minute; deferred while playing)" },
    { &log_stall,     "LOG\\STALL.TXT",     "프레임이 1초 넘게 멈췄을 때 그 순간의 기록", "Trace when frames stop for over 1 s" },
    { &log_sndlog,    "LOG\\SNDLOG.TXT",    "소리 이상(잡음·클리핑·무음) 감지 때 기록 (20초에 한 번)", "Sound anomaly log (noise, clipping, silence; at most every 20 s)" },
    { &log_key,       "LOG\\KEY_X<ver>.TXT", "키를 누를 때 프레임 간격·처리 시간 (메뉴 열었을 때 씀)", "Key latency log (written while a menu is open)" },
    { &log_sndglitch, "LOG\\SNDGLITCH.TXT", "소리 끊김 순간의 프레임 간격·구간별 시간 (메뉴 열었을 때 씀)", "Sound glitch log (written while a menu is open)" },
};
#define DG_SWN ((int)(sizeof dg_sw / sizeof dg_sw[0]))
static int dg_sw_sel;
static void dg_sw_toggle(int i) { if (i < 0 || i >= DG_SWN) return; *dg_sw[i].flag = !*dg_sw[i].flag; dg_sw_sel = i; settings_changed(); full_redraw(); }
static void hc_dgsw(const UiHit *h, int ev) { if (ev == 1) dg_sw_toggle((int)h->a); }

static void draw_diag(void)
{
    fe_trace("draw begin");
    if (diag_tab == 8 && dg_col_page > 0) { dg_color_full(); return; }
    if (diag_tab == 8 && D.bpp == 1 && P.dac_set) {      /* 8비트 화면: 장식 색과 시험 색이 DAC 를 나눠 쓸 수 없어 화면 전체를 시험 화면으로 (Tab 다음 탭, Esc 닫기) */
        dg_pal_n = 0;
        dg_fill(0, 0, D.w, D.h, 0, 0, 0);
        dg_color_overview(D.w / 40, 10 * S, D.w - D.w / 20, D.h - 10 * S);
        hit_add(hc_key, 0x1C, 0, 0, 0x1C, 0, 0, D.w, D.h);
        return;
    }
    if (diag_tab != 8) dg_color_restore();
    UiRect o;
    UiRect b = ui_full_card(T(S_DIAG_TITLE), "X86MSX " MSXEMU_VERSION_FULL " (" MSXEMU_BUILD ")", machine_name, &o);
    const char *tn[DG_TABS] = { T(S_DG_TAB_USB), T(S_DG_TAB_KBD), T(S_DG_TAB_SND), T(S_DG_TAB_MOUSE), T(S_DG_TAB_JOY), T(S_DG_TAB_TP), "Video", "WiFi", "Color", lang == 1 ? "Summary" : lang == 2 ? "要約" : "요약", lang == 1 ? "Switch" : lang == 2 ? "スイッチ" : "스위치" };
    const char *tl[DG_TABS]; for (int i = 0; i < DG_TABS; i++) tl[i] = tn[dg_order[i]];
    ui_tabs(b.x, b.y + 4 * S, b.w, tl, DG_TABS, dg_pos(diag_tab), 1);
    uint32_t pad = 24 * S, x = b.x + pad, w = b.w - 2 * pad, y = b.y + 4 * S + ui_tabs_h() + 14 * S, bottom = dlg_cmd_y() - 8 * S;
    dg_small = diag_tab == 0 || diag_tab == 3 || diag_tab == 5;
    {   /* 탭의 영역 판정 한 줄 (요약 탭과 같은 판정) */
        static const int8_t tab_area[DG_TABS] = { DA_USB, DA_KBD, DA_SND, DA_MOUSE, DA_PAD, DA_TP, DA_VID, DA_NET, -1, -1, -1 };
        if (tab_area[diag_tab] >= 0) { dg_refresh(0); y += dg_verdict_row(x, y, w, tab_area[diag_tab]) + 4 * S; }
    }
    switch (diag_tab) {
    case 0: {                                           /* USB: 붙은 장치 모두, 그다음 시스템 */
        ui_rrect(x, y, w, LH + 14 * S, 6 * S, C_HEAD);
        text_clip(x + 12 * S, ui_ty(y, LH + 14 * S), T(S_DIAG_HINT), C_WARN, 0xFF, w - 24 * S);
        y += LH + 20 * S;
        uint32_t sys_h = SLH + 12 * S + 8 * (LH + 6 * S) + 12 * S;
        y = dg_lines(x, y, w, bottom > sys_h ? bottom - sys_h : bottom, 0, T(S_DG_USBDEV));
        y += 10 * S;
        y += ui_section(x, y, w, T(S_DG_SYSTEM));
        char t[64] = "";
        num_append(t, sizeof t, (uint32_t)measured_speed, 0, ' '); str_append(t, sizeof t, " %   busy ");
        num_append(t, sizeof t, (uint32_t)measured_busy, 0, ' '); str_append(t, sizeof t, " %");
        y += info_line(x, y, w, "speed", t, C_TEXT);
        char t3[32] = ""; num_append(t3, sizeof t3, gap_show, 0, ' '); str_append(t3, sizeof t3, " ms");
        y += info_line(x, y, w, "frame gap max", t3, gap_show > 40 ? C_WARN : C_TEXT);
        { char pt[160]; pf_text(pt, sizeof pt, 0); y += info_line(x, y, w, "time share", pt, C_TEXT); pf_text(pt, sizeof pt, 1); y += info_line(x, y, w, "worst/s", pt, C_TEXT); }
        y += info_line(x, y, w, "video", wc_status, C_TEXT);
        if (usb_status[0]) y += info_line(x, y, w, "usb", usb_status, C_TEXT);
        char t2[96] = "msxromdb.xml ";
        str_append(t2, sizeof t2, romdb_len ? "OK" : "-"); str_append(t2, sizeof t2, "   msxsysromdb.xml "); str_append(t2, sizeof t2, sysdb_len ? "OK" : "-");
        y += info_line(x, y, w, "ROM DB", t2, romdb_len && sysdb_len ? C_OK : C_WARN);
        {   /* 플랫폼의 시스템 줄 (부팅 방식·화면·드라이브) */
            static char sl[80][64];
            int sn = P.info ? P.info(sl, 80) : 0;
            for (int i = 0; i < sn && y + SLH < bottom; i++) if (sl[i][0] && dg_kind(sl[i]) == 4) { small_ell(x + 4 * S, y, sl[i], C_TEXT, 0xFF, w - 8 * S); y += SLH + 6 * S; }   /* 4.0.34: 줄이 많아 작은 글꼴 */
        }
        break;
    }
    case 1: {                                           /* 키보드: 누른 키 보기 */
        y += ui_section(x, y, w, T(S_DG_K_LAST));
        uint32_t ch = LH * 2;
        if (dg_key >= 0) {
            char nm[48] = "";
            int vk = key_vk(dg_key);
            if (vk) combo_name(vk, dg_key_mods, nm, sizeof nm); else copy_str(nm, sizeof nm, "?");
            uint32_t cw = text_f(F, S * 2, 0, 0, nm, 0, 0, 0, 0) + 40 * S;
            ui_rrect(x, y, cw, ch, 8 * S, C_ACC2);
            text_f(F, S * 2, x + 20 * S, y + (ch - (uint32_t)F->height * S * 2) / 2, nm, 15, 0xFF, 0, 1);
            char sc[48] = "scan ";
            static const char hx[] = "0123456789ABCDEF";
            if (dg_key >= 0x80) str_append(sc, sizeof sc, "E0 ");
            char h2[3] = { hx[(dg_key & 0x7F) >> 4], hx[dg_key & 15], 0 };
            str_append(sc, sizeof sc, h2);
            str_append(sc, sizeof sc, "   #"); num_append(sc, sizeof sc, dg_key_n, 0, ' ');
            text_clip(x + cw + 20 * S, y + (ch - LH) / 2, sc, C_MUTED, 0xFF, w - cw - 20 * S);
        } else {
            ui_rrect(x, y, w, ch, 8 * S, C_HEAD);
            text_clip(x + 20 * S, y + (ch - LH) / 2, T(S_DG_K_NOTE), C_WARN, 0xFF, w - 40 * S);
        }
        y += ch + 12 * S;
        if (dg_key >= 0) { small_ell(x, y, T(S_DG_K_NOTE), C_MUTED, 0xFF, w); y += SLH + 8 * S; }
        dg_lines(x, y, w, bottom, 1, T(S_DG_DEVICES));
        break;
    }
    case 2: {                                           /* 소리 (2.8.1 소리 진단) */
        int tone = (int32_t)(tone_until - P.ms()) > 0;
        char t[64] = "";
        num_append(t, sizeof t, (uint32_t)volume, 0, ' ');
        str_append(t, sizeof t, muted ? " % (mute)" : " %");
        if (tone) str_append(t, sizeof t, "   TEST TONE");
        y += info_line(x, y, w, "volume", t, muted ? C_WARN : tone ? C_OK : C_TEXT);
        uint32_t bw = w / 2, k = bw * (uint32_t)volume / 100;
        ui_rrect(x + w / 4, y, bw, 4 * S, 2 * S, C_LINE);
        if (k) ui_rrect(x + w / 4, y, k, 4 * S, 2 * S, tone ? C_OK : C_ACC);
        y += 16 * S;
        y += info_line(x, y, w, "sound", snd_status, C_TEXT);
        {   /* 3.0.4: 누를 수 있는 단추 두 개 — 시험음, 소리 장치 다시 찾기 */
            uint32_t bh = LH + 22 * S, bx = x, gap = 16 * S;
            const char *lab[2] = { tone ? T(S_DG_S_PLAYING) : T(S_DG_S_TONE), T(S_DG_S_RESCAN) };
            const char *key[2] = { "T", "R" };
            static const int sc[2] = { 0x14, 0x13 };
            y += 6 * S;
            for (int i = 0; i < 2; i++) {
                uint32_t kw = text_w(key[i]) + 16 * S, lw = text_w(lab[i]);
                if (i == 0) { uint32_t a = text_w(T(S_DG_S_TONE)), b2 = text_w(T(S_DG_S_PLAYING)); lw = a > b2 ? a : b2; }
                uint32_t bw = kw + lw + 50 * S;
                int hot = i == 0 && tone;
                ui_rrect(bx, y, bw, bh, 8 * S, hot ? C_OK : C_ACC2);
                ui_rrect(bx + 12 * S, y + (bh - LH - 4 * S) / 2, kw, LH + 4 * S, 4 * S, hot ? 15 : C_CHIP);
                text_clip(bx + 20 * S, ui_ty(y, bh), key[i], hot ? C_OK : C_TEXT, 0xFF, 0);
                text_clip(bx + kw + 26 * S, ui_ty(y, bh), lab[i], 15, 0xFF, 0);
                hit_add(hc_key, sc[i], 0, 0, sc[i], bx, y, bw, bh);
                bx += bw + gap;
            }
            y += bh + 14 * S;
        }
        uint32_t sb = bottom - SLH - 12 * S;
        uint32_t t0 = P.ms();
        fe_trace("snd info begin");
        y = dg_lines(x, y, w, sb, 2, "HD Audio");
        uint32_t t1 = P.ms();
        fe_trace("snd sound_diag begin");
        static char lines[40][64];
        static int snd_n; static uint32_t snd_at;          /* 코덱 진단은 0.5초마다만 (매 프레임 코덱 명령 수십 개를 보내지 않게) */
        if (!snd_at || P.ms() - snd_at >= 500) { snd_n = P.sound_diag ? P.sound_diag(lines, 40) : 0; snd_at = P.ms() | 1; }
        int n = snd_n;
        uint32_t t2 = P.ms();
        fe_trace("snd sound_diag end");
        if (t1 - t0 > dg_t_info) dg_t_info = t1 - t0;
        if (t2 - t1 > dg_t_snd) dg_t_snd = t2 - t1;
        if (n) y += 6 * S + ui_section(x, y + 6 * S, w, "Codec");
        for (int i = 0; i < n && y + LH < sb; i++) y += info_line(x, y, w, 0, lines[i], C_TEXT);
        small_ell(x, bottom - SLH - 4 * S, T(S_SOUND_DIAG_NOTE), C_WARN, 0xFF, w);
        fe_trace("snd draw end");
        break;
    }
    case 3: {                                           /* 마우스: 연결·신호·시험 칸 */
        int conn = mouse_count > 0;
        uint32_t rh = LH + 16 * S;
        ui_rrect(x, y, w, rh, 6 * S, C_HEAD);
        ui_rrect(x + 12 * S, y + (rh - LH / 2) / 2, LH / 2, LH / 2, LH / 4, conn ? C_OK : C_BAD);
        char st[96] = "";
        str_append(st, sizeof st, T(S_DG_STATE)); str_append(st, sizeof st, ": ");
        str_append(st, sizeof st, T(conn ? S_DG_M_CONN : S_DG_M_NONE));
        if (conn && mouse_name[0]) { str_append(st, sizeof st, "  ·  "); str_append(st, sizeof st, mouse_name); }
        text_clip(x + 24 * S + LH / 2, ui_ty(y, rh), st, C_TEXT, 0xFF, w - LH - 40 * S);
        y += rh + 8 * S;
        int live = dg_events && P.ms() - dg_last_ev < 1500;
        ui_rrect(x, y, w, rh, 6 * S, C_HEAD);
        ui_rrect(x + 12 * S, y + (rh - LH / 2) / 2, LH / 2, LH / 2, LH / 4, live ? C_OK : dg_events ? C_ACC : C_FAINT);
        char sg[96] = "";
        str_append(sg, sizeof sg, T(S_DG_M_SIG)); str_append(sg, sizeof sg, ": ");
        str_append(sg, sizeof sg, T(dg_events ? S_DG_M_GOT : S_DG_M_WAIT));
        if (dg_events) { str_append(sg, sizeof sg, "  ("); num_append(sg, sizeof sg, dg_events, 0, ' '); str_append(sg, sizeof sg, ")"); }
        text_clip(x + 24 * S + LH / 2, ui_ty(y, rh), sg, C_TEXT, 0xFF, w - LH - 40 * S);
        y += rh + 8 * S;
        if (P.mouse_driver) {                           /* 3.0.4: USB 마우스를 읽는 길 */
            char dv[160] = "";
            str_append(dv, sizeof dv, T(S_DG_M_DRV)); str_append(dv, sizeof dv, ": ");
            str_append(dv, sizeof dv, T(mouse_own == 2 ? S_DG_M_DRV_SYNC : mouse_own ? S_DG_M_DRV_OWN : S_DG_M_DRV_FW));
            small_ell(x + 12 * S, y, dv, C_MUTED, 0xFF, w - 24 * S);
            y += SLH + 10 * S;
        }
        /* 시험 칸: 점이 마우스를 따라감, 버튼 L M R, 휠 */
        uint32_t bh = (bottom - y) / 3, bw = w / 2;
        if (bh > 160 * S) bh = 160 * S;
        if (bh < LH * 3) bh = LH * 3;
        y += ui_section(x, y, w, T(S_DG_M_TEST));
        ui_rframe(x, y, bw, bh, 8 * S, C_LINE, C_FIELD);
        int hw = (int)bw / 2 - (int)(8 * S), hh = (int)bh / 2 - (int)(8 * S);
        if (dg_mx > hw) dg_mx = hw;
        if (dg_mx < -hw) dg_mx = -hw;
        if (dg_my > hh) dg_my = hh;
        if (dg_my < -hh) dg_my = -hh;
        fe_fill(x + bw / 2, y + 6 * S, S, bh - 12 * S, C_LINE);
        fe_fill(x + 6 * S, y + bh / 2, bw - 12 * S, S, C_LINE);
        uint32_t dd = 10 * S, cx = (uint32_t)((int)(x + bw / 2) + dg_mx), cy = (uint32_t)((int)(y + bh / 2) + dg_my);
        ui_rrect(cx - dd / 2, cy - dd / 2, dd, dd, dd / 2, live ? C_OK : C_WARN);
        uint32_t px = x + bw + 24 * S, py = y + 4 * S;
        text_f(SF, 1, px, py, T(S_DG_M_BTN), C_MUTED, 0xFF, 0, 1);
        py += SLH + 8 * S;
        static const char *const bn[3] = { "L", "M", "R" };
        static const int bb[3] = { 1, 4, 2 };
        for (int i = 0; i < 3; i++) {
            uint32_t kw = LH * 2;
            ui_rrect(px + (uint32_t)i * (kw + 10 * S), py, kw, kw, 8 * S, (dg_btn & bb[i]) ? C_OK : C_CHIP);
            text_clip(px + (uint32_t)i * (kw + 10 * S) + (kw - text_w(bn[i])) / 2, py + (kw - LH) / 2, bn[i], (dg_btn & bb[i]) ? 15 : C_TEXT, 0xFF, 0);
        }
        py += LH * 2 + 16 * S;
        text_f(SF, 1, px, py, T(S_DG_M_WHEEL), C_MUTED, 0xFF, 0, 1);
        py += SLH + 8 * S;
        char wv[24] = "";
        if (dg_wheel < 0) str_append(wv, sizeof wv, "-");
        num_append(wv, sizeof wv, (uint32_t)(dg_wheel < 0 ? -dg_wheel : dg_wheel), 0, ' ');
        text_clip(px, py, wv, C_TEXT, 0xFF, 0);
        y += bh + 8 * S;
        y = dg_lines(x, y, w, bottom - SLH * 2 - 16 * S, 3, T(S_DG_DEVICES));
        small_ell(x, bottom - SLH * 2 - 10 * S, T(S_DG_M_NOTE), C_WARN, 0xFF, w);
        {   /* 레거시 부팅 안내 */
            static char l1[2][64];
            if (P.info && P.info(l1, 1) > 0 && dg_has(l1[0], "legacy")) small_ell(x, bottom - SLH - 4 * S, T(S_DG_M_LEGACY), C_MUTED, 0xFF, w);
        }
        break;
    }
    case 4: {                                           /* 3.8.1 조이스틱: 축 · 방향 스위치 · 버튼 · MSX로 들어가는 값 */
        char nm[32] = "";
        int n_pad = P.pad_view ? P.pad_view(dg_jpad, &dg_pv, nm, sizeof nm) : -1;
        if (n_pad < 0) {
            ui_rrect(x, y, w, LH + 14 * S, 6 * S, C_HEAD);
            text_clip(x + 12 * S, ui_ty(y, LH + 14 * S), T(S_DG_J_NONE), C_WARN, 0xFF, w - 24 * S);
            break;
        }
        char ttl[64] = "";
        str_append(ttl, sizeof ttl, nm); str_append(ttl, sizeof ttl, " #"); num_append(ttl, sizeof ttl, (uint32_t)dg_jpad + 1, 0, ' ');
        y += ui_section(x, y, w, ttl);
        if (n_pad == 0) { small_ell(x + 4 * S, y, T(S_DG_J_WAIT), C_MUTED, 0xFF, w - 8 * S); y += SLH + 8 * S; }
        const PadView *v = &dg_pv;
        uint32_t sq = w / 3;
        if (sq > bottom - y - (SLH + 8 * S)) sq = bottom - y - (SLH + 8 * S);
        if (sq < LH * 4) sq = LH * 4;
        /* 왼쪽: 축 시험 칸 */
        ui_rframe(x, y, sq, sq, 8 * S, C_LINE, C_FIELD);
        fe_fill(x + sq / 2, y + 6 * S, S, sq - 12 * S, C_LINE);
        fe_fill(x + 6 * S, y + sq / 2, sq - 12 * S, S, C_LINE);
        if (n_pad > 0) {
            if (v->x < dg_jmin_x) dg_jmin_x = v->x;
            if (v->x > dg_jmax_x) dg_jmax_x = v->x;
            if (v->y < dg_jmin_y) dg_jmin_y = v->y;
            if (v->y > dg_jmax_y) dg_jmax_y = v->y;
        }
        int xr = v->xmax - v->xmin, yr = v->ymax - v->ymin;
        int nx = 0, ny = 0;                              /* -1000 ~ 1000 */
        if (xr > 0) nx = (int)(((int64_t)(v->x - v->xmin) * 2000) / xr) - 1000;
        if (yr > 0) ny = (int)(((int64_t)(v->y - v->ymin) * 2000) / yr) - 1000;
        uint32_t dd = 12 * S, half = sq / 2 - 10 * S;
        uint32_t cx = (uint32_t)((int)(x + sq / 2) + nx * (int)half / 1000), cy = (uint32_t)((int)(y + sq / 2) + ny * (int)half / 1000);
        ui_rrect(cx - dd / 2, cy - dd / 2, dd, dd, dd / 2, (nx || ny || v->hat >= 0) ? C_OK : C_ACC2);
        /* 오른쪽 */
        uint32_t px = x + sq + 24 * S, rw = w - sq - 24 * S, py = y;
        py += ui_section(px, py, rw, T(S_DG_J_AXIS));
        char t[64] = "X ";
        if (v->x < 0) str_append(t, sizeof t, "-");
        num_append(t, sizeof t, (uint32_t)(v->x < 0 ? -v->x : v->x), 0, ' ');
        str_append(t, sizeof t, "    Y ");
        if (v->y < 0) str_append(t, sizeof t, "-");
        num_append(t, sizeof t, (uint32_t)(v->y < 0 ? -v->y : v->y), 0, ' ');
        if (xr > 0 || yr > 0) { str_append(t, sizeof t, "   ("); num_append(t, sizeof t, (uint32_t)(xr > 0 ? xr : yr), 0, ' '); str_append(t, sizeof t, ")"); }
        text_clip(px + 4 * S, py, t, C_TEXT, 0xFF, rw - 8 * S);
        py += LH + 8 * S;
        py += ui_section(px, py, rw, T(S_DG_J_HAT));
        {   /* 방향 스위치 8방향을 십자 모양 칸 네 개 + 모서리로 */
            static const char *const hn[8] = { "↑", "↑→", "→", "↓→", "↓", "↓←", "←", "↑←" };
            uint32_t kw = LH * 2;
            for (int i = 0; i < 8; i++) {
                uint32_t bx = px + 4 * S + (uint32_t)i * (kw + 6 * S);
                if (bx + kw > px + rw) break;
                ui_rrect(bx, py, kw, kw, 8 * S, v->hat == i ? C_OK : C_CHIP);
                text_clip(bx + (kw - text_w(hn[i])) / 2, py + (kw - LH) / 2, hn[i], v->hat == i ? 15 : C_MUTED, 0xFF, 0);
            }
            py += kw + 10 * S;
        }
        py += ui_section(px, py, rw, T(S_DG_J_BTN));
        {
            int nb = v->nbtn > 16 ? 16 : v->nbtn;
            uint32_t gap = 6 * S, kw = (rw - 8 * S - 7 * gap) / 8;
            if (kw > LH * 2) kw = LH * 2;
            for (int i = 0; i < nb; i++) {
                uint32_t bx = px + 4 * S + (uint32_t)(i & 7) * (kw + gap), by = py + (uint32_t)(i >> 3) * (kw + gap);
                int on = (v->btn >> i) & 1, seen = (dg_jbtn_seen >> i) & 1;
                ui_rrect(bx, by, kw, kw, 8 * S, on ? C_OK : seen ? C_ACC2 : C_CHIP);
                char bn[4] = ""; num_append(bn, sizeof bn, (uint32_t)i + 1, 0, ' ');
                text_clip(bx + (kw - text_w(bn)) / 2, by + (kw - LH) / 2, bn, on ? 15 : C_TEXT, 0xFF, 0);
            }
            py += (uint32_t)((nb + 7) / 8) * (kw + gap) + 6 * S;
        }
        py += ui_section(px, py, rw, T(S_DG_J_MSX));
        {
            static const StrId nid[8] = { S_DG_J_UP, S_DG_J_DOWN, S_DG_J_LEFT, S_DG_J_RIGHT, S_DG_J_UP, S_DG_J_UP, S_DG_J_UP, S_DG_J_UP };
            static const char *const mn[8] = { 0, 0, 0, 0, "A", "B", "Start", "Select" };
            uint32_t gap = 6 * S, kw = (rw - 8 * S - 7 * gap) / 8;
            int d = n_pad > 0 && v->dec > 0 ? v->dec : 0;
            for (int i = 0; i < 8; i++) {
                uint32_t bx = px + 4 * S + (uint32_t)(i & 3) * (kw * 2 + gap), by = py + (uint32_t)(i >> 2) * (LH * 2 + gap);
                ui_rrect(bx, by, kw * 2, LH * 2, 8 * S, (d >> i) & 1 ? C_OK : C_CHIP);
                const char *lab = mn[i] ? mn[i] : T(nid[i]);
                text_clip(bx + (kw * 2 - text_w(lab)) / 2, by + (LH * 2 - LH) / 2, lab, (d >> i) & 1 ? 15 : C_TEXT, 0xFF, kw * 2);
            }
        }
        small_ell(x, bottom - SLH - 4 * S, T(S_DG_J_NOTE), C_MUTED, 0xFF, w);
        break;
    }
    case 5: {                                           /* 3.8.7 터치패드: 어떤 길로 붙어 있는지 */
        ui_rrect(x, y, w, LH + 14 * S, 6 * S, C_HEAD);
        text_clip(x + 12 * S, ui_ty(y, LH + 14 * S), T(S_DG_TP_HINT), C_WARN, 0xFF, w - 24 * S);
        y += LH + 20 * S;
        y = dg_lines(x, y, w, bottom, 5, T(S_DG_TP_INFO));
        y += 6 * S + ui_section(x, y + 6 * S, w, T(S_DG_TP_PTR));
        char dn[64]; int on = 0, nd = 0;
        for (int k = 0; k < 8 && P.mouse_dev && P.mouse_dev(k, dn, sizeof dn, &on) && y + LH < bottom; k++, nd++) {
            char ln[96] = "";
            str_append(ln, sizeof ln, dn); str_append(ln, sizeof ln, on ? "   ON" : "   OFF");
            small_ell(x + 4 * S, y, ln, C_TEXT, 0xFF, w - 8 * S); y += SLH + 6 * S;
        }
        if (!nd) { small_ell(x + 4 * S, y, "-", C_MUTED, 0xFF, w - 8 * S); y += SLH + 6 * S; }
        break;
    }
    case 6: {                                           /* 화면: 속도·화면 갱신·vsync·화면 크기 (4.0.29) */
        { static int gdump; if (!gdump && P.gpu_try) { gdump = 1; P.gpu_try(0); } }   /* 4.0.34: Video 탭을 처음 열면 GPU_DIAG.TXT 저장 */
        int sk = dg_vs_off;
        char t[64] = "";
        num_append(t, sizeof t, (uint32_t)measured_speed, 0, ' '); str_append(t, sizeof t, " %   busy ");
        num_append(t, sizeof t, (uint32_t)measured_busy, 0, ' '); str_append(t, sizeof t, " %");
        y += info_line(x, y, w, "speed", t, C_TEXT);
        char t3[32] = ""; num_append(t3, sizeof t3, gap_show, 0, ' '); str_append(t3, sizeof t3, " ms");
        y += info_line(x, y, w, "frame gap max", t3, gap_show > 40 ? C_WARN : C_TEXT);
        y += info_line(x, y, w, "video", wc_status, C_TEXT);
        { if (gpu_sel < 0) gpu_sel = (P.gpu_try && P.gpu_try(-1) == 1) ? 1 : 0;      /* 4.0.58: 처음엔 Intel 그래픽만 있으면 Intel */
          y += info_line(x, y, w, "HDMI target", gpu_sel ? "Intel   (V: AMD/Intel, Enter: on/off, A: sound diag, S: HDMI audio)" : "AMD   (V: AMD/Intel, Enter: run)", C_TEXT); }
        { char pt[160]; pf_text(pt, sizeof pt, 0); y += info_line(x, y, w, "time share", pt, C_TEXT); pf_text(pt, sizeof pt, 1); y += info_line(x, y, w, "worst/s", pt, C_TEXT); }
        {   /* 8.0.22: 화면 맞추기 시점(←/→ 로 바꿈)과 복사 때 빔 줄 위치를 탭 위쪽에 — 전엔 아래 목록에 섞여 화면에 안 나왔음 */
            static char sl2[80][64];
            int sn2 = P.info ? P.info(sl2, 80) : 0, shown = 0;
            for (int i = 0; i < sn2; i++) {
                const char *l = sl2[i];
                if (!strncmp(l, "screen sync lines ", 18)) { y += info_line(x, y, w, "sync lines  (<- ->)", l + 18, C_ACC); shown = 1; }
                else if (!strncmp(l, "copy ", 5)) y += info_line(x, y, w, "copy beam", l + 5, C_TEXT);
            }
            if (!shown) y += info_line(x, y, w, "sync lines  (<- ->)", "vsync off", C_MUTED);
        }
        if (P.video_diag) {                              /* 4.0.34: 화면 출력(모니터) 목록 */
            static char vl[40][64];
            int vn = P.video_diag(vl, 40);
            y += 6 * S + ui_section(x, y + 6 * S, w, "Video outputs");
            for (int i = 0; i < vn && y + SLH < bottom; i++) { if (sk > 0) { sk--; continue; } small_ell(x + 4 * S, y, vl[i], C_TEXT, 0xFF, w - 8 * S); y += SLH + 2 * S; }
        }
        {
            static char sl[80][64];
            int sn = P.info ? P.info(sl, 80) : 0;
            for (int i = 0; i < sn && y + LH < bottom; i++) if (sl[i][0] && dg_kind(sl[i]) == 4) { if (sk > 0) { sk--; continue; } small_ell(x + 4 * S, y, sl[i], C_TEXT, 0xFF, w - 8 * S); y += SLH + 2 * S; }   /* 4.0.34: 작은 글꼴로 amd hpd·BAR4/5 줄까지 */
        }
        break;
    }
    case 8: dg_color_overview(x, y, w, bottom); break;
    case DG_SW: {                                       /* 8.0.30 스위치: 진단용 USB 저장 켜기/끄기 (↑↓ 고르기, Enter·누르기 바꿈) */
        y += ui_section(x, y, w, lang == 1 ? "Diagnostic files written to USB" : lang == 2 ? "USB に書く診断ファイル" : "진단용으로 USB 에 쓰는 파일");
        uint32_t rh = LH * 2 + 22 * S;
        for (int i = 0; i < DG_SWN && y + rh < bottom; i++) {
            int on = *dg_sw[i].flag;
            ui_rrect(x, y, w, rh, 8 * S, i == dg_sw_sel ? C_ACC2 : C_HEAD);
            uint32_t cw = text_w("OFF") + 40 * S;
            ui_rrect(x + 14 * S, y + (rh - LH - 12 * S) / 2, cw, LH + 12 * S, 6 * S, on ? C_OK : C_LINE);
            text_clip(x + 14 * S + 20 * S, y + (rh - LH) / 2, on ? "ON" : "OFF", on ? 15 : C_MUTED, 0xFF, 0);
            uint32_t tx = x + cw + 34 * S;
            text_clip(tx, y + 8 * S, dg_sw[i].file, C_TEXT, 0xFF, x + w - tx - 8 * S);
            text_clip(tx, y + 8 * S + LH + 2 * S, lang == 1 || lang == 2 ? dg_sw[i].en : dg_sw[i].ko, i == dg_sw_sel ? C_TEXT : C_MUTED, 0xFF, x + w - tx - 8 * S);
            hit_add(hc_dgsw, i, 0, 0, 0, x, y, w, rh);
            y += rh + 8 * S;
        }
        break;
    }
    case DG_SUM: {                                      /* 5.2.26 요약: 시험 PC(풀다운으로 고르기) + 영역마다 정상·주의·실패·진단 안 됨 (LOG\\DIAG.TXT 의 SUM 줄과 같음) */
        dg_refresh(0);
        {   /* 시험 PC */
            char v[160] = "";
            if (dg_pc_i >= 0) { const DiagPc *pc = diag_pc(dg_pc_i); { str_append(v, sizeof v, "#"); num_append(v, sizeof v, (uint32_t)pc->pc, 0, ' '); str_append(v, sizeof v, " "); } str_append(v, sizeof v, pc->name); }
            else str_append(v, sizeof v, lang == 1 ? "not in list" : lang == 2 ? "リストにない" : "목록에 없음");
            str_append(v, sizeof v, dg_pc_isauto ? (lang == 1 ? "  (auto)" : lang == 2 ? "  (自動)" : "  (자동)") : (lang == 1 ? "  (chosen)" : lang == 2 ? "  (選択)" : "  (고름)"));
            str_append(v, sizeof v, "   ▼");
            uint32_t rh = LH + 14 * S;
            ui_rrect(x, y, w, rh, 6 * S, C_HEAD);
            hit_add(hc_dgpc, 0, 0, 0, 0, x, y, w, rh);
            dg_pc_dd_x = x + w / 5; dg_pc_dd_y = y + rh; dg_pc_dd_w = w - w / 5;
            text_clip(x + 12 * S, ui_ty(y, rh), lang == 1 ? "Test PC" : lang == 2 ? "テストPC" : "시험 PC", C_MUTED, 0xFF, w / 5);
            text_clip(x + w / 5, ui_ty(y, rh), v, C_TEXT, 0xFF, w - w / 5 - 12 * S);
            y += rh + 8 * S;
            if (dg_pc_i >= 0) { small_ell(x + 4 * S, y, diag_pc(dg_pc_i)->note, C_MUTED, 0xFF, w - 8 * S); y += SLH + 8 * S; }
        }
        for (int a = 0; a < DA_N && y + LH + 10 * S < bottom; a++) y += dg_verdict_row(x, y, w, a);
        y += 8 * S;
        int any = 0;
        for (int i = 0; i < dg_sn && y + SLH < bottom; i++) if (dg_sl[i][0] == 'S' && dg_sl[i][1] == 'M' && dg_sl[i][2] == 'B') {
            if (!any) { y += ui_section(x, y, w, "SMBIOS"); any = 1; }
            small_ell(x + 4 * S, y, dg_sl[i] + 7, C_TEXT, 0xFF, w - 8 * S); y += SLH + 6 * S;
        }
        small_ell(x, bottom - SLH, lang == 1 ? "Everything is saved to USB LOG\\DIAG.TXT (10 s after start, on tab change)" : lang == 2 ? "すべて USB LOG\\DIAG.TXT に保存 (起動10秒後・タブ切替時)" : "모든 내용은 USB LOG\\DIAG.TXT 에 저장 (켠 뒤 10초·탭을 바꿀 때)", C_MUTED, 0xFF, w);
        break;
    }
    case 7: {                                           /* 4.0.34: 무선랜 (PCI 정보, 읽기 전용) */
        y += ui_section(x, y, w, "WiFi");
        static char wl[80][64];
        int wn = P.info ? P.info(wl, 80) : 0;
        for (int i = 0; i < wn && y + SLH < bottom; i++) if (wl[i][0] && dg_kind(wl[i]) == 7) { small_ell(x + 4 * S, y, wl[i], C_TEXT, 0xFF, w - 8 * S); y += SLH + 6 * S; }
        break;
    }
    }
    if (dd_on && dd_id == 4000) dd_draw_at(dg_pc_dd_x, dg_pc_dd_y, dg_pc_dd_w, bottom);   /* 5.2.26 시험 PC 풀다운 */
    static const StrId dk[DG_TABS] = { S_DG_KEYS_U, S_DG_KEYS_K, S_DG_KEYS, S_DG_KEYS_M, S_DG_KEYS_J, S_DG_KEYS_T, S_DG_KEYS_U, S_DG_KEYS_U, S_DG_KEYS_U, S_DG_KEYS_U, S_DG_KEYS_U };
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, diag_tab == DG_SW ? (lang == 1 ? "Up/Down Select   Enter On/Off   Tab Tab   1~0 - Tab   Esc Close" : lang == 2 ? "↑↓ 選ぶ   Enter オン/オフ   Tab タブ   1~0 - タブ   Esc 閉じる" : "↑↓ 고르기   Enter 켬/끔   Tab 탭   1~0 - 탭   Esc 닫기") : diag_tab == DG_SUM ? (lang == 1 ? "Tab Tab   1~0 Tab   Enter Choose test PC   Esc Close" : lang == 2 ? "Tab タブ   1~0 タブ   Enter テストPCを選ぶ   Esc 閉じる" : "Tab 탭   1~0 탭   Enter 시험 PC 고르기   Esc 닫기") : T(dk[diag_tab]));
}

static void diag_key(int key)
{
    menu_dirty = 1;
    if (dd_on && dd_id == 4000) { dd_key_event(key); return; }   /* 5.2.26 시험 PC 풀다운 */
    if (diag_tab == 8 && dg_col_page > 0) {              /* 색 시험 전체 화면: Enter·누르기 = 다음 색, 끝나면 개요, Esc = 개요 */
        if (key == 0x01) dg_col_page = 0;
        else if (key == 0x1C || key == 0x9C || key == 0x39) dg_col_page = (dg_col_page + 1) % DG_COL_PAGES;
        full_redraw(); return;
    }
    if (key == 0x01) { dialog_back(); return; }
    if (key == 0x0F) { diag_tab = dg_order[(dg_pos(diag_tab) + ((mods & 0x03) ? DG_TABS - 1 : 1)) % DG_TABS]; full_redraw(); dg_save_due = P.ms() + 1500; fe_trace("tab key"); return; }
    if (key >= 0x02 && key <= 0x0C && (diag_tab != 1 || ui_pressing)) { diag_tab = dg_order[key - 0x02]; dg_col_page = 0; full_redraw(); dg_save_due = P.ms() + 1500; fe_trace("tab key"); return; }
    if (diag_tab == DG_SW) {
        if (key == 0xC8 && dg_sw_sel > 0) { dg_sw_sel--; full_redraw(); }
        else if (key == 0xD0 && dg_sw_sel < DG_SWN - 1) { dg_sw_sel++; full_redraw(); }
        else if (key == 0x1C || key == 0x9C || key == 0x39) dg_sw_toggle(dg_sw_sel);
        return;
    }
    if (diag_tab == 8 && (key == 0x1C || key == 0x9C || key == 0x39)) { dg_col_page = 1; full_redraw(); return; }
    if (diag_tab == DG_SUM && (key == 0x1C || key == 0x9C || key == 0x39)) { dg_pc_dd_open(); return; }   /* 시험 PC 는 풀다운으로 고름 (Enter·누르기) */
    if (diag_tab == 1) {                                /* 키보드 시험: 숫자 키도 보여 줌 (탭은 Tab으로) */
        dg_key = key; dg_key_mods = mods; dg_key_n++;
        return;
    }
    if (diag_tab == 2) {
        if (key == 0x14) tone_until = P.ms() + 1000;
        else if (key == 0x22) { if (P.snd_gpio && P.snd_gpio() >= 0) tone_until = P.ms() + 1500; }   /* G = 코덱 GPIO 값을 다음으로 돌리고 시험음 (진단 소리 탭) */
        else if (key == 0x13 && P.audio_select) { if (P.audio_select(audio_out, snd_status, sizeof snd_status)) P.audio_select(0, snd_status, sizeof snd_status); dg_save_due = P.ms() + 500; }   /* 다시 찾은 결과도 LOG */
        return;
    }
    if (diag_tab == 6) {
        if (key == 0xC8 && dg_vs_off > 0) { dg_vs_off--; full_redraw(); return; }       /* ↑ */
        if (key == 0xD0 && dg_vs_off < 40) { dg_vs_off++; full_redraw(); return; }      /* ↓ */
        if (key == 0x1F && P.gpu_try) { int r = P.gpu_try(4); toast(r ? "HDMI audio ON (see LOG)" : "HDMI audio OFF/failed", 0); full_redraw(); return; }   /* S: HDMI 소리 그래픽 쪽 (4.0.65) */
        if (key == 0x1E && P.gpu_try) { P.gpu_try(3); toast("HDA_DIAG.TXT saved (LOG)", 0); full_redraw(); return; }   /* A: HDMI 소리 진단 (4.0.62) */
        if (key == 0x2F) { gpu_sel = gpu_sel > 0 ? 0 : 1; full_redraw(); return; }          /* V: HDMI 시험 대상 AMD/Intel 고르기 (4.0.58) */
        if ((key == 0x1C || key == 0x9C || key == 0x22) && P.gpu_try) { P.gpu_try(gpu_sel > 0 ? 2 : 1); full_redraw(); return; }   /* Enter: GPU HDMI 실험 (4.0.34 3d-1) */
        if (P.vs_adj && (key == 0xCB || key == 0xCD)) { int v = P.vs_adj(key == 0xCD ? 8 : -8, 0); vs_pc_store(v); full_redraw(); } return;   /* 8.0.22: 바꾼 값을 이 PC 번호로 ini 에 저장 */
    }   /* 4.0.29 화면 맞추기 시점 */
    if (diag_tab == 4) {                                /* 조이스틱: R = 눌러 본 버튼 지움, F = 다음 조이스틱 */
        if (key == 0x13) { dg_jbtn_seen = 0; }
        else if (key == 0x21) { dg_jpad++; dg_jbtn_seen = 0; char nm[8]; PadView t; if (!P.pad_view || P.pad_view(dg_jpad, &t, nm, sizeof nm) < 0) dg_jpad = 0; }
        return;
    }
    if (diag_tab == 3 && (key == 0x13)) { dg_mx = dg_my = dg_wheel = 0; dg_events = 0; }   /* R: 시험 칸 지우기 */
    if (diag_tab == 3 && key == 0x21 && P.mouse_driver) {   /* F: USB 마우스 드라이버 바꾸기 (3.0.4) */
        mouse_own = P.mouse_driver((mouse_own + 1) % 3);
        dg_mx = dg_my = dg_wheel = 0; dg_events = 0;
        settings_changed();
        full_redraw();
    }
}

/* ================================================================== */
/* 4.0: 끝내기·PC 다시 시작 확인 (dialog 15)                            */
/* ================================================================== */
static void draw_power_ask(void)
{
    UiRect o;
    UiRect b = ui_full_card(T(pq_act == A_EXIT ? S_EXIT : S_REBOOT_PC), 0, 0, &o);
    hit_add(hc_none, 0, 0, 0, 0, o.x, o.y, o.w, o.h);
    const char *q = T(pq_act == A_EXIT ? S_PQ_EXIT : S_PQ_REBOOT), *nt = T(S_PQ_NOTE);
    uint32_t cy = dlg_cmd_y(), y = b.y + 22 * S;
    uint32_t qw = text_w(q), nw = ui_sw(nt);
    uint32_t lim = b.w - 40 * S;
    if (qw > lim) qw = lim;
    if (nw > lim) nw = lim;
    text_clip(b.x + (b.w - qw) / 2, y, q, C_TEXT, 0xFF, lim);
    y += LH + 10 * S;
    small_ell(b.x + (b.w - nw) / 2, y, nt, C_MUTED, 0xFF, lim);
    uint32_t bh = LH + 18 * S, bw = 150 * S, gap = 24 * S, by = cy - bh - 22 * S;
    uint32_t bx = b.x + (b.w - 2 * bw - gap) / 2;
    for (int i = 0; i < 2; i++) {
        const char *lab = T(i == 0 ? S_PQ_YES : S_PQ_NO);
        uint32_t x = bx + (uint32_t)i * (bw + gap), sel = pq_sel == i;
        ui_rrect(x + 2 * S, by + 3 * S, bw, bh, bh / 2, C_SHADOW);
        ui_rframe(x, by, bw, bh, bh / 2, sel ? (i == 0 ? C_WARN : C_ACC) : C_LINE, sel ? C_ROWSEL : C_HEAD);
        uint32_t tw = text_w(lab); if (tw > bw - 16 * S) tw = bw - 16 * S;
        text_clip(x + (bw - tw) / 2, ui_ty(by, bh), lab, C_TEXT, 0xFF, bw - 16 * S);
        UiHit *h = hit_add(hc_sel, (intptr_t)&pq_sel, i, 1, i == 0 ? 0x15 : 0x31, x, by, bw, bh);
        (void)h;
    }
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, T(S_PQ_KEYS));
}
