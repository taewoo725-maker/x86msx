/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fe_state.inc - 2.10: 상태 저장·불러오기, 화면 저장(PNG), 소리 저장(WAV).
 * fe.c 안에서 include 한다.
 *
 * 상태 파일 (USB\States\*.sta) — 5.2.18: 장치별 덩이 (src/msxstate.h)
 *   "X86MSXS2" + FEHD 덩이(프런트엔드: 기종·매체 경로·속도, 늘 맨 앞) + 기계 덩이들 (CORE, CPU, VDP, …, MRAM, …)
 *   장치 구조가 바뀐 덩이만 건너뛰고 나머지는 읽음. CORE(슬롯·매퍼 레지스터 등)는 5.2.87 부터 필드 표(COR2) — 필드를 더해도 옛 파일이 읽힘
 */
#include <stddef.h>
#include "../src/msxstate.h"

#define ST_MAGIC "X86MSXS2"
#define ST_FEHD_VER 1
#define ST_BLOB_MAX (10u << 20)         /* 작업 메모리 앞 10 MiB: 상태 (매퍼 RAM 4 MiB + 외장 매퍼 4 MiB까지) */
#define SHOT_OFF    (10u << 20)         /* 10~12 MiB: PNG */
#define SHOT_MAX    (2u << 20)
#define REC_OFF     (12u << 20)         /* 12 MiB~: 소리 녹음 */

typedef struct {                        /* FEHD 덩이 — 뒤에 필드를 붙이면 옛 파일은 모자란 부분을 0 으로 읽음 */
    char build[16];
    char machine[128];
    int32_t slot_expand;
    char cart_path[MSX_CART_SLOTS][FE_PATH_MAX];
    char cart_name[MSX_CART_SLOTS][64];
    int32_t cart_special[MSX_CART_SLOTS], cart_mapper[MSX_CART_SLOTS];
    char disk_path[2][FE_PATH_MAX], disk_name[2][64];
    int32_t disk_wp[2];
    char tape_path[FE_PATH_MAX], tape_name[64];
    char hd_path[FE_PATH_MAX], hd_name[64];
    int32_t pana_front, pana_pause;
    int32_t speed, speed_max;
} StHeader;
static StHeader st_hh;                 /* 저장·불러오기가 같이 씀 (BSS 아낌) */
#define ST_HEAD_AT (8u + ST_CHUNK_HDR)  /* 파일 안 FEHD 내용 자리 (상태 목록이 기종 이름만 읽을 때) */

static int st_sel, st_top, st_n;
static FeDirEntry st_list[64];
static char st_mach[64][64];                /* 파일 머리의 기종 이름 (창을 열 때 읽음) */

static int st_ready(void) { return P.scratch && P.scratch_size >= ST_BLOB_MAX + SHOT_MAX + (1u << 20) && P.write_file && P.create && P.read_file; }

/* 폴더 만들고 "0:\폴더\접두어NNNN.확장자"로 아직 없는 이름 */
static int next_free_name(const char *dir, const char *prefix, const char *ext, char *out, int max)
{
    char d[64] = "0:\\";
    str_append(d, sizeof d, dir);
    if (P.create(d, 1)) return -1;
    static uint8_t tiny[8];
    for (uint32_t k = 1; k < 10000; k++) {
        copy_str(out, max, d); str_append(out, max, "\\"); str_append(out, max, prefix);
        num_append(out, max, k, 4, '0');
        str_append(out, max, ext);
        uint32_t n = 0;
        if (P.read_file(out, tiny, sizeof tiny, &n)) return 0;          /* 없음 → 이 이름 */
    }
    return -1;
}

/* ---------------- 상태 저장 ---------------- */
static int state_save_to(const char *path)
{
    if (!st_ready()) return -1;
    for (int d = 0; d < 2; d++) disk_flush(d);              /* 디스크·테이프·하드 디스크 바뀐 내용을 먼저 파일로 */
    tape_flush();
    hd_flush();
    uint8_t *b = P.scratch;
    StHeader *h = &st_hh;
    memset(h, 0, sizeof *h);
    copy_str(h->build, sizeof h->build, MSXEMU_VERSION);
    copy_str(h->machine, sizeof h->machine, machine_name);
    h->slot_expand = slot_expand;
    for (int n = 0; n < MSX_CART_SLOTS; n++) {
        copy_str(h->cart_path[n], sizeof h->cart_path[n], cart_path[n]);
        copy_str(h->cart_name[n], sizeof h->cart_name[n], cart_name[n]);
        h->cart_special[n] = cart_special[n];
        h->cart_mapper[n] = cart_mapper[n];
    }
    for (int d = 0; d < 2; d++) {
        if (disk_size[d]) { copy_str(h->disk_path[d], sizeof h->disk_path[d], disk_path[d]); copy_str(h->disk_name[d], sizeof h->disk_name[d], disk_name[d]); }
        h->disk_wp[d] = disk_wp[d];
    }
    if (M->tape) { copy_str(h->tape_path, sizeof h->tape_path, tape_path); copy_str(h->tape_name, sizeof h->tape_name, tape_name); }
    copy_str(h->hd_path, sizeof h->hd_path, hd_path);
    copy_str(h->hd_name, sizeof h->hd_name, hd_name);
    h->pana_front = pana_front; h->pana_pause = pana_pause;
    h->speed = speed; h->speed_max = speed_max;
    memcpy(b, ST_MAGIC, 8);
    uint32_t len = 8, k = st_put(b + len, ST_BLOB_MAX - len, "FEHD", ST_FEHD_VER, h, (uint32_t)sizeof *h);
    if (!k) return -1;
    len += k;
    if (!(k = msx_st_save(M, b + len, ST_BLOB_MAX - len))) return -1;
    len += k;
    if (P.create(path, 0) || P.write_file(path, b, len)) return -1;
    return 0;
}

/* 카트리지 하나를 경로로 다시 꽂기 (상태 불러오기용, 리셋 없음) */
static int st_cart_reinsert(int n, const StHeader *h, MapperType mt)
{
    int k = special_kind(h->cart_special[n]);
    if (k >= 0) return special_apply(n, k, 1);
    if (!h->cart_path[n][0]) return 0;
    uint32_t size = 0;
    uint8_t *buf = cart_read_any(n, h->cart_path[n], &size);   /* 4.0.129: 2 MiB 넘는 롬 */
    if (!buf) return -1;
    if ((int)mt < 0) mt = cart_guess_mapper(buf, size);      /* 5.2.18 */
    if (msx_insert_cart(M, n, buf, size, mt)) return -1;
    if (buf == cart_big) cart_big_owner = n;
    copy_str(cart_path[n], sizeof cart_path[n], h->cart_path[n]);
    copy_str(cart_name[n], sizeof cart_name[n], h->cart_name[n]);
    cart_size[n] = size;
    cart_special[n] = 0;
    cart_mapper[n] = h->cart_mapper[n];
    return 0;
}

