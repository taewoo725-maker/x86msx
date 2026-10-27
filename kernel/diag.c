/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* diag.c - 5.2.26: 진단 정보 분류·판정 (diag.h 설명). 표는 위에서부터 처음 맞는 줄이 이김 */
#include "diag.h"

const char *const diag_area_key[DA_N] = { "SYS", "VID", "SND", "KBD", "MOUSE", "TP", "PAD", "USB", "NET", "PERF" };

/* 시험 PC 표 — 실기 확인 기록만 (X86MSX_시험PC목록.md, X86MSX_기종별_반영현황). 영역 차례: SYS VID SND KBD MOUSE TP PAD USB NET PERF
 * U = 모름, O = 동작해야 함, X = 지원 안 함(알고 있음), N = 장치 없음 */
#define U EX_UNKNOWN
#define O EX_OK
#define X EX_UNSUP
#define N EX_NONE
const DiagPc diag_pcs[] = {
    { 1, "Lenovo V14-ARE (82DQ, AMD Renoir)", "1002:1636", "V14-ARE", "uefi", 0, { O, O, O, O, U, O, U, O, O, O },
      "I2C touchpad (AMD) OK; wifi RTL8822CE 10EC:C822 OK (ping 5.0.161); HDMI video+sound OK (test room v53, X86MSX 4.0.115~), plug HDMI before boot" },
    { 2, "Lenovo IdeaPad 310-15IKB (80TV, Intel HD 620)", "8086:5916", "80TV", "both", "14F1:510F", { O, O, O, O, U, U, U, O, U, O },
      "BIOS: Legacy support + UEFI first (UEFI-only = built-in keyboard dead); HDA 8086:9D71; speaker + HDMI sound OK (4.0.88), HDMI 1920x1080 only" },
    { 3, "ASUS B860M AYW GAMING WIFI + RTX 4060 Ti (Core Ultra 7 265)", 0, "B860M", "uefi", 0, { O, O, O, O, U, N, U, O, U, O },
      "earphone jack sound OK (4.0.22); HDMI/DP monitor sound not supported (NVIDIA); codec addr 2 no response handled (4.0.21)" },
    { 4, "GIGABYTE GA-H81-D3 (4th gen H81)", 0, "H81-D3", "both", 0, { O, O, U, O, U, N, U, O, U, O },
      "checked 2026-10-08; board has PS/2, VGA, parallel port" },
    { 5, "Acer Aspire V5-171 (Ivy Bridge, Intel HD 4000)", "8086:0166", "V5-171", "legacy", 0, { O, O, O, O, U, U, U, X, U, O },
      "legacy BIOS (CSM) only; HDMI+panel dual and HDMI sound OK (test room v38/v39, 4.0.108~); plug HDMI before boot; wired BCM57785 in progress, wifi 168C:0034 unsupported" },
    { 0, "LG gram 16 (16Z90S-GA5VK, Core Ultra / Meteor Lake)", "8086:7D55", "16Z90S", "uefi", "10EC:0298", { O, O, O, O, O, X, U, O, X, O },
      "UEFI only; sound needs HDA DMA map (5.2.20); I2C touchpad (Intel) and wifi 8086:7E40 unsupported; speaker amp not checked; diag sound tab hang under investigation" },
};
#undef U
#undef O
#undef X
#undef N
const int diag_npcs = (int)(sizeof diag_pcs / sizeof diag_pcs[0]);

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static int has(const char *s, const char *w)           /* 대소문자 무시 */
{
    for (; *s; s++) { int i = 0; while (w[i] && lc((unsigned char)s[i]) == lc((unsigned char)w[i])) i++; if (!w[i]) return 1; }
    return 0;
}
static int ishex(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }

