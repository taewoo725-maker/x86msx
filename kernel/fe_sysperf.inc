/* 만든이: KimTaeWoo */
/* fe_sysperf.inc - 8.0.34 시스템 보기 > 성능 탭: CPU(코어별)·메모리·디스크(USB) 를 작업 관리자처럼.
 * 보기만 한다. 수치는 플랫폼(P.sys_sample)이 내준 누적값이고, 창이 열려 있는 동안에만 1초마다 읽어 차이로 60칸 그래프를 만든다
 * (닫혀 있으면 아무 일도 하지 않음 → 연주에 끼치는 일 없음). 그림 탭(fe_sysview.inc)과 한 창(dialog 21)을 나눠 쓴다. */
#define PERF_N 60                                         /* 그래프 칸 수 = 60초 */
static FeSysSample sp_cur;                              /* 마지막으로 읽은 값 */
static int sp_have, sp_n;                               /* 읽은 값이 있는지 / 그래프에 채워진 칸 수 */
static uint32_t sp_last_ms;
static uint8_t sp_main[PERF_N], sp_w0[FE_SYS_WK][PERF_N], sp_w1[FE_SYS_WK][PERF_N];   /* 코어별 사용률 % (메인 / 일꾼 화면 / 일꾼 MIDI) */
static uint8_t sp_mem[PERF_N], sp_io[PERF_N], sp_ior[PERF_N], sp_iow[PERF_N];           /* 메모리 사용 % / USB 활성 % / 초당 읽기·쓰기 횟수 */

static uint8_t sp_pct(uint64_t d, uint64_t dt) { if (!dt) return 0; uint64_t v = d * 100 / dt; return (uint8_t)(v > 100 ? 100 : v); }
static void sp_shift(uint8_t *a) { for (int i = 0; i + 1 < PERF_N; i++) a[i] = a[i + 1]; }

static void sp_reset(void) { sp_have = 0; sp_n = 0; for (int i = 0; i < PERF_N; i++) { sp_main[i] = sp_mem[i] = sp_io[i] = sp_ior[i] = sp_iow[i] = 0; for (int k = 0; k < FE_SYS_WK; k++) sp_w0[k][i] = sp_w1[k][i] = 0; } }

/* 1초마다 (fe 본 루프에서). 성능 탭이 보일 때만 읽음 */
static void sysperf_tick(uint32_t now)
{
    if (dialog != 21 || !P.sys_sample) { sp_have = 0; return; }
    if (sp_have && now - sp_last_ms < 1000) return;
    FeSysSample c;
    if (!P.sys_sample(&c)) return;
    if (sp_have) {
        uint64_t dt = c.tsc - sp_cur.tsc;
        if (dt) {
            sp_shift(sp_main); sp_shift(sp_mem); sp_shift(sp_io); sp_shift(sp_ior); sp_shift(sp_iow);
            for (int k = 0; k < FE_SYS_WK; k++) { sp_shift(sp_w0[k]); sp_shift(sp_w1[k]); }
            sp_main[PERF_N - 1] = sp_pct(c.main_busy - sp_cur.main_busy, dt);
            for (int k = 0; k < FE_SYS_WK; k++) {
                uint8_t a = sp_pct(c.wk_busy[0][k] - sp_cur.wk_busy[0][k], dt), b = sp_pct(c.wk_busy[1][k] - sp_cur.wk_busy[1][k], dt);
                if (a + b > 100) b = (uint8_t)(100 - a);
                sp_w0[k][PERF_N - 1] = a; sp_w1[k][PERF_N - 1] = b;
            }
            sp_mem[PERF_N - 1] = c.mem_total ? sp_pct(c.mem_total - c.mem_free, c.mem_total) : 0;
            { uint64_t ms = c.tsc_hz ? dt / (c.tsc_hz / 1000) : 1000; sp_io[PERF_N - 1] = sp_pct(c.io_ms - sp_cur.io_ms, ms ? ms : 1); }
            { uint32_t r = c.io_r - sp_cur.io_r, w = c.io_w - sp_cur.io_w; sp_ior[PERF_N - 1] = (uint8_t)(r > 255 ? 255 : r); sp_iow[PERF_N - 1] = (uint8_t)(w > 255 ? 255 : w); }
            if (sp_n < PERF_N) sp_n++;
        }
    }
    sp_cur = c; sp_have = 1; sp_last_ms = now;
    if (sv_tab == 0) menu_dirty = 1;
}

