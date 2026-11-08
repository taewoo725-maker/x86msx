/* 만든이: KimTaeWoo
 * 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * fe_log.inc (8.0.129) - 분야별 상태 기록: 진단 판정(dg_sum)이 바뀔 때만 한 줄씩 LOG\<분야>.TXT 에 모아 씀.
 *   분야: USB · 마우스 · 조이스틱 · 터치패드 · WiFi · LAN · Color. (키보드·소리·화면은 예전 로그를 그 분야 스위치가 맡음)
 *   새 분야 = 아래 표에 한 줄. 스위치가 꺼져 있으면 아무것도 안 함. 연주·MSX 화면 중에는 쓰지 않음(USB 쓰기로 멈추지 않게) */
typedef struct { int la; int da; const char *file; const char *name; } LogSnap;
static const LogSnap log_snap[] = {
    { LA_USB,   DA_USB,   "0:\\LOG\\USB.TXT",      "USB" },
    { LA_MOUSE, DA_MOUSE, "0:\\LOG\\MOUSE.TXT",    "MOUSE" },
    { LA_JOY,   DA_PAD,   "0:\\LOG\\JOY.TXT",      "JOY" },
    { LA_TP,    DA_TP,    "0:\\LOG\\TOUCHPAD.TXT", "TOUCHPAD" },
    { LA_WIFI,  DA_NET,   "0:\\LOG\\WIFI.TXT",     "WIFI" },
    { LA_LAN,   DA_NET,   "0:\\LOG\\LAN.TXT",      "LAN" },
    { LA_COLOR, -1,       "0:\\LOG\\COLOR.TXT",    "COLOR" },     /* da = -1: 판정 대신 화면 색 깊이 */
};
#define LOGSNAP_N ((int)(sizeof log_snap / sizeof log_snap[0]))
#define LOGSNAP_MAX 3000
static char logsnap_buf[LOGSNAP_N][LOGSNAP_MAX + 1], logsnap_last[LOGSNAP_N][200];
static int logsnap_len[LOGSNAP_N], logsnap_dirty[LOGSNAP_N];
/* 8.0.175 진단 LogVramTop: VRAM 맨 앞(0x0000~0x03FF)에 쓰는 코드를 CRUMB.TXT 에 적는다.
 *   같은 위치(PC)에서 이어서 쓴 바이트들은 한 줄로 묶음: "VT pc=XXXX a=XXXX-XXXX n=N v=VV". 영상 열기 때 목록 제목줄에 생기는 점줄의 원인 찾기용 */
