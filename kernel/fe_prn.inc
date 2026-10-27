/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_prn.inc - 3.3.0
 *   MENU3-01/02 프린터를 파일로: MSX 프린터 포트(0x90 스트로브·상태, 0x91 데이터)로 나온 바이트를 모아
 *     USB\Printer\PRN_####.txt(텍스트) 또는 .prn(원본)으로 저장. 5초 동안 새 바이트가 없거나 "인쇄 끝내기"(LCtrl+LShift+P)면 저장
 *   MENU3-04 메모리 불러오기: 파일을 MSX 메모리(지금 RAM으로 보이는 자리)에 넣고, 실행 주소가 있으면 거기서 실행
 *     BLOAD 파일(FE 머리: 시작·끝·실행 주소)이면 주소를 머리에서 가져옴
 */

/* prn_mode: 0 출력 안 함, 1 텍스트(.txt), 2 원본(.prn) (fe.c에 선언) */
static uint32_t prn_last;
static char prn_saved[48];                               /* 4.0.4: 방금 저장한 파일 이름 (조용해져 저절로 저장된 뒤 '인쇄 끝내기'를 눌러도 알려 줌) */

static void prn_forget(void) { prn_saved[0] = 0; }
static int prn_printing(void) { return prn_mode != 0 && prn_len != 0; }   /* 4.0.3: 프린터 아이콘 */
static void prn_flush(int user)
{
    if (!prn_len) { if (user) { if (prn_saved[0]) toast(T(S_PRN_SAVED), prn_saved); else toast(T(S_PRN_NONE), 0); } return; }
    uint32_t raw_len = prn_len;                         /* 4.0.5: 미리보기는 받은 그대로 */
    char path[160];
    const uint8_t *out = prn_buf;
    uint32_t n = prn_len;
    if (prn_mode != 2 && P.scratch && P.scratch_size >= (1u << 20)) {   /* 텍스트: 줄바꿈을 CR LF로, 제어 문자·ESC 열은 버림 */
        uint8_t *t = P.scratch;
        uint32_t k = 0;
        for (uint32_t i = 0; i < prn_len && k + 2 < (1u << 20); i++) {
            uint8_t c = prn_buf[i];
            if (c == 0x1B) { i++; continue; }                         /* ESC + 한 바이트 (글꼴·줄 간격 명령) */
            if (c == 0x0D) continue;
            if (c == 0x0A || c == 0x0C) { t[k++] = 13; t[k++] = 10; continue; }
            if (c == 0x09 || c >= 0x20) t[k++] = c;
        }
        out = t; n = k;
    }
    if (!P.create || !P.write_file || next_free_name("Printer", "PRN_", prn_mode == 2 ? ".prn" : ".txt", path, sizeof path) ||
        P.create(path, 0) || P.write_file(path, out, n)) { toast(T(S_PRN_FAIL), 0); prn_len = 0; return; }
    prn_len = 0;
    const char *nm = path;
    for (const char *q = path; *q; q++) if (*q == '\\') nm = q + 1;
    copy_str(prn_saved, sizeof prn_saved, nm);
    if (user != 2) toast(T(S_PRN_SAVED), nm);
    pv_last_len = raw_len; copy_str(pv_last_name, sizeof pv_last_name, nm); pv_valid = 1;   /* 4.0.5: 인쇄 미리보기 */
    if (user != 2 && prn_preview && !ui_active() && (user == 1 || prn_preview == 2 || pv_count_pages(prn_buf, raw_len) >= 2)) pv_open(prn_buf, raw_len, nm);   /* 4.0.6: 한 줄씩 찍다 쉬면 팝업 없음 (FF·쪽 넘침이 있었거나 끝내기일 때만) */
}

static void prn_set_mode(int m)
{
    if (m == prn_mode) return;
    prn_flush(0);
    prn_mode = m;
    settings_changed();
}

static void prn_frame(uint32_t now)
{
    M->prn_on = prn_mode != 0;
    pv_grab_font();                                     /* 4.0.6 */
    if (M->prn_poked) {                                 /* 4.0.7: 프린터가 꺼져 있는데 BASIC·프로그램이 인쇄하려 함 → 바쁨 신호는 이미 나가고 있음 (진짜 MSX처럼 멈춤), 알려 줌 */
        M->prn_poked = 0;
        static uint32_t last_noconn;
        if (!prn_mode && (!last_noconn || now - last_noconn >= 8000)) { last_noconn = now ? now : 1; toast(T(S_PRN_NOCONN), 0); }
    }
    if (M->prn_n) {
        uint32_t k = M->prn_n;
        if (!prn_len) pv_valid = 0;                     /* 4.0.5: 새 인쇄가 시작되면 지난 미리보기 자료는 덮임 */
        if (k > sizeof prn_buf - prn_len) k = sizeof prn_buf - prn_len;
        memcpy(prn_buf + prn_len, M->prn_q, k);
        prn_len += k;
        M->prn_n = 0;
        prn_last = now;
        if (prn_len == sizeof prn_buf) prn_flush(0);
    }
    if (prn_len && now - prn_last >= 5000) prn_flush(0);
    if (!prn_len && prn_saved[0] && now - prn_last >= 60000) prn_saved[0] = 0;
}

