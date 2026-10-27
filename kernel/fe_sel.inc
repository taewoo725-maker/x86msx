/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fe_sel.inc - 선택형 설정 표 (5.2.121, 구조 개편 6단계)
 *
 * "여러 값 중 하나 고르기" 설정(모니터 종류·가로 비율·마우스 포인터/포트/잠금·조이스틱 포트)을 표 한 줄로 모은다.
 * 예전에는 한 설정이 속성 창 줄(props_items) · 풀다운 목록(dd_gen) · 풀다운 고르기(dd_apply) · ←/→ 넘기기(props_change) 네 군데에 흩어져 있었다.
 * 새 선택형 설정 = 이 표에 FESEL(...) 한 줄 (+ 이름·값 글자). 속성 창·풀다운·넘기기 코드는 안 고친다.
 *
 *   id    = 속성 항목 번호 (props_items 의 줄 번호와 같음)       var  = 값을 담은 변수 (0 ~ n-1)
 *   name  = 줄 이름 글자                                         n, lab[] = 값 개수와 값 글자 (순서 = 풀다운 순서 = 넘기는 순서)
 *   after = 값이 바뀐 뒤 할 일 (NULL = 없음; settings_changed 는 늘 부름)
 * 화면 소스(160)·MIDI·수직 동기·슬롯 확장은 값 목록이 기종·장치에 따라 달라 아직 표 밖 (6단계 2차).
 */
static void sel_after_mon(void) { recompute_palette(); if (P.set_palette) P.set_palette(); full_redraw(); }
static void sel_after_asp(void) { layout(); force_redraw = 1; margin_bd = -1; }

static const char *sel_lab_asp(int i) { return i == 0 ? T(S_ASPECT_SQ) : T(S_ASPECT_FULL); }   /* 4:3 · 전체화면 두 가지만 (저장값 0 = 4:3, 2 = 전체화면; 옛 TV 비율 1 은 4:3 으로 봄) */
static int sel_get_asp(void) { return aspect_mode == 2; }
static void sel_set_asp(int v) { aspect_mode = v ? 2 : 0; sel_after_asp(); settings_changed(); }
static void set_expand(int want);
static const char *sel_lab_vsync(int i) { return i == 0 ? T(S_VSYNC_AUTO) : i == 1 ? "60 Hz" : "50 Hz"; }
static const char *sel_lab_vsrc(int i) { return i == 0 ? T(S_VSRC_AUTO) : i <= VSRC_N ? msx_vsrc[i - 1].name : i == VSRC_N + 1 ? T(S_VSRC_BLEND) : T(S_VSRC_SIDE); }   /* 겹치기·나란히 (V9990 이 꽂혀 있을 때) */
static int sel_n_vsrc(void) { return VSRC_N + 1 + (M && M->v9 ? 2 : 0); }   /* 화면 소스: 자동 + src/vsrc.h 의 소스들 */
static int sel_get_vsrc(void) { return vsrc_pref + 1; }
static void sel_set_vsrc(int v) { vsrc_pref = v - 1; force_redraw = 1; margin_bd = -1; settings_changed(); }
static int sel_get_vsync(void) { return M->vsync_force % 3; }
static void sel_set_vsync(int v) { M->vsync_force = v; settings_changed(); }

/* var = 값 변수. 변수가 기종마다 다른 곳(M->…)에 있으면 get 으로 읽음. set = 값을 바꾸는 쪽이 따로 있을 때(부작용·실패 처리를 스스로 함, settings_changed 포함).
 * label = 값 글자를 StrId 가 아닌 글로 줄 때 (lab[] 대신). */
