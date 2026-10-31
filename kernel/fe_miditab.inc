/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_miditab.inc - 5.2.54 속성 ▶ MIDI 탭 새 디자인 (MIDI 개편 4단계) — 디자인 캔버스 확정본: 2안 기계 카드 + 3안 신호 띠.
 *   위: 신호 띠  [기종] ━ [MIDI OUT] ━▶ [기계1 ch] ━▶ [기계2 ch] ━▶ [+ 꽂기]   …   [MIDI IN ◀ USB 장치]
 *   왼쪽: MSX 쪽 MIDI 단자 (OUT: 내장 MSX-MIDI·SFG, IN: 내장 MSX-MIDI ← USB 장치). 단자는 기종·꽂은 카트리지가 알려 줌
 *   오른쪽: 고른 OUT 단자에 꽂힌 기계 카드 (번호·이름·설명·받는 채널 알약·LCD·채널 불 16·VOL·L/R) + 빈 자리 카드
 *   아래: 단추 — 기계 꽂기 ▾ / 빼기 / 기계 설정… / ↑ 위로 / ↓ 아래로 / MIDI 믹서(채널)…
 * 기계 목록은 midi_bus (src/mididev.c), 저장은 ini MidiDev 줄. 예전 MIDI 출력·음원 변수는 midi_ui_from_bus 로 맞춰 둠 (옛 판 호환). */
static int mt_zone = 1, mt_port, mt_dev;                /* 고른 쪽 (0 단자, 1 기계), 단자 줄, 보이는 기계 차례 (= 기계 수면 빈 자리) */
/* 단자 줄 = OUT p (0~2 = MDP_*), IN p (10 + p), 없음 (99). 단자는 기종·꽂은 카트리지가 알려 줌 (msx_midi_jacks) */
enum { MTP_IN = 10, MTP_NONE = 99 };
static int mt_out_last = MDP_MSXMIDI;                   /* IN 줄을 고른 동안 오른쪽에 보일 OUT 단자 */
static int mt_ports(int *pt)
{
    int n = 0, j = msx_midi_jacks(M);
    for (int p = 0; p < MDP_N; p++) if (j >> p & 1) pt[n++] = p;
    if (!n) pt[n++] = MTP_NONE;
    for (int p = 0; p < MDP_N; p++) if (j >> p & 1) pt[n++] = MTP_IN + p;
    return n;
}
static int mt_njacks(void) { int j = msx_midi_jacks(M), c = 0; for (int p = 0; p < MDP_N; p++) c += j >> p & 1; return c; }
static int mt_two_out(void) { return mt_njacks() >= 2; }   /* 단자가 둘 이상이면 기계는 단자별로 + 왼쪽 단자 칸 */
static const char *mt_jack_src(int p)                   /* 단자가 달린 곳 */
{
    if (p == MDP_SFG) return M->opm.opp ? "SFG-05" : "SFG-01";
    if (p == MDP_MM) return "Music Module";
    return msx_dev(M, MDEV_MIDI_HW) && midi_int ? nt("본체", "Main unit", "本体") : sp_msxmidi_slot >= 0 && cart_special[sp_msxmidi_slot] == sp_code(67) ? "X86MSX MIDI" : "MSX-MIDI";   /* 5.2.62 */
}
static void mt_jack_name(int code, char *b, int max)    /* "본체 MIDI OUT", "SFG-05 MIDI IN", "Music Module MIDI OUT" */
{
    if (code == MTP_NONE) { copy_str(b, max, nt("MIDI 단자 없음", "No MIDI jack", "MIDI端子なし")); return; }
    copy_str(b, max, mt_jack_src(code % MTP_IN)); str_append(b, max, code >= MTP_IN ? " MIDI IN" : " MIDI OUT");
}
static int mt_sel_out(void)                             /* 고른 OUT 단자 (MDP_*), 단자가 하나면 MDP_ANY */
{
    int pt[8], n = mt_ports(pt), p = mt_port < n ? pt[mt_port] : MTP_NONE;
    if (!mt_two_out()) return MDP_ANY;
    if (p < MTP_IN) mt_out_last = p;
    if (!(msx_midi_jacks(M) >> mt_out_last & 1)) mt_out_last = pt[0] < MTP_IN ? pt[0] : MDP_MSXMIDI;
    return mt_out_last;
}
static int mt_in_port(void)                             /* 지금 받는 IN 단자 (고른 것이 없어졌으면 있는 첫 단자) */
{
    int j = msx_midi_jacks(M), p = midi_in_port;
    if (!(j >> p & 1)) for (p = 0; p < MDP_N && !(j >> p & 1); p++) ;
    return p < MDP_N ? p : -1;
}
static int mt_visible(int *idx)                         /* 고른 단자에 보이는 기계 (midi_bus 번호) */
{
    int n = 0, sp = mt_sel_out();
    for (int i = 0; i < midi_bus.n; i++) { const MidiDev *d = &midi_bus.dev[i]; if (sp == MDP_ANY || d->port == MDP_ANY || d->port == sp) idx[n++] = i; }
    return n;
}
static void mt_chan_text(uint16_t m, char *b, int max)  /* 받는 채널 → "2 ~ 10", "1, 11 ~ 16", "모두", "없음" */
{
    b[0] = 0;
    if (m == 0xFFFF) { copy_str(b, max, nt("모두", "all", "全部")); return; }
    if (!m) { copy_str(b, max, nt("없음", "none", "なし")); return; }
    for (int c = 0; c < 16; ) {
        if (!(m >> c & 1)) { c++; continue; }
        int e = c; while (e + 1 < 16 && (m >> (e + 1) & 1)) e++;
        if (b[0]) str_append(b, max, ", ");
        char t[8]; int_to_str(t, sizeof t, c + 1); str_append(b, max, t);
        if (e > c) { str_append(b, max, e > c + 1 ? " ~ " : ", "); int_to_str(t, sizeof t, e + 1); str_append(b, max, t); }
        c = e + 1;
    }
}
static void mt_after_change(void) { x86midi_check(0); midi_ui_from_bus(); midi_synth_apply(); settings_changed(); menu_dirty = 1; }   /* GT 가 아니면 X86MSX MIDI 카트리지 자동 */

/* ---- 풀다운: 5100 기계 꽂기, 5101 기계 설정 ---- */
static void mt_dd_plug(void)
{
    if (midi_bus.n >= MIDI_DEV_MAX) { toast(nt("더 꽂을 자리가 없습니다 (최대 4대)", "No free slot (max 4 devices)", "空きがありません (最大4台)"), 0); return; }
    dd_n = 0; dd_sel = 0; dd_top = 0; dd_id = 5100;
    static const struct { int kind, opt; const char *ko, *en, *ja; } k[] = {
        /* 종류별로 한 줄 — MT-32/CM-32L 는 롬, GM 은 SoundFont/FM, SC-55 는 롬/GS SoundFont 를 기계 줄의 풀다운에서 고름 */
        { MDK_MT32, 0, "Roland MT-32 / CM-32L (롬)", "Roland MT-32 / CM-32L (ROM)", "Roland MT-32 / CM-32L (ROM)" },
        { MDK_GM, 0, "GM 음원 (SoundFont · 내장 FM)", "GM module (SoundFont / built-in FM)", "GM音源 (SoundFont・内蔵FM)" },
        { MDK_GS, 0, "Roland SC-55 (롬 · GS SoundFont)", "Roland SC-55 (ROM / GS SoundFont)", "Roland SC-55 (ROM・GS SoundFont)" },
        { MDK_USB, 0, "USB MIDI 장치 (바깥 기계)", "USB MIDI device (external)", "USB MIDI機器 (外部)" },
        { MDK_FILE, 0, ".mid 녹음기 (Audio 폴더)", ".mid recorder (Audio folder)", ".mid録音 (Audioフォルダ)" },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) {
        if (k[i].kind == MDK_MT32 && !MT32_OK) continue;
        copy_str(dd_txt[dd_n], sizeof dd_txt[dd_n], nt(k[i].ko, k[i].en, k[i].ja)); dd_val[dd_n] = k[i].kind * 16 + k[i].opt; dd_n++;
    }
    dd_on = dd_n > 0;
}
static void mt_dd_setting(int di)
{
    if (di < 0 || di >= midi_bus.n) return;
    const MidiDev *d = &midi_bus.dev[di];
    dd_n = 0; dd_sel = 0; dd_top = 0; dd_id = 5101;
#define MTDD(txt, val, cur) do { if (dd_n < DD_MAX) { copy_str(dd_txt[dd_n], sizeof dd_txt[dd_n], (txt)); dd_val[dd_n] = (val); if (cur) dd_sel = dd_n; dd_n++; } } while (0)
    if (mt_two_out()) {
        MTDD(nt("단자: 모든 OUT 단자", "Jack: all OUT jacks", "端子: 全OUT"), 100 + MDP_ANY, d->port == MDP_ANY);
        int j = msx_midi_jacks(M);
        for (int p = 0; p < MDP_N; p++) if (j >> p & 1) { char t[64], jn[40]; copy_str(t, sizeof t, nt("단자: ", "Jack: ", "端子: ")); mt_jack_name(p, jn, sizeof jn); str_append(t, sizeof t, jn); MTDD(t, 100 + p, d->port == p); }
    }
    MTDD(nt("받는 채널: 모두 (1~16)", "Channels: all (1-16)", "受信ch: 全部"), 201, d->rx_mask == 0xFFFF);
    MTDD(nt("받는 채널: 기본값 (MT-32 = 2~10, 그 밖 = 모두)", "Channels: default (MT-32 = 2-10, others = all)", "受信ch: 既定"), 200, 0);
#undef MTDD
    dd_on = dd_n > 0;
}
static int mt_cur_dev(void) { int idx[MIDI_DEV_MAX], n = mt_visible(idx); return mt_dev < n ? idx[mt_dev] : -1; }

/* ---- 5.2.171: 새 MIDI 탭 — 하위 탭 [기계와 롬] / [공간 음향], 기계 줄마다 롬·SoundFont 풀다운 (5103) ---- */
static int mt_sub;                                      /* 0 기계와 롬, 1 공간 음향 */
static int mt_fx = -1;                                  /* 공간 음향 쪽 키 초점: -1 없음, 0 켜기, 1 리버브, 2 잔향 길이, 3 코러스, 4 기본값으로, 5 시험음 */
static uint32_t mt_rom_x, mt_rom_y, mt_rom_w;           /* 풀다운이 열릴 자리 */
static uint32_t mt_plug_y;                              /* 기계 꽂기 풀다운 자리 (빈 자리 줄) */
static int mt_test_di = -1, mt_test_ch, mt_test_note; static uint32_t mt_test_off;   /* 시험음: 끌 시각 */