/* 글자 만들기 */
static void sp_size(char *d, int max, uint64_t by)       /* "2.4 GB" / "512 MB" */
{
    d[0] = 0;
    if (by >= (1ull << 30)) { uint32_t t = (uint32_t)(by * 10 >> 30); num_append(d, max, t / 10, 0, ' '); str_append(d, max, "."); num_append(d, max, t % 10, 0, ' '); str_append(d, max, " GB"); }
    else { num_append(d, max, (uint32_t)(by >> 20), 0, ' '); str_append(d, max, " MB"); }
}
static void sp_pct_txt(char *d, int max, int p) { d[0] = 0; num_append(d, max, (uint32_t)p, 0, ' '); str_append(d, max, "%"); }
static void sp_ghz(char *d, int max, uint64_t hz) { d[0] = 0; uint32_t t = (uint32_t)(hz / 10000000ull); num_append(d, max, t / 100, 0, ' '); str_append(d, max, "."); num_append(d, max, t % 100, 2, '0'); str_append(d, max, " GHz"); }

/* 칸 그래프: a (아래) 위에 b 를 쌓음. 값은 0~100 %. 앞쪽(오래된 칸)은 아직 없으면 비움 */
static void sp_graph(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t *a, const uint8_t *b, uint8_t ca, uint8_t cb)
{
    if (w < 8 || h < 8) return;
    fe_fill(x, y, w, h, C_FIELD);
    for (uint32_t q = 1; q < 4; q++) fe_fill(x, y + h * q / 4, w, 1, C_LINE);
    for (int i = PERF_N - sp_n; i < PERF_N; i++) {
        uint32_t x0 = x + (uint32_t)i * w / PERF_N, x1 = x + (uint32_t)(i + 1) * w / PERF_N;
        if (x1 <= x0) x1 = x0 + 1;
        uint32_t ha = (uint32_t)a[i] * h / 100, hb = b ? (uint32_t)b[i] * h / 100 : 0;
        if (ha + hb > h) hb = h - ha;
        if (ha) fe_fill(x0, y + h - ha, x1 - x0, ha, ca);
        if (hb) fe_fill(x0, y + h - ha - hb, x1 - x0, hb, cb);
    }
    fe_fill(x, y, w, 1, C_LINE); fe_fill(x, y + h - 1, w, 1, C_LINE); fe_fill(x, y, 1, h, C_LINE); fe_fill(x + w - 1, y, 1, h, C_LINE);
}
static void hc_svp(const UiHit *h, int ev) { (void)ev; if (sv_perf_sel != h->b) { sv_perf_sel = h->b; menu_dirty = 1; } }

static void sp_stat(uint32_t x, uint32_t y, uint32_t w, const char *label, const char *val, uint8_t col)
{
    text_f(SF, 1, x, y, label, C_MUTED, 0xFF, w, 1);
    text_f(F, S, x, y + SLH + 2 * S, val, col, 0xFF, w, 1);
}
static void sp_legend(uint32_t x, uint32_t y, uint8_t c, const char *t)
{
    fe_fill(x, y + 3 * S, 10 * S, 10 * S, c);
    text_f(SF, 1, x + 14 * S, y, t, C_MUTED, 0xFF, 0, 1);
}
static uint32_t sp_legend_w(const char *t) { return 14 * S + ui_sw(t) + 16 * S; }