typedef struct FeSel {
    uint16_t id; int *var; int (*get)(void); void (*set)(int); StrId name; uint8_t n; StrId lab[4]; const char *(*label)(int); void (*after)(void); int (*count)(void);
} FeSel;
#define FESEL(i, v, nm, cnt, af, ...) { .id = i, .var = v, .name = nm, .n = cnt, .lab = { __VA_ARGS__ }, .after = af }
static const FeSel fe_sel[] = {
    FESEL(40,  &monitor_kind,  S_MONITOR,    4, sel_after_mon, S_MON_COLOR, S_MON_BW, S_MON_GREEN, S_MON_AMBER),
    { .id = 57, .get = sel_get_asp, .set = sel_set_asp, .name = S_ASPECT, .n = 2, .label = sel_lab_asp },                    /* 가로 비율: 4:3 / 전체화면 */
    FESEL(98,  &mouse_ptr,     S_MOUSE_PTR,  4, 0,             S_MOUSE_PTR_AUTO, S_MOUSE_PTR_MENU, S_MOUSE_PTR_ALWAYS, S_MOUSE_PTR_HIDE),
    FESEL(90,  &mouse_port,    S_MOUSE_PORT, 3, 0,             S_JOY_NONE, S_PORT1, S_PORT2),
    FESEL(97,  &mouse_lock,    S_MOUSE_LOCK, 2, 0,             S_MOUSE_LOCK_AUTO, S_MOUSE_LOCK_MANUAL),
    FESEL(20,  &joy_mode[0],   S_PORT1,      4, 0,             S_JOY_NONE, S_JOY_KEYS, S_JOY_PAD1, S_JOY_PAD2),
    FESEL(21,  &joy_mode[1],   S_PORT2,      4, 0,             S_JOY_NONE, S_JOY_KEYS, S_JOY_PAD1, S_JOY_PAD2),
    { .id = 8,  .var = &slot_expand, .set = set_expand, .name = S_SLOT_EXPANDER, .n = 3, .lab = { S_NONE, S_CART1, S_CART2 } },      /* 카트리지 슬롯 확장 (꽂힌 카트를 빼고 다시 구성) */
    { .id = 160, .get = sel_get_vsrc, .set = sel_set_vsrc, .name = S_VSRC, .n = VSRC_N + 3, .count = sel_n_vsrc, .label = sel_lab_vsrc },               /* 화면 소스 (vsrc_pref, -1 = 자동). 줄은 V9990 이 꽂혀야 켜짐 */
    { .id = 50, .get = sel_get_vsync, .set = sel_set_vsync, .name = S_VSYNC, .n = 3, .label = sel_lab_vsync },                    /* 주사 주파수 (M->vsync_force) */
};
static const FeSel *sel_find(int id)
{
    for (unsigned i = 0; i < sizeof fe_sel / sizeof fe_sel[0]; i++) if (fe_sel[i].id == id) return &fe_sel[i];
    return 0;
}
static const char *sel_label(const FeSel *s, int i) { return s->label ? s->label(i) : T(s->lab[i]); }
static int sel_n(const FeSel *s) { return s->count ? s->count() : s->n; }
static int sel_cur(const FeSel *s)
{
    int v = s->get ? s->get() : *s->var, n = sel_n(s);
    return n == 2 ? v != 0 : v >= n ? 0 : (int)((unsigned)v % n);
}
static void sel_set(const FeSel *s, int v)                      /* v = 0 ~ n-1 */
{
    if (s->set) { s->set(v); return; }
    *s->var = v;
    if (s->after) s->after();
    settings_changed();
}
static int sel_change(int id, int dir)                          /* ←/→ (dir < 0 앞, 아니면 뒤로 한 칸). 표에 있으면 1 */
{
    const FeSel *s = sel_find(id);
    if (!s) return 0;
    sel_set(s, (sel_cur(s) + (dir < 0 ? sel_n(s) - 1 : 1)) % sel_n(s));
    return 1;
}

/*
 * 막대형 설정 표 (5.2.124): 스캔라인 · Z80 속도 · R800 속도 · 속도 배수. 값 막대의 글자·자리(item), ←/→ 와 Enter(change), 마우스로 누르기·끌기(slide)를
 * 한 줄로 모음. 예전에는 props_items · props_change · hc_slide 세 곳에 흩어져 있었음. 새 막대 = 표 한 줄 + 세 함수.
 * (밝기·대비·채도·감마·효과·마우스 감도처럼 값을 바로 바꾸는 단순 막대는 fe_opts 표 (5.2.38) 가 맡음)
 */
