/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_medit.inc - 머신 편집기 (대화 창 9): 자료·저장·실행 부분
 *
 * blueMSX 2.8.2 "Machine Configuration Editor"의 기능을 모두 담는다: 머신 이름(다른 머신 열기)·머신 종류,
 * 슬롯 0~3 기본/확장, 카트리지 1·2 자리, 메모리 표(슬롯·주소·형식·ROM 파일)와 매퍼 정보(파일·형식·슬롯·주소·크기),
 * 비디오 칩·램, Z80·R800 주파수, 드라이브 수, CMOS·배터리. 파일을 고르면 롬 DB(SHA-1)와 크기로 형식·주소를 정한다.
 * 화면과 키는 fe_medit_ui.inc (2.17 키보드 카드 디자인: 탭 1~4·Tab/Shift+Tab, ↑/↓ 줄, ←/→ 값, Enter 값 목록,
 *     F2 저장, F3 머신, F4 다른 이름으로, F5 실행, Esc 닫기).
 * 다시 시작: 저장(F2)만으로는 다시 켜지 않는다. 지금 켜진 머신을 고쳤으면 닫을 때(Esc) "다시 시작할까요?"를 묻고
 *     Y면 다시 시작, N·Esc면 편집기로 돌아가고, C면 적용하지 않고 닫는다.
 * fe.c 안에 포함되어 그 정적 함수·변수를 쓴다. 자료 모형(config.ini 읽기·쓰기·검사·형식 표)은 medit.c */

static MeMachine me;
static char me_src[128];                /* 편집 중인 머신 폴더 이름 */
static int me_tab, me_dirty, me_pending;    /* me_pending: 켜진 머신에 저장했지만 아직 다시 켜지 않음 */
static int me_focus = 1, me_dfocus, me_pfocus;
static int me_lsel, me_ltop;            /* 메모리 목록 */
static int me_det;                      /* 매퍼 정보 창 열림 */
enum { ME_POP_NONE, ME_POP_NAME, ME_POP_PROBLEMS, ME_POP_CONFIRM, ME_POP_BROWSE };
static int me_pop;
enum { ME_Q_REBOOT = 1, ME_Q_SAVE, ME_Q_SWITCH };
static int me_q;
static char me_switch_to[96];
static int me_drop, me_dsel, me_dtop;   /* 펼친 콤보 (컨트롤 번호, 0 = 없음) */
static char me_name_buf[96];
static char me_msgs[12][ME_MSG];
static int me_nmsg;
static uint8_t me_cfg[32768];

static const char *const me_vdps[] = { "TMS99x8A", "TMS9929A", "V9938", "V9958" };
static const int me_vrams[] = { 16, 64, 128, 192 };
static const uint32_t me_freqs[] = { 21477270, 10738635, 7159090, 5369317, 3579545, 2386363, 1431818, 859090, 429545, 214772 };
static const char *const me_boards[] = { "MSX", "MSX-S3527", "MSX-S1985", "MSX-T9769B", "MSX-T9769C", "MSX-ForteII" };
static const char *const me_board_names[] = { "MSX", "MSX S-3527", "MSX S-1985", "MSX T-9769B", "MSX T-9769C", "MSX Forte II" };

static int me_marked(const MeMachine *m) { return strstr_ci(m->sec_extra[0], "EditedBy=X86MSX"); }
static void hex4(char *out, int max, uint32_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    char t[5] = { hx[(v >> 12) & 15], hx[(v >> 8) & 15], hx[(v >> 4) & 15], hx[v & 15], 0 };
    str_append(out, max, t);
}