/* 줄 분류 표: 'p' = 줄 앞, 's' = 어디든 (대소문자 무시) */
static const struct { char how; const char *pat; DiagArea a; } rules[] = {
    { 'p', "HDA", DA_SND }, { 'p', "VT-d", DA_SND }, { 'p', "codec", DA_SND }, { 'p', "stream ", DA_SND }, { 'p', "pin ", DA_SND },
    { 'p', "8042", DA_KBD }, { 'p', "USB keyboard", DA_KBD }, { 'p', "PS/2 keyboard", DA_KBD },
    { 'p', "HID I2C", DA_TP }, { 'p', "HID touchpad", DA_TP }, { 'p', "HID      ", DA_TP }, { 'p', "I2C", DA_TP }, { 'p', "HID CPU", DA_SYS }, { 'p', "HID PCI", DA_SYS },
    { 'p', "USB mouse", DA_MOUSE }, { 'p', "USB mice", DA_MOUSE }, { 'p', "PS/2 mouse", DA_MOUSE }, { 'p', "  packets", DA_MOUSE }, { 'p', "HID ", DA_MOUSE },
    { 'p', "USB gamepad", DA_PAD }, { 'p', "pad ", DA_PAD },
    { 'p', "wifi", DA_NET }, { 'p', "NET", DA_NET }, { 'p', "nic", DA_NET }, { 's', "ethernet", DA_NET },
    { 'p', "video back buffer", DA_VID }, { 'p', "perf/irq", DA_PERF }, { 'p', "screen", DA_VID }, { 'p', "display:", DA_VID }, { 'p', "gpu", DA_VID }, { 'p', " BAR", DA_VID }, { 'p', "note: no AMD", DA_VID }, { 'p', "amd ", DA_VID }, { 'p', "gfx", DA_VID },
    { 'p', "vsync", DA_VID }, { 'p', "VGA", DA_VID }, { 'p', "> ", DA_VID }, { 'p', "    modes", DA_VID }, { 'p', "SRC", DA_VID }, { 'p', "STR", DA_VID }, { 'p', "HTA", DA_VID },
    { 'p', "USB MIDI", DA_USB }, { 'p', "USB ", DA_USB }, { 'p', "USB devices", DA_USB },
    { 'p', "boot mode", DA_SYS }, { 'p', "ACPI", DA_SYS }, { 'p', "SMBIOS", DA_SYS }, { 'p', "file", DA_SYS }, { 'p', "R 0:", DA_SYS }, { 'p', "W 0:", DA_SYS },
};

DiagArea diag_area_of(const char *l)
{
    if (!l || !l[0]) return DA_SYS;
    for (unsigned i = 0; i < sizeof rules / sizeof rules[0]; i++)
        if (rules[i].how == 'p' ? starts(l, rules[i].pat) : has(l, rules[i].pat)) return rules[i].a;
    if (has(l, "keyboard")) return DA_KBD;
    if (has(l, "mouse") || has(l, "mice")) return DA_MOUSE;
    if (has(l, "audio")) return DA_SND;
    return DA_SYS;
}

static void cpy(char *d, int max, const char *s) { int i = 0; for (; s && s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }
static void cat(char *d, int max, const char *s) { int i = 0; while (d[i]) i++; for (; s && *s && i < max - 1; s++) d[i++] = *s; d[i] = 0; }
static void catn(char *d, int max, uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); char r[12]; int k = 0; while (n) r[k++] = t[--n]; r[k] = 0; cat(d, max, r); }
static void set(DiagSum *s, DiagState st, const char *why, const char *text) { s->st = st; cpy(s->why, sizeof s->why, why ? why : ""); cpy(s->text, sizeof s->text, text); }
static const char *find(char (*lines)[64], int n, const char *pfx) { for (int i = 0; i < n; i++) if (starts(lines[i], pfx)) return lines[i]; return 0; }
static const char *finds(char (*lines)[64], int n, const char *w) { for (int i = 0; i < n; i++) if (has(lines[i], w)) return lines[i]; return 0; }
static int num_after(const char *l, const char *key)  /* "USB keyboards: 1" → 1, 없으면 -1 */
{
    if (!l) return -1;
    for (const char *s = l; *s; s++) if (starts(s, key)) { s += 0; while (*s && !(*s >= '0' && *s <= '9')) s++; int v = 0, any = 0; while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); any = 1; } return any ? v : -1; }
    return -1;
}

