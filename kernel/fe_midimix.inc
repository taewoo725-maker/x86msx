/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_midimix.inc - 5.2.53 MIDI 믹서(채널 라우팅) 창 (창 19). MIDI 개편 3단계 ②-2, 디자인 캔버스 "MIDI 믹서" 아트보드대로.
 * 디자인 A안 콘솔 스트립 — 기계마다 세로 띠 (받는 채널 4×4 불, 좌우, 페이더 + 계기, 켜기). 받음 = 파란 칸, Note On 이 오면 노란 불.
 * 값은 기계(MidiDev)에 바로 들어가고 본체가 소리 섞을 때 씀 (소리 믹서의 MIDI 띠는 전체 마스터). 창은 MSX 를 멈추지 않음 (live_mixer). */
static int mm_row, mm_col;                              /* 고른 기계(띠)와 칸: 0 음량, 1 좌우, 2 켜기, 3~18 채널 1~16 */
#define MM_COLS 19
static uint32_t mm_cnt[MIDI_DEV_MAX][16], mm_lit[MIDI_DEV_MAX][16];   /* 채널 불: 마지막 Note On 수, 켜진 시각 */

static void open_midimix(void) { if (mm_row >= midi_bus.n) mm_row = 0; if (mm_col < 0 || mm_col >= MM_COLS) mm_col = 0; open_dialog(19); }

/* 마우스 — a = 기계, b = 칸 (0 음량 막대, 1 좌우 막대, 2 켜기, 3~18 채널), c·k = 막대 자리 (음량: 위 y·높이, 좌우: 왼쪽 x·폭) */
static void hc_mm(const UiHit *h, int ev)
{
    int i = (int)h->a, col = h->b;
    if (i < 0 || i >= midi_bus.n) return;
    if (mm_row != i || mm_col != col) { mm_row = i; mm_col = col; menu_dirty = 1; }
    if (ev == 0) return;
    MidiDev *d = &midi_bus.dev[i];
    if (col == 0 && h->k) { uint32_t ty = (uint32_t)h->c, bh = h->k, py = ptr_y < ty ? ty : ptr_y >= ty + bh ? ty + bh - 1 : ptr_y; d->vol = (uint8_t)((ty + bh - py) * 100 / bh); d->mute = 0; }
    else if (col == 1 && h->k) { uint32_t bx = (uint32_t)h->c, bw = h->k, px = ptr_x < bx ? bx : ptr_x > bx + bw ? bx + bw : ptr_x; int v = (int)((px - bx) * 200 / bw) - 100; if (v > -6 && v < 6) v = 0; d->pan = (int8_t)v; }
    else if (ev == 1 && col == 2) d->mute = !d->mute;
    else if (ev == 1 && col >= 3) d->rx_mask ^= (uint16_t)(1u << (col - 3));
    else return;
    settings_changed(); menu_dirty = 1;
}

static const char *mm_dev_name(const MidiDev *d, char *buf, int max)
{
    copy_str(buf, max, d->kind == MDK_GM ? (d->opt ? "GM-FM" : "GM-SF") : d->kind == MDK_GS ? "SC-55" : midi_dev_class[d->kind].short_name);
    return buf;
}
static const char *mm_dev_full(const MidiDev *d, char *buf, int max)
{
    if (d->kind == MDK_MT32) copy_str(buf, max, d->opt ? "Roland CM-32L (munt)" : "Roland MT-32 (munt)");
    else if (d->kind == MDK_GS && gmi_mode((int)(d - midi_bus.dev)) == GM_SC55) { copy_str(buf, max, "Roland SC-55 · Nuked-SC55 · "); str_append(buf, max, sc55_info); }   /* 5.2.60 */
    else if (d->kind == MDK_GS) { copy_str(buf, max, "Roland SC-55 · GS · "); str_append(buf, max, gs_slot() && gs_state == 1 ? gs_name : sf_state == 1 ? sf_name : "FM"); }   /* 5.2.57 */
    else if (d->kind == MDK_GM) { copy_str(buf, max, nt("GM 음원 · ", "GM module · ", "GM音源 · ")); str_append(buf, max, d->opt ? nt("내장 FM", "built-in FM", "内蔵FM") : (sf_state == 1 ? sf_name : nt("SoundFont 없음 → FM", "no SoundFont → FM", "SoundFontなし → FM"))); }
    else if (d->kind == MDK_USB) { copy_str(buf, max, nt("USB MIDI 장치", "USB MIDI device", "USB MIDI機器")); if (midi_out_name[0] && usb_midi_ok) { str_append(buf, max, " · "); str_append(buf, max, midi_out_name); } else str_append(buf, max, nt(" (없음)", " (not found)", " (なし)")); }
    else copy_str(buf, max, nt(".mid 녹음기 (Audio 폴더)", ".mid recorder (Audio folder)", ".mid録音 (Audioフォルダ)"));
    if (d->port == MDP_SFG) str_append(buf, max, nt(" · SFG OUT 단자", " · SFG OUT jack", " · SFG OUT端子"));
    else if (d->port == MDP_MSXMIDI) str_append(buf, max, nt(" · MSX-MIDI OUT 단자", " · MSX-MIDI OUT jack", " · MSX-MIDI OUT端子"));
    return buf;
}

