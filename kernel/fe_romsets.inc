/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_romsets.inc - 5.2.171 MIDI 탭: MT-32/CM-32L · SC-55 롬 묶음 찾기.
 *   MidiSound\MT32 의 .ROM 을 내용(SHA-1)으로 알아봐 "MT-32 v1.07" 처럼 이름 붙이고, 같은 내용의 파일은 한 묶음으로 합침.
 *   소리 롬(PCM)은 크기로 짝 (MT-32 = 512 KiB, CM-32L = 1 MiB). 소리 롬이 없으면 회색으로 보이고 못 고름.
 *   MidiSound\SC55 는 mkII / 원판 파일 묶음. 읽고 여는 쪽(mt_load · sc55_load)은 그대로이고 여기서는 "어느 파일" 만 찾음. */
typedef struct {
    char label[40];                     /* "MT-32 v1.07" */
    char ctl[64], pcm[64];              /* 고르면 저장될 파일 이름 (mt_ctl_name / mt_pcm_name) */
    char files[96];                     /* 보여 줄 파일 이름들 */
    char why[64];                       /* 못 고르는 이유 (비면 고를 수 있음) */
    uint8_t model;                      /* MT-32 0, CM-32L 1 */
    uint8_t nfiles;                     /* 같은 내용의 제어 롬 파일 수 */
} RomSet;
#define RS_MAX 12
static RomSet rs_mt[RS_MAX]; static int rs_mt_n;
static RomSet rs_sc[2]; static int rs_sc_n;             /* 0 = mkII, 1 = 원판 */
static uint8_t rs_scanned;
static int sc55_set;                                    /* ini Sc55Set: 0 자동 (mkII 먼저), 1 mkII, 2 원판 */

static const struct { const char *sha, *label; uint8_t model; } rs_ctl_tab[] = {
    { "5a5cb5a77d7d55ee69657c2f870416daed52dea7", "MT-32 v1.04", 0 }, { "e17a3a6d265bf1fa150312061134293d2b58288c", "MT-32 v1.05", 0 },
    { "a553481f4e2794c10cfe597fef154eef0d8257de", "MT-32 v1.06", 0 }, { "b083518fffb7f66b03c23b7eb4f868e62dc5a987", "MT-32 v1.07", 0 },
    { "7b8c2a5ddb42fd0732e2f22b3340dcf5360edf92", "MT-32 BlueRidge", 0 },
    { "5837064c9df4741a55f7c4d8787ac158dff2d3ce", "MT-32 v2.03", 0 }, { "2c16432b6c73dd2a3947cba950a0f4c19d6180eb", "MT-32 v2.04", 0 },
    { "2869cf4c235d671668cfcb62415e2ce8323ad4ed", "MT-32 v2.06", 0 }, { "47b52adefedaec475c925e54340e37673c11707c", "MT-32 v2.07", 0 },
    { "73683d585cd6948cc19547942ca0e14a0319456d", "CM-32L v1.00", 1 }, { "a439fbb390da38cada95a7cbb1d6ca199cd66ef8", "CM-32L v1.02", 1 },
    { "dc1c5b1b90a4646d00f7daf3679733c7badc7077", "CM-32LN v1.00", 1 },
};
static uint8_t rs_usb_bad;                              /* 5.3.53: 1 = USB(0:\\)를 못 읽음 — 롬이 "없다"가 아니라 USB 문제 */
static void rs_usb_probe(void)
{
    FeDirEntry pe[1];
    rs_usb_bad = (uint8_t)(P.list_dir && P.list_dir("0:\\", pe, 1) < 0);
}
static const char *rs_path(int k, const char *nm) { static char b[FE_PATH_MAX]; midi_path(b, sizeof b, k, nm); return b; }