static void mt_test_stop(void)
{
    if (mt_test_di >= 0 && mt_test_di < midi_bus.n && midi_bus.dev[mt_test_di].active) { gmi_midi(mt_test_di, (uint8_t)(0x80 | mt_test_ch)); gmi_midi(mt_test_di, (uint8_t)mt_test_note); gmi_midi(mt_test_di, 0); }
    mt_test_di = -1; mt_test_off = 0;
}
static void mt_test(int di)                             /* 시험음 듣기: 이 기계가 받는 첫 채널로 도 음을 0.9초 */
{
    if (di < 0) { for (int i = 0; i < midi_bus.n; i++) if (midi_bus.dev[i].active && !midi_bus.dev[i].mute) { di = i; break; } }
    if (di < 0 || di >= midi_bus.n) { toast(nt("소리를 낼 수 있는 기계가 없습니다", "No device that can play", "鳴らせる機器がありません"), 0); return; }
    const MidiDev *d = &midi_bus.dev[di];
    if (!d->active) { toast(nt("이 기계는 지금 소리를 낼 수 없습니다 (롬·음원 확인)", "This device cannot play now (check ROM/engine)", "この機器は今は鳴らせません"), 0); return; }
    int ch = -1; for (int c = 0; c < 16; c++) if (d->rx_mask >> c & 1) { ch = c; if (c != 9) break; }
    if (ch < 0) { toast(nt("받는 채널이 없습니다", "No receiving channel", "受信chなし"), 0); return; }
    mt_test_stop();
    mt_test_di = di; mt_test_ch = ch; mt_test_note = ch == 9 ? 38 : 60; mt_test_off = (P.ms() + 900) | 1;
    gmi_midi(di, (uint8_t)(0x90 | ch)); gmi_midi(di, (uint8_t)mt_test_note); gmi_midi(di, 100);
}

/* 풀다운 (5103): 이 기계 줄의 롬 · SoundFont. 값: 400+i MT-32 묶음, 410 MT-32 자동, 420 SC-55 자동, 421 mkII, 422 원판, 1 내장 FM, 300 SoundFont, 302/303 GS SoundFont */
static void mt_dd_sel(int di)
{
    if (di < 0 || di >= midi_bus.n) return;
    const MidiDev *d = &midi_bus.dev[di];
    if (d->kind != MDK_MT32 && d->kind != MDK_GM && d->kind != MDK_GS) { mt_dd_setting(di); return; }
    if (!rs_scanned) rs_scan();
    dd_n = 0; dd_sel = 0; dd_top = 0; dd_id = 5103; dd_two = 0; for (int q = 0; q < DD_MAX; q++) dd_sub[q][0] = 0;
#define MTDD(txt, val, cur) do { if (dd_n < DD_MAX) { copy_str(dd_txt[dd_n], sizeof dd_txt[dd_n], (txt)); dd_val[dd_n] = (val); if (cur) dd_sel = dd_n; dd_n++; } } while (0)
    if (d->kind == MDK_MT32) {
        int cur = rs_mt_cur(d);
        for (int i = 0; i < rs_mt_n; i++) {
            char t[64]; copy_str(t, sizeof t, rs_mt[i].why[0] ? "\xE2\x80\x94 " : (i == cur ? "\xE2\x97\x8F " : "")); str_append(t, sizeof t, rs_mt[i].label);
            if (rs_mt[i].why[0]) { str_append(t, sizeof t, "  ("); str_append(t, sizeof t, rs_mt[i].why); str_append(t, sizeof t, ")"); }
            else if (rs_mt[i].nfiles > 1) { str_append(t, sizeof t, nt("  · 같은 롬 · 파일 ", "  · same ROM · files ", "  · 同じROM · ファイル ")); char z[8]; int_to_str(z, sizeof z, rs_mt[i].nfiles); str_append(t, sizeof t, z); }
            MTDD(t, 400 + i, i == cur);
            copy_str(dd_sub[dd_n - 1], sizeof dd_sub[0], rs_mt[i].files);
            if (rs_mt[i].pcm[0]) { str_append(dd_sub[dd_n - 1], sizeof dd_sub[0], "  +  "); str_append(dd_sub[dd_n - 1], sizeof dd_sub[0], rs_mt[i].pcm); }
        }
        if (!rs_mt_n) MTDD(nt("MidiSound\\MT32 에서 롬을 찾지 못했습니다", "No ROM found in MidiSound\\MT32", "MidiSound\\MT32にROMなし"), 499, 0);
        MTDD(nt("자동 (롬 종류 이름의 파일을 찾음)", "Auto (find files by ROM-type name)", "自動 (ROM名のファイルを探す)"), 410, cur == -1);
        for (int q = 0; q < dd_n; q++) if (dd_val[q] == 410 || dd_val[q] == 499) dd_sub[q][0] = 0;
        dd_two = 1;
    } else if (d->kind == MDK_GS) {
        for (int m = 0; m < 2; m++) {
            char t[64]; int cur = d->opt == 0 && sc55_set == m + 1;
            copy_str(t, sizeof t, rs_sc[m].why[0] ? "\xE2\x80\x94 " : (cur ? "\xE2\x97\x8F " : "")); str_append(t, sizeof t, rs_sc[m].label);
            if (rs_sc[m].why[0]) { str_append(t, sizeof t, "  ("); str_append(t, sizeof t, rs_sc[m].why); str_append(t, sizeof t, ")"); }
            MTDD(t, 421 + m, cur);
        }
        MTDD(nt("자동 (mkII 가 있으면 mkII)", "Auto (mkII if present)", "自動 (mkIIがあればmkII)"), 420, d->opt == 0 && !sc55_set);
        MTDD(nt("GS SoundFont: GM 음원과 같은 파일", "GS SoundFont: same file as GM module", "GS SoundFont: GM音源と同じ"), 302, d->opt == 1 && !gs_slot());
        static FeDirEntry se[DD_MAX];
        int n = P.list_dir ? P.list_dir(midi_dir(MD_SF), se, DD_MAX) : 0;
        for (int i = 0; i < n && dd_n < DD_MAX - 2; i++) {
            const char *nm = se[i].key[0] ? se[i].key : se[i].name;
            if (se[i].is_dir || !is_sf2(nm)) continue;
            char t[64] = "GS SoundFont: "; str_append(t, sizeof t, se[i].name[0] ? se[i].name : nm);
            copy_str(dd_key[dd_n], sizeof dd_key[dd_n], nm);
            MTDD(t, 303, d->opt == 1 && gs_slot() && !strcmp(nm, gs_name));
        }
    } else {
        MTDD(nt("내장 FM", "Built-in FM", "内蔵FM"), 1, d->opt == 1);
        static FeDirEntry se[DD_MAX];
        int n = P.list_dir ? P.list_dir(midi_dir(MD_SF), se, DD_MAX) : 0, got = 0;
        for (int i = 0; i < n && dd_n < DD_MAX - 2; i++) {
            const char *nm = se[i].key[0] ? se[i].key : se[i].name;
            if (se[i].is_dir || !is_sf2(nm)) continue;
            char t[64] = "SoundFont: "; str_append(t, sizeof t, se[i].name[0] ? se[i].name : nm);
            copy_str(dd_key[dd_n], sizeof dd_key[dd_n], nm); got++;
            MTDD(t, 300, d->opt == 0 && !strcmp(nm, sf_name));
        }
        if (!got) MTDD(nt("SoundFont: (MidiSound\\SoundFont 에 .sf2 없음)", "SoundFont: (no .sf2 in MidiSound\\SoundFont)", "SoundFont: (.sf2なし)"), 301, 0);
    }
#undef MTDD
    dd_on = dd_n > 0;
}
static void mt_dd_apply(int i)
{
    int v = dd_val[i];
    if (dd_id == 5100) {
        int kind = v / 16, opt = v % 16, di = midibus_plug(kind, opt);
        if (di < 0) return;
        MidiDev *d = &midi_bus.dev[di];
        d->port = (uint8_t)mt_sel_out();
        if (kind == MDK_MT32) d->rx_mask = 0x03FE;      /* 실기 기본 2~10 */
        if (kind == MDK_GS && opt == 0 && sc55_state < 0) sc55_state = 0;   /* 롬을 다시 찾아봄 */
        int idx[MIDI_DEV_MAX], n = mt_visible(idx); for (int k = 0; k < n; k++) if (idx[k] == di) mt_dev = k;
        toast(nt("기계를 꽂았습니다", "Device plugged in", "機器を接続しました"), midi_dev_class[kind].name);
        mt_after_change();                              /* 카트리지를 자동으로 꽂았으면 그 알림이 나중 (보이게) */
        return;
    }
    if (dd_id == 5104) {                                /* MIDI 입력 장치: 0 받지 않음, 1 USB MIDI 장치, 10+p 이 IN 단자로 */
        if (v == 0) midi_in = 0; else { midi_in = 1; if (v >= 10) midi_in_port = v - 10; }
        settings_changed(); menu_dirty = 1; return;
    }
    int di = mt_cur_dev(); if (di < 0) return;
    MidiDev *d = &midi_bus.dev[di];
    if (dd_id == 5103) {
        if (v >= 400 && v < 400 + RS_MAX) {              /* MT-32 / CM-32L 롬 묶음 */
            const RomSet *s = &rs_mt[v - 400];
            if (s->why[0]) { toast(s->label, s->why); return; }
            copy_str(d->rom_ctl, sizeof d->rom_ctl, s->ctl); copy_str(d->rom_pcm, sizeof d->rom_pcm, s->pcm);
            d->opt = s->model;                          /* 롬은 이 기계의 것 (엔진도 기계마다 따로) */
        } else if (v == 410) { d->rom_ctl[0] = d->rom_pcm[0] = 0; }
        else if (v >= 420 && v <= 422) {
            if (v > 420 && rs_sc[v - 421].why[0]) { toast(rs_sc[v - 421].label, rs_sc[v - 421].why); return; }
            sc55_set = v - 420; d->opt = 0; sc55_state = 0;
        }
        else if (v == 302) { gs_name[0] = 0; gs_state = 0; d->opt = 1; }
        else if (v == 303) { copy_str(gs_name, sizeof gs_name, dd_key[i]); gs_state = 0; d->opt = 1; if (gs_slot()) sf_load_slot(1, 0); }
        else if (v == 300) { d->opt = 0; copy_str(sf_name, sizeof sf_name, dd_key[i]); sf_state = 0; sf_load(0); }
        else if (v == 1) d->opt = 1;
        else return;
        settings_changed(); mt_after_change(); return;
    }
    if (v >= 100 && v < 200) { int p = v - 100; d->port = (uint8_t)(p < MDP_N ? p : MDP_ANY); }
    else if (v == 200) d->rx_mask = d->kind == MDK_MT32 ? 0x03FE : 0xFFFF;
    else if (v == 201) d->rx_mask = 0xFFFF;
    else return;
    mt_after_change();
}