void diag_judge(const DiagCtx *c, char (*lines)[64], int n, DiagSum o[DA_N])
{
    for (int a = 0; a < DA_N; a++) set(&o[a], DS_INFO, "", "");
    char t[96];
    /* SYS: 부팅 방식 + 기종(SMBIOS) */
    {
        const char *bm = find(lines, n, "boot mode"), *sm = find(lines, n, "SMBIOS system");
        cpy(t, sizeof t, bm ? bm + 11 : "boot mode ?");
        if (sm) { cat(t, sizeof t, " | "); cat(t, sizeof t, sm + 14); }
        set(&o[DA_SYS], bm ? DS_OK : DS_INFO, "", t);
    }
    /* VID: 화면 + 쓰기 결합(WC) + vsync */
    {
        const char *sc = find(lines, n, "screen ");
        cpy(t, sizeof t, sc ? sc + 7 : "screen ?");
        int wc_on = c->wc_status && has(c->wc_status, "on");
        cat(t, sizeof t, wc_on ? " | WC on" : " | WC off");
        if (c->vs_status && c->vs_status[0]) { cat(t, sizeof t, " | vsync "); cat(t, sizeof t, c->vs_status); }
        set(&o[DA_VID], wc_on ? DS_OK : DS_WARN, wc_on ? "" : "no_write_combining", t);
        const char *bb = find(lines, n, "video back buffer");     /* 뒤 버퍼가 MTRR UC 자리면 화면 전체 그리기가 수백 배 느림 (LG gram 멈춤) */
        if (bb && has(bb, "mtrr UC")) { cat(t, sizeof t, " | back buffer UC"); set(&o[DA_VID], DS_FAIL, "backbuffer_uncached", t); }
    }
    /* SND: 소리 상태 문자열 + HDA 줄 */
    {
        const char *ss = c->snd_status ? c->snd_status : "", *hl = find(lines, n, "HDA ");
        cpy(t, sizeof t, ss);
        if (!ss[0] && !hl) { set(&o[DA_SND], DS_NA, "no_data", "sound not initialised"); goto snd_done; }
        if (hl) { cat(t, sizeof t, " | "); cat(t, sizeof t, hl + 4); }
        if (has(ss, "snd: on")) set(&o[DA_SND], DS_OK, "", t);
        else if (has(ss, "no HDA")) set(&o[DA_SND], DS_FAIL, "no_hda", t);
        else if (has(ss, "no codec")) set(&o[DA_SND], DS_FAIL, "no_codec", t);
        else if (has(ss, "stuck")) set(&o[DA_SND], DS_FAIL, "stream_stuck", t);
        else if (has(ss, "no output") && hl && has(hl, "stuck")) set(&o[DA_SND], DS_FAIL, "stream_stuck", t);
        else if (has(ss, "no output")) set(&o[DA_SND], DS_FAIL, "no_output_pin", t);
        else if (has(ss, "reset fail")) set(&o[DA_SND], DS_FAIL, "ctrl_reset", t);
        else set(&o[DA_SND], DS_WARN, "off", t);
        if (o[DA_SND].st == DS_OK && hl && has(hl, " est")) { o[DA_SND].st = DS_WARN; cpy(o[DA_SND].why, sizeof o[DA_SND].why, "pos_estimated"); }
    snd_done:;
    }
    /* KBD: 8042 또는 USB 키보드 */
    {
        int k8 = finds(lines, n, "8042 keyboard: present") != 0, ku = num_after(find(lines, n, "USB keyboards"), "USB keyboards");
        t[0] = 0;
        cat(t, sizeof t, k8 ? "8042 present" : "no 8042");
        if (ku >= 0) { cat(t, sizeof t, " | USB keyboards "); catn(t, sizeof t, (uint32_t)ku); }
        if (!finds(lines, n, "8042") && ku < 0) set(&o[DA_KBD], DS_NA, "no_data", t);
        else if (k8 || ku > 0) set(&o[DA_KBD], DS_OK, "", t); else set(&o[DA_KBD], DS_FAIL, "no_keyboard", t);
    }
    /* MOUSE */
    {
        int mu = num_after(find(lines, n, "USB mice"), "USB mice");
        const char *ps = find(lines, n, "PS/2 mouse");
        int ps_on = ps && !has(ps, "none");
        t[0] = 0; cat(t, sizeof t, "USB mice "); catn(t, sizeof t, mu < 0 ? 0u : (uint32_t)mu); cat(t, sizeof t, ps_on ? " | PS/2 on" : " | PS/2 none");
        if (mu < 0 && !ps) set(&o[DA_MOUSE], DS_NA, "no_data", t);
        else set(&o[DA_MOUSE], mu > 0 || ps_on ? DS_OK : DS_INFO, mu > 0 || ps_on ? "" : "no_mouse", t);
    }
    /* TP: ACPI 에 터치패드가 있는데 쓰지 못하면 주의 */
    {
        const char *ta = find(lines, n, "HID touchpad ACPI"), *pr = find(lines, n, "HID I2C probe"), *cpu = find(lines, n, "HID CPU");
        int used = finds(lines, n, "I2C touchpad") != 0 || finds(lines, n, "touchpad: on") != 0;
        if (!ta && !cpu) set(&o[DA_TP], DS_NA, "no_data", "touchpad probe did not run (legacy boot?)");
        else if (!ta) set(&o[DA_TP], DS_INFO, "", "no touchpad in ACPI");
        else if (used) set(&o[DA_TP], DS_OK, "", ta + 4);
        else {
            cpy(t, sizeof t, ta + 4); if (pr) { cat(t, sizeof t, " | "); cat(t, sizeof t, pr + 4); }
            set(&o[DA_TP], DS_WARN, cpu && has(cpu, "Intel") ? "i2c_intel_unsupported" : "i2c_tp_not_found", t);
        }
    }
    /* PAD */
    {
        int pu = num_after(find(lines, n, "USB gamepads"), "USB gamepads");
        t[0] = 0; cat(t, sizeof t, "USB gamepads "); catn(t, sizeof t, pu < 0 ? 0u : (uint32_t)pu);
        if (pu < 0) set(&o[DA_PAD], DS_NA, "no_data", "gamepad list not available (legacy boot?)");
        else set(&o[DA_PAD], pu > 0 ? DS_OK : DS_INFO, "", t);
    }
    /* USB: 장치 수 (USB VVVV:DDDD 줄) */
    {
        int dev = 0, stor = 0;
        for (int i = 0; i < n; i++) if (starts(lines[i], "USB ") && ishex(lines[i][4]) && lines[i][8] == ':') { dev++; if (has(lines[i], "storage")) stor++; }
        t[0] = 0; cat(t, sizeof t, "interfaces "); catn(t, sizeof t, (uint32_t)dev); cat(t, sizeof t, ", storage "); catn(t, sizeof t, (uint32_t)stor);
        if (c->usb_status && c->usb_status[0]) { cat(t, sizeof t, " | "); cat(t, sizeof t, c->usb_status); }
        if (!dev && find(lines, n, "boot mode: legacy")) set(&o[DA_USB], DS_NA, "legacy_bios_usb", "USB handled by BIOS (legacy boot): device list not available");
        else set(&o[DA_USB], dev ? DS_OK : DS_INFO, "", t);
    }
    /* NET: 무선 칩이 있는데 드라이버가 없으면 주의 */
    {
        const char *wn = find(lines, n, "wifi 0 name"), *wf = finds(lines, n, "not found"), *wid = find(lines, n, "wifi 0 ");
        if (!wid) set(&o[DA_NET], DS_INFO, "", "no wifi chip");
        else {
            cpy(t, sizeof t, wn ? wn + 7 : wid + 7);
            if (wid) { cat(t, sizeof t, " | "); cat(t, sizeof t, wid + 7); }
            int unsup = wf && has(wf, "wifi");
            set(&o[DA_NET], unsup ? DS_WARN : DS_OK, unsup ? "wifi_unsupported" : "", t);
        }
    }
    /* PERF */
    {
        t[0] = 0; cat(t, sizeof t, "speed "); catn(t, sizeof t, (uint32_t)(c->speed < 0 ? 0 : c->speed)); cat(t, sizeof t, "% busy ");
        catn(t, sizeof t, (uint32_t)(c->busy < 0 ? 0 : c->busy)); cat(t, sizeof t, "% gap max "); catn(t, sizeof t, (uint32_t)(c->gap_ms < 0 ? 0 : c->gap_ms)); cat(t, sizeof t, " ms");
        if (c->speed <= 0) set(&o[DA_PERF], DS_NA, "not_measured_yet", t);
        else if (c->speed > 0 && c->speed < 90) set(&o[DA_PERF], DS_WARN, "slow", t);
        else if (c->gap_ms > 40) set(&o[DA_PERF], DS_WARN, "frame_gap", t);
        else if (c->busy > 90) set(&o[DA_PERF], DS_WARN, "cpu_busy", t);
        else set(&o[DA_PERF], DS_OK, "", t);
    }
}