static int rs_is_rom(const char *nm) { int l = (int)strlen(nm); return l > 4 && nm[l - 4] == '.' && has_ci(nm + l - 4, ".rom"); }
static void rs_scan_mt(void)
{
    rs_mt_n = 0;
    if (!P.list_dir || !P.read_file || !P.scratch || P.scratch_size < (10u << 20)) return;
    uint8_t *rs_buf = P.scratch + (9u << 20);           /* 9~10 MiB: 잠깐 쓰는 자리 (카세트 DB 와 같은 곳) */
    static FeDirEntry re[64]; static char sha_of[RS_MAX][41];
    int rn = P.list_dir(midi_dir(MD_MT32), re, 64);
    char pcm[2][64] = { "", "" };                       /* 모델별 첫 소리 롬 */
    for (int i = 0; i < rn; i++) {
        const char *nm = re[i].key[0] ? re[i].key : re[i].name;
        if (re[i].is_dir || !rs_is_rom(nm)) continue;
        if (re[i].size == 524288 && !pcm[0][0]) copy_str(pcm[0], sizeof pcm[0], nm);
        else if (re[i].size == 1048576 && !pcm[1][0]) copy_str(pcm[1], sizeof pcm[1], nm);
    }
    for (int i = 0; i < rn && rs_mt_n < RS_MAX; i++) {
        const char *nm = re[i].key[0] ? re[i].key : re[i].name;
        if (re[i].is_dir || !rs_is_rom(nm) || re[i].size > 131072 || re[i].size < 1024 || (re[i].size != 65536 && re[i].size != 131072 && !has_ci(nm, "control") && !has_ci(nm, "ctrl"))) continue;   /* 제어 롬 후보 (모르는 크기여도 이름에 control 이 있으면 회색으로 보임) */
        uint32_t n = 0; char hex[41];
        if (P.read_file(rs_path(MD_MT32, nm), rs_buf, 131072u, &n) || n != re[i].size) continue;
        sha1_hex(rs_buf, n, hex);
        int k = 0; for (; k < rs_mt_n; k++) if (!strcmp(sha_of[k], hex)) break;
        if (k < rs_mt_n) {                              /* 같은 내용: 한 묶음으로 */
            RomSet *s = &rs_mt[k]; s->nfiles++;
            if (strlen(s->files) + strlen(nm) + 4 < sizeof s->files) { str_append(s->files, sizeof s->files, ", "); str_append(s->files, sizeof s->files, re[i].name[0] ? re[i].name : nm); }
            continue;
        }
        RomSet *s = &rs_mt[rs_mt_n]; memset(s, 0, sizeof *s); copy_str(sha_of[rs_mt_n], sizeof sha_of[0], hex); rs_mt_n++;
        int t = -1; for (unsigned q = 0; q < sizeof rs_ctl_tab / sizeof rs_ctl_tab[0]; q++) if (!strcmp(rs_ctl_tab[q].sha, hex)) t = (int)q;
        copy_str(s->ctl, sizeof s->ctl, nm); s->nfiles = 1; copy_str(s->files, sizeof s->files, re[i].name[0] ? re[i].name : nm);
        if (t < 0) { copy_str(s->label, sizeof s->label, nt("알 수 없는 롬", "Unknown ROM", "不明なROM")); copy_str(s->why, sizeof s->why, nt("munt 가 알아보지 못하는 롬", "not recognized by munt", "muntが認識できないROM")); continue; }
        copy_str(s->label, sizeof s->label, rs_ctl_tab[t].label); s->model = rs_ctl_tab[t].model;
        if (!pcm[s->model][0]) copy_str(s->why, sizeof s->why, nt("소리 롬(PCM) 없음", "PCM ROM missing", "PCM ROMなし"));
        else copy_str(s->pcm, sizeof s->pcm, pcm[s->model]);
    }
}
static void rs_sort_mt(void)                            /* MT-32 먼저(새 판 위로), 그 다음 CM-32L */
{
    for (int i = 1; i < rs_mt_n; i++) {
        RomSet t = rs_mt[i]; int k = i - 1;
        for (; k >= 0; k--) { int a = rs_mt[k].why[0] ? 1 : 0, b = t.why[0] ? 1 : 0; if (a != b ? a > b : rs_mt[k].model != t.model ? rs_mt[k].model > t.model : strcmp(rs_mt[k].label, t.label) < 0) rs_mt[k + 1] = rs_mt[k]; else break; }
        rs_mt[k + 1] = t;
    }
}
static void rs_scan_sc(void)
{
    rs_sc_n = 2;
    for (int m = 0; m < 2; m++) {
        RomSet *s = &rs_sc[m]; memset(s, 0, sizeof *s);
        copy_str(s->label, sizeof s->label, m == 0 ? "SC-55mkII" : nt("SC-55 (원판)", "SC-55 (original)", "SC-55 (初代)")); s->model = (uint8_t)m;
        int have = 0; char miss[64] = "";
        for (int f = 0; f < SC55_NFILE; f++) {
            uint8_t hb[4]; uint32_t fs = 0;
            if (!P.read_file || P.read_file(rs_path(MD_SC55, sc55w_files[m][f]), hb, 1, &fs) || !fs) { if (strlen(miss) + 20 < sizeof miss) { if (miss[0]) str_append(miss, sizeof miss, ", "); str_append(miss, sizeof miss, sc55w_files[m][f]); } }
            else have++;
        }
        s->nfiles = (uint8_t)have;
        copy_str(s->files, sizeof s->files, sc55w_files[m][0]); str_append(s->files, sizeof s->files, " ...");
        if (have < SC55_NFILE) { copy_str(s->why, sizeof s->why, have ? nt("파일 모자람: ", "missing: ", "不足: ") : nt("파일 없음", "not found", "なし")); if (have) str_append(s->why, sizeof s->why, miss); }
    }
}
static void rs_scan(void) { rs_usb_probe(); rs_scan_mt(); rs_sort_mt(); rs_scan_sc(); rs_scanned = 1; }
/* 이 MT-32/CM-32L 기계가 고른 묶음: -1 = 자동, -2 = 목록에 없는 파일 (5.2.173: 롬 이름은 기계마다) */
static int rs_mt_cur(const MidiDev *d)
{
    if (!d->rom_ctl[0] && !d->rom_pcm[0]) return -1;
    for (int i = 0; i < rs_mt_n; i++) if (!strcmp(rs_mt[i].ctl, d->rom_ctl) && !rs_mt[i].why[0] && !strcmp(rs_mt[i].pcm, d->rom_pcm)) return i;
    for (int i = 0; i < rs_mt_n; i++) if (!strcmp(rs_mt[i].ctl, d->rom_ctl)) return i;
    return -2;
}