/* ---- 슬롯 목록 (슬롯 배치에 따라: "슬롯 0", "슬롯 2-0" …) ---- */
static int me_slot_opts(int *ps, int *ss)
{
    int n = 0;
    for (int p = 0; p < 4; p++) {
        if (me.subslotted[p]) for (int s = 0; s < 4; s++) { ps[n] = p; ss[n] = s; n++; }
        else { ps[n] = p; ss[n] = 0; n++; }
    }
    return n;
}
static void me_slot_text(int ps, int ss, char *out, int max, int prefix)
{
    out[0] = 0;
    if (prefix) { copy_str(out, max, T(S_ME_SLOTN)); str_append(out, max, " "); }
    num_append(out, max, (uint32_t)ps, 0, ' ');
    if (me.subslotted[ps & 3]) { str_append(out, max, prefix ? "-" : " - "); num_append(out, max, (uint32_t)ss, 0, ' '); }
}

/* ---- 표 한 줄 (blueMSX와 같은 모양) ---- */
static void me_range_text(int s, int w, char *out, int max)
{
    out[0] = 0;
    hex4(out, max, (uint32_t)s * 0x2000u); str_append(out, max, "-");
    hex4(out, max, (uint32_t)(s + w) * 0x2000u - 1u);
}
static void me_addr_text(int type, int start, int count, char *out, int max)
{
    out[0] = 0;
    int w = me_window(type, count);
    if (!w) return;
    me_range_text(w == 8 ? 0 : start, w, out, max);
}
static void me_size_text(int pages, char *out, int max)
{
    out[0] = 0;
    uint32_t kb = (uint32_t)pages * 8u;
    if (kb >= 1024) { num_append(out, max, kb / 1024, 0, ' '); str_append(out, max, " MB"); }
    else { num_append(out, max, kb, 0, ' '); str_append(out, max, " kB"); }
}
static void me_type_text(int type, int count, char *out, int max)
{
    const MeTypeInfo *t = me_type_info(type);
    out[0] = 0;
    if (t && count && (t->kind == ME_K_MAPRAM || t->kind == ME_K_RAM)) { me_size_text(count, out, max); str_append(out, max, " "); }
    if (t) str_append(out, max, t->name);
    else { str_append(out, max, "#"); num_append(out, max, (uint32_t)type, 0, ' '); }
}
static void me_row_text(const MeSlot *q, char *slot, char *addr, char *type, char *file, int max)
{
    int w = me_window(q->type, q->count);
    slot[0] = 0;
    if (w) me_slot_text(q->ps, q->ss, slot, max, 0);
    me_addr_text(q->type, q->start, q->count, addr, max);
    me_type_text(q->type, q->count, type, max);
    copy_str(file, max, q->file[0] ? path_base(q->file) : "");
}

/* 표 순서: 메모리 없는 장치 먼저, 그다음 슬롯·서브슬롯·주소 순 (blueMSX와 같음) */
static int me_row_key(const MeSlot *q)
{
    int w = me_window(q->type, q->count);
    if (!w) return -1;
    return (q->ps * 4 + q->ss) * 16 + (w == 8 ? 0 : q->start);
}
static void me_sort(void)
{
    for (int i = 1; i < me.nslot; i++) {
        MeSlot t = me.slot[i];
        int k = me_row_key(&t), j = i;
        while (j > 0 && me_row_key(&me.slot[j - 1]) > k) { me.slot[j] = me.slot[j - 1]; j--; }
        me.slot[j] = t;
    }
}

/* ---- 열기 ---- */
static int me_load(const char *name)
{
    char path[256] = "0:\\Machines\\";
    str_append(path, sizeof path, name);
    str_append(path, sizeof path, "\\config.ini");
    uint32_t n = 0;
    if (!P.read_file || P.read_file(path, me_cfg, sizeof me_cfg - 1, &n) || !n) { toast(T(S_ME_OPEN_FAIL), name); return -1; }
    if (n > sizeof me_cfg - 1) n = sizeof me_cfg - 1;
    me_parse((const char *)me_cfg, n, &me);
    copy_str(me_src, sizeof me_src, name);
    copy_str(me.name, sizeof me.name, name);                    /* 이름 = 폴더 이름 (저장은 이 머신에 덮어씀) */
    me_sort();
    me_dirty = 0; me_pending = 0; me_det = 0; me_pop = 0; me_drop = 0;
    me_lsel = 0; me_ltop = 0;
    return 0;
}
static void open_medit_name(const char *name)
{
    if (me_load(name)) return;
    me_tab = 0; me_focus = 1;
    open_dialog(9);
}