/* ---- 5.2.26: 사용자 시험 PC (TESTPC.TXT) ---- */
typedef struct { DiagPc pc; char name[64], gpu[12], smbios[48], boot[8], codec[12], note[112]; } DiagPcU;
static DiagPcU upc[DIAG_UPC_MAX];
static int nupc;
int diag_pc_count(void) { return diag_npcs + nupc; }
const DiagPc *diag_pc(int i) { return i < 0 ? 0 : i < diag_npcs ? &diag_pcs[i] : i < diag_npcs + nupc ? &upc[i - diag_npcs].pc : 0; }
int diag_pc_is_user(int i) { return i >= diag_npcs && i < diag_npcs + nupc; }
static void upc_link(DiagPcU *u)
{
    u->pc.name = u->name; u->pc.gpu = u->gpu[0] ? u->gpu : 0; u->pc.smbios = u->smbios[0] ? u->smbios : 0;
    u->pc.boot = u->boot[0] ? u->boot : "?"; u->pc.codec = u->codec[0] ? u->codec : 0; u->pc.note = u->note;
}
static const char exc[4] = { 'U', 'O', 'X', 'N' };      /* DiagExpect 차례 */
int diag_pc_user_load(const char *t, int len)
{
    nupc = 0;
    int i = 0;
    while (i < len && nupc < DIAG_UPC_MAX) {
        int e = i; while (e < len && t[e] != '\n' && t[e] != '\r') e++;
        int p = i; while (p < e && (t[p] == ' ' || t[p] == '\t')) p++;
        if (p < e && t[p] != '#' && t[p] != ';') {
            DiagPcU *u = &upc[nupc]; *u = (DiagPcU){ 0 };
            int got = 0;
            while (p < e) {
                while (p < e && (t[p] == ' ' || t[p] == '\t')) p++;
                char k[12]; int kn = 0; while (p < e && t[p] != '=' && t[p] != ' ' && kn < 11) k[kn++] = (char)lc(t[p++]); k[kn] = 0;
                if (p >= e || t[p] != '=') { while (p < e && t[p] != ' ') p++; continue; }
                p++;
                char v[112]; int vn = 0;
                if (p < e && t[p] == '"') { p++; while (p < e && t[p] != '"') { if (vn < 111) v[vn++] = t[p]; p++; } if (p < e) p++; }
                else while (p < e && t[p] != ' ' && t[p] != '\t') { if (vn < 111) v[vn++] = t[p]; p++; }
                v[vn] = 0; if (vn == 1 && v[0] == '-') v[0] = 0;
                if (!kn) continue;
                if (starts(k, "pc") && !k[2]) { int x = 0; for (const char *c = v; *c >= '0' && *c <= '9'; c++) x = x * 10 + (*c - '0'); u->pc.pc = x; }
                else if (starts(k, "name")) { cpy(u->name, sizeof u->name, v); got = 1; }
                else if (starts(k, "smbios")) cpy(u->smbios, sizeof u->smbios, v);
                else if (starts(k, "gpu")) cpy(u->gpu, sizeof u->gpu, v);
                else if (starts(k, "codec")) cpy(u->codec, sizeof u->codec, v);
                else if (starts(k, "boot")) cpy(u->boot, sizeof u->boot, v);
                else if (starts(k, "note")) cpy(u->note, sizeof u->note, v);
                else if (starts(k, "ex")) for (int a = 0; a < DA_N && v[a]; a++) { int c = v[a] >= 'a' ? v[a] - 32 : v[a]; u->pc.ex[a] = (uint8_t)(c == 'O' ? EX_OK : c == 'X' ? EX_UNSUP : c == 'N' ? EX_NONE : EX_UNKNOWN); }
            }
            if (got && (u->smbios[0] || u->gpu[0] || u->name[0])) { upc_link(u); nupc++; }
        }
        i = e; while (i < len && (t[i] == '\n' || t[i] == '\r')) i++;
    }
    return nupc;
}
static void hexid(const char *l, const char *after, char *out, int max)     /* "codec 10EC:0298" → "10EC:0298" */
{
    out[0] = 0;
    for (; *l; l++) if (starts(l, after)) {
        const char *q = l + 0; while (*q && *q != ' ') q++; while (*q == ' ') q++;
        int k = 0; while (q[k] && (ishex(q[k]) || q[k] == ':') && k < max - 1) { out[k] = q[k]; k++; } out[k] = 0;
        if (k == 9 && out[4] == ':') return;
        out[0] = 0;
    }
}
int diag_pc_user_add(char (*lines)[64], int n, const DiagSum s[DA_N], const char *ver)
{
    const char *sm = find(lines, n, "SMBIOS system "), *gl = 0, *hl = 0, *bl = find(lines, n, "boot mode");
    for (int k = 0; k < n && !gl; k++) if (starts(lines[k], "gpu ") || starts(lines[k], "screen gpu ")) gl = lines[k];
    for (int k = 0; k < n && !hl; k++) if (starts(lines[k], "HDA ") && has(lines[k], "codec")) hl = lines[k];
    DiagPcU t = { 0 };
    if (sm) { cpy(t.smbios, sizeof t.smbios, sm + 14); cpy(t.name, sizeof t.name, sm + 14); }
    if (gl) { for (const char *q = gl; *q; q++) if (ishex(q[0]) && ishex(q[1]) && ishex(q[2]) && ishex(q[3]) && q[4] == ':' && ishex(q[5])) { int k = 0; while (k < 9 && q[k]) { t.gpu[k] = q[k]; k++; } t.gpu[k] = 0; break; } }
    if (hl) hexid(hl, "codec", t.codec, sizeof t.codec);
    if (!t.smbios[0] && !t.gpu[0]) return -1;           /* 다시 알아볼 수 없는 PC */
    if (!t.name[0]) { cpy(t.name, sizeof t.name, "PC gpu "); cat(t.name, sizeof t.name, t.gpu); }
    cpy(t.boot, sizeof t.boot, bl && has(bl, "UEFI") ? "uefi" : bl && has(bl, "legacy") ? "legacy" : "?");
    for (int a = 0; a < DA_N; a++)                      /* 지금 상태가 기준: 정상 = 동작해야 함, 주의·실패 = 지원 안 함, 나머지 = 모름 */
        t.pc.ex[a] = (uint8_t)(s[a].st == DS_OK ? EX_OK : s[a].st == DS_WARN || s[a].st == DS_FAIL ? EX_UNSUP : EX_UNKNOWN);
    cpy(t.note, sizeof t.note, "added in X86MSX "); cat(t.note, sizeof t.note, ver ? ver : "?"); cat(t.note, sizeof t.note, " from its own diag (edit TESTPC.TXT)");
    int slot = -1, maxpc = 5;
    for (int i = 0; i < diag_npcs; i++) if (diag_pcs[i].pc > maxpc) maxpc = diag_pcs[i].pc;
    for (int i = 0; i < nupc; i++) {
        if (upc[i].pc.pc > maxpc) maxpc = upc[i].pc.pc;
        if (slot < 0 && ((t.smbios[0] && upc[i].smbios[0] && has(t.smbios, upc[i].smbios)) || (!t.smbios[0] && t.gpu[0] && !upc[i].smbios[0] && has(upc[i].gpu, t.gpu)))) slot = i;
    }
    if (slot >= 0) { t.pc.pc = upc[slot].pc.pc; }       /* 같은 PC 는 번호 그대로 새 기준으로 */
    else { if (nupc >= DIAG_UPC_MAX) return -1; slot = nupc++; t.pc.pc = maxpc + 1; }
    upc[slot] = t; upc_link(&upc[slot]);
    return diag_npcs + slot;
}
static int put(char *b, int q, int max, const char *s);
int diag_pc_user_save(char *b, int max)
{
    int q = 0; b[0] = 0;
    q = put(b, q, max, "# X86MSX test PCs added by the user (diag > Summary > Test PC > + Add this PC). One PC per line.\r\n"
                       "# ex = expectation per area SYS VID SND KBD MOUSE TP PAD USB NET PERF: O must work, X known unsupported, N no device, U unknown\r\n");
    for (int i = 0; i < nupc; i++) {
        const DiagPcU *u = &upc[i]; char nb[12] = ""; catn(nb, sizeof nb, (uint32_t)u->pc.pc);
        char ex[DA_N + 1]; for (int a = 0; a < DA_N; a++) ex[a] = exc[u->pc.ex[a] & 3]; ex[DA_N] = 0;
        q = put(b, q, max, "pc="); q = put(b, q, max, nb);
        q = put(b, q, max, " name=\""); q = put(b, q, max, u->name);
        q = put(b, q, max, "\" smbios=\""); q = put(b, q, max, u->smbios);
        q = put(b, q, max, "\" gpu="); q = put(b, q, max, u->gpu[0] ? u->gpu : "-");
        q = put(b, q, max, " codec="); q = put(b, q, max, u->codec[0] ? u->codec : "-");
        q = put(b, q, max, " boot="); q = put(b, q, max, u->boot);
        q = put(b, q, max, " ex="); q = put(b, q, max, ex);
        q = put(b, q, max, " note=\""); q = put(b, q, max, u->note); q = put(b, q, max, "\"\r\n");
    }
    return q;
}