/* ---- 마우스 ---- */
/* (mt_edit 선언은 fe.c 앞쪽) 5.2.179: 1 = 소리·좌우 칸을 ←/→ 로 조절 중 (Esc/Enter 로 나옴) */
static int mt_col;                                      /* 기계 줄 안 칸: 0 롬·파일 풀다운, 1 받는 채널 풀다운 */
static uint32_t mt_ch_x, mt_ch_y, mt_ch_w, mt_in_x, mt_plug_x, mt_plug_w;   /* 풀다운이 열릴 자리 */
static void hc_fx(const UiHit *h, int ev)               /* a = 160 켜기, 161~163 막대 (c = 막대 너비), 164 기본값으로, 165 시험음 */
{
    int id = (int)h->a;
    mt_fx = h->b; menu_dirty = 1;
    if (id >= 161 && id <= 163) {
        if (ev == 0) return;
        uint32_t bw = (uint32_t)h->c, bx = h->x + 8 * S;
        if (!bw) return;
        uint32_t px = ptr_x < bx ? bx : ptr_x > bx + bw ? bx + bw : ptr_x, f = (px - bx) * 1000 / bw;
        if (!fx_on) fx_on = 1;
        slide_press(id, f);
        return;
    }
    if (ev != 1) return;
    if (id == 160) { fx_on = !fx_on; fx_apply(); settings_changed(); }
    else if (id == 164) { fx_rev = fx_cho = fx_len = 100; fx_apply(); settings_changed(); }
    else if (id == 165) mt_test(-1);
}
static void mt_set_sub(int s) { mt_edit = 0; mt_sub = s; mt_fx = s ? 0 : -1; if (s) mt_zone = 1; dd_on = 0; menu_dirty = 1; }
static void mt_open_sel(int k)                          /* k 번째 보이는 기계의 롬·SoundFont 풀다운 */
{
    mt_dev = k; mt_zone = 1; mt_col = 0;
    int di = mt_cur_dev(); if (di < 0) return;
    mt_dd_sel(di);
}
static void mt_open_chan(int k)                         /* 받는 채널 (그리고 단자) 풀다운 */
{
    mt_dev = k; mt_zone = 1; mt_col = 1;
    int di = mt_cur_dev(); if (di < 0) return;
    mt_dd_setting(di);
}
static void mt_open_in(void)                            /* MIDI 입력 장치… */
{
    if (!P.midi_read) { toast(nt("이 판에서는 MIDI 입력을 쓸 수 없습니다", "MIDI input is not available here", "MIDI入力は使えません"), 0); return; }
    dd_n = 0; dd_sel = 0; dd_top = 0; dd_id = 5104; dd_two = 0;
#define MTDD(txt, val, cur) do { if (dd_n < DD_MAX) { copy_str(dd_txt[dd_n], sizeof dd_txt[dd_n], (txt)); dd_val[dd_n] = (val); if (cur) dd_sel = dd_n; dd_n++; } } while (0)
    MTDD(nt("받지 않음", "Not receiving", "受信しない"), 0, !midi_in);
    if (mt_two_out()) { int j = msx_midi_jacks(M); for (int p = 0; p < MDP_N; p++) if (j >> p & 1) { char t[96], jn[40]; mt_jack_name(MTP_IN + p, jn, sizeof jn); copy_str(t, sizeof t, nt("USB MIDI 장치 → ", "USB MIDI device → ", "USB MIDI機器 → ")); str_append(t, sizeof t, jn); MTDD(t, 10 + p, midi_in && mt_in_port() == p); } }
    else { char t[96]; copy_str(t, sizeof t, nt("USB MIDI 장치로 받기", "Receive from USB MIDI device", "USB MIDI機器から受信")); if (midi_in && midi_in_dev >= 0 && midi_in_name[0]) { str_append(t, sizeof t, " \xC2\xB7 "); str_append(t, sizeof t, midi_in_name); } MTDD(t, 1, midi_in); }
#undef MTDD
    dd_on = dd_n > 0;
}
static void mt_row_op(int k, int op)                    /* 0 시험음, 1 위로, 2 아래로, 3 빼기 */
{
    int idx[MIDI_DEV_MAX], n = mt_visible(idx);
    if (k < 0 || k >= n) return;
    mt_dev = k; mt_zone = 1; menu_dirty = 1;
    if (op == 0) mt_test(idx[k]);
    else if (op == 1) ui_press(0xC8, 1);
    else if (op == 2) ui_press(0xD0, 1);
    else ui_press(0xD3, 0);
}
static void mt_set_bar(int di, int pan, const UiHit *h)  /* 소리·좌우 막대를 누르거나 끎 */
{
    if (di < 0 || di >= midi_bus.n || !h->w) return;
    uint32_t px = ptr_x < h->x ? h->x : ptr_x > h->x + h->w ? h->x + h->w : ptr_x;
    int f = (int)((px - h->x) * 200 / h->w);            /* 0~200 */
    MidiDev *d = &midi_bus.dev[di];
    if (pan) { int v = f - 100; if (v > -8 && v < 8) v = 0; d->pan = (int8_t)(v < -100 ? -100 : v > 100 ? 100 : v); }
    else { int v = f / 2; d->vol = (uint8_t)(v > 100 ? 100 : v); if (v) d->mute = 0; }
    settings_changed(); menu_dirty = 1;
}
/* a: 0 단자 알약, 1 기계 줄, 2 단추(키), 3 하위 탭, 4 롬 풀다운 칸, 6 채널 풀다운 칸, 7 소리 막대, 8 좌우 막대, 9 줄 끝 단추 (c = 동작), 10 MIDI 입력 장치… */
static void hc_mt(const UiHit *h, int ev)
{
    int z = (int)h->a, i = h->b;
    if (z == 2) { if (ev == 1 && h->k) ui_press(h->k, h->c); return; }   /* 단추 = 키 */
    if (z == 3) { if (ev == 1 && mt_sub != i) mt_set_sub(i); return; }
    if (z == 4) { if (ev == 1) mt_open_sel(i); else if (mt_dev != i || mt_zone != 1) { mt_zone = 1; mt_dev = i; menu_dirty = 1; } return; }
    if (z == 6) { if (ev == 1) mt_open_chan(i); else if (mt_dev != i || mt_zone != 1) { mt_zone = 1; mt_dev = i; menu_dirty = 1; } return; }
    if (z == 7 || z == 8) { if (ev) { mt_dev = i; mt_zone = 1; mt_set_bar(mt_cur_dev(), z == 8, h); } return; }
    if (z == 9) { if (ev == 1) mt_row_op(i, h->c); return; }
    if (z == 10) { if (ev == 1) mt_open_in(); return; }
    int was = mt_zone == z && (z ? mt_dev : mt_port) == i;
    if (!was) { mt_zone = z; if (z) mt_dev = i; else mt_port = i; menu_dirty = 1; }
    if (ev == 1 && z == 0) { int pt[8], n = mt_ports(pt); if (i < n && pt[i] >= MTP_IN && pt[i] != MTP_NONE && P.midi_read) ui_press(0x1C, 0); return; }
    if (ev == 1 && was) ui_press(0x1C, 0);
}