static int me_has_type(int type) { for (int i = 0; i < me.nslot; i++) if (me.slot[i].type == type) return 1; return 0; }
static void me_freq_text(uint32_t hz, char *out, int max)
{
    out[0] = 0;
    num_append(out, max, hz / 1000000u, 0, ' '); str_append(out, max, ".");
    num_append(out, max, (hz / 1000u) % 1000u, 3, '0'); str_append(out, max, " MHz");
}

/* ================================================================== */
/* 매퍼 정보 (메모리 탭의 추가·편집)                                    */
/* ================================================================== */
static MeSlot me_ed;
static int me_ed_idx;
static uint32_t me_ed_bytes;            /* 고른 파일 크기 (0 = 모름) */

static int me_ed_fixed_addr(void) { int w = me_window(me_ed.type, me_ed.count); return w == 0 || w == 8; }
static int me_ed_size_sel(void)
{
    const MeTypeInfo *t = me_type_info(me_ed.type);
    return t && (t->kind == ME_K_RAM || t->kind == ME_K_MAPRAM);
}
/* 형식에 맞게 블록 수·시작 블록 고침 */
static void me_ed_fix(void)
{
    const MeTypeInfo *t = me_type_info(me_ed.type);
    int kind = t ? t->kind : ME_K_ROM;
    int sizes[8], ns = me_size_options(me_ed.type, sizes, 8), ok = 0;
    switch (kind) {
    case ME_K_RAM: case ME_K_MAPRAM:
        for (int i = 0; i < ns; i++) if (sizes[i] == me_ed.count) ok = 1;
        if (!ok) me_ed.count = kind == ME_K_RAM ? 8 : 64;          /* 64 kB / 512 kB */
        break;
    case ME_K_RAMFIX: case ME_K_WIN: case ME_K_NOFILE: me_ed.count = t->win; break;
    case ME_K_FULL: me_ed.count = 8; break;
    case ME_K_DEV: case ME_K_DEVFILE: me_ed.count = 0; break;
    default:
        if (me_ed_bytes) { int p = (int)((me_ed_bytes + 0x1FFF) / 0x2000); me_ed.count = p < 1 ? 1 : p > 8 ? 8 : p; }
        else if (me_ed.count < 1 || me_ed.count > 8) me_ed.count = 2;
        break;
    }
    if (kind == ME_K_DEV || kind == ME_K_NOFILE || kind == ME_K_RAM || kind == ME_K_RAMFIX || kind == ME_K_MAPRAM) {
        if (me_ed.type != 120) me_ed.file[0] = 0;                   /* 파일을 쓰지 않는 형식 */
        if (kind != ME_K_ROM) me_ed_bytes = 0;
    }
    int w = me_window(me_ed.type, me_ed.count);
    if (w == 0 || w == 8) me_ed.start = 0;
    else {
        if (me_ed.start + w > 8) me_ed.start = 8 - w;
        if (me_ed.start < 0) me_ed.start = 0;
    }
    if (!me.subslotted[me_ed.ps & 3]) me_ed.ss = 0;
}