int diag_pc_auto(char (*lines)[64], int n)
{
    const char *sm = find(lines, n, "SMBIOS system");
    int cnt = diag_pc_count();
    for (int pass = 0; pass < 2; pass++)                /* 사용자 PC 먼저 (같은 PC 를 사용자가 다시 정했을 때) */
        for (int i = 0; i < cnt; i++) { if (diag_pc_is_user(i) != !pass) continue; const DiagPc *p = diag_pc(i); if (sm && p->smbios && has(sm, p->smbios)) return i; }
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < cnt; i++) { if (diag_pc_is_user(i) != !pass) continue; const DiagPc *p = diag_pc(i); if (p->gpu)
            for (int k = 0; k < n; k++) if ((starts(lines[k], "gpu ") || starts(lines[k], "screen gpu ")) && has(lines[k], p->gpu)) return i; }
    return -1;
}

void diag_expect(const DiagPc *pc, char (*lines)[64], int n, DiagSum s[DA_N])
{
    if (!pc) return;
    for (int a = 0; a < DA_N; a++) {
        DiagSum *d = &s[a];
        if (pc->ex[a] == EX_UNSUP && (d->st == DS_WARN || d->st == DS_FAIL)) { d->st = DS_INFO; cpy(d->why, sizeof d->why, "known_unsupported"); }
        else if (pc->ex[a] == EX_NONE && d->st != DS_OK) { d->st = DS_INFO; cpy(d->why, sizeof d->why, "no_device"); }
        else if (pc->ex[a] == EX_OK && (d->st == DS_FAIL || d->st == DS_WARN || d->st == DS_NA)) {
            char w[32] = "regress_"; cat(w, sizeof w, d->why[0] ? d->why : "unknown"); cpy(d->why, sizeof d->why, w);
            if (d->st != DS_NA) d->st = DS_FAIL;
        }
    }
    if (pc->codec) {                                    /* 소리 코덱이 표와 다름 → 다른 PC 이거나 코덱 판별이 틀림 */
        const char *hl = 0;
        for (int i = 0; i < n && !hl; i++) if (starts(lines[i], "HDA ") && has(lines[i], "codec")) hl = lines[i];
        if (hl && !has(hl, pc->codec) && s[DA_SND].st == DS_OK) { s[DA_SND].st = DS_WARN; cpy(s[DA_SND].why, sizeof s[DA_SND].why, "codec_mismatch"); }
    }
}