/* ---- 그리기 (디자인: MIDI 탭 새 디자인 — 기계 / 공간 음향) ---- */
static uint32_t mt_pill(uint32_t x, uint32_t y, uint32_t h, const char *t, const char *sub, int cur, uint32_t subcol)   /* 하위 탭 알약: 고른 것은 파랑 가득, 아니면 테두리 */
{
    uint32_t w = ui_sw(t) + 40 * S + (sub ? ui_sw(sub) : 0);
    ui_rframe(x, y, w, h, h / 2, cur ? C_ACC : C_LINE, cur ? C_ACC : C_HEAD);
    text_f(SF, 1, x + 20 * S, ui_sty(y, h), t, cur ? C_BACK : C_MUTED, 0xFF, w, 1);
    if (sub) text_f(SF, 1, x + 20 * S + ui_sw(t), ui_sty(y, h), sub, subcol, 0xFF, w, 1);
    return w;
}
static const char *mt_kind_name(const MidiDev *d, int di)
{
    return d->kind == MDK_MT32 ? "Roland MT-32" : d->kind == MDK_GM ? nt("GM 음원", "GM module", "GM音源") : d->kind == MDK_GS ? "Roland SC-55" : d->kind == MDK_USB ? nt("USB MIDI 장치", "USB MIDI device", "USB MIDI機器") : nt(".mid 녹음기", ".mid recorder", ".mid録音");
}
static const char *mt_kind_sub(const MidiDev *d, int di)  /* 이름 밑 작은 글자 */
{
    if (d->kind == MDK_MT32) return d->opt ? nt("CM-32L · 실제 롬", "CM-32L - real ROM", "CM-32L · 実ROM") : nt("실제 롬", "real ROM", "実ROM");
    if (d->kind == MDK_GM) return d->opt ? nt("내장 FM", "built-in FM", "内蔵FM") : "SoundFont";
    if (d->kind == MDK_GS) return d->opt ? "GS SoundFont" : nt("실제 롬", "real ROM", "実ROM");
    if (d->kind == MDK_USB) return nt("바깥 기계", "external", "外部機器");
    (void)di; return nt("Audio 폴더에 저장", "saved to Audio folder", "Audioフォルダに保存");
}
static const char *mt_field_cap(const MidiDev *d)       /* 롬 칸 위 이름표 */
{
    if (d->kind == MDK_MT32) return nt("MT-32 / CM-32L 롬", "MT-32 / CM-32L ROM", "MT-32 / CM-32L ROM");
    if (d->kind == MDK_GM) return d->opt ? nt("음원", "Engine", "音源") : nt("SoundFont 파일", "SoundFont file", "SoundFontファイル");
    if (d->kind == MDK_GS) return d->opt ? nt("GS SoundFont 파일", "GS SoundFont file", "GS SoundFontファイル") : nt("SC-55 롬", "SC-55 ROM", "SC-55 ROM");
    if (d->kind == MDK_USB) return nt("장치", "Device", "機器");
    return nt("저장", "Save", "保存");
}
static void mt_sel_text(const MidiDev *d, char *cur, int cmax, int *dim)   /* 롬 칸에 보일 지금 고른 것 */
{
    *dim = 0; cur[0] = 0;
    if (d->kind == MDK_MT32) {
        int c = rs_mt_cur(d);
        if (c >= 0) copy_str(cur, cmax, rs_mt[c].label); else if (c == -2) copy_str(cur, cmax, d->rom_ctl); else copy_str(cur, cmax, nt("자동", "Auto", "自動"));
    } else if (d->kind == MDK_GM) {
        if (d->opt) copy_str(cur, cmax, nt("내장 FM", "Built-in FM", "内蔵FM")); else if (sf_name[0]) copy_str(cur, cmax, sf_name); else { copy_str(cur, cmax, nt("SoundFont 없음", "no SoundFont", "SoundFontなし")); *dim = 1; }
    } else if (d->kind == MDK_GS) {
        if (d->opt == 0) { copy_str(cur, cmax, sc55_set == 1 ? "SC-55mkII" : sc55_set == 2 ? nt("SC-55 (원판)", "SC-55 (original)", "SC-55 (初代)") : nt("자동", "Auto", "自動")); }
        else { if (gs_slot()) copy_str(cur, cmax, gs_name); else { copy_str(cur, cmax, nt("GM 음원과 같은 파일", "same as GM module", "GM音源と同じ")); *dim = 1; } }
    } else if (d->kind == MDK_USB) { copy_str(cur, cmax, midi_out_name[0] && usb_midi_ok ? midi_out_name : nt("찾는 중 / 없음", "searching / none", "検索中/なし")); *dim = !(midi_out_name[0] && usb_midi_ok); }
    else copy_str(cur, cmax, "MIDI_nnn.mid");
}
static void mt_short_status(const MidiDev *d, int di, char *b, int max, uint32_t *col)   /* 줄 오른쪽의 짧은 상태 */
{
    *col = C_OK; copy_str(b, max, nt("정상", "OK", "正常"));
    if (d->mute) { *col = C_FAINT; copy_str(b, max, nt("꺼짐", "muted", "ミュート")); return; }
    if (d->kind == MDK_MT32) {
        int sl = mt_slot_of(di), st = sl >= 0 ? mt_st[sl] : -8;
        if (st == 1) { copy_str(b, max, nt("롬 확인됨", "ROM OK", "ROM確認")); if (gmi_mode(di) != GM_MT32) { *col = C_WARN; copy_str(b, max, nt("다른 음원", "other synth", "別音源")); } }
        else if (st == 0) { *col = C_MUTED; copy_str(b, max, nt("읽는 중", "loading", "読込中")); }
        else { *col = C_BAD; copy_str(b, max, st == -8 ? nt("엔진 2대까지", "2 engines max", "2台まで") : st == -1 ? (rs_usb_bad ? nt("USB 못 읽음", "USB unreadable", "USB不可") : nt("롬 없음", "no ROM", "ROMなし")) : st == -4 ? nt("메모리 부족", "no memory", "メモリ不足") : st == -9 ? nt("이 판에 없음", "not in build", "非対応") : nt("롬 오류", "ROM error", "ROMエラー")); }
    } else if (d->kind == MDK_GM) { if (!d->opt && sf_state != 1) { *col = C_WARN; copy_str(b, max, nt("FM으로", "using FM", "FMで")); } }
    else if (d->kind == MDK_GS) {
        if (gmi_mode(di) == GM_SC55) copy_str(b, max, nt("롬 확인됨", "ROM OK", "ROM確認"));
        else if (!d->opt) { *col = C_BAD; copy_str(b, max, sc55_state == -9 ? nt("이 판에 없음", "not in build", "非対応") : sc55_state == -1 ? (rs_usb_bad ? nt("USB 못 읽음", "USB unreadable", "USB不可") : nt("롬 없음", "no ROM", "ROMなし")) : sc55_state < 0 ? nt("롬 오류", "ROM error", "ROMエラー") : nt("엔진 1대만", "1 engine only", "1台のみ")); }
        else if (gmi_mode(di) != GM_SF2) { *col = C_WARN; copy_str(b, max, nt("FM으로", "using FM", "FMで")); }
    }
    else if (d->kind == MDK_USB) { if (!(midi_out_name[0] && usb_midi_ok)) { *col = C_MUTED; copy_str(b, max, nt("찾는 중", "searching", "検索中")); } else copy_str(b, max, nt("연결됨", "connected", "接続")); }
    else { *col = C_MUTED; copy_str(b, max, midi_len ? nt("녹음 중", "recording", "録音中") : nt("대기", "standby", "待機")); }
}
static void mt_status_text(const MidiDev *d, int di, char *b, int max, uint32_t *col)   /* 고른 줄의 자세한 상태 */
{
    mt_short_status(d, di, b, max, col);
    if (d->kind == MDK_MT32) {
        int sl = mt_slot_of(di), st = sl >= 0 ? mt_st[sl] : -8;
        if (st == -1 && rs_usb_bad) copy_str(b, max, nt("USB 를 읽지 못했습니다 (롬 파일 문제가 아님) — USB 를 다시 꽂고 F5 를 누르세요", "Cannot read the USB (not a ROM problem) - replug it and press F5", "USBを読めません(ROMの問題ではありません) — 挿し直してF5"));
        else if (st == -1) copy_str(b, max, nt("MidiSound\\MT32 에서 제어 롬과 소리 롬(PCM)을 찾지 못했습니다 — 롬 칸에서 고르거나 F5 로 다시 찾으세요", "MT-32 control/PCM ROM not found in MidiSound\\MT32 - pick one or press F5", "MidiSound\\MT32 にROMがありません — 選択またはF5"));
        else if (st == -8) copy_str(b, max, nt("MT-32 엔진은 두 대까지 쓸 수 있습니다", "At most two MT-32 engines", "MT-32エンジンは2台まで"));
    } else if (d->kind == MDK_GS && !d->opt && gmi_mode(di) != GM_SC55) {
        if (sc55_state == -1 && rs_usb_bad) copy_str(b, max, nt("USB 를 읽지 못했습니다 (롬 파일 문제가 아님) — USB 를 다시 꽂고 F5 를 누르세요", "Cannot read the USB (not a ROM problem) - replug it and press F5", "USBを読めません(ROMの問題ではありません) — 挿し直してF5"));
        else if (sc55_state == -1) copy_str(b, max, nt("MidiSound\\SC55 에서 SC-55 롬 파일을 찾지 못했습니다", "SC-55 ROM files not found in MidiSound\\SC55", "MidiSound\\SC55 にROMがありません"));
        else if (sc55_state >= 0) copy_str(b, max, nt("SC-55 엔진은 지금 한 대만 쓸 수 있어 다른 SC-55 가 쓰는 중입니다", "Only one SC-55 engine for now; another SC-55 is using it", "SC-55エンジンは現在1台のみ"));
    }
}
static void mt_ring(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t fill)   /* 2px 파랑 테두리 + 안쪽 색 */
{
    ui_rrect(x, y, w, h, r, C_ACC); ui_rrect(x + 2 * S, y + 2 * S, w - 4 * S, h - 4 * S, r > 2 * S ? r - 2 * S : r, fill);
}
static void mt_bar(uint32_t x, uint32_t y, uint32_t w, int pct, int pan, int mute)   /* 시안: 두께 5 막대 + 지름 14 흰 동그라미. 소리는 왼쪽부터, 좌우는 가운데 기준 */
{
    uint32_t h = 5 * S, kd = 14 * S;
    ui_rrect(x, y, w, h, h / 2, C_TRACK);
    uint32_t k;
    if (pan) { uint32_t c = x + w / 2; k = x + w * (uint32_t)(pct + 100) / 200; if (k > c) fe_fill(c, y, k - c, h, C_ACC); else fe_fill(k, y, c - k, h, C_ACC); }
    else { uint32_t f = w * (uint32_t)pct / 100; k = x + f; ui_rrect(x, y, f > h ? f : h, h, h / 2, mute ? C_FAINT : C_ACC); }
    ui_rrect(k - kd / 2, y - 5 * S, kd, kd, kd / 2, C_TEXT);
}
static void mt_fx_card(uint32_t cx, uint32_t py, uint32_t cw, uint32_t fh, uint32_t rowh)   /* 공간 음향 이펙트 카드 (큰 쪽) */
{
    int on = fx_on;
    ui_rframe(cx, py, cw, fh, 14 * S, C_LINE, C_CARD);
    text_clip(cx + 24 * S, py + 20 * S, nt("공간 음향 이펙트", "Space effects", "空間エフェクト"), C_TEXT, 0xFF, cw - 160 * S);
    text_f(SF, 1, cx + 24 * S, py + 24 * S + LH, nt("리버브(울림)와 코러스(겹침)를 더해 소리를 풍성하게 합니다", "Adds reverb and chorus to enrich the sound", "リバーブとコーラスで音を豊かにします"), C_MUTED, 0xFF, cw - 160 * S, 1);
    {   /* 켜짐 + 스위치 */
        uint32_t tw = 44 * S, th = 24 * S, tx = cx + cw - 24 * S - tw, ty = py + 20 * S;
        if (mt_fx == 0) ui_rrect(tx - 3 * S, ty - 3 * S, tw + 6 * S, th + 6 * S, th / 2 + 3 * S, C_TEXT);
        ui_rrect(tx, ty, tw, th, th / 2, on ? C_ACC : C_FIELD);
        ui_rrect(on ? tx + tw - th + 3 * S : tx + 3 * S, ty + 3 * S, th - 6 * S, th - 6 * S, (th - 6 * S) / 2, C_TEXT);
        hit_add(hc_fx, 160, 0, 0, 0, tx - 4 * S, ty - 4 * S, tw + 8 * S, th + 8 * S);
        const char *st = on ? nt("켜짐", "On", "オン") : nt("꺼짐", "Off", "オフ");
        text_f(SF, 1, tx - 12 * S - ui_sw(st), ui_sty(ty, th), st, on ? C_OK : C_MUTED, 0xFF, ui_sw(st) + 4, 1);
    }
    static const struct { const char *ko, *en, *ja; int id; } sl[3] = {
        { "리버브 세기", "Reverb level", "リバーブ量", 161 }, { "잔향 길이", "Reverb length", "残響の長さ", 163 }, { "코러스 세기", "Chorus level", "コーラス量", 162 } };
    uint32_t y0 = py + LH + SLH + 56 * S;
    for (int k = 0; k < 3; k++) {
        uint32_t ry = y0 + (uint32_t)k * rowh, labw = 130 * S, valw = 80 * S;
        int v = sl[k].id == 161 ? fx_rev : sl[k].id == 162 ? fx_cho : fx_len;
        int foc = mt_fx == (k == 0 ? 1 : k == 1 ? 2 : 3);
        text_clip(cx + 24 * S, ui_ty(ry, rowh), nt(sl[k].ko, sl[k].en, sl[k].ja), on ? C_TEXT : C_FAINT, 0xFF, labw);
        uint32_t bx = cx + 24 * S + labw, bw = cw - labw - valw - 56 * S, ym = ry + rowh / 2;
        ui_rrect(bx, ym - 4 * S, bw, 8 * S, 4 * S, C_FIELD);
        ui_rrect(bx, ym - 4 * S, bw * (uint32_t)v / 200 > 8 * S ? bw * (uint32_t)v / 200 : 8 * S, 8 * S, 4 * S, on ? C_ACC : C_FAINT);
        ui_rrect(bx + bw * (uint32_t)v / 200 - 10 * S, ym - 10 * S, 20 * S, 20 * S, 10 * S, C_TEXT);
        if (foc) { ui_rrect(bx + bw * (uint32_t)v / 200 - 13 * S, ym - 13 * S, 26 * S, 26 * S, 13 * S, C_ACC); ui_rrect(bx + bw * (uint32_t)v / 200 - 10 * S, ym - 10 * S, 20 * S, 20 * S, 10 * S, C_TEXT); }
        char vt[24]; if (sl[k].id == 163 && v == 100) copy_str(vt, sizeof vt, nt("기본", "default", "既定")); else { int_to_str(vt, sizeof vt, v); str_append(vt, sizeof vt, " %"); }
        text_clip(cx + cw - 24 * S - ui_sw(vt), ui_ty(ry, rowh), vt, on ? C_ACC : C_FAINT, 0xFF, valw);
        hit_add(hc_fx, sl[k].id, k == 0 ? 1 : k == 1 ? 2 : 3, (int)bw, 0, bx - 8 * S, ry, bw + 16 * S, rowh);
    }
    {   /* 기본값으로 · 시험음 듣기 · 안내 */
        uint32_t bh = LH + 14 * S, by2 = py + fh - bh - 22 * S, x = cx + 24 * S;
        const char *t1 = nt("기본값으로", "Defaults", "既定へ"), *t2 = nt("시험음 듣기", "Play test note", "テスト音"), *kc = "Enter";
        uint32_t w1 = ui_sw(t1) + ui_sw(kc) + 44 * S, w2 = ui_sw(t2) + 36 * S;
        ui_rframe(x, by2, w1, bh, 8 * S, mt_fx == 4 ? C_ACC : C_LINE, C_HEAD); text_f(SF, 1, x + 18 * S, ui_sty(by2, bh), t1, C_TEXT, 0xFF, w1, 1); text_f(SF, 1, x + 26 * S + ui_sw(t1), ui_sty(by2, bh), kc, C_FAINT, 0xFF, w1, 1); hit_add(hc_fx, 164, 4, 0, 0, x, by2, w1, bh);
        x += w1 + 12 * S;
        ui_rframe(x, by2, w2, bh, 8 * S, mt_fx == 5 ? C_ACC : C_LINE, C_HEAD); text_f(SF, 1, x + 18 * S, ui_sty(by2, bh), t2, C_TEXT, 0xFF, w2, 1); hit_add(hc_fx, 165, 5, 0, 0, x, by2, w2, bh);
        uint32_t hx = x + w2 + 16 * S;
        text_f(SF, 1, hx, ui_sty(by2, bh), nt("슬라이더: 위·아래로 고르고 +/- 로 바꿉니다 (마우스로 끌어도 됩니다)", "Sliders: pick with Up/Down, change with +/- (or drag)", "スライダー: ↑/↓ で選び +/- で変更 (ドラッグも可)"), C_MUTED, 0xFF, cx + cw - 24 * S - hx, 1);
    }
}