/* 5.2.66 (마스터님 결정: 디자인 A안 콘솔 스트립) — 꽂힌 기계마다 세로 띠: 번호·이름·설명, LCD, 받는 채널 불 4×4, 좌우, 페이더 + LED 계기, 값·켜기.
 * 모든 기계 소리의 합은 소리 믹서(F12)의 MIDI 채널 (여기에는 합침 띠를 두지 않음) */
static void mm_lcd_text(const MidiDev *d, int di, char *lc, int max)
{
    lc[0] = 0;
    if (d->kind == MDK_MT32) { copy_str(lc, max, d->opt ? "CM-32L " : "MT-32 "); { int sl = mt_slot_of(di); if (sl >= 0 && mt_st[sl] == 1) str_append(lc, max, "READY"); } }
    else if (d->kind == MDK_GM) copy_str(lc, max, gmi_mode(di) == GM_SF2 ? "GM SoundFont" : "GM FM");
    else if (d->kind == MDK_GS && gmi_mode(di) == GM_SC55) { char t[17]; sc55w_lcd_text(t); copy_str(lc, max, t); }
    else if (d->kind == MDK_GS) copy_str(lc, max, gmi_mode(di) == GM_SF2 ? "SC-55 GS" : "SC-55 FM");
    else if (d->kind == MDK_USB) copy_str(lc, max, usb_midi_ok ? "USB OUT" : "USB --");
    else copy_str(lc, max, midi_len ? "REC" : "REC STBY");
    if (d->lcd_seq && d->kind != MDK_GS) { }       /* 곡이 보낸 글자는 MIDI 탭 카드에서 */
}
static UiRect midipop_rect(void)                       /* 띠 4개가 들어가는 폭, 화면 아래 (높이는 페이더가 보이게 넉넉히) */
{
    uint32_t m = 10 * S, w = out_w > 2 * m ? out_w - 2 * m : out_w, h = D.h * 3 / 5;
    uint32_t want = 4 * 240 * S + 3 * 14 * S + 2 * 14 * S + 8 * S;
    if (want < w) w = want;
    uint32_t minh = ui_head_h() + 2 * SLH + 3 * LH + 4 * (SLH + 10 * S) + 2 * SLH + 220 * S + ui_cmdbar_h();
    if (h < minh) h = minh;
    if (h > D.h - 2 * m) h = D.h - 2 * m;
    UiRect r = { x0 + m + (out_w > 2 * m ? (out_w - 2 * m - w) / 2 : 0), D.h - h - m, w, h };
    return r;
}
static void draw_midimix(void)
{
    UiRect o;
    char sub[120];
    copy_str(sub, sizeof sub, nt("모든 기계 소리는 소리 믹서(F12)의 MIDI 채널로", "All device sound goes to the MIDI channel of the mixer (F12)", "全機器の音はミキサー(F12)のMIDIへ"));
    o = midipop_rect();                                 /* 소리 믹서(F12)처럼 화면 아래 팝업 — MSX 는 위에 계속 보임 */
    UiRect b = ui_card(o.x, o.y, o.w, o.h, nt("MIDI 믹서", "MIDI Mixer", "MIDIミキサー"), sub, 0);
    hit_add(hc_none, 0, 0, 0, 0, o.x, o.y, o.w, o.h);
    uint32_t cy = o.y + o.h - ui_cmdbar_h() - 2 * S, now = P.ms();
    uint32_t X = b.x + 14 * S, Y = b.y + 8 * S, W = b.w > 28 * S ? b.w - 28 * S : b.w, H = cy > Y + 20 * S ? cy - Y - 10 * S : 100;
    uint32_t gap = 14 * S, sw = (W - gap * 3) / 4;       /* 띠 4개 (마스터님: 오른쪽 안내 칸은 필요 없음) */
    if (sw > 300 * S) sw = 300 * S;
    X += (W - (sw * 4 + gap * 3)) / 2;
    if (mm_row >= midi_bus.n) mm_row = midi_bus.n ? midi_bus.n - 1 : 0;
    for (int i = 0; i < MIDI_DEV_MAX; i++) {
        uint32_t sx = X + (uint32_t)i * (sw + gap);
        if (i >= midi_bus.n) {                          /* 빈 자리 */
            ui_rframe(sx, Y, sw, H, 10 * S, C_LINE, C_BACK);
            char t[8]; int_to_str(t, sizeof t, i + 1);
            uint32_t mid = Y + H / 2;
            text_f(SF, 1, sx + (sw - ui_sw(t)) / 2, mid - SLH - 4 * S, t, C_FAINT, 0xFF, sw, 1);
            const char *e = nt("+ 기계 꽂기 (속성 ▶ MIDI)", "+ Plug a device (Properties > MIDI)", "+ 機器を接続 (プロパティ▶MIDI)");
            uint32_t ew = ui_sw(e); text_f(SF, 1, sx + (sw > ew ? (sw - ew) / 2 : 4 * S), mid + 2 * S, e, C_FAINT, 0xFF, sw - 8 * S, 1);
            continue;
        }
        MidiDev *d = &midi_bus.dev[i];
        int foc = i == mm_row, on = !d->mute;
        ui_rrect(sx + 3 * S, Y + 4 * S, sw, H, 10 * S, C_SHADOW);
        ui_rframe(sx, Y, sw, H, 10 * S, foc ? C_ACC : C_LINE, foc ? C_ROWSEL : C_CARD);
        uint32_t ix = sx + 12 * S, iw = sw - 24 * S, y = Y + 12 * S;
        /* 번호 + 이름 */
        { char t[4]; int_to_str(t, sizeof t, i + 1); uint32_t bs = SLH + 4 * S;
          ui_rrect(ix, y, bs, bs, bs / 2, foc ? C_ACC : C_HEAD);
          text_f(SF, 1, ix + (bs - ui_sw(t)) / 2, ui_sty(y, bs), t, foc ? C_BACK : C_MUTED, 0xFF, bs, 1);
          char nm[24]; mm_dev_name(d, nm, sizeof nm);
          text_clip(ix + bs + 8 * S, ui_ty(y, bs), nm, on ? C_TEXT : C_MUTED, 0xFF, iw - bs - 8 * S);
          y += bs + 6 * S; }
        { char full[96]; text_f(SF, 1, ix, y, mm_dev_full(d, full, sizeof full), C_MUTED, 0xFF, iw, 1); y += SLH + 8 * S; }
        /* LCD */
        { char lc[40]; mm_lcd_text(d, i, lc, sizeof lc); uint32_t lh = SLH + 10 * S;
          ui_rframe(ix, y, iw, lh, 3 * S, C_LINE, C_BACK);
          text_f(SF, 1, ix + 8 * S, ui_sty(y, lh), lc, !on ? C_FAINT : d->kind == MDK_MT32 ? C_OK : d->kind == MDK_GS ? C_WARN : C_ACC, 0xFF, iw - 16 * S, 1);
          y += lh + 10 * S; }
        /* 받는 채널 4×4 */
        { uint32_t cg = 4 * S, cw = (iw - 3 * cg) / 4, chh = SLH + 6 * S;
          for (int c = 0; c < 16; c++) {
              uint32_t gx = ix + (uint32_t)(c % 4) * (cw + cg), gy = y + (uint32_t)(c / 4) * (chh + cg);
              int rx = (d->rx_mask >> c) & 1, sel = foc && mm_col == 3 + c;
              if (mm_cnt[i][c] != d->ch_on[c]) { mm_cnt[i][c] = d->ch_on[c]; mm_lit[i][c] = now ? now : 1; }
              int lit = rx && on && mm_lit[i][c] && now - mm_lit[i][c] < 400;
              uint8_t fill = lit ? C_WARN : rx ? (on ? C_ACC : C_FAINT) : C_FIELD;
              ui_rframe(gx, gy, cw, chh, 4 * S, sel ? C_TEXT : rx ? fill : C_LINE, fill);
              char t[4]; int_to_str(t, sizeof t, c + 1); uint32_t tw = ui_sw(t);
              text_f(SF, 1, gx + (cw > tw ? (cw - tw) / 2 : 0), ui_sty(gy, chh), t, rx ? C_BACK : C_FAINT, 0xFF, cw, 1);
              hit_add(hc_mm, i, 3 + c, 0, 0, gx, gy, cw, chh);
          }
          y += 4 * (chh + cg) + 8 * S; }
        /* 좌우 */
        { char t[12]; if (d->pan == 0) copy_str(t, sizeof t, "C"); else { copy_str(t, sizeof t, d->pan < 0 ? "L" : "R"); char q[8]; int_to_str(q, sizeof q, d->pan < 0 ? -d->pan : d->pan); str_append(t, sizeof t, q); }
          int sel = foc && mm_col == 1;
          text_f(SF, 1, ix, y, "L", C_MUTED, 0xFF, 20 * S, 1);
          text_f(SF, 1, ix + iw - ui_sw("R"), y, "R", C_MUTED, 0xFF, 20 * S, 1);
          uint32_t tw = ui_sw(t); text_f(SF, 1, ix + (iw - tw) / 2, y, t, sel ? C_TEXT : C_MUTED, 0xFF, iw, 1);
          y += SLH + 4 * S;
          uint32_t by = y + 4 * S;
          ui_rrect(ix, by, iw, 6 * S, 3 * S, C_FIELD); ui_rrect(ix + iw / 2, by, S, 6 * S, 0, C_LINE);
          uint32_t kx = ix + iw * (uint32_t)(d->pan + 100) / 200; ui_rrect(kx > 2 * S ? kx - 2 * S : kx, by - 4 * S, 4 * S, 14 * S, 2 * S, sel ? C_TEXT : C_ACC2);
          hit_add(hc_mm, i, 1, (int)ix, (uint16_t)iw, ix, by - 8 * S, iw, 22 * S);
          y = by + 18 * S; }
        /* 페이더 + LED 계기 */
        uint32_t bh_ = LH + 10 * S;                      /* 맨 아래 값 줄 */
        uint32_t fb = Y + H - 12 * S - bh_ - 10 * S;
        if (fb > y + 40 * S) {
            uint32_t fh = fb - y, tw = 8 * S, tx = sx + sw / 2 - tw - 6 * S;
            int sel = foc && mm_col == 0;
            for (int t = 0; t <= 4; t++) { uint32_t yy = y + fh - fh * (uint32_t)t / 4; ui_rrect(tx - 14 * S, yy > y + S ? yy - S : y, (t % 2 == 0) ? 8 * S : 5 * S, 2 * S, S, C_LINE); }
            ui_rrect(tx, y, tw, fh, tw / 2, C_FIELD);
            uint32_t fill = fh * (uint32_t)d->vol / 100;
            if (fill) ui_rrect(tx, y + fh - fill, tw, fill, tw / 2, on ? C_ACC : C_FAINT);
            int segs = (int)(fh / (6 * S)); if (segs > 24) segs = 24; if (segs < 6) segs = 6;
            uint32_t sh = fh / (uint32_t)segs, mx = tx + tw + 10 * S;
            int lv = on ? (int)((uint32_t)d->lvl * 1000 / 20000) : 0; if (lv > 1000) lv = 1000;
            int lit = lv * segs / 1000;
            for (int g = 0; g < segs; g++) { uint32_t yy = y + fh - (uint32_t)(g + 1) * sh; uint8_t col = g >= lit ? C_FIELD : g >= segs * 85 / 100 ? C_BAD : g >= segs * 65 / 100 ? C_WARN : C_OK; ui_rrect(mx, yy + S, 7 * S, sh > 2 * S ? sh - 2 * S : 1, S, col); }
            uint32_t kh = 14 * S, kw = sw * 3 / 10, ky = y + fh - fill; ky = ky > y + kh / 2 ? ky - kh / 2 : y;
            uint32_t kx = tx + tw / 2 - kw / 2;
            ui_rrect(kx + 2 * S, ky + 3 * S, kw, kh, 4 * S, C_SHADOW);
            ui_rrect(kx, ky, kw, kh, 4 * S, sel ? 15 : C_TEXT);
            ui_rrect(kx + 4 * S, ky + kh / 2 - S, kw - 8 * S, 2 * S, S, on ? C_ACC : C_FAINT);
            hit_add(hc_mm, i, 0, (int)y, (uint16_t)fh, sx, y, sw, fh);
        }
        /* 값 + 켜기 */
        { uint32_t vy = Y + H - 12 * S - bh_; char t[8]; int_to_str(t, sizeof t, d->vol);
          text_clip(ix, ui_ty(vy, bh_), t, on ? C_TEXT : C_FAINT, 0xFF, iw / 2);
          const char *bt = on ? "ON" : "OFF"; uint32_t bw = ui_sw(bt) + 30 * S, bx = ix + iw - bw;
          int sel = foc && mm_col == 2;
          ui_rframe(bx, vy, bw, bh_, bh_ / 2, sel ? C_TEXT : on ? C_OK : C_BAD, C_FIELD);
          ui_rrect(bx + 10 * S, vy + bh_ / 2 - 3 * S, 6 * S, 6 * S, 3 * S, on ? C_OK : C_BAD);
          text_f(SF, 1, bx + 20 * S, ui_sty(vy, bh_), bt, on ? C_OK : C_BAD, 0xFF, bw, 1);
          hit_add(hc_mm, i, 2, 0, 0, bx, vy, bw, bh_); }
    }
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, nt("←/→ 기계   ↑/↓ 음량   [ ] 좌우   1~0 Q~Y 채널   Space 켜기/끄기   A 모두  N 없음  D 기본값   Esc 닫기",
                                             "Left/Right device   Up/Down volume   [ ] pan   1-0 Q-Y channels 1-16   Space on/off   A all  N none  D default   Esc close",
                                             "←/→ 機器   ↑/↓ 音量   [ ] 左右   1~0 Q~Y ch1~16   Space オン/オフ   A 全部  N なし  D 既定   Esc 閉じる"));
}