static void sl_scan_item(char *v, int n, int *pos)
{
    v[0] = 0;
    if (scanlines) { num_append(v, (size_t)n, (uint32_t)scanlines, 0, ' '); str_append(v, (size_t)n, "%"); } else copy_str(v, (size_t)n, T(S_OFF));
    *pos = scanlines * 5;
}
static void sl_scan_change(int dir)                             /* ←/→ 10 %씩, Enter 끔 ↔ 마지막 세기 */
{
    if (dir) { scanlines += dir * 10; if (scanlines < 0) scanlines = 0; if (scanlines > 100) scanlines = 100; }
    else scanlines = scanlines ? 0 : scan_last;
    if (scanlines) scan_last = scanlines;
    full_redraw(); settings_changed();
}
static void sl_scan_slide(uint32_t f)
{
    int raw = (int)((f * 100u + 500) / 1000), v = (raw + 5) / 10 * 10; if (v > 100) v = 100;
    scanlines = v; if (v) scan_last = v; full_redraw();
}
static void sl_z80_item(char *tv, int n, int *pos)             /* 최대 속도·배수가 Z-80·R800 두 게이지에 함께 보임 (turbo R가 아니면 Z-80만) */
{
    tv[0] = 0;
    if (speed_max) { if (msx_dev(M, MDEV_S1990)) str_append(tv, (size_t)n, "Z80 "); str_append(tv, (size_t)n, T(S_OV_XMAX)); }
    else {
        uint32_t khz = 3579u * (uint32_t)speed / 100;
        if (msx_dev(M, MDEV_S1990)) str_append(tv, (size_t)n, "Z80 ");
        num_append(tv, (size_t)n, khz / 1000, 0, ' '); str_append(tv, (size_t)n, ".");
        num_append(tv, (size_t)n, khz % 1000, 3, '0'); str_append(tv, (size_t)n, " MHz (");
        num_append(tv, (size_t)n, (uint32_t)speed, 0, ' '); str_append(tv, (size_t)n, "%)");
    }
    *pos = speed_max ? 500 : speed;
}
static void sl_z80_change(int dir)                              /* 최대에서 움직이면 원래 속도로 */
{
    if (speed_max) { speed_max = 0; speed_osd(); return; }
    speed += dir * 5; if (speed < 10) speed = 10; if (speed > 500) speed = 500; margin_bd = -1;
}
static void sl_pct_slide(uint32_t f, int r800)                  /* Z80·R800 속도 막대 (10~500 %, 5 % 단위) */
{
    int v = (int)((f * 500 + 500) / 1000 + 2) / 5 * 5; if (v < 10) v = 10; if (v > 500) v = 500;
    if (speed_max) { speed_max = 0; speed_osd(); }
    if (!r800) { speed = v; margin_bd = -1; } else M->r800_pct = v;
}
static void sl_z80_slide(uint32_t f) { sl_pct_slide(f, 0); }
static void sl_r800_item(char *tr, int n, int *pos)             /* turbo R: R800 = R800 % × 배수 (실제 클럭 7.159 MHz 기준) */
{
    tr[0] = 0;
    uint32_t eff = (uint32_t)M->r800_pct * (uint32_t)speed / 100;
    if (eff > 500) eff = 500;
    if (speed_max) { str_append(tr, (size_t)n, "R800 "); str_append(tr, (size_t)n, T(S_OV_XMAX)); eff = 500; }
    else {
        uint32_t rk = (uint32_t)((uint64_t)7159u * (uint32_t)M->r800_pct * (uint32_t)speed / 10000u);
        num_append(tr, (size_t)n, rk / 1000, 0, ' '); str_append(tr, (size_t)n, "."); num_append(tr, (size_t)n, rk % 1000, 3, '0');
        str_append(tr, (size_t)n, " MHz ("); num_append(tr, (size_t)n, (uint32_t)M->r800_pct * (uint32_t)speed / 100, 0, ' '); str_append(tr, (size_t)n, "%)");
    }
    *pos = (int)eff;
}
static void sl_r800_change(int dir)
{
    if (speed_max) { speed_max = 0; speed_osd(); return; }
    M->r800_pct += dir * 5; if (M->r800_pct < 10) M->r800_pct = 10; if (M->r800_pct > 500) M->r800_pct = 500;
}
static void sl_r800_slide(uint32_t f) { sl_pct_slide(f, 1); }
static void sl_mult_item(char *pv, int n, int *pos)             /* 속도 배수 게이지 1배·2배·4배·최대 */
{
    int p = speed_max ? 500 : speed <= 100 ? 0 : speed <= 200 ? (speed - 100) * 167 / 100
          : speed <= 400 ? 167 + (speed - 200) * 166 / 200 : 333 + (speed - 400) * 167 / 100;
    if (p > 500) p = 500;
    copy_str(pv, (size_t)n, speed_max ? T(S_OV_XMAX) : speed == 100 ? T(S_OV_X1) : speed == 200 ? T(S_OV_X2) : speed == 400 ? T(S_OV_X4) : T(S_SPEED_CUSTOM));
    *pos = p;
}
static void sl_mult_change(int dir)                             /* 1배 · 2배 · 4배 · 최대 (끝에서 멈춤) */
{
    static const int pv[3] = { 100, 200, 400 };
    int k = speed_max ? 3 : speed >= 400 ? 2 : speed >= 200 ? 1 : 0;
    if (dir == 0) dir = 1;
    if (!speed_max && speed != pv[k > 2 ? 2 : k] && dir < 0) dir = 0;   /* 직접 정한 값에서 ←: 아래 칸에 맞춤 */
    k += dir; if (k < 0) k = 0; if (k > 3) k = 3;
    if (k == 3) { if (!speed_max) do_action(A_SPEED_MAX); }
    else { speed = pv[k]; speed_max = 0; turbo_hold = 0; speed_osd(); }
    margin_bd = -1;
}
static void sl_mult_slide(uint32_t f)
{
    static const int pv[3] = { 100, 200, 400 };
    int k = (int)((f * 3 + 500) / 1000);
    if (k >= 3) { if (!speed_max) do_action(A_SPEED_MAX); }
    else { speed = pv[k]; speed_max = 0; turbo_hold = 0; speed_osd(); }
    margin_bd = -1;
}