static void me_ed_open(int idx)
{
    me_ed_idx = idx;
    me_ed_bytes = 0;
    if (idx >= 0) {
        me_ed = me.slot[idx];
        if (me_ed.file[0] && P.list_dir) {                         /* 파일 크기 (크기 칸 표시용) */
            char dir[256] = "0:\\", nm[96];
            int k = 3;
            for (const char *s = me_ed.file; *s && k < (int)sizeof dir - 1; s++) dir[k++] = *s == '/' ? '\\' : *s;
            dir[k] = 0;
            copy_str(nm, sizeof nm, path_base(dir));
            char *sl = dir + k;
            while (sl > dir + 3 && sl[-1] != '\\') sl--;
            if (sl > dir + 3) sl[-1] = 0; else dir[3] = 0;
            int n = P.list_dir(dir, entries, DIR_MAX);
            for (int i = 0; i < n; i++) if (!strcmp(entries[i].key[0] ? entries[i].key : entries[i].name, nm)) me_ed_bytes = entries[i].size;
        }
    } else {                                                        /* 새 줄: blueMSX처럼 Normal ROM, 슬롯 0, 0000h */
        memset(&me_ed, 0, sizeof me_ed);
        me_ed.type = 20; me_ed.count = 2;
    }
    me_dfocus = 100;
    me_det = 1;
}

/* 파일을 고른 뒤: 크기와 롬 데이터베이스로 형식·주소를 정함 (blueMSX와 같은 동작) */
static void me_ed_set_file(const char *rel, uint32_t size)
{
    copy_str(me_ed.file, sizeof me_ed.file, rel);
    me_ed_bytes = size;
    int dbt = -1, dbstart = -1, hdr_ab = 0, hdr_bios = 0;
    if (P.scratch && P.scratch_size >= (1u << 20) && P.read_file) {
        char a[300] = "0:\\";
        int k = 3;
        for (const char *s = rel; *s && k < (int)sizeof a - 1; s++) a[k++] = *s == '/' ? '\\' : *s;
        a[k] = 0;
        uint32_t n = 0;
        if (!P.read_file(a, P.scratch, 1u << 20, &n) && n) {
            if (n > (1u << 20)) n = 1u << 20;
            me_ed_bytes = n;
            hdr_ab = P.scratch[0] == 'A' && P.scratch[1] == 'B';
            hdr_bios = P.scratch[0] == 0xF3 && P.scratch[1] == 0xC3;
            romdb_load();
            if (romdb_len || sysdb_len) {
                char hex[41];
                sha1_hex(P.scratch, n, hex);
                RomDbInfo ri;
                if ((sysdb_len && romdb_find(sysdb_buf, sysdb_len, hex, &ri)) || (romdb_len && romdb_find(romdb_buf, romdb_len, hex, &ri))) {
                    dbt = me_db_type(ri.type);
                    dbstart = ri.start;
                }
            }
        }
    }
    const MeTypeInfo *cur = me_type_info(me_ed.type);
    if (dbt >= 0) me_ed.type = dbt;
    else if (!cur || cur->kind == ME_K_RAM || cur->kind == ME_K_RAMFIX || cur->kind == ME_K_MAPRAM || cur->kind == ME_K_DEV || cur->kind == ME_K_NOFILE)
        me_ed.type = 20;                                            /* 모르는 롬 = Normal ROM */
    me_ed_fix();
    int w = me_window(me_ed.type, me_ed.count);
    if (w && w < 8) {                                               /* 시작 주소 */
        int st;
        if (dbstart >= 0) st = dbstart / 0x2000;
        else if (me_ed.type == 42) st = 2;
        else if (me_ed.type == 86) st = 6;
        else if (me_ed.type == 39) st = 4;
        else if (me_type_info(me_ed.type) && me_type_info(me_ed.type)->kind == ME_K_WIN) st = 2;
        else st = hdr_ab && !hdr_bios ? 2 : 0;
        me_ed.start = st;
        me_ed_fix();
    }
}

static void me_ed_ok(void)
{
    if (me_type_rom(me_ed.type) && !me_ed.file[0]) { toast(T(S_ME_NEEDFILE), me_type_name(me_ed.type)); me_dfocus = 100; return; }
    if (me_ed_idx >= 0) me.slot[me_ed_idx] = me_ed;
    else if (me.nslot < ME_SLOTS) me.slot[me.nslot++] = me_ed;
    me_sort();
    for (int i = 0; i < me.nslot; i++) if (!memcmp(&me.slot[i], &me_ed, sizeof me_ed)) me_lsel = i;
    me_dirty = 1;
    me_det = 0;
}