static char st_skip[96];                               /* 불러올 때 건너뛴 장치 (구조가 바뀜 → 켠 상태로) */
static void st_loaded_toast(const char *name) { if (st_skip[0]) toast(T(S_ST_PARTIAL), st_skip); else toast(T(S_ST_LOADED), name); }
static int st_resume_mode;                              /* 1 = 켤 때·기종 바꿀 때 이어하기 (resume_try) */
static int state_load_from(const char *path, char *err, int errlen)
{
    err[0] = 0; st_skip[0] = 0;
    if (!st_ready()) { copy_str(err, errlen, "no memory"); return -1; }
    uint8_t *b = P.scratch;
    uint32_t size = 0, hv = 0, hl = 0;
    if (P.read_file(path, b, ST_BLOB_MAX, &size) || size < ST_HEAD_AT || size > ST_BLOB_MAX) { copy_str(err, errlen, "read"); return -1; }
    const uint8_t *hp = memcmp(b, ST_MAGIC, 8) ? 0 : st_find(b + 8, size - 8, "FEHD", &hv, &hl);
    if (!hp || hv != ST_FEHD_VER || hl > sizeof(StHeader)) { copy_str(err, errlen, "not a state file"); return -1; }
    StHeader *h = &st_hh;
    memset(h, 0, sizeof *h); memcpy(h, hp, hl);
    const uint8_t *ck = b + 8; uint32_t cl = size - 8;      /* 기계 덩이들 (FEHD 는 msx_st_load 가 모르는 덩이로 건너뜀) */
    if (msx_st_check(ck, cl)) {                             /* 기계 뼈대(CORE)가 다른 빌드 — 기종·매체를 바꾸기 전에 */
        copy_str(err, errlen, T(S_ST_OTHER_VER)); str_append(err, errlen, " ("); str_append(err, errlen, h->build); str_append(err, errlen, ")");
        return -1;
    }
    if (st_resume_mode) {                                   /* 4.0.34: 켤 때 이어하기 — ini(마지막으로 끼운 매체)가 저장 상태보다 새로우면 그 매체로 새로 켬 (끼운 카트리지가 사라지던 것) */
        int same = !strcmp(h->machine, machine_name);
        for (int n = 0; same && n < MSX_CART_SLOTS; n++)
            if (cart_special[n] != h->cart_special[n] || strcmp(cart_path[n], h->cart_path[n]) || M->cart[n].present != (h->cart_special[n] || h->cart_path[n][0] ? 1 : 0)) same = 0;
        for (int d = 0; same && d < 2; d++) if (strcmp(disk_path[d], h->disk_path[d])) same = 0;
        if (same && strcmp(tape_path, h->tape_path)) same = 0;
        if (!same) { copy_str(err, errlen, "media changed"); return -1; }
    }
    /* 1. 기종 */
    if (strcmp(h->machine, machine_name)) {
        char e2[160];
        if (switch_machine(h->machine, e2, sizeof e2)) { copy_str(err, errlen, e2); return -1; }
    }
    /* 2. 매체를 저장 때와 같게 (리셋 없이) */
    for (int n = 0; n < MSX_CART_SLOTS; n++) {
        if (M->cart[n].present) cart_remove(n);
        cart_special[n] = 0; cart_path[n][0] = 0; cart_name[n][0] = 0; cart_mapper[n] = -1; cart_size[n] = 0;
    }
    slot_expand = h->slot_expand;
    if (msx_set_expand(M, slot_expand)) { slot_expand = 0; msx_set_expand(M, 0); }
    for (int n = 0; n < MSX_CART_SLOTS; n++) {
        int pr = 0, mp = -1;
        if (msx_st_cart(ck, cl, n, &pr, &mp)) {                 /* 카트 덩이 구조가 바뀜 → 매체 목록으로 */
            pr = h->cart_special[n] || h->cart_path[n][0];
            mp = h->cart_mapper[n];                     /* -1 = 롬을 보고 정함 */
        }
        if (pr && st_cart_reinsert(n, h, (MapperType)mp)) toast(T(S_LOAD_ERROR), h->cart_name[n][0] ? h->cart_name[n] : h->cart_path[n]);
    }
    for (int d = 0; d < 2; d++) {
        disk_wp[d] = h->disk_wp[d];
        if (h->disk_path[d][0]) insert_disk_file(d, h->disk_path[d], h->disk_name[d], 0);
        else if (disk_size[d]) disk_eject(d);
    }
    tape_eject();
    if (h->tape_path[0]) {
        uint32_t ts = 0;
        if (!tape_load_file(h->tape_path, &ts, 0, 0, 1)) {
            copy_str(tape_path, sizeof tape_path, h->tape_path);
            copy_str(tape_name, sizeof tape_name, h->tape_name);
            msx_tape_insert(M, tape_buf, ts, TAPE_MAX);
        }
    }
    if (h->hd_path[0] && strcmp(h->hd_path, hd_path)) hd_insert(h->hd_path, h->hd_name, 1);
    else if (!h->hd_path[0] && hd_path[0]) hd_eject();
    { char *sk = st_skip;
      if (msx_st_load(M, ck, cl, sk, sizeof st_skip)) { copy_str(err, errlen, T(S_ST_OTHER_VER)); str_append(err, errlen, " ("); str_append(err, errlen, h->build); str_append(err, errlen, ")"); return -1; }
      for (int gq = 0; gq < GM_INST_MAX; gq++) gmi_reset(gq);   /* 3.4.5: 소프트웨어 MIDI 음원은 상태에 없음 → 모든 음 끔 */
    }
    rtc_sync(1);
    pana_front = h->pana_front; pana_pause = h->pana_pause;
    if (h->speed >= 10 && h->speed <= 1000) { speed = h->speed; speed_max = h->speed_max != 0; }
    run_state = RUN_ON;
    input_init(&in);
    memset(M->kbd, 0xFF, sizeof M->kbd);                    /* 눌린 키는 놓은 것으로 */
    M->vdp.pal_dirty = 1;
    full_redraw();
    settings_changed();
    return 0;
}

static void quick_path(char *out, int max) { copy_str(out, max, "0:\\States\\quicksave.sta"); }

static void do_quick_save(void)
{
    char p[96];
    if (!st_ready() || P.create("0:\\States", 1)) { toast(T(S_ST_SAVE_FAIL), 0); return; }
    quick_path(p, sizeof p);
    toast(state_save_to(p) ? T(S_ST_SAVE_FAIL) : T(S_ST_SAVED), "quicksave.sta");
}

static void do_quick_load(void)
{
    char p[96], err[160];
    quick_path(p, sizeof p);
    if (state_load_from(p, err, sizeof err)) toast(T(S_ST_LOAD_FAIL), err);
    else st_loaded_toast("quicksave.sta");
}

static void do_state_save(void)
{
    char p[160];
    if (!st_ready() || next_free_name("States", "state", ".sta", p, sizeof p)) { toast(T(S_ST_SAVE_FAIL), 0); return; }
    toast(state_save_to(p) ? T(S_ST_SAVE_FAIL) : T(S_ST_SAVED), path_base(p));
}

/* ---------------- 상태 불러오기 창 (대화 창 10) ---------------- */
static int st_date_key(const FeDirEntry *e) { return ((((e->year * 13 + e->mon) * 32 + e->day) * 24 + e->hour) * 60 + e->min); }

static void open_state_list(void)
{
    st_n = 0; st_sel = 0; st_top = 0;
    int n = P.list_dir ? P.list_dir("0:\\States", entries, DIR_MAX) : -1;
    for (int i = 0; i < n && st_n < 64; i++) {
        const char *nm = entries[i].key[0] ? entries[i].key : entries[i].name;
        if (entries[i].is_dir || !ext_is(nm, "sta", 0, 0)) continue;
        st_list[st_n++] = entries[i];
    }
    for (int i = 1; i < st_n; i++)                          /* 새것부터 */
        for (int j = i; j > 0 && st_date_key(&st_list[j]) > st_date_key(&st_list[j - 1]); j--) { FeDirEntry t = st_list[j]; st_list[j] = st_list[j - 1]; st_list[j - 1] = t; }
    static uint8_t hb[ST_HEAD_AT + sizeof(StHeader)];
    for (int i = 0; i < st_n; i++) {
        char p[160] = "0:\\States\\";
        str_append(p, sizeof p, st_list[i].key[0] ? st_list[i].key : st_list[i].name);
        uint32_t n = 0;
        st_mach[i][0] = 0;
        if (!P.read_file(p, hb, sizeof hb, &n) && n >= ST_HEAD_AT + offsetof(StHeader, machine) + 64 && !memcmp(hb, ST_MAGIC, 8) && !memcmp(hb + 8, "FEHD", 4)) copy_str(st_mach[i], 64, (const char *)hb + ST_HEAD_AT + offsetof(StHeader, machine));
    }
    open_dialog(10);
}