int log_vram_top;
typedef struct { uint16_t pc; uint32_t first_addr, last_addr, count; uint8_t first_value; int pending; } VramRun;
static VramRun vram_run;
static int vram_lines_left;
static void vram_run_flush(void)
{
    if (!vram_run.pending) return;
    vram_run.pending = 0;
    if (!P.crumb) return;
    char ln[80]; ln[0] = 0;
    str_append(ln, sizeof ln, "VT pc="); hex_append(ln, sizeof ln, vram_run.pc, 4);
    str_append(ln, sizeof ln, " a="); hex_append(ln, sizeof ln, vram_run.first_addr, 4);
    str_append(ln, sizeof ln, "-"); hex_append(ln, sizeof ln, vram_run.last_addr, 4);
    str_append(ln, sizeof ln, " n="); num_append(ln, sizeof ln, vram_run.count, 0, ' ');
    str_append(ln, sizeof ln, " v="); hex_append(ln, sizeof ln, vram_run.first_value, 2);
    P.crumb(ln);
}
/* 8.0.177: 처음 보는 PC 마다 한 번 "VC pc=… s=A8 sp=… st=스택 3칸 code=pc-8 부터 16바이트" 를 적음 — 그 자리가 어느 프로그램 코드인지 알아내기 위함 */
static uint16_t vram_seen_pc[32]; static int vram_seen_n;
static void vram_code_note(uint16_t pc)
{
    for (int i = 0; i < vram_seen_n; i++) if (vram_seen_pc[i] == pc) return;
    if (vram_seen_n >= 32 || !P.crumb) return;
    vram_seen_pc[vram_seen_n++] = pc;
    char ln[120]; ln[0] = 0;
    str_append(ln, sizeof ln, "VC pc="); hex_append(ln, sizeof ln, pc, 4);
    str_append(ln, sizeof ln, " s="); hex_append(ln, sizeof ln, M->primary, 2);
    str_append(ln, sizeof ln, " st=");
    for (int k = 0; k < 3; k++) {
        uint16_t at = (uint16_t)(M->cpu.sp.w + 2 * k);
        hex_append(ln, sizeof ln, (uint32_t)(msx_mem_read(M, at) | (msx_mem_read(M, (uint16_t)(at + 1)) << 8)), 4);
        if (k < 2) str_append(ln, sizeof ln, ",");
    }
    P.crumb(ln);                                                       /* 기록 한 줄은 70자까지라 둘로 나눔 */
    ln[0] = 0;
    str_append(ln, sizeof ln, "VD pc="); hex_append(ln, sizeof ln, pc, 4); str_append(ln, sizeof ln, " c=");
    for (int k = -8; k < 8; k++) hex_append(ln, sizeof ln, msx_mem_read(M, (uint16_t)(pc + k)), 2);
    P.crumb(ln);
    /* 8.0.178: 4000h~7FFFh 에서 처음 나온 PC 는 둘레 256바이트와 부른 쪽(스택 맨 위·둘째) 둘레 32바이트씩을 "VM aaaa 26바이트" 줄로 더 적음 (256바이트 쪽이 다른 최대 2곳) */
    static int dumped; static uint8_t dumped_page[2];
    if (pc >= 0x4000 && pc < 0x8000 && dumped < 2 && !(dumped && dumped_page[0] == (pc >> 8))) {
        dumped_page[dumped] = (uint8_t)(pc >> 8); dumped++;
        uint16_t from[3] = { (uint16_t)(pc - 0x80), 0, 0 };
        for (int k = 0; k < 2; k++) from[k + 1] = (uint16_t)((msx_mem_read(M, (uint16_t)(M->cpu.sp.w + 2 * k)) | (msx_mem_read(M, (uint16_t)(M->cpu.sp.w + 2 * k + 1)) << 8)) - 0x10);
        const int len[3] = { 0x100, 0x20, 0x20 };
        for (int r = 0; r < 3; r++)
            for (int off = 0; off < len[r]; off += 26) {
                ln[0] = 0; str_append(ln, sizeof ln, "VM "); hex_append(ln, sizeof ln, (uint16_t)(from[r] + off), 4); str_append(ln, sizeof ln, " ");
                for (int b = 0; b < 26 && off + b < len[r]; b++) hex_append(ln, sizeof ln, msx_mem_read(M, (uint16_t)(from[r] + off + b)), 2);
                P.crumb(ln);
            }
    }
}
static void vram_top_note(uint32_t addr, uint8_t value)
{
    uint16_t pc = M->cpu.pc;
    vram_code_note(pc);
    if (vram_run.pending && vram_run.pc == pc && addr == vram_run.last_addr + 1) { vram_run.last_addr = addr; vram_run.count++; return; }
    vram_run_flush();
    if (vram_lines_left <= 0) return;
    vram_lines_left--;
    vram_run.pc = pc; vram_run.first_addr = vram_run.last_addr = addr; vram_run.count = 1; vram_run.first_value = value; vram_run.pending = 1;
}
/* 스위치를 켜고 끌 때와 주기마다 부름: 켜면 훅을 걸고 한도(200줄)를 채우고, 끄면 훅을 풀고 남은 줄을 적음 */
static void vram_top_tick(void)
{
    static int was_on;
    if (log_vram_top && !was_on) { vram_lines_left = 400; vram_seen_n = 0; vram_run.pending = 0; vdp_vram_trace = vram_top_note; }
    if (!log_vram_top && was_on) { vram_run_flush(); vdp_vram_trace = 0; }
    was_on = log_vram_top;
    if (log_vram_top) vram_run_flush();                          /* 마지막 묶음이 다음 쓰기를 기다리며 남지 않게 */
}
static void log_area_tick(uint32_t now)
{
    vram_top_tick();
    static uint32_t next_look, next_write;
    if (!P.write_file || (int32_t)(now - next_look) < 0) return;
    next_look = now + 1000;
    int any = 0; for (int i = 0; i < LOGSNAP_N; i++) if (log_area[log_snap[i].la]) any = 1;
    if (!any) return;
    dg_refresh(0);
    for (int i = 0; i < LOGSNAP_N; i++) {
        const LogSnap *s = &log_snap[i];
        if (!log_area[s->la]) continue;
        char v[200]; v[0] = 0;
        if (s->da >= 0) {
            const DiagSum *d = &dg_sum[s->da];
            str_append(v, sizeof v, dg_state_name(d->st)); str_append(v, sizeof v, " ");
            if (d->why[0]) { str_append(v, sizeof v, "["); str_append(v, sizeof v, d->why); str_append(v, sizeof v, "] "); }
            str_append(v, sizeof v, d->text);
        } else {
            str_append(v, sizeof v, "bpp "); num_append(v, sizeof v, (uint32_t)D.bpp, 0, ' ');
            str_append(v, sizeof v, P.dac_set ? " dac-set" : " no-dac"); str_append(v, sizeof v, dialog == 2 && diag_tab == 8 ? " color-tab-open" : "");
        }
        int same = 1; for (int k = 0; k < 199; k++) { if (v[k] != logsnap_last[i][k]) { same = 0; break; } if (!v[k]) break; }
        if (same) continue;
        for (int k = 0; k < 199; k++) { logsnap_last[i][k] = v[k]; if (!v[k]) break; }
        logsnap_last[i][199] = 0;
        char ln[260]; ln[0] = 0;
        str_append(ln, sizeof ln, "t="); num_append(ln, sizeof ln, now, 0, ' '); str_append(ln, sizeof ln, " "); str_append(ln, sizeof ln, v); str_append(ln, sizeof ln, "\r\n");
        int n = 0; while (ln[n]) n++;
        if (logsnap_len[i] + n > LOGSNAP_MAX) {                  /* 가득 차면 오래된 앞쪽 절반을 버림 */
            int half = logsnap_len[i] / 2; while (half < logsnap_len[i] && logsnap_buf[i][half - 1] != '\n') half++;
            for (int k = half; k < logsnap_len[i]; k++) logsnap_buf[i][k - half] = logsnap_buf[i][k];
            logsnap_len[i] -= half;
        }
        for (int k = 0; k < n; k++) logsnap_buf[i][logsnap_len[i]++] = ln[k];
        logsnap_dirty[i] = 1;
    }
    if ((int32_t)(now - next_write) < 0 || !ui_active() || live_ui() || !P.create || !log_write_go) return;   /* 8.0.204: 소리가 클 때·입력 직후에는 미룸 (log_write_gate) */
    next_write = now + 3000;
    for (int i = 0; i < LOGSNAP_N; i++) {
        if (!logsnap_dirty[i]) continue;
        logsnap_dirty[i] = 0;
        static char ob[LOGSNAP_MAX + 300]; int q = 0;
        const char *h1 = "#LOG 1\r\n#prog=X86MSX ver=" MSXEMU_VERSION "\r\n#what=state of "; while (*h1) ob[q++] = *h1++;
        for (const char *c = log_snap[i].name; *c; c++) ob[q++] = *c;
        const char *h2 = " (a line is added only when the diagnosis changes)\r\n"; while (*h2) ob[q++] = *h2++;
        for (int k = 0; k < logsnap_len[i]; k++) ob[q++] = logsnap_buf[i][k];
        { PF_BEGIN; P.create("0:\\LOG", 1); P.create(log_snap[i].file, 0); P.write_file(log_snap[i].file, (const uint8_t *)ob, (uint32_t)q); PF_END(PF_FILE); log_wrote_ms = P.ms(); }   /* 8.0.204: 쓰는 시간을 file 로 재고, 이 쓰기가 만든 멈춤은 STALL 에 안 적음 */
        break;                                                   /* 한 번에 파일 하나만 (USB 쓰기를 나눔) */
    }
}