/* ---------------- 3.3.3 SND3-04 MIDI 출력 → .mid 파일 ---------------- */
/* MSX-MIDI(E8h)로 보낸 바이트를 ms 시간과 함께 모으고, 5초 조용하거나 끄거나 끝낼 때 Audio\MIDI_####.mid (SMF 형식 0,
 * 4분음표 = 500 틱, 템포 500000 µs → 1 틱 = 1 ms). 실시간 메시지(F8~FF)는 버림 */
#define MIDI_MAX 32768
static uint8_t midi_raw[MIDI_MAX];
static uint32_t midi_ms[MIDI_MAX], midi_len, midi_last, midi_t0;

static int vlq(uint8_t *o, uint32_t v)
{
    uint8_t t[5]; int n = 0;
    t[n++] = v & 0x7F;
    while (v >>= 7) t[n++] = (uint8_t)(0x80 | (v & 0x7F));
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    return n;
}
static void be32(uint8_t *o, uint32_t v) { o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16); o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; }

static void midi_flush(void)
{
    if (!midi_len) return;
    uint32_t n = midi_len;
    midi_len = 0;
    if (!P.scratch || P.scratch_size < (2u << 20) || !P.create || !P.write_file) { toast(T(S_MIDI_FAIL), 0); return; }
    uint8_t *o = P.scratch;
    uint32_t k = 0, cap = (1u << 20) - 64;
    memcpy(o, "MThd", 4); be32(o + 4, 6); o[8] = 0; o[9] = 0; o[10] = 0; o[11] = 1; o[12] = 0x01; o[13] = 0xF4;   /* 형식 0, 트랙 1, 500 틱 */
    memcpy(o + 14, "MTrk", 4);
    k = 22;
    uint32_t last = 0;
    uint8_t run = 0;
    for (uint32_t i = 0; i < n && k < cap; ) {
        uint8_t b = midi_raw[i];
        if (b >= 0xF8) { i++; continue; }                       /* 실시간 */
        uint32_t t = midi_ms[i], st = i;
        uint8_t status;
        if (b & 0x80) { status = b; i++; } else { if (!run) { i++; continue; } status = run; }
        if (status == 0xF0) {                                   /* 시스템 익스클루시브: F7까지 */
            uint32_t j = i;
            while (j < n && midi_raw[j] != 0xF7 && j - i < 4096) j++;
            uint32_t len = j - i + 1;
            k += (uint32_t)vlq(o + k, t - last); last = t;
            o[k++] = 0xF0; k += (uint32_t)vlq(o + k, len);
            for (uint32_t q = i; q < j && k < cap; q++) o[k++] = midi_raw[q];
            o[k++] = 0xF7;
            i = j < n ? j + 1 : j;
            run = 0;
            continue;
        }
        int need = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 || status == 0xF1 || status == 0xF3 ? 1 : status == 0xF6 || status == 0xF7 ? 0 : 2;
        if (status >= 0xF1) { i += (uint32_t)need; run = 0; (void)st; continue; }   /* 시스템 공통: 파일에는 쓰지 않음 */
        if (i + (uint32_t)need > n) break;
        int bad = 0;
        for (int q = 0; q < need; q++) if (midi_raw[i + (uint32_t)q] & 0x80) bad = 1;
        if (bad) { run = 0; continue; }
        k += (uint32_t)vlq(o + k, t - last); last = t;
        o[k++] = status;
        for (int q = 0; q < need; q++) o[k++] = midi_raw[i++];
        run = status;
    }
    o[k++] = 0; o[k++] = 0xFF; o[k++] = 0x2F; o[k++] = 0;    /* 트랙 끝 */
    be32(o + 18, k - 22);
    char path[160];
    if (next_free_name("Audio", "MIDI_", ".mid", path, sizeof path) || P.create(path, 0) || P.write_file(path, o, k)) { toast(T(S_MIDI_FAIL), 0); return; }
    const char *nm = path;
    for (const char *q = path; *q; q++) if (*q == '\\') nm = q + 1;
    toast(T(S_MIDI_SAVED), nm);
}