/* ---- 파일 고르기 ("ROM 파일 열기" 창: USB 전체를 폴더로 돌아다님) ---- */
static char me_bdir[256];
static int me_bsel, me_btop, me_bn;
static int me_bidx[DIR_MAX];
static void me_browse_load(void)
{
    int n = P.list_dir ? P.list_dir(me_bdir, entries, DIR_MAX) : -1;
    me_bn = 0;
    if (strlen(me_bdir) > 3) me_bidx[me_bn++] = -1;                 /* .. */
    for (int pass = 0; pass < 2; pass++)                            /* 폴더 먼저, 이름 순 */
        for (int i = 0; i < n; i++) {
            if ((entries[i].is_dir != 0) != (pass == 0)) continue;
            int j = me_bn++;
            while (j > 0 && me_bidx[j - 1] >= 0 && (entries[me_bidx[j - 1]].is_dir != 0) == (pass == 0) && name_cmp(entries[me_bidx[j - 1]].name, entries[i].name) > 0) { me_bidx[j] = me_bidx[j - 1]; j--; }
            me_bidx[j] = i;
        }
    me_bsel = me_bn > 1 && me_bidx[0] < 0 ? 1 : 0;
    me_btop = 0;
}
static void me_browse_open(void)
{
    copy_str(me_bdir, sizeof me_bdir, "0:\\Machines\\");
    str_append(me_bdir, sizeof me_bdir, me_src);
    if (me_ed.file[0]) {                                            /* 지금 파일이 있는 폴더 */
        char d[256] = "0:\\";
        int k = 3;
        for (const char *s = me_ed.file; *s && k < (int)sizeof d - 1; s++) d[k++] = *s == '/' ? '\\' : *s;
        d[k] = 0;
        char *sl = d + k;
        while (sl > d + 3 && sl[-1] != '\\') sl--;
        if (sl > d + 3) { sl[-1] = 0; copy_str(me_bdir, sizeof me_bdir, d); }
    }
    me_browse_load();
    if (me_ed.file[0])
        for (int i = 0; i < me_bn; i++) {
            int e = me_bidx[i];
            if (e >= 0 && !strcmp(entries[e].key[0] ? entries[e].key : entries[e].name, path_base(me_ed.file))) me_bsel = i;
        }
    me_pop = ME_POP_BROWSE;
    me_pfocus = 150;
}
static void me_browse_up(void)
{
    int k = (int)strlen(me_bdir);
    char was[96];
    copy_str(was, sizeof was, path_base(me_bdir));
    while (k > 3 && me_bdir[k - 1] != '\\') k--;
    me_bdir[k > 3 ? k - 1 : 3] = 0;
    me_browse_load();
    for (int i = 0; i < me_bn; i++) {                               /* 나온 폴더에 커서 */
        int e = me_bidx[i];
        if (e >= 0 && !strcmp(entries[e].key[0] ? entries[e].key : entries[e].name, was)) me_bsel = i;
    }
}
static void me_browse_enter(void)
{
    if (me_bsel < 0 || me_bsel >= me_bn) return;
    int e = me_bidx[me_bsel];
    if (e < 0) { me_browse_up(); return; }
    const char *nm = entries[e].key[0] ? entries[e].key : entries[e].name;
    if (entries[e].is_dir) {
        if (strlen(me_bdir) > 3) str_append(me_bdir, sizeof me_bdir, "\\");
        str_append(me_bdir, sizeof me_bdir, nm);
        me_browse_load();
        return;
    }
    char rel[200] = "";                                             /* USB 최상위 기준 (blueMSX와 같은 "Machines\…" 모양) */
    if (strlen(me_bdir) > 3) { copy_str(rel, sizeof rel, me_bdir + 3); str_append(rel, sizeof rel, "\\"); }
    str_append(rel, sizeof rel, nm);
    uint32_t size = entries[e].size;
    me_pop = ME_POP_NONE;
    me_ed_set_file(rel, size);
    me_dfocus = 102;
}