static void draw_state_list(void)
{
    UiRect o;
    char chip[24] = ""; num_append(chip, sizeof chip, (uint32_t)st_n, 0, ' '); str_append(chip, sizeof chip, " states");
    UiRect b = ui_full_card(T(S_ST_LIST), "States\\", chip, &o);
    uint32_t pad = 14 * S, X = b.x + pad, W = b.w - 2 * pad, rh = LH + SLH + 16 * S;
    uint32_t y0 = b.y + 10 * S, by = dlg_cmd_y() - 8 * S;
    if (!st_n) text_clip(X + 16 * S, y0, T(S_ST_NONE), C_MUTED, 0xFF, W - 32 * S);
    int vis = (int)((by - y0) / rh);
    if (vis < 1) vis = 1;
    if (st_sel < st_top) st_top = st_sel;
    if (st_sel >= st_top + vis) st_top = st_sel - vis + 1;
    for (int r = 0; r < vis && st_top + r < st_n; r++) {
        int i = st_top + r, sel = i == st_sel;
        const FeDirEntry *e = &st_list[i];
        uint32_t y = y0 + (uint32_t)r * rh;
        ui_list_row_bg(X, y + 2 * S, W - 10 * S, rh - 4 * S, sel, 0, 1);
        hit_add(hc_sel, (intptr_t)&st_sel, i, 0, 0, X, y, W - 10 * S, rh);   /* 3.0 마우스 */
        if (!sel) ui_rrect(X, y + 2 * S, W - 10 * S, rh - 4 * S, 6 * S, C_HEAD);
        uint32_t tx0 = X + 18 * S + LH;                      /* 4.0.30: 왼쪽에 상태 파일 아이콘 */
        draw_icon_hd(X + 10 * S, y + (rh - LH) / 2, LH, ICON_STATE, 3, 15, sel ? C_ACC2 : C_HEAD);
        text_clip(tx0, y + 8 * S, e->name, sel ? 15 : C_TEXT, 0xFF, W / 2);
        char d[32] = "";
        num_append(d, sizeof d, e->year, 4, '0'); str_append(d, sizeof d, "-"); num_append(d, sizeof d, e->mon, 2, '0'); str_append(d, sizeof d, "-");
        num_append(d, sizeof d, e->day, 2, '0'); str_append(d, sizeof d, "  "); num_append(d, sizeof d, e->hour, 2, '0'); str_append(d, sizeof d, ":");
        num_append(d, sizeof d, e->min, 2, '0');
        text_clip(X + W - 24 * S - text_w(d), y + 8 * S, d, sel ? 15 : C_MUTED, 0xFF, W / 3);
        small_ell(tx0, y + 10 * S + LH, st_mach[i][0] ? st_mach[i] : "-", sel ? 15 : C_MUTED, 0xFF, W - 60 * S);
    }
    ui_sbar(X + W - 5 * S, y0, (uint32_t)vis * rh, &st_top, &st_sel, vis, st_n);
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, T(S_ST_KEYS));
}

static void state_list_key(int key)
{
    menu_dirty = 1;
    switch (key) {
    case 0x01: dialog_back(); break;
    case 0xC8: if (st_sel > 0) st_sel--; break;
    case 0xD0: if (st_sel < st_n - 1) st_sel++; break;
    case 0xC9: st_sel = st_sel > 10 ? st_sel - 10 : 0; break;
    case 0xD1: st_sel = st_sel + 10 < st_n ? st_sel + 10 : (st_n ? st_n - 1 : 0); break;
    case 0x1C: case 0x9C:
        if (st_n) {
            char p[160] = "0:\\States\\", err[160];
            const FeDirEntry *e = &st_list[st_sel];
            str_append(p, sizeof p, e->key[0] ? e->key : e->name);
            (void)err;
            menu_close_all();
            copy_str(pend_path, sizeof pend_path, p);           /* Enter를 놓은 뒤 불러옴 (2.10.1) */
            pend_act = A_LOAD_PATH; pend_since = P.ms();
        }
        break;
    }
}

/* ---------------- 화면 저장 (PNG) ---------------- */
static uint32_t crc_tab[256];
static uint32_t crc32_upd(uint32_t c, const uint8_t *p, uint32_t n)
{
    if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; crc_tab[i] = k; }
    c = ~c;
    while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}