/* 3.8.1: 재부팅·끄기 직전 소리 멈춤 — USB MIDI 장치에는 16채널 모두에 소리 끄기(CC 120)·음 끄기(CC 123)·서스테인 끄기(CC 64)를 보내
 * 울리던 음이 재부팅 동안 남지 않게 하고, 내장 소리(HD Audio)는 버퍼를 비우고 멈춤 */
static void silence_all(void)
{
    if (midibus_find(MDK_USB) && P.midi_write) {     /* USB MIDI 기계가 꽂혀 있으면 */
        uint8_t b[16 * 9];
        int n = 0;
        for (int ch = 0; ch < 16; ch++) {
            b[n++] = (uint8_t)(0xB0 | ch); b[n++] = 0x40; b[n++] = 0;     /* 서스테인 끄기 */
            b[n++] = (uint8_t)(0xB0 | ch); b[n++] = 0x7B; b[n++] = 0;     /* 모든 음 끄기 */
            b[n++] = (uint8_t)(0xB0 | ch); b[n++] = 0x78; b[n++] = 0;     /* 모든 소리 끄기 */
        }
        P.midi_write(b, n, 0, 0);
    }
    if (P.silence) P.silence();
}

static void midi_frame(uint32_t now)
{
    /* MSX 가 보낸 바이트는 본체가 소리를 만들 때 단자(midi_bus)의 기계들에게 이미 나눠 줌 (소프트웨어 음원은 거기서 바로).
     * 여기서는 바깥 기계(USB MIDI 장치·.mid 녹음기)의 큐를 비워 보냄 */
    int ui = -1, fi = -1;
    for (int i = 0; i < midi_bus.n; i++) { if (midi_bus.dev[i].kind == MDK_USB && ui < 0) ui = i; if (midi_bus.dev[i].kind == MDK_FILE && fi < 0) fi = i; }
    {   /* 3.7.0: 음원 아이콘 — 내장 MIDI(MSX-MIDI 인터페이스로 보냄), SOFT MIDI·USB MIDI(그 출력으로 보냄) */
        static uint32_t last_int;
        uint32_t t = now ? now : 1;
        if (msx_midi_int_bytes != last_int) { last_int = msx_midi_int_bytes; snd_evt[MIX_MIDI_INT] = t; }
        if (M->midi_n && midibus_soft() >= 0) snd_evt[MIX_MIDI_SOFT] = t;
        if (M->midi_n && ui >= 0 && usb_midi_ok) snd_evt[MIX_MIDI_USB] = t;
        static uint32_t chk;                            /* USB MIDI 장치가 꽂혀 있나 (1초마다) */
        if (ui >= 0 && P.midi_write && now - chk >= 1000) { chk = now; usb_midi_ok = P.midi_write(0, 0, midi_out_name, sizeof midi_out_name) >= 0; }
        if (ui < 0) usb_midi_ok = 0;
    }
    static uint8_t ub[MIDI_DEV_Q]; static uint32_t ut[MIDI_DEV_Q];
    if (ui >= 0 && P.midi_write) {                      /* 3.5.4: USB MIDI 장치로 (프레임마다 모아서). 3.7.0: 믹서 USB MIDI 음량 = Note On 세기 */
        int n = midibus_take(ui, ub, 0, MIDI_DEV_Q);
        if (n) {
            static MidiVel uv;
            int uvol = midi_bus.dev[ui].mute ? 0 : usb_midi_vol * midi_bus.dev[ui].vol / 100;   /* MIDI 띠(마스터) × 기계 음량 */
            for (int i = 0; i < n; i++) ub[i] = midivel_feed(&uv, ub[i], uvol);
            int r = P.midi_write(ub, n, midi_out_name, sizeof midi_out_name);
            if (r < 0) { midi_out_name[0] = 0; usb_midi_ok = 0; } else { usb_midi_ok = 1; usb_midi_sent += (uint32_t)n; }
        }
    }
    if (fi >= 0) {
        int n = midibus_take(fi, ub, ut, MIDI_DEV_Q);
        if (n) {
            if (!midi_len) midi_t0 = now;
            for (int i = 0; i < n && midi_len < MIDI_MAX; i++) {
                midi_raw[midi_len] = ub[i];
                midi_ms[midi_len] = now - midi_t0 + ut[i] / 3580u;
                midi_len++;
            }
            midi_last = now;
            if (midi_len >= MIDI_MAX) midi_flush();
        } else if (midi_len && now - midi_last >= 5000) midi_flush();
    } else if (midi_len && now - midi_last >= 5000) midi_flush();
    M->midi_n = 0;
}