/* ================================================================== */
/* 저장                                                                */
/* ================================================================== */
static int me_exists(const char *p)
{
    static uint8_t tiny[16];
    char a[300] = "0:\\";
    int k = 3;
    for (const char *s = p; *s && k < (int)sizeof a - 1; s++) a[k++] = *s == '/' ? '\\' : *s;
    a[k] = 0;
    uint32_t n = 0;
    if (!P.read_file(a, tiny, sizeof tiny, &n) && n) return 1;
    char b[300] = "0:\\Machines\\";                                 /* 머신 폴더에 같은 이름 (machine_load와 같은 규칙) */
    str_append(b, sizeof b, me_src); str_append(b, sizeof b, "\\"); str_append(b, sizeof b, path_base(p));
    return !P.read_file(b, tiny, sizeof tiny, &n) && n;
}

static int me_is_running(void) { return !strcmp(me.name, machine_name); }

/* 저장만 한다 (다시 켜지 않음). 켜진 머신에 저장했으면 me_pending = 1 → 닫을 때 다시 시작을 묻는다 */
static int me_save(void)
{
    static char raw[16][ME_MSG];
    int n = me_check(&me, P.read_file ? me_exists : 0, raw, 16);
    me_nmsg = 0;
    if (strncmp(me.name, "MSX", 3)) copy_str(me_msgs[me_nmsg++], ME_MSG, T(S_ME_E_NAMEMSX));
    int same = !strcmp(me.name, me_src);
    if (!same) {                                                    /* 다른 이름: 이미 있는 머신이면 막음 */
        char path[256] = "0:\\Machines\\";
        str_append(path, sizeof path, me.name); str_append(path, sizeof path, "\\config.ini");
        uint32_t len = 0;
        if (me.name[0] && P.read_file && !P.read_file(path, me_cfg, sizeof me_cfg - 1, &len) && len)
            copy_str(me_msgs[me_nmsg++], ME_MSG, T(S_ME_E_EXISTS));
    }
    for (int i = 0; i < n && i < 16 && me_nmsg < 12; i++) {
        const char *r = raw[i];
        char *o = me_msgs[me_nmsg];
        o[0] = 0;
        static const struct { const char *k; StrId s; int row; } map[] = {
            { "NAMECHAR", S_ME_E_NAMECHAR, 0 }, { "NAME", S_ME_E_NAME, 0 }, { "RANGE ", S_ME_E_RANGE, 1 }, { "NOTSUB ", S_ME_E_NOTSUB, 1 },
            { "OVERLAP ", S_ME_E_OVERLAP, 1 }, { "NOFILE ", S_ME_E_NOFILE, 1 }, { "MISSING ", S_ME_E_MISSING, 1 }, { "NORAM", S_ME_E_NORAM, 0 },
            { "VDP", S_ME_E_VDP, 0 }, { "EXTSLOT", S_ME_E_EXT, 0 },
        };
        for (unsigned k = 0; k < sizeof map / sizeof map[0]; k++) {
            int kl = (int)strlen(map[k].k);
            if (strncmp(r, map[k].k, (size_t)kl)) continue;
            if (map[k].row) { copy_str(o, ME_MSG, T(S_ME_ROW)); str_append(o, ME_MSG, " "); str_append(o, ME_MSG, r + kl); }
            str_append(o, ME_MSG, T(map[k].s));
            break;
        }
        if (!o[0]) copy_str(o, ME_MSG, r);
        me_nmsg++;
    }
    if (me_nmsg) { me_pop = ME_POP_PROBLEMS; me_pfocus = 140; return -1; }
    if (!me_marked(&me)) {
        int l = (int)strlen(me.sec_extra[0]);
        if (l + 20 < (int)sizeof me.sec_extra[0]) { memcpy(me.sec_extra[0] + l, "EditedBy=X86MSX\r\n", 18); }
    }
    char dir[256] = "0:\\Machines\\", file[280], bak[290];
    str_append(dir, sizeof dir, me.name);
    copy_str(file, sizeof file, dir); str_append(file, sizeof file, "\\config.ini");
    copy_str(bak, sizeof bak, file); str_append(bak, sizeof bak, ".bak");
    int backed = 0;
    if (same && P.read_file && P.write_file && P.create) {          /* 처음 덮어쓸 때 원래 설정을 config.ini.bak으로 */
        uint32_t bl = 0, ol = 0;
        static uint8_t tiny[16];
        if (P.read_file(bak, tiny, sizeof tiny, &bl) || !bl) {
            if (!P.read_file(file, me_cfg, sizeof me_cfg, &ol) && ol && ol <= sizeof me_cfg && !P.create(bak, 0) && !P.write_file(bak, me_cfg, ol)) backed = 1;
        }
    }
    int len = me_write(&me, (char *)me_cfg, sizeof me_cfg);
    if (len < 0 || !P.create || !P.write_file || P.create(dir, 1) || P.create(file, 0) || P.write_file(file, me_cfg, (uint32_t)len)) {
        toast(T(S_ME_SAVE_FAIL), me.name);
        return -1;
    }
    copy_str(me_src, sizeof me_src, me.name);                       /* 다음 저장은 이 폴더 기준 */
    me_dirty = 0;
    if (me_is_running()) me_pending = 1;
    mc_st[mc_find(me.name)] = 0;                                    /* 머신 목록의 검사 결과 다시 */
    toast(T(S_ME_SAVED), backed ? T(S_ME_BACKUP) : me.name);
    return 0;
}

