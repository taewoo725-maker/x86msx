/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mmvid.c (8.0.75) - X86 Multimedia 영상 쪽. 규격은 mmvid.h. MV2 를 호스트 폴더에서 직접 읽어 SCREEN 2 로 보임. */
#include <string.h>
#include "mmvid.h"
#include "mmbar.h"
#include "mvsnd.h"
#include "mmusb.h"
#include "mmtext.h"
#include "mp4info.h"
#include "mp4play.h"
#include "mp4limits.h"
#include "mmv9.h"


#define MV2_HDR   0x4000u            /* 머리 */
#define MV2_FRAME 0x4000u            /* 프레임 한 장 */
#define MV2_VID   0x3020u            /* 화면 자료 (패턴 6144 + 색 6144 + 팔레트 30 + 예비) */
#define MV2_SND   0x3200u            /* 소리 칸 (길이/32, MP3 조각) */
#define AHEAD     45                 /* 소리를 위치보다 이만큼 앞(프레임)까지 먹여 둠 (3초) */
#define FEED_MAX  8                  /* 한 프레임 끝에 먹이는 최대 프레임 수 (USB 읽기 시간 제한) */
#define START_MIN 10                 /* 이만큼 먹인 뒤 소리를 시작 (끊김 없이) */
#define MAXF      64                 /* 폴더 번호 표 */
#define MAXE      250                /* 한 폴더 항목 */

enum { ST_BUSY = 1, ST_OVLOK = 2, ST_END = 4, ST_ERR = 8, ST_LISTEND = 16, ST_PLAY = 32, ST_PAUSE = 64 };

static const MmHost *host;
static char SEP[2] = "\\";
static const char *sep(void) { SEP[0] = host && host->sep ? host->sep : (char)92; return SEP; }
static VDP *vdp;
static V9990 *v9;                                               /* 8.0.131: Gfx9000 (msx.c 가 프레임마다 알려 줌, 없으면 0) */
static void crumb_nums(const char *tag, const uint8_t *v, int n);
static int v9m, v9_first;                                                 /* 지금 영상을 V9990 으로 내보내는 중이면 화면 번호(13·14) */
static void v9_end(void) { if (v9 && v9m) mmv9_stop(v9); v9m = 0; v9_first = 0; mp4p_raw = 0; }   /* 영상 끝·정지·리셋: V9990 화면을 끄고 VDP 화면으로 */
void mmvid_set_v9990(V9990 *p) { if (p != v9) v9_end(); v9 = p; }

static struct {
    char fpath[MAXF][160]; uint16_t fpar[MAXF]; int nf;       /* 폴더 번호 → 경로, 위 폴더 */
    MmEnt ent[MAXE]; int nent, ent_folder;                    /* 목록 캐시 (정렬해 걸러 둔) */
    uint8_t res[64]; int res_n, res_pos;                      /* 25h 로 읽히는 결과 */
    char dpath[136]; int dpath_ok; uint8_t ddrv;              /* 8.0.109 2Ah SETPATH 로 받은 MSX 디스크 파일 */
    uint8_t arg[208]; int argn; uint8_t cmd;                  /* 8.0.88: 30h TEXT 는 앞머리 5 + 글자 200 바이트 */
    uint8_t status;                                           /* 오류·끝·목록 끝 (나머지는 계산) */
    int txtm; uint8_t tstat; uint8_t txt[MMT_OUTMAX]; int txt_n, txt_pos;   /* 8.0.88 30h TEXT: 마지막 명령이 TEXT 이면 25h·오류 비트는 이쪽 (영상·USB 쪽 상태는 건드리지 않음) */
    int usbm; uint8_t ustat;                                  /* 8.0.80 마지막 명령이 USB 명령(20h~29h)이면 25h·상태는 그쪽 */
    int opened, playing, paused, snd_started;
    char path[1100]; uint32_t fsize, frames;
    int32_t shown; uint32_t feed;                             /* 보이는 프레임(-1 = 아직), 소리를 먹인 다음 프레임 */
    uint8_t ov[4096]; int ov_on, ov_rx;                       /* 글자 줄 자료와 켜짐, 받는 중이면 받은 수 */
    int mp_clk;   /* 8.0.115 부드러운 시계·진단 */
    int mp_au, mp_aon, mp_afin, mp_aoff; uint32_t mp_base, mp_el_last, mp_stall;   /* 8.0.93 MP4 소리: 소리 있음 / 소리 내는 중 / 다 넣음 / 소리 시계를 버리고 프레임 시계로 */
    int mp4; uint32_t mp_us; uint32_t mp_len_ms; uint8_t mp_img[54272 + 16];   /* 8.0.91 MP4 재생 중: 시계(µs), 길이, 글자 줄을 얹은 그림 */
    Mp4Info mi; int mi_ok; uint8_t oerr;                       /* 8.0.90 마지막 OPEN 의 MP4 정보·오류 코드 */
    uint8_t buf[MV2_VID];                                     /* 프레임 읽기 */
    uint8_t sbuf[4608];                                       /* 소리 칸 읽기 (MV2 3584, MV5~MVC 4608) */
    int mv; uint32_t mv_fsz, mv_pic, mv_pal, mv_snd; int mv_k, mv_lines, mv_w;   /* 8.0.101 MV5~MVC: 화면 번호(0 = MV2), 프레임 크기, 그림 크기, 팔레트·소리 위치, k, 줄 수, 가로 */
    uint8_t mvf[0xC200 + 16];                                 /* 8.0.101 MV5~MVC 프레임 (그림 + 팔레트) */
    int bar, bar_level; uint32_t bar_key;                     /* 8.0.149 MP4 계열 파란 표시줄: 켜짐, 볼륨 단계, 마지막으로 그린 상태 */
    uint16_t pos_latch;
    uint32_t last_pos; int stall;                              /* 위치가 멈춘 프레임 수 */
} S;