static int put(char *b, int q, int max, const char *s) { while (s && *s && q < max - 1) b[q++] = *s++; b[q] = 0; return q; }

int diag_format(char *b, int max, const char *ver, const DiagCtx *c, char (*lines)[64], int n, const DiagSum s[DA_N], const DiagPc *pc, int pc_auto)
{
    static const char *const stn[5] = { "info", "ok", "warn", "fail", "na" };
    static const char *const exn[4] = { "?", "ok", "unsup", "none" };
    int q = 0;
    b[0] = 0;
    q = put(b, q, max, "#LOG 1\r\n#prog=X86MSX ver="); q = put(b, q, max, ver); q = put(b, q, max, "\r\n");
    {   /* #pc= 번호 name= 고른 시험 PC (sel=auto|manual|none), smbios= 펌웨어가 알려 준 이름 */
        const char *sm = find(lines, n, "SMBIOS system");
        q = put(b, q, max, "#pc=");
        if (pc && pc->pc) { char t[8] = ""; catn(t, sizeof t, (uint32_t)pc->pc); q = put(b, q, max, t); } else q = put(b, q, max, "?");
        q = put(b, q, max, " name=\""); q = put(b, q, max, pc ? pc->name : "?"); q = put(b, q, max, "\" sel=");
        q = put(b, q, max, !pc ? "none" : pc_auto ? "auto" : "manual");
        q = put(b, q, max, " smbios=\""); q = put(b, q, max, sm ? sm + 14 : "?"); q = put(b, q, max, "\"\r\n");
        if (pc) { q = put(b, q, max, "#pc_boot="); q = put(b, q, max, pc->boot); q = put(b, q, max, " note=\""); q = put(b, q, max, pc->note); q = put(b, q, max, "\"\r\n"); }
    }
    { char t[64] = "#pcs builtin="; catn(t, sizeof t, (uint32_t)diag_npcs); cat(t, sizeof t, " user="); catn(t, sizeof t, (uint32_t)nupc); cat(t, sizeof t, " file=TESTPC.TXT\r\n"); q = put(b, q, max, t); }
    for (int i = 0; i < nupc; i++) {                    /* 사용자 PC 줄 그대로 (TESTPC.TXT 를 안 보내도 알 수 있게) */
        char nb[12] = ""; catn(nb, sizeof nb, (uint32_t)upc[i].pc.pc); char ex[DA_N + 1]; for (int a = 0; a < DA_N; a++) ex[a] = exc[upc[i].pc.ex[a] & 3]; ex[DA_N] = 0;
        q = put(b, q, max, "#upc pc="); q = put(b, q, max, nb); q = put(b, q, max, " smbios=\""); q = put(b, q, max, upc[i].smbios); q = put(b, q, max, "\" gpu="); q = put(b, q, max, upc[i].gpu[0] ? upc[i].gpu : "-"); q = put(b, q, max, " ex="); q = put(b, q, max, ex); q = put(b, q, max, "\r\n");
    }
    { const char *bi = find(lines, n, "SMBIOS bios"); if (bi) { q = put(b, q, max, "#bios "); q = put(b, q, max, bi + 12); q = put(b, q, max, "\r\n"); } }
    q = put(b, q, max, "#machine \""); q = put(b, q, max, c->machine ? c->machine : "?"); q = put(b, q, max, "\"\r\n");
    q = put(b, q, max, "== summary (area res why text) ==\r\n");
    for (int a = 0; a < DA_N; a++) {
        q = put(b, q, max, "SUM."); q = put(b, q, max, diag_area_key[a]); q = put(b, q, max, " res="); q = put(b, q, max, stn[s[a].st]);
        if (s[a].why[0]) { q = put(b, q, max, " why="); q = put(b, q, max, s[a].why); }
        if (pc) { q = put(b, q, max, " expect="); q = put(b, q, max, exn[pc->ex[a] & 3]); }
        q = put(b, q, max, " \""); q = put(b, q, max, s[a].text); q = put(b, q, max, "\"\r\n");
    }
    for (int a = 0; a < DA_N; a++) {
        int any = 0;
        for (int i = 0; i < n; i++) {
            if (!lines[i][0] || diag_area_of(lines[i]) != (DiagArea)a) continue;
            if (!any) { q = put(b, q, max, "== "); q = put(b, q, max, diag_area_key[a]); q = put(b, q, max, " ==\r\n"); any = 1; }
            q = put(b, q, max, lines[i]); q = put(b, q, max, "\r\n");
        }
    }
    return q;
}