typedef struct FeSlide {
    uint16_t id; StrId name;
    void (*item)(char *val, int n, int *pos);                   /* 값 글자와 막대 자리 (0~500) */
    void (*change)(int dir);                                    /* ←/→ (dir -1/+1), Enter (0) */
    void (*slide)(uint32_t f);                                  /* 막대를 누르거나 끌 때 (f = 0~1000) */
} FeSlide;
static const FeSlide fe_slide[] = {
    { 42,  S_SCANLINES,    sl_scan_item, sl_scan_change, sl_scan_slide },
    { 3,   S_EMU_CORE,     sl_z80_item,  sl_z80_change,  sl_z80_slide },
    { 9,   S_R800_SPEED,   sl_r800_item, sl_r800_change, sl_r800_slide },
    { 117, S_SPEED_PRESET, sl_mult_item, sl_mult_change, sl_mult_slide },
};
static const FeSlide *slide_find(int id)
{
    for (unsigned i = 0; i < sizeof fe_slide / sizeof fe_slide[0]; i++) if (fe_slide[i].id == id) return &fe_slide[i];
    return 0;
}
static void fx_apply(void) { gm_fx_set(fx_on ? fx_rev : 100, fx_on ? fx_cho : 100, fx_on ? fx_len : 100); }
static void fx_slide_set(int id, int v) { v = v < 0 ? 0 : v > 200 ? 200 : v; if (id == 161) fx_rev = v; else if (id == 162) fx_cho = v; else fx_len = v; fx_apply(); settings_changed(); }   /* 5.2.159 공간 음향 막대 (161 리버브, 162 코러스) */
static int slide_change(int id, int dir)
{
    if (id >= 161 && id <= 163) { int v = id == 161 ? fx_rev : id == 162 ? fx_cho : fx_len; fx_slide_set(id, dir == 0 ? 100 : v + dir * 10); return 1; }
    const FeSlide *s = slide_find(id); if (!s) return 0; s->change(dir); return 1;
}
static int slide_press(int id, uint32_t f)
{
    if (id >= 161 && id <= 163) { fx_slide_set(id, (int)((f * 200 + 500) / 1000 / 5 * 5)); return 1; }
    const FeSlide *s = slide_find(id); if (!s) return 0; s->slide(f); return 1;
}