static void draw_sysperf(uint32_t bx, uint32_t bw, uint32_t top, uint32_t bottom)
{
    uint32_t pad = 14 * S, LX = bx + pad, lw = bw * 24 / 100;
    uint32_t ih = LH + SLH + 16 * S, gap = 6 * S;
    int nwk = (int)sp_cur.workers; if (nwk > FE_SYS_WK) nwk = FE_SYS_WK;
    int total = (int)sp_cur.logical; if (total < 1 + nwk) total = 1 + nwk; if (total > 24) total = 24;
    int mem_p = sp_cur.mem_total ? (int)((sp_cur.mem_total - sp_cur.mem_free) * 100 / sp_cur.mem_total) : 0;
    int cpu_p = 0; { int n = 1 + nwk; int sum = sp_main[PERF_N - 1]; for (int k = 0; k < nwk; k++) sum += sp_w0[k][PERF_N - 1] + sp_w1[k][PERF_N - 1]; cpu_p = n ? sum / n : 0; }
    /* 왼쪽: 항목 3개 (작은 그래프 + 이름 + 한 줄 설명) */
    for (int i = 0; i < 3; i++) {
        uint32_t y = top + (uint32_t)i * (ih + gap);
        if (y + ih > bottom) break;
        int sel = i == sv_perf_sel;
        ui_list_row_bg(LX, y, lw, ih, sel, 0, 1);
        uint32_t mw = ih * 3 / 2, mh = ih - 12 * S;
        const uint8_t *ma = i == 0 ? sp_main : i == 1 ? sp_mem : sp_io;
        sp_graph(LX + 6 * S, y + 6 * S, mw, mh, ma, 0, i == 0 ? C_ACC : i == 1 ? C_ACC2 : C_OK, 0);
        char t[48], sub[48]; sub[0] = 0;
        if (i == 0) { sp_pct_txt(sub, sizeof sub, cpu_p); str_append(sub, sizeof sub, "  "); char g[24]; sp_ghz(g, sizeof g, sp_cur.tsc_hz); str_append(sub, sizeof sub, g); }
        else if (i == 1) { char a[24], b2[24]; sp_size(a, sizeof a, sp_cur.mem_total - sp_cur.mem_free); sp_size(b2, sizeof b2, sp_cur.mem_total); copy_str(sub, sizeof sub, a); str_append(sub, sizeof sub, " / "); str_append(sub, sizeof sub, b2); }
        else { str_append(sub, sizeof sub, "USB "); char p[16]; sp_pct_txt(p, sizeof p, sp_io[PERF_N - 1]); str_append(sub, sizeof sub, p); }
        copy_str(t, sizeof t, i == 0 ? "CPU" : i == 1 ? grow_t("메모리", "Memory", "メモリ") : grow_t("디스크", "Disk", "ディスク"));
        uint32_t tx = LX + 6 * S + mw + 10 * S;
        text_clip(tx, y + 4 * S, t, sel ? 15 : C_TEXT, 0xFF, LX + lw - tx - 6 * S);
        text_f(SF, 1, tx, y + 4 * S + LH, sub, sel ? 15 : C_MUTED, 0xFF, LX + lw - tx - 6 * S, 1);
        hit_add(hc_svp, 0, i, 0, 0, LX, y, lw, ih);
    }
    fe_fill(LX + lw + 12 * S, top, S, bottom - top, C_LINE);
    uint32_t RX = LX + lw + 28 * S, RW = bx + bw - pad - RX;
    /* 오른쪽 머리: 이름 + (오른쪽) 부가 설명 */
    char title[40], right[64]; right[0] = 0;
    if (sv_perf_sel == 0) { copy_str(title, sizeof title, "CPU"); copy_str(right, sizeof right, sp_cur.cpu_name); }
    else if (sv_perf_sel == 1) { copy_str(title, sizeof title, grow_t("메모리", "Memory", "メモリ")); sp_size(right, sizeof right, sp_cur.mem_total); }
    else { copy_str(title, sizeof title, grow_t("디스크", "Disk", "ディスク")); copy_str(right, sizeof right, grow_t("USB 메모리 (파일 접근)", "USB drive (file access)", "USBメモリ（ファイルアクセス）")); }
    text_clip(RX, top, title, C_ACC, 0xFF, RW);
    if (right[0]) text_r(RX + RW, top, right, C_MUTED, RW * 3 / 4);
    uint32_t y = top + LH + 10 * S;

    if (sv_perf_sel == 0) {
        /* 코어별 그래프 (작업 관리자처럼). 쓰지 않는 코어는 어둡게 */
        text_f(SF, 1, RX, y, grow_t("코어별 사용률 · 최근 60초", "Per-core load, last 60 s", "コア別使用率・直近60秒"), C_MUTED, 0xFF, RW, 1);
        y += SLH + 6 * S;
        uint32_t stats_h = SLH + LH + 14 * S, leg_h = SLH + 10 * S;
        uint32_t gb = bottom > stats_h + leg_h + y + 12 * S ? bottom - stats_h - leg_h - 8 * S : y + 8 * S;
        int cols = total <= 2 ? total : total <= 6 ? 3 : total <= 12 ? 4 : 5;
        int rows = (total + cols - 1) / cols;
        uint32_t cg = 5 * S, cw = (RW - (uint32_t)(cols - 1) * cg) / (uint32_t)cols, chh = (gb - y - (uint32_t)(rows - 1) * cg) / (uint32_t)rows;
        for (int k = 0; k < total; k++) {
            uint32_t cx = RX + (uint32_t)(k % cols) * (cw + cg), cy = y + (uint32_t)(k / cols) * (chh + cg);
            char lab[24] = "", pc[8];
            if (k <= nwk && sp_have) {
                const uint8_t *a = k == 0 ? sp_main : sp_w0[k - 1], *b = k == 0 ? 0 : sp_w1[k - 1];
                sp_graph(cx, cy, cw, chh, a, b, k == 0 ? C_ACC : C_ACC2, C_OK);
                int p = a[PERF_N - 1] + (b ? b[PERF_N - 1] : 0);
                sp_pct_txt(pc, sizeof pc, p > 100 ? 100 : p);
                text_r(cx + cw - 4 * S, cy + 2 * S, pc, C_TEXT, cw / 2);
                if (k == 0) copy_str(lab, sizeof lab, grow_t("코어 1 (본체)", "Core 1 (main)", "コア1（本体）"));
                else { copy_str(lab, sizeof lab, grow_t("코어 ", "Core ", "コア ")); num_append(lab, sizeof lab, (uint32_t)k + 1, 0, ' '); }
            } else {
                fe_fill(cx, cy, cw, chh, C_HEAD);
                fe_fill(cx, cy, cw, 1, C_LINE); fe_fill(cx, cy + chh - 1, cw, 1, C_LINE); fe_fill(cx, cy, 1, chh, C_LINE); fe_fill(cx + cw - 1, cy, 1, chh, C_LINE);
                copy_str(lab, sizeof lab, grow_t("안 씀", "Not used", "未使用"));
            }
            text_f(SF, 1, cx + 4 * S, cy + 2 * S, lab, k <= nwk ? C_TEXT : C_MUTED, 0xFF, cw - 8 * S, 1);
        }
        uint32_t ly = gb + 4 * S, lx = RX;
        const char *l1 = grow_t("MSX 본체", "MSX main", "MSX本体"), *l2 = grow_t("화면 확대 코어", "Screen core", "画面コア"), *l3 = grow_t("MIDI 합성 코어", "MIDI core", "MIDIコア");
        sp_legend(lx, ly, C_ACC, l1); lx += sp_legend_w(l1);
        sp_legend(lx, ly, C_ACC2, l2); lx += sp_legend_w(l2);
        sp_legend(lx, ly, C_OK, l3);
        uint32_t sy = ly + leg_h, sw4 = RW / 4;
        char v[32], t2[24];
        sp_pct_txt(v, sizeof v, cpu_p); sp_stat(RX, sy, sw4 - 6 * S, grow_t("쓰는 코어 평균", "Avg of used cores", "使用コア平均"), v, C_TEXT);
        sp_ghz(v, sizeof v, sp_cur.tsc_hz); sp_stat(RX + sw4, sy, sw4 - 6 * S, grow_t("기준 속도", "Base clock", "基準クロック"), v, C_TEXT);
        v[0] = 0; num_append(v, sizeof v, (uint32_t)measured_speed, 0, ' '); str_append(v, sizeof v, "%"); sp_stat(RX + 2 * sw4, sy, sw4 - 6 * S, grow_t("MSX 속도", "MSX speed", "MSX速度"), v, measured_speed >= 95 ? C_OK : C_WARN);
        v[0] = 0; num_append(v, sizeof v, (uint32_t)total, 0, ' '); str_append(v, sizeof v, " / "); num_append(v, sizeof v, (uint32_t)(1 + nwk), 0, ' '); copy_str(t2, sizeof t2, grow_t("코어 / 쓰는 코어", "Cores / in use", "コア／使用中")); sp_stat(RX + 3 * sw4, sy, sw4 - 6 * S, t2, v, C_TEXT);
    } else if (sv_perf_sel == 1) {
        text_f(SF, 1, RX, y, grow_t("사용량 · 최근 60초", "Usage, last 60 s", "使用量・直近60秒"), C_MUTED, 0xFF, RW, 1);
        text_r(RX + RW, y, "100%", C_MUTED, 80 * S);
        y += SLH + 6 * S;
        uint32_t gh = (bottom - y) * 38 / 100; if (gh < 40 * S) gh = 40 * S;
        sp_graph(RX, y, RW, gh, sp_mem, 0, C_ACC2, 0);
        y += gh + 14 * S;
        text_f(SF, 1, RX, y, grow_t("메모리 구성 (펌웨어 메모리 지도)", "Composition (firmware memory map)", "構成（ファームウェアのメモリマップ）"), C_MUTED, 0xFF, RW, 1);
        y += SLH + 6 * S;
        uint64_t tot = sp_cur.mem_total ? sp_cur.mem_total : 1;
        uint64_t seg[4] = { sp_cur.mem_prog, sp_cur.mem_data, sp_cur.mem_fw, sp_cur.mem_free };
        uint8_t sc[4] = { C_ACC, C_ACC2, C_WARN, C_TRACK };
        const char *sn[4] = { grow_t("프로그램", "Program", "プログラム"), grow_t("작업 메모리", "Work memory", "作業メモリ"), grow_t("펌웨어·기타", "Firmware etc.", "ファーム他"), grow_t("여유", "Free", "空き") };
        uint32_t bh = LH, x0 = RX;
        fe_fill(RX, y, RW, bh, C_FIELD);
        for (int i = 0; i < 4; i++) { uint32_t sw = (uint32_t)(seg[i] * RW / tot); if (i == 3) sw = RX + RW - x0; if (sw) fe_fill(x0, y + 1, sw, bh - 2, sc[i]); x0 += sw; }
        y += bh + 8 * S;
        uint32_t lx = RX;
        for (int i = 0; i < 4; i++) { char t3[64], z[24]; sp_size(z, sizeof z, seg[i]); copy_str(t3, sizeof t3, sn[i]); str_append(t3, sizeof t3, " "); str_append(t3, sizeof t3, z);
            if (lx + sp_legend_w(t3) > RX + RW) { lx = RX; y += SLH + 6 * S; }
            sp_legend(lx, y, sc[i], t3); lx += sp_legend_w(t3); }
        y += SLH + 14 * S;
        uint32_t sw4 = RW / 4; char v[32];
        sp_size(v, sizeof v, sp_cur.mem_total); sp_stat(RX, y, sw4 - 6 * S, grow_t("전체", "Total", "全体"), v, C_TEXT);
        sp_size(v, sizeof v, sp_cur.mem_total - sp_cur.mem_free); sp_stat(RX + sw4, y, sw4 - 6 * S, grow_t("사용 중", "In use", "使用中"), v, C_TEXT);
        sp_size(v, sizeof v, sp_cur.mem_free); sp_stat(RX + 2 * sw4, y, sw4 - 6 * S, grow_t("여유", "Free", "空き"), v, C_OK);
        sp_pct_txt(v, sizeof v, mem_p); sp_stat(RX + 3 * sw4, y, sw4 - 6 * S, grow_t("사용률", "Use", "使用率"), v, C_TEXT);
    } else {
        text_f(SF, 1, RX, y, grow_t("USB 활성 시간 · 최근 60초 (파일을 읽고 쓰느라 쓴 시간의 비율)", "USB busy time, last 60 s (share of time spent in file access)", "USB稼働時間・直近60秒"), C_MUTED, 0xFF, RW, 1);
        y += SLH + 6 * S;
        uint32_t gh = (bottom - y) * 30 / 100; if (gh < 36 * S) gh = 36 * S;
        sp_graph(RX, y, RW, gh, sp_io, 0, C_OK, 0);
        y += gh + 12 * S;
        uint32_t sw4 = RW / 4; char v[32];
        sp_pct_txt(v, sizeof v, sp_io[PERF_N - 1]); sp_stat(RX, y, sw4 - 6 * S, grow_t("활성 시간", "Busy", "稼働"), v, C_TEXT);
        v[0] = 0; num_append(v, sizeof v, sp_ior[PERF_N - 1], 0, ' '); sp_stat(RX + sw4, y, sw4 - 6 * S, grow_t("읽기/초", "Reads/s", "読み/秒"), v, C_TEXT);
        v[0] = 0; num_append(v, sizeof v, sp_iow[PERF_N - 1], 0, ' '); sp_stat(RX + 2 * sw4, y, sw4 - 6 * S, grow_t("쓰기/초", "Writes/s", "書き/秒"), v, C_TEXT);
        { uint32_t mx = 0; for (int i = 0; i < sp_cur.io_n; i++) if (sp_cur.io[i].ms > mx) mx = sp_cur.io[i].ms;
          v[0] = 0; num_append(v, sizeof v, mx, 0, ' '); str_append(v, sizeof v, " ms"); sp_stat(RX + 3 * sw4, y, sw4 - 6 * S, grow_t("최근 가장 긴 접근", "Longest recent", "直近の最長"), v, mx >= 47 ? C_BAD : C_TEXT); }
        y += SLH + LH + 16 * S;
        text_f(SF, 1, RX, y, grow_t("최근 쓰기와 20 ms 넘은 읽기 (47 ms 넘으면 빨강: 소리 버퍼보다 긺)", "Recent writes and reads over 20 ms (red = longer than the 47 ms sound buffer)", "最近の書込と20ms超の読込（赤=音バッファ超）"), C_MUTED, 0xFF, RW, 1);
        y += SLH + 6 * S;
        uint32_t rh = LH + 4 * S, c1 = RX, c2 = RX + 70 * S, c3 = RX + 110 * S, c4 = RX + 190 * S;
        fe_fill(RX, y + rh - 1, RW, 1, C_LINE);
        text_f(SF, 1, c1, y + 2 * S, grow_t("시각", "Time", "時刻"), C_MUTED, 0xFF, 64 * S, 1);
        text_f(SF, 1, c2, y + 2 * S, grow_t("구분", "Type", "種別"), C_MUTED, 0xFF, 36 * S, 1);
        text_f(SF, 1, c3, y + 2 * S, grow_t("걸린 시간", "Took", "所要"), C_MUTED, 0xFF, 76 * S, 1);
        text_f(SF, 1, c4, y + 2 * S, grow_t("파일", "File", "ファイル"), C_MUTED, 0xFF, RX + RW - c4, 1);
        y += rh;
        if (!sp_cur.io_n) text_f(SF, 1, c1, y + 4 * S, grow_t("아직 기록이 없습니다", "Nothing yet", "まだ記録なし"), C_MUTED, 0xFF, RW, 1);
        for (int i = sp_cur.io_n - 1; i >= 0 && y + rh <= bottom; i--, y += rh) {
            char t4[24]; uint8_t col = sp_cur.io[i].ms >= 47 ? C_BAD : C_TEXT;
            t4[0] = 0; num_append(t4, sizeof t4, sp_cur.io[i].at / 1000, 0, ' '); str_append(t4, sizeof t4, " s");
            text_f(SF, 1, c1, y + 2 * S, t4, C_TEXT, 0xFF, 64 * S, 1);
            text_f(SF, 1, c2, y + 2 * S, sp_cur.io[i].w ? grow_t("쓰기", "Write", "書込") : grow_t("읽기", "Read", "読込"), C_TEXT, 0xFF, 36 * S, 1);
            t4[0] = 0; num_append(t4, sizeof t4, sp_cur.io[i].ms, 0, ' '); str_append(t4, sizeof t4, " ms");
            text_f(SF, 1, c3, y + 2 * S, t4, col, 0xFF, 76 * S, 1);
            text_f(SF, 1, c4, y + 2 * S, sp_cur.io[i].path, C_TEXT, 0xFF, RX + RW - c4, 1);
        }
    }
}