/* ---- 5.2.189 랙 장비 모양 (시안 7-2): 나사 박힌 랙 귀, 초록 LCD 이름창, 돌리는 손잡이, LED, 금속 단추 ---- */
static int mt_sin(int deg)                              /* sin(deg) x 1000, deg -180..360 */
{
    static const uint16_t t[19] = { 0, 87, 174, 259, 342, 423, 500, 574, 643, 707, 766, 819, 866, 906, 940, 966, 985, 996, 1000 };
    int neg = 0; while (deg > 180) deg -= 360; while (deg < -180) deg += 360;
    if (deg < 0) { neg = 1; deg = -deg; }
    if (deg > 90) deg = 180 - deg;
    int i = deg / 5, f = deg % 5, v = i >= 18 ? 1000 : t[i] + (t[i + 1] - t[i]) * f / 5;
    return neg ? -v : v;
}
static void mt_screw(uint32_t cx, uint32_t cy)
{
    uint32_t d = 10 * S;
    ui_rrect(cx - d / 2, cy - d / 2, d, d, d / 2, C_RACKIN);
    ui_rrect(cx - d / 2 + S, cy - d / 2 + S, d - 2 * S, d - 2 * S, d / 2 - S, C_SCREW);
    fe_fill(cx - 3 * S, cy, 6 * S, S, C_RACKIN);
}
static void mt_knob(uint32_t cx, uint32_t cy, int p, int mute)   /* 지름 36 손잡이. p 0..100 → -135°..135° */
{
    uint32_t d = 36 * S;
    ui_rrect(cx - d / 2, cy - d / 2 + S, d, d, d / 2, C_RACKIN);             /* 그림자 */
    ui_rrect(cx - d / 2, cy - d / 2, d, d, d / 2, mute ? C_FAINT : C_KNOB);
    ui_rrect(cx - d / 2 + 5 * S, cy - d / 2 + 5 * S, d - 10 * S, d - 10 * S, (d - 10 * S) / 2, C_KNOBIN);
    int a = -135 + (p < 0 ? 0 : p > 100 ? 100 : p) * 270 / 100, dx = mt_sin(a), dy = -mt_sin(90 - a);
    for (int r = 5 * (int)S; r <= (int)(d / 2) - 3 * (int)S; r += (int)S) {
        int px = (int)cx + dx * r / 1000, py = (int)cy + dy * r / 1000;
        fe_fill((uint32_t)px - S, (uint32_t)py - S, 3 * S, 3 * S, C_TEXT);
    }
}
static void mt_led(uint32_t cx, uint32_t cy, uint8_t col)
{
    uint32_t d = 14 * S;
    ui_rrect(cx - d / 2 - S, cy - d / 2 - S, d + 2 * S, d + 2 * S, d / 2 + S, C_RACKIN);
    ui_rrect(cx - d / 2, cy - d / 2, d, d, d / 2, col);
    ui_rrect(cx - 4 * S, cy - 4 * S, 4 * S, 4 * S, 2 * S, 15);
}
static void mt_metal(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int hot)   /* 금속 단추 */
{
    ui_rrect(x, y + 2 * S, w, h, 6 * S, C_RACKIN);
    ui_rrect(x, y, w, h, 6 * S, hot ? C_ACC : C_SCREW);
    ui_rrect(x + S, y + S, w - 2 * S, h - 2 * S, 5 * S, C_RACKBTN);
}
static void draw_miditab(uint32_t X, uint32_t W, uint32_t top, uint32_t bottom)
{
    uint32_t now = P.ms();
    if (!rs_scanned) rs_scan();
    if (mt_test_off && (int32_t)(now - mt_test_off) >= 0) mt_test_stop();
    if (mt_sub == 0) mt_fx = -1; else if (mt_fx < 0) mt_fx = 0;
    /* 위: 탭 둘 (1 기계 / 2 공간 음향) + 오른쪽 요약 — 키: 1·2, Tab, PgUp/PgDn, [ ] */
    uint32_t ph = ui_tabs_h(), y = top;
    int bad = 0;
    for (int i = 0; i < midi_bus.n; i++) { char b[40]; uint32_t col; mt_short_status(&midi_bus.dev[i], i, b, sizeof b, &col); if (col == C_BAD) bad++; }
    {
        char t1[96], z[8];
        copy_str(t1, sizeof t1, nt("기계", "Devices", "機器")); str_append(t1, sizeof t1, " \xC2\xB7 "); int_to_str(z, sizeof z, midi_bus.n); str_append(t1, sizeof t1, z); str_append(t1, sizeof t1, nt("대", "", "台"));
        if (bad) str_append(t1, sizeof t1, nt(" \xC2\xB7 롬 확인 필요", " \xC2\xB7 check ROM", " \xC2\xB7 ROM要確認"));
        const char *lab[2] = { t1, nt("공간 음향 \xC2\xB7 모든 MIDI에 적용", "Space effects \xC2\xB7 all MIDI", "空間音響 \xC2\xB7 全MIDI") };
        ui_tabs(X - 14 * S, y, W + 28 * S, lab, 2, mt_sub, 0);
        char sm[160];
        if (mt_sub == 0) {
            copy_str(sm, sizeof sm, "MIDI IN: "); str_append(sm, sizeof sm, midi_in ? (midi_in_dev >= 0 && midi_in_name[0] ? midi_in_name : nt("USB 장치 없음", "no USB device", "USB機器なし")) : nt("없음", "none", "なし"));
            uint32_t w = ui_sw(sm); text_f(SF, 1, X + W - w, ui_sty(y, ph), sm, C_MUTED, 0xFF, w + 4, 1);
        }
    }
    uint32_t y0 = y + ph + 12 * S, btn_h = LH + 18 * S, by = bottom - btn_h;
    {   /* 알림 줄 — 본체 MIDI 꺼짐 / MIDI 단자 없음 */
        const char *warn = 0;
        if (msx_dev(M, MDEV_MIDI_HW) && !midi_int) warn = nt("본체 MIDI 가 꺼져 있어 꽂힌 기계로 아무것도 가지 않습니다 — O 또는 누르기: 켜기 (사운드 탭과 같은 설정)", "Main unit MIDI is off; nothing reaches the devices — O or click: turn on (same as Sound tab)", "本体MIDIがオフです — O またはクリック: オン");
        else if (!midi_bus.n && midi_bus.def_on) warn = nt("꽂힌 기계가 없어도 GT 내장 MIDI 음원(GM — SoundFont, 없으면 FM)이 GTMIDI 채널로 소리 냅니다", "No device plugged: the GT built-in GM synth (SoundFont, else FM) plays on the GTMIDI channel", "機器なし: GT内蔵GM音源がGTMIDIチャンネルで鳴ります");
        else if (!msx_midi_jacks(M)) warn = midi_bus.n ? nt("MIDI 단자가 없고 빈 카트리지 슬롯도 없어 X86MSX MIDI 카트리지를 꽂지 못했습니다 — 슬롯을 하나 비우세요", "No MIDI jack and no free cartridge slot for the X86MSX MIDI cartridge — free a slot", "MIDI端子も空きスロットもありません — スロットを空けてください")
                                               : nt("MIDI 단자가 없습니다 — 기계를 꽂으면 X86MSX MIDI 카트리지가 마지막 빈 슬롯에 자동으로 꽂힙니다", "No MIDI jack — plugging a device inserts the X86MSX MIDI cartridge into the last free slot", "MIDI端子がありません — 機器を接続するとX86MSX MIDIカートリッジを自動で挿します");
        if (warn) {
            uint32_t wh = SLH + 14 * S;
            ui_rframe(X, y0, W, wh, 6 * S, C_WARN, C_CARD);
            text_f(SF, 1, X + 14 * S, ui_sty(y0, wh), warn, C_WARN, 0xFF, W - 28 * S, 1);
            if (msx_dev(M, MDEV_MIDI_HW) && !midi_int) hit_add(hc_mt, 2, 9, 0, 0x18, X, y0, W, wh);
            y0 += wh + 10 * S;
        }
    }
    if (mt_sub == 1) {   /* ---- 공간 음향 ---- */
        mt_zone = 1;
        uint32_t gap = 20 * S, lw = (W - gap) * 62 / 100, rw2 = W - gap - lw, hh = bottom - y0 - 8 * S, rowh = LH + 30 * S;
        mt_fx_card(X, y0, lw, hh, rowh);
        uint32_t rx2 = X + lw + gap, yy = y0;
        ui_rframe(rx2, yy, rw2, hh, 14 * S, C_LINE, C_CARD);
        text_clip(rx2 + 22 * S, yy + 20 * S, nt("적용되는 기계", "Applies to", "適用される機器"), C_TEXT, 0xFF, rw2 - 44 * S);
        yy += LH + 36 * S;
        text_f(SF, 1, rx2 + 22 * S, yy, nt("지금 꽂힌 기계 모두에 같은 값이 쓰입니다.", "The same values apply to every plugged device.", "接続中の全機器に同じ値が使われます。"), C_MUTED, 0xFF, rw2 - 44 * S, 1);
        yy += SLH + 14 * S;
        uint32_t ih = LH + SLH + 22 * S;
        for (int i = 0; i < midi_bus.n && yy + ih < y0 + hh - 50 * S; i++, yy += ih + 8 * S) {
            const MidiDev *d = &midi_bus.dev[i];
            ui_rframe(rx2 + 14 * S, yy, rw2 - 28 * S, ih, 10 * S, C_LINE, C_HEAD);
            char t[8]; int_to_str(t, sizeof t, i + 1); uint32_t bs = 24 * S, by3 = yy + (ih - bs) / 2;
            ui_rframe(rx2 + 26 * S, by3, bs, bs, bs / 2, C_LINE, C_FIELD);                      /* 모든 번호 같은 모양 (1번만 파랗게 채워 '고른 기계'로 오해하던 것) */
            text_f(SF, 1, rx2 + 26 * S + (bs - ui_sw(t)) / 2, ui_sty(by3, bs), t, C_TEXT, 0xFF, bs, 1);
            uint32_t tx = rx2 + 26 * S + bs + 12 * S;
            text_clip(tx, yy + 8 * S, mt_kind_name(d, i), C_TEXT, 0xFF, rw2 - (tx - rx2) - 90 * S);
            text_f(SF, 1, tx, yy + 10 * S + LH, d->kind == MDK_MT32 ? nt("자체 리버브의 세기만 조절", "only the reverb level is adjusted", "リバーブ量のみ調整") : d->kind == MDK_USB ? nt("바깥 기계 — 이펙트는 그 기계 몫", "external - it does its own effects", "外部機器 — 機器側で処理") : d->kind == MDK_FILE ? nt("녹음기 — 이펙트 없음", "recorder - no effects", "録音 — エフェクトなし") : mt_kind_sub(d, i), C_MUTED, 0xFF, rw2 - (tx - rx2) - 90 * S, 1);
            const char *ap = d->kind == MDK_FILE ? "" : nt("적용", "applied", "適用"); text_f(SF, 1, rx2 + rw2 - 26 * S - ui_sw(ap), ui_sty(yy, ih), ap, C_OK, 0xFF, ui_sw(ap) + 4, 1);
        }
        if (!midi_bus.n) text_f(SF, 1, rx2 + 22 * S, yy, nt("꽂힌 기계가 없습니다", "No devices plugged", "接続機器なし"), C_FAINT, 0xFF, rw2 - 44 * S, 1);
        text_f(SF, 1, rx2 + 22 * S, y0 + hh - SLH - 18 * S, nt("효과를 끄면 CPU를 조금 덜 씁니다. 느린 PC에서 소리가 끊기면 끄세요.", "Effects off saves a little CPU; try it if sound stutters.", "エフェクトをオフにするとCPU負荷が少し減ります。"), C_FAINT, 0xFF, rw2 - 44 * S, 1);
        return;
    }
    /* ---- 기계 ---- */
    int left = mt_two_out();                            /* OUT 단자가 둘(본체 + SFG)일 때만 단자 알약 줄 */
    if (!left) mt_zone = 1;
    if (left) {
        int pt[8], n = mt_ports(pt);
        if (mt_port >= n) mt_port = n - 1;
        uint32_t x = X, hh = SLH + 10 * S;
        for (int i = 0; i < n; i++) {
            int p = pt[i], foc = mt_zone == 0 && mt_port == i, sel = mt_port == i;
            char nm[56]; mt_jack_name(p, nm, sizeof nm);
            uint32_t w = ui_sw(nm) + 24 * S;
            if (x + w > X + W) break;
            ui_rframe(x, y0, w, hh, hh / 2, foc ? C_ACC : sel ? C_MUTED : C_LINE, sel ? C_ROWSEL : C_HEAD);
            text_f(SF, 1, x + 12 * S, ui_sty(y0, hh), nm, p == MTP_NONE ? C_FAINT : C_TEXT, 0xFF, w, 1);
            hit_add(hc_mt, 0, i, 0, 0, x, y0, w, hh);
            x += w + 6 * S;
        }
        y0 += hh + 10 * S;
    }
    uint32_t rx = X, rw = W, yy = y0;
    {   /* 제목 줄: 꽂힌 기계 · 안내 · + 기계 꽂기 Ins */
        char hd[96]; int sp = mt_sel_out();
        copy_str(hd, sizeof hd, nt("꽂힌 기계", "Plugged devices", "接続中の機器"));
        if (left && sp != MDP_ANY) { char jn[40]; mt_jack_name(sp, jn, sizeof jn); str_append(hd, sizeof hd, " \xC2\xB7 "); str_append(hd, sizeof hd, jn); }
        uint32_t th = LH + 14 * S;
        uint32_t hw = text_f(&font_head, 1, rx + 4 * S, yy + (th - LH) / 2, hd, C_TEXT, 0xFF, rw / 3, 1);
        const char *h2 = nt("롬·파일은 기계 줄에서 바로 고릅니다. 위에서부터 차례로 울립니다.", "Pick ROM/file right on the device row. Devices sound in order from the top.", "ROM・ファイルは各行で選びます。上から順に鳴ります。");
        text_f(SF, 1, rx + 4 * S + hw + 16 * S, ui_sty(yy, th), h2, C_MUTED, 0xFF, rw / 2, 1);
        if (midi_bus.n < MIDI_DEV_MAX) {
            const char *bt = nt("+ 기계 꽂기", "+ Plug device", "+ 機器接続"), *kc = "Ins"; uint32_t bw = ui_sw(bt) + ui_sw(kc) + 46 * S, bx = rx + rw - bw;
            ui_rframe(bx, yy, bw, th, 10 * S, C_ACC, C_ACC);
            text_f(SF, 1, bx + 18 * S, ui_sty(yy, th), bt, C_BACK, 0xFF, bw, 1); text_f(SF, 1, bx + 28 * S + ui_sw(bt), ui_sty(yy, th), kc, C_BACK, 0xFF, bw, 1);
            hit_add(hc_mt, 2, 0, 0, 0xD2, bx, yy, bw, th);
            mt_plug_x = bx; mt_plug_w = bw; mt_plug_y = yy + th;
        }
        yy += th + 10 * S;
    }
    { uint32_t cut = (S >= 2 ? 20 : 56) * S; if (rw > cut + 500 * S) { rx += cut / 2; rw -= cut; } }                          /* ↑↓ 단추를 뺀 만큼 기계 줄을 좁힘 (좌우는 여백, 창 폭은 그대로) */
    int idx[MIDI_DEV_MAX], n = mt_visible(idx), slots = n + (midi_bus.n < MIDI_DEV_MAX ? 1 : 0);
    if (mt_dev >= slots) mt_dev = slots - 1;
    if (mt_dev < 0) mt_dev = 0;
    uint32_t cap_h = SLH + 3 * S, ctl_h = LH + 8 * S, rh = 10 * S + cap_h + ctl_h + 10 * S, gap = 8 * S, lim = by - 14 * S - (SLH + 6 * S) * 2;
    /* 칸 너비 */
    if (rh < 2 * SLH + 36 * S + 12 * S) rh = 2 * SLH + 36 * S + 12 * S;                 /* 손잡이 높이 */
    uint32_t cg = 10 * S, pad = 8 * S, bs = 26 * S, ear = 22 * S, rwc = rw - 2 * ear;
    const char *bt_t[4] = { nt("시험음", "Test", "テスト"), "\xE2\x86\x91", "\xE2\x86\x93", nt("빼기", "Remove", "外す") };
    uint32_t bw_[4], btnw = 5 * S;                              /* ↑↓ 단추는 뺌 → 시험음 · 빼기 */
    for (int q = 0; q < 4; q++) { bw_[q] = q == 1 || q == 2 ? 0 : text_f(SF, 1, 0, 0, bt_t[q], 0, 0, 0, 0) + 20 * S; btnw += bw_[q]; }
    uint32_t cw_name = 140 * S, cw_sel = 190 * S, cw_ch = 116 * S, cw_vol = 86 * S, cw_pan = 86 * S, cw_led = 46 * S, cw_st = 100 * S;
    uint32_t tot = 2 * pad + bs + cw_name + cw_sel + cw_ch + cw_vol + cw_pan + cw_led + cw_st + btnw + 9 * cg;
    /* 좁은 화면 (노트북 1366): 글자 너비를 재서 칸 최소 너비를 정하고, 롬 칸 → 막대 → 채널 칸 → 레벨 막대 → 상태 칸 → 이름 칸 → 칸 사이 순으로 줄임 (글자가 칸 밖으로 나가지 않게) */
    uint32_t mn_ch = text_f(ft_cap(), 1, 0, 0, nt("모든 채널", "All channels", "全チャンネル"), 0, 0, 0, 0) + 40 * S, mn_vol = ui_sw(nt("소리 100", "Vol 100", "音量 100")) + 8 * S,
             mn_pan = ui_sw(nt("가운데", "center", "中央")) + 8 * S, mn_name = text_f(&font_bold, 1, 0, 0, "Roland MT-32", 0, 0, 0, 0) + 28 * S;
    if (mn_vol < 44 * S) mn_vol = 44 * S; if (mn_pan < 44 * S) mn_pan = 44 * S;
    uint32_t mn_sel = text_f(ft_cap(), 1, 0, 0, nt("SoundFont 없음", "SoundFont none", "SoundFontなし"), 0, 0, 0, 0) + 44 * S; if (mn_sel < (S >= 2 ? 100 : 150) * S) mn_sel = (S >= 2 ? 100 : 150) * S; mn_sel += 16 * S;
    while (tot > rwc && cw_sel > mn_sel) { cw_sel -= 5 * S; tot -= 5 * S; }
    while (tot > rwc && (cw_vol > mn_vol || cw_pan > mn_pan)) { if (cw_vol > mn_vol) { cw_vol -= 2 * S; tot -= 2 * S; } if (cw_pan > mn_pan) { cw_pan -= 2 * S; tot -= 2 * S; } }
    while (tot > rwc && cw_ch > mn_ch) { cw_ch -= 4 * S; tot -= 4 * S; }
    while (tot > rwc && cw_st > 84 * S) { cw_st -= 4 * S; tot -= 4 * S; }
    while (tot > rwc && cw_name > mn_name) { cw_name -= 4 * S; tot -= 4 * S; }
    while (tot > rwc && cw_sel > mn_sel - 20 * S && cw_sel > (S >= 2 ? 80 : 130) * S) { cw_sel -= 5 * S; tot -= 5 * S; }
    while (tot > rwc && cw_led > 24 * S) { cw_led -= 2 * S; tot -= 2 * S; }                         /* 소리 막대(이퀄라이저)는 마지막까지 지킴 — 좁히되 없애지 않음 */
    while (tot > rwc && cw_st > 62 * S) { cw_st -= 2 * S; tot -= 2 * S; }
    while (tot > rwc && cg > 5 * S) { cg -= S; tot -= 9 * S; }
    while (tot > rwc && cw_name > mn_name) { cw_name -= 3 * S; tot -= 3 * S; }
    while (tot > rwc && cw_ch > mn_ch) { cw_ch -= 3 * S; tot -= 3 * S; }
    while (tot > rwc && (cw_vol > 38 * S || cw_pan > 38 * S)) { if (cw_vol > 38 * S) { cw_vol -= 2 * S; tot -= 2 * S; } if (cw_pan > 38 * S) { cw_pan -= 2 * S; tot -= 2 * S; } }
    if (tot > rwc && cw_led) { tot -= cw_led + cg; cw_led = 0; }
    while (tot > rwc && cg > 4 * S) { cg -= S; tot -= 9 * S; }
    if (tot < rwc) cw_sel += rwc - tot;
    mt_plug_y = mt_plug_y ? mt_plug_y : yy;
    for (int k = 0; k < slots && yy + rh <= lim; k++, yy += rh + gap) {
        int foc = mt_zone == 1 && mt_dev == k;
        if (k == n) {                                   /* 빈 자리 */
            uint32_t eh = LH + 20 * S;
            ui_rframe(rx, yy, rw, eh, 12 * S, foc ? C_ACC : C_FAINT, foc ? C_ROWSEL : C_CARD);
            char t[8]; int_to_str(t, sizeof t, midi_bus.n + 1);
            ui_rframe(rx + pad, yy + (eh - bs) / 2, bs, bs, bs / 2, C_FAINT, C_CARD);
            text_f(SF, 1, rx + pad + (bs - ui_sw(t)) / 2, ui_sty(yy + (eh - bs) / 2, bs), t, C_FAINT, 0xFF, 20 * S, 1);
            text_clip(rx + pad + bs + 12 * S, ui_ty(yy, eh), nt("비어 있음 — Ins 또는 Enter 로 기계 꽂기 (MT-32/CM-32L · GM 음원 · SC-55 · USB MIDI · .mid 녹음기)", "Empty — Ins or Enter to plug a device (MT-32/CM-32L · GM · SC-55 · USB MIDI · .mid recorder)", "空き — Ins/Enterで機器を接続"), C_FAINT, 0xFF, rw - 60 * S);
            hit_add(hc_mt, 1, k, 0, 0, rx, yy, rw, eh);
            yy -= rh - eh;
            continue;
        }
        const MidiDev *d = &midi_bus.dev[idx[k]];
        int di = idx[k];
        if (foc) mt_ring(rx, yy, rw, rh, 8 * S, C_RACK); else ui_rframe(rx, yy, rw, rh, 8 * S, C_RACKIN, C_RACK);   /* 고른 줄: 2px 파랑 테두리 (랙 앞판) */
        {   /* 랙 귀 + 나사, 앞판 윗선 */
            uint32_t bd = foc ? 2 * S : S, eh = rh - 2 * bd;
            ui_rrect(rx + bd, yy + bd, ear, eh, 6 * S, C_RACKEAR); fe_fill(rx + bd + ear - 6 * S, yy + bd, 6 * S, eh, C_RACKEAR);
            ui_rrect(rx + rw - bd - ear, yy + bd, ear, eh, 6 * S, C_RACKEAR); fe_fill(rx + rw - bd - ear, yy + bd, 6 * S, eh, C_RACKEAR);
            mt_screw(rx + bd + ear / 2, yy + 14 * S); mt_screw(rx + bd + ear / 2, yy + rh - 14 * S);
            mt_screw(rx + rw - bd - ear / 2, yy + 14 * S); mt_screw(rx + rw - bd - ear / 2, yy + rh - 14 * S);
            fe_fill(rx + bd + ear, yy + bd, rw - 2 * (bd + ear), S, C_RACKHI);
        }
        hit_add(hc_mt, 1, k, 0, 0, rx, yy, rw, rh);
        uint32_t cx = rx + ear + pad + S, cy = yy + (rh - cap_h - ctl_h) / 2, cmid = yy + rh / 2;
        { char t[8]; int_to_str(t, sizeof t, di + 1); ui_rframe(cx, cmid - bs / 2, bs, bs, bs / 2, foc ? C_ACC : C_KNOBIN, C_RACKNUM);
          text_f(SF, 1, cx + (bs - ui_sw(t)) / 2, ui_sty(cmid - bs / 2, bs), t, foc ? C_ACC : C_MUTED, 0xFF, bs, 1); cx += bs + cg; }
        ui_rrect(cx, yy + 12 * S, cw_name, rh - 24 * S, 6 * S, C_LCD);                      /* 초록 LCD 이름창 */
        text_f(&font_bold, 1, cx + 9 * S, yy + (rh - LH - SLH - 2 * S) / 2, mt_kind_name(d, di), d->mute ? C_FAINT : C_LCDTX, 0xFF, cw_name - 9 * S, 1);
        text_f(SF, 1, cx + 9 * S, yy + (rh - LH - SLH - 2 * S) / 2 + LH + 2 * S, mt_kind_sub(d, di), d->mute ? C_FAINT : C_LCDSUB, 0xFF, cw_name - 9 * S, 1);
        cx += cw_name + cg;
        {   /* 롬·파일 칸 */
            char cur[64]; int dim; mt_sel_text(d, cur, sizeof cur, &dim);
            text_f(ft_cap(), 1, cx, cy, mt_field_cap(d), C_MUTED, 0xFF, cw_sel, 1);
            uint32_t fy = cy + cap_h, open = dd_on && dd_id == 5103 && mt_dev == k;
            if (open || (foc && mt_col == 0)) mt_ring(cx, fy, cw_sel, ctl_h, 6 * S, C_RACKIN); else ui_rframe(cx, fy, cw_sel, ctl_h, 6 * S, C_KNOBIN, C_RACKIN);
            text_f(ft_cap(), 1, cx + 10 * S, ui_sty(fy, ctl_h), cur, dim ? C_MUTED : C_TEXT, 0xFF, cw_sel - 36 * S, 1);
            text_f(SF, 1, cx + cw_sel - 20 * S, ui_sty(fy, ctl_h), open ? "\xE2\x96\xB2" : "\xE2\x96\xBC", C_MUTED, 0xFF, 14 * S, 1);
            hit_add(hc_mt, 4, k, 0, 0, cx, fy, cw_sel, ctl_h);
            if (foc) { mt_rom_x = cx; mt_rom_y = fy + ctl_h; mt_rom_w = cw_sel > 380 * S ? cw_sel : 380 * S; }
            cx += cw_sel + cg;
        }
        {   /* 받는 채널 칸 */
            char r[28]; mt_chan_text(d->rx_mask, r, sizeof r);
            if (d->rx_mask == 0xFFFF) copy_str(r, sizeof r, nt("모든 채널", "All", "全チャンネル"));
            text_f(ft_cap(), 1, cx, cy, nt("받는 채널", "Channels", "受信ch"), C_MUTED, 0xFF, cw_ch, 1);
            uint32_t fy = cy + cap_h, open = dd_on && dd_id == 5101 && mt_dev == k;
            if (open || (foc && mt_col == 1)) mt_ring(cx, fy, cw_ch, ctl_h, 6 * S, C_RACKIN); else ui_rframe(cx, fy, cw_ch, ctl_h, 6 * S, C_KNOBIN, C_RACKIN);
            text_f(ft_cap(), 1, cx + 10 * S, ui_sty(fy, ctl_h), r, C_TEXT, 0xFF, cw_ch - 34 * S, 1);
            text_f(SF, 1, cx + cw_ch - 20 * S, ui_sty(fy, ctl_h), "\xE2\x96\xBC", C_MUTED, 0xFF, 14 * S, 1);
            hit_add(hc_mt, 6, k, 0, 0, cx, fy, cw_ch, ctl_h);
            if (foc) { mt_ch_x = cx; mt_ch_y = fy + ctl_h; mt_ch_w = cw_ch > 300 * S ? cw_ch : 300 * S; }
            cx += cw_ch + cg;
        }
        {   /* 소리 · 좌우 손잡이 (시안: 이름표 / 돌리는 손잡이 / 값) */
            uint32_t kh = SLH + 2 * S + 36 * S + 2 * S + SLH, ky0 = yy + (rh - kh) / 2, kcy = ky0 + SLH + 2 * S + 18 * S;
            for (int q = 0; q < 2; q++) {
                uint32_t cwk = q ? cw_pan : cw_vol; int col = q ? 3 : 2, on = foc && mt_col == col;
                char cap[16], val[40], z[8];
                copy_str(cap, sizeof cap, q ? nt("좌우", "L/R", "左右") : nt("소리", "Vol", "音量"));
                if (!q) { copy_str(val, sizeof val, nt("소리 ", "Vol ", "音量 ")); int_to_str(z, sizeof z, d->vol); str_append(val, sizeof val, z); }
                else if (!d->pan) copy_str(val, sizeof val, nt("가운데", "center", "中央"));
                else { int_to_str(z, sizeof z, d->pan < 0 ? -d->pan : d->pan); copy_str(val, sizeof val, d->pan < 0 ? "L " : "R "); str_append(val, sizeof val, z); }
                if (on && mt_edit) str_append(val, sizeof val, " \xE2\x97\x80\xE2\x96\xB6");
                if (on) ui_rframe(cx - 3 * S, yy + 5 * S, cwk + 6 * S, rh - 10 * S, 8 * S, C_ACC, C_RACK);
                uint32_t w1 = ui_sw(cap), w2 = ui_sw(val);
                text_f(SF, 1, cx + (cwk > w1 ? (cwk - w1) / 2 : 0), ky0, cap, on ? C_ACC : C_MUTED, 0xFF, cwk, 1);
                mt_knob(cx + cwk / 2, kcy, q ? (d->pan + 100) / 2 : d->vol, !q && d->mute);
                text_f(SF, 1, cx + (cwk > w2 ? (cwk - w2) / 2 : 0), kcy + 18 * S + 2 * S, val, C_TEXT, 0xFF, cwk, 1);
                hit_add(hc_mt, q ? 8 : 7, k, 0, 0, cx - 2 * S, yy + 4 * S, cwk + 4 * S, rh - 8 * S);
                cx += cwk + cg;
            }
        }
        {   /* 소리 나는 정도: 시안의 올라가는 막대 6개 (폭 5, 사이 2, 높이 8·14·20·24·12·6 이 가득일 때) */
            uint32_t fl = d->lvl && !d->mute ? (uint32_t)d->lvl : 0; if (fl > 16384) fl = 16384;
            static const uint8_t hb[6] = { 8, 14, 20, 24, 12, 6 };
            uint32_t lw = 5 * S, lg = 2 * S, base = cmid + 12 * S;
            for (int q = 0; q < 6 && cw_led; q++) {
                uint32_t bh3 = fl ? hb[q] * S * fl / 16384 : 0; uint8_t bc = C_OK;
                if (bh3 < 4 * S) { bh3 = hb[q] > 12 ? 6 * S : 4 * S; bc = C_LINE; }
                else if (q == 3 && fl > 14000) bc = C_WARN;
                fe_fill(cx + (uint32_t)q * (lw + lg), base - bh3, lw, bh3, bc);
            }
            cx += cw_led ? cw_led + cg : 0;
        }
        {   /* 상태: LED 램프 + 아래 글자 */
            char st[40]; uint32_t col; mt_short_status(d, di, st, sizeof st, &col);
            uint32_t w3 = ui_sw(st); if (w3 > cw_st) w3 = cw_st;
            mt_led(cx + cw_st / 2, cmid - 10 * S, (uint8_t)col);
            text_f(SF, 1, cx + (cw_st > w3 ? (cw_st - w3) / 2 : 0), cmid + 2 * S, st, col, 0xFF, cw_st, 1);
            cx += cw_st + cg;
        }
        {   /* 줄 끝 단추 (시안): 높이 26, 얇은 빛 줄, 시험음 · [↑ ↓ 한 덩어리] · 빼기 */
            uint32_t bh = 26 * S, byy = cmid - bh / 2 - S, x = cx;
            for (int q = 0; q < 4; q++) {
                if (q == 1 || q == 2) continue;                                                   /* ↑↓ 단추 없음 */
                int en = q == 0 ? d->kind != MDK_USB && d->kind != MDK_FILE : q == 1 ? k > 0 : q == 2 ? k + 1 < n : 1;
                int hot = foc && mt_col == 4 + q;
                uint32_t r = q == 1 ? 0 : q == 2 ? 0 : 6 * S;
                ui_rrect(x, byy + S, bw_[q], bh, 6 * S, C_RACKIN);                                  /* 아래 그림자 */
                ui_rrect(x, byy, bw_[q], bh, 6 * S, hot ? C_ACC : C_RACKIN);                         /* 테두리 */
                ui_rrect(x + S, byy + S, bw_[q] - 2 * S, bh - 2 * S, 5 * S, en ? C_RACKBTN : C_RACK);
                fe_fill(x + 5 * S, byy + S, bw_[q] - 10 * S, S, en ? C_KNOB : C_RACKEAR);             /* 윗 빛 줄 */
                (void)r;
                if (q == 2) fe_fill(x, byy + 4 * S, S, bh - 8 * S, C_RACKIN);                         /* ↑↓ 가운데 선 */
                { uint32_t tw = ui_sw(bt_t[q]); text_f(SF, 1, x + (bw_[q] > tw ? (bw_[q] - tw) / 2 : 0), ui_sty(byy, bh), bt_t[q], en ? C_TEXT : C_FAINT, 0xFF, bw_[q], 1); }
                if (en) hit_add(hc_mt, 9, k, q, 0, x, byy, bw_[q], bh);
                x += bw_[q] + (q == 1 ? 0 : 5 * S);
            }
        }
    }
    {   /* 고른 줄의 자세한 상태 + 롬 파일 이름 */
        int idx2[MIDI_DEV_MAX], n2 = mt_visible(idx2);
        if (mt_dev < n2) {
            const MidiDev *d = &midi_bus.dev[idx2[mt_dev]]; char st[160]; uint32_t col;
            mt_status_text(d, idx2[mt_dev], st, sizeof st, &col);
            uint32_t ty = yy + 2 * S; if (ty + SLH < by) text_clip(rx + 6 * S, ty, st, col, 0xFF, rw - 12 * S);
            if (d->kind == MDK_MT32) { int c = rs_mt_cur(d); if (c >= 0 && ty + LH + SLH + 4 * S < by) { char t[200]; copy_str(t, sizeof t, nt("파일: ", "Files: ", "ファイル: ")); str_append(t, sizeof t, rs_mt[c].files); str_append(t, sizeof t, "  +  "); str_append(t, sizeof t, rs_mt[c].pcm); text_f(SF, 1, rx + 6 * S, ty + LH + 4 * S, t, C_MUTED, 0xFF, rw - 12 * S, 1); } }
        }
    }
    /* 아래: 단추 둘 + 롬 폴더 안내 */
    {
        static const struct { const char *ko, *en, *ja; int key; } bt[2] = {
            { "MIDI 믹서 (채널별 음량)…", "MIDI Mixer (volume per channel)...", "MIDIミキサー (チャンネル別音量)...", 0x32 }, { "MIDI 입력 장치…", "MIDI input device...", "MIDI入力機器...", 0 } };
        uint32_t x = X;
        for (int i = 0; i < 2; i++) {
            const char *t = nt(bt[i].ko, bt[i].en, bt[i].ja); uint32_t w = ui_sw(t) + 36 * S;
            int en = i == 0 ? midi_bus.n > 0 : 1;
            ui_rframe(x, by, w, btn_h, 10 * S, en ? C_LINE : C_FIELD, C_HEAD);
            text_f(SF, 1, x + 18 * S, ui_sty(by, btn_h), t, en ? C_TEXT : C_FAINT, 0xFF, w, 1);
            if (en) hit_add(hc_mt, i == 0 ? 2 : 10, i == 0 ? 5 : 0, 0, bt[i].key, x, by, w, btn_h);
            if (i == 1) mt_in_x = x;
            x += w + 10 * S;
        }
        char f1[140]; copy_str(f1, sizeof f1, nt("롬 폴더: USB의 MidiSound\\MT32 \xC2\xB7 SC55 \xC2\xB7 SoundFont", "ROM folders: USB MidiSound\\MT32 \xC2\xB7 SC55 \xC2\xB7 SoundFont", "ROMフォルダ: USB の MidiSound\\MT32 \xC2\xB7 SC55 \xC2\xB7 SoundFont"));
        const char *f2 = nt("다시 찾기", "rescan", "再検索"); uint32_t w2 = ui_sw(f2), kw = ui_sw("F5") + 16 * S, w1 = ui_sw(f1), tx = X + W - w2 - kw - 14 * S - w1 - 10 * S;
        if (tx > x) {
            text_f(SF, 1, tx, ui_sty(by, btn_h), f1, C_MUTED, 0xFF, w1 + 4, 1);
            uint32_t kx = tx + w1 + 10 * S, kh = SLH + 8 * S, ky = by + (btn_h - kh) / 2;
            ui_rframe(kx, ky, kw, kh, 6 * S, C_LINE, C_HEAD); text_f(SF, 1, kx + 8 * S, ui_sty(ky, kh), "F5", C_TEXT, 0xFF, kw, 1);
            text_f(SF, 1, kx + kw + 8 * S, ui_sty(by, btn_h), f2, C_MUTED, 0xFF, w2 + 4, 1);
        }
    }
    if (dd_on && dd_id == 5103) dd_draw_at(mt_rom_x, mt_rom_y + 2 * S, mt_rom_w, bottom);
    if (dd_on && dd_id == 5101) dd_draw_at(mt_ch_x, mt_ch_y + 2 * S, mt_ch_w, bottom);
    if (dd_on && dd_id == 5100) dd_draw_at(mt_plug_x + mt_plug_w > X + W * 45 / 100 ? mt_plug_x + mt_plug_w - W * 45 / 100 : mt_plug_x, mt_plug_y + 4 * S, W * 45 / 100, bottom);
    if (dd_on && dd_id == 5104) dd_draw_at(mt_in_x, by - 4 * S, W * 45 / 100, bottom);
}

