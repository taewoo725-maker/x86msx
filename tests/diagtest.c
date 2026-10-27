/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 5.2.26 진단 분류·판정 시험 (kernel/diag.c) — 실제 시험 PC 의 LOG 줄로 */
#include "../kernel/diag.h"
#include "../kernel/smbios.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char L[200][64];
static int load_info(const char *path)                  /* DIAG.TXT 의 == info == 아래 줄 */
{
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    char s[256]; int n = 0, on = 0;
    while (fgets(s, sizeof s, f) && n < 200) {
        s[strcspn(s, "\r\n")] = 0;
        if (!strcmp(s, "== info ==")) { on = 1; continue; }
        if (!strncmp(s, "== steps", 8)) break;
        if (on) { strncpy(L[n], s, 63); L[n][63] = 0; n++; }
    }
    fclose(f);
    return n;
}

int main(void)
{
    /* 분류 */
    CHECK(diag_area_of("HDA 00:1F.3 8086:7E28 codec 10EC:0298 pins 17,21 corb <- used") == DA_SND, "HDA");
    CHECK(diag_area_of("VT-d: no ACPI 2") == DA_SND, "VT-d");
    CHECK(diag_area_of("8042 keyboard: present, firmware driver detached") == DA_KBD, "8042");
    CHECK(diag_area_of("USB keyboards: 1  (firmware USB stack, hubs OK)") == DA_KBD, "usb kbd");
    CHECK(diag_area_of("HID touchpad ACPI: MSFT0001 PNP0C500 ") == DA_TP, "tp");
    CHECK(diag_area_of("HID      INTC1077 INTC1073 INTC109B ") == DA_TP, "tp cont");
    CHECK(diag_area_of("HID CPU: GenuineIntel fam 6 model AA") == DA_SYS, "cpu");
    CHECK(diag_area_of("HID 046D:C52B iface 0 1/1 -> keyboard (Logitech)") == DA_MOUSE, "hid iface");
    CHECK(diag_area_of("USB mice: 1") == DA_MOUSE, "mice");
    CHECK(diag_area_of("wifi 0 00:14.3 8086:7E40 rev 20") == DA_NET, "wifi");
    CHECK(diag_area_of("screen 1920x1080 scale x5") == DA_VID, "screen");
    CHECK(diag_area_of(" BAR0=1800000C 1=00000050 2=0000000C 3=00000040 CMD=0007") == DA_VID, "bar");
    CHECK(diag_area_of("USB 0781:55A3 if0 storage 08/06/50") == DA_USB, "usb dev");
    CHECK(diag_area_of("video back buffer: 200000000 mtrr UC") == DA_VID, "backbuf");
    CHECK(diag_area_of("perf/irq bench us cpu 21") == DA_PERF, "perfirq");
    {   char Z[2][64] = { "screen 1920x1080 scale x5", "video back buffer: 200000000 mtrr UC" }; DiagCtx cz = { "snd: on", "WC: on", "", "", "", 100, 1, 17 }; DiagSum zs[DA_N];
        diag_judge(&cz, Z, 2, zs); CHECK(zs[DA_VID].st == DS_FAIL && !strcmp(zs[DA_VID].why, "backbuffer_uncached"), "backbuf uc %s", zs[DA_VID].why); }
    CHECK(diag_area_of("boot mode: UEFI, 64-bit (firmware services kept)") == DA_SYS, "boot");
    CHECK(diag_area_of("SMBIOS system LG Electronics 16Z90S-GA5VK") == DA_SYS, "smbios");

    /* LG gram 16Z90S, 5.2.23 (소리 남, 터치패드·무선 미지원) */
    int n = load_info("tests/data/diag_gram_5223.txt");
    CHECK(n > 40, "load %d", n);
    DiagCtx c = { "snd: on", "WC: on (PAT)", "off (timer pacing)", "usb: kbd 1", "MSX2 - Daewoo CPC-400", 100, 2, 17 };
    DiagSum s[DA_N];
    diag_judge(&c, L, n, s);
    CHECK(s[DA_SND].st == DS_OK, "gram snd %d %s", s[DA_SND].st, s[DA_SND].why);
    CHECK(s[DA_KBD].st == DS_OK, "gram kbd");
    CHECK(s[DA_TP].st == DS_WARN && !strcmp(s[DA_TP].why, "i2c_intel_unsupported"), "gram tp %s", s[DA_TP].why);
    CHECK(s[DA_NET].st == DS_WARN && !strcmp(s[DA_NET].why, "wifi_unsupported"), "gram net %s", s[DA_NET].why);
    CHECK(s[DA_VID].st == DS_OK, "gram vid");
    CHECK(s[DA_PERF].st == DS_OK, "gram perf");
    CHECK(s[DA_USB].st == DS_OK, "gram usb");
    CHECK(s[DA_MOUSE].st == DS_OK, "gram mouse");

    /* 시험 PC 자동 판별·예상 반영 */
    int pi = diag_pc_auto(L, n);
    CHECK(pi >= 0 && strstr(diag_pcs[pi].name, "16Z90S"), "auto pc %d", pi);
    if (pi >= 0) {
        diag_expect(&diag_pcs[pi], L, n, s);
        CHECK(s[DA_TP].st == DS_INFO && !strcmp(s[DA_TP].why, "known_unsupported"), "expect tp %s", s[DA_TP].why);
        CHECK(s[DA_NET].st == DS_INFO, "expect net");
        CHECK(s[DA_SND].st == DS_OK, "expect snd ok");
    }
    {   /* 코덱이 표와 다르면 주의 */
        char save[64]; int hi = -1; for (int i = 0; i < n; i++) if (!strncmp(L[i], "HDA ", 4) && strstr(L[i], "codec")) hi = i;
        if (hi >= 0 && pi >= 0) { strcpy(save, L[hi]); strcpy(L[hi], "HDA 00:1F.3 8086:7E28 codec 14F1:510F pins 17,21 corb <- used");
          DiagSum e[DA_N]; diag_judge(&c, L, n, e); diag_expect(&diag_pcs[pi], L, n, e);
          CHECK(e[DA_SND].st == DS_WARN && !strcmp(e[DA_SND].why, "codec_mismatch"), "codec mismatch %s", e[DA_SND].why); strcpy(L[hi], save); }
    }
    /* 진단 안 됨: 줄이 없으면 */
    { DiagSum e[DA_N]; DiagCtx z = { "", "", "", "", "?", 0, 0, 0 }; diag_judge(&z, L, 0, e);
      CHECK(e[DA_KBD].st == DS_NA && e[DA_SND].st == DS_NA && e[DA_TP].st == DS_NA && e[DA_PERF].st == DS_NA, "na %d %d %d %d", e[DA_KBD].st, e[DA_SND].st, e[DA_TP].st, e[DA_PERF].st);
      diag_expect(&diag_pcs[pi < 0 ? 0 : pi], L, 0, e);
      CHECK(!strncmp(e[DA_KBD].why, "regress_", 8) && e[DA_KBD].st == DS_NA, "na expect %s", e[DA_KBD].why); }

    /* 5.2.19 같은 PC: 스트림 멈춤 */
    strcpy(L[n], "HDA 00:1F.3 8086:7E28 codec 10EC:0298 pins 17,21 stuck retry re");
    for (int i = 0; i < n; i++) if (!strncmp(L[i], "HDA ", 4)) strcpy(L[i], L[n]);
    DiagCtx c2 = c; c2.snd_status = "snd: no output";
    diag_judge(&c2, L, n, s);
    CHECK(s[DA_SND].st == DS_FAIL && !strcmp(s[DA_SND].why, "stream_stuck"), "stuck %s", s[DA_SND].why);
    if (pi >= 0) { diag_expect(&diag_pcs[pi], L, n, s); CHECK(s[DA_SND].st == DS_FAIL && !strcmp(s[DA_SND].why, "regress_stream_stuck"), "regress %s", s[DA_SND].why); }
    c2.snd_status = "snd: no HDA"; diag_judge(&c2, L, n, s);
    CHECK(s[DA_SND].st == DS_FAIL && !strcmp(s[DA_SND].why, "no_hda"), "no hda");

    /* 성능 */
    DiagCtx c3 = c; c3.speed = 17; c3.busy = 84; diag_judge(&c3, L, n, s);
    CHECK(s[DA_PERF].st == DS_WARN && !strcmp(s[DA_PERF].why, "slow"), "slow");
    c3.speed = 100; c3.gap_ms = 642; diag_judge(&c3, L, n, s);
    CHECK(s[DA_PERF].st == DS_WARN && !strcmp(s[DA_PERF].why, "frame_gap"), "gap");

    /* 로그 글: 요약 줄과 영역 머리 */
    static char b[16384];
    diag_judge(&c, L, n, s);
    int len = diag_format(b, sizeof b, "5.2.26", &c, L, n, s, 0, 0);
    CHECK(len > 500 && strstr(b, "SUM.SND res=ok") && strstr(b, "SUM.TP res=warn why=i2c_intel_unsupported") && strstr(b, "== SND ==") && strstr(b, "#LOG 1"), "format");
    if (pi >= 0) { diag_expect(&diag_pcs[pi], L, n, s); len = diag_format(b, sizeof b, "5.2.26", &c, L, n, s, &diag_pcs[pi], 1);
      CHECK(strstr(b, "#pc=? name=\"LG gram") && strstr(b, "sel=auto") && strstr(b, "SUM.TP res=info why=known_unsupported expect=unsup"), "format pc"); }
    {   /* SMBIOS: 만든 표 (BIOS·시스템·보드) */
        static uint8_t tb[256], ep[32];
        int k = 0;
        uint8_t t0[] = { 0, 0x12, 0, 0, 1, 2, 0, 0, 3 }; for (unsigned i = 0; i < sizeof t0; i++) tb[k++] = t0[i]; tb[1] = (uint8_t)sizeof t0;
        const char *s0 = "Phoenix\0V1.2\0" "05/01/2024\0\0"; for (int i = 0; i < 25; i++) tb[k++] = (uint8_t)s0[i];
        uint8_t t1[] = { 1, 8, 1, 0, 1, 2, 0, 0 }; for (unsigned i = 0; i < sizeof t1; i++) tb[k++] = t1[i];
        const char *s1 = "LG Electronics\0" "16Z90S-GA5VK\0\0"; for (int i = 0; i < 29; i++) tb[k++] = (uint8_t)s1[i];
        uint8_t t2[] = { 127, 4, 2, 0, 0, 0 }; for (unsigned i = 0; i < sizeof t2; i++) tb[k++] = t2[i];
        memcpy(ep, "_SM3_", 5); ep[0x0C] = (uint8_t)k; uint64_t a = (uint64_t)(uintptr_t)tb; memcpy(ep + 0x10, &a, 8);
        char sl[4][64]; int sn = smbios_lines(ep, sl, 4);
        CHECK(sn == 2 && !strcmp(sl[0], "SMBIOS system LG Electronics 16Z90S-GA5VK") && !strcmp(sl[1], "SMBIOS bios Phoenix V1.2 05/01/2024"), "smbios %d '%s' '%s'", sn, sn > 0 ? sl[0] : "", sn > 1 ? sl[1] : "");
    }
    {   /* 사용자 시험 PC — 지금 PC 더하기 · 저장 · 다시 읽기 · 자동 판별 */
        strcpy(L[n], "SMBIOS system LG Electronics 16Z90S-GA5VK"); n++;
        DiagCtx cu = c; DiagSum u[DA_N]; diag_judge(&cu, L, n, u);
        static char tb[4096];
        CHECK(diag_pc_count() == diag_npcs, "no user");
        int ui = diag_pc_user_add(L, n, u, "5.2.26");
        CHECK(ui == diag_npcs && diag_pc_count() == diag_npcs + 1 && diag_pc_is_user(ui), "add %d", ui);
        const DiagPc *up = diag_pc(ui);
        CHECK(up && up->pc == 6 && !strcmp(up->smbios, "LG Electronics 16Z90S-GA5VK") && up->codec && !strcmp(up->codec, "10EC:0298") && up->ex[DA_TP] == EX_UNSUP && up->ex[DA_SND] == EX_OK, "user pc %d '%s' '%s'", up ? up->pc : -1, up && up->smbios ? up->smbios : "", up && up->codec ? up->codec : "");
        CHECK(diag_pc_auto(L, n) == ui, "auto user first");
        CHECK(diag_pc_user_add(L, n, u, "5.2.27") == ui && diag_pc_count() == diag_npcs + 1, "same pc replaces");
        int tl = diag_pc_user_save(tb, sizeof tb);
        CHECK(tl > 100 && strstr(tb, "pc=6 name=\"LG Electronics 16Z90S-GA5VK\"") && strstr(tb, " ex=OOOOOXUOXO") , "save:\n%s", tb);
        strcat(tb, "pc=9 name=\"Test box\" smbios=\"ABC-123\" gpu=- codec=- boot=legacy ex=ooxn note=\"hand made\"\r\n# comment\r\n");
        CHECK(diag_pc_user_load(tb, (int)strlen(tb)) == 2, "load 2");
        const DiagPc *t9 = diag_pc(diag_npcs + 1);
        CHECK(t9 && t9->pc == 9 && !t9->gpu && !t9->codec && t9->ex[DA_SND] == EX_UNSUP && t9->ex[DA_KBD] == EX_NONE && t9->ex[DA_PERF] == EX_UNKNOWN && !strcmp(t9->boot, "legacy"), "parsed");
        diag_pc_user_load("", 0); n--;
    }
    printf("diagtest: %d/%d passed\n", checks - fails, checks);
    return fails != 0;
}