static void mm_default(MidiDev *d) { d->rx_mask = d->kind == MDK_MT32 ? 0x03FE : 0xFFFF; }   /* MT-32 실기 기본 2~10 */

static void midimix_key(int key)
{
    if (!midi_bus.n) { if (key == 0x01 || key == 0x1C) dialog_back(); return; }
    if (mm_row >= midi_bus.n) mm_row = midi_bus.n - 1;
    MidiDev *d = &midi_bus.dev[mm_row];
    int chg = 1;
    static const uint8_t chk[16] = { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15 };   /* 1~9 0 = 채널 1~10, Q~Y = 11~16 */
    for (int c = 0; c < 16; c++) if (key == chk[c]) { d->rx_mask ^= (uint16_t)(1u << c); mm_col = 3 + c; settings_changed(); menu_dirty = 1; return; }
    switch (key) {
    case 0x01: dialog_back(); return;
    case 0xCB: if (mm_row > 0) mm_row--; chg = 0; break;                                      /* ←/→ 기계 (세로 띠) */
    case 0xCD: if (mm_row + 1 < midi_bus.n) mm_row++; chg = 0; break;
    case 0xC8: d->vol = (uint8_t)(d->vol + 5 > 100 ? 100 : d->vol + 5); d->mute = 0; mm_col = 0; break;   /* ↑/↓ 음량 */
    case 0xD0: d->vol = (uint8_t)(d->vol < 5 ? 0 : d->vol - 5); mm_col = 0; break;
    case 0x39: case 0x1C: case 0x9C: d->mute = !d->mute; mm_col = 2; break;                   /* Space/Enter: 켜기/끄기 */
    case 0x0D: case 0x4E: d->vol = (uint8_t)(d->vol + 5 > 100 ? 100 : d->vol + 5); d->mute = 0; mm_col = 0; break;   /* + */
    case 0x0C: case 0x4A: d->vol = (uint8_t)(d->vol < 5 ? 0 : d->vol - 5); mm_col = 0; break;                        /* - */
    case 0x1A: d->pan = (int8_t)(d->pan - 10 < -100 ? -100 : d->pan - 10); mm_col = 1; break;                        /* [ */
    case 0x1B: d->pan = (int8_t)(d->pan + 10 > 100 ? 100 : d->pan + 10); mm_col = 1; break;                          /* ] */
    case 0x2B: d->pan = 0; mm_col = 1; break;                                                                        /* \ */
    case 0x1E: d->rx_mask = 0xFFFF; break;                                                               /* A */
    case 0x31: d->rx_mask = 0; break;                                                                    /* N */
    case 0x20: mm_default(d); d->vol = 100; d->pan = 0; d->mute = 0; break;                               /* D */
    default: return;
    }
    if (chg) settings_changed();
    menu_dirty = 1;
}