static void miditab_key(int key)
{
    int pt[8], np = mt_ports(pt);
    int idx[MIDI_DEV_MAX], n = mt_visible(idx), slots = n + (midi_bus.n < MIDI_DEV_MAX ? 1 : 0);
    int shift = (mods & 0x03) != 0, di = mt_dev < n ? idx[mt_dev] : -1;
    menu_dirty = 1;
    if (key == 0x02 || key == 0x1A) { if (mt_sub != 0) mt_set_sub(0); return; }                       /* 1 · [ */
    if (key == 0x03) { if (mt_sub != 1) mt_set_sub(1); return; }                                      /* 2 */
    if (key == 0x0F || key == 0xD1 || key == 0xC9) { mt_set_sub(!mt_sub); return; }                   /* Tab · PgDn · PgUp */        /* [ ] = 하위 탭 */
    if (key == 0x1B) { if (mt_sub != 1) mt_set_sub(1); return; }
    if (key == 0x3F) { if (P.usb_rescan) P.usb_rescan(); rs_scanned = 0; rs_scan(); toast(nt("롬 폴더를 다시 찾았습니다", "ROM folders rescanned", "ROMフォルダを再検索しました"), 0); for (int q = 0; q < MT_N; q++) if (mt_st[q] != 1) mt_st[q] = 0; if (sc55_state < 0) sc55_state = 0; return; }   /* F5 */
    if (mt_sub == 1) {                                  /* 공간 음향 */
        switch (key) {
        case 0xC8: if (mt_fx > 0) mt_fx--; return;
        case 0xD0: if (mt_fx < 5) mt_fx++; return;
        case 0xCB: case 0xCD: case 0x0D: case 0x4E: case 0x0C: case 0x4A: {
            int up = key == 0xCD || key == 0x0D || key == 0x4E;
            if (mt_fx >= 1 && mt_fx <= 3) { int id = mt_fx == 1 ? 161 : mt_fx == 2 ? 163 : 162; slide_change(id, up ? 1 : -1); if (!fx_on) { fx_on = 1; fx_apply(); } }
            else if (key == 0xCB || key == 0xCD) { if (mt_fx == 4 || mt_fx == 5) mt_fx = mt_fx == 4 ? 5 : 4; }
            return; }
        case 0x1C: case 0x9C: case 0x39:
            if (mt_fx == 0) { fx_on = !fx_on; fx_apply(); settings_changed(); }
            else if (mt_fx == 4) { fx_rev = fx_cho = fx_len = 100; fx_apply(); settings_changed(); }
            else if (mt_fx == 5) mt_test(-1);
            else if (mt_fx >= 1 && mt_fx <= 3) slide_change(mt_fx == 1 ? 161 : mt_fx == 2 ? 163 : 162, 0);   /* Enter = 100 % */
            return;
        case 0x14: mt_test(-1); return;                 /* T */
        default: return;
        }
    }
    if (mt_edit) {                                      /* 소리·좌우 칸 조절 중: ←/→ 값, Esc·Enter 나옴 */
        if (key == 0x01 || key == 0x1C || key == 0x9C || key == 0x39) { mt_edit = 0; return; }
        if (di >= 0 && (key == 0xCB || key == 0xCD)) {
            MidiDev *d = &midi_bus.dev[di]; int up = key == 0xCD;
            if (mt_col == 2) { int v = d->vol + (up ? 5 : -5); d->vol = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); if (up) d->mute = 0; }
            else { int v = d->pan + (up ? 10 : -10); if (v > -8 && v < 8) v = 0; d->pan = (int8_t)(v < -100 ? -100 : v > 100 ? 100 : v); }
            settings_changed();
        }
        return;
    }
    switch (key) {
    case 0xCB:
        if (mt_zone == 0) { mt_port = (mt_port + np - 1) % np; return; }
        if (mt_col > 0) { mt_col--; if (mt_col == 6 || mt_col == 5) mt_col = 4; } else if (mt_two_out()) mt_zone = 0;
        return;
    case 0xCD:
        if (mt_zone == 0) { mt_port = (mt_port + 1) % np; return; }
        if (di >= 0 && mt_col < 7) { mt_col++; if (mt_col == 5) mt_col = 7; }
        return;
    case 0xC8: case 0xD0: {
        int d = key == 0xC8 ? -1 : 1;
        if (mt_zone == 0) { if (d > 0) mt_zone = 1; return; }
        if (shift && di >= 0) {                         /* 차례 바꾸기 (THRU 차례 = 음원 인스턴스 차례) */
            int k2 = mt_dev + d; if (k2 < 0 || k2 >= n) return;
            int dj = idx[k2]; MidiDev t = midi_bus.dev[di]; midi_bus.dev[di] = midi_bus.dev[dj]; midi_bus.dev[dj] = t;
            mt_dev = k2; mt_after_change(); return;
        }
        if (d < 0 && mt_dev == 0 && mt_two_out()) { mt_zone = 0; return; }
        if (slots) { int k2 = mt_dev + d; if (k2 >= 0 && k2 < slots) mt_dev = k2; }
        if (mt_dev >= n) mt_col = 0;
        return;
    }
    case 0x1C: case 0x9C: case 0x39:
        if (mt_zone == 0) {
            int p = pt[mt_port];
            if (p >= MTP_IN && p != MTP_NONE && P.midi_read) {   /* USB MIDI 장치를 이 IN 단자로 (이미 이 단자면 끔) */
                int q = p - MTP_IN;
                if (midi_in && mt_in_port() == q) midi_in = 0; else { midi_in = 1; midi_in_port = q; }
                settings_changed();
            } else mt_zone = 1;
            return;
        }
        if (di < 0) mt_dd_plug();
        else if (mt_col == 0) mt_dd_sel(di);
        else if (mt_col == 1) mt_dd_setting(di);
        else if (mt_col <= 3) mt_edit = 1;              /* 소리·좌우: Enter 로 들어가 ←/→ 로 조절 */
        else mt_row_op(mt_dev, mt_col - 4);             /* 시험음 · ↑ · ↓ · 빼기 */
        return;
    case 0x14: mt_test(di); return;                     /* T: 시험음 */
    case 0xD2: mt_dd_plug(); return;                    /* Ins: 꽂기 */
    case 0xD3: case 0x0E:                               /* Del: 빼기 */
        if (di < 0) return;
        if (midi_bus.dev[di].kind == MDK_FILE) midi_flush();
        mt_test_stop();
        midibus_unplug(di); if (mt_dev > 0 && mt_dev >= n - 1) mt_dev--;
        mt_after_change(); return;
    case 0x32: open_midimix(); return;                  /* M: MIDI 믹서 */
    case 0x17: mt_open_in(); return;                    /* I: MIDI 입력 장치 */
    case 0x18: if (msx_dev(M, MDEV_MIDI_HW) && !midi_int) { props_change(116, 1); toast(nt("본체 MIDI 를 켰습니다", "Main unit MIDI on", "本体MIDIをオン"), 0); } return;   /* O */
    case 0x2B: if (di >= 0) { midi_bus.dev[di].pan = 0; settings_changed(); } return;
    default: return;
    }
}