/* 고친 설정으로 그 머신을 다시 켬 (실행 단추, 닫을 때 Y) */
static void me_run(void)
{
    if (me_dirty && me_save()) return;
    char err[160];
    if (switch_machine(me.name, err, sizeof err)) { toast(T(S_MACHINE_FAIL), err); return; }
    me_pending = 0;
    me_det = 0; me_pop = 0; me_drop = 0;
    dialog_done();
    toast(T(S_ME_APPLIED), me.name);
}

static void me_close_now(void) { me_det = 0; me_pop = 0; me_drop = 0; dialog_back(); }

/* 닫기 (Esc·닫기 단추): 바뀐 것이 있을 때만 묻는다 */
static void me_try_close(void)
{
    if (me_is_running() && (me_dirty || me_pending)) { me_q = ME_Q_REBOOT; me_pop = ME_POP_CONFIRM; me_pfocus = 130; return; }
    if (me_dirty) { me_q = ME_Q_SAVE; me_pop = ME_POP_CONFIRM; me_pfocus = 130; return; }
    me_close_now();
}

/* US 배열 글자 (이름 입력) */
static char key_char(int key, int shift)
{
    static const char sh_dig[] = "!@#$%^&*()";
    if (key >= 0x02 && key <= 0x0B) return shift ? sh_dig[key - 2] : sc_ascii[key];
    if (key < 0x40 && sc_ascii[key] >= 'a') return shift ? (char)(sc_ascii[key] - 32) : sc_ascii[key];
    switch (key) {
    case 0x39: return ' ';
    case 0x0C: return shift ? '_' : '-';
    case 0x0D: return shift ? '+' : '=';
    case 0x1A: return shift ? '{' : '[';
    case 0x1B: return shift ? '}' : ']';
    case 0x27: return shift ? ':' : ';';
    case 0x28: return shift ? '"' : '\'';
    case 0x33: return shift ? '<' : ',';
    case 0x34: return shift ? '>' : '.';
    case 0x35: return shift ? '?' : '/';
    case 0x29: return shift ? '~' : '`';
    case 0x2B: return shift ? '|' : '\\';
    }
    return 0;
}

#include "fe_medit_ui.inc"