void mmvid_set_host(const MmHost *h) { host = h; }
void mmvid_attach(VDP *v) { vdp = v; }

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scopy(char *d, int max, const char *s) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void sapp(char *d, int max, const char *s) { int n = slen(d); scopy(d + n, max - n, s); }
static int ucase(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static int ends_mv2(const char *n) { int l = slen(n); return l > 4 && n[l - 4] == '.' && ucase(n[l - 3]) == 'M' && ucase(n[l - 2]) == 'V' && n[l - 1] == '2'; }
static int ncmp(const char *a, const char *b) { while (*a && ucase((uint8_t)*a) == ucase((uint8_t)*b)) { a++; b++; } return ucase((uint8_t)*a) - ucase((uint8_t)*b); }

void mmvid_reset(void)
{
    v9_end();
    mp4p_close();
    memset(&S, 0, sizeof S);
    S.shown = -1; S.ent_folder = -1;
    mmusb_reset();
    if (host && host->root) { scopy(S.fpath[0], sizeof S.fpath[0], host->root); S.fpar[0] = 0xFFFF; S.nf = 1; }
    S.status = 0;
}

/* ---- 목록 ---- */
static int folder_of(const char *path, uint16_t parent)
{
    for (int i = 0; i < S.nf; i++) if (!ncmp(S.fpath[i], path)) return i;
    if (S.nf >= MAXF) return -1;
    scopy(S.fpath[S.nf], sizeof S.fpath[0], path); S.fpar[S.nf] = parent;
    return S.nf++;
}
static int load_folder(int f)                                  /* 그 폴더를 읽어 걸러 정렬 (폴더 먼저) */
{
    if (!host || !host->list || f < 0 || f >= S.nf) return -1;
    static MmEnt tmp[MAXE + 32];
    int n = host->list(S.fpath[f], tmp, MAXE + 32);
    if (n < 0) return -1;
    int k = 0;
    for (int i = 0; i < n && k < MAXE; i++) {
        const char *nm = tmp[i].name;
        if (!nm[0] || (nm[0] == '.' && (!nm[1] || (nm[1] == '.' && !nm[2])))) continue;
        if (!tmp[i].is_dir && !ends_mv2(nm) && !mp4_name_is(nm)) continue;
        S.ent[k++] = tmp[i];
    }
    for (int i = 1; i < k; i++) {                              /* 삽입 정렬: 폴더 먼저, 이름순 */
        MmEnt t = S.ent[i]; int j = i - 1;
        while (j >= 0 && ((S.ent[j].is_dir < t.is_dir) || (S.ent[j].is_dir == t.is_dir && ncmp(S.ent[j].name, t.name) > 0))) { S.ent[j + 1] = S.ent[j]; j--; }
        S.ent[j + 1] = t;
    }
    S.nent = k; S.ent_folder = f;
    return k;
}
static void res_clear(void) { S.res_n = S.res_pos = 0; }
static void res_put(uint8_t b) { if (S.res_n < (int)sizeof S.res) S.res[S.res_n++] = b; }
static void cmd_list(void)
{
    int f = S.arg[0] | (S.arg[1] << 8), it = S.arg[2] | (S.arg[3] << 8);
    res_clear(); S.status &= (uint8_t)~ST_LISTEND;
    uint8_t kind = 0xFF; const char *nm = ""; uint16_t num = 0;
    if (f >= S.nf || ((S.ent_folder != f || it == 0) && load_folder(f) < 0)) { S.status |= ST_LISTEND; kind = 0xFF; }
    else if (it == 0xFFFF) { kind = 2; nm = ".."; num = S.fpar[f]; }
    else if (it >= S.nent) { S.status |= ST_LISTEND; kind = 0xFF; }
    else {
        const MmEnt *e = &S.ent[it]; nm = e->name;
        if (e->is_dir) { char p[160]; scopy(p, sizeof p, S.fpath[f]); sapp(p, sizeof p, sep()); sapp(p, sizeof p, e->name); int c = folder_of(p, (uint16_t)f); kind = c < 0 ? 0xFF : 1; num = (uint16_t)(c < 0 ? 0 : c); }
        else kind = 0;
    }
    res_put(kind);
    for (int i = 0; i < 12; i++) res_put(kind != 0xFF && nm[i] && i < (int)slen(nm) ? (uint8_t)nm[i] : 0);
    res_put((uint8_t)num); res_put((uint8_t)(num >> 8));
}

/* ---- 화면 ---- */
static void vram_w(uint32_t a, const uint8_t *p, uint32_t n) { if (vdp && a + n <= VDP_VRAM) memcpy(vdp->vram + a, p, n); }
static void screen2(void)                                      /* VDP 를 SCREEN 2 로 */
{
    if (!vdp) return;
    static const uint8_t rv[][2] = { {0, 0x02}, {2, 0x06}, {3, 0xFF}, {4, 0x03}, {5, 0x36}, {6, 0x07}, {7, 0x00}, {10, 0x00}, {14, 0x00}, {23, 0x00}, {25, 0x00}, {26, 0x00}, {27, 0x00} };
    for (unsigned i = 0; i < sizeof rv / sizeof rv[0]; i++) vdp_set_reg(vdp, rv[i][0], rv[i][1]);
    vdp_set_reg(vdp, 1, (uint8_t)(0x40 | (vdp->reg[1] & 0x23)));     /* 표시 켬, M1·M2 끔, 인터럽트·스프라이트 크기 유지 */
    vdp_set_reg(vdp, 8, (uint8_t)(vdp->reg[8] | 0x02));            /* 스프라이트 끔 */
    vdp_set_reg(vdp, 9, (uint8_t)(vdp->reg[9] & ~0x80));           /* 192줄 */
    static uint8_t nt[768]; for (int i = 0; i < 768; i++) nt[i] = (uint8_t)i;
    vram_w(0x1800, nt, 768);
    static const uint8_t hide = 0xD0; vram_w(0x1B00, &hide, 1);
    vdp_set_pal3(vdp, 0, 0, 0, 0);
}
static int mv_draw(uint32_t f);
static const MmDisk *disk;                                      /* 8.0.109 MSX 디스크 파일 */
static int vrd(const char *p, uint32_t off, uint8_t *b, uint32_t n)   /* 8.0.110 첫 글자 01h = 하드 이미지 안 파일(표식) */
{
    if (p[0] == 1) return disk && disk->read_at ? disk->read_at(p, off, b, n) : -1;
    return host->read_at(p, off, b, n);
}

static int draw(uint32_t f)
{
    if (!host || !host->read_at) return -1;
    if (S.mv) return mv_draw(f);
    if (vrd(S.path, MV2_HDR + f * MV2_FRAME, S.buf, MV2_VID)) return -1;
    vram_w(0x0000, S.buf, 6144);
    vram_w(0x2000, S.buf + 6144, 6144);
    for (int i = 1; i <= 15; i++) { const uint8_t *p = S.buf + 0x3000 + (i - 1) * 2; if (vdp) vdp_set_pal3(vdp, i, (p[0] >> 4) & 7, p[1] & 7, p[0] & 7); }
    if (S.ov_on) {                                              /* 글자 줄: 위 3줄 = 칸 줄 0~2, 아래 5줄 = 칸 줄 19~23 (패턴 줄 256 B) */
        vram_w(0x0000, S.ov, 768);               vram_w(0x2000, S.ov + 2048, 768);
        vram_w(19 * 256, S.ov + 768, 1280);      vram_w(0x2000 + 19 * 256, S.ov + 2048 + 768, 1280);
    }
    S.shown = (int32_t)f;
    return 0;
}

/* ---- 소리 ---- */
static void snd_cmd(uint8_t c, uint16_t param) { mvsnd_write(2, (uint8_t)param); mvsnd_write(3, (uint8_t)(param >> 8)); mvsnd_write(0, c); }
static int feed_one(void)                                      /* 다음 프레임의 MP3 조각을 소리 FIFO 로 */
{
    if (!host || !host->read_at || S.feed >= S.frames) return 0;
    uint32_t slen_ = S.mv_fsz - S.mv_snd;                       /* 8.0.101: 소리 칸 길이 (MV2 3584, MV5~MVC 4608) */
    if (vrd(S.path, MV2_HDR + S.feed * S.mv_fsz + S.mv_snd, S.sbuf, slen_)) { S.status |= ST_ERR; return 0; }
    uint32_t n = (uint32_t)S.sbuf[0] * 32u; if (n > slen_ - 1) n = slen_ - 1;
    for (uint32_t i = 0; i < n; i++) mvsnd_write(1, S.sbuf[1 + i]);
    S.feed++;
    return 1;
}
static void sound_open(uint32_t at)                            /* 소리 시계를 그 프레임에서 시작 (멈춘 채) */
{
    snd_cmd(0x05, (uint16_t)(S.mv ? 150 / S.mv_k : 150)); snd_cmd(0x04, 0); snd_cmd(0x01, (uint16_t)at);   /* 8.0.101: 프레임 속도 (MV5~MVC 는 k/15 초) */
    S.feed = at; S.snd_started = 0;
}

/* ---- 명령 ---- */
/* ---- 8.0.91 MP4 화면 ---- */
static void vram_put(uint32_t a, const uint8_t *p, uint32_t n)          /* 포트처럼 써서 G6·G7 끼워 넣기도 맞음 */
{
    if (!vdp) return;
    vdp_set_reg(vdp, 14, (uint8_t)(a >> 14));
    vdp_write_ctrl(vdp, (uint8_t)a); vdp_write_ctrl(vdp, (uint8_t)(0x40 | ((a >> 8) & 0x3F)));
    for (uint32_t i = 0; i < n; i++) vdp_write_data(vdp, p[i]);
}
static void screen_bitmap(int scr, int lines)
{
    if (!vdp) return;
    static const uint8_t r0[13] = { 0, 0, 0, 0, 0, 0x06, 0x08, 0x0A, 0x0E, 0, 0, 0, 0x0E };   /* SCREEN 12 = G7 + R#25 YJK */
    vdp_set_reg(vdp, 0, r0[scr]);
    vdp_set_reg(vdp, 1, (uint8_t)(0x40 | (vdp->reg[1] & 0x23)));
    vdp_set_reg(vdp, 2, 0x1F);
    vdp_set_reg(vdp, 7, 0x00);
    vdp_set_reg(vdp, 8, (uint8_t)(vdp->reg[8] | 0x02));                     /* 스프라이트 끔 */
    vdp_set_reg(vdp, 9, (uint8_t)((vdp->reg[9] & ~0x8C) | (lines == 212 ? 0x80 : 0)));
    vdp_set_reg(vdp, 23, 0); vdp_set_reg(vdp, 25, scr == 12 ? 0x08 : 0); vdp_set_reg(vdp, 26, 0); vdp_set_reg(vdp, 27, 0);
}
static void px_put(int scr, uint8_t *row, int x, int code)
{
    if (scr == 8 || scr >= 10) { row[x] = (uint8_t)code; return; }
    if (scr == 6) { int sh = 6 - 2 * (x & 3); row[x >> 2] = (uint8_t)((row[x >> 2] & ~(3 << sh)) | (code << sh)); return; }
    if (x & 1) row[x >> 1] = (uint8_t)((row[x >> 1] & 0xF0) | code); else row[x >> 1] = (uint8_t)((row[x >> 1] & 0x0F) | (code << 4));
}
/* 8.0.143 글자 줄 색: SCREEN 2 의 X86MV 화면처럼 초록. 팔레트 화면은 그 영상 팔레트에서 가장 초록에 가까운 칸 (초록다운 칸이 없으면 -1 → 기존 밝은색) */
#define OV_GREEN_R 0
#define OV_GREEN_G 6
#define OV_GREEN_B 2                                                /* 3비트 RGB 초록 */
/* 8.0.143: 초록다운 칸이 없으면(8.0.143 첫판은 흰색으로 돌아갔음) 초록에 가장 가까운 칸 하나를 진짜 초록으로 바꿈 → *forced = 1. dark 칸은 건드리지 않음 */
static int ov_green_entry(int n, int dark, int *forced, int (*rgb_of)(int k, int ch, const void *src), const void *src)
{
    int best = -1, best_score = 2, near = -1, near_d = 1 << 30; *forced = 0;
    for (int k = 0; k < n; k++) {
        int r = rgb_of(k, 0, src), g = rgb_of(k, 1, src), b = rgb_of(k, 2, src), score = 2 * g - r - b;
        if (g >= 4 && score > best_score) { best_score = score; best = k; }
        int d = (r - OV_GREEN_R) * (r - OV_GREEN_R) + (g - OV_GREEN_G) * (g - OV_GREEN_G) + (b - OV_GREEN_B) * (b - OV_GREEN_B);
        if (k != dark && d < near_d) { near_d = d; near = k; }
    }
    if (best < 0 && near >= 0) { best = near; *forced = 1; }
    return best;
}
static int ov_rgb_pic(int k, int ch, const void *src) { const Mp4Pic *pc = (const Mp4Pic *)src; return pc->pal[k][ch]; }
static int ov_rgb_mv(int k, int ch, const void *src)  { const uint8_t *p = (const uint8_t *)src + k * 2; return ch == 0 ? (p[0] >> 4) & 7 : ch == 1 ? p[1] & 7 : p[0] & 7; }
#define OV_YJK_LIT_Y 0x40                                          /* Y = 8 (위 5비트), 아래 3비트는 J·K 조각이 들어갈 자리 */
/* 8.0.143 YJK: 4점 묶음이 J·K 를 같이 쓴다. 켜진 점이 하나라도 있으면 묶음 전체에 초록(J = -8, K = 12 → R 0, G 약 20, B 약 10)을 입히고, 없으면 J = K = 0 (검정) */
static void ov_yjk_green_row(uint8_t *row, int width_px)
{
    for (int g = 0; g + 4 <= width_px; g += 4) {
        int lit = 0; for (int i = 0; i < 4; i++) lit |= (row[g + i] >> 3) != 0;
        static const uint8_t jk_green[4] = { 4, 1, 0, 7 };         /* K 아래 3비트, K 위 3비트, J 아래 3비트, J 위 3비트 */
        for (int i = 0; i < 4; i++) row[g + i] = (uint8_t)((row[g + i] & 0xF8) | (lit ? jk_green[i] : 0));
    }
}
#define OV_GREEN_GRB8 0x1C                                         /* SCREEN 8 (GRB 3-3-2): G = 7, R = B = 0 */
static void ov_apply(uint8_t *img, int scr, int H, int stride, int w, int dark, int light)   /* 글자 줄을 그림에 얹음 (8.0.101: MP4·MV5~MVC 공용) */
{
    if (!S.ov_on) return;
    for (int y = 0; y < H; y++) {
        int blk = y < 24 ? y >> 3 : (y >= H - 40 ? 3 + ((y - (H - 40)) >> 3) : -1);
        if (blk < 0) continue;
        uint8_t *row = img + (size_t)y * (size_t)stride;
        for (int c = 0; c < 32; c++) {
            int ix = blk * 256 + c * 8 + (y & 7);
            uint8_t pb = S.ov[ix], cb = S.ov[2048 + ix];
            int fg = (cb >> 4) ? light : dark, bg = (cb & 15) ? light : dark;
            for (int k = 0; k < 8; k++) {
                int col = (pb >> (7 - k)) & 1 ? fg : bg, x = c * 8 + k;
                if (w > 256) { px_put(scr, row, 2 * x, col); px_put(scr, row, 2 * x + 1, col); } else px_put(scr, row, x, col);
            }
        }
        if (scr >= 10) ov_yjk_green_row(row, w);
    }
}
/* 8.0.142 진단: 글자 줄(OVL)을 받았는지·켜졌는지 (재생 중에는 명령 기록이 조용해져서 따로 남김). 받은 기록은 처음 24번까지 */
static void ov_crumb_received(void)
{
    static int n; if (n++ >= 24) return;
    int nonzero = 0; for (int i = 0; i < 4096; i++) nonzero += S.ov[i] != 0;
    uint8_t d[6] = { (uint8_t)nonzero, (uint8_t)(nonzero >> 8), (uint8_t)S.mp4, (uint8_t)S.mv, (uint8_t)v9m, (uint8_t)S.ov_on };
    crumb_nums("OVL got nz,mp4,mv,v9,on", d, 6);
}
static void ov_crumb_switch(int on)
{
    static int n; if (n++ >= 48) return;
    uint8_t d[4] = { (uint8_t)on, (uint8_t)S.mp4, (uint8_t)S.mv, (uint8_t)v9m };
    crumb_nums("OVLON v,mp4,mv,v9", d, 4);
}
MmVidTimes mmvid_t_dec, mmvid_t_show;                           /* 8.0.155 */
static void tstat_add(MmVidTimes *t, uint32_t ms) { t->n++; t->sum += ms; if (ms > t->max) t->max = ms; if (ms > 33u) t->slow++; }
/* 8.0.149 표시줄 색: 팔레트 화면은 그 그림의 팔레트에서 어두운 칸·밝은 칸·가장 파란 칸을 고름 (X86MV b6_pick 과 같은 규칙), SCREEN 8·12 는 고정 값 */
static void bar_pick(int scr, const uint8_t (*pal)[3], int npal, int code[4])
{
    if (scr == 8)  { code[0] = 0x00; code[1] = 0xFF; code[2] = 0x27; code[3] = 0xFF; return; }
    if (scr >= 10) { code[0] = 0x00; code[1] = 0xF8; code[2] = 0x40; code[3] = 0xF8; return; }
    int br[16], dk = 0, lt = 0, bar = 0, best = 0;
    for (int k = 0; k < npal; k++) { br[k] = pal[k][0] + pal[k][1] + pal[k][2]; if (br[k] < br[dk]) dk = k; if (br[k] > br[lt]) lt = k; }
    for (int k = 0; k < npal; k++) { if (k == dk || k == lt) continue; int sc = 2 * pal[k][2] - pal[k][0] - pal[k][1] + 16; if (sc > best) { best = sc; bar = k; } }
    code[0] = dk; code[1] = lt; code[2] = bar; code[3] = br[bar] >= 11 ? dk : lt;
}
static void bar_overlay_picture(uint8_t *img, int scr, int stride, int w, const int code[4])   /* 8.0.152 영상 위 이름표(바와 함께 보임)와 일시정지 큰 표시(일시정지 중 계속) */
{
    uint8_t badge[MMBAR_BADGE_H][MMBAR_BADGE_W], sign[MMBAR_SIGN_H][MMBAR_SIGN_W];
    int xs = w > 256 ? 2 : 1;
    if (S.ov_on) {
        mmbar_badge(scr, badge);
        for (int j = 0; j < MMBAR_BADGE_H; j++) for (int i = 0; i < MMBAR_BADGE_W; i++) if (badge[j][i] != MMBAR_NONE)
            for (int k = 0; k < xs; k++) px_put(scr, img + (size_t)(MMBAR_BADGE_Y + j) * (size_t)stride, (MMBAR_BADGE_X + i) * xs + k, code[badge[j][i]]);
    }
    if (S.paused) {
        mmbar_sign(sign);
        for (int j = 0; j < MMBAR_SIGN_H; j++) for (int i = 0; i < MMBAR_SIGN_W; i++) if (sign[j][i] != MMBAR_NONE)
            for (int k = 0; k < xs; k++) px_put(scr, img + (size_t)(MMBAR_SIGN_Y + j) * (size_t)stride, (MMBAR_SIGN_X + i) * xs + k, code[sign[j][i]]);
    }
}
static uint32_t mv_bar_frame;                                   /* 8.0.156 MV5~MVC: 표시줄에 쓰려고 마지막으로 그린 프레임 번호 */
static uint32_t bar_cur15(void) { return S.mv ? mv_bar_frame * (uint32_t)S.mv_k : (uint32_t)((uint64_t)S.mp_us * 15 / 1000000u); }   /* 지금 시각 (1/15초 단위) */
static uint32_t bar_total15(void) { return S.mv ? S.frames * (uint32_t)S.mv_k : S.frames; }                                          /* 전체 길이 (1/15초 단위) */
static void bar_paint(uint8_t *img, int scr, int stride, int w, const uint8_t (*pal)[3], int npal)   /* img = 그림 맨 위. 192..211 줄에 표시줄을 씀 */
{
    MmBarState st; uint8_t roles[MMBAR_H][MMBAR_W]; int code[4];
    st.hidden = !S.ov_on; st.paused = S.paused; st.level = S.bar_level;
    st.cur = bar_cur15(); st.total = bar_total15();
    mmbar_render(&st, roles); bar_pick(scr, pal, npal, code);
    for (int j = 0; j < MMBAR_H; j++) {
        uint8_t *row = img + (size_t)(192 + j) * (size_t)stride;
        for (int x = 0; x < MMBAR_W; x++) {
            int c = code[roles[j][x]];
            if (w > 256) { px_put(scr, row, 2 * x, c); px_put(scr, row, 2 * x + 1, c); } else px_put(scr, row, x, c);
        }
    }
    bar_overlay_picture(img, scr, stride, w, code);
}
static uint32_t bar_key_now(void)                               /* 표시줄에 보이는 것이 바뀌었는지 보는 값 */
{
    uint32_t cur = bar_cur15();
    return (cur / 15) | (uint32_t)(S.paused ? 1u << 20 : 0) | (uint32_t)S.bar_level << 21 | (uint32_t)(S.ov_on ? 1u << 24 : 0) | (uint32_t)mmbar_progress(cur, bar_total15()) << 25;
}
static void mp4_present(void)                                   /* 마지막 그림 + 글자 줄 → VRAM·팔레트 */
{
    if (v9m) { if (vdp) { uint8_t bi = vdp_backdrop(vdp) & 15; mmv9_bg = ((uint32_t)vdp->palette[bi][0] << 16) | ((uint32_t)vdp->palette[bi][1] << 8) | vdp->palette[bi][2]; }   /* 8.0.148: 위아래 띠 = 왼쪽·오른쪽 여백과 같은 MSX 테두리 색 */
      if (v9) { MmBarState bs; bs.hidden = !S.ov_on; bs.paused = S.paused; bs.level = S.bar_level; bs.cur = (uint32_t)((uint64_t)S.mp_us * 15 / 1000000u); bs.total = S.frames; mmv9_frame_bar(v9, v9m, S.ov_on ? S.ov : 0, S.bar ? &bs : 0); S.bar_key = bar_key_now(); } if (v9 && !v9_first) { v9_first = 1; uint8_t d[6] = { (uint8_t)v9m, v9->regs[6], v9->regs[8], v9->regs[13], (uint8_t)v9990_visible(v9), (uint8_t)(v9->vram[0x1000] | v9->vram[0x2000]) }; crumb_nums("V9 first frame scr,R6,R8,R13,vis,vram", d, 6); } S.shown = 0; return; }   /* 8.0.131: V9990 으로 (글자 줄은 안 얹음) */
    const Mp4Pic *pc = mp4p_pic(); if (!pc || !pc->bytes || pc->bytes > sizeof S.mp_img) return;
    memcpy(S.mp_img, mp4p_image(), pc->bytes);
    int H = pc->h, scr = pc->scr, dark = 0, light = 0, forced_green = 0;
    int bar = S.bar && H == 192 && (size_t)pc->stride * 212 <= sizeof S.mp_img;                           /* 8.0.149 */
    if (scr == 8) { dark = 0x00; light = OV_GREEN_GRB8; }
    else if (scr >= 10) { dark = 0x00; light = OV_YJK_LIT_Y; }   /* 8.0.143 YJK(10~12): 초록. 켜진 점 Y 8, 4점 묶음에 J·K 를 입힘 (ov_apply) */
    else {
        int lo = 1 << 30, hi = -1;
        for (int k = 0; k < pc->npal; k++) { int l = 2 * pc->pal[k][0] + 4 * pc->pal[k][1] + 3 * pc->pal[k][2]; if (l < lo) { lo = l; dark = k; } if (l > hi) { hi = l; light = k; } }
        int g = ov_green_entry(pc->npal, dark, &forced_green, ov_rgb_pic, pc); if (g >= 0) light = g;
    }
    if (!bar) ov_apply(S.mp_img, scr, H, pc->stride, pc->w, dark, light);                           /* 8.0.161: 파란 표시줄이 있으면 옛 초록 글자 줄은 안 얹음 */
    for (int i = 0; i < pc->npal; i++) vdp_set_pal3(vdp, i, pc->pal[i][0], pc->pal[i][1], pc->pal[i][2]);
    if (forced_green && S.ov_on && !bar) vdp_set_pal3(vdp, light, OV_GREEN_R, OV_GREEN_G, OV_GREEN_B);   /* 글자 줄이 켜졌을 때만 */
    if (bar) {                                                                                       /* 8.0.149 파란 표시줄 (글자 줄이 켜진 팔레트 그대로 보고 색을 고름) */
        uint8_t lp[16][3]; memcpy(lp, pc->pal, sizeof lp);
        bar_paint(S.mp_img, scr, pc->stride, pc->w, (const uint8_t (*)[3])lp, pc->npal);
        S.bar_key = bar_key_now();
        vram_put(0, S.mp_img, (uint32_t)pc->stride * 212);
    } else
    vram_put(0, S.mp_img, pc->bytes);
    S.shown = 0;
}
/* 8.0.174: MV 머리의 형식 글자. 보통 0x11 ("MMCSD_MV" 뒤 공백 8칸 + "v2.00"). 공백 8칸이 없는 파일(ONE_SPRING)은 "v2.00"이 0x08부터라 글자가 0x09 */
static uint8_t mv_header_letter(const uint8_t *h) { return h[0x11] ? h[0x11] : h[0x09]; }
/* ---- 8.0.101 MV5·MV8·MVC: USB 의 MV 파일을 직접 (SCREEN 5·8·12). 머리 16384 + 프레임(그림, 팔레트, 소리 칸) ---- */
static const struct { char c; int scr; uint32_t fsz, pic, pal, snd; uint8_t r0, r25; } MVF[] = {
    { '5', 5, 29696, 24576, 0x6000, 0x6200, 0x06, 0x00 }, { '8', 8, 54272, 49152, 0, 0xC200, 0x0E, 0x00 }, { 'C', 12, 54272, 49152, 0, 0xC200, 0x0E, 0x08 },   /* 8.0.149: MV6·MV7·MVA·MVB 는 없앰, 8.0.150: MV8 은 되살림 (X86MV V1.11.31) */
};
void mmvid_set_disk(const MmDisk *d) { disk = d; }
/* 8.0.145: MV5~MVC 파일은 설정(P4)과 상관없이 자기 머리의 화면으로 상영한다. 머리가 MV5~MVC 형식이면 그 SCREEN 번호, 아니면 0 */
static int mv_screen_of_header(const uint8_t *h)
{
    if (memcmp(h, "MMCSD_MV", 8)) return 0;
    for (unsigned k = 0; k < sizeof MVF / sizeof MVF[0]; k++) if (ucase(mv_header_letter(h)) == MVF[k].c) return MVF[k].scr;
    return 0;
}
#define MMV_DISKNOTVHD 9                                       /* 8.0.109 가상 하드가 아닌 드라이브 (플로피·하드 이미지): 가상 하드에서만 동작 */
#define MMV_AVI 11                                             /* 8.0.117 오류 11 (8.0.120 부터 MJPEG+PCM 은 재생, 안 씀) */
#define MMV_DISKERR 8                                          /* 8.0.109 MSX 디스크 파일을 못 읽음 */
static void crumb_nums(const char *tag, const uint8_t *v, int n);
static void open_fail(uint8_t code);
static int mv_scr_of(int scr) { return scr == 6 || scr == 7 ? 512 : 256; }
static void mv_regs(int i)
{
    if (!vdp) return;
    vdp_set_reg(vdp, 0, MVF[i].r0);
    vdp_set_reg(vdp, 1, (uint8_t)(0x40 | (vdp->reg[1] & 0x23)));
    vdp_set_reg(vdp, 2, 0x1F); vdp_set_reg(vdp, 7, 0x00);
    vdp_set_reg(vdp, 8, (uint8_t)(vdp->reg[8] | 0x02));                       /* 스프라이트 끔 */
    vdp_set_reg(vdp, 9, (uint8_t)((vdp->reg[9] & ~0x8C) | (S.mv_lines == 212 || S.bar ? 0x80 : 0)));   /* 8.0.156: 표시줄이 있으면 212줄 */
    vdp_set_reg(vdp, 23, 0); vdp_set_reg(vdp, 25, MVF[i].r25); vdp_set_reg(vdp, 26, 0); vdp_set_reg(vdp, 27, 0);
}
/* 8.0.146 화면 번호표: MV5~MVC 를 열면 처음 몇 초 동안 위쪽(글자 줄 아래)에 "SCREEN n" 을 얹어, 지금 어느 SCREEN 에서 도는지 눈으로 알게 함. MP4 계열·MV2 는 안 건드림 */
#define BADGE_SECONDS 4                                            /* 번호표를 보이는 시간 (초) */
#define BADGE_SCALE 2                                              /* 글자 한 점의 크기 (화면 점 단위) */
#define BADGE_Y 26                                                 /* 글자 줄 위 3줄(24점) 바로 아래 */
static const char *badge_glyph(char c)                             /* 5x7 점 글자: 7줄 x 5칸, 켜진 점 = '#' */
{
    switch (c) {
    case 'S': return ".####" "#...." "#...." ".###." "....#" "....#" "####.";
    case 'C': return ".####" "#...." "#...." "#...." "#...." "#...." ".####";
    case 'R': return "####." "#...#" "#...#" "####." "#.#.." "#..#." "#...#";
    case 'E': return "#####" "#...." "#...." "####." "#...." "#...." "#####";
    case 'N': return "#...#" "##..#" "#.#.#" "#..##" "#...#" "#...#" "#...#";
    case '0': return ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###.";
    case '1': return "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###.";
    case '2': return ".###." "#...#" "....#" "...#." "..#.." ".#..." "#####";
    case '5': return "#####" "#...." "####." "....#" "....#" "#...#" ".###.";
    case '6': return ".###." "#...." "#...." "####." "#...#" "#...#" ".###.";
    case '7': return "#####" "....#" "...#." "..#.." ".#..." ".#..." ".#...";
    case '8': return ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###.";
    default:  return ".....""....." "....." "....." "....." "....." ".....";
    }
}
static void badge_apply(uint8_t *img, int scr, int stride, int w, int dark, int light)
{
    char text[12]; int n = 0; const char *t = "SCREEN ";
    while (*t) text[n++] = *t++;
    if (scr >= 10) text[n++] = (char)('0' + scr / 10);
    text[n++] = (char)('0' + scr % 10); text[n] = 0;
    int cell_w = 6, x0 = (w > 256 ? 256 : w) - n * cell_w * BADGE_SCALE - 4;   /* 오른쪽 위 모서리 (논리 점 단위) */
    x0 &= ~3;                                                       /* YJK 4점 묶음에 맞춤 */
    for (int row = 0; row < 7 * BADGE_SCALE; row++) {
        uint8_t *line = img + (size_t)(BADGE_Y + row) * (size_t)stride;
        for (int ci = 0; ci < n; ci++) {
            const char *g = badge_glyph(text[ci]);
            for (int col = 0; col < cell_w * BADGE_SCALE; col++) {
                int gc = col / BADGE_SCALE, gr = row / BADGE_SCALE;
                int on = gc < 5 && g[gr * 5 + gc] == '#', x = x0 + ci * cell_w * BADGE_SCALE + col, color = on ? light : dark;
                if (w > 256) { px_put(scr, line, 2 * x, color); px_put(scr, line, 2 * x + 1, color); } else px_put(scr, line, x, color);
            }
        }
        if (scr >= 10) {                                            /* YJK: 번호표 범위의 4점 묶음만 초록 입힘 (그림은 건드리지 않음) */
            int xe = x0 + n * cell_w * BADGE_SCALE;
            for (int g4 = x0; g4 + 4 <= xe; g4 += 4) {
                int lit = 0; for (int i = 0; i < 4; i++) lit |= (line[g4 + i] >> 3) != 0;
                static const uint8_t jk_green[4] = { 4, 1, 0, 7 };
                for (int i = 0; i < 4; i++) line[g4 + i] = (uint8_t)((line[g4 + i] & 0xF8) | (lit ? jk_green[i] : 0));
            }
        }
    }
}
static int badge_visible(uint32_t frame)                           /* 처음 BADGE_SECONDS 초 (프레임 길이 = k/15 초) */
{
    uint32_t until = (uint32_t)(BADGE_SECONDS * 15) / (uint32_t)(S.mv_k ? S.mv_k : 3);
    return frame < (until ? until : 1);
}
static int mv_draw(uint32_t f)
{
    int scr = S.mv, H = S.mv_lines;
    if (vrd(S.path, MV2_HDR + f * S.mv_fsz, S.mvf, S.mv_pal ? S.mv_pal + 32 : S.mv_pic)) return -1;
    memcpy(S.mp_img, S.mvf, S.mv_pic);
    int dark = 0, light = 0xFF, forced_green = 0;
    if (scr == 8) { dark = 0x00; light = OV_GREEN_GRB8; }
    else if (scr >= 10) { dark = 0x00; light = OV_YJK_LIT_Y; }                  /* 8.0.143 YJK: 초록 */
    else {
        int lo = 1 << 30, hi = -1;
        for (int k = 0; k < 16; k++) { const uint8_t *p = S.mvf + S.mv_pal + k * 2; int r = (p[0] >> 4) & 7, g = p[1] & 7, b = p[0] & 7, l = 2 * r + 4 * g + 3 * b; if (l < lo) { lo = l; dark = k; } if (l > hi) { hi = l; light = k; } }
        int g = ov_green_entry(16, dark, &forced_green, ov_rgb_mv, S.mvf + S.mv_pal); if (g >= 0) light = g;
    }
    uint32_t stride = S.mv_pic / (uint32_t)192;
    int bar = S.bar && S.mv_lines == 192;
    if (!bar) ov_apply(S.mp_img, scr, H, stride, S.mv_w, dark, light);                  /* 8.0.161: 파란 표시줄이 있으면 옛 초록 글자 줄은 안 얹음 */
    /* 8.0.156 MV5~MVC 도 MP4 계열과 같은 파란 표시줄 (번호표는 표시줄 안의 이름표가 대신함) */
    int badge = !bar && badge_visible(f);
    if (badge) badge_apply(S.mp_img, scr, stride, S.mv_w, dark, light);
    if (S.mv_pal && vdp) for (int i = 0; i < 16; i++) { const uint8_t *p = S.mvf + S.mv_pal + i * 2; vdp_set_pal3(vdp, i, (p[0] >> 4) & 7, p[1] & 7, p[0] & 7); }
    if (forced_green && ((S.ov_on && !bar) || badge) && vdp) vdp_set_pal3(vdp, light, OV_GREEN_R, OV_GREEN_G, OV_GREEN_B);
    if (bar) {
        uint8_t lp[16][3];
        for (int i = 0; i < 16; i++) { const uint8_t *p = S.mvf + S.mv_pal + i * 2; if (S.mv_pal) { lp[i][0] = (p[0] >> 4) & 7; lp[i][1] = p[1] & 7; lp[i][2] = p[0] & 7; } else lp[i][0] = lp[i][1] = lp[i][2] = 0; }
        mv_bar_frame = f;
        bar_paint(S.mp_img, scr, (int)stride, S.mv_w, (const uint8_t (*)[3])lp, 16);
        S.bar_key = bar_key_now();
        vram_put(0, S.mp_img, stride * 212u);
    } else
    vram_put(0, S.mp_img, S.mv_pic);
    S.shown = (int32_t)f;
    return 0;
}
/* 머리 h 가 MMCSD_MV 일 때: 형식 판별·오류 검사·열기. 돌려주는 값 0 = 성공 (결과 코드는 open_fail 로 이미 남김) */
static int mv_open(const uint8_t *h, int scr)
{
    int i = -1;
    for (unsigned k = 0; k < sizeof MVF / sizeof MVF[0]; k++) if (ucase(mv_header_letter(h)) == MVF[k].c) i = (int)k;
    if (i < 0) { open_fail(mv_header_letter(h) == '2' ? MP4E_PROFILE : MP4E_READ); return -1; }
    if (MVF[i].scr != scr) { open_fail(MP4E_PROFILE); return -1; }              /* 머리 글자와 P4 가 다름 → 오류 4 */
    if (!vdp || !vdp->v9938 || (scr >= 10 && !vdp->v9958)) { open_fail(MP4E_SCREEN); return -1; }   /* V9938: SCREEN 10~12 불가 → 오류 6 */
    if (scr != 5 && scr != 6 && vdp->vram_mask < 0x1FFFF) { open_fail(MP4E_SCREEN); return -1; }      /* VRAM 64KB: SCREEN 7·8·10~12 불가 → 오류 6 */
    S.mv = scr; S.mv_fsz = MVF[i].fsz; S.mv_pic = MVF[i].pic; S.mv_pal = MVF[i].pal; S.mv_snd = MVF[i].snd; S.mv_w = mv_scr_of(scr);
    S.mv_k = h[0x20] ? h[0x20] : 3; S.mv_lines = h[0x21] == 212 ? 212 : 192;
    S.bar = S.mv_lines == 192; S.bar_level = 0; S.bar_key = ~0u; mv_bar_frame = 0;   /* 8.0.156 */
    uint32_t n = (uint32_t)h[0x18] | (uint32_t)h[0x19] << 8 | (uint32_t)h[0x1A] << 16 | (uint32_t)h[0x1B] << 24;
    uint32_t fit = S.fsize > MV2_HDR ? (S.fsize - MV2_HDR) / S.mv_fsz : 0;
    if (n == 0 || n > fit) n = fit;
    if (n == 0) { S.mv = 0; open_fail(MP4E_READ); return -1; }
    S.frames = n;
    mv_regs(i); sound_open(0);
    if (draw(0)) { S.mv = 0; open_fail(MP4E_READ); return -1; }
    S.opened = 1; res_clear();
    return 0;
}
static int mp4_rd(void *ctx, uint32_t off, uint8_t *b, uint32_t n) { return vrd((const char *)ctx, off, b, n); }
/* ---- 8.0.93 소리: AAC 를 해독해 믹서 고리에 넣고, 영상 시계는 내보낸 소리 샘플 수를 따름 ---- */
static void au_pump(int maxf)
{
    if (!S.mp_au || S.mp_afin) return;
    for (int k = 0; k < maxf && mvsnd_ext_free() > 4096u; k++) {
        const int16_t *pcm; int n, ch, hz;
        if (!mp4p_audio_next(&pcm, &n, &ch, &hz)) { mvsnd_ext_finish(); S.mp_afin = 1; break; }
        mvsnd_ext_push(pcm, n, ch, hz);
    }
}
static void au_start(uint32_t at_ms)                           /* 그 시각에서 소리를 멈춘 채 준비 (약 0.9초 미리 해독) */
{
    if (!S.mp_au) return;
    mvsnd_ext_clear(); S.mp_afin = 0; S.mp_aon = 0; S.mp_aoff = 0; S.mp_stall = 0; S.mp_el_last = 0; S.mp_clk = 0;
    S.mp_base = mp4p_audio_seek(at_ms);
    au_pump(40);
}
static void au_close(void)
{
    if (S.mp_au) mvsnd_ext_close();
    S.mp_au = S.mp_aon = S.mp_afin = S.mp_aoff = 0;
}
static void crumb_nums(const char *tag, const uint8_t *v, int n);
static void mp4_tick(void)
{
    mm_crumb_quiet = (S.mp4 && S.playing && !S.paused) ? 1 : 0;
    if (S.playing && !S.paused) {
        if (S.mp_au && !S.mp_aoff) {
            au_pump(4);
            if (!S.mp_aon) { mvsnd_ext_play(1); S.mp_aon = 1; }
            uint32_t el = mvsnd_ext_elapsed();
            {   /* 8.0.115: 소리 시계(el)가 한꺼번에 뛰어도 그림이 뚝뚝 끊기지 않게, 화면 시계는 프레임마다 일정하게 가고 소리 시계 쪽으로 천천히 맞춤 */
                uint32_t a_us = (S.mp_base + (uint32_t)((uint64_t)el * 1000u / 48000u)) * 1000u;
                uint32_t tk = (vdp && (vdp->reg[9] & 2)) ? 20000u : 16667u;
                int32_t d = (int32_t)(a_us - S.mp_us);
                if (!S.mp_clk || d > 250000 || d < -250000) { S.mp_us = a_us; S.mp_clk = 1; }
                else { S.mp_us += tk; d = (int32_t)(a_us - S.mp_us); S.mp_us = (uint32_t)((int32_t)S.mp_us + d / 16); }
            }
            if (mvsnd_ext_ended()) S.mp_aoff = 1;                                 /* 소리가 끝남 → 이어서 프레임 시계 */
            else if (el == S.mp_el_last && mvsnd_pcm_ready() > 2400u) { if (++S.mp_stall > 60u) { S.mp_aoff = 1; mvsnd_ext_play(0); } }   /* 1초 넘게 소리가 안 나감(믹서가 안 돎) → 프레임 시계 */
            else S.mp_stall = 0;
            S.mp_el_last = el;
        } else S.mp_us += (vdp && (vdp->reg[9] & 2)) ? 20000u : 16667u;
        uint32_t t = S.mp_us / 1000u;
        uint32_t tm0 = mmusb_ms ? mmusb_ms() : 0;
        Mp4Pic pc; int r = mp4p_frame(t, 12, mmusb_ms, &pc);
        uint32_t tm1 = mmusb_ms ? mmusb_ms() : 0;
        if (r > 0) { mp4_present(); tstat_add(&mmvid_t_show, (mmusb_ms ? mmusb_ms() : 0) - tm1); }
        tstat_add(&mmvid_t_dec, tm1 - tm0);                                   /* 8.0.155 점검: 해독(해독+색 변환 한 프레임)과 화면에 올리기 시간 */
        if ((r < 0 || mp4p_ended()) && t >= S.mp_len_ms) { S.playing = 0; S.status |= ST_END; if (S.mp_au) mvsnd_ext_play(0), S.mp_aon = 0; mm_crumb_quiet = 0; v9_end(); }
    }
    if (S.bar && S.mp4 && S.opened && S.shown >= 0 && bar_key_now() != S.bar_key) S.shown = -1;   /* 8.0.149 표시줄에 보이는 것이 바뀜 */
    if (S.shown < 0) mp4_present();
}
static void crumb_nums(const char *tag, const uint8_t *v, int n)   /* 8.0.107 진단(LogCrumb): "태그 FF FF 05 00 ..." 한 줄 */
{
    char b[72]; int k = 0;
    if (!mmusb_crumb) return;
    while (*tag && k < 24) b[k++] = *tag++;
    for (int i = 0; i < n && k < 68; i++) { b[k++] = ' '; b[k++] = "0123456789ABCDEF"[v[i] >> 4]; b[k++] = "0123456789ABCDEF"[v[i] & 15]; }
    b[k] = 0; mmusb_crumb(b);
}
static void open_fail(uint8_t code)
{
    int sel = 0, nent = 0, valid = 0, flt = 0; mmusb_dbg(&sel, &nent, &valid, &flt);
    uint8_t d[8] = { code, S.arg[2], S.arg[3], (uint8_t)sel, (uint8_t)nent, (uint8_t)(nent >> 8), (uint8_t)valid, (uint8_t)flt };
    crumb_nums("OPEN FAIL code,it,it,sel,n,n,lv,flt", d, 8);
    S.status |= ST_ERR; S.oerr = code; res_clear(); res_put(code);
}
static int scr_ok_mp4(int scr) { return (scr >= 5 && scr <= 8) || scr == 12 || scr == 10 || scr == 11 || (mmv9_is(scr) && v9); }   /* 13·14 = V9990 (8.0.131, Gfx9000 이 있을 때만) */
static void cmd_open(void)
{
    int f = S.arg[0] | (S.arg[1] << 8), it = S.arg[2] | (S.arg[3] << 8), scr = S.arg[4];
    S.status &= (uint8_t)~(ST_ERR | ST_END);
    MM_CRUMB(f == 0xFFFF ? "video OPEN usb begin" : "video OPEN folder begin");
    crumb_nums("OPEN args", S.arg, 12);
    if (S.opened) MM_CRUMB("OPEN while opened (no STOP before)");     /* 8.0.107 진단: 앞 영상을 05h STOP 없이 두고 새로 열면 */
    au_close();
    if (S.mp4) { mp4p_close(); S.mp4 = 0; }
    v9_end();
    S.opened = S.playing = S.paused = 0; S.shown = -1; S.mi_ok = 0; S.oerr = 0; S.frames = 0;   /* 8.0.107: 앞 파일의 길이가 INFO 에 남지 않게 */
    S.mv = 0; S.mv_fsz = MV2_FRAME; S.mv_snd = MV2_SND; S.mv_k = 1; S.mv_lines = 192; S.mv_w = 256;
    if (!host) { open_fail(MP4E_READ); return; }
    if (f == 0xFFFD) {                                           /* 8.0.109: 방금 SETPATH 로 보낸 MSX 디스크 파일 */
        uint32_t sz = 0;
        if (!disk || !disk->find || !S.dpath_ok) { open_fail(MMV_DISKERR); return; }
        { int fr = disk->find(S.ddrv, S.dpath, S.path, sizeof S.path, &sz); if (fr == 2 && !disk->read_at) fr = -1; if (fr <= 0) { open_fail(fr < 0 ? MMV_DISKNOTVHD : MMV_DISKERR); return; } }
        S.fsize = sz;
    } else if (f == 0xFFFF) {                                    /* 8.0.82: P0P1 = FFFFh → USB 직접 접근에서 마지막 LIST 의 it 번째 파일 */
        uint32_t sz = 0;
        if (!mmusb_entry_file(it, S.path, sizeof S.path, &sz)) { open_fail(MP4E_READ); return; }
        S.fsize = sz;
    } else {
        if (f >= S.nf || load_folder(f) < 0 || it >= S.nent || S.ent[it].is_dir) { open_fail(MP4E_READ); return; }
        scopy(S.path, sizeof S.path, S.fpath[f]); sapp(S.path, sizeof S.path, sep()); sapp(S.path, sizeof S.path, S.ent[it].name);
        S.fsize = S.ent[it].size;
    }
    if (scr != 2) {
        uint8_t h16[0x40];                                       /* 8.0.101: 머리가 MMCSD_MV 면 MV5~MVC. 8.0.145: 설정(P4)이 V9 FAST 등이어도 MV5 는 SCREEN 5, MV6 는 SCREEN 6 … 자기 화면으로 */
        if (host->read_at && !vrd(S.path, 0, h16, sizeof h16)) { int mvs = mv_screen_of_header(h16); if (mvs) { uint8_t d[3] = { (uint8_t)mvs, (uint8_t)scr, (uint8_t)S.ov_on }; crumb_nums("OPEN MV scr,p4,ovon", d, 3); mv_open(h16, mvs); return; } }
    }
    if (scr != 2 && !scr_ok_mp4(scr)) { open_fail(MP4E_SCREEN); return; }   /* MP4·MOV·M4V·AVI 는 설정 SCREEN 을 따름 (8.0.145: 예전엔 맨 앞에서 검사해서 MV5~MVC 도 설정 SCREEN 을 따라야 했음) */
    if (scr != 2) {                                              /* 8.0.90 MP4: 구조만 읽어 INFO·오류 코드. 해독기가 생기면 여기서 이어서 연다 */
        if (!host->read_at) { open_fail(MP4E_READ); return; }
        int e = mp4_probe(mp4_rd, S.path, S.fsize, &S.mi);
        S.mi_ok = (e == MP4E_NONE);
        { uint8_t d[13] = { (uint8_t)e, (uint8_t)S.mi.width, (uint8_t)(S.mi.width >> 8), (uint8_t)S.mi.height, (uint8_t)(S.mi.height >> 8), (uint8_t)S.mi.dbg_top, (uint8_t)S.mi.dbg_boxes, (uint8_t)(S.mi.dbg_boxes >> 8),
                            (uint8_t)(S.mi.dbg_last >> 24), (uint8_t)(S.mi.dbg_last >> 16), (uint8_t)(S.mi.dbg_last >> 8), (uint8_t)S.mi.dbg_last, (uint8_t)(S.fsize >> 20) };
          crumb_nums("MP4 prb e,w,h,top,bx", d, 13); }               /* 8.0.115 진단: 코드 7 원인(마지막 최상위 상자 4글자는 ASCII 16진) */
        S.mp_clk = 0;
        if ((e == MP4E_NONE || e == MP4E_NOMOOV) && S.mi.width && !mp4lim_size_ok(S.mi.width, S.mi.height)) e = MP4E_TOOBIG;   /* 8.0.141: 4K 는 조각 MP4 여도 "크기 초과" 로 안내 */
        if (e == MP4E_NONE && S.mi.vcodec == 0x61766331 /* avc1 */ && S.mi.avc_profile != 66 && S.mi.avc_profile != 77 && S.mi.avc_profile != 100) e = MP4E_PROFILE;
        int v9o = mmv9_is(scr);                                                                                    /* 8.0.131 V9990 으로 내보냄 */
        if (e == MP4E_NONE && v9o && !v9) e = MP4E_SCREEN;                                                    /* Gfx9000 없음 (8.0.148: AVI 도 V9990 으로 내보냄) */
        if (e == MP4E_NONE && !v9o && (!vdp || !vdp->v9938 || scr == 10 || scr == 11)) e = MP4E_SCREEN;           /* MSX1·VDP 없음·10/11 */
        if (e == MP4E_NONE && !v9o && scr == 12 && !vdp->v9958) e = MP4E_SCREEN;                                       /* 8.0.92: YJK 는 V9958 이상만 */
        if (e == MP4E_NONE && !v9o && (scr == 8 || scr == 12) && vdp->vram_mask < 0x1FFFF) scr = 5;                      /* 8.0.92: VRAM 64KB 면 SCREEN 5(16색)로 대신 */
        if (e == MP4E_NONE && !v9o && scr == 7 && vdp->vram_mask < 0x1FFFF) e = MP4E_SCREEN;
        if (e == MP4E_NONE) { e = mp4p_open(mp4_rd, S.path, S.fsize, v9o ? 5 : scr, S.arg[5], S.arg[8], S.arg[9], S.arg[10]);
            if (e == MP4E_PROFILE) e = mp4lim_error_from_why(mp4p_why()); }   /* 8.0.141: 이유에 따라 4(프로파일) / 13(크기) / 14(해독 실패) */   /* 8.0.141: 이유에 따라 4(프로파일) / 13(크기) / 14(해독 실패) */
        if (e == MP4E_PROFILE || e == MP4E_TOOLONG || e == MP4E_TOOBIG || e == MP4E_DECODE) { uint8_t d[9] = { (uint8_t)e, (uint8_t)S.mi.avc_profile, (uint8_t)S.mi.width, (uint8_t)(S.mi.width >> 8), (uint8_t)S.mi.height, (uint8_t)(S.mi.height >> 8), (uint8_t)S.mi.nframes, (uint8_t)(S.mi.nframes >> 8), (uint8_t)mp4p_why() };
            crumb_nums("MP4 why e,p,w,h,nfr,at", d, 9); }           /* 8.0.122: 코드 4·12 의 이유 (프로파일·크기·프레임 수 아래 16비트) */
        if (e != MP4E_NONE) { open_fail((uint8_t)e); return; }
        S.mp4 = 1; S.mp_us = 0; S.mp_len_ms = mp4p_len_ms(); memset(&mmvid_t_dec, 0, sizeof mmvid_t_dec); memset(&mmvid_t_show, 0, sizeof mmvid_t_show);
        { uint32_t f = (uint32_t)((uint64_t)S.mp_len_ms * 15 / 1000); S.frames = f ? f : 1; }
        if (v9o) { S.bar = 1; S.bar_level = 0; S.bar_key = ~0u; mp4p_raw = 1; v9m = scr; mmv9_start(v9, scr); { uint8_t d[5] = { (uint8_t)scr, v9->regs[6], v9->regs[8], v9->regs[13], (uint8_t)v9990_visible(v9) }; crumb_nums("V9 start scr,R6,R8,R13,vis", d, 5); } } else { S.bar = S.arg[5] != 212; S.bar_level = 0; S.bar_key = ~0u; screen_bitmap(scr, (S.arg[5] == 212 || S.bar) ? 212 : 192); }   /* 8.0.131: 13·14 는 VDP 를 건드리지 않고 V9990 포트로 */
        { Mp4Pic pc; mp4p_frame(0, 0, 0, &pc); mp4_present(); }
        if (mp4p_audio_open()) { mvsnd_ext_open(); S.mp_au = 1; au_start(0); }
        S.opened = 1; res_clear();
        { uint8_t d[2] = { (uint8_t)S.frames, (uint8_t)(S.frames >> 8) }; crumb_nums("OPEN mp4 ok frames", d, 2); }
        return;
    }
    uint8_t h[0x40];
    if (!host->read_at || vrd(S.path, 0, h, sizeof h) || memcmp(h, "MMCSD_MV", 8) || mv_header_letter(h) != '2') { open_fail(MP4E_READ); return; }
    uint32_t n = (uint32_t)h[0x18] | (uint32_t)h[0x19] << 8 | (uint32_t)h[0x1A] << 16 | (uint32_t)h[0x1B] << 24;
    uint32_t fit = S.fsize > MV2_HDR ? (S.fsize - MV2_HDR) / MV2_FRAME : 0;
    if (n == 0 || n > fit) n = fit;
    if (n == 0) { open_fail(MP4E_READ); return; }
    S.frames = n;
    MM_CRUMB("video OPEN header ok, screen2/sound_open");
    screen2();
    sound_open(0);
    MM_CRUMB("video OPEN sound_open done, draw(0)");
    if (draw(0)) { open_fail(MP4E_READ); return; }
    S.opened = 1;
    MM_CRUMB("video OPEN done");
    { uint8_t d[2] = { (uint8_t)S.frames, (uint8_t)(S.frames >> 8) }; crumb_nums("OPEN ok frames", d, 2); }
    res_clear();                                                 /* INFO 를 OPEN 직후에 읽어도 되게 미리 채움 */
}
static void cmd_info(void)
{
    res_clear();
    { uint8_t d[6] = { (uint8_t)S.frames, (uint8_t)(S.frames >> 8), (uint8_t)S.opened, (uint8_t)S.mv, (uint8_t)S.mp4, (uint8_t)S.mi_ok }; crumb_nums("INFO frames,opened,mv,mp4,mi", d, 6); }
    uint16_t v[4] = { (uint16_t)(S.frames > 65535 ? 65535 : S.frames), 256, 192, 1500 };
    if (S.mi_ok) {                                  /* 8.0.90 MP4: 길이(1/15초)·가로·세로·원본 fps×100·소리 */
        uint32_t len = (uint32_t)((uint64_t)(S.mp4 ? S.mp_len_ms : S.mi.dur_ms) * 15 / 1000);
        v[0] = (uint16_t)(len > 65535 ? 65535 : len); v[1] = S.mi.width; v[2] = S.mi.height; v[3] = (uint16_t)(S.mi.fps100 > 65535 ? 65535 : S.mi.fps100);
    }
    if (S.mv && S.opened) { uint32_t len = S.frames * (uint32_t)S.mv_k; v[0] = (uint16_t)(len > 65535 ? 65535 : len); v[1] = (uint16_t)S.mv_w; v[2] = (uint16_t)S.mv_lines; v[3] = (uint16_t)(1500 / S.mv_k); }   /* 8.0.101 MV5~MVC: 길이 1/15초 = 프레임 × k */
    for (int i = 0; i < 4; i++) { res_put((uint8_t)v[i]); res_put((uint8_t)(v[i] >> 8)); }
    res_put(S.mi_ok ? (S.mi.has_audio ? 1 : 0) : 1);
}
static void snd_start_if_ready(void)
{
    if (S.playing && !S.paused && !S.snd_started && (S.feed >= S.frames || S.feed >= (uint32_t)START_MIN + (uint32_t)mvsnd_position())) { snd_cmd(0x02, 0); S.snd_started = 1; }
}
static void command(uint8_t c)
{
    S.cmd = c; S.argn = 0; S.txtm = 0;
    if (c < 0x20) mm_crumb_cmd("video cmd", c, c == 0x01 || c == 0x03 || c == 0x04 || c == 0x05 || c == 0x07 || c == 0x10 || c == 0x11);
    if (mmusb_args(c) >= 0) {                                  /* 8.0.80 USB 직접 접근 */
        S.usbm = 1;
        if (mmusb_args(c) == 0) { S.ustat = mmusb_run(c, S.arg); S.cmd = 0; }
        return;
    }
    S.usbm = 0;
    switch (c) {
    case 0x01: res_clear(); res_put('X'); res_put('V'); res_put('D'); res_put(0x01); res_put((mmusb_available() ? 0x03 : 0x00) | 0x04 | 0x08 | (mmusb_available() ? 0x10 : 0x00) | (disk ? 0x20 : 0x00) | (v9 ? 0x40 : 0x00)); break;   /* 8.0.109 bit5 = MSX 디스크 파일 열기. 5번째 = 능력 (bit0 USB 직접 접근 20h~29h, bit1 영상 OPEN P0P1=FFFFh 로 USB 영상 열기 — 8.0.82) */
    case 0x08: cmd_info(); break;
    case 0x2A: if (disk) S.dpath_ok = 0; break;                /* 8.0.109 SETPATH: 드라이브 1바이트 + 경로(0 끝) 를 인자로 받음 */
    case 0x30: S.txtm = 1; S.tstat = 0; S.txt_n = S.txt_pos = 0; break;       /* 8.0.88 TEXT: 인자(앞머리 5 + 글자)를 다 받으면 그림 */
    case 0x0D: S.ov_rx = 0; break;        /* 이후 25h 쓰기 4096 바이트가 글자 줄 */
    default: break;                                            /* 나머지는 인자를 다 받은 뒤 (또는 인자 없는 것은 바로) */
    }
    switch (c) {                                               /* 인자 없는 명령은 바로 실행 */
    case 0x03: if (S.opened && S.mp4) {                        /* 8.0.91: 끝난 뒤 PLAY 면 처음부터 */
                   if (S.status & ST_END) { uint32_t a = mp4p_seek(0); S.mp_us = a * 1000u; Mp4Pic pc; mp4p_frame(a, 0, 0, &pc); mp4_present(); au_start(a); }
                   S.playing = 1; S.paused = 0; S.status &= (uint8_t)~ST_END;
               } else if (S.opened) { S.playing = 1; S.paused = 0; S.status &= (uint8_t)~ST_END; if (S.snd_started) snd_cmd(0x02, 0); else snd_start_if_ready(); } break;
    case 0x04: if (S.opened && S.playing) { S.paused = 1; if (!S.mp4) snd_cmd(0x03, 0); else if (S.mp_au) { mvsnd_ext_play(0); S.mp_aon = 0; } } break;
    case 0x05: if (S.opened) { if (S.mp4) { mm_crumb_quiet = 0; au_close(); mp4p_close(); S.mp4 = 0; v9_end(); } else snd_cmd(0x04, 0); S.opened = S.playing = S.paused = 0; S.snd_started = 0; } break;
    case 0x09: if (S.mp4 && S.opened) { uint32_t p = (uint32_t)((uint64_t)(S.mp_us / 1000u) * 15 / 1000); S.pos_latch = (uint16_t)(p > 65535 ? 65535 : p); }
               else if (S.mv && S.opened) { uint32_t p = (mvsnd_position() > S.frames ? S.frames : mvsnd_position()) * (uint32_t)S.mv_k; S.pos_latch = (uint16_t)(p > 65535 ? 65535 : p); }   /* 8.0.101: 1/15초 단위 */
               else S.pos_latch = (uint16_t)(S.opened ? (mvsnd_position() > S.frames ? S.frames : mvsnd_position()) : 0); break;
    default: break;
    }
}
static void run_text(void)                                     /* 8.0.88 30h TEXT */
{
    int r = mmtext_draw(S.arg[0], S.arg[1], S.arg[2] | S.arg[3] << 8, S.arg + 5, S.arg[4], S.txt, sizeof S.txt);
    S.txt_pos = 0; S.txt_n = r; S.tstat = r ? 0 : ST_ERR;
}
static void run_with_args(void)                                /* 인자를 다 받은 뒤 */
{
    if (mmusb_args(S.cmd) > 0) { S.ustat = mmusb_run(S.cmd, S.arg); return; }
    switch (S.cmd) {
    case 0x10: cmd_list(); break;
    case 0x11: cmd_open(); break;
    case 0x06: if (S.opened && S.mp4) {                        /* 8.0.91: 키프레임으로 이동, 화면은 바로 바뀜, PLAY/PAUSE 유지 */
                  uint32_t want = (uint32_t)((uint64_t)(S.arg[0] | S.arg[1] << 8) * 1000 / 15), a = mp4p_seek(want);
                  S.mp_us = a * 1000u; S.status &= (uint8_t)~ST_END; Mp4Pic pc; mp4p_frame(a, 0, 0, &pc); mp4_present(); au_start(a);
               } else if (S.opened) { uint32_t t = (uint32_t)(S.arg[0] | S.arg[1] << 8); if (S.mv) t /= (uint32_t)S.mv_k; if (t >= S.frames) t = S.frames - 1; int was = S.playing && !S.paused;
                  sound_open(t); S.shown = -1; S.status &= (uint8_t)~ST_END; if (was) snd_start_if_ready(); draw(t); } break;
    case 0x07: if (!S.mp4) { snd_cmd(0x06, S.arg[0]); S.bar_level = mmbar_level_of_percent(S.arg[0]); } else { mvsnd_ext_volume(S.arg[0]); S.bar_level = mmbar_level_of_percent(S.arg[0]); } break;
    case 0x0E: { int on = S.arg[0] ? 1 : 0; if (on != S.ov_on) ov_crumb_switch(on); S.ov_on = on; S.shown = -1; } break;
    default: break;
    }
}
static int need_args(uint8_t c) { if (mmusb_args(c) > 0) return mmusb_args(c); switch (c) { case 0x10: return 4; case 0x11: return 12; case 0x06: return 2; case 0x07: case 0x0E: return 1; default: return 0; } }

uint8_t mmvid_read(int port)
{
    switch (port & 3) {
    case 0: return (uint8_t)(ST_OVLOK | ((S.txtm ? ((S.usbm ? S.ustat : S.status) & (ST_END | ST_LISTEND)) | (S.tstat & ST_ERR) : S.usbm ? S.ustat : S.status) & (ST_END | ST_ERR | ST_LISTEND)) | (S.playing && !S.paused ? ST_PLAY : 0) | (S.paused ? ST_PAUSE : 0));
    case 1: if (S.txtm) return S.txt_pos < S.txt_n ? S.txt[S.txt_pos++] : 0;
        if (S.usbm) return mmusb_get(); return S.res_pos < S.res_n ? S.res[S.res_pos++] : 0;
    case 2: return (uint8_t)S.pos_latch;
    default: return (uint8_t)(S.pos_latch >> 8);
    }
}
void mmvid_write(int port, uint8_t v)
{
    switch (port & 3) {
    case 0: command(v); break;
    case 1:
        if (S.cmd == 0x0D) { if (S.ov_rx < 4096) S.ov[S.ov_rx++] = v; if (S.ov_rx == 4096) { S.shown = -1; S.cmd = 0; ov_crumb_received(); } }
        break;
    case 2:
        if (!S.cmd) { uint8_t d[1] = { v }; crumb_nums("~ARG WITHOUT CMD", d, 1); }   /* 8.0.107 진단: 명령 없이 온 인자(순서가 거꾸로면 OPEN 이 안 돎) */
        if (S.argn < (int)sizeof S.arg) S.arg[S.argn++] = v;
        if (S.cmd == 0x2A) {                                   /* 8.0.109 SETPATH: 0 을 받으면 끝 (경로 최대 128) */
            if (S.argn >= 2 && S.arg[S.argn - 1] == 0) {
                S.ddrv = S.arg[0]; memcpy(S.dpath, S.arg + 1, (size_t)S.argn - 1); S.dpath_ok = 1;
                crumb_nums("SETPATH drive,len", (const uint8_t[2]){ S.ddrv, (uint8_t)(S.argn - 2) }, 2);
                S.argn = 0; S.cmd = 0;
            } else if (S.argn > 129) { S.dpath_ok = 0; S.argn = 0; S.cmd = 0; }
            break;
        }
        if (S.cmd == 0x30) {                                   /* 8.0.88 TEXT: 글자 수(5번째)만큼 더 받음 */
            if (S.argn >= 5 && S.arg[4] > 200) { S.tstat = ST_ERR; S.cmd = 0; }
            else if (S.argn >= 5 && S.argn >= 5 + S.arg[4]) { run_text(); S.argn = 0; S.cmd = 0; }
            break;
        }
        if (S.argn >= need_args(S.cmd) && need_args(S.cmd) > 0) { run_with_args(); S.argn = 0; S.cmd = 0; }
        break;
    default: break;
    }
}

int mmvid_playing(void) { return S.opened && S.playing && !S.paused; }
uint32_t mmvid_frame(void) { return S.shown < 0 ? 0 : (uint32_t)S.shown; }

void mmvid_tick(VDP *v)
{
    if (v) vdp = v;
    if (!S.opened) return;
    if (S.mp4) { mp4_tick(); return; }
    uint32_t pos = mvsnd_position(); if (pos >= S.frames) pos = S.frames ? S.frames - 1 : 0;
    if (S.playing && !S.paused) {
        for (int k = 0; k < FEED_MAX && S.feed < S.frames && S.feed < pos + AHEAD && MVSND_FIFO_SIZE - 1 - mvsnd_fifo_used() > 8192u; k++) if (!feed_one()) break;
        snd_start_if_ready();
    }
    if (S.bar && S.mv && S.shown >= 0 && bar_key_now() != S.bar_key) S.shown = -1;   /* 8.0.156 표시줄에 보이는 것이 바뀜 */
    uint32_t want = S.playing ? pos : (S.shown < 0 ? 0 : (uint32_t)S.shown);
    if ((int32_t)want != S.shown) { if (draw(want)) S.status |= ST_ERR; }
    if (S.playing && !S.paused && S.snd_started) {                  /* 끝: 소리를 다 먹였고 위치가 더 안 감 (끝 프레임들은 소리 길이 0 이라 위치가 끝까지 안 가기도 함) */
        if (pos == S.last_pos) S.stall++; else { S.stall = 0; S.last_pos = pos; }
        if (S.feed >= S.frames && (mvsnd_position() + 1 >= S.frames || S.stall >= 20)) {
            snd_cmd(0x04, 0); S.playing = 0; S.snd_started = 0; S.status |= ST_END; S.stall = 0;
        }
    } else S.stall = 0;
}