static void be32w(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* MSX 화면 한 줄을 RGB로 (512점) */
static void shot_line_raw(int y, int odd, uint8_t *out)
{
    const uint8_t *s0 = odd ? M->vdp.fb_odd[y] : M->vdp.fb[y];
    int dr = odd ? M->vdp.line_direct_odd[y] : M->vdp.line_direct[y];
    if (dr >= 2) {
        uint8_t raw[256], rgb[256][3], pi[256];
        for (int x = 0; x < 256; x++) raw[x] = s0[2 * x];
        vdp_yjk_line(raw, dr == 3, rgb, pi);
        const uint8_t *ov = M->vdp.spr_ov[y];
        for (int x = 0; x < VDP_W; x++) {
            int sx = x >> 1;
            uint8_t r, g, b;
            if (ov[sx] != 0xFF) vdp_g7_rgb(&M->vdp, ov[sx], &r, &g, &b);
            else if (pi[sx] != 0xFF) { r = mon_rgb[pi[sx] & 15][0]; g = mon_rgb[pi[sx] & 15][1]; b = mon_rgb[pi[sx] & 15][2]; out[x * 3] = r; out[x * 3 + 1] = g; out[x * 3 + 2] = b; continue; }
            else { r = rgb[sx][0]; g = rgb[sx][1]; b = rgb[sx][2]; }
            monitor_tint(&r, &g, &b);
            out[x * 3] = r; out[x * 3 + 1] = g; out[x * 3 + 2] = b;
        }
        return;
    }
    for (int x = 0; x < VDP_W; x++) {
        uint8_t r, g, b;
        if (dr) { vdp_g7_rgb(&M->vdp, s0[x], &r, &g, &b); monitor_tint(&r, &g, &b); }
        else if (M->vdp.pal_nsnap > 1 && y < (int)(sizeof M->vdp.line_pal / sizeof M->vdp.line_pal[0])) {   /* 4.0.31: 줄마다 팔레트 */
            const uint8_t *q = M->vdp.pal_snap[M->vdp.line_pal[y]][s0[x] & 15];
            r = q[0]; g = q[1]; b = q[2]; monitor_tint(&r, &g, &b);
        }
        else { r = mon_rgb[s0[x] & 15][0]; g = mon_rgb[s0[x] & 15][1]; b = mon_rgb[s0[x] & 15][2]; }
        out[x * 3] = r; out[x * 3 + 1] = g; out[x * 3 + 2] = b;
    }
}

/* 4.0.31: 화면 위치 조정(R#18 가로)을 줄마다 적용 — 화면에 보이는 대로 저장·녹화 */
static void shot_line(int y, int odd, uint8_t *out)
{
    shot_line_raw(y, odd, out);
    if (!M->vdp.v9938 || y < 0 || y >= (int)(sizeof M->vdp.line_adj / sizeof M->vdp.line_adj[0])) return;
    int adj = ((M->vdp.line_adj[y] & 15) ^ 7) - 7;
    if (!adj) return;
    int sh = adj * 2, n = VDP_W;                         /* 512점 기준 */
    uint8_t bd = M->vdp.line_bd[y] & 15;
    if (sh > 0) { memmove(out + sh * 3, out, (size_t)(n - sh) * 3); for (int x = 0; x < sh; x++) { out[x * 3] = mon_rgb[bd][0]; out[x * 3 + 1] = mon_rgb[bd][1]; out[x * 3 + 2] = mon_rgb[bd][2]; } }
    else { sh = -sh; memmove(out, out + sh * 3, (size_t)(n - sh) * 3); for (int x = n - sh; x < n; x++) { out[x * 3] = mon_rgb[bd][0]; out[x * 3 + 1] = mon_rgb[bd][1]; out[x * 3 + 2] = mon_rgb[bd][2]; } }
}

/* 3.2.10: zlib 고정 허프만 압축 (LZ77은 거리 3(앞 점)·한 줄(위 줄)만 찾음 — 큰 화면도 몇 % 크기로) */
typedef struct { uint8_t *o; uint32_t n, max, bits, nb; int err; } ZBits;
static void zb_put(ZBits *z, uint32_t v, int n)              /* 아래 비트부터 */
{
    z->bits |= v << z->nb; z->nb += (uint32_t)n;
    while (z->nb >= 8) { if (z->n < z->max) z->o[z->n++] = (uint8_t)z->bits; else z->err = 1; z->bits >>= 8; z->nb -= 8; }
}
static void zb_huff(ZBits *z, uint32_t code, int len)       /* 허프만 부호는 위 비트부터 */
{
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    zb_put(z, r, len);
}
static void zb_lit(ZBits *z, int v)
{
    if (v < 144) zb_huff(z, 0x30u + (uint32_t)v, 8);
    else if (v < 256) zb_huff(z, 0x190u + (uint32_t)(v - 144), 9);
    else if (v < 280) zb_huff(z, (uint32_t)(v - 256), 7);
    else zb_huff(z, 0xC0u + (uint32_t)(v - 280), 8);
}
static void zb_match(ZBits *z, uint32_t len, uint32_t dist)
{
    static const uint16_t lb[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const uint8_t le[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const uint32_t db[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    int li = 28; while (lb[li] > len) li--;
    zb_lit(z, 257 + li); if (le[li]) zb_put(z, len - lb[li], le[li]);
    int di = 29; while (db[di] > dist) di--;
    zb_huff(z, (uint32_t)di, 5);
    int de = di < 4 ? 0 : di / 2 - 1;
    if (de) zb_put(z, dist - db[di], de);
}
/* raw를 zlib 스트림으로 out에. 돌려줌: 바이트 수, 0 = 모자람 */
static uint32_t zlib_fixed(const uint8_t *raw, uint32_t len, uint32_t row, uint8_t *out, uint32_t max)
{
    ZBits z = { out, 0, max, 0, 0, 0 };
    zb_put(&z, 0x78, 8); zb_put(&z, 0x01, 8);
    zb_put(&z, 1, 1); zb_put(&z, 1, 2);                  /* 마지막 블록, 고정 허프만 */
    uint32_t i = 0;
    while (i < len) {
        uint32_t best = 0, bd = 0;
        const uint32_t ds[2] = { 3, row };
        for (int k = 0; k < 2; k++) {
            uint32_t d = ds[k];
            if (d > i || d > 32768) continue;
            uint32_t m = 0, lim = len - i < 258 ? len - i : 258;
            while (m < lim && raw[i + m] == raw[i + m - d]) m++;
            if (m > best) { best = m; bd = d; }
        }
        if (best >= 3) { zb_match(&z, best, bd); i += best; }
        else zb_lit(&z, raw[i++]);
        if (z.err) return 0;
    }
    zb_lit(&z, 256);
    if (z.nb) zb_put(&z, 0, 8 - (int)z.nb);
    uint32_t a1 = 1, a2 = 0;
    for (uint32_t k = 0; k < len; k++) { a1 = (a1 + raw[k]) % 65521; a2 = (a2 + a1) % 65521; }
    uint32_t ad = (a2 << 16) | a1;
    zb_put(&z, ad >> 24, 8); zb_put(&z, (ad >> 16) & 255, 8); zb_put(&z, (ad >> 8) & 255, 8); zb_put(&z, ad & 255, 8);
    return z.err ? 0 : z.n;
}

/* 화면 저장: 512 x 줄 x 2 (3.2.10부터 압축) */
/* ---------------- 5.2.69: 조금씩 저장 (화면·소리·영상) — 프레임마다 몇 ms 씩 파일 끝에 덧붙여 MSX 를 멈추지 않음 ----------------
 * P.append 가 있는 판(UEFI)만. 없으면(레거시) 예전처럼 한 번에. 버퍼는 저장이 끝날 때까지 그대로 둬야 함 (scratch PNG·녹음 자리, 영상 버퍼) —
 * 같은 버퍼를 쓰는 동작(상태 저장·불러오기, 새 녹음·녹화, SoundFont·MT-32 자리 잡기, 창 열기, 끝내기)은 bgw_finish() 로 먼저 마저 씀 */
static struct { char path[160]; const uint8_t *buf; uint32_t len, off; StrId ok, fail; char msg[96]; } bgw;
static int bgw_busy(void) { return bgw.buf != 0; }
static void bgw_end(int ok) { bgw.buf = 0; margin_bd = -1; toast(T(ok ? bgw.ok : bgw.fail), ok ? bgw.msg : 0); }
static int bgw_start(const char *path, const uint8_t *buf, uint32_t len, StrId ok, StrId fail, const char *msg)
{
    if (P.create(path, 0)) { toast(T(fail), 0); return -1; }
    if (!P.append) {                                    /* 레거시: 한 번에 */
        if (P.write_file(path, buf, len)) { toast(T(fail), 0); return -1; }
        toast(T(ok), msg); return 0;
    }
    copy_str(bgw.path, sizeof bgw.path, path); bgw.buf = buf; bgw.len = len; bgw.off = 0; bgw.ok = ok; bgw.fail = fail;
    copy_str(bgw.msg, sizeof bgw.msg, msg ? msg : "");
    return 0;
}
static void bgw_step(uint32_t budget_ms)
{
    if (!bgw.buf) return;
    uint32_t t0 = P.ms();
    do {
        uint32_t k = bgw.len - bgw.off; if (k > (64u << 10)) k = 64u << 10;
        if (k && P.append(bgw.path, bgw.buf + bgw.off, k)) { bgw_end(0); return; }
        bgw.off += k;
        if (bgw.off >= bgw.len) { bgw_end(1); return; }
    } while (P.ms() - t0 < budget_ms);
}
static void bgw_finish(void) { while (bgw.buf) bgw_step(1000); }

/* 5.3.28: 메뉴·창이 열려 있을 때는 보이는 화면 그대로 (프레임 버퍼 전체) 를 PNG 로. 창이 없을 때의 MSX 화면 스크린샷(아래 do_screenshot)은 그대로 둠 */
static int do_screenshot_ui(void)
{
    if (!D.fb || rec_on || P.scratch_size < (24u << 20)) return 0;      /* 소리 녹음 중이면 작업 메모리가 겹쳐 옛 방식으로 */
    char path[160];
    if (!st_ready() || next_free_name("Screenshots", "X86MSX_", ".png", path, sizeof path)) { toast(T(S_SHOT_FAIL), 0); return 1; }
    uint32_t N = 1;
    while ((D.w / N) * (D.h / N) * 3 + D.h / N > (14u << 20)) N++;
    uint32_t w = D.w / N, h = D.h / N, row = w * 3 + 1;
    uint8_t *raw = P.scratch + (16u << 20), *png = P.scratch + (4u << 20);
    static uint8_t srow[16384], vpal[256 * 3];
    if (D.w * (uint32_t)D.bpp > sizeof srow) return 0;
    if (D.bpp == 1) {
        if (P.pal_get) P.pal_get(vpal);
        else {
            for (int i = 0; i < 16; i++) memcpy(vpal + i * 3, &mon_rgb[i][0], 3);
            for (int i = 0; i < 210; i++) { vpal[(16 + i) * 3] = (uint8_t)(i / 35 * 255 / 5); vpal[(16 + i) * 3 + 1] = (uint8_t)(i / 5 % 7 * 255 / 6); vpal[(16 + i) * 3 + 2] = (uint8_t)(i % 5 * 255 / 4); }
            for (int i = 0; i < 16; i++) memcpy(vpal + (UI_DAC + i) * 3, ui_rgb[i], 3);
        }
    }
    int ri = D.bgr ? 2 : 0, bi = D.bgr ? 0 : 2;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *o = raw + y * row; *o++ = 0;
        memcpy(srow, D.fb + (uintptr_t)(y * N) * D.pitch, D.w * (uint32_t)D.bpp);
        for (uint32_t x = 0; x < w; x++, o += 3) {
            uint32_t sx = x * N;
            if (D.bpp == 1) { const uint8_t *a = vpal + srow[sx] * 3; o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; }
            else { const uint8_t *a = srow + sx * 4; o[0] = a[ri]; o[1] = a[1]; o[2] = a[bi]; }
        }
    }
    uint32_t n = 0;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    memcpy(png, sig, 8); n = 8;
    be32w(png + n, 13); memcpy(png + n + 4, "IHDR", 4);
    be32w(png + n + 8, w); be32w(png + n + 12, h);
    png[n + 16] = 8; png[n + 17] = 2; png[n + 18] = 0; png[n + 19] = 0; png[n + 20] = 0;
    be32w(png + n + 21, crc32_upd(0, png + n + 4, 17)); n += 25;
    uint32_t idat = n;
    uint32_t zl = zlib_fixed(raw, row * h, row, png + idat + 8, (12u << 20) - 1024);   /* 4~16 MiB */
    if (!zl) { toast(T(S_SHOT_FAIL), 0); return 1; }
    be32w(png + idat, zl); memcpy(png + idat + 4, "IDAT", 4);
    n = idat + 8 + zl;
    be32w(png + n, crc32_upd(0, png + idat + 4, zl + 4)); n += 4;
    be32w(png + n, 0); memcpy(png + n + 4, "IEND", 4); be32w(png + n + 8, crc32_upd(0, png + n + 4, 4)); n += 12;
    bgw_start(path, png, n, S_SHOT_SAVED, S_SHOT_FAIL, path_base(path));
    return 1;
}
static void do_screenshot(void)
{
    bgw_finish();
    if (shot_full && do_screenshot_ui()) return;   /* 5.3.28: 보이는 화면 그대로 (끄면 아래 MSX 화면만) */
    char path[160];
    if (!st_ready() || next_free_name("Screenshots", "X86MSX_", ".png", path, sizeof path)) { toast(T(S_SHOT_FAIL), 0); return; }
    uint32_t lines = disp_lines ? disp_lines : 192;
    uint32_t w = VDP_W, h = lines * 2, row = w * 3 + 1;
    uint8_t *raw = P.scratch;                           /* 상태 저장 자리(앞 10 MiB)를 잠깐 씀 */
    if (row * h > (4u << 20)) { toast(T(S_SHOT_FAIL), 0); return; }
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *r = raw + y * row;
        r[0] = 0;
        int ly = (int)(y / 2), odd = (y & 1) && M->vdp.line_eo[ly];
        if (run_state == RUN_STOPPED) { memset(r + 1, 0, w * 3); continue; }
        shot_line(ly, odd, r + 1);
    }
    uint8_t *png = P.scratch + (4u << 20);              /* 4~10 MiB */
    uint32_t n = 0;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    memcpy(png, sig, 8); n = 8;
    be32w(png + n, 13); memcpy(png + n + 4, "IHDR", 4);
    be32w(png + n + 8, w); be32w(png + n + 12, h);
    png[n + 16] = 8; png[n + 17] = 2; png[n + 18] = 0; png[n + 19] = 0; png[n + 20] = 0;
    be32w(png + n + 21, crc32_upd(0, png + n + 4, 17)); n += 25;
    uint32_t idat = n;
    uint32_t zl = zlib_fixed(raw, row * h, row, png + idat + 8, (6u << 20) - 1024);
    if (!zl) { toast(T(S_SHOT_FAIL), 0); return; }
    be32w(png + idat, zl); memcpy(png + idat + 4, "IDAT", 4);
    n = idat + 8 + zl;
    be32w(png + n, crc32_upd(0, png + idat + 4, zl + 4)); n += 4;
    be32w(png + n, 0); memcpy(png + n + 4, "IEND", 4); be32w(png + n + 8, crc32_upd(0, png + n + 4, 4)); n += 12;
    bgw_start(path, png, n, S_SHOT_SAVED, S_SHOT_FAIL, path_base(path));   /* 조금씩 */
}

/* ---------------- 소리 저장 (WAV, 48 kHz 16비트 모노) ---------------- */
static uint32_t rec_max(void) { return P.scratch_size > REC_OFF + 44 ? (P.scratch_size - REC_OFF - 44) / 2 : 0; }

static void rec_stop(void)
{
    if (!rec_on) return;
    rec_on = 0;
    margin_bd = -1;
    char path[160];
    if (next_free_name("Audio", "X86MSX_", ".wav", path, sizeof path)) { toast(T(S_REC_FAIL), 0); return; }
    uint8_t *w = P.scratch + REC_OFF;
    uint32_t data = rec_n * 2;
    static const uint8_t hdr[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0, 0x80,0xBB,0,0, 0,0x77,1,0, 2,0,16,0, 'd','a','t','a',0,0,0,0 };
    memcpy(w, hdr, 44);
    uint32_t riff = data + 36;
    w[4] = (uint8_t)riff; w[5] = (uint8_t)(riff >> 8); w[6] = (uint8_t)(riff >> 16); w[7] = (uint8_t)(riff >> 24);
    w[40] = (uint8_t)data; w[41] = (uint8_t)(data >> 8); w[42] = (uint8_t)(data >> 16); w[43] = (uint8_t)(data >> 24);
    bgw_start(path, w, 44 + data, S_REC_SAVED, S_REC_FAIL, path_base(path));   /* 조금씩 (녹음 자리는 다음 녹음 전까지 그대로) */
}

static void rec_toggle(void)
{
    if (rec_on) { rec_stop(); return; }
    bgw_finish();                                       /* 앞 녹음을 아직 쓰는 중이면 마저 */
    if (!st_ready() || !rec_max()) { toast(T(S_REC_FAIL), 0); return; }
    rec_on = 1; rec_n = 0; rec_start = P.ms();
    margin_bd = -1;
    toast(T(S_REC_START), 0);
}

/* play_audio가 한 프레임 소리마다 부름 */
static void rec_feed(const int16_t *s, int n)
{
    if (!rec_on || n <= 0) return;
    uint32_t room = rec_max() - rec_n;
    if ((uint32_t)n > room) n = (int)room;
    int16_t *d = (int16_t *)(void *)(P.scratch + REC_OFF + 44);
    memcpy(d + rec_n, s, (size_t)n * 2);
    rec_n += (uint32_t)n;
    if (rec_n >= rec_max()) rec_stop();                     /* 가득 참: 저장하고 끝 */
}

/* 키를 모두 놓은 뒤 실행 (2.10.1) */
static void run_pending(int a)
{
    if (a != A_SCREENSHOT && a != A_AUDIO_REC) bgw_finish();   /* 상태 저장·불러오기는 scratch 를 씀 */
    switch (a) {
    case A_QUICK_LOAD: do_quick_load(); break;
    case A_QUICK_SAVE: do_quick_save(); break;
    case A_STATE_SAVE: do_state_save(); break;
    case A_SCREENSHOT: do_screenshot(); break;
    case A_AUDIO_REC: rec_stop(); break;
    case A_LOAD_PATH: {
        char err[160];
        if (state_load_from(pend_path, err, sizeof err)) toast(T(S_ST_LOAD_FAIL), err);
        else st_loaded_toast(path_base(pend_path));
        break;
    }
    }
}

/* ---------------- SRAM (배터리 백업) 저장 — 2.11 ---------------- */
/* FM-PAC = USB\SRAM\FMPAC.pac (blueMSX와 같은 "PAC2 BACKUP DATA" 머리 + 8190바이트),
 * ASCII8/16 SRAM 게임 = USB\SRAM\<롬 파일 이름>.sram (8 KiB 그대로) */
static const char pac_hdr[16] = { 'P','A','C','2',' ','B','A','C','K','U','P',' ','D','A','T','A' };
static uint32_t sram_due[MSX_CART_ALL];

/* 무엇을·어떤 꼴로·어떤 이름으로 남길지는 장치 선언(src/persist.c)이 정함. 여기는 읽고 쓰기·시점만 */
static int sram_decl(int n, MsxPersist *p)
{
    return msx_cart_persist(M, n, n < MSX_CART_SLOTS && cart_path[n][0], n < MSX_CART_SLOTS && cart_special[n], p);
}

static void sram_path(int n, char *out, int max)
{
    MsxPersist d;
    copy_str(out, max, "0:\\SRAM\\");
    if (!sram_decl(n, &d)) return;
    if (!d.from_rom) { str_append(out, max, d.name); return; }
    const char *b = path_base(cart_path[n]);
    int l = (int)strlen(out), k = 0;
    for (; b[k] && l + k < max - 6; k++) out[l + k] = b[k];
    out[l + k] = 0;
    for (int i = (int)strlen(out) - 1; i > l; i--) if (out[i] == '.') { out[i] = 0; break; }   /* 확장자 빼기 */
    str_append(out, max, d.name);
}

/* 4.0.131: ASCII16-X·Yamanooto 플래시 저장. 바뀐 8 KiB 조각만 "XFL1" + 롬 크기 + 조각 수, 조각마다 (번호 2바이트 + 8192바이트).
 * 조각이 127개를 넘으면 플래시 전체를 그대로 (파일 크기 = 롬 크기). 켤 때 롬 위에 덮어 씀 */
#define FL_REC_MAX 127
static uint8_t fl_rec[16 + FL_REC_MAX * (2 + 8192)];
static void sram_path(int n, char *out, int max);
static void flash_file_load(int n)
{
    Cartridge *c = &M->cart[n];
    char p[FE_PATH_MAX]; uint32_t sz = 0;
    uint8_t *fl = (uint8_t *)(uintptr_t)c->data;
    sram_path(n, p, sizeof p);
    c->sram_dirty = 0;
    if (P.read_file(p, fl_rec, 16, &sz) || !sz) return;
    if (!memcmp(fl_rec, "XFL1", 4)) {
        uint32_t rs = (uint32_t)fl_rec[4] | (uint32_t)fl_rec[5] << 8 | (uint32_t)fl_rec[6] << 16 | (uint32_t)fl_rec[7] << 24;
        uint32_t cnt = (uint32_t)fl_rec[8] | (uint32_t)fl_rec[9] << 8;
        if (rs > c->size || cnt > FL_REC_MAX || sz != 16 + cnt * (2 + 8192u)) return;
        if (fl_rec[10] && fl_rec[10] != (uint8_t)(c->mapper + 1)) return;   /* 다른 매퍼(예: 전에 Yamanooto 로 잘못 잡혔을 때)로 저장한 조각은 롬을 망가뜨리므로 쓰지 않음 (BURAI 선택창 글씨 사라짐) */
        if (P.read_file(p, fl_rec, sizeof fl_rec, &sz)) return;
        for (uint32_t i = 0; i < cnt; i++) {
            const uint8_t *r = fl_rec + 16 + i * (2 + 8192u);
            uint32_t k = (uint32_t)r[0] | (uint32_t)r[1] << 8;
            if (rs < c->size && (uint64_t)k * 8192u < (uint64_t)(c->size - rs)) k += rs / 8192u;   /* 예전 판은 롬 파일 크기로 감싸 저장했음 — 파일 밖(칩 위쪽)에 쓴 기록을 제자리로 */
            if ((k + 1) * 8192u > c->size) continue;
            memcpy(fl + k * 8192u, r + 2, 8192);
            c->fl_dirty[k >> 3] |= (uint8_t)(1u << (k & 7));
        }
    } else if (sz == c->size || (sz > MSX_ROM_MAX && sz < c->size)) {      /* 칩 크기로 채우기 전 저장 파일(롬 파일 크기)도 */
        if (P.read_file(p, fl, sz, &sz)) return;
        memset(c->fl_dirty, 0xFF, sizeof c->fl_dirty);
    } else return;
    msx_reset_maps(M);
}
static void flash_file_save(int n)
{
    Cartridge *c = &M->cart[n];
    char p[FE_PATH_MAX];
    uint32_t cnt = 0, total = (c->size + 8191) >> 13;
    for (uint32_t k = 0; k < total; k++) if (c->fl_dirty[k >> 3] & (1u << (k & 7))) cnt++;
    sram_path(n, p, sizeof p);
    if (P.create("0:\\SRAM", 1) || P.create(p, 0)) return;
    if (cnt > FL_REC_MAX) { if (P.write_file(p, c->data, c->size)) return; c->sram_dirty = 0; return; }
    memcpy(fl_rec, "XFL1", 4);
    fl_rec[4] = (uint8_t)c->size; fl_rec[5] = (uint8_t)(c->size >> 8); fl_rec[6] = (uint8_t)(c->size >> 16); fl_rec[7] = (uint8_t)(c->size >> 24);
    fl_rec[8] = (uint8_t)cnt; fl_rec[9] = (uint8_t)(cnt >> 8); memset(fl_rec + 10, 0, 6); fl_rec[10] = (uint8_t)(c->mapper + 1);   /* 저장한 매퍼 */
    uint32_t i = 0;
    for (uint32_t k = 0; k < total; k++) if (c->fl_dirty[k >> 3] & (1u << (k & 7))) {
        uint8_t *r = fl_rec + 16 + i++ * (2 + 8192u);
        r[0] = (uint8_t)k; r[1] = (uint8_t)(k >> 8);
        memcpy(r + 2, c->data + k * 8192u, 8192);
    }
    if (P.write_file(p, fl_rec, 16 + cnt * (2 + 8192u))) return;
    c->sram_dirty = 0;
}

static void sram_load(int n)
{
    MsxPersist d;
    if (!sram_decl(n, &d) || !P.read_file) return;
    Cartridge *c = &M->cart[n];
    if (d.fmt == PS_FLASH) { flash_file_load(n); return; }
    char p[FE_PATH_MAX]; uint32_t size = 0;
    sram_path(n, p, sizeof p);
    if (d.fmt == PS_RAW && d.data != c->sram) {               /* 3.1.1: 카트리지 메모리 그대로 (바로 읽음) */
        if (!P.read_file(p, d.data, d.load_max, &size) && size) msx_reset_maps(M);
        c->sram_dirty = 0;
        return;
    }
    static uint8_t b[32768 + 16];                           /* 3.8.0: ASCII8 SRAM 32 KiB */
    if (P.read_file(p, b, sizeof b, &size) || !size) return;
    if (d.fmt == PS_PAC && size >= 16 && !memcmp(b, pac_hdr, 16)) memcpy(d.data, b + 16, size - 16 > d.size ? d.size : size - 16);
    else memcpy(d.data, b, size > d.load_max ? d.load_max : size);
    c->sram_dirty = 0;
    msx_reset_maps(M);                                      /* 지금 SRAM이 보이는 자리도 새 내용으로 */
}

static void sram_save(int n)
{
    if (n < 0 || n >= MSX_CART_ALL) return;
    Cartridge *c = &M->cart[n];
    MsxPersist d;
    sram_due[n] = 0;
    if (!sram_decl(n, &d) || !c->sram_dirty || !P.write_file || !P.create) return;
    if (d.fmt == PS_FLASH) { flash_file_save(n); return; }
    char p[FE_PATH_MAX];
    sram_path(n, p, sizeof p);
    if (P.create("0:\\SRAM", 1) || P.create(p, 0)) return;
    if (d.fmt == PS_PAC) {
        static uint8_t b[16 + 8192];
        memcpy(b, pac_hdr, 16); memcpy(b + 16, d.data, d.size);
        if (P.write_file(p, b, 16 + d.size)) return;
    } else if (P.write_file(p, d.data, d.size)) return;
    c->sram_dirty = 0;
}

/* 3.4.0: 기종 내장 펌웨어 SRAM·Matsushita SRAM = SRAM\<기종 이름>.fwsram (펌웨어 SRAM 크기 + 2048) */
static void fw_sram_path(char *out, int max) { copy_str(out, max, "0:\\SRAM\\"); str_append(out, max, machine_name); str_append(out, max, ".fwsram"); }
static void fw_sram_load(void)                          /* 무엇을 남길지는 msx_fw_persist (펌웨어 SRAM + Matsushita 2 KiB) */
{
    MsxFwPersist f; uint32_t total = msx_fw_persist(M, &f);
    if (!total || !P.read_file) return;
    static uint8_t b[32768 + 2048];
    char p[300]; uint32_t n = 0;
    fw_sram_path(p, sizeof p);
    if (P.read_file(p, b, sizeof b, &n) || n != total) return;
    memcpy(f.data[0], b, f.size[0]); memcpy(f.data[1], b + f.size[0], f.size[1]);
    *f.dirty[0] = *f.dirty[1] = 0;
    msx_reset_maps(M);
}
static void fw_sram_save(void)
{
    MsxFwPersist f; uint32_t total = msx_fw_persist(M, &f);
    if (!total || !(*f.dirty[0] || *f.dirty[1]) || !P.write_file || !P.create) return;
    static uint8_t b[32768 + 2048];
    memcpy(b, f.data[0], f.size[0]); memcpy(b + f.size[0], f.data[1], f.size[1]);
    char p[300];
    fw_sram_path(p, sizeof p);
    if (P.create("0:\\SRAM", 1) || P.create(p, 0) || P.write_file(p, b, total)) return;
    *f.dirty[0] = *f.dirty[1] = 0;
}
static void sram_save_all(void) { for (int n = 0; n < MSX_CART_ALL; n++) sram_save(n); fw_sram_save(); }

/* fe_frame에서: 마지막으로 바뀐 뒤 2초가 지나면 저장 */
static void sram_tick(uint32_t now)
{
    {   /* 3.4.0 펌웨어 SRAM: 바뀐 뒤 2초 */
        static uint32_t fw_due;
        if (!(M->fw.sram_dirty || M->fw.mat_dirty)) fw_due = 0;
        else { if (!fw_due) fw_due = now + 2000; if ((int32_t)(now - fw_due) >= 0) { fw_sram_save(); fw_due = 0; } }
    }
    for (int n = 0; n < MSX_CART_ALL; n++) {
        if (!M->cart[n].sram_dirty) { sram_due[n] = 0; continue; }
        if (!sram_due[n]) sram_due[n] = now + 2000;
        if ((int32_t)(now - sram_due[n]) >= 0) sram_save(n);
    }
}

/* ---------------- 끝낼 때 저장, 켤 때 이어서 — 2.11 ---------------- */
#define RESUME_PATH "0:\\States\\resume.sta"             /* 4.0.33까지: 기종 하나뿐 (예전 파일은 처음 한 번만 읽음) */
#define RESUME_DIR "0:\\States\\Resume"
static void resume_path(char *out, int max)               /* 4.0.34: 기종마다 따로 (States\Resume\<기종 이름>.sta) */
{
    copy_str(out, max, RESUME_DIR "\\"); str_append(out, max, machine_name); str_append(out, max, ".sta");
}
static void resume_save(void)
{
    if (!resume_on || !st_ready()) return;
    opts_save();                                        /* 속성 옵션도 기종별로 */
    if (run_state == RUN_STOPPED) return;
    if (P.create("0:\\States", 1) || P.create(RESUME_DIR, 1)) return;
    char p[300];
    resume_path(p, sizeof p);
    state_save_to(p);
}

static int resume_try(const char *path)
{
    static uint8_t tiny[8];
    uint32_t n = 0;
    if (P.read_file(path, tiny, sizeof tiny, &n) || n < 8) return 0;
    char err[160];
    st_resume_mode = 1;
    int ok = !state_load_from(path, err, sizeof err);
    st_resume_mode = 0;
    return ok;
}

static void resume_load(void)                             /* 켤 때: 전체 상태 저장이 켜져 있어야 */
{
    if (!global_save || !resume_on || !st_ready()) return;
    opts_load();
    char p[300];
    resume_path(p, sizeof p);
    resume_try(p);                                      /* 예전 한 파일(resume.sta)은 쓰지 않음: 안에 적힌 기종으로 바꿔 버려서, 마지막에 쓴 기종으로 못 켜짐 */
}

static void resume_enter(void)                            /* 기종을 바꾼 뒤: 그 기종이 지난번 하던 상태로 */
{
    if (!resume_on || !st_ready()) return;
    opts_load();
    char p[300];
    resume_path(p, sizeof p);
    resume_try(p);
}

/* ---------------- 다음 디스크 (LAlt+F9) — 2.11 ---------------- */
/* 같은 폴더에서 이름순으로 다음 디스크 이미지 (끝이면 처음으로). 여러 장짜리 게임의 디스크 바꾸기 */
static void disk_next(int d)
{
    if (!disk_size[d] || !disk_path[d][0] || !P.list_dir) return;
    if (is_zip(disk_path[d]) && dz_total[d] > 1) {      /* 4.0.34: zip 안에 디스크가 여러 장이면 그 안의 다음 장 (이름순, 끝이면 처음) */
        char zp[FE_PATH_MAX];
        copy_str(zp, sizeof zp, disk_path[d]);
        int nx = dz_idx[d] + 1;
        dz_req = nx;
        insert_disk_file(d, zp, zp, 0);
        dz_req = -1;
        if (disk_size[d]) {
            char t[96]; t[0] = 0;
            num_append(t, sizeof t, (uint32_t)dz_idx[d] + 1, 0, ' '); str_append(t, sizeof t, "/"); num_append(t, sizeof t, (uint32_t)dz_total[d], 0, ' ');
            str_append(t, sizeof t, " "); str_append(t, sizeof t, disk_name[d]);
            toast(d ? T(S_DISK_B) : T(S_DISK_A), t);
        }
        return;
    }
    char dir[FE_PATH_MAX];
    copy_str(dir, sizeof dir, disk_path[d]);
    int cut = (int)strlen(dir);
    while (cut > 0 && dir[cut - 1] != '\\' && dir[cut - 1] != '/') cut--;
    if (cut <= 0) return;
    const char *cur = disk_path[d] + cut;
    dir[cut > 0 ? cut - 1 : 0] = 0;
    int n = P.list_dir(dir, entries, DIR_MAX);
    int best = -1, first = -1;
    /* 번호로 찾기 — 이름 속 숫자 자리(오른쪽부터)를 다음 번호로 바꾼 파일이 있으면 그것 (Disk 1 of 6 → Disk 2 of 6, game1 → game2).
     * 같은 틀(숫자 자리만 다름)의 파일이 둘 이상이면 그 번호 차례로 돌고 끝이면 처음. "Disk 10" 이 "Disk 2" 앞에 오는 글자순 오류·[Fixed] 같은 변종 건너뜀 */
    {
        int cl = (int)strlen(cur), de = cl;
        while (de > 0 && cur[de - 1] != '.') de--;       /* 확장자 앞 */
        int base_end = de > 0 ? de - 1 : cl;
        #define ALNUM(c) (((c) >= '0' && (c) <= '9') || ((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
        for (int e = base_end; e > 0 && best < 0; ) {
            /* 오른쪽부터 토큰 찾기: 숫자 덩어리, 또는 홀로 떨어진 알파벳 한 글자 (Disk A / Side B / (C)) */
            int isnum = 0, b;
            while (e > 0) {
                char c = cur[e - 1];
                if (c >= '0' && c <= '9') { isnum = 1; break; }
                if (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && (e - 1 == 0 || !ALNUM(cur[e - 2])) && (e >= cl || !ALNUM(cur[e]))) break;
                e--;
            }
            if (e <= 0) break;
            b = e - 1;
            if (isnum) { b = e; while (b > 0 && cur[b - 1] >= '0' && cur[b - 1] <= '9') b--; }
            long curn = 0;
            if (isnum) for (int k = b; k < e; k++) curn = curn * 10 + (cur[k] - '0'); else curn = lower_c(cur[b]);
            long bestn = -1, firstn = -1; int cnt = 0, firsti = -1, besti = -1;
            for (int i = 0; i < n; i++) {
                const char *nm = entries[i].key[0] ? entries[i].key : entries[i].name;
                if (entries[i].is_dir || !(ext_is(nm, "dsk", "di1", "di2") || is_zip(nm))) continue;
                int nl = (int)strlen(nm);
                int ok = 1;
                for (int k = 0; k < b && ok; k++) if (k >= nl || lower_c(nm[k]) != lower_c(cur[k])) ok = 0;   /* 앞부분 같음 */
                if (!ok) continue;
                int q = b; long v = 0;
                if (isnum) {
                    int nd = 0;
                    while (q < nl && nm[q] >= '0' && nm[q] <= '9') { v = v * 10 + (nm[q] - '0'); q++; nd++; }
                    if (!nd) continue;
                } else {
                    if (q >= nl || !((nm[q] >= 'a' && nm[q] <= 'z') || (nm[q] >= 'A' && nm[q] <= 'Z')) || (q + 1 < nl && ALNUM(nm[q + 1]))) continue;
                    v = lower_c(nm[q]); q++;
                }
                if (nl - q != cl - e) continue;             /* 뒷부분 길이 같음 */
                for (int k = 0; k < cl - e && ok; k++) if (lower_c(nm[q + k]) != lower_c(cur[e + k])) ok = 0;
                if (!ok) continue;
                cnt++;
                if (firsti < 0 || v < firstn) { firstn = v; firsti = i; }
                if (v > curn && (besti < 0 || v < bestn)) { bestn = v; besti = i; }
            }
            if (cnt >= 2) { best = besti >= 0 ? besti : firsti; break; }
            e = b;
        }
        #undef ALNUM
    }
    if (best < 0)
    for (int i = 0; i < n; i++) {
        const char *nm = entries[i].key[0] ? entries[i].key : entries[i].name;
        if (entries[i].is_dir || !(ext_is(nm, "dsk", "di1", "di2") || is_zip(nm))) continue;   /* zip(디스크 한 장)도 다음 디스크 차례에 넣음 */
        if (first < 0 || name_cmp(nm, entries[first].key[0] ? entries[first].key : entries[first].name) < 0) first = i;
        if (name_cmp(nm, cur) > 0 && (best < 0 || name_cmp(nm, entries[best].key[0] ? entries[best].key : entries[best].name) < 0)) best = i;
    }
    if (best < 0) best = first;
    if (best < 0) return;
    const char *nm = entries[best].key[0] ? entries[best].key : entries[best].name;
    if (!strcmp(nm, cur)) { toast(T(S_DISK_NO_NEXT), 0); return; }
    char p[FE_PATH_MAX + FE_KEY_MAX];
    copy_str(p, sizeof p, dir); str_append(p, sizeof p, "\\"); str_append(p, sizeof p, nm);
    insert_disk_file(d, p, entries[best].name, 0);
    if (disk_size[d]) toast(d ? T(S_DISK_B) : T(S_DISK_A), entries[best].name);
}

/* ---------------- 최근 파일 (2.12) ---------------- */
/* kind 0 롬, 1 디스크, 2 테이프. 새로 넣은 것이 맨 위, 같은 경로는 위로 올림, 8개까지 */
static void recent_add(int kind, const char *path, const char *name)
{
    if (kind < 0 || kind > 2 || !path[0]) return;
    int at = RECENT_N - 1;
    for (int i = 0; i < RECENT_N; i++) if (!strcmp(rc_path[kind][i], path)) { at = i; break; }
    for (int i = at; i > 0; i--) { memcpy(rc_path[kind][i], rc_path[kind][i - 1], sizeof rc_path[0][0]); memcpy(rc_name[kind][i], rc_name[kind][i - 1], sizeof rc_name[0][0]); }
    copy_str(rc_path[kind][0], sizeof rc_path[0][0], path);
    copy_str(rc_name[kind][0], sizeof rc_name[0][0], name && name[0] ? name : path_base(path));
    settings_changed();
}

static void recent_save(char *b)
{
    static const char kc[3] = { 'R', 'D', 'T' };
    for (int k = 0; k < 3; k++)
        for (int i = 0; i < RECENT_N; i++) {
            if (!rc_path[k][i][0]) continue;
            char key[12] = "RecentR0", v[FE_PATH_MAX + 80];
            key[6] = kc[k]; key[7] = (char)('0' + i);
            copy_str(v, sizeof v, rc_path[k][i]); str_append(v, sizeof v, "|"); str_append(v, sizeof v, rc_name[k][i]);
            ini_put(b, key, v);
        }
}

static void recent_load(const char *k, const char *v)       /* k = "R0" 등 ("Recent" 뒤) */
{
    int kind = k[0] == 'R' ? 0 : k[0] == 'D' ? 1 : k[0] == 'T' ? 2 : -1, i = k[1] - '0';
    if (kind < 0 || i < 0 || i >= RECENT_N || k[2]) return;
    const char *bar = strchr(v, '|');
    int pl = bar ? (int)(bar - v) : (int)strlen(v);
    if (pl >= (int)sizeof rc_path[0][0]) return;
    memcpy(rc_path[kind][i], v, (size_t)pl); rc_path[kind][i][pl] = 0;
    copy_str(rc_name[kind][i], sizeof rc_name[0][0], bar ? bar + 1 : path_base(rc_path[kind][i]));
}

/* 메뉴 번호 k(0~3 카트리지 슬롯, 4·5 디스크 A·B, 6 테이프)의 i번째 최근 파일 열기 */
static void recent_open(int k, int i)
{
    int kind = recent_kind_of(k);
    char path[FE_PATH_MAX], name[64];
    copy_str(path, sizeof path, rc_path[kind][i]);
    copy_str(name, sizeof name, rc_name[kind][i]);
    if (!path[0]) return;
    if (kind == 0) {
        FeDirEntry x;
        memset(&x, 0, sizeof x);
        copy_str(x.name, sizeof x.name, name);
        cart_insert_file(k == 7 ? 4 : k, path, &x);   /* 3.6.7: 7 = 카트리지 5 */
    } else if (kind == 1) {
        int d = k - 4;
        insert_disk_file(d, path, name, 0);
        if (disk_size[d] && disk_reset_after_insert) power_on();
    } else {
        tape_flush();
        uint32_t ts = 0;
        if (tape_load_file(path, &ts, 0, 0, 0)) { if (!is_zip(path)) toast(T(S_LOAD_ERROR), name); return; }
        copy_str(tape_path, sizeof tape_path, path);
        copy_str(tape_name, sizeof tape_name, name);
        msx_tape_insert(M, tape_buf, ts, TAPE_MAX);
        tape_db_lookup(ts);
        recent_add(2, path, name);
    }
}

