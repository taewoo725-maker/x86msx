/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fe.c - 공통 프런트엔드 v2.0 (레거시·UEFI 공용, 설계서 §7)
 *
 *  ┌ 메뉴 막대 (F11, 5초 숨김) ───────────────────────────┐
 *  │LED 판│              MSX 화면 (정수배)                 │
 *  └──────┴─────────────────────────────────────────────┘
 * 키 흐름: 스캔 코드 → 수정 키 → 오른쪽 Alt 단독 판정 → blueMSX 단축키 → 메뉴/대화 창 → MSX(input.c)
 */
#include "fe.h"
#include "../src/gmsynth.h"
#include "../third_party/nukedsc55/sc55w.h"   /* 5.2.60 */
#include "../src/midipkt.h"
#include "../src/vsrc.h"
#include "../src/persist.h"
#include "../src/media.h"
#include "diag.h"
#include "input.h"
#include "font.h"
#include "lang.h"
#include "lang2.h"
#include "lang_ar.h"                                  /* 3.4.3 LANG3-01 */
#include "version.h"
#include "machine.h"
#include "icons.h"
#ifdef FE_HOST_DEBUG
#include <stdio.h>
#endif
#include "../src/status.h"
#include "../src/spcat.h"
#include "../src/mididev.h"                        /* MIDI 기계 부품 틀 */
#include "badges.h"
#include "keymap.h"
#include "sha1.h"
#include "png.h"
#include "medit.h"
#include "romdb.h"
#include "dskimg.h"
#include "msxfat.h"
#include "fat.h"
#include "help.h"
#include "zipx.h"
#include "basictxt.h"
#include <string.h>

static int rep_key = -1;   /* 4.0.62: 앞쪽에서 쓰므로 위로 */
static FeDisplay D;
static FePlatform P;
static MSX *M;

/* 화면 배치 */
static uint32_t x0, y0, out_w, out_h;
static void layout(void);
static uint32_t sx_eff, sy_eff, disp_lines = 192;
#define FRAME_LINES 212u                    /* 틀은 늘 212줄 (192줄 화면은 그 안 가운데, 위아래 10줄은 테두리 색) — 점 모양이 기종·화면 모드와 상관없이 같게 */
static uint16_t row_y[213];                 /* 줄마다 화면 y (정수가 아닌 배율) */
static uint32_t voff;                       /* 192줄 화면이 212줄 틀에서 시작하는 줄 (10), 212줄이면 0 */
static uint32_t pix_aspect_mil;             /* 지금 그리는 MSX 점의 가로/세로 (×1000, 진단용) */
static uint32_t g7_32[256];
static uint32_t pal32[16];
static const UiFont *F;             /* 메뉴 글꼴 */
static uint32_t S = 1;              /* 글꼴 배율 */
static uint32_t LH;                 /* 줄 높이 */
static const UiFont *SF;            /* 작은 글씨 (2.9): 기종 이름, 슬롯 표 */
static uint32_t SLH;                /* 작은 글씨 줄 높이 */
static int force_redraw = 1, margin_bd = -1;
/* 3.5.0 떠 있는 막대 (fe_ov.inc): 비트 0 위 도구 막대, 비트 1 오른쪽 미디어 판 */
static int ov_on, ov_dirty, rp_fixed;
static uint32_t rp_x, rp_y, rp_w, rp_h;
static uint32_t tb_h(void);
static int pop_mode, pop_ralign;                   /* 막대에서 연 팝업 메뉴 (메뉴 막대 없이) */
static uint32_t pop_x, pop_y;
static uint8_t shadow_bd[VDP_H];            /* 줄 테두리 색 (MSX 화면 옆 띠) */

/* 상태 */
enum { RUN_ON, RUN_PAUSED, RUN_STOPPED };
static int run_state = RUN_ON;
static int speed = 100, speed_max, turbo_hold;
static int video_out;
static int dd_on;                       /* 드롭다운 펼침 여부 (아래에 본체) */                                   /* 4.0.34: 화면 출력 번호 (UEFI, 다시 시작하면 적용) */
static int volume = 70, muted, vol_undo = 70, muted_undo;   /* 4.0.34: ↑↓ 같이 누르면 무음 (먼저 눌린 한 단계는 되돌림) */
static int lang;                    /* 0 한국어, 1 영어, 2 일본어, 3~9 독·프·스페인·스웨덴·러·포르투갈(브)·에스토니아 */
static int monitor_kind;            /* 0 컬러, 1 흑백, 2 녹색, 3 호박색 */
static int scanlines;               /* 3.2.10 VID-01: 스캔라인 세기 0~100 % (0 = 끔) */
static int scan_last = 40;
static int mon_emu;                 /* 3.2.11 모니터 에뮬레이션 (blueMSX 목록): 0 없음, 1 모니터, 2 Yc, 3 Yc+잡음, 4 컴포지트, 5 컴포지트+잡음, 6 Scale 2x, 7 Hq2x */
#define MON_EMU_N 8
static int fx_blur, fx_glow, fx_noise;
static int vsrc_cur, vsrc_pref = -1;       /* 지금 보이는 화면 소스 (src/vsrc.h), 고른 소스 (-1 자동) */
static uint32_t v9x, v9y, v9w, v9h;        /* 나란히일 때 V9990 그림 자리 (오른쪽 반). 그 밖엔 MSX 화면 자리 그대로 */
static int vcomp;                          /* 0 하나만 보임, 1 겹치기 (V9990 을 MSX 위에 50 %), 2 나란히 */
#define v9_show (vsrc_cur != VSRC_VDP)       /* VDP 가 아닌 소스 — 여백은 테두리 색 줄 없이 */
static int aspect_mode;                    /* 3.2.15 VID-08: 0 4:3 (5.2.125: 정사각 점에서 바꿈), 1 TV(점 1.14:1), 2 화면 가득 */     /* 3.2.11 화면 효과 0~100 (openMSX blur·glow·noise) */
static int spr_hide, spr_nolimit;                  /* 3.2.0 VID-03 */
static const int snd_out_order[3] = { 0, 1, 2 };            /* 4.0.93: 목록 순서 = 자동, 스피커, 이어폰 출력, HDMI 출력 (값: 0 자동, 1 스피커, 3 이어폰, 2 HDMI) */
static StrId snd_out_str(int v) { return v == 1 || v == 3 ? S_SND_SPK : v == 2 ? S_SND_HDMI : S_AUDIO_AUTO; }
static int snd_out_pos(int v) { if (v == 3) v = 1; for (int i = 0; i < 3; i++) if (snd_out_order[i] == v) return i; return 0; }
static int audio_out;               /* 0 자동, 1 스피커·라인 출력, 2 HDMI */
static uint8_t mon_rgb[16][3];      /* 모니터 종류를 적용한 MSX 색 (8비트 DAC에도 씀) */

const uint8_t *fe_palette_rgb(void) { return &mon_rgb[0][0]; }

/* 화면 설정 (2.12): 밝기 -50~50, 대비 50~150 %, 채도 0~200 %, 감마 50~200 (100 = 1.0) */
static int vid_bright, vid_contrast = 100, vid_satur = 100, vid_gamma = 100;
static uint8_t vid_lut[256];
static int vid_lut_g;
void gamma_lut(unsigned char *out, int g100);
static int vid_clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static void video_adjust(uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!vid_bright && vid_contrast == 100 && vid_satur == 100 && vid_gamma == 100) return;
    if (vid_lut_g != vid_gamma) { gamma_lut(vid_lut, vid_gamma); vid_lut_g = vid_gamma; }
    int c[3] = { *r, *g, *b }, l = (c[0] * 30 + c[1] * 59 + c[2] * 11) / 100;
    for (int i = 0; i < 3; i++) {
        int v = l + (c[i] - l) * vid_satur / 100;                       /* 채도 */
        v = 128 + (v - 128) * vid_contrast / 100;                       /* 대비 */
        v += vid_bright * 255 / 100;                                    /* 밝기 */
        c[i] = vid_lut[vid_clamp(v)];                                   /* 감마 */
    }
    *r = (uint8_t)c[0]; *g = (uint8_t)c[1]; *b = (uint8_t)c[2];
}
static void monitor_tint(uint8_t *r, uint8_t *g, uint8_t *b)
{
    video_adjust(r, g, b);
    if (!monitor_kind) return;
    uint32_t l = ((uint32_t)*r * 30 + (uint32_t)*g * 59 + (uint32_t)*b * 11) / 100;
    if (monitor_kind == 1) { *r = *g = *b = (uint8_t)l; }
    else if (monitor_kind == 2) { *r = (uint8_t)(l * 20 / 100); *g = (uint8_t)l; *b = (uint8_t)(l * 30 / 100); }
    else { *r = (uint8_t)l; *g = (uint8_t)(l * 70 / 100); *b = 0; }
}
static char wc_status[32] = "WC: ?", snd_status[32] = "snd: off", usb_status[32] = "", vs_status[64] = "";
static int measured_speed = 100, measured_busy;
/* 4.0.15: 구간별 시간 계측 (진단 화면). 0 코어 1 소리 2 MIDI 3 화면 4 가상하드·USB */
enum { PF_CORE, PF_AUDIO, PF_MIDI, PF_VIDEO, PF_IO, PF_POLL, PF_BLIT, PF_USBW, PF_IDLE, PF_FILE, PF_MOUSE, PF_SYNTH, PF_N };   /* PF_SYNTH = MIDI 합성기 렌더 (core 에서 따로 뗌) */
static uint64_t pf_acc[PF_N], pf_max[PF_N], pf_t0;
static uint32_t pf_ms0, pf_pct[PF_N + 1], pf_worst[PF_N];
static inline uint64_t pf_now(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
#define PF_BEGIN uint64_t pf_s_ = pf_now()
#define PF_END(k) do { uint64_t d_ = pf_now() - pf_s_; pf_acc[k] += d_; if (d_ > pf_max[k]) pf_max[k] = d_; } while (0)
void fe_pf_add(int k, uint64_t d) { if (k >= 0 && k < PF_N) { pf_acc[k] += d; if (d > pf_max[k]) pf_max[k] = d; } }
static void pf_window(uint32_t now)
{
    if (!pf_t0) { pf_t0 = pf_now(); pf_ms0 = now; return; }
    uint32_t ems = now - pf_ms0;
    if (ems < 1000) return;
    uint64_t el = pf_now() - pf_t0;
    uint32_t e16 = (uint32_t)(el >> 16); if (!e16) e16 = 1;
    uint32_t sum = 0;
    for (int k = 0; k < PF_N; k++) {
        pf_pct[k] = (uint32_t)(pf_acc[k] >> 16) * 100u / e16; sum += pf_pct[k];
        pf_worst[k] = (uint32_t)(pf_max[k] >> 16) * ems / e16;
        pf_acc[k] = pf_max[k] = 0;
    }
    pf_pct[PF_N] = sum < 100 ? 100 - sum : 0;
    pf_t0 = pf_now(); pf_ms0 = now;
}
/* 최근 파일 (2.12): 0 롬, 1 디스크, 2 테이프 */
static char rc_path[3][8][FE_PATH_MAX], rc_name[3][8][64];
static int recent_kind_of(int k) { return k < 4 || k == 7 ? 0 : k < 6 ? 1 : 2; }   /* 메뉴 번호(0~7, 3.6.7: 7 = 카트리지 5) → 목록 종류 */
static void recent_add(int kind, const char *path, const char *name);
static char cart_name[MSX_CART_SLOTS][64];
static char cart_path[MSX_CART_SLOTS][FE_PATH_MAX];  /* 마지막 상태 저장용 */
static int cart_mapper[MSX_CART_SLOTS] = { -1, -1, -1, -1, -1 };   /* 사용자가 고른 매퍼, -1 = 자동 */
static uint32_t cart_size[MSX_CART_SLOTS];
static int slot_expand;                     /* 카트리지 슬롯 확장: 0 없음, 1 슬롯 1, 2 슬롯 2 */
static char machine_name[128] = FALLBACK_MACHINE;
static int joy_mode[2];
static uint8_t joy_keys[2][6] = { { 0xC8, 0xD0, 0xCB, 0xCD, 0x39, 0x32 }, { 0x11, 0x1F, 0x1E, 0x20, 0x22, 0x23 } };   /* 2.9: 포트별 키 */
static int joy_cap;                         /* 키 잡는 중인 항목 id (0 = 아님) */                     /* 조이스틱 포트 1·2: 0 없음, 1 키보드 */
/* 디스크 드라이브 A·B (설계서 §6.7) */
#define DISK_MAX 1474560u                       /* 3.0.2: 2HD 1.44 MB까지 */
static uint8_t disk_buf[2][DISK_MAX];
static uint32_t disk_size[2];
static char disk_path[2][FE_PATH_MAX], disk_name[2][64];
static int disk_wp[2];
static uint32_t disk_due[2];
static int sp_joyrex_slot = -1;                /* 2.16: Joyrex PSG 카트리지가 꽂힌 자리 */
static int x86midi_auto = -1;                    /* 자동으로 꽂은 X86MSX MIDI 카트리지 자리 (기계를 다 빼면 같이 뺌) */
static int sp_msxmidi_slot = -1;               /* 외장 MSX-MIDI 인터페이스 카트리지 자리 (포트 E8h~EFh, 본체 MSX-MIDI 와 같은 것) */
static int cart_special[MSX_CART_SLOTS];    /* 64/65 디스크 인터페이스, 1 SCC, 2 FM-PAC, 3 MSX-DOS2, 4 메모리 매퍼 */
static uint8_t cart_buf[MSX_CART_SLOTS][MSX_ROM_MAX];   /* 앞으로 옮김 (fe_v9990.inc 가 씀) */
static int disk_drives(void);
static int vhd_cart(void);
static void vhd_img_chg(void);
static uint32_t vhd_img_off, vhd_img_n;                /* 장치 8로 보이는 구간 (파티션 하나 = FAT 부트 섹터부터) */
static int vhd_img;                                    /* 하드 디스크 이미지를 Nextor 2.1.3 가상 디스크 카트리지의 장치 8로 (fe_vhd.inc) */
static int hd_rd_cb(void *c, uint32_t lba, uint8_t *b);
static int hd_wr_cb(void *c, uint32_t lba, uint8_t *b);                            /* 3.7.1 fe_vhd.inc */
static long ini_long(const char *v);
static int vhd_build_rom(uint8_t *b, uint32_t *size);
static int is_nextor_rom(const uint8_t *b, uint32_t n);
static void vhd_mount_all(int quiet);
static void vhd_sync_all(void);
static void vhd_unmount_all(void);
static void vhd_make_dirs(void);
/* 카세트 테이프 */
#define TAPE_MAX (1024u * 1024u)
static uint8_t tape_buf[TAPE_MAX];
static char tape_path[FE_PATH_MAX], tape_name[64];
static char tape_title[128], tape_start[64];       /* 3.3.2 MED-08 카세트 DB 제목·시작 명령 */
static int tape_nfile;                              /* 3.2.1: 테이프 안 파일 (tape_scan) */
static uint32_t tape_fpos[16];
static uint8_t tape_ftype[16];
static char tape_fname[16][8];
static void tape_scan(void);
static void tape_new(void);
static uint32_t tape_due;
static char tape_dir[FE_PATH_MAX] = FE_CAS, tape_dir_disp[FE_PATH_MAX] = FE_DISP_STORE " > CAS", tape_sel[FE_KEY_MAX];
static int tape_top = -1;
static char bas_dir[FE_PATH_MAX] = FE_BAS, bas_dir_disp[FE_PATH_MAX] = FE_DISP_STORE " > BASIC", bas_sel[FE_KEY_MAX];   /* 3.2.7 BASIC 텍스트 */
static int bas_top = -1;
static int prn_mode;
static int prn_preview = 1;   /* 4.0.6: 0 끔 1 쪽이 차거나 끝낼 때 2 5초 쉬면 */                          /* 4.0.5: 인쇄가 끝나면 미리보기 팝업 */
static int pv_valid;                                 /* 마지막 인쇄가 prn_buf에 남아 있음 */
static uint32_t pv_last_len;
static char pv_last_name[48];
static uint8_t prn_buf[262144];
static uint32_t prn_len;
static void pv_open(const uint8_t *d, uint32_t n, const char *name);
static void prn_forget(void);
static void prn_flush(int user);
static void prn_set_mode(int m);
static void prn_frame(uint32_t now);
static int midi_int = 1;                             /* 3.6.1: 내장 MIDI(turbo R GT의 MSX-MIDI) 켬 */
static int midi_io_on(void);                           /* 5.2.74 */
static int midi_out = 1;                            /* 3.5.4 MIDI 출력: 0 없음, 1 소프트웨어 MIDI, 2 USB MIDI 장치, 3 파일(.mid) (3.3.3 SND3-04) */
static int ini_mo_seen, ini_old_out = -1, ini_old_syn = -1;   /* 3.4.x ini(MidiOut·MidiSynth) 옮기기 */
static char midi_out_name[32];                      /* USB MIDI 출력 장치 이름 (비면 없음) */
static int midi_in = 0;                             /* 3.4.4 DEV-35: USB MIDI 장치 → MSX-MIDI 받기 */
static int midi_in_port = 0;                        /* 받는 MIDI IN 단자 (MDP_*: 0 MSX-MIDI 1 SFG 2 Music Module) — 없으면 있는 첫 단자 */
static int midi_synth = GM_SF2;                     /* 3.4.5 SND3-07: 소프트웨어 MIDI 음원 1 FM 2 SoundFont (3.5.4: 끄기는 MIDI 출력에서) */
#ifdef FE_UEFI
#define MT32_OK 1                                        /* MT-32 / CM-32L 롬 음원은 UEFI 판만 */
#else
#define MT32_OK 0
#endif
static void int_to_str(char *o, int max, int v);
static int fx_rev = 100, fx_cho = 100, fx_len = 100, fx_on = 1;   /* 공간 음향 (MIDI 탭): 리버브·코러스 세기·잔향 길이 (%, 곡이 정한 값 = 100), 켜기 */
static void fx_apply(void);
static int has_ci(const char *h, const char *n);
static char ini_mtc[64], ini_mtp[64], ini_mt2[140];       /* ini 의 MtCtlRom·MtPcmRom (첫 MT-32), MtRom2 (둘째 MT-32 "제어|PCM") — 읽은 뒤 기계에 넣음 */
static int acpi_mode; static char acpi_line[64];           /* ACPI 모드 켜기 실험 (Lenovo V14 스페이스+대각선) */
static int mt_model;                                     /* 0 MT-32, 1 CM-32L (USB\MidiSound\MT32 폴더의 롬) */
#define MT_N 2                                           /* MT-32 엔진 둘 — 꽂은 순서대로 MT-32 기계 1·2 가 엔진 0·1 (기계마다 자기 롬) */
static int mt_st[MT_N];                                  /* 0 아직, 1 불러옴, -1 롬 파일 없음, -2 롬이 아님, -3 열기 실패, -4 메모리, -9 이 판에는 없음 */
static char mt_inf[MT_N][48];
static char mt_sigl[MT_N][140];                          /* 지금 불러온 롬의 서명 (모델·제어·PCM 이름) — 기계의 선택이 달라졌는지 봄 */
static uint8_t *mt_mem_s[MT_N];                          /* 엔진 4 MiB + 제어 롬 128 KiB + PCM 롬 1 MiB (영상 녹화 버퍼 끝을 떼어 냄, 슬롯마다 한 번만) */
/* SoundFont 칸 둘 — 0 = GM 음원 (SoundFont=), 1 = GS 기계 (GsSoundFont=, 비면 칸 0 을 같이 씀) */
typedef struct {
    char name[FE_KEY_MAX];                          /* SoundFont 폴더 안 파일 이름 (칸 0: 비면 첫 .sf2) */
    uint8_t *mem;                                   /* SoundFont 자리 = 영상 녹화 버퍼 끝을 떼어 냄 (한 번 정하면 그대로) */
    uint32_t cap, size;
    int state;                                      /* 0 아직, 1 불러옴, -1 파일 없음, -2 너무 큼, -3 형식, -4 메모리 */
    char path[FE_PATH_MAX];                         /* 스트림 읽기용 */
} FeSf;
static FeSf fe_sf[GM_SF_MAX];
#define sf_name  (fe_sf[0].name)
#define sf_mem   (fe_sf[0].mem)
#define sf_cap   (fe_sf[0].cap)
#define sf_size  (fe_sf[0].size)
#define sf_state (fe_sf[0].state)
#define gs_name  (fe_sf[1].name)
#define gs_state (fe_sf[1].state)
static void sf_load_slot(int slot, int quiet);
#define sf_load(q) sf_load_slot(0, (q))
static int is_sf2(const char *nm);
static int sf_presets(int slot);
static int gs_slot(void) { return gs_name[0] && strcmp(gs_name, sf_name) ? 1 : 0; }   /* GS 기계가 쓰는 칸 */
static void midi_synth_apply(void);
static void midi_bus_from_ui(void);                   /* 설정(출력 방식·음원) → 단자에 꽂힌 기계 목록 */
static void midi_ui_from_bus(void);                   /* 기계 목록 → 설정 변수 (ini 읽은 뒤) */
static int ini_mididev_seen;
static void mt_load(int slot, int quiet); static int mt_slot_of(int di); static int mt_dev_n(int slot);                       /* 5.0.180 */
static int sc55_state;                                  /* SC-55 실기 롬 (USB\MidiSound\SC55): 0 아직, 1 켬, -1 파일 없음, -2 크기 틀림, -3 켜기 실패, -9 이 판(레거시)에는 없음 */
static char sc55_info[40];
static void sc55_load(int quiet);
/* MIDI 음원 파일 폴더 = USB\MidiSound\{SoundFont, MT32, SC55} (마스터님 — 맨 위 SoundFont·MT32 폴더는 없어짐) */
enum { MD_SF, MD_MT32, MD_SC55 };
static const char *const midi_dirs[3] = { "0:\\MidiSound\\SoundFont", "0:\\MidiSound\\MT32", "0:\\MidiSound\\SC55" };
#define midi_dir(k) (midi_dirs[(k)])
static void midi_path(char *out, int max, int k, const char *name);
static const char *nt(const char *ko, const char *en, const char *ja);   /* fe_net.inc */
static int midi_in_dev = -1;                        /* 지난 읽기: -1 장치 없음, 0 이상 있음 */
static char midi_in_name[32];
static void midi_flush(void);
static void silence_all(void);
static void midi_frame(uint32_t now);
static void tape_flush(void);
static void tape_eject(void);
/* 하드 디스크 (2.8): USB의 이미지 파일을 섹터 단위로 (Sunrise IDE 카트리지에 연결) */
static char hd_path[FE_PATH_MAX], hd_name[64];
static uint32_t hd_secs, hd_last_ms, hd_write_ms;
static char hd2_path[FE_PATH_MAX], hd2_name[64];              /* 3.4.3 DEV-15: IDE 슬레이브 */
static uint32_t hd2_secs;
static int hd_dirty;
static char hd_dir[FE_PATH_MAX] = FE_HDD, hd_dir_disp[FE_PATH_MAX] = FE_DISP_STORE " > HARDDISK", hd_sel[FE_KEY_MAX];
static int hd_top = -1;
static void hd_flush(void);
static int browse_full = 1;                 /* 디스크 인터페이스 64/65, 0 = 일반 롬팩 */

static const char *T(StrId id)
{
    if (lang >= 3 && lang <= 9) {                       /* 추가 언어: 번역이 없으면 영어 */
        for (unsigned i = 0; i < sizeof lang2_rows / sizeof lang2_rows[0]; i++)
            if (lang2_rows[i].id == id) return lang2_rows[i].t[lang - 3];
        return lang_en[id];
    }
    if (lang == 10) {                                   /* 3.4.3 아랍어: 미리 이어 쓰고 뒤집어 둔 문구 (tools/mkarabic.py), 없으면 영어 */
        for (unsigned i = 0; i < sizeof lang_ar / sizeof lang_ar[0]; i++)
            if (lang_ar[i].id == id) return lang_ar[i].text;
        return lang_en[id];
    }
    const char *const *t = lang == 1 ? lang_en : lang == 2 ? lang_ja : lang_ko;
    return t[id];
}

/* ------------------------------------------------------------------ */
/* 그리기                                                               */
/* ------------------------------------------------------------------ */
static inline uint32_t px32(uint8_t c) { return pal32[c & 15]; }
static uint32_t snap32[VDP_PAL_SNAPS][16];                  /* 4.0.29: 줄마다 팔레트 (프레임 안에서 바뀐 것) */
static int plmode, plmode_prev;
/* 화면 장식(메뉴, LED 판, 대화 창) 색: MSX 팔레트와 무관한 고정 색 (8비트 화면은 DAC 240~255) */
static uint32_t ui32[64];
static uint8_t ui8[64];                     /* 8비트 화면에서 쓸 DAC 번호 (16~31은 SCREEN 8 색 상자에서 가장 가까운 색) */
static const uint8_t ui_rgb[63][3] = {
    {128, 128, 128}, {  0,   0,   0}, { 36, 219,  36}, {109, 255, 109},   /* 0: 어두운 회색 (2.16 대화 상자 그림자·흐린 글자) */
    { 36,  36, 255}, { 73, 109, 255}, {182,  36,  36}, { 73, 219, 255},
    {255,  36,  36}, {255, 109, 109}, {219, 219,  36}, {219, 219, 146},
    { 36, 146,  36}, {219,  73, 182}, {182, 182, 182}, {255, 255, 255},
    /* 2.16 새 화면 디자인 (팝업·메뉴 공통, fe_ui.inc): 16 바탕, 17 카드, 18 머리·줄무늬, 19 고른 줄, 20 선, 21 글자, 22 흐린 글자,
     * 23 강조, 24 강조(진함), 25 좋음, 26 주의, 27 위험, 28 입력 칸, 29 그림자, 30 키 칩, 31 꺼진 글자 */
    { 11,  15,  23}, { 22,  28,  41}, { 30,  38,  56}, { 36,  52,  84}, { 46,  58,  82}, {230, 234, 242}, {136, 146, 166},
    { 76, 141, 255}, { 42,  91, 191}, { 52, 211, 153}, {251, 191,  36}, {248, 113, 113}, { 15,  20,  32}, {  5,   7,  11},
    { 42,  51,  70}, { 82,  93, 115},
    /* 3.5.0 떠 있는 막대 (fe_ov.inc): 32 막대 바탕, 33 가리킴, 34 선, 35 꺼짐, 36 이름 글자, 37 키 칩, 38 카드, 39 팝업 바탕,
     * 40 MSX 파랑, 41 밝은 파랑, 42 흰색, 43 전원 빨강, 44 띠 빨강, 45 띠 노랑, 46 띠 초록, 47 띠 파랑 */
    { 75,  83,  97}, { 97, 106, 121}, {122, 131, 146}, {167, 174, 187}, {230, 233, 239}, { 38,  42,  50}, { 60,  67,  80}, { 29,  34,  48},
    { 61, 123, 255}, {127, 176, 255}, {255, 255, 255}, {255, 122, 122}, {229,  72,  77}, {245, 196,   0}, { 47, 179, 107}, { 61, 123, 255},
    /* 5.2.188 미디 시안: 48 막대 바탕(어두운), 49 정상 알약 바탕, 50 정상 알약 테두리 */
    { 11,  15,  25}, { 16,  40,  31}, { 44, 107,  80},
    /* 5.2.189 미디 랙 시안: 51 장비 앞판, 52 앞판 윗선, 53 랙 귀, 54 움푹한 칸, 55 LCD 바탕, 56 LCD 글자, 57 LCD 작은 글자, 58 손잡이 테, 59 손잡이 안, 60 나사, 61 금속 단추, 62 번호 바탕 */
    { 35,  40,  50}, { 66,  72,  86}, { 27,  31,  40}, { 10,  13,  20}, { 10,  30,  22}, {140, 255, 192}, { 79, 191, 138}, { 74,  80,  93}, { 46,  52,  64}, {140, 148, 165}, { 58,  64,  76}, { 13,  16,  23},
};
#define UI_NCOL 63
#define UI_DAC 240
static inline uint32_t uipx(uint8_t c) { return ui32[c < UI_NCOL ? c : c & 31]; }
static inline uint8_t ui8c(uint8_t c) { return ui8[c < UI_NCOL ? c : c & 31]; }
#define UIC(c) ((c) < UI_NCOL ? (c) : (c) & 31)

/* 그린 영역 (Blt 갱신용). 3.5.5: 하나로 합친 줄 범위 대신 사각형 여러 개 (위·아래가 같이 바뀌어도 화면 전체를 보내지 않게) */
#define DIRTY_MAX 8
static FeRect dirty_r[DIRTY_MAX];
static int dirty_n;
static inline uint64_t rect_area(const FeRect *r) { return (uint64_t)(r->x1 - r->x0) * (r->y1 - r->y0); }
static void mark_dirty_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!w || !h || x >= D.w || y >= D.h) return;
    FeRect n = { x, y, x + w > D.w ? D.w : x + w, y + h > D.h ? D.h : y + h };
    for (;;) {                                          /* 겹치거나 아주 가까운 것과는 합침 (합친 뒤 다시 확인) */
        int hit = -1;
        for (int i = 0; i < dirty_n && hit < 0; i++) {
            const FeRect *r = &dirty_r[i];
            if (n.x0 <= r->x1 + 16 && r->x0 <= n.x1 + 16 && n.y0 <= r->y1 + 4 && r->y0 <= n.y1 + 4) hit = i;
        }
        if (hit < 0) break;
        FeRect *r = &dirty_r[hit];
        if (r->x0 < n.x0) n.x0 = r->x0;
        if (r->y0 < n.y0) n.y0 = r->y0;
        if (r->x1 > n.x1) n.x1 = r->x1;
        if (r->y1 > n.y1) n.y1 = r->y1;
        dirty_r[hit] = dirty_r[--dirty_n];
    }
    if (dirty_n < DIRTY_MAX) { dirty_r[dirty_n++] = n; return; }
    int best = 0; uint64_t bc = ~0ull;                  /* 가득: 늘어나는 넓이가 가장 작은 것과 합침 */
    for (int i = 0; i < dirty_n; i++) {
        FeRect u = dirty_r[i];
        if (n.x0 < u.x0) u.x0 = n.x0;
        if (n.y0 < u.y0) u.y0 = n.y0;
        if (n.x1 > u.x1) u.x1 = n.x1;
        if (n.y1 > u.y1) u.y1 = n.y1;
        uint64_t c = rect_area(&u) - rect_area(&dirty_r[i]);
        if (c < bc) { bc = c; best = i; }
    }
    FeRect u = dirty_r[best];
    dirty_r[best] = dirty_r[--dirty_n];
    if (n.x0 > u.x0) n.x0 = u.x0;
    if (n.y0 > u.y0) n.y0 = u.y0;
    if (n.x1 < u.x1) n.x1 = u.x1;
    if (n.y1 < u.y1) n.y1 = u.y1;
    mark_dirty_rect(n.x0, n.y0, n.x1 - n.x0, n.y1 - n.y0);
}
static inline void mark_dirty(uint32_t y, uint32_t h) { mark_dirty_rect(0, y, D.w, h); }
/* 진단 창(2)·소리 진단 창(7)은 0.5초마다 카드 전체를 다시 그린다. 실기(Acer 노트북)에서 화면으로 매번 통째로 복사하면
 * 검은 줄이 아래에서 위로 올라가며 깜빡였다. 그래서 이 두 창에서는 줄 내용(해시)이 지난번 화면으로 보낸 것과 같은 줄은 보내지 않는다.
 * 창을 막 열었거나 강제로 다시 그리는 프레임(force_redraw)이면 기록을 지우고 전부 보낸다 (펌웨어가 화면을 지웠을 때 안전). */
static int dialog;
static uint64_t dh_row[4320];
static int dh_ok, dh_dlg;
static uint64_t dh_hash(const uint8_t *p, uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    const uint64_t *q = (const uint64_t *)p;
    for (uint32_t i = 0; i < n / 8; i++) { h ^= q[i]; h *= 1099511628211ull; h ^= h >> 29; }
    return h;
}
static int dirty_filter(FeRect *tmp, int tmax)
{
    int on = (dialog == 2 || dialog == 7) && D.bpp == 4 && D.h <= 4320 && !force_redraw && !(D.pitch & 7) && D.w * 4 <= D.pitch;
    if (!on || dh_dlg != dialog) { dh_ok = 0; dh_dlg = on ? dialog : 0; }
    if (!on) return -1;
    int n = 0;
    for (int i = 0; i < dirty_n; i++) {
        const FeRect *r = &dirty_r[i];
        int run = -1;
        for (uint32_t y = r->y0; y <= r->y1 && y <= D.h; y++) {
            int diff = 0;
            if (y < r->y1 && y < D.h) {
                uint64_t h = dh_hash(D.fb + (uintptr_t)y * D.pitch, D.w * 4 & ~7u);
                diff = !dh_ok || h != dh_row[y];
                dh_row[y] = h;
            }
            if (diff && run < 0) run = (int)y;
            else if (!diff && run >= 0) {
                if (n < tmax) { tmp[n].x0 = r->x0; tmp[n].x1 = r->x1; tmp[n].y0 = (uint32_t)run; tmp[n].y1 = y; n++; }
                else { tmp[n - 1].y1 = y; }                 /* 넘치면 마지막과 합침 */
                run = -1;
            }
        }
    }
    dh_ok = 1;
    return n;
}
/* 뒤 버퍼가 없는 BIOS(CSM) 부팅은 화면 메모리에 바로 그린다. 진단 창(2)·소리 진단 창(7)이 0.5초마다 카드를 지웠다 다시 그리면
 * 그 과정이 화면에 그대로 보여 검은 줄이 아래에서 위로 깜빡이며 올라갔다 (5번 Acer 노트북, 사진·영상 확인). 그래서 이 두 창은
 * 처음 한 번만 화면에 바로 그리고 그 모습을 임시 메모리(P.scratch 16 MiB 뒤)에 복사해 두고, 그 뒤 다시 그리기는 임시 메모리에 그린 다음
 * 내용이 바뀐 줄만 화면으로 복사한다 (줄 해시 비교). 창이 바뀌거나 강제로 다시 그릴 때는 처음부터. */
static void draw_dialog(void);
static void fe_trace(const char *what);                /* 5.2.24 멈춤 추적 (fe_sys.inc) */
static void fe_trace_str(const char *what, const char *x);
static uint64_t sh_hash[4320];
static int sh_dlg;
/* 화면 메모리에 바로 그리는 부팅(BIOS/CSM)은 메뉴·팝업·창을 지웠다 다시 그리는 과정이 화면에 그대로 보여 검은 줄이 올라갔다.
 * 그래서 한 프레임 전체를 뒤 버퍼(P.back)에 그린 다음, 프레임 끝에서 내용이 바뀐 줄만 화면으로 복사한다 (줄 해시 비교).
 * 지웠다 그리는 중간 모습은 화면에 안 나감. 뒤 버퍼가 있는 쪽(P.flush)은 이미 그래서 쓰지 않음. */
static uint64_t dbl_hash[4320];
static uint8_t *dbl_lfb;
static int dbl_state, dbl_on;                           /* state: 0 아직, 1 사용, -1 안 씀 */
static uint64_t seg_hash(const uint8_t *p, uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t q; memcpy(&q, p + i, 8); h ^= q; h *= 1099511628211ull; h ^= h >> 29; }
    for (; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
static void dbl_begin(void)
{
    if (dbl_state < 0) return;
    uint32_t need = D.pitch * D.h;
    uint8_t *b = P.back;
    if (dbl_state == 0) {
        if (P.flush || !D.fb || !b || (D.bpp != 1 && D.bpp != 4) || D.h > 4320 || D.w * (uint32_t)D.bpp > D.pitch || P.back_size < need) { dbl_state = -1; return; }
        memcpy(b, D.fb, need);                          /* 지금 화면을 한 번 복사 (화면 메모리는 읽기가 느려 이때뿐) */
        for (uint32_t y = 0; y < D.h; y++) dbl_hash[y] = seg_hash(b + (uintptr_t)y * D.pitch, D.w * (uint32_t)D.bpp);
        dbl_lfb = D.fb; dbl_state = 1;
    }
    D.fb = b; dbl_on = 1;
}
static void dbl_sync(void)                              /* 바뀐 줄만 화면으로 (프레임 끝, 오래 걸리는 일 앞) */
{
    if (!dbl_on) return;
    uint32_t n = D.w * (uint32_t)D.bpp;
    for (uint32_t y = 0; y < D.h; y++) {
        const uint8_t *src = D.fb + (uintptr_t)y * D.pitch;
        uint64_t h = seg_hash(src, n);
        if (h == dbl_hash[y]) continue;
        dbl_hash[y] = h;
        memcpy(dbl_lfb + (uintptr_t)y * D.pitch, src, n);
    }
}
static void dbl_end(void) { if (!dbl_on) return; if (P.vsync_wait) P.vsync_wait(); dbl_sync(); D.fb = dbl_lfb; dbl_on = 0; }   /* 새 화면 시작에 복사 (찢김·몰림 없게) */
static void fe_flush_now(void) { if (P.flush) P.flush(); else dbl_sync(); }
static void draw_dialog_direct(void)
{
    fe_trace("ddd begin");
    uint32_t need = D.pitch * D.h;
    int ok = (dialog == 2 || dialog == 7) && !P.flush && !dbl_on && D.bpp == 4 && P.scratch && P.scratch_size >= (16u << 20) + need && need <= (16u << 20) && D.h <= 4320 && !(D.pitch & 7);
    if (!ok) { fe_trace("ddd direct"); sh_dlg = 0; draw_dialog(); return; }
    uint8_t *sh = P.scratch + (16u << 20), *lfb = D.fb;
    if (sh_dlg == dialog && !force_redraw) {
        fe_trace("ddd shadow");
        D.fb = sh; draw_dialog(); D.fb = lfb;
        fe_trace("ddd copy");
        for (uint32_t y = 0; y < D.h; y++) {
            uint64_t h = dh_hash(sh + (uintptr_t)y * D.pitch, D.w * 4 & ~7u);
            if (h == sh_hash[y]) continue;
            sh_hash[y] = h;
            memcpy(lfb + (uintptr_t)y * D.pitch, sh + (uintptr_t)y * D.pitch, D.w * 4);
        }
        return;
    }
    fe_trace("ddd first");
    draw_dialog();                                      /* 처음·강제: 화면에 바로 */
    memcpy(sh, lfb, need);                              /* 그 모습을 복사 (화면 메모리는 읽기가 느려 한 번만) */
    for (uint32_t y = 0; y < D.h; y++) sh_hash[y] = dh_hash(sh + (uintptr_t)y * D.pitch, D.w * 4 & ~7u);
    sh_dlg = dialog;
}
int fe_take_dirty_rects(FeRect *out, int max)
{
    FeRect ft[48];
    int fn = dirty_filter(ft, 48);
    if (fn >= 0) { for (int i = 0; i < fn && i < DIRTY_MAX; i++) dirty_r[i] = ft[i]; dirty_n = fn > DIRTY_MAX ? DIRTY_MAX : fn;   /* 8개만 복사 (전에는 fn 개 모두 복사해 dirty_r 뒤의 dirty_n·S·F·D(화면 주소·크기)를 덮어씀 → 화면 주소가 엉뚱한(UC·없는) 메모리로 바뀌어 수십~수백 초 멈춤, LG gram) */
        if (fn > DIRTY_MAX) for (int i = DIRTY_MAX; i < fn; i++) { FeRect *o = &dirty_r[DIRTY_MAX - 1]; if (ft[i].x0 < o->x0) o->x0 = ft[i].x0; if (ft[i].y0 < o->y0) o->y0 = ft[i].y0; if (ft[i].x1 > o->x1) o->x1 = ft[i].x1; if (ft[i].y1 > o->y1) o->y1 = ft[i].y1; } }
    int n = dirty_n < max ? dirty_n : max;
    for (int i = 0; i < n; i++) out[i] = dirty_r[i];
    if (dirty_n > max) {                                /* 받는 쪽이 작으면 나머지를 마지막에 합침 */
        for (int i = max; i < dirty_n; i++) {
            FeRect *o = &out[max - 1]; const FeRect *r = &dirty_r[i];
            if (r->x0 < o->x0) o->x0 = r->x0;
            if (r->y0 < o->y0) o->y0 = r->y0;
            if (r->x1 > o->x1) o->x1 = r->x1;
            if (r->y1 > o->y1) o->y1 = r->y1;
        }
    }
    dirty_n = 0;
    return n;
}
void fe_take_dirty(uint32_t *y0, uint32_t *y1)
{
    FeRect r[DIRTY_MAX];
    int n = fe_take_dirty_rects(r, DIRTY_MAX);
    *y0 = 0; *y1 = 0;
    if (!n) return;
    *y0 = r[0].y0; *y1 = r[0].y1;
    for (int i = 1; i < n; i++) { if (r[i].y0 < *y0) *y0 = r[i].y0; if (r[i].y1 > *y1) *y1 = r[i].y1; }
}
static void fill_any(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t c8, uint32_t c32)
{
    if (!D.fb || x >= D.w || y >= D.h) return;
    if (x + w > D.w) w = D.w - x;
    if (y + h > D.h) h = D.h - y;
    mark_dirty_rect(x, y, w, h);
    for (uint32_t r = 0; r < h; r++) {
        uint8_t *row = D.fb + (uintptr_t)(y + r) * D.pitch;
        if (D.bpp == 1) memset(row + x, c8, w);
        else {
            uint32_t *p = (uint32_t *)row + x;
            for (uint32_t i = 0; i < w; i++) p[i] = c32;
        }
    }
}

/* 장식 색으로 칠하기 */
void fe_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t c)
{
    fill_any(x, y, w, h, ui8c(c), uipx(c));
}

/* 2.17: RGB로 칠하기 (부드러운 아이콘·배지). 8비트 화면은 SCREEN 8 색 상자에서 가까운 색 */
static void fe_fill_rgb(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t c32 = D.bgr ? (r << 16) | (g << 8) | b : (b << 16) | (g << 8) | r;
    uint8_t c8 = (uint8_t)(16 + ((r * 5 + 127) / 255) * 35 + ((g * 6 + 127) / 255) * 5 + (b * 4 + 127) / 255);
    fill_any(x, y, w, h, c8, c32);
}

/* MSX 팔레트 색으로 칠하기 (테두리) */
static void fill_msx(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t c)
{
    fill_any(x, y, w, h, (uint8_t)(c & 15), px32(c));
}

static void recompute_palette(void)
{
    for (int i = 0; i < UI_NCOL; i++) {
        uint32_t r = ui_rgb[i][0], g = ui_rgb[i][1], b = ui_rgb[i][2];
        ui32[i] = D.bgr ? (r << 16) | (g << 8) | b : (b << 16) | (g << 8) | r;
        ui8[i] = i < 16 ? (uint8_t)(UI_DAC + i) : (uint8_t)(16 + ((r * 5 + 127) / 255) * 35 + ((g * 6 + 127) / 255) * 5 + (b * 4 + 127) / 255);
    }
    for (int i = 0; i < 16; i++) {
        uint8_t r8 = M->vdp.palette[i][0], g8 = M->vdp.palette[i][1], b8 = M->vdp.palette[i][2];
        monitor_tint(&r8, &g8, &b8);
        mon_rgb[i][0] = r8; mon_rgb[i][1] = g8; mon_rgb[i][2] = b8;
        uint32_t r = r8, g = g8, b = b8;
        pal32[i] = D.bgr ? (r << 16) | (g << 8) | b : (b << 16) | (g << 8) | r;
    }
    for (int c = 0; c < 256 && D.fb; c++) {             /* SCREEN 8 색에도 모니터 종류 */
        uint8_t r, g, b;
        vdp_g7_rgb(&M->vdp, (uint8_t)c, &r, &g, &b);
        monitor_tint(&r, &g, &b);
        g7_32[c] = D.bgr ? ((uint32_t)r << 16) | ((uint32_t)g << 8) | b : ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
    }
}

uint32_t utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((uint32_t)(p[0] & 63) << 12) | ((uint32_t)(p[1] & 63) << 6) | (p[2] & 63); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((uint32_t)(p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

const FontGlyph *font_glyph(const UiFont *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* 부드러운 글꼴 한 점: a = 진하기 0..15. bg==0xFF 면 화면에 이미 있는 색 위에 섞는다 (8비트 화면은 반 넘으면 켬) */
static inline void put_aa(uint8_t *line, uint32_t px, int a, uint8_t fg, uint8_t bg)
{
    if (a <= 0) { if (bg != 0xFF) { if (D.bpp == 1) line[px] = ui8c(bg); else ((uint32_t *)line)[px] = uipx(bg); } return; }
    if (a >= 15 || D.bpp == 1) {
        uint8_t col = a >= (D.bpp == 1 ? 8 : 15) ? fg : bg;
        if (col == 0xFF) return;
        if (D.bpp == 1) line[px] = ui8c(col); else ((uint32_t *)line)[px] = uipx(col);
        return;
    }
    const uint8_t *cf = ui_rgb[UIC(fg)];
    uint32_t r, g, b;
    if (bg != 0xFF) { const uint8_t *cb = ui_rgb[UIC(bg)]; r = cb[0]; g = cb[1]; b = cb[2]; }
    else {
        uint32_t o = ((uint32_t *)line)[px];
        if (D.bgr) { r = (o >> 16) & 255; g = (o >> 8) & 255; b = o & 255; } else { b = (o >> 16) & 255; g = (o >> 8) & 255; r = o & 255; }
    }
    r = (r * (uint32_t)(15 - a) + cf[0] * (uint32_t)a) / 15; g = (g * (uint32_t)(15 - a) + cf[1] * (uint32_t)a) / 15; b = (b * (uint32_t)(15 - a) + cf[2] * (uint32_t)a) / 15;
    ((uint32_t *)line)[px] = D.bgr ? (r << 16) | (g << 8) | b : (b << 16) | (g << 8) | r;
}

/* 글자 하나: 배경까지 칠한다. 그린 폭 반환 */
static uint32_t draw_glyph(const UiFont *f, uint32_t s, uint32_t x, uint32_t y, uint32_t cp, uint8_t fg, uint8_t bg, int draw)
{
    const FontGlyph *g = font_glyph(f, cp);
    if (!g && f == &font_large && font_glyph(&font_small, cp)) {     /* 큰 글꼴에 없는 글자(키릴 등): 작은 글꼴로, 기준선 맞춤 */
        const FontGlyph *sg = font_glyph(&font_small, cp);
        uint32_t w2 = sg->adv * s;
        if (draw) {
            uint32_t dy = (uint32_t)(font_large.ascent - font_small.ascent) * s;
            if (bg != 0xFF) fe_fill(x, y, w2, (uint32_t)font_large.height * s, bg);
            draw_glyph(&font_small, s, x, y + dy, cp, fg, 0xFF, 1);
        }
        return w2;
    }
    if (!g) g = font_glyph(f, '?');
    if (!g) return 0;
    uint32_t w = g->adv * s;
    if (!draw) return w;
    if (x + w > D.w || y + (uint32_t)f->height * s > D.h) return w;
    const uint8_t *b = f->bits + g->off;
    mark_dirty_rect(x, y, w, (uint32_t)f->height * s);
    for (int r = 0; r < f->height; r++) {
        for (uint32_t sy = 0; sy < s; sy++) {
            uint8_t *line = D.fb + (uintptr_t)(y + r * s + sy) * D.pitch;
            for (int c = 0; c < g->adv; c++) {
                int a = glyph_a(f, g, b, r, c);
                if (!a && bg == 0xFF) continue;                     /* 배경 투명 */
                for (uint32_t sx = 0; sx < s; sx++) put_aa(line, x + c * s + sx, a, fg, bg);
            }
        }
    }
    return w;
}

/* 문자열 (최대 폭 max_w 픽셀, 0이면 제한 없음). 그린 폭 반환 */
static uint32_t text_f(const UiFont *f, uint32_t s, uint32_t x, uint32_t y, const char *str, uint8_t fg, uint8_t bg, uint32_t max_w, int draw)
{
    uint32_t w = 0;
    while (*str) {
        const char *save = str;
        uint32_t cp = utf8_next(&str);
        uint32_t gw = draw_glyph(f, s, 0, 0, cp, 0, 0, 0);
        if (max_w && w + gw > max_w) { (void)save; break; }
        draw_glyph(f, s, x + w, y, cp, fg, bg, draw);
        w += gw;
    }
    return w;
}

/* 3.7.2: 폭 max_w에 맞게 가로로 줄여 그림 (넘치지 않으면 그대로). 배율 1, 배경 투명. 그린 폭 */
static uint32_t text_fit(const UiFont *f, uint32_t x, uint32_t y, const char *str, uint8_t fg, uint32_t max_w)
{
    uint32_t W = text_f(f, 1, 0, 0, str, 0, 0, 0, 0);
    if (!W) return 0;
    if (W <= max_w || !max_w) return text_f(f, 1, x, y, str, fg, 0xFF, 0, 1);
    if (x + max_w > D.w || y + (uint32_t)f->height > D.h) return max_w;
    mark_dirty_rect(x, y, max_w, (uint32_t)f->height);
    uint32_t gc = 0;
    while (*str) {
        uint32_t cp = utf8_next(&str);
        const FontGlyph *g = font_glyph(f, cp);
        if (!g) g = font_glyph(f, '?');
        if (!g) continue;
        const uint8_t *b = f->bits + g->off;
        for (int c = 0; c < g->adv; c++, gc++) {
            uint32_t px = x + gc * max_w / W;
            for (int r = 0; r < f->height; r++) {
                int a = glyph_a(f, g, b, r, c);
                if (!a) continue;
                put_aa(D.fb + (uintptr_t)(y + (uint32_t)r) * D.pitch, px, a, fg, 0xFF);
            }
        }
    }
    return max_w;
}

uint32_t fe_text(uint32_t x, uint32_t y, const char *s, uint8_t fg, uint8_t bg) { return text_f(F, S, x, y, s, fg, bg, 0, 1); }
static uint32_t text_w(const char *s) { return text_f(F, S, 0, 0, s, 0, 0, 0, 0); }
static uint32_t text_clip(uint32_t x, uint32_t y, const char *s, uint8_t fg, uint8_t bg, uint32_t max_w) { return text_f(F, S, x, y, s, fg, bg, max_w, 1); }
static uint32_t small_ell(uint32_t x, uint32_t y, const char *s, uint8_t fg, uint8_t bg, uint32_t max_w);
static void str_append(char *d, int max, const char *s);
/* 작은 글씨로 폭 max_w 안에. 넘치면 들어가는 만큼 + "..." (2.9). 그린 폭 반환 */
static uint32_t small_ell(uint32_t x, uint32_t y, const char *s, uint8_t fg, uint8_t bg, uint32_t max_w)
{
    uint32_t w = text_f(SF, 1, 0, 0, s, 0, 0, 0, 0);
    if (lang == 10 && max_w) {                          /* 3.4.4 아랍어: 칸 오른쪽에 맞추고, 넘치면 왼쪽(문장 끝)을 "..."로 */
        if (w <= max_w) return text_f(SF, 1, x + max_w - w, y, s, fg, bg, 0, 1);
        uint32_t dw = text_f(SF, 1, 0, 0, "...", 0, 0, 0, 0);
        if (dw > max_w) return 0;
        while (*s && w + dw > max_w) { const char *q = s; uint32_t cp = utf8_next(&q); w -= draw_glyph(SF, 1, 0, 0, cp, 0, 0, 0); s = q; }
        text_f(SF, 1, x + max_w - w - dw, y, "...", fg, bg, 0, 1);
        return dw + text_f(SF, 1, x + max_w - w, y, s, fg, bg, 0, 1);
    }
    if (w <= max_w) return text_f(SF, 1, x, y, s, fg, bg, 0, 1);
    uint32_t dw = text_f(SF, 1, 0, 0, "...", 0, 0, 0, 0);
    if (dw > max_w) return 0;
    char t[160]; int n = 0;
    const char *q = s;
    while (*q && n < (int)sizeof t - 8) {               /* 글자 단위로 늘려 보며 "..."까지 들어가는 데까지 */
        const char *st = q;
        utf8_next(&q);
        int k = (int)(q - st);
        if (n + k >= (int)sizeof t - 4) break;
        for (int i = 0; i < k; i++) t[n + i] = st[i];
        t[n + k] = 0;
        if (text_f(SF, 1, 0, 0, t, 0, 0, 0, 0) + dw > max_w) { t[n] = 0; break; }
        n += k;
    }
    t[n] = 0;
    str_append(t, sizeof t, "...");
    return text_f(SF, 1, x, y, t, fg, bg, 0, 1);
}

/* 3.4.7: 보통 글씨로 폭 max_w 안에, 넘치면 "..." (긴 파일 이름) */
static uint32_t text_ell(uint32_t x, uint32_t y, const char *s, uint8_t fg, uint8_t bg, uint32_t max_w)
{
    if (lang == 10 || text_f(F, S, 0, 0, s, 0, 0, 0, 0) <= max_w) return text_clip(x, y, s, fg, bg, max_w);
    uint32_t dw = text_f(F, S, 0, 0, "...", 0, 0, 0, 0);
    if (dw > max_w) return 0;
    static char t[FE_KEY_MAX + 8];
    int n = 0;
    uint32_t w = 0;
    const char *q = s;
    while (*q) {                                        /* 글자마다 폭을 더해 가며 (매번 전체를 재지 않음) */
        const char *st = q;
        utf8_next(&q);
        int k = (int)(q - st);
        if (n + k >= (int)sizeof t - 4) break;
        char one[8];
        for (int i = 0; i < k; i++) one[i] = st[i];
        one[k] = 0;
        uint32_t cw = text_f(F, S, 0, 0, one, 0, 0, 0, 0);
        if (w + cw + dw > max_w) break;
        for (int i = 0; i < k; i++) t[n + i] = st[i];
        n += k; w += cw;
    }
    t[n] = 0;
    str_append(t, sizeof t, "...");
    return text_f(F, S, x, y, t, fg, bg, 0, 1);
}

/* 3.4.7: 잘릴 때 UTF-8 글자 중간에서 끊지 않음 (긴 한글 이름) */
static int utf8_cut(const char *d, int i, const char *rest)
{
    if (rest && ((uint8_t)*rest & 0xC0) == 0x80) {
        while (i > 0 && ((uint8_t)d[i - 1] & 0xC0) == 0x80) i--;
        if (i > 0) i--;
    }
    return i;
}
static void copy_str(char *d, int n, const char *s)
{
    int i = 0;
    for (; s && s[i] && i < n - 1; i++) d[i] = s[i];
    if (s && s[i]) i = utf8_cut(d, i, s + i);
    d[i] = 0;
}
static void str_append(char *d, int max, const char *s)
{
    int n = (int)strlen(d), n0 = n;
    while (*s && n < max - 1) d[n++] = *s++;
    if (*s) { n = utf8_cut(d, n, s); if (n < n0) n = n0; }
    d[n] = 0;
}
static void num_append(char *d, int max, uint32_t v, int width, char pad)
{
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char r[16]; int k = 0;
    while (width-- > n) r[k++] = pad;
    while (n) r[k++] = t[--n];
    r[k] = 0;
    str_append(d, max, r);
}

/* 3.4.4 LANG3-01: 아랍어는 오른쪽 → 왼쪽 배치 (창 머리·탭·구역·줄·명령 줄·메뉴를 좌우로 뒤집음. 파일 목록 같은 자료는 그대로) */
#define RTL (lang == 10)
/* 오른쪽 끝 xr에 맞춰 그림. max_w를 넘치면 왼쪽(아랍어에서는 문장 끝)부터 잘라 냄. 그린 폭 반환 */
static uint32_t text_rf(const UiFont *f, uint32_t s, uint32_t xr, uint32_t y, const char *str, uint8_t fg, uint32_t max_w)
{
    uint32_t w = text_f(f, s, 0, 0, str, 0, 0, 0, 0);
    while (max_w && w > max_w && *str) { const char *q = str; uint32_t cp = utf8_next(&q); w -= draw_glyph(f, s, 0, 0, cp, 0, 0, 0); str = q; }
    return text_f(f, s, xr > w ? xr - w : 0, y, str, fg, 0xFF, 0, 1);
}
static uint32_t text_r(uint32_t xr, uint32_t y, const char *s, uint8_t fg, uint32_t max_w) { return text_rf(F, S, xr, y, s, fg, max_w); }
#include "fe_ui.inc"

/* ------------------------------------------------------------------ */
/* 알림 (LED 판 아래)                                                   */
/* ------------------------------------------------------------------ */
static int lower_c(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int menu_dirty;
static int bar_visible;                     /* 메뉴 막대가 보임 */
static uint8_t mods;                        /* 수정 키 (0x04 = 왼쪽 Ctrl) */
static char toast_msg[96];
static uint32_t toast_until;

static void vid_snd_follow(int i);
static void toast(const char *a, const char *b)
{
    toast_msg[0] = 0;
    str_append(toast_msg, sizeof toast_msg, a);
    if (b) { str_append(toast_msg, sizeof toast_msg, " "); str_append(toast_msg, sizeof toast_msg, b); }
    toast_until = P.ms() + 2000;
    margin_bd = -1;
}


/* ------------------------------------------------------------------ */
/* 상태 표시 (설계서 §7.7, 2.2.1 개편)                                  */
/*   왼쪽 위: 기종 세대 배지, CAPS KANA / 오른쪽 위: FDD1 FDD2 CAS HD SLOT1 SLOT2 */
/*   왼쪽 아래: 파나소닉 스위치. 배경 상자 없이 테두리 색 위에 그린다.    */
/* ------------------------------------------------------------------ */
enum { LED_CAPS, LED_KANA, LED_FDD1, LED_FDD2, LED_CAS, LED_HD, LED_SLOT1, LED_SLOT2, LED_SLOT3, LED_SLOT4, LED_SLOT5,
       LED_PFRONT, LED_PPAUSE, LED_PAUDIO, LED_COUNT };
static const char *const led_name[LED_COUNT] = { "CAPS", "KANA", "FDD1", "FDD2", "CAS", "HD", "SLOT1", "SLOT2", "SLOT3", "SLOT4", "SLOT5",
                                                  "FRONT", "PAUSE", "AUDIO" };
static const uint8_t led_icon[LED_COUNT] = { ICON_CAPS, ICON_KANA, ICON_FDD, ICON_FDD, ICON_CAS, ICON_HD,
                                              ICON_CART, ICON_CART, ICON_CART, ICON_CART, ICON_CART, ICON_SWITCH, ICON_SWITCH, ICON_SWITCH };
static uint8_t led_now[LED_COUNT], led_drawn[LED_COUNT];
static int strstr_ci(const char *h, const char *n);
static int pana_has(int i);
static const char *led_label(int i);
static int rec_on;                                  /* 소리 저장 중 (2.10) */
static int vrec_on;                                 /* 3.2.13 영상 녹화 중 */
static uint32_t vrec_start;
static void vrec_toggle(void);
static void vrec_stop(void);
static void vrec_frame(void);
static void vrec_audio(const int16_t *s, int n);
static int pend_act;                                /* 2.10.1: 키를 모두 놓은 뒤 할 오래 걸리는 동작 */
static uint32_t pend_since;
static char pend_path[FE_PATH_MAX];
#define A_LOAD_PATH 900                             /* pend_path 상태 파일 불러오기 */
static uint32_t rec_n, rec_start;
static void settings_changed(void);
static int tape_slot(void);                         /* 5.2.36 */
static int fw_switch;                               /* 4.0.1 앞 선언 */
static int pana_front, pana_pause;                  /* 파나소닉 스위치 (MSX-AUDIO는 특수 카트리지로) */

static uint8_t ms_st[MS_N], ms_alt[MS_N];         /* 장치가 선언한 상태 (src/status.c) */
static void read_leds(void)
{
    /* 0 꺼짐, 1·2 노랑(켜짐·꽂힘), 3 초록(접근 중 깜박임), 4 회색(이 기종에 없음) */
    msx_status(M, ms_st, ms_alt);
    int blink = ((M->frame >> 2) & 1) ? 3 : 2;
    led_now[LED_CAPS] = ms_st[MS_CAPS] != SS_OFF;
    led_now[LED_KANA] = ms_st[MS_KANA] != SS_OFF;
    int nd = disk_drives();                                         /* 가상 디스크 카트리지가 A·B 를 맡으면 2 */
    for (int d = 0; d < 2; d++) {
        uint8_t v = ms_st[MS_FDD1 + d];
        if (d >= nd) v = 4;
        else if (v == SS_BUSY && M->fdc.motor) v = (uint8_t)blink;
        else v = disk_size[d] ? 2 : 0;
        led_now[LED_FDD1 + d] = v;
    }
    led_now[LED_CAS] = (uint8_t)(ms_st[MS_CAS] == SS_BUSY ? blink : ms_st[MS_CAS]);   /* 있음 노랑, 모터 깜박임, 단자 없음 회색 */
    led_now[LED_HD] = (uint8_t)(!hd_secs && !hd2_secs ? ms_st[MS_HD] : P.ms() - hd_last_ms < 200 ? blink : 2);   /* 이미지를 넣었으면 노랑, 읽고 쓰면 깜박임 */
    for (int k = 0; k < 5; k++) led_now[LED_SLOT1 + k] = ms_st[MS_SLOT1 + k] == SS_NONE && k < 4 ? 0 : ms_st[MS_SLOT1 + k];   /* 1~4 는 예전처럼 0 */
    led_now[LED_PFRONT] = (uint8_t)(fw_switch && pana_has(LED_PFRONT) ? 2 : 0);
    led_now[LED_PPAUSE] = (uint8_t)(pana_pause && pana_has(LED_PPAUSE) ? 2 : 0);
    M->pause_sw = pana_pause;                                       /* turbo R 일시 정지 키 (포트 A7h) */
    led_now[LED_PAUDIO] = 0;
}

/* 3.0 아이콘·배지 그림: USB의 Icons 폴더에서 읽어 쓰고, 없으면 본체에 든 그림.
 *   Icons\이름.png  — 아이콘 (이름 = icon_name[], 64x64까지, 투명 배경). 색 그림은 그대로 그리고 꺼진 상태는 흐리게,
 *                     흑백(검정·회색) 그림은 2.x처럼 모양으로 보고 상태 색으로 칠한다. Icons\이름.txt(16x16 '#')도 받음
 *   Icons\BADGE_<MSX|MSX2|MSX2+|turboR>.png — 기종 세대 배지 (512x96까지, 투명 배경)
 * 이미지에는 본체와 같은 그림을 Icons에 넣어 두므로, 사용자가 그 파일을 바꾸면 그 그림이 나온다. */
#define ICO_MAX 64
static uint8_t icon_px[ICON_COUNT][ICO_MAX * ICO_MAX * 4];      /* RGBA */
static uint8_t icon_w[ICON_COUNT], icon_h[ICON_COUNT], icon_kind[ICON_COUNT];   /* 0 본체 그림, 1 모양(알파만), 2 색 그림 */

static int load_png_rgba(const char *path, uint8_t *out, uint32_t maxw, uint32_t maxh, uint32_t *w, uint32_t *h, int *mono)
{
    static uint8_t buf[131072];
    uint32_t n = 0;
    if (!P.read_file || P.read_file(path, buf, sizeof buf, &n) || !n || n > sizeof buf) return -1;
    if (png_load(buf, n, w, h) || *w > maxw || *h > maxh || !*w || !*h) return -1;
    int alpha = png_has_alpha(), m = 1;
    for (uint32_t y = 0; y < *h; y++)
        for (uint32_t x = 0; x < *w; x++) {
            int c[4];
            png_rgba(x, y, c);
            if (!alpha) { c[3] = 255; }
            uint8_t *d = &out[(y * *w + x) * 4];
            d[0] = (uint8_t)c[0]; d[1] = (uint8_t)c[1]; d[2] = (uint8_t)c[2]; d[3] = (uint8_t)c[3];
            if (c[3] >= 32) {                                       /* 무채색이고 어두우면 모양 그림 */
                int mx = c[0] > c[1] ? (c[0] > c[2] ? c[0] : c[2]) : (c[1] > c[2] ? c[1] : c[2]);
                int mn = c[0] < c[1] ? (c[0] < c[2] ? c[0] : c[2]) : (c[1] < c[2] ? c[1] : c[2]);
                if (mx - mn > 24 || mx > 150) m = 0;
            }
        }
    if (!alpha && m)                                                /* 투명도 없는 흑백: 어두운 점이 그림 */
        for (uint32_t i = 0; i < *w * *h; i++) { uint8_t *d = &out[i * 4]; d[3] = (uint8_t)(255 - d[0]); }
    *mono = m;
    return 0;
}

static void icons_load(void)
{
    static uint8_t buf[1024];
    for (int i = 0; i < ICON_COUNT; i++) {
        char path[64] = "0:\\Icons\\";
        str_append(path, sizeof path, icon_name[i]);
        char pp[64];
        copy_str(pp, sizeof pp, path);
        str_append(pp, sizeof pp, ".png");
        uint32_t w, h, n = 0;
        int mono;
        icon_kind[i] = 0;
        if (!load_png_rgba(pp, icon_px[i], ICO_MAX, ICO_MAX, &w, &h, &mono)) {
            icon_w[i] = (uint8_t)w; icon_h[i] = (uint8_t)h; icon_kind[i] = mono ? 1 : 2;
            continue;
        }
        str_append(path, sizeof path, ".txt");                     /* 16줄 x 16글자, '#' = 그림 */
        if (!P.read_file || P.read_file(path, buf, sizeof buf - 1, &n) || !n) continue;
        int row = 0, col = 0;
        memset(icon_px[i], 0, 16 * 16 * 4);
        for (uint32_t k = 0; k < n && row < 16; k++) {
            char c = (char)buf[k];
            if (c == '\n') { if (col) { row++; col = 0; } continue; }
            if (c == '\r') continue;
            if (col < 16) { if (c == '#') icon_px[i][(row * 16 + col) * 4 + 3] = 255; col++; }
        }
        if (col && row < 16) row++;
        if (row == 16) { icon_w[i] = icon_h[i] = 16; icon_kind[i] = 1; }
    }
}

#define BADGE_PW 512
#define BADGE_PH 96      /* 4.0.15: 128 → 96 (CPU 아이콘 둘 추가로 늘어난 BSS를 줄임. 기본 배지는 높이 64) */
static uint8_t badge_png[BADGE_GENS][BADGE_PW * BADGE_PH * 4];
static uint16_t badge_pw[BADGE_GENS], badge_ph[BADGE_GENS];
static const char *const badge_file[BADGE_GENS] = { "MSX", "MSX2", "MSX2+", "turboR" };

static void badges_load(void)
{
    for (int g = 0; g < BADGE_GENS; g++) {
        char path[64] = "0:\\Icons\\BADGE_";
        str_append(path, sizeof path, badge_file[g]);
        str_append(path, sizeof path, ".png");
        uint32_t w, h;
        int mono;
        badge_pw[g] = 0;
        if (!load_png_rgba(path, badge_png[g], BADGE_PW, BADGE_PH, &w, &h, &mono)) { badge_pw[g] = (uint16_t)w; badge_ph[g] = (uint16_t)h; }
    }
}

/* RGBA 그림(sw x sh)을 dw x dh로 줄여(면적 평균) bg 위에 섞어 그림. mono면 알파를 모양으로 보고 col 색, 아니면 그림 색.
 * dim: 0 그대로, 1 반쯤 흐리게, 2 많이 흐리게 (꺼진·없는 상태) */
static void draw_rgba_scaled(uint32_t x, uint32_t y, uint32_t dw, uint32_t dh, const uint8_t *px, uint32_t sw, uint32_t sh,
                             int mono, uint8_t col, uint8_t bg, int dim)
{
    const uint8_t *cb = ui_rgb[UIC(bg)], *cc = ui_rgb[UIC(col)];
    for (uint32_t oy = 0; oy < dh; oy++) {
        uint32_t sy0 = oy * sh / dh, sy1 = (oy + 1) * sh / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        uint32_t run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t ox = 0; ox <= dw; ox++) {
            uint32_t colr = 0xFFFFFFFFu;
            if (ox < dw) {
                uint32_t sx0 = ox * sw / dw, sx1 = (ox + 1) * sw / dw, t[4] = { 0, 0, 0, 0 }, n = 0;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                for (uint32_t sy = sy0; sy < sy1; sy++)
                    for (uint32_t sx = sx0; sx < sx1; sx++, n++) {
                        const uint8_t *q = &px[(sy * sw + sx) * 4];
                        t[0] += q[0] * q[3]; t[1] += q[1] * q[3]; t[2] += q[2] * q[3]; t[3] += q[3];
                    }
                uint32_t a = t[3] / n, c[3];                            /* a = 평균 알파 0~255 */
                if (dim) a = a * (dim == 1 ? 55 : 30) / 100;
                for (int ch = 0; ch < 3; ch++) {
                    uint32_t fg = mono ? cc[ch] : (t[3] ? t[ch] / t[3] : 0);
                    c[ch] = (cb[ch] * (255 - a) + fg * a) / 255;
                }
                colr = c[0] << 16 | c[1] << 8 | c[2];
            }
            if (colr != run_c) {
                if (run_c != 0xFFFFFFFFu && ox > run_x) fe_fill_rgb(x + run_x, y + oy, ox - run_x, 1, run_c >> 16, (run_c >> 8) & 255, run_c & 255);
                run_c = colr; run_x = ox;
            }
        }
    }
}

/* 아이콘 sz x sz. main = 상태 색(C_TEXT 켜짐, C_MUTED 꺼짐, C_FAINT 없음), acc = 강조 색, bg = 그 자리 바탕 */
static void draw_icon_hd(uint32_t x, uint32_t y, uint32_t sz, int icon, uint8_t main, uint8_t acc, uint8_t bg)
{
    if (icon < 0 || icon >= ICON_COUNT || !sz) return;
    if (icon_kind[icon]) {                                          /* USB Icons 폴더의 그림 */
        int dim = main == C_FAINT ? 2 : main == C_MUTED ? 1 : 0;
        draw_rgba_scaled(x, y, sz, sz, icon_px[icon], icon_w[icon], icon_h[icon], icon_kind[icon] == 1, main, bg, icon_kind[icon] == 1 ? 0 : dim);
        return;
    }
    const uint8_t *cm = ui_rgb[UIC(main)], *ca = ui_rgb[UIC(acc)], *cb = ui_rgb[UIC(bg)];
    const uint8_t *L0 = icon_hd[icon][0], *L1 = icon_hd[icon][1];
    for (uint32_t oy = 0; oy < sz; oy++) {
        uint32_t sy0 = oy * 48 / sz, sy1 = (oy + 1) * 48 / sz;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        uint32_t run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t ox = 0; ox <= sz; ox++) {
            uint32_t col = 0xFFFFFFFFu;
            if (ox < sz) {
                uint32_t sx0 = ox * 48 / sz, sx1 = (ox + 1) * 48 / sz, am = 0, aa = 0, n = 0;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                for (uint32_t sy = sy0; sy < sy1; sy++)
                    for (uint32_t sx = sx0; sx < sx1; sx++) {
                        uint32_t i = sy * 48 + sx, sh = (i & 1) * 4;
                        am += (L0[i >> 1] >> sh) & 15; aa += (L1[i >> 1] >> sh) & 15; n++;
                    }
                uint32_t d = n * 15, c[3];
                for (int ch = 0; ch < 3; ch++) {
                    uint32_t v = (cb[ch] * (d - am) + cm[ch] * am) / d;
                    c[ch] = (v * (d - aa) + ca[ch] * aa) / d;
                }
                col = c[0] << 16 | c[1] << 8 | c[2];
            }
            if (col != run_c) {
                if (run_c != 0xFFFFFFFFu && ox > run_x) fe_fill_rgb(x + run_x, y + oy, ox - run_x, 1, run_c >> 16, (run_c >> 8) & 255, run_c & 255);
                run_c = col; run_x = ox;
            }
        }
    }
}

/* 2.17 상태 칩: 테두리 색과 상관없이 잘 보이게 어두운 알약 카드 + 부드러운 아이콘 + 이름 + 오른쪽 불
 * 상태: 0 꺼짐(비어 있음), 1·2 켜짐·꽂힘(노랑), 3 쓰는 중(초록), 4 이 기종에 없음(흐림), 5 녹음(빨강) */
static uint32_t chip_h(void) { return LH + 8 * S; }
static uint32_t chip_row(void) { return chip_h() + 5 * S; }
static uint32_t chip_isz(void) { return LH; }
/* 3.0: 칩 폭을 2/3로 (오른쪽 불 대신 테두리 색이 상태). 이름 없는 칩은 아이콘만 */
static uint32_t chip_w_text(const char *t) { return 6 * S + chip_isz() + (t && t[0] ? 5 * S + ui_sw(t) + 2 * S : 0) + 6 * S; }
static void draw_chip(uint32_t x, uint32_t y, uint32_t w, int icon, const char *label, int st)
{
    uint32_t h = chip_h(), r = 7 * S, isz = chip_isz();
    uint8_t im = st == 4 ? C_FAINT : st == 0 ? C_MUTED : C_TEXT;
    uint8_t ia = st == 4 || st == 0 ? C_FAINT : st == 3 ? C_OK : st == 5 ? C_BAD : C_WARN;
    uint8_t lc = st == 4 ? C_FAINT : st == 5 ? C_BAD : C_WARN;      /* 3.0: 이름은 노란 글씨 */
    ui_rrect(x + 2 * S, y + 3 * S, w, h, r, C_SHADOW);
    ui_rframe(x, y, w, h, r, (st == 1 || st == 2) ? C_WARN : st == 3 ? C_OK : st == 5 ? C_BAD : C_LINE, C_CARD);
    int has_text = label && label[0];
    uint32_t ix = has_text ? x + 6 * S : x + (w - isz) / 2;
    if (icon >= 0) draw_icon_hd(ix, y + (h - isz) / 2, isz, icon, im, ia, C_CARD);
    if (!has_text) return;
    uint32_t tx = icon >= 0 ? ix + isz + 5 * S : x + 8 * S, end = x + w - 5 * S;
    if (end > tx) small_ell(tx, ui_sty(y, h), label, lc, 0xFF, end - tx);
}
static uint32_t status_item_w(int i);

/* 한 항목: 아이콘 + 이름. 켜지거나 꽂히면 아이콘이 노란색 */
/* 일본 기종이면 KANA, 그 밖(한국·영어권 등)은 "한글" */
static int machine_japanese(void)
{
    static const char *const jp[] = { "Japan", "Panasonic", "Sony", "National", "Sanyo", "Toshiba", "Canon", "Casio", "Yamaha",
                                      "Victor", "JVC", "Mitsubishi", "Fujitsu", "Hitachi", "Pioneer", "Kyocera", "turbo", 0 };
    for (int k = 0; jp[k]; k++) if (strstr_ci(machine_name, jp[k])) return 1;
    return 0;
}
static const char *led_label(int i) { return (i == LED_KANA && !machine_japanese()) ? "\xED\x95\x9C\xEA\xB8\x80" : led_name[i]; }   /* 한글 */
static int icon_of_name(const char *n) { for (int i = 0; n && i < ICON_COUNT; i++) if (!strcmp(icon_name[i], n)) return i; return -1; }
static int ms_icon(int k, int alt)                      /* 장치가 선언한 아이콘 이름 → 그림 번호 */
{
    static int16_t cache[MS_N][2];
    static uint8_t done;
    if (!done) { for (int j = 0; j < MS_N; j++) { cache[j][0] = (int16_t)icon_of_name(msx_status_decl[j].icon); cache[j][1] = (int16_t)icon_of_name(msx_status_decl[j].icon_alt); } done = 1; }
    int r = cache[k][alt && cache[k][1] >= 0];
    return r >= 0 ? r : ICON_OTHER;
}
static const uint8_t led_ms[LED_COUNT] = { MS_CAPS, MS_KANA, MS_FDD1, MS_FDD2, MS_CAS, MS_HD, MS_SLOT1, MS_SLOT2, MS_SLOT3, MS_SLOT4, MS_SLOT5, MS_N, MS_N, MS_N };
static int led_icon_of(int i) { return led_ms[i] < MS_N ? ms_icon(led_ms[i], i == LED_KANA && !machine_japanese()) : led_icon[i]; }

static uint32_t status_item_w(int i) { return chip_w_text(led_label(i)); }
static void draw_status_item(uint32_t x, uint32_t y, uint32_t w, int i)
{
    draw_chip(x, y, w, led_icon_of(i), led_label(i), led_now[i]);
    led_drawn[i] = led_now[i];
}

static int strstr_ci(const char *h, const char *n)
{
    for (; *h; h++) {
        int i = 0;
        while (n[i] && lower_c(h[i]) == lower_c(n[i])) i++;
        if (!n[i]) return 1;
    }
    return 0;
}

/* 상태 아이콘 표시 규칙 (2.7.6, 요구 15 개정)
 *  - 메뉴·대화 창(단축키 팝업 포함)이 열렸거나 일시 정지·정지 중이면: 이 기종에 있는 장치·음원 아이콘 (3.2.2: 전에는 모두)
 *  - CAPS·한글은 메뉴와 상관없이 불이 켜졌을 때만, F6(3.2.2)로 아이콘 모두 숨기기/보이기
 *  - 롬팩 게임 실행 중(일반 롬 카트리지가 꽂힘): 아무것도 안 보임
 *  - 그 밖: CAPS·KANA/한글은 불이 켜졌을 때, 디스크·테이프·HD·롬팩은 넣었을 때,
 *          음원은 소리를 내는 동안(+1초). 파나소닉 스위치는 그 스위치가 있는 기종에서 켰을 때만(메뉴를 열어도 그대로) */
static int status_show_all(void);
static int status_rom_game(void);
static uint8_t chip_eff;                                /* 3.2.2: 표시용 음원 활동 (PSG 짧은 소리 거름) */
static int icons_off;                                   /* 3.2.2: F6로 아이콘을 모두 숨김 */
static int icons_always;                                /* 3초 제한 없이 계속 보이기 (F6: 보이기 → 계속 보이기 → 숨기기) */
static int led_vis(int i)
{
    if (icons_off) return 0;
    if (i == LED_PFRONT || i == LED_PPAUSE) return led_now[i] != 0 && !status_rom_game();   /* 켰을 때만 */
    if (i == LED_CAPS || i == LED_KANA) return led_now[i] != 0 && (status_show_all() || !status_rom_game());   /* 3.2.2: 메뉴와 상관없이 켜졌을 때만 */
    if (status_show_all()) return led_now[i] != 4;      /* 3.2.2: 메뉴를 열면 이 기종에 있는(인식된) 장치만 */
    if (status_rom_game()) return 0;
    return led_now[i] != 0 && led_now[i] != 4;
}

/* ---- 소리 채널 (5.2.14: 본체 소리 채널 표 msx_ch_info 가 차례·이름·기본 음량·좌우를 정함) -------------------------------
 * 믹서 채널 번호 c: 0 ~ MSX_CH_N-1 = 본체 채널 (번호 그대로), 그 뒤 = 프런트엔드만의 채널 (내장 MIDI·USB MIDI, 마스터).
 * 속성 ▶ 사운드 목록·음원 아이콘·믹서 띠가 모두 이 차례. 새 칩은 본체 표에 한 줄이면 여기 고칠 것 없음 (아이콘은 모르면 일반 음원 그림) */
#define MIX_PSG       MSX_CH_PSG
#define MIX_PSG2      MSX_CH_PSG2
#define MIX_SCC       MSX_CH_SCC
#define MIX_MM        MSX_CH_MM
#define MIX_MA        MSX_CH_MA
#define MIX_PCM       MSX_CH_PCM
#define MIX_MS        MSX_CH_MS
#define MIX_OPP       MSX_CH_SFG
#define MIX_MIDI_SOFT MSX_CH_MIDI
#define MIX_KBD       MSX_CH_KEY
#define MIX_IO        MSX_CH_IO
enum { MIX_MIDI_INT = MSX_CH_N, MIX_MASTER, MIX_N };
#define MIX_SCC2 50   /* SCC 카트리지가 둘일 때 둘째 SCC 의 상태 아이콘 전용 번호 (믹서 채널은 SCC 하나, 배열 첨자로 쓰지 말 것) */   /* 마스터는 늘 맨 끝 (설정 파일 채널 줄은 마스터 앞까지) */
/* 소리 믹서의 MIDI 는 띠 하나 = 단자에 꽂힌 MIDI 기계 전체의 마스터 (소프트웨어 음원 합 + USB MIDI 장치 세기). 기계별 음량·좌우·끄기·받는 채널은 MIDI 믹서 창 */
#define MIX_MIDI_USB  MIX_MIDI_SOFT                     /* 예전 USB MIDI 띠 = 같은 MIDI 띠 */
/* 설정 파일 이름 = 본체 표의 key (3.6.x 이름과 같음: SFG → OPP, 예전 MIDI = SOFT MIDI) */
static const char *mix_key_of(int c) { return c < MSX_CH_N ? msx_ch_info[c].key : c == MIX_MIDI_INT ? "MIDIINT" : "MASTER"; }
static int mix_key_is(const char *k, int c) { return !strcmp(k, mix_key_of(c)) || (c == MIX_OPP && !strcmp(k, "SFG")); }   /* 3.6.x "SFG" = OPP */
static int mix_vol[MIX_N], mix_mute[MIX_N];             /* 처음 값은 mix_defaults (본체 표의 기본 음량, 프런트엔드 기본) */
static int mix_pan[MIX_N];                              /* 3.3.3 SND3-01 좌우 (−100~100) */
static int mix_meter[MIX_N], scc1_meter, scc2_meter;   /* SCC 둘일 때 칩별 계기 */                            /* 3.3.3 SND3-02 계기 (0~1000, 천천히 내려감) */
static uint32_t snd_evt[MIX_N], scc2_evt;                         /* 채널별 마지막 소리 시각 (음원 아이콘) */
static int usb_midi_ok;                                 /* USB MIDI 출력 장치가 꽂혀 있음 (1초마다 확인) */
static int usb_midi_vol = 100;                          /* 믹서 USB MIDI (보내는 Note On 세기 비율) */
static uint32_t usb_midi_sent;                          /* 3.7.0: USB MIDI로 보낸 바이트 수 (계기·아이콘) */
static void mix_defaults(void)                          /* 프런트엔드 기본: 칩 100, KBD·I-O 60(I-O 끔), 내장·USB MIDI 100, 마스터 70 */
{
    for (int c = 0; c < MIX_N; c++) { mix_vol[c] = 100; mix_mute[c] = 0; }
    mix_vol[MIX_KBD] = 60; mix_vol[MIX_IO] = 60; mix_mute[MIX_IO] = 1;   /* 3.4.3: 드라이브 소리는 처음에 끔 */
    mix_vol[MIX_MASTER] = 70;
}
static int mix_icon(int c)                              /* 아이콘 (모르는 새 칩 = 일반 음원 그림) */
{
    switch (c) {
    case MIX_SCC2: return ICON_SCC;
    case MIX_PSG: return ICON_PSG; case MIX_PSG2: return ICON_PSG2; case MIX_SCC: return ICON_SCC; case MIX_MM: return ICON_MM;
    case MIX_MA: return ICON_MA; case MIX_PCM: return ICON_PCM; case MIX_MS: return ICON_MS; case MIX_OPP: return ICON_OPP;
    case MIX_MIDI_SOFT: return midibus_soft() < 0 && midibus_find(MDK_USB) ? ICON_USBMIDI : ICON_SOFTMIDI;   /* USB 기계만 꽂혔으면 USB 그림 */
    case MIX_MIDI_INT: return ICON_MIDI;
    case MIX_KBD: return ICON_KEYBOARD; case MIX_IO: return ICON_FDD; case MIX_MASTER: return ICON_MIXER;
    default: return ICON_ETC;
    }
}
static int snd_is_src(int c) { return c == MIX_MIDI_INT || (c >= 0 && c < MSX_CH_N && c != MIX_KBD && c != MIX_IO); }   /* 음원 (아이콘·속성 ▶ 사운드 목록) */
/* 띠·아이콘 이름 */
static const char *snd_name(int c)
{
    if (c >= 0 && c < MSX_CH_N) return msx_ch_info[c].name;
    return c == MIX_MIDI_INT ? T(S_SND_MIDI_INT) : "MASTER";
}
/* 긴 이름 = 속성 ▶ 사운드, 믹서 설명 줄 */
static const char *snd_full(int c)
{
    if (c == MIX_IO) return T(S_SND_DRIVE);
    if (c == MIX_MIDI_SOFT) return T(S_MIDI_BAND);      /* 단자에 꽂힌 MIDI 기계 전체 */
    if (c >= 0 && c < MSX_CH_N) return msx_ch_info[c].full;
    return c == MIX_MIDI_INT ? T(S_MIDI_INT) : "MASTER";
}
static int midi_src(void) { return msx_dev(M, MDEV_MIDI) || msx_dev(M, MDEV_MIDI_HW) || M->sfg_on; }   /* GT 는 단자가 있어 내장 MIDI 를 꺼도 기계가 꽂히면 MIDI 채널 */   /* MIDI를 내보내는 장치 (내장 MSX-MIDI·SFG) */
static int midi_out;
/* 지금 실제로 동작하는 채널 (속성·아이콘에서 흐리지 않음) — 본체 채널은 본체가 앎 (msx_ch_present) */
static int snd_present(int c)
{
    if (c == MIX_SCC2) return msx_scc_count(M) >= 2;   /* 5.2.157 */
    if (c == MIX_MIDI_SOFT) return midi_src() && (midibus_dev_soft_on() || (midibus_find(MDK_USB) && usb_midi_ok));   /* 꽂힌 기계(소프트웨어 음원·USB)가 있을 때 — 5.2.64: MIDI 믹서의 모든 기계 (GT 단자 기계도) */
    if (c >= 0 && c < MSX_CH_N) return msx_ch_present(M, c);
    if (c == MIX_MIDI_INT) return msx_dev(M, MDEV_MIDI_HW) && midi_int;   /* 외장 카트리지 때문에 prof.midi 가 켜져도 GTMIDI 는 내장 설정만 따름 */   /* GT 내장 MSX-MIDI (켰을 때). 5.2.63: X86MSX MIDI·외장 MSX-MIDI 카트리지는 MIDI 띠 */
    return c == MIX_MASTER;
}
/* 이번 프레임 소리를 냈는가 (아이콘 노란불). PSG 는 짧은 소리를 거른 chip_eff */
static int snd_active(int c)
{
    if (c == MIX_SCC2) return M->scc2_act;   /* 5.2.157 */
    if (c == MIX_PSG) return chip_eff & 1;
    return c >= 0 && c < MSX_CH_N && msx_ch_active(M, c);
}
/* 5.2.11 믹서에 보이는 채널: 고정(본체 표 fixed: PSG·KBD·I-O)과 마스터는 늘, 칩 채널은 있고 + 쓰인 것만 */
static uint32_t mix_used;
static int mix_test_all;                                /* 시험: 모든 음원 채널을 보임 (fehost FEACT=mixall) */
static int mix_shown(int c)
{
    if (mix_test_all && (snd_is_src(c) || c == MIX_MASTER || (c >= 0 && c < MSX_CH_N))) return 1;
    if (c == MIX_MASTER || (c >= 0 && c < MSX_CH_N && msx_ch_info[c].fixed)) return 1;
    if (c == MIX_MIDI_INT) return snd_present(c) && !mix_mute[c];   /* 사운드 탭 "GT 내장 미디" 끄면 믹서에서도 빠짐 (마스터님) */
    if (c == MIX_MIDI_SOFT) return snd_present(c);   /* GT 내장 MIDI 는 켜져 있으면 늘. 5.2.67: MIDI 채널도 기계가 꽂혀 있으면 늘 (소리 나기 전에도) */
    return snd_present(c) && (mix_used >> c & 1);
}
static int snd_hold(int c) { return c == MIX_MIDI_INT || c == MIX_MIDI_SOFT ? 3000 : 1000; }   /* 3.6.5 MIDI는 긴 음이라 3초 */
static int chip_on(int c) { uint32_t ev = c == MIX_SCC2 ? scc2_evt : snd_evt[c]; return snd_active(c) || (ev && P.ms() - ev < (uint32_t)snd_hold(c)); }   /* 3.6.5: 노란불 */
static int chip_vis(int c)                              /* 3.0.2: 믹서·사운드에서 끈 음원은 아이콘을 보이지 않음 */
{
    if (mix_mute[c == MIX_SCC2 ? MIX_SCC : c] || icons_off) return 0;
    if (status_show_all()) return 1;
    return chip_on(c);                                  /* 3.7.2: 소리가 날 때만 보임 (멈추면 잠시 뒤 사라짐) */
}
/* 음원 아이콘 목록 (차례대로) */
static int snd_icon_list(int *out)
{
    int n = 0;
    for (int c = 0; c < MIX_N; c++) {
        if (snd_is_src(c) && snd_present(c) && chip_vis(c)) out[n++] = c;
        if (c == MIX_SCC && snd_present(MIX_SCC2) && chip_vis(MIX_SCC2)) out[n++] = MIX_SCC2;   /* 둘째 SCC 아이콘 */
    }
    return n;
}
/* 4.0.34: 음원 아이콘 글자 (고정). 칸보다 넓으면 글자 크기를 줄여 그림 (snd_label_draw) */
static const char *snd_icon_label(int c, uint32_t w)
{
    (void)w;
    if (c == MIX_SCC2) return "SCC2";                    /* 5.2.157 */
    if (c == MIX_SCC && msx_scc_count(M) >= 2) return "SCC1";
    if (c == MIX_MS) return "Moons";
    if (c >= 0 && c < MSX_CH_N) return msx_ch_info[c].short_name;
    return c == MIX_MIDI_INT ? "GTMIDI" : snd_name(c);
}
/* 글자를 폭 max_w에 맞게 작게 (작은 글꼴을 면적 평균으로 줄임). 오른쪽·아래 맞춤: 오른쪽 끝 xr, 아래 끝 yb */
static void snd_label_draw(uint32_t xr, uint32_t yb, const char *s, uint8_t fg, uint8_t bg, uint32_t max_w)
{
    enum { BW = 160, BH = 24 };
    static uint8_t m[BH][BW];
    uint32_t W = 0, H = (uint32_t)SF->height;
    if (H > BH) H = BH;
    memset(m, 0, sizeof m);
    for (const char *p = s; *p; ) {                      /* 원래 크기로 비트 그림 */
        uint32_t cp = utf8_next(&p);
        const FontGlyph *g = font_glyph(SF, cp);
        if (!g) g = font_glyph(SF, '?');
        if (!g || W + g->adv > BW) break;
        const uint8_t *b = SF->bits + g->off;
        for (uint32_t r = 0; r < H; r++) for (int cx = 0; cx < g->adv; cx++) m[r][W + (uint32_t)cx] = (uint8_t)glyph_a(SF, g, b, (int)r, cx);
        W += g->adv;
    }
    if (!W) return;
    uint32_t dw = W, dh = H;
    if (dw > max_w) { dw = max_w; dh = H * max_w / W; }     /* 가로·세로 같은 비율 */
    if (dh < 6) dh = 6;
    if (dw < 4) return;
    uint32_t x0 = xr > dw ? xr - dw : 0, y0 = yb > dh ? yb - dh : 0;
    const uint8_t *cf = ui_rgb[UIC(fg)], *cb = ui_rgb[UIC(bg)];
    for (uint32_t oy = 0; oy < dh; oy++) {
        uint32_t sy0 = oy * H / dh, sy1 = (oy + 1) * H / dh; if (sy1 <= sy0) sy1 = sy0 + 1;
        for (uint32_t ox = 0; ox < dw; ox++) {
            uint32_t sx0 = ox * W / dw, sx1 = (ox + 1) * W / dw; if (sx1 <= sx0) sx1 = sx0 + 1;
            uint32_t on = 0, n = 0;
            for (uint32_t sy = sy0; sy < sy1 && sy < BH; sy++) for (uint32_t sx = sx0; sx < sx1 && sx < BW; sx++) { on += m[sy][sx]; n++; }
            if (!on) continue;
            uint32_t a = on * 255 / (n * 15); if (a < 90) a = 90;     /* 줄여도 가는 획이 안 사라지게 */
            fe_fill_rgb(x0 + ox, y0 + oy, 1, 1, (cb[0] * (255 - a) + cf[0] * a) / 255, (cb[1] * (255 - a) + cf[1] * a) / 255, (cb[2] * (255 - a) + cf[2] * a) / 255);
        }
    }
}

/* 기종 세대 (배지): 0 MSX, 1 MSX2, 2 MSX2+, 3 turbo R */
static int mach_gen_of(const char *n);
static int machine_gen(void)
{
    if ((machine_name[0] | 32) == 'm' && (machine_name[1] | 32) == 's' && (machine_name[2] | 32) == 'x') return mach_gen_of(machine_name);   /* 2.11: 기종 이름 앞의 세대 */
    if (msx_dev(M, MDEV_S1990) || strstr_ci(machine_name, "turboR") || strstr_ci(machine_name, "turbo R")) return 3;
    if (M->vdp.v9958) return 2;
    if (M->vdp.v9938) return 1;
    return 0;
}
static int badge_vis(void) { return !icons_off && (status_show_all() || !status_rom_game()); }
/* 2.17 배지: 높이 48줄 그림 하나를 원하는 높이로 줄여 카드 색 위에 그린다.
 * badge_fit: 높이 th 이하, 폭 w 이하로 맞춘 크기 (*bw = 폭). 못 그리면 0 */
static uint32_t badge_fit(uint32_t w, uint32_t th, uint32_t *bw)
{
    int g = machine_gen();
    uint32_t W = badge_pw[g] ? badge_pw[g] : (uint32_t)badges[0][g].w, H = badge_pw[g] ? badge_ph[g] : (uint32_t)badges[0][g].h;
    uint32_t h = th > H ? H : th;
    if (W * h / H > w) h = w * H / W;
    if (h < 10) return 0;
    *bw = W * h / H;
    return h;
}
static void draw_badge_at(uint32_t x, uint32_t y, uint32_t bw, uint32_t bh)
{
    int g = machine_gen();
    if (badge_pw[g]) { draw_rgba_scaled(x, y, bw, bh, badge_png[g], badge_pw[g], badge_ph[g], 0, 0, C_CARD, 0); return; }   /* USB Icons 폴더의 그림 */
    static uint8_t src[48 * 256 * 3];
    static int src_g = -1;
    const Badge *b = &badges[0][g];
    uint32_t W = (uint32_t)b->w, H = (uint32_t)b->h;
    if (W > 256 || H > 48 || !bw || !bh) return;
    if (src_g != g) {                                   /* 줄마다 [개수 R G B] 풀기 */
        const uint8_t *p = b->rle;
        for (uint32_t r = 0; r < H; r++)
            for (uint32_t c = 0; c < W; ) {
                uint32_t n = *p++;
                for (uint32_t i = 0; i < n && c < W; i++, c++) { uint8_t *d = &src[(r * W + c) * 3]; d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; }
                p += 3;
            }
        src_g = g;
    }
    for (uint32_t oy = 0; oy < bh; oy++) {
        uint32_t sy0 = oy * H / bh, sy1 = (oy + 1) * H / bh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        uint32_t run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t ox = 0; ox <= bw; ox++) {
            uint32_t col = 0xFFFFFFFFu;
            if (ox < bw) {
                uint32_t sx0 = ox * W / bw, sx1 = (ox + 1) * W / bw, t[3] = { 0, 0, 0 }, n = 0;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                for (uint32_t sy = sy0; sy < sy1; sy++)
                    for (uint32_t sx = sx0; sx < sx1; sx++, n++) { const uint8_t *q = &src[(sy * W + sx) * 3]; t[0] += q[0]; t[1] += q[1]; t[2] += q[2]; }
                col = (t[0] / n) << 16 | (t[1] / n) << 8 | (t[2] / n);
            }
            if (col != run_c) {
                if (run_c != 0xFFFFFFFFu && ox > run_x) fe_fill_rgb(x + run_x, y + oy, ox - run_x, 1, run_c >> 16, (run_c >> 8) & 255, run_c & 255);
                run_c = col; run_x = ox;
            }
        }
    }
}
/* 파나소닉 스위치가 실제로 있는 기종인가: 프런트(펌웨어) 스위치 = 파나소닉 MSX2+·turbo R, 일시 정지 = turbo R */
static int pana_has(int i)
{
    if (!strstr_ci(machine_name, "Panasonic")) return 0;
    if (i == LED_PPAUSE) return msx_dev(M, MDEV_TRPAUSE);
    return machine_gen() >= 2;
}

/* 왼쪽·오른쪽 여백을 줄마다 그 줄의 테두리 색으로 (실제 TV처럼 테두리도 줄별 색) */
static void fill_side_margins(uint8_t bd)
{
    uint32_t rx = x0 + out_w, rw = D.w > rx ? D.w - rx : 0;
    if (run_state == RUN_STOPPED || !M) { fill_msx(0, 0, x0, D.h, bd); if (rw) fill_msx(rx, 0, rw, D.h, bd); return; }
    uint8_t first = M->vdp.line_bd[0], last = M->vdp.line_bd[disp_lines ? disp_lines - 1 : 0];
    uint32_t top = row_y[voff], bot = row_y[voff + disp_lines];
    fill_msx(0, 0, x0, top, first); if (rw) fill_msx(rx, 0, rw, top, first);
    uint32_t y = 0;
    while (y < disp_lines) {                            /* 같은 색 줄은 한 번에 */
        uint32_t e = y + 1;
        while (e < disp_lines && M->vdp.line_bd[e] == M->vdp.line_bd[y]) e++;
        uint32_t ya = row_y[y + voff], yb = row_y[e + voff];
        fill_msx(0, ya, x0, yb - ya, M->vdp.line_bd[y]);
        if (rw) fill_msx(rx, ya, rw, yb - ya, M->vdp.line_bd[y]);
        y = e;
    }
    if (bot < D.h) { fill_msx(0, bot, x0, D.h - bot, last); if (rw) fill_msx(rx, bot, rw, D.h - bot, last); }
}

/* 3.2.15 VID-09: 여백이 60점보다 좁을 때 — 이름 없이 작은 아이콘 칩만 (여백이 16점보다 좁으면 그리지 않음).
 * 왼쪽 아래 CAPS·한글, 오른쪽 위 드라이브·슬롯, 오른쪽 아래 음원. 기종 카드·REC 칩은 뺌 */
/* 3.7.1: 음원 아이콘 LED 계기 (믹서와 같은 초록 → 노랑 → 빨강). 아이콘을 그릴 때 자리를 적어 두고 매 프레임 계기만 다시 그림 */
static int live_mixer(void);
static int live_ui(void);                               /* 5.2.69 */
#define SNDM_MAX 24
static struct { uint32_t x, y, w, h; int c, segs, lit; } sndm[SNDM_MAX];
static int sndm_n;
static void sndm_draw(int i, int force)
{
    int c = sndm[i].c, segs = sndm[i].segs;
    int mc = c == MIX_SCC2 ? MIX_SCC : c, mv = c == MIX_SCC2 ? scc2_meter : (c == MIX_SCC && msx_scc_count(M) >= 2) ? scc1_meter : mix_meter[c];
    int lit = (mix_mute[mc] || run_state != RUN_ON) ? 0 : (mv * segs + 999) / 1000;
    if (lit > segs) lit = segs;
    if (!force && lit == sndm[i].lit) return;
    sndm[i].lit = lit;
    uint32_t sh = sndm[i].h / (uint32_t)segs, gap = sh > 3 * S ? S : (sh > 2 ? 1 : 0);
    for (int g = 0; g < segs; g++) {
        uint32_t yy = sndm[i].y + sndm[i].h - (uint32_t)(g + 1) * sh;
        uint8_t col = g >= lit ? C_FIELD : g >= segs * 85 / 100 ? C_BAD : g >= segs * 65 / 100 ? C_WARN : C_OK;
        fe_fill(sndm[i].x, yy + gap, sndm[i].w, sh - gap ? sh - gap : 1, col);
    }
}
static void sndm_add(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int c)
{
    if (sndm_n >= SNDM_MAX || h < 6 || !w) return;
    int segs = (int)(h / (3 * S > 3 ? 3 * S : 3));
    if (segs > 10) segs = 10;
    if (segs < 3) segs = 3;
    sndm[sndm_n].x = x; sndm[sndm_n].y = y; sndm[sndm_n].w = w; sndm[sndm_n].h = h; sndm[sndm_n].c = c; sndm[sndm_n].segs = segs; sndm[sndm_n].lit = -1;
    sndm_draw(sndm_n++, 1);
}
static void sndm_frame(void)                            /* 매 프레임: 바뀐 계기만 */
{
    for (int i = 0; i < sndm_n; i++) sndm_draw(i, 0);
}
static void draw_mini(uint32_t x, uint32_t y, uint32_t sz, int icon, int st)
{
    uint8_t im = st == 4 ? C_FAINT : st == 0 ? C_MUTED : C_TEXT;
    uint8_t ia = st == 4 || st == 0 ? C_FAINT : st == 3 ? C_OK : st == 5 ? C_BAD : C_WARN;
    ui_rframe(x, y, sz + 4 * S, sz + 4 * S, 3 * S, (st == 1 || st == 2) ? C_WARN : st == 3 ? C_OK : C_LINE, C_CARD);
    if (icon >= 0) draw_icon_hd(x + 2 * S, y + 2 * S, sz, icon, im, ia, C_CARD);
}
static uint16_t md_vis_now;                             /* 4.0.3: 아래 장치 아이콘 표시 설정 앞 선언 */
static uint8_t net_vis_now;                              /* 왼쪽 아래 인터넷 아이콘 보임 (fe_net.inc) */
static uint8_t v9_vis_now;                               /* V9990 칩 아이콘 보임 */
static uint8_t cpu_vis_now, cpu_r800_now;                 /* 4.0.15: 왼쪽 아래 CPU(Z80·R800) 아이콘: 보임 여부, R800이면 1 */
static uint32_t msx_pic_bottom(void)                    /* 좌우 하단 아이콘 기준선 = MSX 그림 아래 끝 (192줄이면 위아래 테두리 10줄 안쪽) */
{
    uint32_t b = y0 + out_h;
    if (!v9_show && disp_lines && voff + disp_lines <= FRAME_LINES) { uint32_t e = row_y[voff + disp_lines]; if (e > y0 && e < b) b = e; }
    return b;
}
static uint32_t msx_pic_top(void)                       /* 오른쪽 위 미디어 판 기준선 = MSX 그림 위 끝 (192줄이면 위 테두리 10줄 안쪽) */
{
    uint32_t t = y0;
    if (!v9_show && disp_lines && voff < FRAME_LINES) { uint32_t e = row_y[voff]; if (e > t && e < y0 + out_h) t = e; }
    return t;
}
static void draw_panel_narrow(uint32_t rx, uint32_t rw)
{
    uint32_t m = x0 < rw ? x0 : rw;
    if (m < 16) return;
    uint32_t sz = m - 8 * S > LH ? LH : (m > 8 * S + 8 ? m - 8 * S : 8), box = sz + 4 * S, row = box + 3 * S;
    uint32_t mtop = y0, mbot = msx_pic_bottom();
    if (bar_visible && mtop < LH + 20 * S) mtop = LH + 20 * S;
    uint32_t lx = (x0 > box ? (x0 - box) / 2 : 0), y = mbot - box;
    for (int i = LED_KANA; i >= LED_CAPS; i--) if (led_vis(i)) { draw_mini(lx, y, sz, led_icon_of(i), led_now[i]); led_drawn[i] = led_now[i]; y -= row; }
    if (net_vis_now) { draw_mini(lx, y, sz, ms_icon(MS_NET, 0), 3); y -= row; }                            /* CAPS·한글 위에 인터넷 (연결됐을 때만) */
    if (v9_vis_now) { draw_mini(lx, y, sz, ms_icon(MS_V9990, 0), 2); y -= row; }                          /* V9990 칩이 일하면 */
    if (cpu_vis_now) { draw_mini(lx, y, sz, ms_icon(MS_CPU, cpu_r800_now), 2); y -= row; }   /* 4.0.15: CAPS·한글 위에 CPU */
    uint32_t ix = rx + (rw > box ? (rw - box) / 2 : 0);
    int last = M->cart_slot[4] >= 0 ? LED_SLOT5 : slot_expand || M->cart_slot[2] >= 0 || M->cart_slot[3] >= 0 ? LED_SLOT4 : LED_SLOT2;   /* 3.3.1: 기종의 C·D (3.6.7: E) */
    y = mtop;
    for (int i = LED_FDD1; i <= last; i++) if (led_vis(i) && (i >= LED_SLOT1 && i <= LED_SLOT5 ? status_show_all() && led_now[i] != 0 : (md_vis_now >> (i == LED_FDD1 ? 5 : i == LED_FDD2 ? 6 : i == LED_CAS ? 7 : 8) & 1))) {   /* 4.0.3: 쓰는 순간만, 4.0.12: 메뉴를 열어도 넣은 것만 */ draw_mini(ix, y, sz, led_icon_of(i), led_now[i]); led_drawn[i] = led_now[i]; y += row; }
    if (md_vis_now >> 9 & 1) { draw_mini(ix, y, sz, ICON_PRINTER, 3); y += row; }          /* 4.0.3: 인쇄 중인 프린터 */
    int sl[MIX_N], sn = snd_icon_list(sl);              /* 3.7.0: 음원 표 차례 (아래에서 위로) */
    y = mbot - box;
    for (int j = sn - 1; j >= 0; j--) {
        int c = sl[j];
        draw_mini(ix, y, sz, mix_icon(c), chip_on(c) ? 3 : 0);
        sndm_add(ix + box - 3 * S - (S > 1 ? S : 1), y + 3 * S, S > 1 ? 2 * S : 2, box - 6 * S, c);   /* 3.7.1 LED 계기 (오른쪽 안쪽) */
        y -= row;
    }
}

/* 3.5.3 장치 아이콘은 쓸 때만: 넣었을 때, 디스크·하드 디스크를 읽고 쓸 때, 테이프 모터가 돌 때 보이고 3초 뒤 사라짐 */
#define MD_SHOW_MEDIA_MS 5000                 /* 미디어 판 (디스크 A·B, 하드 디스크) 는 읽고 쓴 뒤 5초 보임 */
#define MD_SHOW_MS 300                        /* 4.0.3: 쓰는 순간만 (깜박임 방지로 0.3초) */
static uint32_t md_act[10], md_sig[10];                /* 3.6.7: 0~4 카트리지, 5·6 디스크 A·B, 7 테이프, 8 하드 디스크, 9 프린터 (4.0.3: 인쇄하는 동안만) */
static int prn_printing(void);
static uint16_t md_vis_now;
static int md_busy_now(int k)
{
    if (k == 5 || k == 6) return disk_size[k - 5] && M->fdc.motor && M->cpu.cycles - M->fdc.last_access[k - 5] < 3579545u / 6;
    if (k == 7) return M->tape && M->tape_motor;
    if (k == 8) return (hd_secs || hd2_secs) && P.ms() - hd_last_ms < 200;
    if (k == 9) return prn_printing();
    return 0;
}
static uint32_t md_present_sig(int k)
{
    uint32_t h = 2166136261u;
    const char *n = 0;
    if (k <= 4) { if (!M->cart[k].present) return 0; n = cart_name[k]; h ^= (uint32_t)M->cart[k].mapper; }
    else if (k <= 6) { if (!disk_size[k - 5]) return 0; n = disk_name[k - 5]; }
    else if (k == 7) { if (!M->tape) return 0; n = tape_name; }
    else if (k == 9) return prn_printing() ? 1 : 0;      /* 4.0.3: 프린터가 켜져 있고 받은 글이 쌓이는 동안 */
    else { if (!hd_secs && !hd2_secs) return 0; n = hd_secs ? hd_name : hd2_name; }
    for (; n && *n; n++) h = (h ^ (uint8_t)*n) * 16777619u;
    return h | 1;
}
/* 매 프레임: 바뀐(새로 넣은) 장치·동작 중인 장치의 시간을 고치고, 보일 장치가 바뀌면 여백을 다시 그림 */
static void md_tick(uint32_t now)
{
    uint16_t vis = 0;
    for (int k = 0; k < 10; k++) {
        uint32_t sg = md_present_sig(k);
        if (sg != md_sig[k]) { md_sig[k] = sg; md_act[k] = 0; }   /* 4.0.3: 넣었다고 보이지 않음 */
        if (sg && md_busy_now(k)) md_act[k] = now;
        if (sg && md_act[k] && now - md_act[k] < (k == 5 || k == 6 || k == 8 ? MD_SHOW_MEDIA_MS : MD_SHOW_MS)) vis |= (uint16_t)(1u << k);   /* 미디어 판의 디스크·하드는 쓴 뒤 5초 */
        if (status_show_all() && !icons_off && (k == 9 ? prn_mode != 0 : sg != 0)) vis |= (uint16_t)(1u << k);   /* 4.0.12: 메뉴·창이 열렸거나 멈췄을 때도 넣은 장치·켠 프린터만 (빈 칸은 안 보임) */
    }
    if (vis != md_vis_now) { md_vis_now = vis; margin_bd = -1; }
}
/* 4.0.15: CPU 아이콘 (Z80·R800): 켜지거나 CPU가 바뀔 때, 기종을 바꿀 때 3초 보이고 사라짐. 메뉴·창이 열렸거나 멈췄을 때는 계속 보임 (F6로 숨김) */
static void cpu_tick(uint32_t now)
{
    static int sig = -1;
    static uint32_t act;
    int r8 = ms_alt[MS_CPU], sg = r8 | (msx_dev(M, MDEV_S1990) ? 2 : 0);   /* 장치 선언 */
    if (sg != sig) { sig = sg; act = now | 1; }
    uint8_t v = (uint8_t)(!icons_off && ((act && now - act < 3000) || status_show_all()));
    if (v != cpu_vis_now || r8 != cpu_r800_now) { cpu_vis_now = v; cpu_r800_now = (uint8_t)r8; margin_bd = -1; }
}
#include "fe_media.inc"                                 /* 5.2.141 미디어 상태 판 (슬롯·디스크·하드) */
/* 3.5.0 늘 보이는 표시 (F6): 오른쪽 아래에 CAPS·한글 LED, 그 아래 장치 아이콘 (카트리지 슬롯, 디스크 A·B, 카세트, 하드 디스크).
 * 넣은 것은 밝게, 읽고 쓰는 중이면 노란 테두리. 표시만 (누르면 미디어 판은 막대 쪽에서) */
static void draw_status_cluster(uint32_t rx, uint32_t rw, uint32_t mtop, uint32_t mbot)
{
    uint32_t pad = 8 * S, g = 6 * S, bs = LH + 14 * S, lh = SLH + 8 * S;
    int cols = rw >= 3 * bs + 2 * g + 2 * pad ? 3 : rw >= 2 * bs + g + 2 * pad ? 2 : 1;   /* 여백만큼 1·2·3줄 */
    if (rw < bs + 2 * pad) bs = rw > 2 * pad + 16 ? rw - 2 * pad : 16;
    uint32_t cw = (uint32_t)cols * bs + (uint32_t)(cols - 1) * g, x = D.w - pad - cw;
    if (x < rx + pad / 2) x = rx + pad / 2;
    static const uint8_t md_led[10] = { LED_SLOT1, LED_SLOT2, LED_SLOT3, LED_SLOT4, LED_SLOT5, LED_FDD1, LED_FDD2, LED_CAS, LED_HD, LED_CAS };
    static const char *const md_tag[10] = { "1", "2", "3", "4", "5", "A", "B", "CAS", "HD", "PRN" };
    static const uint8_t md_order[5] = { 5, 6, 7, 9, 8 };   /* 4.0.3: 카세트 밑에 프린터 */
    int list[24], n = 0;
    {   /* 3.6.5: 음원 아이콘 (소리 내는 동안 노랑, 그 뒤 1초 — MIDI는 3초). 장치 아이콘 위에. 3.7.0: 음원 표 차례 */
        int sl[MIX_N], sn = snd_icon_list(sl);
        for (int j = 0; j < sn; j++) list[n++] = 1000 + sl[j];
    }
    /* 카트리지 슬롯·디스크·하드는 맨 아래 미디어 판으로 (디자인 명세). 판 폭 = 오른쪽 여백보다 조금 작게 */
    int mit[8], mn = mdp_items(mit);
    uint32_t mpad = rw / 14 > 4 * S ? rw / 14 : 4 * S, pw = rw > 2 * mpad ? rw - 2 * mpad : rw, ip = pw * 14 / 168, mh = 0;
    if (pw > MDP_CW_MAX / 2 + 2 * ip) pw = MDP_CW_MAX / 2 + 2 * ip;
    uint32_t mtm = msx_pic_top() > mtop ? msx_pic_top() - mtop : 0;   /* 위쪽은 MSX 그림 위 끝에 맞춤 (5.2.142 는 화면 높이 9 %) */
    if (mn && pw >= 60 * S) {
        uint32_t iw = (pw - 2 * ip) & ~1u, avail = mbot > mtop + mtm + LH * 3 ? mbot - mtop - mtm - LH * 3 : mbot - mtop;
        mh = mdp_height(mit, mn, iw, ip);
        while (mh > avail && iw > 40) { iw -= 8; iw &= ~1u; pw = iw + 2 * ip; mh = mdp_height(mit, mn, iw, ip); }   /* 세로가 모자라면 줄임 */
        mdp_draw(D.w - mpad - pw, mtop + mtm, pw, mit, mn, ip);   /* 오른쪽 위 (MSX 화면 위쪽 높이) */
    } else mn = 0;
    for (int oi = 0; oi < 5; oi++) {                    /* 3.6.8: 카트리지 1~5 아이콘은 안 보임 (음원 아이콘·미디어 판으로 충분) */
        int k = md_order[oi];
        if (k == 5 || k == 6 || k == 8) continue;              /* 미디어 판에 */
        if (k != 9 && led_now[md_led[k]] == 4) continue;     /* 이 기종에 없으면 안 보임 */
        if (k != 9 && led_now[md_led[k]] == 0 && !status_show_all()) continue;
        if (!(md_vis_now >> k & 1)) continue;                            /* 3.5.3: 쓸 때만 (넣음·읽고 쓰기·테이프 모터 뒤 3초) */
        list[n++] = k;
    }
    uint32_t rows = ((uint32_t)n + (uint32_t)cols - 1) / (uint32_t)cols;
    uint32_t y = mbot - rows * (bs + g) + g;
    for (int i = 0; i < n; i++) {
        int k = list[i];
        uint32_t bx = x + (uint32_t)(i % cols) * (bs + g), by = y + (uint32_t)(i / cols) * (bs + g);
        if (k >= 1000) {                                /* 음원 (3.7.0: 1000 + 음원 번호, 1100 + 기타 카트리지) */
            int c = k - 1000;
            const char *lb = snd_icon_label(c, bs - 6 * S);
            int on = chip_on(c);
            ui_rrect(bx + 2 * S, by + 3 * S, bs, bs, 9 * S, C_SHADOW);
            ui_rframe(bx, by, bs, bs, 9 * S, on ? C_WARN : C_LINE, C_CARD);
            uint32_t isz = bs - 14 * S;
            draw_icon_hd(bx + 7 * S, by + 5 * S, isz, mix_icon(c), on ? C_WARN : C_MUTED, on ? C_WARN : C_FAINT, C_CARD);
            sndm_add(bx + 2 * S, by + 7 * S, 3 * S, bs - 14 * S, c);   /* 3.7.1 LED 계기 (왼쪽 안쪽) */
            snd_label_draw(bx + bs - 4 * S, by + bs - 3 * S, lb, on ? C_WARN : C_MUTED, C_CARD, bs - 8 * S);
            continue;
        }
        int st = k == 9 ? (prn_printing() ? 3 : 2) : led_now[md_led[k]], busy = st == 3 || (k >= 5 && k <= 6 && st == 2 && M->fdc.motor && M->cpu.cycles - M->fdc.last_access[k - 5] < 3579545u / 6);
        int has = st != 0;
        ui_rrect(bx + 2 * S, by + 3 * S, bs, bs, 9 * S, C_SHADOW);
        ui_rframe(bx, by, bs, bs, 9 * S, busy ? C_WARN : C_LINE, C_CARD);
        uint32_t isz = bs - 14 * S;
        draw_icon_hd(bx + 7 * S, by + 5 * S, isz, k <= 4 ? ICON_CART : k <= 6 ? ICON_FDD : k == 7 ? ICON_CAS : k == 9 ? ICON_PRINTER : ICON_HD,
                     busy ? C_WARN : has ? C_TEXT : C_FAINT, busy ? C_WARN : has ? C_WARN : C_FAINT, C_CARD);
        const char *t = md_tag[k];
        text_f(SF, 1, bx + bs - ui_sw(t) - 4 * S, by + bs - SLH - 2 * S, t, busy ? C_WARN : has ? C_TEXT : C_MUTED, 0xFF, 0, 1);
    }
    (void)lh;
}

static int ui_active(void);
static int dialog;   /* 4.0.6: 앞선 선언 (정의는 아래) */
static void draw_panel(uint8_t bd)
{
    sndm_n = 0;                                         /* 3.7.1: 계기 자리 다시 적음 */
    memcpy(led_drawn, led_now, sizeof led_drawn);       /* 3.0: 안 보이는 LED까지 그린 것으로 (매 프레임 여백을 다시 그리던 문제) */
    fill_side_margins(bd);
    uint32_t rx = x0 + out_w, rw = D.w > rx ? D.w - rx : 0;
    if (x0 < 60) { if (!(ui_active() && dialog == 16)) draw_panel_narrow(rx, rw); return; }   /* 3.2.15 VID-09: 좁은 여백은 아이콘만 */
    uint32_t row = chip_row(), pad = 8 * S;
    uint32_t mtop = y0, mbot = msx_pic_bottom();                       /* 2.12 (5.2.165: 아래는 MSX 그림 끝에 맞춤): MSX 화면 위·아래에 맞춤 */
    if (bar_visible && mtop < LH + 20 * S) mtop = LH + 20 * S;          /* 2.17: 메뉴 막대 아래에서 */
    uint32_t ly = mtop, lw = x0 - 2 * pad;
    /* 왼쪽 위: 기종 카드 (MSX 화면 위쪽과 같은 높이) */
    /* 3.7.2: 왼쪽 위 기종 카드는 없앰 → 메뉴 막대 왼쪽 끝 (F11 메뉴와 함께) */
    for (int vr = 0; vr < 2; vr++) {                                    /* 소리 저장·영상 녹화 중: 빨간 불 칩 (기종 카드 아래) */
        if (!(vr ? vrec_on : rec_on)) continue;
        uint32_t sec = (P.ms() - (vr ? vrec_start : rec_start)) / 1000;
        char r[24] = "";
        copy_str(r, sizeof r, vr ? "VIDEO " : "REC ");
        num_append(r, sizeof r, sec / 60, 0, ' '); str_append(r, sizeof r, ":"); num_append(r, sizeof r, sec % 60, 2, '0');
        uint32_t w = 16 * S + ui_sw(r);
        draw_chip(pad, ly, w < lw ? w : lw, -1, r, 5);
        ly += row;
    }
    /* 왼쪽 아래: 맨 아래 CAPS·한글 불 (3.5.1, 켜지면 노랑), 그 위에 파나소닉 스위치 */
    {
        uint32_t y = mbot - chip_h();
        if (!icons_off) {
            uint32_t bs = LH + 14 * S, g = 6 * S;
            /* 왼쪽 여백이 넓은 만큼 1·2·3줄로 (아래에서부터 왼쪽→오른쪽) */
            uint32_t cols = lw >= 3 * bs + 2 * g ? 3 : lw >= 2 * bs + g ? 2 : 1;
            if (cols == 1 && bs > lw) bs = lw > 16 ? lw : 16;
            int it[5], nl = 0, ncaps = 0;
            for (int i = 0; i < 2; i++) { int li = i ? LED_KANA : LED_CAPS; led_drawn[li] = led_now[li]; if (led_now[li]) { it[nl++] = li; ncaps++; } }   /* 3.5.2: 불이 켜졌을 때만 */
            if (net_vis_now) it[nl++] = 100;                            /* 5.0.57 인터넷 */
            if (v9_vis_now) it[nl++] = 101;                             /* 5.0.178 V9990 칩 */
            if (cpu_vis_now) it[nl++] = 102;                            /* 4.0.15 CPU 칩 (Z80·R800) */
            for (int i = 0; i < nl; i++) {
                uint32_t lx = pad + (uint32_t)(i % (int)cols) * (bs + g), yy = mbot - bs - (uint32_t)(i / (int)cols) * (bs + g);
                int k = it[i], warn = k < 100 || k == 101 || k == 102;
                ui_rrect(lx + 2 * S, yy + 3 * S, bs, bs, 9 * S, C_SHADOW);
                ui_rframe(lx, yy, bs, bs, 9 * S, k == 100 ? C_OK : warn ? C_WARN : C_LINE, C_CARD);
                if (k < 100) {
                    int ic = led_icon_of(k);
                    if (icon_kind[ic]) draw_rgba_scaled(lx + 7 * S, yy + 7 * S, bs - 14 * S, bs - 14 * S, icon_px[ic], icon_w[ic], icon_h[ic], 1, C_WARN, C_CARD, 0);   /* 켜짐 = 노란 불 (USB 그림도 모양만 노랗게) */
                    else draw_icon_hd(lx + 7 * S, yy + 7 * S, bs - 14 * S, ic, C_WARN, C_WARN, C_CARD);
                    led_drawn[k] = led_now[k];
                } else if (k == 100) draw_icon_hd(lx + 7 * S, yy + 7 * S, bs - 14 * S, ms_icon(MS_NET, 0), C_TEXT, C_OK, C_CARD);
                else if (k == 101) draw_icon_hd(lx + 7 * S, yy + 7 * S, bs - 14 * S, ms_icon(MS_V9990, 0), C_TEXT, C_WARN, C_CARD);
                else draw_icon_hd(lx + 7 * S, yy + 7 * S, bs - 14 * S, ms_icon(MS_CPU, cpu_r800_now), C_TEXT, C_WARN, C_CARD);
            }
            (void)ncaps;
            uint32_t top = mbot - (uint32_t)((nl + (int)cols - 1) / (int)cols) * (bs + g);
            if (!nl) top = mbot;
            if (nl || cpu_vis_now || net_vis_now || v9_vis_now) y = top - chip_h();
        }
        int pl[2], pn = 0;
        for (int i = LED_PFRONT; i <= LED_PPAUSE; i++) if (pana_has(i) && led_vis(i)) pl[pn++] = i;
        uint32_t cw = 0;
        for (int k = 0; k < pn; k++) if (status_item_w(pl[k]) > cw) cw = status_item_w(pl[k]);
        if (cw > lw) cw = lw;
        for (int k = pn - 1; k >= 0; k--, y -= row) draw_status_item(pad, y, cw, pl[k]);
    }
    if (rw >= 60 && !icons_off && !(ui_active() && dialog == 16)) draw_status_cluster(rx, rw, mtop, mbot);   /* 3.5.0: 오른쪽 아래 LED·장치 아이콘 (F6) */
}

/* 일시 정지·중지 표시 (2.12): TV처럼 MSX 화면 가운데에 */
static int state_osd_drawn;
static int ui_active(void);
static void draw_state_osd(void)
{
    int want = run_state == RUN_PAUSED ? 1 : run_state == RUN_STOPPED ? 2 : 0;
    if (want != state_osd_drawn) { state_osd_drawn = want; force_redraw = 1; margin_bd = -1; return; }   /* 바뀌면 화면을 다시 그린 뒤 */
    if (!want || ui_active()) return;
    const char *t = want == 1 ? T(S_PAUSED) : T(S_STOPPED);
    const char *sub = want == 1 ? T(S_PAUSE_HINT) : T(S_STOPPED_HINT);
    uint32_t s2 = S * 2, th = (uint32_t)F->height * s2;
    uint32_t tw = text_f(F, s2, 0, 0, t, 0, 0, 0, 0), bw = th * 2 / 3;
    uint32_t sw = text_w(sub);
    uint32_t inner = bw + 16 * S + tw;
    if (sw > inner) inner = sw;
    uint32_t w = inner + 48 * S, h = th + LH + 40 * S;
    uint32_t x = x0 + (out_w > w ? (out_w - w) / 2 : 0), y = y0 + (out_h > h ? (out_h - h) / 2 : 0);
    ui_rrect(x + 4 * S, y + 6 * S, w, h, 10 * S, C_SHADOW);            /* 2.16 카드 모양 */
    ui_rframe(x, y, w, h, 10 * S, C_LINE, C_CARD);
    uint32_t cx = x + (w - (bw + 16 * S + tw)) / 2, cy = y + 14 * S;
    if (want == 1) {                                                    /* ❚❚ */
        uint32_t bar = bw / 3;
        ui_rrect(cx, cy + th / 8, bar, th * 3 / 4, bar / 3, C_WARN);
        ui_rrect(cx + bw - bar, cy + th / 8, bar, th * 3 / 4, bar / 3, C_WARN);
    } else ui_rrect(cx, cy + th / 8, bw, th * 3 / 4, bw / 6, C_BAD);   /* ■ 중지 (빨강) */
    text_f(F, s2, cx + bw + 16 * S, cy, t, C_TEXT, 0xFF, 0, 1);
    text_f(F, S, x + (w - sw) / 2, cy + th + 8 * S, sub, C_MUTED, 0xFF, 0, 1);
    mark_dirty(y, h);
}

/* 음량 표시 (2.8.2): TV처럼 화면 아래 가운데에 2초. 막대 50칸(칸마다 2%) + 숫자 */
#define VOL_OSD_MS 500                          /* 2.9: 0.5초 (2.8.2는 2초) */
static uint32_t vol_osd_until;
static int vol_osd_on;
static void vol_osd(void) { vol_osd_until = P.ms() + VOL_OSD_MS; vol_osd_on = 1; }
static void speed_osd(void) { force_redraw = 1; margin_bd = -1; }   /* 3.6.6: 화면 아래 속도 게이지 없앰 (속도는 도구 막대·에뮬레이션 창에서 봄) */
static void draw_vol_osd(void)
{
    if (!vol_osd_on) return;
    if ((int32_t)(P.ms() - vol_osd_until) >= 0) {       /* 끝: 지운 자리를 다시 그리게 */
        vol_osd_on = 0;
        force_redraw = 1; margin_bd = -1; menu_dirty = 1;
        return;
    }
    uint32_t seg = (out_w / 2) / 50 ? (out_w / 2) / 50 : 1;     /* 칸 하나 폭 */
    uint32_t lw = text_w(T(S_VOLUME)) + 16 * S, nw = text_w("1000%") + 16 * S;
    uint32_t w = lw + seg * 50 + nw + 16 * S, h = LH + 16 * S;
    if (w > D.w) w = D.w;
    uint32_t x = (D.w - w) / 2, y = y0 + out_h > h + 24 * S ? y0 + out_h - h - 24 * S : 0;
    ui_rrect(x + 3 * S, y + 4 * S, w, h, h / 2, C_SHADOW);
    ui_rframe(x, y, w, h, h / 2, C_LINE, C_CARD);
    text_f(F, S, x + 14 * S, ui_ty(y, h), muted ? T(S_MUTED) : T(S_VOLUME), C_TEXT, 0xFF, lw, 1);
    uint32_t bx = x + lw, by = y + h / 2 - 5 * S, bh = 10 * S;
    int lit = muted ? 0 : (volume + 1) / 2;
    for (int i = 0; i < 50; i++) fe_fill(bx + (uint32_t)i * seg, by, seg > 1 ? seg - 1 : 1, bh, i < lit ? C_ACC : C_LINE);
    char t[16] = "";
    num_append(t, sizeof t, (uint32_t)volume, 0, ' '); str_append(t, sizeof t, "%");
    text_f(F, S, bx + seg * 50 + 10 * S, ui_ty(y, h), t, muted ? C_FAINT : C_TEXT, 0xFF, nw, 1);
}

/* 알림(오류·경고만): 화면 가운데에 2초 동안. MSX 화면 출력 뒤 매 프레임 다시 그림 */
static void draw_toast_center(void)
{
    if (!toast_msg[0]) return;
    if ((int32_t)(P.ms() - toast_until) >= 0) {         /* 끝: 지운 자리를 다시 그리게 */
        toast_msg[0] = 0;
        force_redraw = 1;                               /* MSX 화면·대화 창·메뉴를 다시 그림 */
        margin_bd = -1;
        menu_dirty = 1;
        return;
    }
    uint32_t w = text_w(toast_msg) + 48 * S, h = LH + 24 * S;
    if (w > D.w - 16 * S) w = D.w - 16 * S;
    uint32_t x = (D.w - w) / 2, y = (D.h - h) / 2;
    ui_rrect(x + 4 * S, y + 6 * S, w, h, 8 * S, C_SHADOW);   /* 2.16 카드 모양 + 왼쪽 강조 띠 */
    ui_rframe(x, y, w, h, 8 * S, C_LINE, C_CARD);
    ui_rrect(x + 10 * S, y + 8 * S, 4 * S, h - 16 * S, 2 * S, C_ACC);
    text_f(F, S, x + 26 * S, ui_ty(y, h), toast_msg, C_TEXT, 0xFF, w - 40 * S, 1);
}

static void draw_margins(uint8_t bd)
{
    fe_trace("margins begin");
    if (M) memcpy(shadow_bd, M->vdp.line_bd, sizeof shadow_bd);   /* 3.0: 지금 줄별 테두리 색으로 그리므로 기억 (2.x는 0xFF로 지워 매 프레임 여백을 다시 그렸음) */
    draw_panel(bd);
    uint8_t tb = (run_state == RUN_STOPPED || !M || v9_show) ? bd : M->vdp.line_bd[0];
    uint8_t bb = (run_state == RUN_STOPPED || !M || v9_show) ? bd : M->vdp.line_bd[disp_lines ? disp_lines - 1 : 0];
    if (y0) fill_msx(x0, 0, out_w, y0, tb);
    if (y0 + out_h < D.h) fill_msx(x0, y0 + out_h, out_w, D.h - y0 - out_h, bb);
    if (voff && !v9_show) {                             /* MSX1: 212줄 틀 안의 위·아래 */
        fill_msx(x0, row_y[0], out_w, row_y[voff] - row_y[0], bd);
        fill_msx(x0, row_y[voff + disp_lines], out_w, row_y[FRAME_LINES] - row_y[voff + disp_lines], bd);
    }
    margin_bd = bd;
    if (ov_on) ov_dirty = 1;                            /* 3.5.0: 여백의 미디어 판도 다시 */
    fe_trace("margins end");
}

/* ------------------------------------------------------------------ */
/* MSX 화면: 바뀐 줄만                                                   */
/* ------------------------------------------------------------------ */
static uint8_t shadow[VDP_H][VDP_W], shadow_direct[VDP_H], shadow_odd[VDP_H][VDP_W], shadow_eo[VDP_H];
static uint8_t shadow_ov[VDP_H][256];       /* YJK 줄의 스프라이트 겹침 */


static uint16_t xw[VDP_W];                  /* 버퍼 512점 각각의 출력 폭 */
static uint8_t g7_8[256];                   /* 8비트 화면: DAC 16~225 근사 */

/* 화면 배치 (5.2.42 규칙 — 가로 비율이 기종·PC 에 따라 달라지던 것을 고침, 마스터님)
 *   틀은 늘 256 x 212 점. 192줄 화면은 틀 가운데(위아래 10줄은 테두리 색). 세로는 위아래 2 mm(EDID, 없으면 1 %)만 빼고 꽉 채움.
 *   점 모양(가로/세로)은 모드마다 고정:  정사각 점 1.000 / TV 1.140 (WebMSX 와 같은 NTSC 점) / 화면 가득 = PC 화면 비율대로.
 *   PC 화면의 점이 정사각이 아니면(EDID 가로·세로 mm 와 화면 모드가 안 맞음: 16:9 판에 1024x768 같은 때) 그만큼 가로를 보정해
 *   어느 PC 에서나 실제로 보이는 모양이 같게 한다. 예전(5.2.41 까지)에는 192줄이면 256x192, 212줄이면 256x212 를 세로 가득 채워
 *   MSX1 과 MSX2 212줄 화면의 점 모양이 달랐다 (4:3 ↔ 1.21:1). */
static uint32_t phys_pix_mil(void)                     /* PC 화면 점 하나의 가로/세로 (×1000). 모르면 1000 */
{
    if (!D.mm_w || !D.mm_h || !D.w || !D.h) return 1000;
    uint64_t r = (uint64_t)D.mm_w * D.h * 1000 / ((uint64_t)D.mm_h * D.w);
    return r < 500 || r > 2000 ? 1000 : (uint32_t)r;  /* 엉뚱한 EDID 는 무시 */
}
static void layout(void)
{
    disp_lines = M && M->vdp.lines == 212 ? 212 : 192;      /* MSX가 지금 실제로 보여 주는 줄 수 */
    voff = (FRAME_LINES - disp_lines) / 2;
    uint32_t margin = D.mm_h ? 2u * D.h / D.mm_h : D.h / 100;
    if (margin * 2 + FRAME_LINES > D.h) margin = 0;
    out_h = D.h - 2 * margin;                          /* 세로 기준: 위아래 2 mm만 빼고 꽉 채움 */
    uint32_t pa = aspect_mode == 2 ? 1000 : 1104;      /* MSX 점 가로/세로 ×1000 (4:3 = 1.333/(256/212) = 1.104) */
    pix_aspect_mil = pa;
    out_w = (uint32_t)((uint64_t)out_h * 256 * pa / (FRAME_LINES * (uint64_t)phys_pix_mil()));   /* 틀 256x212 에 점 모양, PC 점 모양 보정 */
    if (aspect_mode == 2) out_w = D.w;                 /* 전체화면 */
    else out_w += margin;                              /* 위아래 여백의 반만큼 좌우로도 넓힘 (4:3: 위·아래·좌·우 모두 여백 절반 = 각 변 margin/2) */
    out_h += margin;                                   /* 위·아래 여백을 반으로 줄여 화면을 키움 (전체화면은 가로가 이미 꽉 차서 위아래만) */
    if (out_w > D.w) {                                 /* 폭이 더 좁은 특이한 화면만: 폭에 맞추고 비율 유지 */
        out_h = (uint32_t)((uint64_t)D.w * FRAME_LINES * phys_pix_mil() / (256 * (uint64_t)pa));
        out_w = D.w;
    }
    if (out_h < FRAME_LINES) out_h = FRAME_LINES;
    if (vcomp == 2) {                                  /* 나란히 — 왼쪽 반 = MSX, 오른쪽 반 = V9990. 각 그림은 반쪽 폭에 비율을 지켜 맞춤 */
        uint32_t half = D.w / 2;
        if (out_w > half) { out_h = (uint32_t)((uint64_t)out_h * half / out_w); out_w = half; }
        if (out_h < FRAME_LINES) out_h = FRAME_LINES;
        v9w = out_w; v9h = out_h; v9y = (D.h - out_h) / 2;
        v9x = (half + (half - out_w) / 2) & ~3u;
        x0 = ((half - out_w) / 2) & ~3u;
    }
    sx_eff = out_w / 256 ? out_w / 256 : 1;
    sy_eff = out_h / FRAME_LINES ? out_h / FRAME_LINES : 1;
    if (vcomp != 2) x0 = ((D.w - out_w) / 2) & ~3u;
    y0 = (D.h - out_h) / 2;
#ifndef FE_UEFI
#ifdef FE_HOST_DEBUG
    fprintf(stderr, "layout: lines %u voff %u out %ux%u at %u,%u pa %u phys %u\n", disp_lines, voff, out_w, out_h, x0, y0, pa, phys_pix_mil());
#endif
#endif
    for (int y = 0; y <= (int)FRAME_LINES; y++) row_y[y] = (uint16_t)(y0 + (uint32_t)y * out_h / FRAME_LINES);
    for (int x = 0; x < VDP_W; x++) xw[x] = (uint16_t)((uint32_t)(x + 1) * out_w / VDP_W - (uint32_t)x * out_w / VDP_W);
    recompute_palette();
    for (int c = 0; c < 256; c++) {
        uint8_t r, g, b;
        vdp_g7_rgb(&M->vdp, (uint8_t)c, &r, &g, &b);
        int ri = r * 5 / 255, gi = g * 6 / 255, bi = b * 4 / 255;     /* 6 x 7 x 5 = 210 (DAC 16~225) */
        g7_8[c] = (uint8_t)(16 + (ri * 7 + gi) * 5 + bi);
    }
}
static uint32_t last_present;
static uint32_t pres_clip_x = 0, pres_clip_w = 0;      /* 4.0: 팝업 가로 자리 (이 칸만 건너뛰고 양옆은 MSX 화면을 그림) */
static uint32_t pres_clip_y2 = 0xFFFFFFFFu;            /* 4.0: 팝업 아래 끝 (그 아래는 다시 MSX 화면) */
static uint32_t pres_clip_y = 0xFFFFFFFFu;            /* 3.7.0: 이 줄부터 아래는 MSX 화면을 그리지 않음 (믹서 팝업 자리) */
#include "fe_monemu.inc"

static void adj_shift(uint8_t *dst, const uint8_t *src, int sh, uint8_t fill)
{   /* sh > 0 = 오른쪽으로 */
    for (int x = 0; x < VDP_W; x++) { int s = x - sh; dst[x] = s >= 0 && s < VDP_W ? src[s] : fill; }
}

#include "fe_v9990.inc"                              /* 5.2.85 Gfx9000 (V9990) 프런트엔드 부품: 화면 그리기·아이콘·꽂기/빼기 */

static uint32_t pres_line32[4096];
static void present_impl(void)
{
    {   /* 5.2.19 화면 출력 층: 소스 표(src/vsrc.h)로 보일 장치를 고름 — 자동이면 V9990 이 화면을 내보낼 때 V9990, 아니면 MSX(VDP) */
        static void (*const vsrc_draw[VSRC_N])(void) = { [VSRC_VDP] = 0, [VSRC_V9990] = present_v9990 };
        int id = run_state == RUN_STOPPED ? VSRC_VDP : msx_vsrc_pick(M, vsrc_pref);
        int comp = vsrc_pref >= VSRC_N && M->v9 && run_state != RUN_STOPPED ? vsrc_pref - VSRC_N + 1 : 0;   /* 겹치기 1 · 나란히 2 (V9990 이 꽂혀 있을 때만) */
        if (comp) id = VSRC_VDP;
        if (id != vsrc_cur || comp != vcomp) { vsrc_cur = id; vcomp = comp; layout(); force_redraw = 1; margin_bd = -1; }
        if (comp == 1) { force_redraw = 1; margin_bd = -1; }   /* 겹치기: 매 프레임 MSX 와 여백을 새로 그린 위에 섞음 (쌓여 진해지지 않게) */
        if (vsrc_draw[id]) {
            if (M->vdp.pal_dirty) { M->vdp.pal_dirty = 0; recompute_palette(); if (P.set_palette) P.set_palette(); force_redraw = 1; }
            if (margin_bd != 1) draw_margins(1);
            if (!ui_active() || live_ui()) sndm_frame();
            if (msx_vsrc[id].active(M)) vsrc_draw[id]();
            else {                                      /* 고른 소스가 화면을 내보내지 않음 (V9990 표시 꺼짐) → 검은 화면 */
                if (force_redraw) { fill_msx(x0, y0, out_w, out_h, 1); mark_dirty_rect(x0, y0, out_w, out_h); force_redraw = 0; }
                draw_toast_center(); draw_vol_osd(); last_present = P.ms();
            }
            return;
        }
    }
    int emu_on = (mon_emu || fx_blur || fx_glow || fx_noise) && D.bpp == 4;
    int plany = D.bpp == 4 && M->vdp.pal_nsnap > 1 && run_state != RUN_STOPPED;
    plmode = !emu_on && plany;   /* 4.0.29: 프레임 안에서 팔레트가 바뀜 */
    if (plany) {
        for (int k = 0; k < M->vdp.pal_nsnap; k++)
            for (int i = 0; i < 16; i++) {
                uint8_t r8 = M->vdp.pal_snap[k][i][0], g8 = M->vdp.pal_snap[k][i][1], b8 = M->vdp.pal_snap[k][i][2];
                monitor_tint(&r8, &g8, &b8);
                snap32[k][i] = D.bgr ? ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8 : ((uint32_t)b8 << 16) | ((uint32_t)g8 << 8) | r8;
            }
        force_redraw = 1;
    } else if (plmode_prev) force_redraw = 1;
    plmode_prev = plmode;
    /* 3.2.10 VID-05: V9938 R#18 화면 위치 (openMSX와 같이 니블 ^ 7 − 7: 가로 −7~+8점, 세로 −7~+8줄, + = 오른쪽·아래) */
    int adj_on = M->vdp.v9938 && run_state != RUN_STOPPED, adj_v = 0;
    if (adj_on) adj_v = ((M->vdp.line_adj[0] >> 4) ^ 7) - 7;   /* 4.0.31: 가로는 줄마다 (Psycho World: 위쪽 필드는 R#18로 미세 스크롤, HUD 줄에서 0), 세로는 첫 줄 값 */
    uint32_t *line32 = pres_line32;
    uint8_t *line8 = (uint8_t *)line32;
    if (M->vdp.pal_dirty) {                          /* 프로그램이 팔레트를 바꿈 */
        M->vdp.pal_dirty = 0;
        recompute_palette();
        if (P.set_palette) P.set_palette();
        force_redraw = 1;
    }
    if ((M->vdp.lines == 212 ? 212u : 192u) != disp_lines) { layout(); force_redraw = 1; margin_bd = -1; }   /* 192 ↔ 212줄 모드 전환 */
    uint8_t bd = run_state == RUN_STOPPED ? 1 : vdp_backdrop(&M->vdp);
    if (memcmp(shadow_bd, M->vdp.line_bd, disp_lines)) {   /* 줄별 테두리 색이 바뀜 (MSX2+ 로고 같은 효과) → 여백 다시 */
        memcpy(shadow_bd, M->vdp.line_bd, disp_lines);
        margin_bd = -1;
    }
    if (bd != margin_bd) draw_margins(bd);
    fe_trace("pres: sndm");
    if (!ui_active() || live_ui()) sndm_frame();     /* 3.7.1: 음원 아이콘 LED 계기 */
    fe_trace("pres: lines");
    for (uint32_t y = 0; y < disp_lines; y++) {
        if (!(y & 63) && y) fe_trace("pres: line+64");   /* 5.2.27 멈춤 추적 */
        static const uint8_t black[VDP_W];
        static uint8_t adj_e[VDP_W], adj_o[VDP_W], adj_b[VDP_W];
        int sy = (int)y - adj_v;                            /* 3.2.10 VID-05: R#18 세로 맞춤 */
        const uint8_t *src, *odd, *ovp;
        int direct, direct_odd, eo;
        if (sy < 0 || sy >= (int)disp_lines) {          /* 밀려 들어온 줄 = 테두리 색 */
            memset(adj_b, bd, VDP_W); src = odd = adj_b; ovp = black; direct = direct_odd = 0; eo = 0;
        } else {
            src = M->vdp.fb[sy]; odd = M->vdp.fb_odd[sy]; ovp = M->vdp.spr_ov[sy];
            direct = M->vdp.line_direct[sy]; direct_odd = M->vdp.line_direct_odd[sy];
            eo = M->vdp.line_eo[sy] && run_state != RUN_STOPPED;
            int adj_h = adj_on ? ((M->vdp.line_adj[sy] & 15) ^ 7) - 7 : 0;
            if (adj_h) {                                    /* R#18 가로 맞춤: 2점 단위(512폭)로 밀고 빈 곳은 테두리 */
                adj_shift(adj_e, src, adj_h * 2, direct ? (direct == 1 ? M->vdp.reg[7] : 0) : bd); src = adj_e;
                if (eo) { adj_shift(adj_o, odd, adj_h * 2, direct_odd ? (direct_odd == 1 ? M->vdp.reg[7] : 0) : bd); odd = adj_o; }
            }
        }
        if (run_state == RUN_STOPPED) { src = black; direct = 0; }
        if (!force_redraw && direct == shadow_direct[y] && eo == shadow_eo[y] && !memcmp(src, shadow[y], VDP_W) &&
            (!eo || !memcmp(odd, shadow_odd[y], VDP_W)) && (direct < 2 || !memcmp(ovp, shadow_ov[y], 256))) continue;
        if (direct >= 2) memcpy(shadow_ov[y], ovp, 256);
        memcpy(shadow[y], src, VDP_W);
        if (eo) memcpy(shadow_odd[y], odd, VDP_W);
        shadow_direct[y] = (uint8_t)direct;
        shadow_eo[y] = (uint8_t)eo;
        if (emu_on) { emu_ly = plany && (int)y < (int)disp_lines ? (int)y : -1; emu_line_rgb(src, direct, ovp, emu_rgb[y]); emu_chg[y] = 1; continue; }   /* 3.2.3: 그리기는 아래 emu_render */
        uint32_t bytes = out_w * (uint32_t)D.bpp;
        uint32_t ry = row_y[y + voff], rh_ = row_y[y + voff + 1] - ry;   /* 이 줄의 화면 위치·높이 */
        uint32_t even_rows = eo ? (rh_ + 1) / 2 : rh_;          /* 인터레이스: 위쪽은 짝수 필드, 아래쪽은 홀수 필드 */
        for (int half = 0; half < (eo ? 2 : 1); half++) {
            const uint8_t *s0 = half ? odd : src;
            int dr = half ? direct_odd : direct;
            uint32_t k = 0;
            if (dr >= 2) {                                  /* MSX2+ YJK (SCREEN 10~12): 4점씩 해독 */
                uint8_t raw[256], rgb[256][3], pi[256];
                for (int x = 0; x < 256; x++) raw[x] = s0[2 * x];
                vdp_yjk_line(raw, dr == 3, rgb, pi);
                const uint8_t *ov = ovp;
                for (int x = 0; x < VDP_W; x++) {
                    int sx = x >> 1;
                    uint8_t r8 = rgb[sx][0], g8 = rgb[sx][1], b8 = rgb[sx][2];
                    monitor_tint(&r8, &g8, &b8);
                    if (D.bpp == 1) {
                        uint8_t c = ov[sx] != 0xFF ? g7_8[ov[sx]] : pi[sx] != 0xFF ? pi[sx]
                                  : (uint8_t)(16 + ((r8 * 5 / 255) * 7 + g8 * 6 / 255) * 5 + b8 * 4 / 255);
                        for (uint32_t q = 0; q < xw[x]; q++) line8[k++] = c;
                    } else {
                        uint32_t c = ov[sx] != 0xFF ? g7_32[ov[sx]] : pi[sx] != 0xFF ? px32(pi[sx])
                                   : D.bgr ? ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8 : ((uint32_t)b8 << 16) | ((uint32_t)g8 << 8) | r8;
                        for (uint32_t q = 0; q < xw[x]; q++) line32[k++] = c;
                    }
                }
            } else if (D.bpp == 1) {
                for (int x = 0; x < VDP_W; x++) {
                    uint8_t c = dr ? g7_8[s0[x]] : s0[x];
                    for (uint32_t s = 0; s < xw[x]; s++) line8[k++] = c;
                }
            } else {
                for (int x = 0; x < VDP_W; x++) {
                    uint32_t c = dr ? g7_32[s0[x]] : plmode && sy >= 0 && sy < (int)disp_lines ? snap32[M->vdp.line_pal[sy]][s0[x] & 15] : px32(s0[x]);
                    for (uint32_t s = 0; s < xw[x]; s++) line32[k++] = c;
                }
            }
            uint32_t r0 = half ? even_rows : 0, r1 = half ? rh_ : even_rows;
            if (!half && !eo) r1 = rh_;
            uint32_t r_end = r1;
            if (scanlines && !eo && rh_ >= 2) {                 /* 3.2.10 VID-01: 스캔라인 세기 (마지막 줄을 그만큼 어둡게) */
                r1 = rh_ - 1;
                uint8_t *dst = D.fb + (uintptr_t)(ry + r1) * D.pitch + x0 * (uint32_t)D.bpp;
                if (ry + r1 >= pres_clip_y && ry + r1 < pres_clip_y2) dst = 0;   /* 3.7.0: 믹서 팝업 자리 */
                if (!dst) { }
                else if (D.bpp == 1) { if (scanlines >= 50) memset(dst, UI_DAC, out_w); else memcpy(dst, line32, bytes); }
                else {
                    uint32_t f = (uint32_t)(100 - scanlines) * 256 / 100, *d32 = (uint32_t *)(void *)dst;
                    for (uint32_t q = 0; q < out_w; q++) { uint32_t c = line32[q]; d32[q] = ((((c & 0xFF00FFu) * f) >> 8) & 0xFF00FFu) | ((((c & 0x00FF00u) * f) >> 8) & 0x00FF00u); }
                }
                r_end = rh_;
            }
            if (ry + r_end > pres_clip_y && !pres_clip_w) {     /* 3.7.0: 믹서 팝업 자리는 건너뜀 */
                uint32_t lim = pres_clip_y > ry ? pres_clip_y - ry : 0;
                if (r_end > lim) r_end = lim;
                if (r1 > lim) r1 = lim;
                if (r0 > r1) r0 = r1;
            }
            if (ry + r1 > D.h || x0 + out_w > D.w || bytes > sizeof pres_line32) {   /* 화면 밖에 쓰지 않음 (멈춤 추적) */
                static int bad_n; if (bad_n < 4) { bad_n++; char t[24] = "y"; char *q = t + 1; uint32_t v = y; char nb[12]; int nn = 0; do { nb[nn++] = (char)('0' + v % 10); v /= 10; } while (v); while (nn) *q++ = nb[--nn]; *q++ = ' '; v = ry + r1; nn = 0; do { nb[nn++] = (char)('0' + v % 10); v /= 10; } while (v); while (nn && q < t + 22) *q++ = nb[--nn]; *q = 0; fe_trace_str("pres: BAD rows", t); }
                continue;
            }
            for (uint32_t r = r0; r < r1; r++) {
                uint8_t *dr = D.fb + (uintptr_t)(ry + r) * D.pitch + x0 * (uint32_t)D.bpp;
                if (ry + r >= pres_clip_y && ry + r < pres_clip_y2 && pres_clip_w) {      /* 4.0: 팝업 칸만 비우고 양옆·아래는 그림 */
                    uint32_t bp = (uint32_t)D.bpp, cx0 = pres_clip_x > x0 ? pres_clip_x - x0 : 0, cx1 = cx0 + pres_clip_w;
                    if (cx0 > out_w) cx0 = out_w;
                    if (cx1 > out_w) cx1 = out_w;
                    memcpy(dr, line32, cx0 * bp);
                    memcpy(dr + cx1 * bp, (const uint8_t *)line32 + cx1 * bp, (out_w - cx1) * bp);
                } else memcpy(dr, line32, bytes);
            }
            mark_dirty_rect(x0, ry + r0, out_w, r_end > r0 ? r_end - r0 : 0);
            if (ov_on && (((ov_on & 1) && ry < tb_h()) || ((ov_on & 2) && !rp_fixed && ry + rh_ > rp_y && ry < rp_y + rp_h))) ov_dirty = 1;   /* 3.5.0: 막대 밑 줄을 다시 그림 */
        }
    }
    fe_trace("pres: lines done");
    if (vcomp && M->v9) {                              /* 겹치기·나란히 — MSX 를 그린 뒤 V9990 을 얹음 */
        if (msx_vsrc[VSRC_V9990].active(M)) present_v9990_draw(vcomp);
        else if (vcomp == 2 && force_redraw) { fill_msx(v9x, v9y, v9w, v9h, 1); mark_dirty_rect(v9x, v9y, v9w, v9h); }
    }
    if (emu_on) { emu_render(); if (ov_on) ov_dirty = 1; }
    force_redraw = 0;
    draw_toast_center();
    draw_vol_osd();
    last_present = P.ms();
    fe_trace("pres: end");
}

static void present(void) { PF_BEGIN; present_impl(); PF_END(PF_VIDEO); }

void fe_fatal(const char *msg, const char *detail)
{
    if (!D.fb) for (;;) __asm__ volatile("hlt");
    fe_fill(0, 0, D.w, D.h, 4);
    fe_text(32, 32, "X86MSX: fatal error", 15, 4);
    fe_text(32, 32 + LH * 2, msg, 15, 4);
    if (detail) fe_text(32, 32 + LH * 3, detail, 15, 4);
    fe_flush_now();                                     /* 뒤 버퍼 방식: 멈추기 전에 화면으로 */
    for (;;) __asm__ volatile("hlt");
}

/* ------------------------------------------------------------------ */
/* 동작                                                                 */
/* ------------------------------------------------------------------ */
enum {
    A_NONE, A_NOT_YET, A_MENU,
    A_RUN_PAUSE, A_HARD_RESET, A_SOFT_RESET, A_FULL_RESET,
    A_VOL_UP, A_VOL_DOWN, A_MUTE, A_VHD_C, A_VHD_D, A_VHD_E, A_VHD_F, A_VHD_G, A_VHD_H,
    A_SPEED_NORMAL, A_SPEED_UP, A_SPEED_DOWN, A_SPEED_MAX, A_TURBO_HOLD,
    A_CART1_INSERT, A_CART2_INSERT, A_CART1_EJECT, A_CART2_EJECT,
    A_CART3_INSERT, A_CART4_INSERT, A_CART3_EJECT, A_CART4_EJECT, A_INFO_CART3, A_INFO_CART4,
    A_EXIT, A_REBOOT, A_EXIT_NOW, A_REBOOT_NOW, A_DIAG, A_ABOUT, A_SOUND_DIAG,
    A_LANG_KO, A_LANG_EN, A_LANG_JA, A_LANG_DE, A_LANG_FR, A_LANG_ES, A_LANG_SV, A_LANG_RU, A_LANG_PT, A_LANG_ET, A_LANG_AR, A_SCALE_AUTO, A_USB_GROW,
    A_RESET_AFTER_INSERT, A_HD_RESET_EJECT, A_HD_RESET_INS, A_VHD_RESET_INS, A_DISK_RESET_AFTER, A_PANA_FRONT, A_PANA_PAUSE, A_PANA_AUDIO,
    A_PROPS_EMU, A_PROPS_MIX, A_MIDI_MIX, A_PROPS_KB, A_NET, A_NET_WIFI, A_NET_WIRED, A_PROPS_VIDEO, A_PROPS_EFFECTS, A_PROPS_SOUND, A_PROPS_MIDI, A_POP_JOY, A_POP_MOUSE, A_IMAGE_MAKE, A_HELP, A_DISK_COPY,
    A_DISKA_INSERT, A_DISKB_INSERT, A_DISKA_EJECT, A_DISKB_EJECT, A_DISKA_WP, A_DISKB_WP,
    A_INFO_DISKA, A_INFO_DISKB, A_TAPE_INSERT, A_TAPE_EJECT, A_TAPE_REWIND, A_INFO_TAPE,
    A_HD_INSERT, A_HD_EJECT, A_INFO_HD, A_HD2_INSERT, A_HD2_EJECT, A_INFO_HD2,
    A_MACHINE, A_MACHINE_EDIT, A_SC_EDIT, A_KEYMAP_EDIT,
    A_QUICK_LOAD, A_QUICK_SAVE, A_STATE_LOAD, A_STATE_SAVE, A_AUDIO_REC, A_SCREENSHOT,
    A_DISKA_NEXT, A_DISKB_NEXT,
    A_VIDEO_RESET,
    A_RESET_CTRL,                           /* 3.2.0: CTRL을 누른 채 리셋 (디스크 드라이브 하나만 = 메모리 아낌) */
    A_TAPE_NEW,                             /* 3.2.1 MED-07: 새 테이프 */
    A_ICONS_TOGGLE,                         /* 3.2.2: 아이콘 모두 숨기기/보이기 (F6) */
    A_BAS_CONV,                             /* 3.2.9: BASIC TXT변환 (넣기·저장 한 창) */
    A_VID_REC,                              /* 3.2.13 VID-02 영상 녹화 */
    A_SP_MENU1, A_SP_MENU2, A_TAPE_POSMENU,  /* 3.2.14: 특수 카트리지 메뉴·테이프 위치 메뉴 바로 열기 */
    A_PRN_OFF, A_PRN_TEXT, A_PRN_RAW, A_PRN_END, A_PRN_POP0, A_PRN_POP1, A_PRN_POP2, A_PRN_TEAR, A_PRN_VIEW,   /* 3.3.0 프린터 */
    A_LANG_MENU, A_LANG_WIN, A_SPR_TOGGLE,   /* 3.3.1 */
    A_STEREO,                               /* 3.3.3 */
    A_INFO_CART1, A_INFO_CART2, A_INFO_VOLUME, A_INFO_MACHINE,  /* 표시 전용 (고를 수 없음) */
    A_MEDIA,                                /* 3.5.0: 미디어 팝업 (RCtrl+RShift+D) */
    A_SPEED_2X, A_SPEED_4X, A_MON_COLOR, A_MON_BW, A_MON_GREEN, A_MON_AMBER, A_SCANLINE_TOGGLE,  /* 3.6.0: F11 메뉴 (도구 막대와 같은 일) */
    A_DISKA_DIR, A_DISKB_DIR,               /* 3.6.1: 디렉토리 삽입 (blueMSX Insert Directory) */
    A_CART5_INSERT, A_CART5_EJECT, A_INFO_CART5,   /* 3.6.7: 카트리지 슬롯 5 */
    A_PROPS_OUTPUT                          /* 4.0.134: 옵션 > 출력 장치 (속성 창 출력 장치 탭) */
};
static int reset_after_insert = 1;
static int hd_reset_insert = 1;             /* 3.9.8: 하드 디스크 삽입·가상 하드 연결 뒤 MSX 다시 시작 (기본 켜짐) */
static int vhd_reset_insert = 1;            /* 3.9.9: 가상 하드 연결 뒤 MSX 다시 시작 (하드 디스크 이미지와 따로, 기본 켜짐) */
static int hd_reset_eject = 0;              /* 3.9.0: 하드 디스크를 꺼낸 뒤 MSX 다시 시작 (기본 꺼짐) */
static void menu_open_special(int slot);
static void menu_open_tpos(void);
static void menu_open_lang(void);
static int bas_len, bas_pos, bas_lines;                 /* 3.2.7: 넣는 중인 BASIC 텍스트 (0 = 없음) */
static void bas_feed(void);
static void bas_stop(int user);
static void bas_open(void);
#define CTRL_HOLD 700                         /* 4.0.31: 리셋 뒤 CTRL을 누르는 프레임 수 (MSX2+는 디스크 롬이 600프레임쯤에야 키를 봄. 450으론 모자랐음) */
static int disk_accel = 1;                              /* FDD·HDD 접근 중에는 속도 제한 없이 (기본 켜짐) */
static int auto_ctrl = 1, auto_tried, auto_busy, auto_stuck; static uint32_t auto_age;   /* 켠 지 얼마 안 됐을 때만 (아무 때나 재시작하던 것 수정) */   /* 4.0.31: 게임이 제자리 점프(JP $)에서 멈추면 CTRL을 누르고 한 번 다시 시작 */
static int boot_ctrl;                        /* 4.0.31: 켤 때마다 CTRL을 누름 (디스크 롬이 둘째 드라이브를 만들지 않아 메모리가 늘어남) */
static int ctrl_hold;                        /* 3.2.0: 남은 프레임 동안 MSX CTRL을 누름 */
static int disk_reset_after_insert = 0;     /* 디스크: 삽입 후 리셋 (기본 꺼짐) */
#define A_RECENT 600                        /* 2.12 최근 파일: + 종류(0 카트리지 슬롯 1~4 = 0~3, 4·5 디스크 A·B, 6 테이프, 7 카트리지 슬롯 5)*8 + 번호 */
#define RECENT_N 8
#define A_SPECIAL 1000                      /* + 슬롯*128 + 종류 (src/spcat.c kind, 2.16 / 5.2.44, 5.2.47: 종류 64 넘어 128 칸) */
#define A_TAPE_POS 700                      /* 3.2.1 MED-06: + 테이프 안 파일 번호 (TAPE_POS_N개까지) */
#define TAPE_POS_N 16
static void hd_eject(void);
static void hd_eject_unit(int u);
static void hd_auto_cart_remove(int only_other);
static int hd2_insert(const char *path, const char *name, int quiet);
static int insert_special(int n, int kind);
static int special_apply(int n, int kind, int quiet);
static void x86midi_check(int quiet);
static int special_kind(int code);
static void do_quick_save(void);
static void do_quick_load(void);
static void do_state_save(void);
static void open_state_list(void);
static void rec_toggle(void);
static void rec_stop(void);
static void do_screenshot(void);
static void sram_save_all(void);
static void fw_sram_save(void);
static void fw_sram_load(void);
static void resume_save(void);
static void resume_load(void);
static void disk_next(int d);
static int snd_stereo = 1;                          /* 3.3.3 SND3-01 스테레오 출력 */
static void mix_apply(void);
#define recent_max 8                                /* 메뉴에 보일 최근 파일 수 */
static int fw_switch;                               /* 3.4.0 Matsushita 펌웨어 스위치 (turbo R은 내장 소프트웨어 롬 붙이기) */
static int machine_has_fw;                          /* 1 = 펌웨어 롬 있음, 2 = turbo R 펌웨어(27·68) */
static int switch_machine(const char *name, char *err, int errlen);
static void resume_enter(void);
static void ini_parse_lines(char *line);
static void settings_flush(void);
static void opts_save(void);
static void opts_load(void);
static void resume_save(void);
static void power_on(void);
static void fw_toggle(void)       /* 4.0.1: 프런트(펌웨어) 스위치 — 속성의 펌웨어 스위치와 같은 것 */
{
    fw_switch = !fw_switch; M->fw.mat_switch = (uint8_t)fw_switch; pana_front = fw_switch; settings_changed();
    if (machine_has_fw == 2) { char e[160]; msx_fw_turbor = fw_switch; if (!switch_machine(machine_name, e, sizeof e)) power_on(); }   /* turbo R: 롬을 붙이거나 떼고 다시 켬 */
}
static int global_save = 1;                         /* 4.0.34: 전체 상태 저장 (쓰던 기종 기억). 끄면 늘 MSX - C-BIOS로 시작 */
static int resume_on = 1;                           /* 2.11: 끝낼 때 상태 저장, 켤 때 이어서 */
/* 3.0 PC 마우스 설정 */
static int mouse_port;                              /* MSX 마우스 포트: 0 없음, 1 포트 1, 2 포트 2 */
static int mouse_sens = 100;                        /* 감도 % (25~400) */
static int mouse_menu = 1;                          /* 메뉴·창에서 마우스 */
/* 3.4.3 MOUSE-05 */
static int mouse_on = 1, mouse_invx, mouse_invy, mouse_lock, mouse_ptr, mouse_wheel = 1;   /* 잠금 0 자동 1 수동, 포인터 0 자동 1 메뉴만 2 항상 3 숨김, 휠 1~5줄 */
static int mouse_own;                                /* 3.0.4: 1 = USB 마우스를 X86MSX가 직접 (0 = 펌웨어 드라이버 먼저) */
static int mouse_offmask, mouse_ndev = -1, touchpad_pref = -1;   /* touchpad_pref: -1 처음(다른 마우스가 있으면 끔), 0 끔, 1 켬 */
static int mouse_is_ps2(const char *n) { return (n[0] == 'P' && n[1] == 'S' && n[2] == '/') || (n[0] == 'F' && n[1] == 'i' && n[2] == 'r') || (n[0] == 'I' && n[1] == '2' && n[2] == 'C'); }   /* PS/2 · 펌웨어 터치패드: 처음엔 다른 마우스가 있을 때만 끔 */
static int mouse_wanted_off(int k, const char *n, int nd) { return mouse_is_ps2(n) ? (touchpad_pref < 0 ? nd > 1 : !touchpad_pref) : (mouse_offmask >> k) & 1; }                 /* 3.8.2: 마우스별 사용 안 함 (비트 n), 지난번 연결 수 */
static int mouse_count;                             /* 연결된 마우스 수 */
static char mouse_name[40];
static void env_save_current(void);
static void env_apply(void);

static Input in;
static void settings_changed(void);
static void settings_flush(void);
static void open_browser(int slot);
static void disk_flush(int d);
static void disk_eject(int d);
static void disks_reinsert(void);
static void open_machines(void);
static void open_props(int tab);
static void open_net(int mode);
static int net_any_adapter(void);
static int net_cart_present(void);
static int net_has_wifi(void);
static int net_has_wired(void);
static int net_wired_on, net_wifi_on;                          /* 5.0.57 fe_net.inc */
static void netdlg_key(int key);
static void open_langwin(void); static void langwin_test_open(void); static void draw_langwin(void); static void langwin_key(int key);   /* 5.2.192 fe_langwin.inc */
static void open_midimix(void); static void draw_midimix(void); static void midimix_key(int key);   /* 5.2.53 fe_midimix.inc */
static void draw_miditab(uint32_t X, uint32_t W, uint32_t top, uint32_t bottom); static void miditab_key(int key); static void mt_dd_apply(int i);   /* 5.2.54 fe_miditab.inc */
static void open_medit_name(const char *name);
static void open_machines_edit(void);
static void scan_name(int key, char *out, int max);
static void open_dialog(int kind);
static void bgw_finish(void);                         /* 5.2.69 fe_state.inc: 조금씩 저장을 마저 */
static int bgw_busy(void);
static void bgw_step(uint32_t budget_ms);
static void open_image_dialog(void);                 /* 3.0.2 fe_sys.inc */
static void open_help(void);
static void open_disk_copy(void);
static void open_diag(int tab);
static int ui_pressing;
static int diag_kbd_tab(void);
static int dg_joy_tab(void);
static void dg_joy_poll(void);
static void dg_tp_poll(void);
static void menu_close_all(void);
static void pop_media(void);                          /* 3.5.0 fe_ov.inc */

static void full_redraw(void) { force_redraw = 1; margin_bd = -1; }

/* 글꼴 파일 — USB\fonts\*.x8f (tools/mkfont.py 가 만듦). 작은·큰·굵은 글꼴 셋이 한 파일에 들어 있고 부팅 때 하나만 메모리에 읽음.
 * 파일이 없거나 틀리면 내장 점글꼴(Galmuri). 설정 파일 Font=<파일 이름> ("-" 는 내장). 설정 파일 Font=- 로 내장 글꼴 */
_Static_assert(sizeof(FontGlyph) == 12, "x8f 글자 표는 12바이트");
#define FX_MAX ((4u << 20) - (64u << 10))      /* 글꼴 파일 최대 (BSS 가 커널 끝 0x03800000 안에 들어가게 64 KB 뺌) */
static uint8_t fx_buf[FX_MAX] __attribute__((aligned(16)));
static char fx_name[64];                                /* 지금 쓰는 파일 이름 ("" = 내장) */
static char fx_pref[64];                                /* 고른 글꼴 ("" = 내장). 모든 언어가 따름 */
static int fx_err;                                      /* 마지막 실패 이유: 1 읽기 실패 2 머리 틀림 3 글꼴 표 틀림 4 이름 없음 */
static int fx_seen;                                     /* 설정 파일에 Font 가 있었나 */
static UiFont fx_builtin[7]; static int fx_have_builtin;
static uint32_t fx_u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void fx_use_builtin(void)
{
    if (!fx_have_builtin) { fx_builtin[0] = font_small; fx_builtin[1] = font_large; fx_builtin[2] = font_large; fx_builtin[3] = font_small; fx_builtin[4] = font_small; fx_builtin[5] = font_large; fx_builtin[6] = font_large; fx_have_builtin = 1; }
    font_small = fx_builtin[0]; font_large = fx_builtin[1]; font_bold = fx_builtin[2]; font_tiny = fx_builtin[3]; font_btn = fx_builtin[4]; font_head = fx_builtin[5]; font_title = fx_builtin[6];
    fx_name[0] = 0;
}
static int fx_install(const char *name)                 /* 0 = 파일 글꼴로 바꿈. 아니면 내장으로 돌아가고 -1 */
{
    char nm[64]; copy_str(nm, sizeof nm, name ? name : ""); name = nm;    /* name 이 fx_name 일 수 있어 먼저 복사 */
    fx_use_builtin();
    fx_err = 0;
    if (!name[0] || !strcmp(name, "-") || !P.read_file) return -1;
    char path[FE_PATH_MAX];
    copy_str(path, sizeof path, "0:\\fonts\\"); str_append(path, sizeof path, name);
    uint32_t n = 0;
    if (P.read_file(path, fx_buf, FX_MAX, &n)) { fx_err = 1; return -1; }
    if (n < 8 + 28 * 3 || memcmp(fx_buf, "X8F1", 4) || (fx_u32(fx_buf + 4) != 3 && fx_u32(fx_buf + 4) != 4 && fx_u32(fx_buf + 4) != 7)) { fx_err = 2; return -1; }
    uint32_t nfont = fx_u32(fx_buf + 4);
    if (n < 8 + 28 * nfont) { fx_err = 2; return -1; }
    static const uint32_t want_h[7] = { 16, 21, 21, 16, 16, 21, 21 };    /* 화면 배치가 줄 높이에 맞춰져 있음 */
    UiFont nf[7];
    for (uint32_t i = 0; i < nfont; i++) {
        const uint8_t *m = fx_buf + 8 + 28 * i;
        uint32_t h = fx_u32(m), asc = fx_u32(m + 4), cnt = fx_u32(m + 8), aa = fx_u32(m + 12), to = fx_u32(m + 16), bo = fx_u32(m + 20), bl = fx_u32(m + 24);
        if (h != want_h[i] || !cnt || (to & 3) || to > n || cnt > (n - to) / 12 || bo > n || bl > n - bo) { fx_err = 3; return -1; }
        nf[i].height = (int)h; nf[i].ascent = (int)asc; nf[i].count = (int)cnt; nf[i].glyphs = (const FontGlyph *)(fx_buf + to); nf[i].bits = fx_buf + bo; nf[i].aa = (int)aa;
    }
    if (nfont < 4) nf[3] = nf[0];
    if (nfont < 7) { nf[4] = nf[0]; nf[5] = nf[2]; nf[6] = nf[2]; }
    font_small = nf[0]; font_large = nf[1]; font_bold = nf[2]; font_tiny = nf[3]; font_btn = nf[4]; font_head = nf[5]; font_title = nf[6];
    copy_str(fx_name, sizeof fx_name, name);
    return 0;
}
static void fx_apply_lang(void)                         /* 글꼴은 한 번 고르면 모든 언어가 따라감 */
{
    char t[64]; copy_str(t, sizeof t, fx_pref);
    if (!strcmp(t, fx_name)) return;
    if (!t[0] || fx_install(t)) fx_use_builtin();
}
static void fx_startup(void)                            /* 설정을 읽은 뒤 한 번 */
{
    if (!fx_seen) copy_str(fx_pref, sizeof fx_pref, "NotoSansKR.x8f");   /* 처음: 있으면 부드러운 글꼴 */
    fx_name[0] = 0; fx_use_builtin();
    fx_apply_lang();
}

/* PC 시계를 MSX RTC에 (2.14). force가 아니면 2초 넘게 어긋났을 때만 (멈춤·속도 조절 뒤) */
static void rtc_sync(int force)
{
    int t[6];
    if (!P.now || P.now(t)) return;
    uint32_t pc = msx_rtc_secs(t[0], t[1], t[2], t[3], t[4], t[5]);
    uint32_t cur = M->rtc_now + (uint32_t)((M->cpu.cycles >= M->rtc_ref ? M->cpu.cycles - M->rtc_ref : 0) / 3579545u);
    int32_t d = (int32_t)(pc - cur);
    if (force || !M->rtc_now || d > 1 || d < -1) msx_rtc_set(M, pc);
}
/* 3.4.0: 펌웨어 스위치를 켠 파나소닉 기종은 CTRL을 누른 채 켬 (디스크 롬이 가상 B 드라이브 몫을 잡지 않아야 내장 소프트웨어 작업 자리가 남음) */
static void fw_boot_ctrl(void) { if (boot_ctrl && ctrl_hold < CTRL_HOLD) ctrl_hold = CTRL_HOLD; if (fw_switch && msx_dev(M, MDEV_MATSU) && M->fw.kind == FW_PANASONIC && ctrl_hold < CTRL_HOLD) ctrl_hold = CTRL_HOLD; }
static void power_on(void)
{
    run_state = RUN_ON;
    auto_tried = auto_busy; auto_busy = 0; auto_stuck = 0; auto_age = 0;
    if (vhd_cart() >= 0) vhd_mount_all(1);              /* 3.7.1: 쓴 것을 USB에 반영하고 폴더를 다시 읽음 (MSX가 새로 부팅) */
    msx_reset(M);
    memset(snd_evt, 0, sizeof snd_evt);                 /* 3.7.1: 음원 아이콘은 리셋 뒤 다시 소리를 낼 때부터 */
    fw_boot_ctrl();
    input_init(&in);
    full_redraw();
}

/* 카트리지를 꽂거나 바꾼 뒤 (2.7.6, 요구 5·6)
 *  - 카트리지 슬롯 2(확장기 없음): 두 개를 꽂고 쓰는 경우를 위해 언제나 리셋
 *  - 그 밖(슬롯 1, 확장기 안 칸): "삽입 후 리셋"이 켜져 있으면 리셋,
 *    꺼져 있으면 정지 상태로 두고 사용자가 실행·리셋할 때까지 돌리지 않음 */
static void hd_eject(void);
static void sram_save(int n);
static void sram_load(int n);
#include "fe_extdev.inc"                               /* 5.2.86 특수 카트리지(외부장치) 꽂기·빼기 표 */
static int cart_big_owner = -1;                         /* 4.0.129: 2 MiB 넘는 롬 버퍼(cart_big)를 쓰는 슬롯 (-1 = 비어 있음) */
static void cart_remove(int n)                          /* 카트리지 빼기 (IDE 카트리지면 하드 디스크도) */
{
    sram_save(n);                                       /* 2.11: SRAM 바뀐 내용 먼저 */
    if (M->cart[n].present && CART_IS_IDE(M->cart[n].mapper)) hd_eject();
    ext_eject_all(n);                                   /* 외부장치별 빼기 정리 (fe_extdev.inc 표: MSX-AUDIO·Joyrex·외장 MSX-MIDI·Gfx9000·MoonSound) */
    msx_eject_cart(M, n);
    if (cart_big_owner == n) cart_big_owner = -1;
}

static char vhd_dir[6][64];                             /* 3.7.2: fe_vhd.inc 가상 하드 C~H 폴더 */
static int vhd_any(void) { for (int i = 0; i < 6; i++) if (vhd_dir[i][0]) return 1; return 0; }   /* 3.8.2: 하드 이미지와 가상 하드는 둘 중 하나만 */
static void open_vhd_browser(int drv);
static void after_cart_insert(int n)
{
    int forced = n == 1 && !slot_expand;
    if (reset_after_insert || forced) power_on();
    else { run_state = RUN_STOPPED; full_redraw(); }
}

static void recent_open(int k, int i);
static void recent_save(char *b);
static void recent_load(const char *k, const char *v);
static int about_len;                                   /* 4.0: 정보 창 본문 (0 = 다시 만듦) */
static uint32_t help_render(uint32_t X, uint32_t W, uint32_t top, uint32_t bottom, int scroll, const char *s, int draw);
static int pq_act, pq_sel;                              /* 4.0: 끝내기·재시작 확인 (dialog 15) */
static void do_action(int a)
{
    if (a >= A_RECENT && a < A_RECENT + 8 * 8) { menu_close_all(); recent_open((a - A_RECENT) / 8, (a - A_RECENT) % 8); return; }
    if (a >= A_TAPE_POS && a < A_TAPE_POS + TAPE_POS_N) { menu_close_all(); tape_scan(); if (M->tape && a - A_TAPE_POS < tape_nfile) M->tape_pos = tape_fpos[a - A_TAPE_POS]; return; }
    if (a >= A_SPECIAL && a < A_SPECIAL + 128 * MSX_CART_SLOTS) { insert_special((a - A_SPECIAL) / 128, (a - A_SPECIAL) % 128); return; }
    switch (a) {
    case A_NOT_YET: toast(T(S_NOT_YET), 0); break;
    case A_RUN_PAUSE:
        if (run_state == RUN_STOPPED) power_on();
        else run_state = run_state == RUN_ON ? RUN_PAUSED : RUN_ON;
        margin_bd = -1;
        break;
    case A_HARD_RESET: if (M->tape) M->tape_pos = 0; power_on(); break;   /* 4.0.34: 다시 켜면 테이프도 처음부터 (RUN"CAS:" 장치 오류) */
    case A_RESET_CTRL: if (M->tape) M->tape_pos = 0; power_on(); ctrl_hold = CTRL_HOLD; break;   /* 3.2.0: 디스크 롬이 키를 볼 때까지 (약 9초) */
    case A_SOFT_RESET: run_state = RUN_ON; msx_soft_reset(M); input_init(&in); full_redraw(); break;
    case A_FULL_RESET:
        for (int n = 0; n < MSX_CART_SLOTS; n++) { cart_remove(n); cart_name[n][0] = 0; cart_path[n][0] = 0; }
        power_on(); settings_changed(); break;
    case A_VOL_UP: vol_undo = volume; muted_undo = muted; volume = volume + 2 > 100 ? 100 : volume + 2; muted = 0; settings_changed(); vol_osd(); break;   /* 3.7.2: LCtrl+↑ 2%씩 */
    case A_VOL_DOWN: vol_undo = volume; muted_undo = muted; volume = volume - 2 < 0 ? 0 : volume - 2; settings_changed(); vol_osd(); break;
    case A_MUTE: muted = !muted; settings_changed(); vol_osd(); break;
    case A_SPEED_NORMAL: speed = 100; speed_max = 0; speed_osd(); break;
    case A_SPEED_UP: speed = speed + 5 > 1000 ? 1000 : speed + 5; speed_osd(); break;
    case A_SPEED_DOWN: speed = speed - 5 < 10 ? 10 : speed - 5; speed_osd(); break;
    case A_SPEED_MAX: speed_max = !speed_max; speed_osd(); { char t[64]; copy_str(t, sizeof t, T(S_SPEED_MAX)); str_append(t, sizeof t, speed_max ? ": ON" : ": OFF"); toast(t, 0); } break;   /* 켜고 끌 때 알림 */
    case A_SPEED_2X: case A_SPEED_4X: speed = a == A_SPEED_2X ? 200 : 400; speed_max = 0; turbo_hold = 0; speed_osd(); break;   /* 3.6.0 */
    case A_MON_COLOR: case A_MON_BW: case A_MON_GREEN: case A_MON_AMBER:
        monitor_kind = a - A_MON_COLOR; recompute_palette(); if (P.set_palette) P.set_palette(); full_redraw(); settings_changed(); break;
    case A_SCANLINE_TOGGLE: scanlines = scanlines ? 0 : scan_last; if (scanlines) scan_last = scanlines; full_redraw(); settings_changed(); break;   /* 가운데 아래에 잠깐 */
    case A_TURBO_HOLD: turbo_hold = 1; speed_osd(); break;
    case A_CART1_INSERT: open_browser(0); break;
    case A_CART2_INSERT: open_browser(1); break;
    case A_CART3_INSERT: open_browser(2); break;
    case A_CART4_INSERT: open_browser(3); break;
    case A_CART5_INSERT: open_browser(4); break;      /* 3.6.7 */
    case A_CART1_EJECT: case A_CART2_EJECT: case A_CART3_EJECT: case A_CART4_EJECT: case A_CART5_EJECT: {
        int n = a == A_CART1_EJECT ? 0 : a == A_CART2_EJECT ? 1 : a == A_CART3_EJECT ? 2 : a == A_CART4_EJECT ? 3 : 4;
        if (M->cart[n].present && CART_IS_IDE(M->cart[n].mapper)) hd_eject();   /* IDE 카트리지와 함께 */
        cart_remove(n);                                 /* 2.16: MSX-AUDIO·Joyrex도 끔, SRAM 저장 */
        cart_name[n][0] = 0;
        cart_path[n][0] = 0;
        cart_special[n] = 0;
        settings_changed();
        if (reset_after_insert) power_on(); else margin_bd = -1;
        break;
    }
    case A_STATE_LOAD: open_state_list(); break;
    case A_VIDEO_RESET: vid_bright = 0; vid_contrast = 100; vid_satur = 100; vid_gamma = 100; recompute_palette(); if (P.set_palette) P.set_palette(); full_redraw(); settings_changed(); break;
    case A_DISKA_NEXT: case A_DISKB_NEXT: disk_next(a == A_DISKB_NEXT); break;
    case A_QUICK_LOAD: case A_QUICK_SAVE: case A_STATE_SAVE: case A_SCREENSHOT:
        /* 2.10.1: USB에 오래 쓰는 동작은 키를 모두 놓은 뒤에 (누른 채로 막히면 키보드 자동 반복이 넘쳐 뗌 신호를 잃음) */
        if (!pend_act) { pend_act = a; pend_since = P.ms(); }
        break;
    case A_VID_REC: vrec_toggle(); break;
    case A_SP_MENU1: case A_SP_MENU2: menu_open_special(a == A_SP_MENU2); break;   /* 3.2.14 */
    case A_TAPE_POSMENU: menu_open_tpos(); break;
    case A_AUDIO_REC:
        if (!rec_on) rec_toggle();
        else if (!pend_act) { pend_act = a; pend_since = P.ms(); }
        break;
    case A_EXIT: case A_REBOOT:                           /* 4.0: 먼저 물어봄 (확인 창 = dialog 15) */
        pq_act = a; pq_sel = 0; open_dialog(15); break;
    case A_EXIT_NOW: bgw_finish(); silence_all(); vhd_sync_all(); prn_flush(0); midi_flush(); rec_stop(); vrec_stop(); sram_save_all(); resume_save(); disk_flush(0); disk_flush(1); tape_flush(); hd_flush(); settings_flush(); if (P.poweroff) P.poweroff(); else P.reboot(); break;
    case A_REBOOT_NOW: bgw_finish(); silence_all(); vhd_sync_all(); prn_flush(0); midi_flush(); rec_stop(); vrec_stop(); sram_save_all(); resume_save(); disk_flush(0); disk_flush(1); tape_flush(); hd_flush(); settings_flush(); P.reboot(); break;
    case A_DIAG: open_diag(9); break;               /* 요약 탭부터 */
    case A_SOUND_DIAG: open_diag(2); break;   /* 3.0.2: 진단의 소리 탭 */
    case A_MACHINE_EDIT: open_machines_edit(); break;
    case A_SC_EDIT: open_dialog(8); break;
    case A_KEYMAP_EDIT: open_dialog(14); break;
    case A_VHD_C: case A_VHD_D: case A_VHD_E: case A_VHD_F: case A_VHD_G: case A_VHD_H: open_vhd_browser(a - A_VHD_C); break;   /* 3.7.2 */           /* 3.2.12 KBD-02 */
    case A_USB_GROW: bgw_finish(); if (vrec_on) { vrec_on = 0; margin_bd = -1; } open_dialog(4); break;   /* 3.2.13: 확장은 영상 버퍼 자리를 씀 → 녹화 버림 */
    case A_MACHINE: open_machines(); break;
    case A_MEDIA: pop_media(); break;          /* 3.5.0 */
    case A_PROPS_EMU: open_props(0); break;
    case A_TAPE_INSERT: if (tape_slot()) open_browser(12); break;   /* 카세트 단자 없는 기종 (turbo R) */
    case A_TAPE_EJECT: tape_eject(); break;
    case A_HD_INSERT: open_browser(13); break;
    case A_HD_EJECT: hd_eject_unit(0); hd_auto_cart_remove(0); settings_changed(); margin_bd = -1; if (hd_reset_eject) power_on(); break;
    case A_HD2_INSERT: open_browser(17); break;          /* 3.4.3 DEV-15 */
    case A_HD2_EJECT: hd_eject_unit(1); settings_changed(); margin_bd = -1; if (hd_reset_eject) power_on(); break;
    case A_TAPE_REWIND: if (M->tape) M->tape_pos = 0; break;
    case A_TAPE_NEW: if (tape_slot()) tape_new(); break;
    case A_BAS_CONV: bas_open(); break;
    case A_PRN_POP0: case A_PRN_POP1: case A_PRN_POP2: prn_preview = a - A_PRN_POP0; settings_changed(); break;   /* 4.0.6 */
    case A_PRN_TEAR: prn_flush(2); pv_valid = 0; pv_last_len = 0; prn_forget(); toast(T(S_PRN_TORN), 0); break;
    case A_PRN_VIEW:                                    /* 4.0.5: 라인 프린터라 쪽 끝이 따로 없음 — 지금까지 나온 용지(저장 전)나 마지막으로 저장한 용지를 언제든 */
        if (prn_mode && prn_len) pv_open(prn_buf, prn_len, T(S_PV_LIVE));
        else if (pv_valid && pv_last_len) pv_open(prn_buf, pv_last_len, pv_last_name);
        else toast(T(S_PRN_NONE), 0);
        break;
    case A_PRN_OFF: case A_PRN_TEXT: prn_set_mode(a - A_PRN_OFF); break;   /* 3.3.0 */
    case A_PRN_RAW:                                     /* 4.0.34: 누를 때마다 켜기 / 끝내기 (안내 글) */
        if (prn_mode == 2) { prn_set_mode(0); toast(T(S_PRN_RAW_END), 0); }
        else { prn_set_mode(2); toast(T(S_PRN_RAW_ON), 0); }
        break;
    case A_PRN_END: prn_flush(1); break;
    case A_LANG_MENU: menu_open_lang(); break;
    case A_LANG_WIN: open_langwin(); break;
    case A_STEREO: snd_stereo = !snd_stereo; mix_apply(); settings_changed(); toast(T(snd_stereo ? S_STEREO_ON : S_MONO), 0); break;   /* 3.3.3 */
    case A_SPR_TOGGLE: spr_hide = !spr_hide; M->vdp.spr_hide = (uint8_t)spr_hide; settings_changed(); { char t[64]; copy_str(t, sizeof t, T(S_SPR_SHOW)); str_append(t, sizeof t, spr_hide ? ": OFF" : ": ON"); toast(t, 0); } break;   /* 3.3.1 VID-03 */
    case A_ICONS_TOGGLE:                                /* 보이기(3초) → 계속 보이기 → 숨기기 → 보이기 */
        if (icons_off) { icons_off = 0; icons_always = 0; } else if (!icons_always) icons_always = 1; else { icons_always = 0; icons_off = 1; }
        margin_bd = -1; full_redraw(); settings_changed(); toast(T(icons_off ? S_ICONS_OFF : icons_always ? S_ICONS_ALWAYS : S_ICONS_ON), 0); break;
    case A_DISKA_DIR: case A_DISKB_DIR: open_browser(a == A_DISKB_DIR ? 19 : 18); break;   /* 3.6.1 */
    case A_DISKA_INSERT: case A_DISKB_INSERT:
        if (disk_drives() < (a == A_DISKB_INSERT ? 2 : 1)) { toast(T(S_NO_FDC), 0); break; }
        open_browser(a == A_DISKA_INSERT ? 10 : 11); break;
    case A_DISKA_EJECT: case A_DISKB_EJECT: disk_eject(a == A_DISKB_EJECT); break;
    case A_DISKA_WP: case A_DISKB_WP: {
        int d = a == A_DISKB_WP;
        disk_wp[d] = !disk_wp[d];
        M->fdc.drv[d].wp = disk_wp[d];
        settings_changed();
        break;
    }

    case A_PROPS_VIDEO: open_props(1); break;
    case A_PROPS_EFFECTS: open_props(2); break;
    case A_PROPS_SOUND: open_props(3); break;
    case A_PROPS_MIDI: open_props(5); break;
    case A_PROPS_OUTPUT: open_props(4); break;      /* 4.0.134 */
    case A_PROPS_MIX: open_props(6); break;
    case A_MIDI_MIX: open_midimix(); break;              /* 5.2.53 */
    case A_PROPS_KB: open_props(7); break;
    case A_NET: open_net(-1); break;
    case A_NET_WIFI: open_net(1); break;                 /* 무선·유선 네트워크 메뉴를 따로 */
    case A_NET_WIRED: open_net(0); break;
    case A_POP_JOY: open_props(8); break;
    case A_POP_MOUSE: open_props(9); break;
    case A_RESET_AFTER_INSERT: reset_after_insert = !reset_after_insert; settings_changed(); break;
    case A_VHD_RESET_INS: vhd_reset_insert = !vhd_reset_insert; settings_changed(); break;
    case A_HD_RESET_INS: hd_reset_insert = !hd_reset_insert; settings_changed(); break;
    case A_HD_RESET_EJECT: hd_reset_eject = !hd_reset_eject; settings_changed(); break;
    case A_PANA_FRONT: fw_toggle(); margin_bd = -1; break;
    case A_PANA_PAUSE: pana_pause = !pana_pause; settings_changed(); margin_bd = -1; break;
    case A_DISK_RESET_AFTER: disk_reset_after_insert = !disk_reset_after_insert; settings_changed(); break;
    case A_ABOUT: about_len = 0; open_dialog(3); break;
    case A_IMAGE_MAKE: open_image_dialog(); break;     /* 3.0.2 */
    case A_HELP: open_help(); break;
    case A_DISK_COPY: open_disk_copy(); break;
    case A_LANG_KO: lang = 0; fx_apply_lang(); full_redraw(); settings_changed(); break;
    case A_LANG_EN: lang = 1; fx_apply_lang(); full_redraw(); settings_changed(); break;
    case A_LANG_JA: lang = 2; fx_apply_lang(); full_redraw(); settings_changed(); break;
    case A_LANG_DE: case A_LANG_FR: case A_LANG_ES: case A_LANG_SV: case A_LANG_RU: case A_LANG_PT: case A_LANG_ET:
        lang = 3 + (a - A_LANG_DE); fx_apply_lang(); full_redraw(); settings_changed(); break;
    case A_LANG_AR: lang = 10; fx_apply_lang(); full_redraw(); settings_changed(); break;   /* 3.4.3 */
    default: break;
    }
}

/* 메뉴 항목 상태: -1 숨김, 0 흐림(고를 수 없음), 1 보통, 2 체크 */
static int sp_has_rom(int kind);
static int action_state(int a)
{
    if (a >= A_SPECIAL && a < A_SPECIAL + 128 * MSX_CART_SLOTS && !sp_has_rom((a - A_SPECIAL) % 128)) return 0;   /* 3.2.14 MED-10: 롬이 없으면 흐리게 */
    switch (a) {
    case A_NOT_YET: return 0;
    case A_MUTE: return muted ? 2 : 1;
    case A_SPEED_MAX: return speed_max ? 2 : 1;
    case A_SPEED_NORMAL: return speed == 100 && !speed_max ? 2 : 1;
    case A_SPEED_2X: return speed == 200 && !speed_max ? 2 : 1;
    case A_SPEED_4X: return speed == 400 && !speed_max ? 2 : 1;
    case A_MON_COLOR: case A_MON_BW: case A_MON_GREEN: case A_MON_AMBER: return monitor_kind == a - A_MON_COLOR ? 2 : 1;
    case A_SCANLINE_TOGGLE: return scanlines ? 2 : 1;
    case A_RUN_PAUSE: return run_state == RUN_PAUSED ? 2 : 1;
    case A_LANG_KO: return lang == 0 ? 2 : 1;
    case A_LANG_EN: return lang == 1 ? 2 : 1;
    case A_LANG_JA: return lang == 2 ? 2 : 1;
    case A_LANG_DE: case A_LANG_FR: case A_LANG_ES: case A_LANG_SV: case A_LANG_RU: case A_LANG_PT: case A_LANG_ET:
        return lang == 3 + (a - A_LANG_DE) ? 2 : 1;
    case A_LANG_AR: return lang == 10 ? 2 : 1;
    case A_SCALE_AUTO: return 2;
    case A_USB_GROW: return P.grow_query ? 1 : 0;
    case A_VID_REC: return !P.vbuf ? 0 : vrec_on ? 2 : 1;
    case A_PRN_POP0: case A_PRN_POP1: case A_PRN_POP2: return prn_preview == a - A_PRN_POP0 ? 2 : 1;
    case A_PRN_OFF: case A_PRN_TEXT: case A_PRN_RAW: return prn_mode == a - A_PRN_OFF ? 2 : 1;
    case A_PRN_END: return prn_mode ? 1 : 0;
    case A_RESET_AFTER_INSERT: return reset_after_insert ? 2 : 1;
    case A_VHD_RESET_INS: return vhd_reset_insert ? 2 : 1;
    case A_HD_RESET_INS: return hd_reset_insert ? 2 : 1;
    case A_HD_RESET_EJECT: return hd_reset_eject ? 2 : 1;
    case A_PANA_FRONT: return fw_switch ? 2 : 1;
    case A_PANA_PAUSE: return pana_pause ? 2 : 1;
    case A_DISK_RESET_AFTER: return disk_reset_after_insert ? 2 : 1;
    case A_INFO_CART1: case A_INFO_CART2: case A_INFO_VOLUME: case A_INFO_MACHINE: case A_INFO_DISKA: case A_INFO_DISKB: case A_INFO_TAPE: return 3;
    case A_TAPE_EJECT: case A_TAPE_REWIND: return M->tape ? 1 : 0;
    case A_TAPE_INSERT: case A_TAPE_NEW: return tape_slot() ? 1 : 0;   /* 5.2.36 */
    case A_HD_INSERT: return P.hd_open && !vhd_any() ? 1 : 0;      /* 3.8.2: 가상 하드가 잡혀 있으면 이미지 삽입 불가 */
    case A_HD_EJECT: return hd_secs && !vhd_any() ? 1 : 0;
    case A_HD2_INSERT: return P.hd_open && hd_secs && !vhd_any() ? 1 : 0;   /* 마스터가 있어야 */
    case A_HD2_EJECT: return hd2_secs ? 1 : 0;
    case A_INFO_HD: case A_INFO_HD2: return vhd_any() ? 0 : 3;   /* 3.9.10: 가상 하드를 쓰는 동안은 비활성 */
    case A_VHD_C: case A_VHD_D: case A_VHD_E: case A_VHD_F: case A_VHD_G: case A_VHD_H: return (hd_secs || hd2_secs) && !vhd_dir[a - A_VHD_C][0] ? 0 : 1;   /* 3.8.2: 하드 이미지가 들어 있으면 가상 하드 연결 불가 */
    case A_DISKA_INSERT: case A_DISKB_INSERT: case A_DISKA_DIR: case A_DISKB_DIR: return M->fdc_kind ? 1 : 0;
    case A_DISKA_EJECT: case A_DISKA_NEXT: return M->fdc_kind && disk_size[0] ? 1 : 0;
    case A_DISKB_NEXT: return M->fdc_kind && disk_size[1] ? 1 : 0;
    case A_DISKB_EJECT: return M->fdc_kind && disk_size[1] ? 1 : 0;
    case A_DISKA_WP: return disk_wp[0] ? 2 : 1;
    case A_DISKB_WP: return disk_wp[1] ? 2 : 1;
    case A_CART1_EJECT: return M->cart[0].present ? 1 : 0;
    case A_CART2_EJECT: return M->cart[1].present ? 1 : 0;
    case A_CART3_EJECT: return M->cart[2].present ? 1 : 0;
    case A_CART4_EJECT: return M->cart[3].present ? 1 : 0;
    case A_CART5_EJECT: return M->cart[4].present ? 1 : 0;
    case A_INFO_CART3: case A_INFO_CART4: case A_INFO_CART5: return 3;
    default: return 1;
    }
}

/* ------------------------------------------------------------------ */
/* 메뉴 정의 (부록 B: blueMSX 구성)                                     */
/* ------------------------------------------------------------------ */
typedef struct Menu Menu;
typedef struct { int label; int action; const Menu *sub; const char *keys; } MenuItem;   /* label -1 = 구분선 */
struct Menu { int n; const MenuItem *items; };
#define SEP { -1, 0, 0, 0 }
#define MENU(name, ...) static const MenuItem name##_items[] = { __VA_ARGS__ }; \
    static const Menu name = { (int)(sizeof name##_items / sizeof name##_items[0]), name##_items }

/* 만든 기능만 넣는다. 버전이 올라갈 때마다 항목을 추가한다 (요구사항서 부록 B). */
/* 특수 카트리지 메뉴는 src/spcat.c 표에서 켤 때 만든다 (sp_menu_build). blueMSX+ "Insert Special" 과 같은 차례.
 * 글: 줄 = SPL_KIND + kind, 묶음 = SPL_GROUP + 묶음 번호 → item_text 가 sp_label() 로 바꿈 (StrId 가 있으면 번역, 없으면 표의 영어 이름) */
#define SPX(n, k) (A_SPECIAL + (n) * 128 + (k))
#define SPL_KIND  30000
#define SPL_GROUP 31000
#define SPL_NOTYET 32000                            /* 자리만 있는 줄 (kind 없음): 표 번호 */
#define SP_MENU_MAX 48                                   /* 28이면 맨 끝 줄(GFX9000·네트워크)이 잘려 나갔음 */
static MenuItem sp_mi[MSX_CART_SLOTS][SPG_N][SP_MENU_MAX];
static Menu sp_menu[MSX_CART_SLOTS][SPG_N];
#define m_special1 sp_menu[0][SPG_ROOT]
#define m_special2 sp_menu[1][SPG_ROOT]
#define m_special3 sp_menu[2][SPG_ROOT]
#define m_special4 sp_menu[3][SPG_ROOT]
#define m_special5 sp_menu[4][SPG_ROOT]
static void sp_menu_build(void)
{
    for (int n = 0; n < MSX_CART_SLOTS; n++)
        for (int g = 0; g < SPG_N; g++) {
            MenuItem *it = sp_mi[n][g]; int k = 0;
            for (int pos = 0; pos < 256 && k < SP_MENU_MAX - 1; pos++) {   /* 차례대로: 줄과 하위 묶음이 같은 번호 공간 */
                int sep = 0, hit = 0;
                for (int i = 0; i < spcat_count(); i++) {
                    const SpCart *d = spcat_at(i);
                    if (d->group != g || d->pos != pos) continue;
                    it[k].label = d->kind >= 0 ? SPL_KIND + d->kind : SPL_NOTYET + i;   /* 자리만 있는 줄은 표 번호로 */
                    it[k].action = d->how == SPK_NOT_YET || d->kind < 0 ? A_NOT_YET : SPX(n, d->kind);
                    it[k].sub = 0; it[k].keys = 0; k++; hit = 1; sep |= d->sep_after;
                }
                for (int h = 1; h < SPG_N && !hit; h++) {
                    const SpGroup *sg = &sp_groups[h];
                    if (sg->parent != g || sg->pos != pos || h == g) continue;
                    it[k].label = SPL_GROUP + h; it[k].action = 0; it[k].sub = &sp_menu[n][h]; it[k].keys = 0; k++; hit = 1; sep |= sg->sep_after;
                }
                if (hit && sep && k < SP_MENU_MAX - 1) { it[k].label = -1; it[k].action = 0; it[k].sub = 0; it[k].keys = 0; k++; }
            }
            if (k && it[k - 1].label == -1) k--;        /* 맨 끝 구분선은 뺌 */
            sp_menu[n][g].n = k; sp_menu[n][g].items = it;
        }
}
/* 줄·묶음 글: StrId 가 있으면 번역, 없으면 표의 영어 이름 */
static int sp_strid(int kind)
{
    switch (kind) {
    case 0: return S_DISKIF_NATIONAL; case 1: return S_DISKIF_PHILIPS; case 2: return S_SP_SCC; case 3: return S_SP_FMPAC; case 4: return S_SP_DOS2;
    case 5: return S_RAM_256K; case 6: return S_RAM_512K; case 7: return S_RAM_1M; case 8: return S_RAM_2M; case 9: return S_RAM_4M;
    case 10: return S_SP_SCCI; case 11: return S_SP_SUNRISE; case 12: return S_SP_AUDIO_FM; case 13: return S_SP_NEXTOR; case 14: return S_SP_JOYREX; case 15: return S_SP_PAC;
    case 16: return S_RAM_128K; case 17: return S_RAM_256K; case 18: return S_RAM_512K; case 19: return S_RAM_768K; case 20: return S_RAM_2M;
    case 21: return S_RAM_16K; case 22: return S_RAM_32K; case 23: return S_RAM_48K; case 24: return S_RAM_64K;
    case 25: return S_SP_AUDIO_PH; case 26: return S_SP_AUDIO_TO;
    case 27: return S_DISKIF_FSFD1A; case 28: return S_DISKIF_HBDF1; case 29: return S_DISKIF_VY0010; case 30: return S_DISKIF_NMS1200; case 31: return S_DISKIF_ML30DC; case 32: return S_DISKIF_HB3600; case 33: return S_DISKIF_DPF550;
    case 34: return S_SP_MOON; case 35: return S_RAM_128K; case 36: return S_RAM_256K; case 37: return S_RAM_512K; case 38: return S_RAM_1M; case 39: return S_SP_MFR;
    case 40: return S_SP_BEER; case 41: return S_SP_GIDE; case 42: return S_RAM_128K; case 43: return S_RAM_256K; case 44: return S_RAM_512K; case 45: return S_SP_GOUDA;
    case 46: return S_SP_AUDIO_CA1; case 47: return S_SP_SFG01; case 48: return S_SP_SFG05; case 49: return S_SP_VHD;
    case 50: case 54: return S_RAM_128K; case 51: case 55: return S_RAM_256K; case 52: case 56: return S_RAM_512K; case 53: case 57: return S_RAM_1M;
    case 58: return S_SP_V9990; case 59: return S_SP_X86NET; case 60: return S_SP_A16X; case 61: return S_SP_YAM; case 62: return S_SP_HBI55; case 63: return S_SP_MFRP; case 64: return S_SP_FRSCC; case 65: return S_SP_NMS1210; case 66: return S_SP_MSXMIDI; case 67: return S_SP_X86MIDI;
    default: return -1;
    }
}
static int spg_strid(int g)
{
    switch (g) {
    case SPG_ESESCC: return S_SP_ESESCC; case SPG_IDE: return S_SP_IDEG; case SPG_SCSI: return S_SP_SCSIG; case SPG_MEGASCSI: return S_SP_MEGASCSI; case SPG_WAVESCSI: return S_SP_WAVESCSI;
    case SPG_NOWIND: return S_SP_NOWIND; case SPG_EXTRAM: return S_SP_EXTRAM; case SPG_MEGARAM: return S_SP_MEGARAM; case SPG_ESERAM: return S_SP_ESERAM;
    case SPG_FLASHROM: return S_SP_MFR; case SPG_FLASHCART: return S_SP_FLASHG; case SPG_DISK: return S_SP_DISKG; case SPG_AUDIO: return S_SP_AUDIOG; case SPG_NET: return S_SP_NETG;
    default: return -1;
    }
}
static const char *sp_label(int label)
{
    if (label >= SPL_NOTYET) {
        const SpCart *d = spcat_at(label - SPL_NOTYET);
        if (!d) return "?";
        if (!strcmp(d->name, "Sony HBI-55")) return T(S_SP_HBI55);
        if (!strcmp(d->name, "Philips NMS1210 Serial Interface")) return T(S_SP_NMS1210);
        return d->name;
    }
    if (label >= SPL_GROUP) { int g = label - SPL_GROUP; int id = spg_strid(g); return id >= 0 ? T((StrId)id) : g < SPG_N ? sp_groups[g].name : "?"; }
    int k = label - SPL_KIND;
    int id = sp_strid(k); if (id >= 0) return T((StrId)id);
    const SpCart *d = spcat_kind(k); return d ? d->name : "?";
}
#define RECENT_MENU(name, k) MENU(name, { S_RECENT, A_RECENT + (k) * 8 + 0, 0, 0 }, { S_RECENT, A_RECENT + (k) * 8 + 1, 0, 0 }, \
     { S_RECENT, A_RECENT + (k) * 8 + 2, 0, 0 }, { S_RECENT, A_RECENT + (k) * 8 + 3, 0, 0 }, { S_RECENT, A_RECENT + (k) * 8 + 4, 0, 0 }, \
     { S_RECENT, A_RECENT + (k) * 8 + 5, 0, 0 }, { S_RECENT, A_RECENT + (k) * 8 + 6, 0, 0 }, { S_RECENT, A_RECENT + (k) * 8 + 7, 0, 0 })
RECENT_MENU(m_rc0, 0); RECENT_MENU(m_rc1, 1); RECENT_MENU(m_rc2, 2); RECENT_MENU(m_rc3, 3);
RECENT_MENU(m_rc4, 4); RECENT_MENU(m_rc5, 5); RECENT_MENU(m_rc6, 6); RECENT_MENU(m_rc7, 7);
MENU(m_cart1, { S_INSERT, A_CART1_INSERT, 0, "LCtrl+F7" }, { S_RECENT, 0, &m_rc0, 0 }, { S_INSERT_SPECIAL, 0, &m_special1, "LCtrl+LAlt+F7" }, { S_EJECT, A_CART1_EJECT, 0, "RCtrl+F7" },
     { S_RESET_AFTER_INSERT, A_RESET_AFTER_INSERT, 0, "LCtrl+LShift+F8" }, SEP, { S_CUR_ROM, A_INFO_CART1, 0, 0 });
MENU(m_cart2, { S_INSERT, A_CART2_INSERT, 0, "LCtrl+F8" }, { S_RECENT, 0, &m_rc1, 0 }, { S_INSERT_SPECIAL, 0, &m_special2, "LCtrl+LAlt+F8" }, { S_EJECT, A_CART2_EJECT, 0, "RCtrl+F8" },
     { S_RESET_AFTER_INSERT, A_RESET_AFTER_INSERT, 0, "LCtrl+LShift+F8" }, SEP, { S_CUR_ROM, A_INFO_CART2, 0, 0 });
MENU(m_cart3, { S_INSERT, A_CART3_INSERT, 0, 0 }, { S_RECENT, 0, &m_rc2, 0 }, { S_INSERT_SPECIAL, 0, &m_special3, 0 }, { S_EJECT, A_CART3_EJECT, 0, 0 },
     SEP, { S_CUR_ROM, A_INFO_CART3, 0, 0 });
MENU(m_cart4, { S_INSERT, A_CART4_INSERT, 0, 0 }, { S_RECENT, 0, &m_rc3, 0 }, { S_INSERT_SPECIAL, 0, &m_special4, 0 }, { S_EJECT, A_CART4_EJECT, 0, 0 },
     SEP, { S_CUR_ROM, A_INFO_CART4, 0, 0 });
MENU(m_cart5, { S_INSERT, A_CART5_INSERT, 0, 0 }, { S_RECENT, 0, &m_rc7, 0 }, { S_INSERT_SPECIAL, 0, &m_special5, 0 }, { S_EJECT, A_CART5_EJECT, 0, 0 },
     SEP, { S_CUR_ROM, A_INFO_CART5, 0, 0 });   /* 3.6.7 */
MENU(m_diska, { S_INSERT, A_DISKA_INSERT, 0, "LCtrl+F9" }, { S_INSERT_DIR, A_DISKA_DIR, 0, 0 }, { S_RECENT, 0, &m_rc4, 0 }, { S_DISK_NEXT, A_DISKA_NEXT, 0, "LAlt+F9" }, { S_EJECT, A_DISKA_EJECT, 0, "RCtrl+F9" }, { S_WRITE_PROTECT, A_DISKA_WP, 0, 0 },
     { S_RESET_AFTER_INSERT, A_DISK_RESET_AFTER, 0, "LCtrl+LAlt+F9" },
     SEP, { S_CUR_DISK, A_INFO_DISKA, 0, 0 });
MENU(m_diskb, { S_INSERT, A_DISKB_INSERT, 0, "LCtrl+F10" }, { S_INSERT_DIR, A_DISKB_DIR, 0, 0 }, { S_RECENT, 0, &m_rc5, 0 }, { S_DISK_NEXT, A_DISKB_NEXT, 0, 0 }, { S_EJECT, A_DISKB_EJECT, 0, "RCtrl+F10" }, { S_WRITE_PROTECT, A_DISKB_WP, 0, 0 },
     { S_RESET_AFTER_INSERT, A_DISK_RESET_AFTER, 0, "LCtrl+LAlt+F9" },
     SEP, { S_CUR_DISK, A_INFO_DISKB, 0, 0 });
MENU(m_tpos, { S_TAPE_POS, A_TAPE_POS + 0, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 1, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 2, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 3, 0, 0 },
     { S_TAPE_POS, A_TAPE_POS + 4, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 5, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 6, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 7, 0, 0 },
     { S_TAPE_POS, A_TAPE_POS + 8, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 9, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 10, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 11, 0, 0 },
     { S_TAPE_POS, A_TAPE_POS + 12, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 13, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 14, 0, 0 }, { S_TAPE_POS, A_TAPE_POS + 15, 0, 0 });
MENU(m_tape, { S_INSERT, A_TAPE_INSERT, 0, "LCtrl+F11" }, { S_RECENT, 0, &m_rc6, 0 }, { S_TAPE_NEW, A_TAPE_NEW, 0, "LCtrl+LShift+F11" }, { S_EJECT, A_TAPE_EJECT, 0, "RCtrl+F11" },
     { S_REWIND, A_TAPE_REWIND, 0, "LCtrl+LAlt+F11" }, { S_TAPE_POSG, 0, &m_tpos, "LCtrl+LAlt+F10" },
     SEP, { S_CUR_TAPE, A_INFO_TAPE, 0, 0 });
MENU(m_prn, { S_PRN_OFF, A_PRN_OFF, 0, 0 }, { S_PRN_TEXT, A_PRN_TEXT, 0, 0 }, { S_PRN_RAW, A_PRN_RAW, 0, 0 }, SEP, { S_PRN_END, A_PRN_END, 0, "LCtrl+LShift+P" }, SEP, { S_PRN_POP_OFF, A_PRN_POP0, 0, 0 }, { S_PRN_POP_PAGE, A_PRN_POP1, 0, 0 }, { S_PRN_POP_IDLE, A_PRN_POP2, 0, 0 }, SEP, { S_PRN_VIEW, A_PRN_VIEW, 0, 0 }, { S_PRN_TEAR, A_PRN_TEAR, 0, 0 });   /* 3.3.0 MENU3-01 */
MENU(m_hd_ins, { S_CUR_HD, A_HD_INSERT, 0, 0 }, { S_CUR_HD2, A_HD2_INSERT, 0, 0 }, SEP, { S_HD_RESET_INS, A_HD_RESET_INS, 0, 0 });   /* 3.8.2: 삽입 ▶ 마스터 / 슬레이브 */
MENU(m_hd_ej, { S_CUR_HD, A_HD_EJECT, 0, 0 }, { S_CUR_HD2, A_HD2_EJECT, 0, 0 }, SEP, { S_HD_RESET_EJECT, A_HD_RESET_EJECT, 0, 0 });
MENU(m_vhd, { S_VHD_TITLE, A_VHD_C, 0, 0 }, { S_VHD_TITLE, A_VHD_D, 0, 0 }, { S_VHD_TITLE, A_VHD_E, 0, 0 },
     { S_VHD_TITLE, A_VHD_F, 0, 0 }, { S_VHD_TITLE, A_VHD_G, 0, 0 }, { S_VHD_TITLE, A_VHD_H, 0, 0 }, SEP, { S_VHD_RESET_INS, A_VHD_RESET_INS, 0, 0 });   /* 3.9.9: 가상 하드 디스크 ▶ C~H, 삽입 후 리셋 */
MENU(m_hd, { S_INSERT, 0, &m_hd_ins, 0 }, { S_EJECT, 0, &m_hd_ej, 0 }, SEP,
     { S_CUR_HD, A_INFO_HD, 0, 0 }, { S_CUR_HD2, A_INFO_HD2, 0, 0 }, SEP,
     { S_VHD_MENU, 0, &m_vhd, 0 });   /* 3.7.2 가상 하드 C~H (3.9.9: 하위 메뉴) */
/* 3.5.0: 미디어 = 미디어 판과 같은 장치 메뉴 (키보드로는 RCtrl+RShift+D 또는 파일 → 미디어) */
MENU(m_media, { S_CART1, 0, &m_cart1, 0 }, { S_CART2, 0, &m_cart2, 0 }, { S_CART3, 0, &m_cart3, 0 }, { S_CART4, 0, &m_cart4, 0 }, { S_CART5, 0, &m_cart5, 0 }, SEP,
     { S_DISK_A, 0, &m_diska, 0 }, { S_DISK_B, 0, &m_diskb, 0 }, { S_HARDDISK, 0, &m_hd, 0 }, SEP, { S_CASSETTE, 0, &m_tape, 0 });   /* 3.7.3: 하드 → 줄 → 카세트 */
MENU(m_save, { S_QUICK_SAVE, A_QUICK_SAVE, 0, "F8" }, { S_CPU_SAVE, A_STATE_SAVE, 0, "LAlt+F8" });       /* 도구 막대 저장 단추 */
MENU(m_load, { S_QUICK_LOAD, A_QUICK_LOAD, 0, "F7" }, { S_CPU_LOAD, A_STATE_LOAD, 0, "LAlt+F7" });       /* 도구 막대 불러오기 단추 */
MENU(m_power, { S_FULL_RESET, A_FULL_RESET, 0, 0 }, { S_SOFT_RESET, A_SOFT_RESET, 0, 0 }, SEP, { S_REBOOT_PC, A_REBOOT, 0, "RCtrl+Pause" }, { S_EXIT, A_EXIT, 0, "LCtrl+Pause" });   /* 전원 단추 */
/* 3.5.0: 도구 막대·미디어 판에 있는 것(카트리지·디스크·카세트·하드 디스크 ▶, 빠른 저장·불러오기, 화면 저장)은 미디어 ▶과 막대로 */
/* 3.6.0: 도구 막대에 있는 것을 F11 메뉴에도 (마우스 없이 키보드로 모두) */
MENU(m_file,
     { S_OV_MEDIA, 0, &m_media, "RCtrl+RShift+D" }, SEP,
     { S_QUICK_SAVE, A_QUICK_SAVE, 0, "F8" }, { S_QUICK_LOAD, A_QUICK_LOAD, 0, "F7" },
     { S_CPU_SAVE, A_STATE_SAVE, 0, "LAlt+F8" }, { S_CPU_LOAD, A_STATE_LOAD, 0, "LAlt+F7" }, SEP,
     { S_SCREENSHOT, A_SCREENSHOT, 0, "PrtSc" }, { S_AUDIO_CAPTURE, A_AUDIO_REC, 0, "LShift+F7" }, { S_VID_REC, A_VID_REC, 0, "LShift+PrtSc" },
     { S_PRINTER, 0, &m_prn, 0 }, SEP,                  /* 3.7.0: 프린터는 영상 녹화 아래 */
     { S_BAS_MENU, A_BAS_CONV, 0, 0 }, SEP,
     { S_REBOOT_PC, A_REBOOT, 0, "RCtrl+Pause" },       /* 3.7.0: 실행에서 옮김 (끝내기 위) */
     { S_EXIT, A_EXIT, 0, "LCtrl+Pause" });
MENU(m_speed, { S_SPEED_NORMAL, A_SPEED_NORMAL, 0, 0 }, { S_OV_X2, A_SPEED_2X, 0, 0 }, { S_OV_X4, A_SPEED_4X, 0, 0 },
     { S_SPEED_MAX_ON, A_SPEED_MAX, 0, "LCtrl+LShift+M" }, SEP,
     { S_SPEED_UP, A_SPEED_UP, 0, "LShift+F12" }, { S_SPEED_DOWN, A_SPEED_DOWN, 0, "LShift+F11" });
MENU(m_run,
     { S_OV_MACHSEL, A_MACHINE, 0, 0 }, SEP,
     { S_RUN_PAUSE, A_RUN_PAUSE, 0, "F9" }, SEP,
     { S_HARD_RESET, A_HARD_RESET, 0, "F12" }, { S_SOFT_RESET, A_SOFT_RESET, 0, "LCtrl+F12" }, { S_FULL_RESET, A_FULL_RESET, 0, "LCtrl+LAlt+F12" }, { S_RESET_CTRL, A_RESET_CTRL, 0, "RCtrl+F12" });
MENU(m_sound, { S_SOUND_SET, A_PROPS_SOUND, 0, "RShift+F8" }, SEP,
     { S_VOL_UP, A_VOL_UP, 0, "LAlt+↑" }, { S_VOL_DOWN, A_VOL_DOWN, 0, "LAlt+↓" }, { S_MUTE, A_MUTE, 0, "LAlt+↑↓" }, SEP, { S_VOLUME, A_INFO_VOLUME, 0, 0 });
MENU(m_lang, { S_LANG_KO, A_LANG_KO, 0, 0 }, { S_LANG_EN, A_LANG_EN, 0, 0 }, { S_LANG_JA, A_LANG_JA, 0, 0 }, SEP,
     { S_LANG_DE, A_LANG_DE, 0, 0 }, { S_LANG_FR, A_LANG_FR, 0, 0 }, { S_LANG_ES, A_LANG_ES, 0, 0 }, { S_LANG_SV, A_LANG_SV, 0, 0 },
     { S_LANG_RU, A_LANG_RU, 0, 0 }, { S_LANG_PT, A_LANG_PT, 0, 0 }, { S_LANG_ET, A_LANG_ET, 0, 0 }, SEP, { S_LANG_AR, A_LANG_AR, 0, 0 });
MENU(m_netnone, { S_NET_NONE, 0, 0, 0 });   /* 랜카드가 없을 때 네트워크를 누르면 나오는 작은 메뉴 */
MENU(m_net, { S_NET_WIFI, A_NET_WIFI, 0, 0 }, { S_NET_WIRED, A_NET_WIRED, 0, 0 });   /* 옵션 > 네트워크 ▶ 무선 네트워크 / 유선 네트워크 */
MENU(m_options, { S_EMULATION, A_PROPS_EMU, 0, "RShift+F6" }, { S_M_NETWORK, 0, &m_net, 0 },   /* 에뮬레이션 바로 밑. 랜카드가 있으면 창, 없으면 "네트워크 없음" 작은 메뉴 */ { S_VIDEO, A_PROPS_VIDEO, 0, "RShift+F7" },
     { S_EFFECTS, A_PROPS_EFFECTS, 0, "RShift+F10" },   /* 3.6.1: 모니터 유형·스캔라인은 효과 창 안 항목으로 (메뉴로는 넣지 않음) */ { S_SOUND, 0, &m_sound, 0 }, { S_OUTPUT, A_PROPS_OUTPUT, 0, 0 }, { S_MIDI, A_PROPS_MIDI, 0, "RShift+F11" },
     SEP, { S_LANGUAGE, A_LANG_WIN, 0, "RCtrl+RShift+L" });   /* 탭 없이 항목마다 자기 창 */
MENU(m_tools, { S_MACHINE_EDITOR, A_MACHINE_EDIT, 0, "RCtrl+RShift+M" }, { S_SHORTCUT_EDITOR, A_SC_EDIT, 0, "RCtrl+RShift+S" }, { S_KEYMAP_EDITOR, A_KEYMAP_EDIT, 0, 0 }, SEP, { S_MIXER_MENU, A_PROPS_MIX, 0, "LCtrl+LAlt+M" }, { S_MIDI_MIXER_MENU, A_MIDI_MIX, 0, "LCtrl+F12" }, { S_INPUT_DEV, A_PROPS_KB, 0, "RCtrl+RShift+K" });   /* 4.0: 키보드·마우스·조이스틱을 입력장치 한 창(탭 3개)으로 */
MENU(m_system, { S_DIAG, A_DIAG, 0, "LCtrl+F6" }, { S_USB_GROW, A_USB_GROW, 0, 0 }, SEP, { S_IMAGE_MAKE, A_IMAGE_MAKE, 0, 0 }, { S_DISK_COPY, A_DISK_COPY, 0, 0 });
MENU(m_help, { S_HELP_MENU, A_HELP, 0, 0 }, { S_M_SYSTEM, 0, &m_system, 0 }, SEP, { S_ABOUT, A_ABOUT, 0, "RCtrl+F6" });   /* 3.0.2: 도움말, 시스템 ▶ */

/* 네트워크 메뉴는 네트워크 카드(Insert Special > Network(UNAPI) > X86Network)를 꽂았을 때만 막대에 나타난다 */
#define BAR_MAX 6
static int bar_n = 5;
static const Menu *bar_menus[BAR_MAX] = { &m_file, &m_run, &m_options, &m_tools, &m_help };
static StrId bar_label[BAR_MAX] = { S_M_FILE, S_M_RUN, S_M_OPTIONS, S_M_TOOLS, S_M_HELP };
static void bar_rebuild(int net)
{
    int n = 0;
    bar_menus[n] = &m_file; bar_label[n++] = S_M_FILE;
    bar_menus[n] = &m_run; bar_label[n++] = S_M_RUN;
    bar_menus[n] = &m_options; bar_label[n++] = S_M_OPTIONS;
    (void)net;                                          /* 네트워크 대메뉴는 두지 않는다 (옵션 > 네트워크, 카드를 꽂았을 때만 보임) */
    bar_menus[n] = &m_tools; bar_label[n++] = S_M_TOOLS;
    bar_menus[n] = &m_help; bar_label[n++] = S_M_HELP;
    bar_n = n;
}

/* 메뉴 상태 */
static int bar_sel;
static int mem_bar, mem_depth;                          /* 4.0.132·133: F11 메뉴를 다시 열면 직전에 열어 둔 메뉴와 고른 줄을 끝 단계까지 되살림 */
static const Menu *mem_menu[6]; static int mem_sel[6];
static int depth;                           /* 펼친 단계 (0 = 막대만) */
static const Menu *open_menu[6];                /* 3.5.0: 파일 → 미디어 → 카트리지 → 특수 → IDE (5단) */
static int open_sel[6];
static uint32_t open_x[6], open_y[6], open_w[6];
static uint32_t last_menu_key;
static int menu_mouse;                      /* 3.4.6: 마지막 메뉴 조작이 마우스 (포인터가 메뉴 밖에 0.8초 있으면 닫음) */
static uint32_t menu_out_ms;
static int dialog;                          /* 0 없음, 1 파일 선택, 2 진단, 3 정보, 4 USB 전체 용량 */
/* 메뉴에서 대화 창을 열었을 때의 메뉴 상태 (Esc로 돌아감) */
static int snap_valid, snap_bar, snap_depth, snap_sel[6];
static const Menu *snap_menu[6];
/* 팝업 보이는 방식 — 메뉴에서 눌러 연 팝업은 메뉴를 그대로 두고 그 위에 (Esc = 메뉴로), 단축키로 연 팝업은 팝업만 (Esc = 그냥 닫힘, 같은 단축키 = 닫힘).
 * 믹서는 어느 창·메뉴가 떠 있어도 먼저 열고, 닫으면 하던 곳(ui_ret)으로 돌아감 */
static int props_tab;                                   /* (정의는 아래, 앞선 선언) */
static int menu_under;                                  /* 1 = 팝업 밑에 메뉴 막대·펼친 메뉴를 그려 둠 */
static int popup_act = -1, popup_dlg, popup_tab;        /* 단축키로 연 팝업: 그 동작·창 종류·속성 탭 */
static int ui_ret_valid;
static void ui_ret_save(void);
static int ui_ret_restore(void);
static int mach_from_props;
static void dialog_back(void);
static void browse_remember(void);
static int browse_imgdir(void);

/* 항목에 보일 글자 (표시 전용 항목은 "제목: 값") */
static const char *item_text(const MenuItem *it, char *buf, int max)
{
    if (it->label >= SPL_KIND) return sp_label(it->label);   /* 5.2.44 특수 카트리지 표 */
    if (it->action >= A_TAPE_POS && it->action < A_TAPE_POS + TAPE_POS_N) {   /* 3.2.1: 테이프 안 파일 */
        int i = it->action - A_TAPE_POS;
        buf[0] = 0;
        if (i < tape_nfile) {
            str_append(buf, max, M->tape_pos >= tape_fpos[i] && (i + 1 >= tape_nfile || M->tape_pos < tape_fpos[i + 1]) ? "> " : "   ");
            str_append(buf, max, tape_fname[i]);
            str_append(buf, max, tape_ftype[i] == 0xD3 ? "  (BASIC)" : tape_ftype[i] == 0xEA ? "  (ASCII)" : tape_ftype[i] == 0xD0 ? "  (BIN)" : "  (DATA)");
        }
        return buf;
    }
    if (it->sub == &m_speed) {   /* 3.6.0: ▶ 옆에 지금 값 ("속도 · 100%") */
        buf[0] = 0;
        str_append(buf, max, T(it->label));
        str_append(buf, max, " \xC2\xB7 ");
        if (speed_max) str_append(buf, max, T(S_OV_XMAX));
        else { num_append(buf, max, (uint32_t)speed, 0, ' '); str_append(buf, max, "%"); }
        return buf;
    }
    if (it->action >= A_VHD_C && it->action <= A_VHD_H) {   /* 3.7.2: "C:  폴더" */
        int i = it->action - A_VHD_C;
        char dl[6] = { (char)('C' + i), ':', ' ', ' ', 0, 0 };
        copy_str(buf, max, dl);
        str_append(buf, max, vhd_dir[i][0] ? vhd_dir[i] : T(S_VHD_NONE));
        return buf;
    }
    if (it->action >= A_RECENT && it->action < A_RECENT + 8 * 8) {   /* 최근 파일: 이름 */
        int k = (it->action - A_RECENT) / 8, i = (it->action - A_RECENT) % 8;
        copy_str(buf, max, rc_name[recent_kind_of(k)][i]);
        return buf;
    }
    if (it->action != A_INFO_CART1 && it->action != A_INFO_CART2 && it->action != A_INFO_CART3 && it->action != A_INFO_CART4 && it->action != A_INFO_CART5 &&
        it->action != A_INFO_VOLUME && it->action != A_INFO_MACHINE &&
        it->action != A_INFO_DISKA && it->action != A_INFO_DISKB && it->action != A_INFO_TAPE && it->action != A_INFO_HD && it->action != A_INFO_HD2) return T(it->label);
    buf[0] = 0;
    str_append(buf, max, T(it->label));
    str_append(buf, max, ": ");
    if (it->action == A_INFO_MACHINE) str_append(buf, max, machine_name);
    else if (it->action == A_INFO_TAPE) {
        if (!M->tape) str_append(buf, max, T(S_NONE));
        else {
            str_append(buf, max, tape_name); str_append(buf, max, " (");
            num_append(buf, max, M->tape_pos, 0, ' '); str_append(buf, max, " / ");
            num_append(buf, max, M->tape_size, 0, ' '); str_append(buf, max, ")");
            if (tape_title[0]) { str_append(buf, max, "  "); str_append(buf, max, tape_title); }   /* 3.3.2 카세트 DB */
        }
    }
    else if (it->action == A_INFO_HD) str_append(buf, max, hd_secs && hd_name[0] ? hd_name : T(S_NONE));
    else if (it->action == A_INFO_HD2) str_append(buf, max, hd2_secs && hd2_name[0] ? hd2_name : T(S_NONE));
    else if (it->action == A_INFO_DISKA || it->action == A_INFO_DISKB) {
        const char *n = disk_name[it->action == A_INFO_DISKB];
        str_append(buf, max, disk_size[it->action == A_INFO_DISKB] && n[0] ? n : T(S_NONE));
    }
    else if (it->action == A_INFO_VOLUME) {
        if (muted) str_append(buf, max, T(S_MUTED));
        else { num_append(buf, max, (uint32_t)volume, 0, ' '); str_append(buf, max, "%"); }
    } else {
        const char *n = cart_name[it->action == A_INFO_CART1 ? 0 : it->action == A_INFO_CART2 ? 1 : it->action == A_INFO_CART3 ? 2 : it->action == A_INFO_CART4 ? 3 : 4];
        str_append(buf, max, n[0] ? n : T(S_NONE));
    }
    return buf;
}

static int item_state(const MenuItem *it);
static uint32_t item_h(const MenuItem *it)              /* 숨긴 항목(예: 확장 안 한 슬롯 3·4)은 높이 0 */
{
    if (it->label >= 0 && item_state(it) < 0) return 0;
    return it->label < 0 ? 11 * S : LH + 12 * S;
}
static int disk_drives(void)
{
    if (vhd_cart() >= 0) return 2;                      /* 3.7.1: 가상 디스크가 플로피 A·B도 맡음 */
    MsxMediaSlots ms; msx_media_slots(M, &ms);          /* 기종의 미디어 자리 (src/media.c) */
    return ms.disk;
}
static int tape_slot(void) { MsxMediaSlots ms; msx_media_slots(M, &ms); return ms.tape; }   /* 카세트 단자 (turbo R 없음) */

static int item_state(const MenuItem *it)
{
    if (it->label < 0) return 1;
    if (it->label == S_TAPE_POSG) { tape_scan(); return tape_nfile ? 1 : 0; }   /* 3.2.1 */
    if (it->action >= A_TAPE_POS && it->action < A_TAPE_POS + TAPE_POS_N) return it->action - A_TAPE_POS < tape_nfile ? 1 : -1;
    if (it->label == S_RECENT && it->sub) {             /* 최근 파일 ▶: 목록이 비면 흐리게 */
        int k = (it->sub->items[0].action - A_RECENT) / 8;
        return rc_path[recent_kind_of(k)][0][0] ? 1 : 0;
    }
    if (it->action >= A_RECENT && it->action < A_RECENT + 8 * 8) {
        int k = (it->action - A_RECENT) / 8, i = (it->action - A_RECENT) % 8;
        if (!rc_path[recent_kind_of(k)][i][0] || i >= recent_max) return -1;   /* 빈 줄·정한 수 넘는 줄은 숨김 */
        if (k >= 4 && k < 6 && disk_drives() < k - 3) return 0;
        return 1;
    }
    if (it->label == S_NET_WIFI && it->action == A_NET_WIFI) return net_has_wifi() && net_cart_present() ? 1 : -1;     /* 무선 장치가 있고 네트워크 카트리지가 꽂혀 있을 때만 보임 (뺐으면 메뉴도 사라짐) */
    if (it->label == S_NET_WIRED && it->action == A_NET_WIRED) return net_has_wired() && net_cart_present() ? 1 : -1;   /* 유선 장치(PCI·USB 랜)가 있고 카트리지가 꽂혀 있을 때만 */
    if (it->label == S_SP_NET && it->action == A_NET) return net_cart_present() ? 1 : -1;   /* 네트워크 카드(Insert Special > Network(UNAPI) > X86Network)를 꽂았을 때만 보임 */
    if (it->label == S_NET_NONE) return 0;               /* 흐리게 (안내만) */
    if (it->label == S_CART5) return M->cart_slot[4] >= 0 ? 1 : -1;   /* 3.6.7 */
    if (it->label == S_CART3 || it->label == S_CART4) return slot_expand || M->cart_slot[it->label == S_CART3 ? 2 : 3] >= 0 ? 1 : -1;   /* 3.3.1: 기종의 C·D */
    if (it->label == S_CASSETTE) return tape_slot() ? 1 : 0;   /* 없는 자리는 흐리게 */
    if (it->label == S_DISK_A) return disk_drives() >= 1 ? 1 : 0;
    if (it->label == S_DISK_B) return disk_drives() >= 2 ? 1 : 0;
    if (it->sub == &m_hd_ins) return action_state(A_HD_INSERT) || action_state(A_HD2_INSERT) ? 1 : 0;   /* 3.8.5: 가상 하드가 잡혀 있으면 삽입 ▶ 전체가 흐려지고 ▶ 도 안 열림 */
    if (it->sub == &m_hd_ej) return vhd_any() ? 0 : 1;   /* 3.9.0: 꺼낸 뒤 다시 시작 단추가 안에 있어 평소엔 늘 열림. 3.9.5: 가상 하드가 잡혀 있으면 삽입처럼 흐리게 */
    if (it->sub == &m_net) return (net_has_wifi() || net_has_wired()) && net_cart_present() ? 1 : -1;   /* 무선·유선 둘 다 없으면, 5.2.108: 카트리지가 없어도 네트워크 메뉴 자체가 안 나온다 */
    if (it->sub) return 1;
    return action_state(it->action);
}

static uint32_t bar_h(void) { return LH + 10 * S + (uint32_t)font_small.height; }   /* 3.7.2: 배지 밑 기종 이름 자리만큼 높게 */
/* 3.7.2: 기종 이름 (배지와 겹치는 "MSX - " 앞부분은 뺌) */
static const char *mach_short_name(void)
{
    const char *mn = machine_name;                      /* "MSX - ", "MSX2 - " 등 앞부분은 배지와 겹치므로 뺌 */
    if ((mn[0] | 32) == 'm' && (mn[1] | 32) == 's' && (mn[2] | 32) == 'x')
        for (const char *q = mn; q[0] && q[1] && q[2]; q++) if (q[0] == ' ' && q[1] == '-' && q[2] == ' ') { mn = q + 3; break; }
    return mn;
}
/* 3.7.2: 메뉴 막대 왼쪽 끝 기종 배지 = 네 색 줄 + 세대 배지, 그 밑에 기종 이름 (작은 글씨 한 줄, 넘치면 가로로 줄임) */
#define BB_NMAX (150 * S)
static uint32_t bb_icon_h(void) { return bar_h() - 4 * S - (uint32_t)font_small.height - 7 * S; }
static uint32_t bb_name_w(void)
{
    uint32_t w = text_f(&font_small, 1, 0, 0, mach_short_name(), 0, 0, 0, 0);
    return w > BB_NMAX ? BB_NMAX : w;
}
static uint32_t bar_badge_w(void)
{
    if (icons_off) return 0;
    uint32_t bw = 0;
    if (!badge_fit(200 * S, bb_icon_h(), &bw)) return 0;
    uint32_t iw = 4 * 3 * S + 6 * S + bw, nw = bb_name_w();
    return 8 * S + (iw > nw ? iw : nw) + 8 * S + 8 * S;      /* 마지막 8S = 메뉴와 사이 */
}
static void draw_bar_badge(uint32_t x)
{
    uint32_t bh = bar_h(), th = bb_icon_h(), bw = 0, w = bar_badge_w();
    if (!w || !badge_fit(200 * S, th, &bw)) return;
    uint32_t y = 2 * S, h = bh - 4 * S, cw = w - 8 * S;
    ui_rframe(x, y, cw, h, 6 * S, C_LINE, C_CARD);
    static const uint8_t band[4] = { C_BAD, C_WARN, C_OK, C_ACC };
    uint32_t iw = 4 * 3 * S + 6 * S + bw, bx = x + (cw - iw) / 2, by = y + 3 * S;
    for (int i = 0; i < 4; i++) ui_rrect(bx + (uint32_t)i * 3 * S, by, 2 * S, th, S, band[i]);
    draw_badge_at(bx + 4 * 3 * S + 6 * S, by, bw, th);
    uint32_t nw = bb_name_w();
    text_fit(&font_small, x + (cw - nw) / 2, by + th + S, mach_short_name(), C_TEXT, BB_NMAX);
    hit_add(hc_none, 0, 9, 0, 0, x, y, cw, h);
}
static uint32_t bar_x(int i)
{
    if (RTL) {                                          /* 3.4.4: 오른쪽부터 (i = BAR_N이면 맨 왼쪽 항목의 왼쪽 끝) */
        uint32_t r = D.w - 18 * S - bar_badge_w();     /* 3.7.2: 오른쪽 끝 = 기종 배지 */
        for (int k = 0; k < i && k < bar_n; k++) r -= text_w(T(bar_label[k])) + 28 * S;
        if (i >= bar_n) return r + 18 * S;
        uint32_t tw = text_w(T(bar_label[i]));
        return r > tw ? r - tw : 0;
    }
    uint32_t x = 18 * S + bar_badge_w();                /* 3.7.2: 왼쪽 끝 = 기종 배지 */
    for (int k = 0; k < i; k++) x += text_w(T(bar_label[k])) + 28 * S;
    return x;
}

/* 3.0 마우스: 메뉴 막대·드롭다운 */
static void open_level(const Menu *m);
static void menu_key(int key);
static void hc_bar(const UiHit *h, int ev)
{
    int i = h->b;
    last_menu_key = P.ms();
    if (ev == 0) {                                      /* 펼친 채로 다른 메뉴를 가리키면 그 메뉴로 */
        if (depth > 0 && bar_sel != i) { bar_sel = i; depth = 0; full_redraw(); open_level(bar_menus[i]); }
        else if (!depth && bar_sel != i) { bar_sel = i; menu_dirty = 1; }
        return;
    }
    if (depth > 0 && bar_sel == i) { depth = 0; full_redraw(); return; }   /* 다시 누르면 접기 */
    bar_sel = i; depth = 0; full_redraw(); open_level(bar_menus[i]);
}
static void hc_mitem(const UiHit *h, int ev)
{
    int d = (int)h->a, i = h->b;
    if (d >= depth) return;
    last_menu_key = P.ms();
    const MenuItem *it = &open_menu[d]->items[i];
    if (depth > d + 1 && (open_sel[d] != i || ev == 1)) { depth = d + 1; full_redraw(); }
    if (open_sel[d] != i) {
        open_sel[d] = i; menu_dirty = 1;
        if (ev == 0 && it->sub && item_state(it) > 0) open_level(it->sub);   /* 하위 메뉴는 가리키면 펼침 */
    }
    if (ev == 1) { depth = d + 1; menu_key(0x1C); }
}

static void draw_bar(void)
{
    uint32_t bh = bar_h();
    fe_fill(0, 0, D.w, bh, C_HEAD);
    fe_fill(0, bh - S, D.w, S, C_LINE);
    draw_bar_badge(RTL ? D.w - bar_badge_w() : 8 * S);   /* 3.7.2: 기종 배지 (메뉴와 함께) */
    for (int i = 0; i < bar_n; i++) {
        uint32_t x = bar_x(i), w = text_w(T(bar_label[i])) + 20 * S;
        int sel = i == bar_sel;
        if (sel) { ui_rrect(x - 10 * S, 3 * S, w, bh - 6 * S, 5 * S, depth ? C_ROWSEL : C_ACC2); fe_fill(x - 4 * S, bh - 3 * S, w - 12 * S, 2 * S, C_ACC); }
        hit_add(hc_bar, 0, i, 0, 0, x - 10 * S, 0, w, bh);
        text_clip(x, ui_ty(0, bh), T(bar_label[i]), sel ? 15 : C_TEXT, 0xFF, 0);
    }
    /* 오른쪽: X86MSX 로고 (3.0.11 — 키 안내 대신). 둥근 타일 안에 MSX 네 색 줄 + "X86"(강조색)"MSX"(흰색, 두껍게) + 버전 */
    {
        uint32_t th = bh - 10 * S, ty = 5 * S, r = th / 4;
        uint32_t ww = text_w("X86") + text_w("MSX") + S, vw = ui_sw(MSXEMU_VERSION);
        uint32_t tot = th + 10 * S + ww + 8 * S + vw;
        uint32_t right = D.w - 12 * S, lx = right > tot ? right - tot : 0;
        if (RTL) lx = 12 * S;                           /* 3.4.4: 로고는 왼쪽 */
        if (RTL ? lx + tot + 16 * S < bar_x(bar_n) : lx > bar_x(bar_n) + 16 * S) {
            ui_rframe(lx, ty, th, th, r, C_LINE, C_CARD);
            static const uint8_t band[4] = { C_BAD, C_WARN, C_OK, C_ACC };
            uint32_t bw = th / 7 ? th / 7 : 1, gap = (th - 4 * bw) / 5, bhh = th * 3 / 5, by = ty + (th - bhh) / 2;
            for (int i = 0; i < 4; i++) ui_rrect(lx + gap + (uint32_t)i * (bw + gap), by, bw, bhh, bw / 2, band[i]);
            uint32_t tx = lx + th + 10 * S, yy = ui_ty(0, bh);
            for (uint32_t b = 0; b < 2; b++) text_clip(tx + b * S, yy, "X86", C_ACC, 0xFF, 0);       /* 두껍게: 한 점 옆에 한 번 더 */
            uint32_t mx = tx + text_w("X86") + S;
            for (uint32_t b = 0; b < 2; b++) text_clip(mx + b * S, yy, "MSX", C_TEXT, 0xFF, 0);
            text_f(SF, 1, mx + text_w("MSX") + 8 * S + S, ui_sty(0, bh), MSXEMU_VERSION, C_MUTED, 0xFF, 0, 1);
        }
    }
}

/* 메뉴 오른쪽 단축키: 지금 단축키 표에서 (2.12: 단축키 편집기로 바꾸면 메뉴에도) */
static const char *menu_keys_for(int action, char *out, int max);

static int menu_scroll[6];                              /* 3.6.10: 화면보다 긴 메뉴의 첫 항목 (위에 숨긴 수) */
static int menu_avail_h(void) { return (int)D.h - (int)(4 * S); }
static uint32_t menu_arrow_h(void) { return LH + 4 * S; }
static void layout_menu(int d)
{
    const Menu *m = open_menu[d];
    uint32_t lw = 0, kw = 0;
    for (int i = 0; i < m->n; i++) {
        const MenuItem *it = &m->items[i];
        if (it->label < 0) continue;
        char tb[128];
        uint32_t w = text_w(item_text(it, tb, sizeof tb));
        if (w > D.w / 2) w = D.w / 2;                   /* 긴 롬 이름은 잘라서 */
        if (w > lw) lw = w;
        char kb[48];
        const char *ks = it->sub ? 0 : menu_keys_for(it->action, kb, sizeof kb);
        if (ks) { uint32_t k = ui_sw(ks) + 12 * S; if (k > kw) kw = k; }
    }
    open_w[d] = LH + 14 * S + lw + (kw ? kw + 32 * S : 0) + LH + 20 * S;
    if (d == 0 && pop_mode) { open_x[0] = pop_ralign ? (pop_x > open_w[0] ? pop_x - open_w[0] : 0) : pop_x; open_y[0] = pop_y; }   /* 3.5.0 막대 팝업 */
    else if (d == 0) { open_x[0] = bar_x(bar_sel) - 10 * S; open_y[0] = bar_h() + 2 * S; }
    else {
        uint32_t y = open_y[d - 1] + 6 * S + (menu_scroll[d - 1] ? menu_arrow_h() : 0);   /* 3.6.10: 위로 밀린 메뉴 */
        for (int i = menu_scroll[d - 1]; i < open_sel[d - 1]; i++) y += item_h(&open_menu[d - 1]->items[i]);
        open_x[d] = open_x[d - 1] + open_w[d - 1] - 6 * S;
        if (pop_mode && pop_ralign) open_x[d] = open_x[d - 1] + 6 * S > open_w[d] ? open_x[d - 1] + 6 * S - open_w[d] : 0;   /* 3.5.0: 오른쪽 판에서 연 팝업은 하위 메뉴를 왼쪽으로 */
        open_y[d] = y - 6 * S;
    }
    if (RTL && !(d == 0 && pop_mode)) {                 /* 3.4.4: 막대 항목의 오른쪽 끝에 맞추고, 하위 메뉴는 왼쪽으로 */
        uint32_t r = d == 0 ? bar_x(bar_sel) + text_w(T(bar_label[bar_sel])) + 10 * S : open_x[d - 1] + 6 * S;
        open_x[d] = r > open_w[d] ? r - open_w[d] : 0;
    }
    if (open_x[d] + open_w[d] > D.w) open_x[d] = D.w > open_w[d] ? D.w - open_w[d] : 0;
}

static void hc_mscroll(const UiHit *h, int ev)          /* 3.6.10: 긴 메뉴의 ▲·▼ 줄 (가리키거나 누르면 한 칸) */
{
    int d = (int)h->a;
    if (d >= depth) return;
    const Menu *m = open_menu[d];
    int i = open_sel[d];
    if (h->b < 0) { do i--; while (i > 0 && (m->items[i].label < 0 || item_h(&m->items[i]) == 0)); if (i < 0) i = 0; }
    else { do i++; while (i < m->n - 1 && (m->items[i].label < 0 || item_h(&m->items[i]) == 0)); if (i >= m->n) i = m->n - 1; }
    (void)ev;
    if (depth > d + 1) { depth = d + 1; full_redraw(); }
    open_sel[d] = i; menu_dirty = 1; last_menu_key = P.ms();
}
static void draw_dropdown(int d)
{
    const Menu *m = open_menu[d];
    uint32_t h = 12 * S;
    for (int i = 0; i < m->n; i++) h += item_h(&m->items[i]);
    uint32_t x = open_x[d], y = open_y[d], w = open_w[d];
    int last = m->n;                                    /* 3.6.10: 화면보다 길면 고른 항목이 보이게 밀어 그림 (▲·▼) */
    uint32_t avail = (uint32_t)menu_avail_h();
    if (h > avail) {
        uint32_t ah = menu_arrow_h();
        if (menu_scroll[d] > open_sel[d]) menu_scroll[d] = open_sel[d];
        for (;;) {                                      /* 고른 항목까지 들어가도록 첫 항목을 뒤로 */
            uint32_t t = 12 * S + (menu_scroll[d] ? ah : 0) + ah;
            for (int i = menu_scroll[d]; i <= open_sel[d] && i < m->n; i++) t += item_h(&m->items[i]);
            if (t <= avail || menu_scroll[d] >= open_sel[d]) break;
            menu_scroll[d]++;
        }
        uint32_t t = 12 * S + (menu_scroll[d] ? ah : 0);
        for (last = menu_scroll[d]; last < m->n; last++) {
            uint32_t need = item_h(&m->items[last]) + (last < m->n - 1 ? ah : 0);
            if (t + need > avail) break;
            t += item_h(&m->items[last]);
        }
        h = t + (last < m->n ? ah : 0);
        y = 2 * S;
    } else menu_scroll[d] = 0;
    {   /* 밀린 자리·크기가 바뀌면 지난 그림(▲·▼, 아래쪽)이 남지 않게 다시 그림 */
        static int ps[6] = { -1, -1, -1, -1, -1, -1 }, pl[6];
        static uint32_t ph[6];
        if (ps[d] != menu_scroll[d] || pl[d] != last || ph[d] != h) { int was = ps[d]; ps[d] = menu_scroll[d]; pl[d] = last; ph[d] = h; if (was >= 0) full_redraw(); }
    }
    if (y + h > D.h) y = D.h > h ? D.h - h : 0;
    open_y[d] = y;
    hit_add(hc_none, (intptr_t)x, (int)y, 0, 0, x, y, w, h);   /* 3.0: 아래 클릭 막기 */
    ui_rrect(x + 4 * S, y + 6 * S, w, h, 8 * S, C_SHADOW);
    ui_rframe(x, y, w, h, 8 * S, C_LINE, C_CARD);
    uint32_t iy = y + 6 * S, active = (uint32_t)d == (uint32_t)(depth - 1);
    if (menu_scroll[d]) {                               /* ▲ 위에 더 있음 */
        uint32_t ah = menu_arrow_h();
        text_f(F, S, x + (w - text_w("▲")) / 2, ui_ty(iy, ah), "▲", C_MUTED, 0xFF, 0, 1);
        hit_add(hc_mscroll, d, -1, 0, 0, x, iy, w, ah);
        iy += ah;
    }
    for (int i = menu_scroll[d]; i < last; i++) {
        const MenuItem *it = &m->items[i];
        uint32_t ih = item_h(it);
        if (it->label < 0) { fe_fill(x + 12 * S, iy + ih / 2, w - 24 * S, S, C_LINE); iy += ih; continue; }
        int st = item_state(it);
        if (st < 0) continue;
        int sel = i == open_sel[d] && st != 3;
        uint8_t fg = sel ? 15 : (st == 0 ? C_FAINT : st == 3 ? C_ACC : C_TEXT);
        if (st == 1 || st == 2) hit_add(hc_mitem, d, i, 0, 0, x + 6 * S, iy, w - 12 * S, ih);
        if (sel) {
            ui_rrect(x + 6 * S, iy, w - 12 * S, ih, 5 * S, active ? C_ACC2 : C_ROWSEL);
            if (active) ui_rrect(RTL ? x + w - 9 * S : x + 6 * S, iy + 4 * S, 3 * S, ih - 8 * S, S, C_ACC);
        }
        uint32_t ty = ui_ty(iy, ih);
        if (RTL) {                                      /* 3.4.4: [키][글][아이콘] — 하위 메뉴 표시 ◀는 왼쪽 */
            if (st == 2) { uint32_t d0 = LH / 3; ui_rrect(x + w - 14 * S - LH + (LH - d0) / 2, iy + (ih - d0) / 2, d0, d0, d0 / 2, sel ? 15 : C_OK); }
            char tb[128];
            int ic = it->action == A_PROPS_MIX ? ICON_MIXER : it->action == A_PROPS_KB ? ICON_KEYBOARD : it->action == A_PROPS_EMU ? ICON_GEAR : -1;
            if (it->action == A_POP_JOY || it->action == A_POP_MOUSE) ic = ICON_SWITCH;
            if (ic >= 0 && st != 2) draw_icon_hd(x + w - 12 * S - LH, iy + (ih - LH) / 2, LH, ic, fg, sel ? 15 : C_ACC, sel ? (active ? C_ACC2 : C_ROWSEL) : C_CARD);
            text_r(x + w - LH - 18 * S, ty, item_text(it, tb, sizeof tb), fg, w - LH - 30 * S);
            if (it->sub) text_f(F, S, x + 10 * S, ty, "◀", sel ? 15 : C_MUTED, 0xFF, 0, 1);
            else {
                char kb[48];
                const char *ks = menu_keys_for(it->action, kb, sizeof kb);
                if (ks && st != 0) { uint32_t ch = SLH + 6 * S; ui_keychip(x + 16 * S, iy + (ih - ch) / 2, ch, ks, 0); }
                else if (ks) text_f(SF, 1, x + 22 * S, ui_sty(iy, ih), ks, C_FAINT, 0xFF, 0, 1);
            }
            iy += ih;
            continue;
        }
        if (st == 2) {                                  /* 체크 표시: 초록 점 */
            uint32_t d0 = LH / 3;
            ui_rrect(x + 14 * S + (LH - d0) / 2, iy + (ih - d0) / 2, d0, d0, d0 / 2, sel ? 15 : C_OK);
        }
        char tb[128];
        int ic = it->action == A_PROPS_MIX ? ICON_MIXER : it->action == A_PROPS_KB ? ICON_KEYBOARD : it->action == A_PROPS_EMU ? ICON_GEAR : -1;
        if (it->action == A_POP_JOY || it->action == A_POP_MOUSE) ic = ICON_SWITCH;
        if (ic >= 0 && st != 2) draw_icon_hd(x + 12 * S, iy + (ih - LH) / 2, LH, ic, fg, sel ? 15 : C_ACC, sel ? (active ? C_ACC2 : C_ROWSEL) : C_CARD);   /* 2.17 */
        text_f(F, S, x + LH + 18 * S, ty, item_text(it, tb, sizeof tb), fg, 0xFF, w - LH - 30 * S, 1);
        if (it->sub) text_f(F, S, x + w - LH - 4 * S, ty, "▶", sel ? 15 : C_MUTED, 0xFF, 0, 1);
        else {
            char kb[48];
            const char *ks = menu_keys_for(it->action, kb, sizeof kb);
            if (ks && st != 0) {
                uint32_t ch = SLH + 6 * S, kw = ui_sw(ks) + 12 * S;
                ui_keychip(x + w - kw - 16 * S, iy + (ih - ch) / 2, ch, ks, 0);
            } else if (ks) text_f(SF, 1, x + w - ui_sw(ks) - 22 * S, ui_sty(iy, ih), ks, C_FAINT, 0xFF, 0, 1);
        }
        iy += ih;
    }
    if (last < m->n) {                                  /* ▼ 아래에 더 있음 */
        uint32_t ah = menu_arrow_h();
        text_f(F, S, x + (w - text_w("▼")) / 2, ui_ty(iy, ah), "▼", C_MUTED, 0xFF, 0, 1);
        hit_add(hc_mscroll, d, 1, 0, 0, x, iy, w, ah);
    }
}

static void draw_dialog(void);
static void draw_dialog_direct(void);
static void draw_grow(void);
static int grow_state;
static int grow_done;                    /* 4.0.28: 1 = 끝났음, 아무 키나 누르면 재부팅 */
static void props_fix_sel(void);                      /* 0 확인, 1 진행 중, 2 끝(메시지) */
static int grow_pct;
static char grow_msg[160];

static void ov_draw_fwd(void);
static void draw_menus(void)
{
    if (dialog) { draw_dialog(); }
    else hit_clear();
    if (pop_mode && !dialog && ov_on) ov_draw_fwd();   /* 3.5.0: 팝업 밑은 떠 있는 막대 그대로 */
    if (bar_visible) draw_bar();
    if (depth > 0 && bar_visible && !dialog) {          /* 4.0.134: 열려 있는 메뉴 단계를 기억 */
        mem_bar = bar_sel; mem_depth = depth;
        for (int k = 0; k < depth && k < 6; k++) { mem_menu[k] = open_menu[k]; mem_sel[k] = open_sel[k]; }
    }
    for (int d = 0; d < depth; d++) { layout_menu(d); draw_dropdown(d); }
    menu_dirty = 0;
}

static void draw_menu_under(void)                       /* 팝업 밑의 메뉴 (팝업 카드가 그 위를 덮음) */
{
    if (bar_visible) draw_bar();
    for (int d = 0; d < depth; d++) { layout_menu(d); draw_dropdown(d); }
}
static void menu_show(void);
/* 3.2.14: 단축키로 메뉴 깊은 곳을 바로 열기 — chain[0]은 막대 메뉴, 다음은 차례로 하위 메뉴 */
static void menu_open_path(const Menu *const *chain, int n)
{
    menu_show();
    depth = 0;                                          /* 4.0.134: 단축키로 깊이 여는 건 기억한 단계 말고 지정한 길로 */
    for (int b = 0; b < bar_n; b++) if (bar_menus[b] == chain[0]) bar_sel = b;
    open_level(chain[0]);
    for (int k = 1; k < n; k++) {
        const Menu *m = open_menu[depth - 1];
        int idx = -1;
        for (int i = 0; i < m->n; i++) if (m->items[i].sub == chain[k]) idx = i;
        if (idx < 0) return;
        open_sel[depth - 1] = idx;
        open_level(chain[k]);
    }
}
static void menu_open_special(int slot) { const Menu *c[4] = { &m_file, &m_media, slot ? &m_cart2 : &m_cart1, slot ? &m_special2 : &m_special1 }; menu_open_path(c, 4); }
static void menu_open_lang(void) { open_langwin(); }   /* 3.3.1 RCtrl+RShift+L */
static void menu_open_tpos(void) { const Menu *c[4] = { &m_file, &m_media, &m_tape, &m_tpos }; tape_scan(); menu_open_path(c, M->tape && tape_nfile ? 4 : 3); }   /* 테이프가 없으면 카세트 메뉴까지 */

static void menu_show(void)
{
    if (ov_on & 3) { ov_on &= ~3; full_redraw(); }      /* 3.5.0: F11 메뉴를 열면 떠 있는 막대는 닫음 */
    pop_mode = 0;
    snap_valid = 0;
    bar_visible = 1;
    bar_sel = mem_bar >= 0 && mem_bar < bar_n ? mem_bar : 0;   /* 4.0.132: 직전에 고른 메뉴부터 (처음엔 "파일") */
    depth = 0;
    for (int k = 0; k < mem_depth && k < 6; k++) {         /* 4.0.134: 부메뉴도 직전에 열어 둔 단계·고른 줄까지 */
        open_level(mem_menu[k]);
        if (mem_sel[k] >= 0 && mem_sel[k] < mem_menu[k]->n) open_sel[k] = mem_sel[k];
    }
    last_menu_key = P.ms();
    menu_dirty = 1;
}

static void dc_remember(void);
static void dg_color_restore(void);
static void dialog_back(void)
{
    fe_trace("dialog back");
    if (dialog == 2) dg_color_restore();                 /* 색 시험이 올린 DAC 되돌림 */
    if (dialog == 13) dc_remember();                    /* 4.0.31: 어떻게 나가도 위치 기억 */
    input_init(&in);
    popup_act = -1;
    if (ui_ret_valid && dialog == 6 && props_tab == 6 && ui_ret_restore()) return;   /* 믹서를 닫으면 하던 곳으로 */

    if (dialog == 5 && mach_from_props) { mach_from_props = 0; open_dialog(6); return; }   /* 기종 선택 → 속성 창 */
    if (dialog == 1 && browse_imgdir()) { browse_remember(); open_dialog(11); return; }   /* 3.8.1: 저장 폴더 고르기 → 이미지 만들기 창으로 돌아감 */
    if (dialog == 1) browse_remember();
    if (snap_valid) {                                   /* Esc: 창을 열었던 바로 전 메뉴로 (펼친 단계까지) */
        dialog = 0; menu_under = 0;
        bar_visible = 1; bar_sel = snap_bar; depth = snap_depth;
        for (int k = 0; k < 6; k++) { open_menu[k] = snap_menu[k]; open_sel[k] = snap_sel[k]; }
        snap_valid = 0;
        last_menu_key = P.ms();
        full_redraw();
        menu_dirty = 1;
        return;
    }
    menu_close_all();
}

/* 창에서 고르기를 마쳤을 때: 메뉴를 모두 닫는다 (롬·디스크·기종을 고르면 MSX 재부팅) */
static void dialog_done(void)
{
    if (dialog == 13) dc_remember();
    snap_valid = 0;
    mach_from_props = 0;
    menu_close_all();
}

static void menu_close_all(void)
{
    if (dialog == 13) dc_remember();
    input_init(&in);                                    /* 창이 열린 동안 뗀 키(Shift 등)가 눌린 채로 남지 않게 */
    fe_trace("menu close all");
    bar_visible = 0;
    depth = 0;
    dialog = 0;
    pop_mode = 0;
    menu_under = 0; popup_act = -1; ui_ret_valid = 0;   /* 5.2.120 */
    full_redraw();
}

static void open_level(const Menu *m)
{
    if (depth >= 6) return;
    open_menu[depth] = m;
    open_sel[depth] = 0;
    menu_scroll[depth] = 0;
    while (open_sel[depth] < m->n - 1 && (m->items[open_sel[depth]].label < 0 || item_state(&m->items[open_sel[depth]]) <= 0 || item_state(&m->items[open_sel[depth]]) == 3)) open_sel[depth]++;   /* 3.8.5: 흐린 줄은 건너뛰고 처음 고름 */
    depth++;
    full_redraw();
}

static void menu_move(int dir)
{
    const Menu *m = open_menu[depth - 1];
    int s = open_sel[depth - 1];
    for (int k = 0; k < m->n; k++) {
        s = (s + dir + m->n) % m->n;
        int st = m->items[s].label >= 0 ? item_state(&m->items[s]) : -1;
        if (st == 1 || st == 2) break;
    }
    open_sel[depth - 1] = s;
}

/* 메뉴가 받은 키 (누름만). 처리했으면 1 */
static void menu_key(int key)
{
    if (RTL && (key == 0xCB || key == 0xCD)) key ^= 0xCB ^ 0xCD;   /* 3.4.4: 오른쪽 → 왼쪽 메뉴는 ←·→도 뒤집음 */
    last_menu_key = P.ms();
    menu_dirty = 1;
    if (depth == 0) {
        switch (key) {
        case 0xCB: bar_sel = (bar_sel + bar_n - 1) % bar_n; break;
        case 0xCD: bar_sel = (bar_sel + 1) % bar_n; break;
        case 0xD0: case 0x1C: case 0x9C: open_level(bar_menus[bar_sel]); break;
        case 0x01: menu_close_all(); break;
        default: break;
        }
        return;
    }
    const MenuItem *it = &open_menu[depth - 1]->items[open_sel[depth - 1]];
    switch (key) {
    case 0xC8: menu_move(-1); break;
    case 0xD0: menu_move(1); break;
    case 0xCB:                                              /* ← */
        if (depth > 1) { depth--; full_redraw(); }
        else if (pop_mode) menu_close_all();                /* 3.5.0: 팝업 맨 위에서 ← = 닫기 */
        else { bar_sel = (bar_sel + bar_n - 1) % bar_n; depth = 0; full_redraw(); open_level(bar_menus[bar_sel]); }
        break;
    case 0xCD:                                              /* → */
        if (it->sub && item_state(it) > 0) open_level(it->sub);
        else if (it->sub && item_state(it) == 0) break;      /* 3.8.5: 흐린 ▶ 줄에서는 → 가 아무 일도 안 함 */
        else if (pop_mode) break;
        else { bar_sel = (bar_sel + 1) % bar_n; depth = 0; full_redraw(); open_level(bar_menus[bar_sel]); }
        break;
    case 0x1C: case 0x9C:
        if (it->label < 0 || item_state(it) == 0 || item_state(it) == 3) break;
        if (it->sub) { open_level(it->sub); break; }
        {
            int a = it->action;
            int keep = a == A_VOL_UP || a == A_VOL_DOWN || a == A_MUTE || a == A_SPEED_UP || a == A_SPEED_DOWN || a == A_RESET_AFTER_INSERT || a == A_HD_RESET_EJECT || a == A_HD_RESET_INS || a == A_VHD_RESET_INS || a == A_DISK_RESET_AFTER;
            snap_valid = !pop_mode; snap_bar = bar_sel; snap_depth = depth;   /* 3.5.0: 팝업에서 연 창은 Esc로 모두 닫음 */
            for (int k = 0; k < 6; k++) { snap_menu[k] = open_menu[k]; snap_sel[k] = open_sel[k]; }
            if (!keep) menu_close_all();
            do_action(a);
            if (keep) full_redraw();
            else if (dialog && dialog != 2 && dialog != 7 && snap_valid) {   /* 메뉴에서 연 팝업 — 메뉴를 그대로 두고 그 위에 (진단·소리 진단은 그림자 버퍼 창이라 예전처럼) */
                bar_visible = 1; bar_sel = snap_bar; depth = snap_depth; menu_under = 1;
                full_redraw(); menu_dirty = 1;
            }
        }
        break;
    case 0x01: depth--; if (!depth && pop_mode) menu_close_all(); else full_redraw(); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* 대화 창: 파일 선택 (openMSX 방식), 진단, 정보                          */
/* ------------------------------------------------------------------ */
#ifdef FE_UEFI
#define DIR_MAX     16384                        /* 4.0.124: UEFI는 메모리가 넉넉 — 한 쪽에 다 담아 쪽 나눔이 거의 없게 */
#else
#define DIR_MAX     2048
#endif

#define PATH_MAX_FE FE_PATH_MAX                  /* 3.4.7: 512 → 1024 (긴 파일 이름) */
static FeDirEntry entries[DIR_MAX];
static int nent, list[DIR_MAX + 2], nlist, menu_sel, menu_top, list_error;
#define ITEM_UP (-2)
#define ITEM_DIRDISK (-3)                       /* 3.6.2: 디렉토리 삽입 창의 [새 폴더 만들기] */
static int dir_naming;                          /* 3.6.2: 새 폴더 이름을 치는 중 */
static int del_ask, del_isdir;                  /* 3.6.4: Del → 지울까요? 팝업 */
static char del_path[FE_PATH_MAX], del_name[FE_KEY_MAX];
static char dir_newname[64];
static char cur_path[PATH_MAX_FE], cur_disp[PATH_MAX_FE];
static char rom_path[PATH_MAX_FE], rom_disp[PATH_MAX_FE];
static int browse_opened;
static char disk_dir[PATH_MAX_FE] = FE_DISK, disk_dir_disp[PATH_MAX_FE] = FE_DISP_STORE " > DISK";
static uint8_t probe_buf[32768];


static int name_cmp(const char *a, const char *b)
{
    while (*a && lower_c(*a) == lower_c(*b)) { a++; b++; }
    return lower_c(*a) - lower_c(*b);
}

static void entry_path(int e, char *out, int max)
{
    out[0] = 0;
    if (cur_path[0]) { str_append(out, max, cur_path); str_append(out, max, "\\"); }
    str_append(out, max, entries[e].key[0] ? entries[e].key : entries[e].name);
}

static int ext_is(const char *n, const char *e1, const char *e2, const char *e3)
{
    const char *dot = 0;
    for (const char *p = n; *p; p++) if (*p == '.') dot = p;
    if (!dot) return 0;
    const char *e[3] = { e1, e2, e3 };
    for (int k = 0; k < 3; k++) {
        if (!e[k]) continue;
        int i = 0;
        while (e[k][i] && lower_c(dot[1 + i]) == e[k][i]) i++;
        if (!e[k][i] && !dot[1 + i]) return 1;
    }
    return 0;
}

/* 4.0.29: 확장자별 아이콘·글자 색 (파일 창들). 4.0.30: 파일 선택창·디스크 복사창이 이 하나를 같이 씀 */
enum { FT_OTHER, FT_TEXT, FT_ROM, FT_DISK, FT_HD, FT_ZIP, FT_TAPE, FT_STATE, FT_SOUND, FT_DISKIMG, FT_DISK360, FT_DISK144, FT_DISK720, FT_PICTURE, FT_MOVIE };
static int file_type_sz(const char *n, uint32_t size)
{
    if (ext_is(n, "txt", "doc", "bat") || ext_is(n, "bas", "ini", "asc") || ext_is(n, "md", "log", "cfg")) return FT_TEXT;
    if (ext_is(n, "rom", "mx1", "mx2") || ext_is(n, "ri", 0, 0)) return FT_ROM;
    if (ext_is(n, "dsk", "di1", "di2") || ext_is(n, "360", "720", 0)) return size > DISK_MAX ? FT_DISKIMG : (size && size <= 368640u) ? FT_DISK360 : size > 737280u ? FT_DISK144 : FT_DISK720;   /* 1.44 MB 넘는 디스크 = 하드, 360 KB 이하 = 5.25인치, 720 KB 넘으면 1.44 MB(2HD) */
    if (ext_is(n, "hdd", "ide", 0)) return FT_HD;
    if (ext_is(n, "zip", 0, 0)) return FT_ZIP;
    if (ext_is(n, "cas", 0, 0)) return FT_TAPE;
    if (ext_is(n, "sta", 0, 0)) return FT_STATE;
    if (ext_is(n, "sf2", 0, 0)) return FT_SOUND;
    if (ext_is(n, "png", "jpg", "jpeg")) return FT_PICTURE;
    if (ext_is(n, "avi", 0, 0)) return FT_MOVIE;
    return FT_OTHER;
}
static int file_type(const char *n) { return file_type_sz(n, 0); }
static void file_look(int ft, int *icon, uint8_t *col)
{
    switch (ft) {
    case FT_TEXT: *icon = ICON_TEXT; *col = 5; break;
    case FT_ROM:  *icon = ICON_CART; *col = 9; break;
    case FT_DISK: *icon = ICON_FDD; *col = C_OK; break;
    case FT_DISK360: *icon = ICON_DSK360; *col = C_OK; break;
    case FT_DISK720: *icon = ICON_DSK720; *col = C_OK; break;
    case FT_DISK144: *icon = ICON_DSK144; *col = C_OK; break;
    case FT_HD:   *icon = ICON_HDD; *col = 13; break;
    case FT_DISKIMG: *icon = ICON_DISKIMG; *col = 13; break;
    case FT_ZIP:  *icon = ICON_ZIP; *col = 7; break;
    case FT_TAPE: *icon = ICON_CAS; *col = 10; break;
    case FT_STATE: *icon = ICON_STATE; *col = 3; break;
    case FT_SOUND: *icon = ICON_SOUND; *col = 11; break;
    case FT_PICTURE: *icon = ICON_PICTURE; *col = 15; break;
    case FT_MOVIE: *icon = ICON_MOVIE; *col = 8; break;
    default:      *icon = ICON_OTHER; *col = 14; break;
    }
}
static int browse_slot;
static int browse_showall;                      /* 4.0.29: F3 = 종류 거르지 않고 모든 파일 보기 */
static int browse_disk(void) { return browse_slot == 10 || browse_slot == 11; }
static int browse_dir(void) { return browse_slot == 18 || browse_slot == 19 || browse_slot == 26; }   /* 3.6.1: 디렉토리 삽입 (폴더만, 디스크 폴더 위치를 같이 씀) */
static int browse_imgdir(void) { return browse_slot == 26; }   /* 3.8.1: 이미지 만들기의 저장 폴더 고르기 (폴더만 보이는 디렉토리 삽입 창과 같은 모양) */
static int browse_vhd(void) { return browse_slot >= 20 && browse_slot <= 25; }   /* 3.7.2: 가상 하드 C~H (VMFolder 폴더) */
static int vb_ask, vb_form;
static int vb_key(int key);
static void vb_scan(void);
static void vb_row_info(int it, uint32_t xt, uint32_t xs, uint32_t xe, uint32_t y, uint32_t rh, int sel);
static void vb_draw_pop(uint32_t X, uint32_t W, uint32_t py0);
static int browse_tape(void) { return browse_slot == 12; }
static int browse_hd(void) { return browse_slot == 13 || browse_slot == 17; }   /* 17 = 슬레이브 (3.4.3) */
static int browse_bas(void) { return browse_slot == 14 || browse_slot == 15; }   /* 3.2.7 BASIC 텍스트 (14 넣기, 15 저장) */
static int browse_bsave(void) { return browse_slot == 15; }
static int browse_kind(void) { return browse_bas() ? MK_BASIC : browse_tape() ? MK_TAPE : browse_hd() ? MK_HD : browse_disk() ? MK_DISK : MK_ROM; }   /* 이 파일 창의 미디어 종류 (src/media.h) */
static char bas_fname[64] = "PROGRAM.txt";     /* 3.2.7 저장 창의 파일 이름 */
static int bas_typed, bas_over;                 /* 이름을 친 뒤인지, 덮어쓰기 한 번 물었는지 */
static int bas_mode_save;                       /* 3.2.9 BASIC TXT변환 창: 0 넣기, 1 저장 (마지막 것 기억) */
static void bas_set_mode(int save)
{
    browse_slot = save ? 15 : 14;
    bas_mode_save = save; bas_typed = save; bas_over = 0;
    browse_full = 1; menu_dirty = 1; force_redraw = 1;
}
static void hc_bas_mode(const UiHit *h, int ev) { if (ev == 1 && h->b != bas_mode_save) bas_set_mode(h->b); }
static char key_char(int key, int shift);
static void bas_save_to(void);
static void ed_open(const char *path, const char *name);
static void ed_open_zip(const uint8_t *zip, uint32_t zlen, const char *zpath, const char *name);       /* 4.0.29 텍스트 편집기 (fe_edit.inc) */
static char rom_sel[FE_KEY_MAX], disk_sel[FE_KEY_MAX];        /* 마지막으로 고른 항목 */
static int rom_top = -1, disk_top = -1;         /* 마지막 스크롤 위치 */
/* 3.8.1: 디렉토리 삽입·가상 하드도 디스크와 따로 마지막 폴더·고른 항목·스크롤 위치를 기억. 처음 폴더: 디렉토리 삽입 = DISK\DirFolder, 가상 하드 = VMFolder */
#define DIR_DEF FE_DISK "\\DirFolder"
#define VHD_DEF FE_VMF
/* 3.9.5: 창마다 맨 위(뿌리) 폴더 — 롬 ROMS, 카세트 CAS, 하드 디스크 HARDDISK, BASIC, 가상 하드 VMFolder. 그 위로는 못 감. 0 = 제한 없음 */
static const char *browse_root(void)
{
    if (browse_vhd()) return VHD_DEF;                   /* 4.0.13: VMFolder는 자기 자신, 나머지(DISK·CAS·HARDDISK·BASIC·ROMS)는 Storage Device가 뿌리 */
    if (browse_dir() || browse_imgdir()) return 0;      /* 이미지 파일 관리는 예전 그대로 (제한 없음) */
    return FE_STORE;
}
/* 4.0.13: 기준 폴더 = 각 창이 처음 열리는 곳 (뿌리와 다름). 기억한 폴더가 뿌리 밖이거나 없어졌을 때도 여기로 */
static void browse_home(char *path, int pm, char *disp, int dm)
{
    const char *p = browse_vhd() ? FE_VMF : browse_tape() ? FE_CAS : browse_hd() ? FE_HDD : browse_bas() ? FE_BAS : browse_dir() ? DIR_DEF : (browse_imgdir() || browse_disk()) ? FE_DISK : FE_ROMS;
    copy_str(path, pm, p);
    copy_str(disp, dm, "Drive 1 (boot)");
    const char *q = p + 3;                              /* "0:\" 다음부터 폴더마다 " > 이름" */
    while (*q) {
        char nm[64]; int n = 0;
        while (*q && *q != '\\' && n < 63) nm[n++] = *q++;
        nm[n] = 0; if (*q == '\\') q++;
        str_append(disp, dm, " > "); str_append(disp, dm, nm);
    }
}
static int at_root(void) { const char *r = browse_root(); return r && !name_cmp(cur_path, r); }
static int path_under(const char *p, const char *r)
{
    int n = (int)strlen(r);
    for (int i = 0; i < n; i++) if (!p[i] || lower_c(p[i]) != lower_c(r[i])) return 0;
    return !p[n] || p[n] == '\\';
}
static char dir_dir[PATH_MAX_FE] = DIR_DEF, dir_dir_disp[PATH_MAX_FE] = FE_DISP_STORE " > DISK > DirFolder", dir_sel[FE_KEY_MAX];
static char vbr_dir[PATH_MAX_FE] = VHD_DEF, vbr_dir_disp[PATH_MAX_FE] = FE_DISP_STORE " > VMFolder", vbr_sel[FE_KEY_MAX];
static int dir_top = -1, vbr_top = -1;
static char img_dir[FE_PATH_MAX] = FE_DISK, img_dir_disp[FE_PATH_MAX] = FE_DISP_STORE " > DISK";   /* 3.8.1: 이미지 만들기 저장 폴더 (처음 DISK, 마지막으로 고른 폴더 기억) */
static void browse_remember(void)
{
    const char *k = (menu_sel < nlist && list[menu_sel] >= 0) ? (entries[list[menu_sel]].key[0] ? entries[list[menu_sel]].key : entries[list[menu_sel]].name) : "";
    if (browse_tape()) { copy_str(tape_dir, sizeof tape_dir, cur_path); copy_str(tape_dir_disp, sizeof tape_dir_disp, cur_disp); copy_str(tape_sel, sizeof tape_sel, k); tape_top = menu_top; }
    else if (browse_bas()) { copy_str(bas_dir, sizeof bas_dir, cur_path); copy_str(bas_dir_disp, sizeof bas_dir_disp, cur_disp); copy_str(bas_sel, sizeof bas_sel, k); bas_top = menu_top; }
    else if (browse_hd()) { copy_str(hd_dir, sizeof hd_dir, cur_path); copy_str(hd_dir_disp, sizeof hd_dir_disp, cur_disp); copy_str(hd_sel, sizeof hd_sel, k); hd_top = menu_top; }
    else if (browse_imgdir()) { copy_str(img_dir, sizeof img_dir, cur_path); copy_str(img_dir_disp, sizeof img_dir_disp, cur_disp); }
    else if (browse_dir()) { copy_str(dir_dir, sizeof dir_dir, cur_path); copy_str(dir_dir_disp, sizeof dir_dir_disp, cur_disp); copy_str(dir_sel, sizeof dir_sel, k); dir_top = menu_top; }
    else if (browse_vhd()) { copy_str(vbr_dir, sizeof vbr_dir, cur_path); copy_str(vbr_dir_disp, sizeof vbr_dir_disp, cur_disp); copy_str(vbr_sel, sizeof vbr_sel, k); vbr_top = menu_top; }
    else if (browse_disk()) { copy_str(disk_dir, sizeof disk_dir, cur_path); copy_str(disk_dir_disp, sizeof disk_dir_disp, cur_disp); copy_str(disk_sel, sizeof disk_sel, k); disk_top = menu_top; }
    else { copy_str(rom_path, sizeof rom_path, cur_path); copy_str(rom_disp, sizeof rom_disp, cur_disp); copy_str(rom_sel, sizeof rom_sel, k); rom_top = menu_top; }
    settings_changed();
}

/* 4.0.34: 폴더에 DIR_MAX(4096)개가 넘는 항목이 있으면 '창'으로 나눠 읽음 — 끝에서 ↓ = 다음 창, 처음에서 ↑ = 앞 창 */
#define BRW_HIST 64
static int brw_base, brw_next = -1, brw_hist[BRW_HIST], brw_depth, lw_found;
static char brw_loaded[FE_PATH_MAX];
static int zin;                                         /* 4.0.34: zip 안 목록을 보는 중 (zip을 폴더처럼 열었을 때) */
/* 4.0.122: 항목 수 제한 없음 — 큰 폴더는 이름만 담은 색인(cx)을 먼저 만들어 정렬하고, 한 쪽(PG개)씩 자세한 정보를 읽음. 창 번호(brw_base)는 정렬 순위 */
static int dir_ok(int is_dir, const char *nm)
{
    return is_dir || browse_showall || (!browse_dir() && !browse_vhd() && media_shows(browse_kind(), nm, browse_bsave()));   /* 보일 확장자는 미디어 종류 표 (src/media.c) */
}
#define CX_MAX 16384
typedef struct { char name[64]; uint8_t is_dir; } CxItem;
static CxItem cx[CX_MAX];
static uint16_t cx_ord[CX_MAX];
static int cx_n, cx_big, cx_cut, cx_sig;
static char cx_path[FE_PATH_MAX];
static int ent_cmp(int ad, const char *an, int bd, const char *bn) { return ad != bd ? (ad ? -1 : 1) : name_cmp(an, bn); }
static int cx_sigv(void) { return browse_showall | (browse_dir() << 1) | (browse_vhd() << 2) | (browse_bas() << 3) | (browse_bsave() << 4) | (browse_tape() << 5) | (browse_hd() << 6) | (browse_disk() << 7) | (browse_imgdir() << 8); }
static int cx_less(int a, int b) { return ent_cmp(cx[a].is_dir, cx[a].name, cx[b].is_dir, cx[b].name) < 0; }
static void cx_build(void)
{
    cx_n = 0; cx_cut = 0;
    int skip = 0;
    for (int g = 0; g < 100000; g++) {
        fe_list_skip = skip; fe_list_next = -1;
        int n = P.list_dir ? P.list_dir(cur_path, entries, DIR_MAX) : -1;
        fe_list_skip = 0;
        int nx = n >= DIR_MAX ? fe_list_next : -1;
        if (n < 0) break;
        for (int i = 0; i < n; i++) {
            const char *nm = entries[i].key[0] ? entries[i].key : entries[i].name;
            if (!dir_ok(entries[i].is_dir, nm)) continue;
            if (cx_n >= CX_MAX) { cx_cut = 1; break; }
            copy_str(cx[cx_n].name, sizeof cx[0].name, entries[i].name);
            cx[cx_n].is_dir = entries[i].is_dir;
            cx_ord[cx_n] = (uint16_t)cx_n; cx_n++;
        }
        if (nx < 0 || cx_cut) break;
        skip = nx;
    }
    for (int gap = cx_n / 2; gap > 0; gap /= 2)                 /* 셸 정렬 (순위 배열만 옮김) */
        for (int i = gap; i < cx_n; i++) {
            uint16_t t = cx_ord[i]; int j = i;
            while (j >= gap && cx_less(t, cx_ord[j - gap])) { cx_ord[j] = cx_ord[j - gap]; j -= gap; }
            cx_ord[j] = t;
        }
    copy_str(cx_path, sizeof cx_path, cur_path);
    cx_sig = cx_sigv();
}
/* 정렬 순위 [pb, pb+PG) 쪽의 자세한 정보를 entries에 읽음 (원본 목록을 다시 훑으며 첫~끝 이름 사이만 담음) */
static int cx_page(int pb)
{
    int pe = pb + (DIR_MAX - 8); if (pe > cx_n) pe = cx_n;
    if (pb >= pe) return 0;
    const CxItem *lo = &cx[cx_ord[pb]], *hi = &cx[cx_ord[pe - 1]];
    int skip = 0, k = 0;
    for (int g = 0; g < 100000 && k < pe - pb; g++) {
        fe_list_skip = skip; fe_list_next = -1;
        int n = P.list_dir ? P.list_dir(cur_path, entries + k, DIR_MAX - k) : -1;
        fe_list_skip = 0;
        int nx = n >= DIR_MAX - k ? fe_list_next : -1;
        if (n < 0) break;
        int kk = k;
        for (int i = k; i < k + n; i++) {
            const char *nm = entries[i].key[0] ? entries[i].key : entries[i].name;
            if (!dir_ok(entries[i].is_dir, nm)) continue;
            if (ent_cmp(entries[i].is_dir, entries[i].name, lo->is_dir, lo->name) < 0 || ent_cmp(entries[i].is_dir, entries[i].name, hi->is_dir, hi->name) > 0) continue;
            if (kk != i) entries[kk] = entries[i];
            kk++;
        }
        k = kk;
        if (nx < 0) break;
        skip = nx;
    }
    return k;
}
static void load_dir_win(const char *sel_key)
{
    /* 4.0.34: 종류가 맞는 항목만 DIR_MAX개 채울 때까지 원본 목록을 창 단위로 이어 읽음 (맞는 파일이 없는 앞부분 때문에 빈 목록이 되지 않게) */
    int skip = brw_base, k = 0, err = 0;
    zin = 0;
    brw_next = -1;
    int bigpg = 0;
    if (cx_big && cur_path[0] && !strcmp(cx_path, cur_path) && cx_sig == cx_sigv() && brw_base > 0 && brw_base < cx_n) {   /* 큰 폴더의 둘째 쪽부터 */
        k = cx_page(brw_base); bigpg = 1;
        brw_next = brw_base + (DIR_MAX - 8) < cx_n ? brw_base + (DIR_MAX - 8) : -1;
    } else if (cx_big && brw_base > 0) { brw_base = 0; brw_depth = 0; skip = 0; }
    if (!bigpg) cx_big = 0;
    for (int g = 0; !bigpg && g < 4096; g++) {
        fe_list_skip = skip; fe_list_next = -1;
        int n = P.list_dir ? P.list_dir(cur_path, entries + k, DIR_MAX - k) : -1;
        fe_list_skip = 0;
        int nx = n >= DIR_MAX - k ? fe_list_next : -1;
        if (n < 0) { if (!g) err = 1; break; }
        if (!cur_path[0]) { k += n; break; }
        int kk = k;
        for (int i = k; i < k + n; i++) {
            const char *nm = entries[i].key[0] ? entries[i].key : entries[i].name;
            int ok = dir_ok(entries[i].is_dir, nm);
            if (ok) { if (kk != i) entries[kk] = entries[i]; kk++; }
        }
        k = kk;
        brw_next = nx;
        if (nx < 0 || k >= DIR_MAX - 8) break;          /* 끝났거나 칸이 거의 참 */
        skip = nx;
    }
    if (!bigpg && cur_path[0] && brw_next >= 0 && !err && brw_base == 0) {   /* 한 쪽에 안 들어가는 폴더: 색인을 만들어 순위 순서로 */
        cx_build();
        k = cx_page(0);                                 /* (색인을 만들며 entries를 썼으니 다시 채움) */
        cx_big = cx_n > DIR_MAX - 8; brw_next = cx_big ? DIR_MAX - 8 : -1;
    }
    list_error = err;
    nent = k;
    if (browse_vhd()) vb_scan();
    nlist = 0;
    if (cur_path[0] && !at_root()) list[nlist++] = ITEM_UP;
    int start = nlist;
    for (int i = 0; i < nent; i++) list[nlist++] = i;
    for (int gap = (nlist - start) / 2; gap > 0; gap /= 2)      /* 4.0.124: 셸 정렬 (항목이 많아도 빠르게) */
        for (int i = start + gap; i < nlist; i++) {
            int t = list[i], j = i;
            while (j - gap >= start) {
                FeDirEntry *x = &entries[list[j - gap]], *y = &entries[t];
                int gt = x->is_dir != y->is_dir ? (!x->is_dir && y->is_dir) : name_cmp(x->name, y->name) > 0;
                if (!gt) break;
                list[j] = list[j - gap]; j -= gap;
            }
            list[j] = t;
        }
    menu_sel = 0; menu_top = 0;
    browse_full = 1;
    lw_found = 0;
    if (sel_key)
        for (int i = 0; i < nlist; i++)
            if (list[i] >= 0 && !name_cmp(entries[list[i]].key[0] ? entries[list[i]].key : entries[list[i]].name, sel_key)) { menu_sel = i; lw_found = 1; }
    menu_dirty = 1;
}
static int dir_total(void) { return cx_big ? cx_n : nent; }
static void load_dir(const char *sel_key)
{
    if (strcmp(brw_loaded, cur_path)) { brw_base = 0; brw_depth = 0; copy_str(brw_loaded, sizeof brw_loaded, cur_path); }   /* 다른 폴더면 처음 창부터 */
    load_dir_win(sel_key);
    if ((list_error || (nent <= 0 && nlist <= 1)) && brw_base) { brw_base = 0; brw_depth = 0; load_dir_win(sel_key); }   /* 창이 폴더 끝을 넘었으면 처음부터 */
    if (sel_key && !sel_key[0]) sel_key = 0;
    int s_base = brw_base, s_depth = brw_depth, s_hist[BRW_HIST];
    for (int i = 0; i < brw_depth; i++) s_hist[i] = brw_hist[i];
    for (int g = 0; brw_next >= 0 && (nent <= 0 || (sel_key && !lw_found)) && g < 256 && brw_depth < BRW_HIST; g++) {   /* 이 창에 (찾는 이름이나 보일 항목이) 없으면 다음 창을 */
        brw_hist[brw_depth++] = brw_base; brw_base = brw_next;
        load_dir_win(sel_key);
    }
    if (sel_key && !lw_found && (brw_base != s_base)) {   /* 끝까지 가도 그 이름이 없으면 처음 보던 창으로 */
        brw_base = s_base; brw_depth = s_depth;
        for (int i = 0; i < s_depth; i++) brw_hist[i] = s_hist[i];
        load_dir_win(0);
    }
    if (nent <= 0 && brw_base && !sel_key) {            /* 끝까지 가도 보일 게 없으면 처음 창으로 */
        brw_base = 0; brw_depth = 0; load_dir_win(0);
    }
}
/* 끝에서 ↓ / 처음에서 ↑: 창을 옮김 (보일 항목이 없는 창은 건너뜀. 옮기면 1) */
static void klog_note(const char *what);
static int brw_move(int dir)
{
    if (zin) return 0;
    int sb = brw_base, sd = brw_depth;
    for (int g = 0; g < 256; g++) {
        if (dir > 0 ? (brw_next < 0 || brw_depth >= BRW_HIST) : brw_depth <= 0) break;
        if (dir > 0) { brw_hist[brw_depth++] = brw_base; brw_base = brw_next; } else brw_base = brw_hist[--brw_depth];
        load_dir_win(0);
        if (nent > 0) break;
    }
    if (nent <= 0 || brw_base == sb) { int ms = menu_sel, mt = menu_top, moved = brw_base != sb || nent <= 0; klog_note(moved ? "목록 끝: 다음 쪽이 비어 되돌림" : "목록 끝: 다음 쪽 없음 (선택 유지)");   /* 끝에서 처음으로 가는 현상 추적 */ brw_base = sb; brw_depth = sd; if (moved) load_dir_win(0); menu_sel = ms; menu_top = mt; if (menu_sel > nlist - 1) menu_sel = nlist > 0 ? nlist - 1 : 0; return 0; }   /* 끝에서 더 내려도 처음으로 돌아가지 않음 (선택 유지) */
    int first = (cur_path[0] && !at_root()) ? 1 : 0;
    menu_sel = dir > 0 ? (first < nlist ? first : 0) : (nlist ? nlist - 1 : 0);
    menu_top = 0;
    klog_note("목록: 다음/이전 쪽으로 넘어감");   /* 5.2.139 */
    char t[48]; t[0] = 0;
    num_append(t, sizeof t, (uint32_t)brw_depth + 1, 0, ' '); str_append(t, sizeof t, " "); str_append(t, sizeof t, T(S_DC_PAGE));
    toast(t, 0);
    return 1;
}

static void enter_dir(int e)
{
    char np[PATH_MAX_FE];
    entry_path(e, np, sizeof np);
    copy_str(cur_path, sizeof cur_path, np);
    if (cur_disp[0]) str_append(cur_disp, sizeof cur_disp, " > ");
    str_append(cur_disp, sizeof cur_disp, entries[e].key[0] ? entries[e].key : entries[e].name);   /* 3.4.7: 긴 이름 그대로 */
    load_dir(0);
}

static void zin_leave(void);
static void go_up(void)
{
    if (zin) { zin_leave(); return; }
    if (!cur_path[0]) return;
    if (at_root()) return;                              /* 3.7.2: VMFolder 위로는 안 감. 3.9.5: 롬 ROMS·카세트 CAS·하드 HARDDISK·BASIC도 */
    int n = 0, last = 0;
    while (cur_path[n]) { if (cur_path[n] == '\\') last = n + 1; n++; }
    char from[FE_KEY_MAX];
    copy_str(from, sizeof from, cur_path + last);
    cur_path[last ? last - 1 : 0] = 0;
    int dn = 0, dl = 0;
    while (cur_disp[dn]) { if (cur_disp[dn] == '>' && dn > 0) dl = dn - 1; dn++; }
    cur_disp[dl] = 0;
    load_dir(from);
}

/* 3.2.0 MED-05: 롬 파일 읽기 — .zip이면 작업 메모리(앞 10 MiB, 상태 저장 자리)에 읽고 안의 첫 롬을 꺼냄 */
static int is_zip(const char *p) { int n = (int)strlen(p); return n > 4 && p[n - 4] == '.' && (p[n - 3] | 32) == 'z' && (p[n - 2] | 32) == 'i' && (p[n - 1] | 32) == 'p'; }
static int rom_nth = -1;                                /* 4.0.34: zip 안 몇 번째 롬인지 (-1 = 첫 롬) */
static int read_rom_file(const char *path, uint8_t *buf, uint32_t max, uint32_t *size)
{
    if (!P.read_file) return -1;
    if (!is_zip(path)) return P.read_file(path, buf, max, size);
    uint32_t zmax = P.scratch_size < (8u << 20) ? P.scratch_size : (8u << 20), zl = 0;   /* 8~10 MiB는 목록 미리보기용 (3.4.4) */
    if (!P.scratch || P.read_file(path, P.scratch, zmax, &zl) || !zl || zl > zmax) return -1;
    int r = rom_nth >= 0 ? zip_extract_nth(P.scratch, zl, "rom|mx1|mx2|ri", rom_nth, buf, max, size, 0, 0, 0) : zip_extract(P.scratch, zl, "rom|mx1|mx2|ri", buf, max, size, 0, 0);
    if (r == ZIPX_NONE) r = zip_extract(P.scratch, zl, "", buf, max, size, 0, 0);   /* 확장자가 다르면 첫 파일 */
    return r == ZIPX_OK ? 0 : -1;
}

/* 4.0.129: 롬을 슬롯 버퍼(2 MiB)에, 넘으면 큰 버퍼(cart_big, 슬롯 하나만)에 읽는다. 돌려주는 값 = 읽은 곳 (NULL = 실패) */
static uint8_t *cart_read_any(int n, const char *path, uint32_t *size)
{
    *size = 0;
    if (!read_rom_file(path, cart_buf[n], MSX_ROM_MAX, size) && *size && *size <= MSX_ROM_MAX) return cart_buf[n];
    if (!cart_big || !cart_big_size || (cart_big_owner >= 0 && cart_big_owner != n && M->cart[cart_big_owner].present)) return 0;
    *size = 0;
    if (read_rom_file(path, cart_big, cart_big_size, size) || !*size || *size <= MSX_ROM_MAX || *size > cart_big_size) return 0;
    return cart_big;
}

/* 3.9.6: zip 안의 첫 exts("cas", "txt|asc|bas", "dsk|hdd|ide") 파일을 메모리로 — 롬 zip과 같은 방식 (USB에 풀어 쓰지 않음).
 * zip은 작업 메모리 앞(8 MB까지)에 읽고, 풀린 파일은 out(max 바이트)에. 0 = 성공. quiet = 알림 없음 */
static int zip_nth_req, zip_nth_total;                  /* 4.0.34: zip 안에 같은 종류가 여럿일 때 몇 번째를 읽을지 (이름순) / 총 개수 */
static int zip_read_member(const char *zpath, const char *exts, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax, int quiet)
{
    uint32_t zl = 0;
    if (!P.read_file || !P.scratch || P.scratch_size < (10u << 20)) return -1;
    if (P.read_file(zpath, P.scratch, 8u << 20, &zl) || !zl || zl > (8u << 20)) { if (!quiet) toast(T(S_HD_ZIP_BIG), 0); return -1; }
    zip_nth_total = 0;
    int r = zip_extract_nth(P.scratch, zl, exts, zip_nth_req, out, max, size, name, namemax, &zip_nth_total);
    zip_nth_req = 0;
    if (r == ZIPX_BIG) { if (!quiet) toast(T(S_HD_ZIP_BIG), 0); return -1; }
    if (r != ZIPX_OK || !*size) { if (!quiet) toast(T(S_HD_ZIP_NONE), zpath); return -1; }
    return 0;
}
/* 카세트: .cas 또는 zip 안의 .cas를 tape_buf로. name = 파일 이름(zip이면 안의 이름) */
static int tape_load_file(const char *path, uint32_t *ts, char *name, int namemax, int quiet)
{
    if (is_zip(path)) { char nm[64]; int r = zip_read_member(path, "cas", tape_buf, TAPE_MAX, ts, nm, sizeof nm, quiet); if (!r && name) copy_str(name, namemax, nm); return r; }
    return !P.read_file || P.read_file(path, tape_buf, TAPE_MAX, ts) || *ts > TAPE_MAX;
}

static const char *zin_exts(void);
static int probe_next = -1;                              /* 종류를 아직 모르는 보이는 줄 (probe_tick) */
static uint8_t probe_redraw;
static void zext_of(const char *nm, char *out)   /* 파일 이름의 확장자(소문자, 5자까지) */
{
    const char *dot = 0;
    for (const char *p = nm; *p; p++) if (*p == '.') dot = p;
    int k = 0;
    if (dot) for (const char *p = dot + 1; *p && k < 5; p++) out[k++] = (char)lower_c(*p);
    out[k] = 0;
}
static void probe_type(int e)
{
    FeDirEntry *x = &entries[e];
    if (x->is_dir || x->type_known) return;
    char path[PATH_MAX_FE];
    entry_path(e, path, sizeof path);
    uint32_t size = x->size, want = x->size < sizeof probe_buf ? x->size : sizeof probe_buf;
    if (browse_disk() || browse_tape()) {               /* 4.0.137: 디스크 크기 / 카세트 내용 종류 (zip이면 안의 파일) */
        int code = 0;
        if (browse_disk()) {
            uint32_t sz = x->size;
            if (is_zip(path) && P.read_file && P.scratch && P.scratch_size >= (10u << 20)) {
                uint32_t zl = 0, zu[4], zd[4]; char nm1[4][48];
                sz = 0;
                if (!P.read_file(path, P.scratch, 8u << 20, &zl) && zl && zl <= (8u << 20)) {
                    if (zip_names_ex(P.scratch, zl, zin_exts(), nm1, zu, zd, 4) > 0) sz = zu[0];
                    else if (zip_names_ex(P.scratch, zl, "", nm1, zu, zd, 4) > 0) {   /* 디스크 파일이 없고 다른 파일만 있는 zip → 구분 칸에 안 파일 확장자, 크기도 안 파일 크기 */
                        x->zdisk = 2; zext_of(nm1[0], x->zx); x->size = zu[0]; x->guess = x->type = (MapperType)0; x->type_known = 1; return;
                    }
                }
            }
            code = sz == 360u * 1024 ? 1 : sz == 720u * 1024 ? 2 : sz == 1440u * 1024 ? 3 : sz == 320u * 1024 ? 4 : sz == 180u * 1024 ? 5 : sz == 160u * 1024 ? 6 : sz ? 7 : 0;
        } else {
            uint32_t ts = 0;
            static const uint8_t hd8[8] = { 0x1F, 0xA6, 0xDE, 0xBA, 0xCC, 0x13, 0x7D, 0x74 };
            const uint8_t *b = 0;
            if (is_zip(path)) { uint8_t *tmp = P.scratch_size >= (10u << 20) ? P.scratch + (8u << 20) : 0; char nm[64]; if (tmp && !zip_read_member(path, "cas", tmp, 2u << 20, &ts, nm, sizeof nm, 1) && ts >= 18) b = tmp; }
            else if (P.read_file && !P.read_file(path, probe_buf, want, &size) && size >= 18) b = probe_buf;
            if (b && !memcmp(b, hd8, 8)) { uint8_t t = b[8]; code = t == 0xD3 ? 1 : t == 0xEA ? 2 : t == 0xD0 ? 3 : 4; }
        }
        x->guess = x->type = (MapperType)code;
        x->type_known = 1;
        return;
    }
    if (is_zip(path) && P.read_file && P.scratch && P.scratch_size >= (10u << 20)) {   /* zip 안이 .dsk 등 디스크 이미지뿐이면 종류에 Disk */
        uint32_t zl = 0, zu[4], zd[4]; char nm1[4][48];
        if (!P.read_file(path, P.scratch, 8u << 20, &zl) && zl && zl <= (8u << 20)
            && zip_names_ex(P.scratch, zl, "rom|mx1|mx2|ri", nm1, zu, zd, 4) <= 0) {
            if (zip_names_ex(P.scratch, zl, "dsk|di1|di2|360|720", nm1, zu, zd, 4) > 0) { x->zdisk = 1; x->size = zu[0]; x->type_known = 1; return; }
            if (zip_names_ex(P.scratch, zl, "", nm1, zu, zd, 4) > 0) { x->zdisk = 2; zext_of(nm1[0], x->zx); x->size = zu[0]; x->type_known = 1; return; }   /* 롬도 디스크도 아닌 파일뿐 → 안 파일 확장자·크기 */
        }
    }
    if (is_zip(path)) {                                 /* 3.2.0: zip은 꺼내서 */
        uint8_t *tmp = P.scratch_size >= (10u << 20) ? P.scratch + (8u << 20) : 0;   /* 3.4.4: 8~10 MiB (롬 2 MiB까지) */
        if (tmp && !read_rom_file(path, tmp, MSX_ROM_MAX, &size) && size) {
            x->guess = cart_guess_mapper(tmp, size);
            x->size = size;
            if (!x->user_type) x->type = x->guess;
        }
    } else if (P.read_file && !P.read_file(path, probe_buf, want, &size)) {
        x->guess = cart_guess_mapper_partial(probe_buf, want, size);
        if (!x->user_type) x->type = x->guess;
    }
    x->type_known = 1;
}

static char zin_path[PATH_MAX_FE], zin_key[FE_KEY_MAX], zin_disp0[PATH_MAX_FE];
static char zin_nm[64][48];
static const char *zin_exts(void)
{
    return browse_bas() ? "txt|asc|bas" : browse_tape() ? "cas" : browse_hd() ? "hdd|ide|hdi|dsk|img" : browse_disk() ? "dsk|di1|di2|360|720" : "rom|mx1|mx2|ri";
}
/* 4.0.34: zip에 이 창 종류 파일이 둘 이상이면 zip 안으로 들어가 고르게 함. 1 = 들어갔음 (아니면 평소처럼 바로 넣음) */
static int zin_enter(int e, int force)   /* force(Alt+Enter) = 하나뿐이어도 안을 보여 줌 (롬 창이면 디스크 파일도 보임) */
{
    char path[PATH_MAX_FE];
    entry_path(e, path, sizeof path);
    uint32_t zl = 0;
    if (!P.read_file || !P.scratch || P.scratch_size < (10u << 20)) return 0;
    if (P.read_file(path, P.scratch, 8u << 20, &zl) || !zl || zl > (8u << 20)) return 0;
    uint32_t zu[64], zd[64];
    int rom_win = !browse_bas() && !browse_tape() && !browse_hd() && !browse_disk();
    int n = zip_names_ex(P.scratch, zl, force && rom_win ? "" : force && browse_disk() ? "" : zin_exts(), zin_nm, zu, zd, 64);   /* 디스크 창도 Alt+Enter 면 모든 파일 */
    if (n < (force ? 1 : 2)) return 0;                                /* 하나뿐이면 들어가지 않고 zip 그대로 넣음 (마스터님: 그래야 다음 디스크가 zip 차례로 됨) */
    copy_str(zin_path, sizeof zin_path, path);
    copy_str(zin_key, sizeof zin_key, entries[e].key[0] ? entries[e].key : entries[e].name);
    copy_str(zin_disp0, sizeof zin_disp0, cur_disp);
    if (cur_disp[0]) str_append(cur_disp, sizeof cur_disp, " > ");
    str_append(cur_disp, sizeof cur_disp, zin_key);
    for (int i = 0; i < n; i++) {
        FeDirEntry *x = &entries[i];
        memset(x, 0, sizeof *x);
        copy_str(x->name, sizeof x->name, zin_nm[i]);
        x->size = zu[i];                                /* 4.0.136: 풀었을 때 크기·수정 시각 */
        x->year = (uint16_t)(1980 + ((zd[i] >> 25) & 0x7F)); x->mon = (uint8_t)((zd[i] >> 21) & 15); x->day = (uint8_t)((zd[i] >> 16) & 31);
        x->hour = (uint8_t)((zd[i] >> 11) & 31); x->min = (uint8_t)((zd[i] >> 5) & 63);
        if (rom_win) { x->zdisk = media_ext_in("dsk di1 di2 360 720", zin_nm[i]) ? 1 : media_ext_in("rom mx1 mx2 ri", zin_nm[i]) ? 0 : 2; if (x->zdisk == 2) zext_of(zin_nm[i], x->zx); x->type_known = 1; x->guess = x->type = MAPPER_PLAIN; continue; }   /* 롬 창 — 디스크 파일은 Disk 표시 */
        if (browse_disk() && !media_ext_in("dsk di1 di2 360 720", zin_nm[i])) { x->zdisk = 2; zext_of(zin_nm[i], x->zx); x->type_known = 1; x->guess = x->type = (MapperType)0; continue; }   /* 디스크가 아닌 파일은 확장자로 표시 */
        { uint32_t sz = zu[i]; int c = browse_tape() ? 4 : sz == 360u * 1024 ? 1 : sz == 720u * 1024 ? 2 : sz == 1440u * 1024 ? 3 : sz == 320u * 1024 ? 4 : sz == 180u * 1024 ? 5 : sz == 160u * 1024 ? 6 : sz ? 7 : 0;
            x->type_known = 1; x->guess = x->type = (MapperType)c; }
    }
    nent = n; nlist = 0;
    list[nlist++] = ITEM_UP;
    for (int i = 0; i < n; i++) list[nlist++] = i;
    menu_sel = 1; menu_top = 0; brw_next = -1;
    zin = 1;
    browse_full = 1; menu_dirty = 1;
    return 1;
}
static void zin_leave(void)
{
    zin = 0;
    copy_str(cur_disp, sizeof cur_disp, zin_disp0);
    load_dir(zin_key);
}

static void open_browser(int slot)
{
    browse_slot = slot; zin = 0;
    dir_naming = 0; del_ask = 0; browse_showall = 0;
    /* 매체 종류마다 마지막 폴더와 마지막으로 고른 항목 */
    if (browse_tape()) { copy_str(cur_path, sizeof cur_path, tape_dir); copy_str(cur_disp, sizeof cur_disp, tape_dir_disp); }
    else if (browse_bas()) { if (P.create && !strcmp(bas_dir, FE_BAS)) P.create(bas_dir, 1); copy_str(cur_path, sizeof cur_path, bas_dir); copy_str(cur_disp, sizeof cur_disp, bas_dir_disp); }
    else if (browse_hd()) { copy_str(cur_path, sizeof cur_path, hd_dir); copy_str(cur_disp, sizeof cur_disp, hd_dir_disp); }
    else if (browse_imgdir()) { copy_str(cur_path, sizeof cur_path, img_dir); copy_str(cur_disp, sizeof cur_disp, img_dir_disp); }
    else if (browse_dir()) { copy_str(cur_path, sizeof cur_path, dir_dir); copy_str(cur_disp, sizeof cur_disp, dir_dir_disp); }
    else if (browse_disk()) { copy_str(cur_path, sizeof cur_path, disk_dir); copy_str(cur_disp, sizeof cur_disp, disk_dir_disp); }
    else if (browse_vhd()) { copy_str(cur_path, sizeof cur_path, vbr_dir); copy_str(cur_disp, sizeof cur_disp, vbr_dir_disp); }
    else { copy_str(cur_path, sizeof cur_path, rom_path); copy_str(cur_disp, sizeof cur_disp, rom_disp); }
    {   const char *rt = browse_root();                 /* 3.9.5: 기억한 폴더가 뿌리 밖이면 뿌리로 */
        if (rt && !path_under(cur_path, rt)) browse_home(cur_path, sizeof cur_path, cur_disp, sizeof cur_disp);
    }
    browse_opened = 1;
    bar_visible = 0;
    depth = 0;
    dialog = 1;
    input_init(&in);
    brw_loaded[0] = 0;                                  /* 4.0.34: 열 때마다 처음 창부터 */
    load_dir(browse_imgdir() ? 0 : browse_dir() ? dir_sel : browse_vhd() ? vbr_sel : browse_bas() ? bas_sel : browse_tape() ? tape_sel : browse_hd() ? hd_sel : browse_disk() ? disk_sel : rom_sel);
    if (list_error) {                                   /* 기억한 폴더가 없어졌으면 기준 폴더로 (4.0.13: 뿌리가 아니라 각자의 기준 폴더) */
        browse_home(cur_path, sizeof cur_path, cur_disp, sizeof cur_disp);
        load_dir(0);
    }
    if (list_error && P.create) { P.create(cur_path, 1); load_dir(0); }   /* 기준 폴더가 아예 없으면 만듦 */
    int top = browse_imgdir() ? 0 : browse_dir() ? dir_top : browse_vhd() ? vbr_top : browse_bas() ? bas_top : browse_tape() ? tape_top : browse_hd() ? hd_top : browse_disk() ? disk_top : rom_top;       /* 마지막 스크롤 위치 (고른 항목이 보이는 범위에서) */
    if (top >= 0 && top <= menu_sel && top < nlist) menu_top = top;
    full_redraw();
}

static void open_dialog(int kind)
{
    if (kind != 6 && kind != 18 && kind != 19) bgw_finish();   /* 창이 scratch·영상 버퍼를 쓸 수 있음 (믹서·MIDI 믹서·네트워크는 MSX 가 계속 도는 창이라 그대로) */
    grow_state = 0;
    if (kind == 6) props_fix_sel();
    bar_visible = 0;
    depth = 0;
    dialog = kind;
    input_init(&in);
    full_redraw();
    menu_dirty = 1;
}

/* ---- blueMSX 롬 데이터베이스 (USB\Databases\msxromdb.xml, msxsysromdb.xml), 요구사항 12 ---- */
static uint8_t romdb_buf[1048576], sysdb_buf[262144], bindb_buf[262144];
static uint32_t romdb_len, sysdb_len, bindb_len;          /* 3.2.15: bindb = openMSX softwaredb에서 만든 이진 DB */
static int romdb_tried;
static void romdb_load(void);
static void pf_text(char *t, int n, int worst)
{
    static const char *const nm[PF_N + 1] = { "core", "audio", "midi", "video", "io", "poll", "blit", "usbw", "idle", "file", "mouse", "synth", "other" };
    t[0] = 0;
    for (int k = 0; k < PF_N + (worst ? 0 : 1); k++) {
        str_append(t, n, nm[k]); str_append(t, n, " ");
        num_append(t, n, worst ? pf_worst[k] : pf_pct[k], 0, ' ');
        str_append(t, n, worst ? "ms  " : "%  ");
    }
}
static uint32_t gap_last, gap_max, gap_show, gap_reset;
static uint32_t gap_show_pub(void) { return gap_show; }     /* 프레임 간격 측정 (진단) */

static void romdb_load(void)
{
    if (romdb_tried || !P.read_file) return;
    romdb_tried = 1;
    uint32_t n = 0;
    if (!P.read_file("0:\\Databases\\msxromdb.xml", romdb_buf, sizeof romdb_buf - 1, &n)) romdb_len = n < sizeof romdb_buf ? n : sizeof romdb_buf - 1;
    n = 0;
    if (!P.read_file("0:\\Databases\\msxsysromdb.xml", sysdb_buf, sizeof sysdb_buf - 1, &n)) sysdb_len = n < sizeof sysdb_buf ? n : sizeof sysdb_buf - 1;
    n = 0;
    if (!P.read_file("0:\\Databases\\x86msx_romdb.bin", bindb_buf, sizeof bindb_buf, &n)) bindb_len = n <= sizeof bindb_buf ? n : 0;
}


/* 3.2.1 MED-03: USB 폴더의 파일로 720 KB 디스크를 만들어 넣음 (하위 폴더는 빼고, 이름은 8+3으로). 바뀐 내용은 폴더에 쓰지 않음 */
static FeDirEntry dd_ent[256];
static void dir_as_disk(int d, const char *dir, const char *disp)
{
    if (!P.list_dir || !P.read_file || !P.scratch || P.scratch_size < (1u << 20)) { toast(T(S_LOAD_ERROR), disp); return; }
    disk_flush(d);
    uint32_t n = dsk_build(0, disk_buf[d], "FOLDER");
    dsk_boot_dos1(disk_buf[d]);                         /* 3.6.1: blueMSX처럼 보통 MSX 부트 섹터 (MSXDOS.SYS가 있으면 MSX-DOS로) */
    MsxFat f;
    if (mf_open(&f, disk_buf[d], n)) { toast(T(S_LOAD_ERROR), disp); return; }
    int cnt = P.list_dir(dir, dd_ent, 256), put = 0, skip = 0;
    for (int i = 0; i < cnt; i++) {
        if (dd_ent[i].is_dir) continue;
        char p[PATH_MAX_FE];
        copy_str(p, sizeof p, dir); str_append(p, sizeof p, "\\"); str_append(p, sizeof p, dd_ent[i].key[0] ? dd_ent[i].key : dd_ent[i].name);
        uint32_t sz = 0;
        if (dd_ent[i].size > 720u * 1024 || P.read_file(p, P.scratch, 720u * 1024, &sz)) { skip++; continue; }
        uint8_t raw[11];
        mf_make83(dd_ent[i].name, raw);
        uint16_t dt = dd_ent[i].year ? (uint16_t)(((dd_ent[i].year - 1980) << 9) | (dd_ent[i].mon << 5) | dd_ent[i].day) : 0;
        uint16_t tm = dd_ent[i].year ? (uint16_t)((dd_ent[i].hour << 11) | (dd_ent[i].min << 5)) : 0;
        if (mf_write(&f, 0, raw, P.scratch, sz, dt, tm)) { skip++; continue; }
        put++;
    }
    disk_size[d] = n;
    disk_path[d][0] = 0;
    copy_str(disk_name[d], sizeof disk_name[d], disp);
    str_append(disk_name[d], sizeof disk_name[d], " (");
    num_append(disk_name[d], sizeof disk_name[d], (uint32_t)put, 0, ' ');
    str_append(disk_name[d], sizeof disk_name[d], ")");
    fdc_insert(&M->fdc, d, disk_buf[d], n, 0);
    if (skip) toast(T(S_DIR_DISK_SKIP), disk_name[d]);
    settings_changed();
}

static int dz_idx[2], dz_total[2], dz_req = -1;         /* 4.0.34: zip 안 몇 번째 디스크인지 / zip 안 디스크 수 / 다음 디스크가 요청한 번호 */
static void insert_disk_file(int d, const char *path, const char *name, int quiet)
{
    uint32_t size = 0;
    char zn[64]; zn[0] = 0;
    disk_flush(d);
    if (is_zip(path)) {                                 /* 4.0.34: zip 안의 첫 디스크 이미지 (바뀐 내용은 zip에 저장하지 않음). 이름은 안의 파일 이름 */
        zip_nth_req = dz_req >= 0 ? dz_req : 0;
        int want_i = zip_nth_req;
        if (zip_read_member(path, "dsk|di1|di2|360|720", disk_buf[d], DISK_MAX, &size, zn, sizeof zn, quiet) || size % 512) size = 0;
        else {
            if (zn[0]) name = zn;
            dz_total[d] = zip_nth_total; dz_idx[d] = zip_nth_total ? ((want_i % zip_nth_total) + zip_nth_total) % zip_nth_total : 0;
        }
    } else { dz_total[d] = 0; dz_idx[d] = 0; }
    if (is_zip(path)) {
    } else
    if (!P.read_file || P.read_file(path, disk_buf[d], DISK_MAX, &size) || !size || size > DISK_MAX || size % 512) {
        size = 0;
    }
    if (!size) {
        if (!quiet) toast(T(S_LOAD_ERROR), name);
        return;
    }
    disk_size[d] = size;
    copy_str(disk_path[d], sizeof disk_path[d], path);
    copy_str(disk_name[d], sizeof disk_name[d], name);
    fdc_insert(&M->fdc, d, disk_buf[d], size, disk_wp[d]);
    recent_add(1, path, name);                          /* 2.12: 최근 파일 */
    settings_changed();
}
void fe_insert_disk(int d, const char *path) { insert_disk_file(d & 1, path, path, 1); }   /* 3.5.5: 시험 틀용 */
static void dir_as_disk(int d, const char *dir, const char *disp);
void fe_insert_dir(int d, const char *dir) { dir_as_disk(d & 1, dir, dir); if (disk_size[d & 1]) power_on(); }   /* 3.6.1: 시험 틀용 (넣고 리셋) */

/* ---- 하드 디스크 (2.8) ---- */
static uint8_t *hz_ptr[2]; static uint32_t hz_len[2];   /* 4.0.34: zip에서 풀어 메모리(가상 하드 작업 메모리)에 둔 하드 디스크 (저장 안 함) */
static int hd_raw_read(int u, uint32_t lba, uint8_t *b)
{
    if (hz_ptr[u & 1]) { if ((uint64_t)(lba + 1) * 512 > hz_len[u & 1]) return -1; memcpy(b, hz_ptr[u & 1] + (size_t)lba * 512, 512); return 0; }
    return P.hd_read ? P.hd_read(u, lba, b) : -1;
}
static int hd_rd_cb(void *c, uint32_t lba, uint8_t *b)  /* c = 장치 (0 마스터, 1 슬레이브) */
{
    hd_last_ms = P.ms();
    int r = hd_raw_read((int)(uintptr_t)c, lba, b);
    /* 3.4.4: 3.0.2~3.4.3의 이미지 만들기 "MSX-DOS2 FAT12" 부트 섹터(PC 모양: EB 3C, 1Ch~1Fh 숨은 섹터 32비트, 26h = 29h)는
     * DOS2 커널이 C01Eh를 부를 때 엉뚱한 코드가 돌아 멈춤 → 읽을 때만 MSX-DOS2 모양으로 고쳐 줌 (파일은 그대로) */
    if (!r && lba && lba < 64 && b[0] == 0xEB && b[1] == 0x3C && !memcmp(b + 3, "MSXDOS20", 8) && b[0x26] == 0x29 && !memcmp(b + 0x36, "FAT12   ", 8)
        && b[0x1E] == 0 && b[0x1F] == 0 && b[0x3E] == 0xC9) {
        b[1] = 0xFE; b[0x1E] = 0x18; b[0x1F] = 0x1E;
        memcpy(b + 0x20, "VOL_ID", 6); b[0x26] = 0x00;
    }
    /* 4.x~5.0.3에서 만든 MSX-DOS1 이미지의 MSXDOS.SYS는 LD SP,C800h로 고쳐져 있음 → 커널이 C300~CA7F에 놓이는 기종(소니 F700P)에서
     * 스택이 덮여 깨진 글자·되돌아가기 오류 → 읽을 때만 C200h로 (파일은 그대로) */
    if (!r && b[0] == 0xC3 && b[0x138] == 0x31 && b[0x139] == 0x00 && b[0x13A] == 0xC8 && b[0x13B] == 0x21 && b[0x13C] == 0xC9 && b[0x13D] == 0xF1)
        b[0x13A] = 0xC2;
    return r;
}
static int hd_wr_cb(void *c, uint32_t lba, uint8_t *b)
{
    hd_last_ms = hd_write_ms = P.ms();
    hd_dirty = 1;
    if (hz_ptr[(int)(uintptr_t)c & 1]) { uint32_t q = (uint32_t)(uintptr_t)c & 1; if ((uint64_t)(lba + 1) * 512 > hz_len[q]) return -1; memcpy(hz_ptr[q] + (size_t)lba * 512, b, 512); return 0; }
    return P.hd_write ? P.hd_write((int)(uintptr_t)c, lba, b) : -1;
}
static void hd_flush(void) { if (hd_dirty && P.hd_flush) P.hd_flush(); hd_dirty = 0; }

static void hd_eject_unit(int u)                        /* 3.4.3: 0 마스터(슬레이브도 함께), 1 슬레이브 */
{
    hd_flush();
    if (hd2_secs && P.hd_close && !hz_ptr[1]) P.hd_close(1);
    hz_ptr[1] = 0;
    hd2_secs = 0; hd2_path[0] = 0; hd2_name[0] = 0;
    ide_attach(&M->ide_slave, 0, 0, 0, 0);
    if (u) return;
    if (vhd_img) {                                      /* 하드 이미지용으로 자동으로 꽂은 가상 디스크 카트리지도 빼기 */
        vhd_img = 0;
        int vn = vhd_cart();
        if (vn >= 0 && !vhd_any()) { cart_remove(vn); cart_special[vn] = 0; cart_path[vn][0] = 0; cart_name[vn][0] = 0; cart_mapper[vn] = -1; cart_size[vn] = 0; }
    }
    if (hd_secs && P.hd_close && !hz_ptr[0]) P.hd_close(0);
    hz_ptr[0] = 0;
    hd_secs = 0; hd_path[0] = 0; hd_name[0] = 0;
    ide_attach(&M->ide, 0, 0, 0, 0);
}
static void hd_eject(void) { hd_eject_unit(0); }

/* 3.9.1: 자동으로 꽂을 빈 카트리지 슬롯. 기본은 기종의 마지막 슬롯(슬롯이 2개면 2번, 5개면 5번), 거기에 이미 뭐가 있으면 그 앞의 빈 슬롯. 없으면 -1 */
static int free_cart_slot(void)
{
    for (int k = MSX_CART_SLOTS - 1; k >= 0; k--) if (M->cart_slot[k] >= 0 && !M->cart[k].present) return k;
    return -1;
}
static int sp_code(int kind);
/* 3.9.4: 하드 디스크 이미지의 FAT 볼륨(LBA base) 맨 위(루트) 폴더에 NEXTOR.SYS(5.0.1: 또는 MSXDOS2.SYS)가 있나 (FAT12/16) */
static int hdv_has_nextor(uint32_t base)
{
    static uint8_t b[512];
    if (hd_raw_read(0, base, b)) return 0;
    uint32_t bps = (uint32_t)(b[11] | (b[12] << 8)), spc = b[13], rsv = (uint32_t)(b[14] | (b[15] << 8)), nf = b[16], rent = (uint32_t)(b[17] | (b[18] << 8)), fsz = (uint32_t)(b[22] | (b[23] << 8));
    if (bps != 512 || !spc || (spc & (spc - 1)) || !rsv || !nf || nf > 2 || !rent || !fsz) return 0;
    uint32_t root = base + rsv + nf * fsz, ns = (rent * 32 + 511) / 512;
    int r = 0;
    for (uint32_t s = 0; s < ns && s < 64; s++) {
        if (hd_raw_read(0, root + s, b)) return r;
        for (int i = 0; i < 16; i++) {
            const uint8_t *e = b + i * 32;
            if (e[0] == 0) return r;
            if (e[0] == 0xE5 || (e[11] & 0x18)) continue;
            if (!memcmp(e, "NEXTOR  SYS", 11)) return 1;
            if (!memcmp(e, "MSXDOS2 SYS", 11)) r = 2;   /* MSXDOS2.SYS만 있어도 MSX-DOS2 (Nextor 2.1 이상 커널이 부팅, MSXDOS2.ROM 없이) */
        }
    }
    return r;
}
static int hd_has_nextor(void)                          /* 열어 둔 마스터 이미지 (FAT 볼륨 하나이거나 파티션 표) */
{
    int r = hdv_has_nextor(0);
    vhd_img_off = 0; vhd_img_n = 0;
    if (r == 1) return 1;
    static uint8_t m[512];
    if (hd_raw_read(0, 0, m) || m[510] != 0x55 || m[511] != 0xAA) return r;
    for (int i = 0; i < 4; i++) {
        const uint8_t *p = m + 0x1BE + i * 16;
        uint32_t st = (uint32_t)p[8] | ((uint32_t)p[9] << 8) | ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
        uint32_t ns = (uint32_t)p[12] | ((uint32_t)p[13] << 8) | ((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24);
        int q = (p[4] && st) ? hdv_has_nextor(st) : 0;
        if (q > r) { r = q; vhd_img_off = st; vhd_img_n = ns; }
        if (q == 1) return 1;
    }
    return r;
}
/* 자동으로 꽂은 하드 디스크 카트리지(IDE (Sunrise) 111, IDE (Sunrise, Nextor) 113) 빼기. 하드 디스크 이미지를 모두 꺼냈을 때 */
static void hd_auto_cart_remove(int only_other)
{
    for (int n = 0; n < MSX_CART_SLOTS; n++) {
        if (!M->cart[n].present || !CART_IS_IDE(M->cart[n].mapper)) continue;
        if (cart_special[n] != sp_code(11) && cart_special[n] != sp_code(13)) continue;
        if (only_other && cart_special[n] == sp_code(only_other)) continue;
        cart_remove(n);
        cart_special[n] = 0; cart_path[n][0] = 0; cart_name[n][0] = 0; cart_mapper[n] = -1; cart_size[n] = 0;
    }
}
static int ide_cart_slot(void)                          /* IDE 카트리지가 꽂힌 카트리지 번호, 없으면 -1 */
{
    for (int n = 0; n < MSX_CART_ALL; n++) if (M->cart[n].present && CART_IS_IDE(M->cart[n].mapper)) return n;   /* 3.4.0: 내장 IDE 포함 */
    return -1;
}

/* 하드 디스크 이미지 넣기. IDE 카트리지가 없으면 빈 카트리지 슬롯에 함께 꽂는다.
 * quiet = 1이면 알림·리셋 없이 (기종 환경 되살리기) */
static char hz_name[64];
static int hz_open(int u, const char *path, uint32_t *secs, int quiet)   /* zip 안의 첫 하드 디스크 이미지를 메모리로 (12 MiB까지) */
{
    const uint32_t slot = 12u << 20;
    uint32_t sz = 0; char zn[64]; zn[0] = 0;
    if (!P.vhd_mem || P.vhd_mem_size < 2 * slot || vhd_any()) { if (!quiet) toast(T(S_HD_ZIP_BIG), 0); return -1; }
    uint8_t *o = P.vhd_mem + (u ? slot : 0);
    if (zip_read_member(path, "hdd|ide|hdi|dsk|img", o, slot, &sz, zn, sizeof zn, quiet) || sz < 512 || sz % 512) return -1;
    hz_ptr[u] = o; hz_len[u] = sz; *secs = sz / 512;
    copy_str(hz_name, sizeof hz_name, zn);
    return 0;
}
static int hd_insert(const char *path, const char *name, int quiet)
{
    if (vhd_any()) { if (!quiet) toast(T(S_HD_EXCL_VHD), 0); return -1; }   /* 3.8.2 */
    hd_eject();
    uint32_t secs = 0;
    if (is_zip(path) ? hz_open(0, path, &secs, quiet) : (!P.hd_open || P.hd_open(0, path, &secs) || !secs)) { if (!quiet) toast(T(S_HD_FAIL), name); return -1; }
    int nx = hd_has_nextor();
    if (!nx) nx = 1;                                    /* MSX-DOS1 이미지도 내장 Nextor 2.1.3 카트리지로 (아래 want 설명) */
    int want = 13;                                      /* MSX-DOS1 이미지도 Nextor 커널로 (Sunrise 2.4 롬은 메모리 매퍼 96KB 이상이 있어야 하는데 MSX1·turboR·64KB 매퍼 기종에서 멈춤) — 롬이 없으면 아래에서 일반 Sunrise로 */
    if (nx) {                                           /* MSXDOS2.SYS만 있거나 Sunrise Nextor 롬이 없으면 → Nextor 2.1.3 가상 디스크 카트리지 (장치 8) */
        int vn = vhd_cart(), fresh = 0;
        if (vn < 0 && ide_cart_slot() < 0) {
            int fn = free_cart_slot();
            int have13 = 0;
            if (fn >= 0 && nx == 2) {                   /* MSXDOS2.SYS만 있으면 2.1.0 이상 Sunrise 롬이 필요 (2.0.x는 못 읽음) */
                static const char *const n21[] = { "0:\\Machines\\Shared Roms\\Nextor-2.1.1.SunriseIDE.ROM", "0:\\Machines\\Shared Roms\\Nextor-2.1.0.SunriseIDE.ROM", "0:\\Machines\\Shared Roms\\Nextor-2.1.0.SunriseIDE.emulators.ROM" };
                int ok21 = 0; uint32_t sz21 = 0;
                for (int i = 0; i < 3 && !ok21; i++) ok21 = P.read_file && !P.read_file(n21[i], cart_buf[fn], MSX_ROM_MAX, &sz21) && sz21;
                if (!ok21) fn = -2;
            }
            if (fn >= 0) { have13 = !special_apply(fn, 13, 1); if (have13) cart_remove(fn), cart_special[fn] = 0, cart_path[fn][0] = 0, cart_name[fn][0] = 0, cart_mapper[fn] = -1, cart_size[fn] = 0; }
            if (fn == -2) fn = free_cart_slot();
            if (fn >= 0 && !have13 && !special_apply(fn, 49, 1)) { vn = fn; fresh = 1; }
        } else if (nx == 2 && vn < 0) {
            int fn = free_cart_slot();
            if (fn >= 0 && !special_apply(fn, 49, 1)) { hd_auto_cart_remove(0); vn = fn; fresh = 1; }
        }
        if (vn >= 0) {
            (void)fresh;
            hd_auto_cart_remove(0);
            vhd_img = 1; vhd_img_off = 0; vhd_img_n = 0; vhd_img_chg();   /* MBR째로 (폴더 가상 하드도 MBR + 파티션) */
            goto hd_cart_ok;
        }
    }
    hd_auto_cart_remove(want);                          /* 자동으로 꽂았던 다른 종류는 빼고 알맞은 것으로 */
    int n = ide_cart_slot();
    if (n < 0) {
        n = free_cart_slot();
        if (n >= 0 && want == 13 && special_apply(n, 13, 1)) want = 11;           /* Nextor 롬이 없으면 일반 Sunrise로 */
        else if (n >= 0 && want == 13) goto hd_cart_ok;
        if (n < 0 || special_apply(n, want, 1)) {
            if (P.hd_close && !hz_ptr[0]) P.hd_close(0);
            hz_ptr[0] = 0;
            if (!quiet) toast(n < 0 ? T(S_NO_FREE_SLOT) : T(S_HD_FAIL), n < 0 ? 0 : name);
            return -1;
        }
    }
hd_cart_ok:
    hd_secs = secs;
    copy_str(hd_path, sizeof hd_path, path);
    copy_str(hd_name, sizeof hd_name, hz_ptr[0] && hz_name[0] ? hz_name : name);
    if (!vhd_img) ide_attach(&M->ide, secs, hd_rd_cb, hd_wr_cb, 0);
    if (!quiet) {
        settings_changed();
        menu_close_all();
        if (hd_reset_insert) power_on(); else full_redraw();   /* 3.9.8: 삽입 후 다시 시작 설정 (꺼두면 그대로 계속) */
    }
    return 0;
}

/* 3.4.3 DEV-15: IDE 슬레이브 넣기 (마스터가 있을 때) */
static int hd2_insert(const char *path, const char *name, int quiet)
{
    if (vhd_any()) { if (!quiet) toast(T(S_HD_EXCL_VHD), 0); return -1; }   /* 3.8.2 */
    hd_eject_unit(1);
    uint32_t secs = 0;
    if (!hd_secs || (is_zip(path) ? hz_open(1, path, &secs, quiet) : (!P.hd_open || P.hd_open(1, path, &secs) || !secs))) { if (!quiet) toast(T(S_HD_FAIL), name); return -1; }
    hd2_secs = secs;
    copy_str(hd2_path, sizeof hd2_path, path);
    copy_str(hd2_name, sizeof hd2_name, hz_ptr[1] && hz_name[0] ? hz_name : name);
    ide_attach(&M->ide_slave, secs, hd_rd_cb, hd_wr_cb, (void *)(uintptr_t)1);
    if (!quiet) {
        settings_changed();
        menu_close_all();
        if (hd_reset_insert && ide_cart_slot() >= 0) power_on(); else full_redraw();
    }
    return 0;
}

static void cart_insert_file(int n, const char *path, const FeDirEntry *xe);
static void rt_dd_open(void);
static void vb_dd_apply(int i);
static int dd_id;
static void dd_key_event(int key);
static void dd_draw_at(uint32_t x, uint32_t y0, uint32_t w, uint32_t bottom);
static void recent_add(int kind, const char *path, const char *name);
static void bas_paste_file(const char *path, const char *name);
/* 3.3.2 MED-08: 카세트 DB (Databases\x86msx_casdb.bin, tools/mkcasdb.py가 blueMSX msxcaswavdb.xml에서 만듦)에서 .cas SHA-1로 제목·시작 명령 */
static void tape_db_lookup(uint32_t ts)
{
    tape_title[0] = tape_start[0] = 0;
    if (!P.read_file || !P.scratch || P.scratch_size < (10u << 20) || !ts) return;
    uint8_t *db = P.scratch + (9u << 20);              /* 9~10 MiB (zip·sp_scan 자리, 잠깐) */
    uint32_t n = 0;
    if (P.read_file("0:\\Databases\\x86msx_casdb.bin", db, 1u << 20, &n) || n < 12 || memcmp(db, "X86CASDB", 8)) return;
    char hex[41]; sha1_hex(tape_buf, ts, hex);
    uint8_t h[20];
    for (int i = 0; i < 20; i++) { int a = hex[2 * i], b = hex[2 * i + 1]; h[i] = (uint8_t)(((a <= '9' ? a - '0' : a - 'a' + 10) << 4) | (b <= '9' ? b - '0' : b - 'a' + 10)); }
    for (uint32_t o = 12; o + 22 <= n; ) {
        const uint8_t *r = db + o;
        uint32_t tl = r[20], sl = o + 21 + tl < n ? r[21 + tl] : 0;
        if (o + 22 + tl + sl > n) break;
        if (!memcmp(r, h, 20)) {
            uint32_t k = tl < sizeof tape_title - 1 ? tl : sizeof tape_title - 1;
            memcpy(tape_title, r + 21, k); tape_title[k] = 0;
            k = sl < sizeof tape_start - 1 ? sl : sizeof tape_start - 1;
            memcpy(tape_start, r + 22 + tl, k); tape_start[k] = 0;
            toast(tape_title, tape_start[0] ? tape_start : 0);
            return;
        }
        o += 22 + tl + sl;
    }
}
/* 4.0.34: 이 창에 맞는 종류인지. 다른 종류(롬·디스크·하드·카세트)면 알림 글자 번호, 알 수 없는 종류도 거절, 맞으면 -1 */
static int wrong_kind(const char *nm)
{
    if (media_accepts(browse_kind(), nm)) return -1;   /* 받을 확장자·종류 짐작은 미디어 종류 표 (src/media.c) */
    int g = media_kind_guess(nm);
    return g == MK_ROM ? S_WK_ROM : g == MK_DISK ? S_WK_DISK : g == MK_HD ? S_WK_HD : g == MK_TAPE ? S_WK_TAPE : S_WK_OTHER;
}
static void insert_selected_in(int e)
{
    char path[PATH_MAX_FE];
    int wk = wrong_kind(entries[e].key[0] ? entries[e].key : entries[e].name);
    if (wk >= 0) { toast(T((StrId)wk), entries[e].name); return; }
    if (entries[e].zdisk == 2) {   /* 디스크 창에서 디스크 아닌 파일(zip 안 포함)은 넣지 않음 */
        char fk[16] = "x."; str_append(fk, sizeof fk, entries[e].zx);
        int g = media_kind_guess(fk);
        toast(T(g == MK_ROM ? S_WK_ROM : g == MK_HD ? S_WK_HD : g == MK_TAPE ? S_WK_TAPE : S_WK_OTHER), entries[e].name); return;
    }
    if (entries[e].zdisk == 1 && browse_slot < MSX_CART_SLOTS) { toast(T(S_WK_DISK), entries[e].name); return; }   /* 롬 창에서 디스크 이미지(zip 안 .dsk 포함)는 넣지 않음 */
    if (zin) copy_str(path, sizeof path, zin_path); else entry_path(e, path, sizeof path);
    int n = browse_slot;
    int zi = zin ? e : -1;
    if (zi >= 0) { zip_nth_req = zi; dz_req = zi; rom_nth = zi; }
    browse_remember();
    if (browse_bas()) { dialog_done(); bas_paste_file(path, entries[e].name); return; }   /* 3.2.7 (3.9.6: zip이면 안의 텍스트) */
    if (browse_tape()) {                                /* 카세트 테이프 */
        dialog_done();
        tape_flush();
        uint32_t ts = 0;
        char tn[64];
        copy_str(tn, sizeof tn, entries[e].name);
        if (tape_load_file(path, &ts, tn, sizeof tn, 0)) { if (!is_zip(path)) toast(T(S_LOAD_ERROR), tn); return; }
        copy_str(tape_path, sizeof tape_path, path);
        copy_str(tape_name, sizeof tape_name, tn);
        msx_tape_insert(M, tape_buf, ts, TAPE_MAX);
        tape_db_lookup(ts);                             /* 3.3.2 MED-08 */
        recent_add(2, path, tn);
        return;
    }
    if (browse_hd()) {                                  /* 하드 디스크 (4.0.3: zip은 받지 않음) */
        dialog_done();
        if (browse_slot == 17) hd2_insert(path, entries[e].name, 0); else hd_insert(path, entries[e].name, 0);
        return;
    }
    if (browse_disk()) {
        dialog_done();
        insert_disk_file(n - 10, path, entries[e].name, 0);
        if (disk_size[n - 10] && disk_reset_after_insert) power_on();   /* 디스크: 삽입 후 리셋 설정 (기본 꺼짐) */
        return;
    }
    cart_insert_file(n, path, &entries[e]);
}

static void insert_selected(int e) { insert_selected_in(e); dz_req = -1; rom_nth = -1; zip_nth_req = 0; }

static int db_num(const char *p) { int v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0'); return v; }   /* 3.8.0: 롬 DB 종류 이름 뒤의 숫자 */

/* 롬 파일을 카트리지 슬롯 n에 (탐색기·최근 파일 공용, 2.12) */
static void cart_insert_file(int n, const char *path, const FeDirEntry *xe)
{
    if (!is_zip(path) && M->cart[n].present && !cart_special[n] && !strcmp(cart_path[n], path)
        && (!xe->user_type || cart_mapper[n] == (int)xe->type)) {   /* 2.14: 같은 롬이면 다시 꽂지도, 리셋하지도 않음 */
        menu_close_all();
        toast(T(S_SAME_CART), xe->name);
        return;
    }
    uint32_t size = 0;
    uint8_t *buf = cart_read_any(n, path, &size);       /* 3.2.0: zip도, 4.0.129: 2 MiB 넘는 롬 */
    if (!buf) {
        toast(T(S_LOAD_ERROR), xe->name);
        return;
    }
    const FeDirEntry *x = xe;
    MapperType mt = x->user_type ? x->type : cart_guess_mapper(buf, size);
    const MapperType guess_mt = mt;                     /* 서명·크기로 알아낸 큰 롬 종류는 DB(ASCII16 등)가 덮어쓰지 못하게 */
    int db_start = -1, db_sram = 0;                     /* 3.8.0: db_sram = ASCII8 SRAM 크기(KB) */
    int nextor = !x->user_type && is_nextor_rom(buf, size);   /* 3.7.1: Nextor 롬 → X86MSX 가상 디스크 카트리지 */
    if (nextor) {
        if (vhd_build_rom(buf, &size)) { toast(T(S_LOAD_ERROR), xe->name); return; }
        mt = MAPPER_VHD;
        for (int k = 0; k < MSX_CART_SLOTS; k++) if (k != n && M->cart[k].present && M->cart[k].mapper == MAPPER_VHD) cart_remove(k);
    }
    romdb_load();
    if (!nextor && (romdb_len || sysdb_len || bindb_len)) {
        char hex[41];
        sha1_hex(buf, size, hex);
        RomDbInfo si, ri;
        if (romdb_find(sysdb_buf, sysdb_len, hex, &si) && (!strcmp(si.type, "MB8877A") || !strcmp(si.type, "WD2793"))) {
            int kind = !strcmp(si.type, "MB8877A") ? 64 : 65;   /* 디스크 ROM → 디스크 인터페이스로 */
            cart_remove(n);
            if (!msx_insert_disk_interface(M, n, buf, size, kind)) {
                cart_special[n] = kind; cart_size[n] = size; cart_mapper[n] = -1;
                copy_str(cart_name[n], sizeof cart_name[n], x->name);
                copy_str(cart_path[n], sizeof cart_path[n], path);
                disks_reinsert();
                browse_remember();
                settings_changed();
                dialog_done();
                after_cart_insert(n);
                return;
            }
        }
        if (!x->user_type && romdb_find(romdb_buf, romdb_len, hex, &ri) && ri.type[0]) {
            const char *t = ri.type;
            if (!strcmp(t, "YAMANOOTO") || !strcmp(t, "Yamanooto")) mt = MAPPER_YAMANOOTO;   /* DB 해시로 Yamanooto 용 롬 인식 */
            else if (!strcmp(t, "KonamiSCC")) mt = MAPPER_KONAMI_SCC;
            else if (!strcmp(t, "Konami") || !strcmp(t, "GenericKonami")) mt = MAPPER_KONAMI;
            else if (!strncmp(t, "ASCII8SRAM", 10)) { mt = MAPPER_ASCII8_SRAM; db_sram = db_num(t + 10); }     /* 2.11: SRAM(배터리 백업) 게임 (3.8.0: 뒤 숫자 = SRAM KB) */
            else if (!strncmp(t, "ASCII16SRAM", 11)) mt = MAPPER_ASCII16_SRAM;
            else if (!strncmp(t, "ASCII8", 6)) mt = MAPPER_ASCII8;
            else if (!strncmp(t, "KoeiSRAM", 8)) { mt = MAPPER_ASCII8_SRAM; db_sram = db_num(t + 8); }          /* 3.0.9: 코에이 = ASCII8 + SRAM (신장의 야망) */
            else if (!strncmp(t, "ASCII16", 7)) mt = MAPPER_ASCII16;
            else if (!strcmp(t, "Normal")) { mt = MAPPER_PLAIN; db_start = ri.start; }
            else if (!strcmp(t, "MSXDOS2")) mt = MAPPER_DOS2;
            else if (!strcmp(t, "R-Type") || !strcmp(t, "RType")) mt = MAPPER_RTYPE;      /* 3.0.9 */
            else if (!strcmp(t, "HarryFox")) mt = MAPPER_HARRYFOX;
            else if (!strcmp(t, "GameMaster2")) mt = MAPPER_GAMEMASTER2;          /* 5.0.38 */
            else if (!strcmp(t, "Zemina80in1")) mt = MAPPER_ZEMINA80;           /* 3.8.0 */
            else if (!strcmp(t, "Zemina90in1")) mt = MAPPER_ZEMINA90;
            else if (!strcmp(t, "Zemina126in1")) mt = MAPPER_ZEMINA126;
            else if (!strcmp(t, "CrossBlaim")) mt = MAPPER_CROSSBLAIM;
            else if (!strcmp(t, "SuperLodeRunner")) mt = MAPPER_LODERUNNER;
            else if (!strcmp(t, "NettouYakyuu")) mt = MAPPER_NETTOU;            /* 3.0.10 */
            else if (!strcmp(t, "MatraInk")) mt = MAPPER_MATRAINK;                  /* 3.9.14: 플래시 512 KiB */
            else if (!strcmp(t, "SuperPierrot")) mt = MAPPER_ASCII16;              /* 판(MR6401)은 ASCII16 */
        } else if (!x->user_type) {                     /* 3.2.15: blueMSX DB에 없으면 openMSX DB(최신)에서 */
            int bt, bp;
            if (romdb_bin_find(bindb_buf, bindb_len, hex, &bt, &bp)) {
                static const MapperType bm[17] = { MAPPER_PLAIN, MAPPER_PLAIN, MAPPER_KONAMI, MAPPER_KONAMI_SCC, MAPPER_ASCII8, MAPPER_ASCII16,
                                                   MAPPER_ASCII8_SRAM, MAPPER_ASCII16_SRAM, MAPPER_RTYPE, MAPPER_HARRYFOX,
                                                   MAPPER_ZEMINA80, MAPPER_ZEMINA90, MAPPER_ZEMINA126, MAPPER_CROSSBLAIM, MAPPER_LODERUNNER, MAPPER_QURAN, MAPPER_YAMANOOTO };   /* 3.8.0: 종류 10~12 */
                if (bt >= 1 && bt <= 16) { mt = bm[bt]; if (bt == 1 && bp <= 3) db_start = bp * 0x4000; if (bt == 6) db_sram = bp; }   /* 3.8.0: 종류 6의 시작 페이지 칸 = SRAM KB */
            }
        }
    }
    if (!x->user_type && (guess_mt == MAPPER_ASCII16X || guess_mt == MAPPER_YAMANOOTO || guess_mt == MAPPER_NEO8 || guess_mt == MAPPER_NEO16)) mt = guess_mt;
    if (mt == MAPPER_MATRAINK && size > 512u * 1024) { toast(T(S_LOAD_ERROR), x->name); return; }   /* 3.9.14: 롬이 512 KiB 플래시보다 크면 안 됨 */
    {   /* 플래시 매퍼는 칩 크기까지 지운 상태(FFh)로 채움 — 크기는 칩 부품이 정함 (msx_cart_alloc_size).
         * 5.0.184 Go Figure: 파일 크기로 감싸서 맨 위 뱅크 기록이 뱅크 1 을 지우던 것. 2 MiB 넘으면 큰 버퍼(cart_big) */
        uint32_t need = msx_cart_alloc_size(mt, size);
        if (need > size && need > MSX_ROM_MAX && buf != cart_big) {
            if (cart_big && cart_big_size >= need && (cart_big_owner < 0 || cart_big_owner == n || !M->cart[cart_big_owner].present)) { memcpy(cart_big, buf, size); buf = cart_big; }
            else need = size;
        }
        if (buf == cart_big && need > cart_big_size) need = size;
        if (need > size) { memset(buf + size, 0xFF, need - size); size = need; }
    }
    cart_remove(n);
    if (msx_insert_cart(M, n, buf, size, mt)) { toast(T(S_LOAD_ERROR), x->name); return; }
    if (buf == cart_big) cart_big_owner = n;
    if (mt == MAPPER_PLAIN && db_start >= 0) msx_cart_set_base(M, n, db_start / 0x2000);   /* DB의 시작 주소 */
    if (mt == MAPPER_ASCII8_SRAM && db_sram) msx_cart_set_sram(M, n, db_sram);             /* 3.8.0: SRAM 2·8·32 KB */
    if (mt == MAPPER_VHD) vhd_mount_all(0);             /* 3.7.1 */
    copy_str(cart_name[n], sizeof cart_name[n], x->name);
    copy_str(cart_path[n], sizeof cart_path[n], path);
    cart_size[n] = size;
    cart_special[n] = 0;
    cart_mapper[n] = x->user_type ? (int)x->type : -1;
    sram_load(n);
    recent_add(0, path, x->name);                       /* 2.12: 최근 파일 */                                       /* 2.11: 배터리 백업 내용 */

    settings_changed();
    menu_close_all();
    after_cart_insert(n);
}
void fe_test_hd(const char *path) { hd_insert(path, path, 1); }
int fe_test_cart_special(int n) { return n >= 0 && n < MSX_CART_SLOTS ? cart_special[n] : -1; }   /* 3.9.4: 시험 틀 (카트리지 특수 번호) */   /* 3.7.1: 시험 틀 */
const char *fe_test_disk_path(int d) { return disk_path[d]; }   /* 다음 디스크 순서 시험 */
/* 시험 — 메뉴의 모든 끝 항목을 키 순서로 내보냄 (tests/menudlgtest.sh: 메뉴에서 연 창을 키로 움직인 뒤 화면이 '전부 다시 그린 것'과 같은지) */
static void (*menu_dump_out)(const char *);
static void menu_dump_rec(const Menu *m, const char *keys, int bar)
{
    int sel = 0;
    for (int i = 0; i < m->n; i++) {
        const MenuItem *it = &m->items[i];
        if (it->label < 0) continue;
        int st = item_state(it);
        if (st != 1 && st != 2) continue;
        char k[512]; copy_str(k, sizeof k, keys);
        for (int d = 0; d < sel; d++) str_append(k, sizeof k, ",down");
        str_append(k, sizeof k, ",enter");
        if (it->sub) menu_dump_rec(it->sub, k, bar);
        else {
            char line[600]; line[0] = 0;
            num_append(line, sizeof line, (uint32_t)bar, 0, ' '); str_append(line, sizeof line, "\t"); str_append(line, sizeof line, k);
            str_append(line, sizeof line, "\t"); num_append(line, sizeof line, (uint32_t)it->action, 0, ' ');
            menu_dump_out(line);
        }
        sel++;
    }
}
int fe_test_dialog(void) { return dialog; }
void fe_test_menu_dump(void (*out)(const char *))
{
    menu_dump_out = out;
    for (int b = 0; b < bar_n; b++) {
        char k[64]; copy_str(k, sizeof k, "f10");
        for (int r = 0; r < b; r++) str_append(k, sizeof k, ",right");
        str_append(k, sizeof k, ",down");
        menu_dump_rec(bar_menus[b], k, b);
    }
}
void fe_test_action(const char *n) { if (!strcmp(n, "mixall")) { mix_test_all = 1; do_action(A_PROPS_MIX); return; } if (!strcmp(n, "net")) { do_action(A_NET); return; } if (!strncmp(n, "dzip:", 5)) { insert_disk_file(0, n + 5, n + 5, 0); return; } if (!strcmp(n, "disknext")) { disk_next(0); return; } if (!strncmp(n, "tape:", 5)) { uint32_t ts = 0; char tn[64] = ""; if (!tape_load_file(n + 5, &ts, tn, sizeof tn, 1)) { copy_str(tape_path, sizeof tape_path, n + 5); copy_str(tape_name, sizeof tape_name, n + 5); msx_tape_insert(M, tape_buf, ts, TAPE_MAX); } return; } if (!strncmp(n, "mach:", 5)) { char e[96]; switch_machine(n + 5, e, sizeof e); return; } if (!strcmp(n, "sced")) { do_action(A_SC_EDIT); return; } if (!strcmp(n, "soft")) { do_action(A_SOFT_RESET); return; } if (!strcmp(n, "hard")) { do_action(A_HARD_RESET); return; } if (!strcmp(n, "prnview")) do_action(A_PRN_VIEW); if (!strcmp(n, "vhd")) do_action(A_VHD_C); if (!strcmp(n, "diskcopy")) do_action(A_DISK_COPY); else if (!strcmp(n, "redraw")) { full_redraw(); } else if (!strcmp(n, "mixer")) do_action(A_PROPS_MIX); else if (!strcmp(n, "midimix")) do_action(A_MIDI_MIX); else if (!strcmp(n, "media")) do_action(A_MEDIA); else if (!strcmp(n, "exit")) do_action(A_EXIT); else if (!strcmp(n, "reboot")) do_action(A_REBOOT); else if (!strcmp(n, "keymap")) do_action(A_KEYMAP_EDIT); else if (!strcmp(n, "about")) do_action(A_ABOUT); else if (!strcmp(n, "hdeject")) do_action(A_HD_EJECT); else if (!strcmp(n, "rombrowse")) open_browser(0); else if (!strcmp(n, "tapebrowse")) open_browser(12); else if (!strcmp(n, "diskbrowse")) open_browser(10); else if (!strcmp(n, "hdbrowse")) open_browser(13); else if (!strcmp(n, "basbrowse")) open_browser(14); else if (!strcmp(n, "diagjoy")) open_diag(4); else if (!strcmp(n, "diag")) open_diag(0); else if (!strcmp(n, "diagsum")) open_diag(9); else if (!strcmp(n, "diagcolor")) open_diag(8); else if (!strcmp(n, "image")) do_action(A_IMAGE_MAKE); else if (!strcmp(n, "lang")) open_langwin(); else if (!strcmp(n, "langopen")) langwin_test_open(); else if (!strncmp(n, "props:", 6)) open_props(n[6] - '0');   /* 속성 창 탭 번호로 열기 (선택형 설정 시험) */ else if (!strcmp(n, "mouseprops")) open_props(9); else if (!strcmp(n, "kbprops")) open_props(7); else if (!strcmp(n, "joyprops")) open_props(8); else if (!strcmp(n, "emuprops")) open_props(0); else if (!strcmp(n, "vidprops")) open_props(1); else if (!strcmp(n, "sndprops")) open_props(3); else if (!strcmp(n, "gfx9000")) special_apply(0, special_kind(158), 1); else if (!strncmp(n, "v9show:", 7)) { if (M->v9) { M->v9->shown = 1; M->v9->regs[15] = 1; M->v9->pal15[1] = (uint16_t)(31 << 10); } vsrc_pref = n[7] - '0' - 1; }   /* 시험 — V9990 이 초록 화면을 내보내게 하고 화면 소스를 고름 */ else if (!strncmp(n, "vsrc:", 5)) { vsrc_pref = n[5] - '0' - 1; } else if (!strncmp(n, "aspect:", 7)) { aspect_mode = n[7] - '0'; layout(); full_redraw(); }   /* 5.2.42 */ else if (!strncmp(n, "special:", 8)) { int k = 0; for (const char *q = n + 8; *q >= '0' && *q <= '9'; q++) k = k * 10 + (*q - '0'); special_apply(0, k, 1); }   /* 5.2.43 */ }   /* 3.7.0: 시험 틀 (5.2.19 vidprops·gfx9000·vsrc:0~2) */
void fe_insert_special(int n, int kind) { if (n >= 0 && n < MSX_CART_SLOTS) do_action(SPX(n, kind)); }   /* 3.6.7: 시험 틀용 */
void fe_insert_rom(int n, const char *path)           /* 3.6.1: 시험 틀용 */
{
    static FeDirEntry e;
    memset(&e, 0, sizeof e);
    const char *b = path; for (const char *q = path; *q; q++) if (*q == '\\') b = q + 1;
    copy_str(e.name, sizeof e.name, b);
    if (n >= 0 && n < MSX_CART_SLOTS) cart_insert_file(n, path, &e);
}

static const char sc_ascii[0x40] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0, 0, 0, 0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0, 0, 0, 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', 0, 0, 0, 0, 0, 'z', 'x', 'c', 'v',
    'b', 'n', 'm', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* 2.16 화면 가득한 창의 자리 (fe_ui.inc ui_full_card와 같은 계산) */
static UiRect dlg_outer(void) { return card_rect(); }
static uint32_t dlg_body_y(void) { return dlg_outer().y + ui_head_h() + 2 * S; }
static uint32_t dlg_cmd_y(void) { UiRect o = dlg_outer(); return o.y + o.h - ui_cmdbar_h() - 2 * S; }
static uint32_t list_rh(void) { return LH + 8 * S; }
/* 파일·기종 목록: 위 줄(경로 / 세대 탭) + 열 제목 아래부터, 아래 파일 이름 줄 위까지 */
static uint32_t list_y0(void) { return dlg_body_y() + 10 * S + (LH + 8 * S) + 10 * S + (SLH + 12 * S); }
static int list_rows(void) { uint32_t b = dlg_cmd_y() - LH - 22 * S, y = list_y0(); int r = b > y ? (int)((b - y) / list_rh()) : 1; return r < 1 ? 1 : r; }

/* 3.6.4: 지우기. 넣어 둔 매체(또는 그것이 든 폴더)는 안 지움. 폴더는 안의 것부터 (깊이 8까지) */
static int path_in_use(const char *p)
{
    size_t n = strlen(p);
    const char *use[8] = { disk_size[0] ? disk_path[0] : "", disk_size[1] ? disk_path[1] : "", M->tape ? tape_path : "", hd_secs ? hd_path : "", hd2_secs ? hd2_path : "", "", "", "" };
    for (int i = 0; i < 5; i++) if (use[i][0] && !strncmp(use[i], p, n) && (use[i][n] == 0 || use[i][n] == '\\')) return 1;
    for (int c = 0; c < MSX_CART_SLOTS; c++) if (M->cart[c].present && cart_path[c][0] && !strncmp(cart_path[c], p, n) && (cart_path[c][n] == 0 || cart_path[c][n] == '\\')) return 1;
    return 0;
}
static FeDirEntry del_ent[16];
static int del_tree(const char *p, int depth)
{
    if (depth > 8) return -1;
    for (int guard = 0; guard < 4096; guard++) {        /* 안의 항목을 하나씩 지우고 다시 읽음 (버퍼 하나로) */
        int n = P.list_dir_all ? P.list_dir_all(p, del_ent, 16) : P.list_dir(p, del_ent, 16);
        if (n <= 0) break;
        char cp[FE_PATH_MAX];
        copy_str(cp, sizeof cp, p); str_append(cp, sizeof cp, "\\"); str_append(cp, sizeof cp, del_ent[0].key[0] ? del_ent[0].key : del_ent[0].name);
        int r = del_ent[0].is_dir ? del_tree(cp, depth + 1) : P.delete_file(cp);
        if (r) return -1;
    }
    return P.delete_file(p);
}
static void del_do(void)
{
    del_ask = 0;
    if (!P.delete_file) { toast(T(S_DC_DEL_FAIL), del_name); return; }
    if (path_in_use(del_path)) { toast(T(S_DEL_INUSE), del_name); return; }
    int r = del_isdir ? del_tree(del_path, 0) : P.delete_file(del_path);
    toast(T(r ? S_DC_DEL_FAIL : S_DC_DELETED), del_name);
    int keep = menu_sel;
    load_dir(0);
    menu_sel = keep < nlist ? keep : nlist > 0 ? nlist - 1 : 0;
}

static void browser_key(int key)
{
    if (browse_vhd() && vb_key(key)) return;            /* 3.7.2 가상 하드 창 */
    if (dd_on) { dd_key_event(key); menu_dirty = 1; return; }   /* ROMtype 풀다운 */
    if (del_ask) { if (key == 0x1C || key == 0x9C) del_do(); else if (key == 0x01 || key == 0x0E) del_ask = 0; menu_dirty = 1; return; }   /* 3.6.4 */
    if (zin && (key == 0xD2 || key == 0xD3 || key == 0x3D)) return;   /* 4.0.34: zip 안에서는 지우기·새 폴더·종류 바꾸기·편집 없음 */
    if (key == 0xD3 && browse_slot < MSX_CART_SLOTS) return;   /* 3.7.0: 카트리지 슬롯 고르기에서는 지우지 않음 */
    if (key == 0xD3 && !dir_naming && cur_path[0] && nlist && list[menu_sel] >= 0) {   /* 3.6.4: Del = 파일·폴더 지우기 (묻고) */
        int e = list[menu_sel];
        entry_path(e, del_path, sizeof del_path);
        copy_str(del_name, sizeof del_name, entries[e].key[0] ? entries[e].key : entries[e].name);
        del_isdir = entries[e].is_dir;
        del_ask = 1; menu_dirty = 1;
        return;
    }
    int rows = list_rows();
    menu_dirty = 1;
    if (browse_bas() && key == 0x0F) { bas_set_mode(!bas_mode_save); return; }   /* 3.2.9: Tab = 넣기 ↔ 저장 */
    if (browse_bsave()) {                               /* 3.2.7 저장 창: 글자는 이름 칸으로 */
        int sel0 = menu_sel;
        switch (key) {
        case 0xC8: case 0xD0: case 0xC9: case 0xD1: case 0xC7: case 0xCF: break;   /* 아래 보통 처리 */
        case 0x0E: { int l = (int)strlen(bas_fname); if (l) bas_fname[l - 1] = 0; bas_typed = 1; bas_over = 0; return; }
        case 0x1C: case 0x9C: {
            int it = nlist ? list[menu_sel] : ITEM_UP;
            if (!bas_typed && nlist && (it == ITEM_UP || entries[it].is_dir)) { if (it == ITEM_UP) go_up(); else enter_dir(it); return; }
            bas_save_to();
            return;
        }
        case 0x01: dialog_back(); return;
        case 0xCB: case 0xCD: return;
        default: {
            char c = key_char(key, (mods & 0x03) != 0);
            int l = (int)strlen(bas_fname);
            if (c && c != '\\' && c != '/' && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|' && l < 48) {
                bas_fname[l] = c; bas_fname[l + 1] = 0; bas_typed = 1; bas_over = 0;
            }
            return;
        }
        }
        (void)sel0;
    }
    if (dir_naming) {                                   /* 4.0.29: 새 폴더 이름 (모든 파일 창에서 Ins) */
        int l = (int)strlen(dir_newname);
        if (key == 0x01) { dir_naming = 0; browse_full = 1; full_redraw(); return; }
        if (key == 0x0E) { if (l) dir_newname[l - 1] = 0; return; }
        if (key == 0x1C || key == 0x9C) {
            if (!l) return;
            char np[PATH_MAX_FE];
            copy_str(np, sizeof np, cur_path); str_append(np, sizeof np, "\\"); str_append(np, sizeof np, dir_newname);
            if (!P.create || P.create(np, 1)) { toast(T(S_DIR_MKFAIL), dir_newname); return; }
            dir_naming = 0;
            toast(T(S_DIR_MADE), dir_newname);
            load_dir(dir_newname);                      /* 만든 폴더를 고른 채로 */
            full_redraw();
            return;
        }
        char c = key_char(key, (mods & 0x03) != 0);
        if (c && c != '\\' && c != '/' && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|' && l < 40) { dir_newname[l] = c; dir_newname[l + 1] = 0; }
        return;
    }
    if (key == 0x3F) {                                  /* F5 = 고른 ROMtype을 자동 판별 값으로 되돌림 */
        if (browse_slot < MSX_CART_SLOTS && nlist && list[menu_sel] >= 0 && !entries[list[menu_sel]].is_dir) { FeDirEntry *e = &entries[list[menu_sel]]; probe_type(list[menu_sel]); e->type = e->guess; e->user_type = 0; }
        return;
    }
    if (key == 0x3E) { if (browse_slot < MSX_CART_SLOTS && nlist && list[menu_sel] >= 0 && !entries[list[menu_sel]].is_dir) rt_dd_open(); return; }   /* F4 = ROMtype 풀다운 (텍스트 편집은 없앰) */
    if (key == 0xD2 && cur_path[0] && !browse_vhd() && !browse_bsave()) { dir_naming = 1; copy_str(dir_newname, sizeof dir_newname, browse_dir() ? "NEWDISK" : "NEWFOLDER"); return; }   /* 4.0.29: Ins = 새 폴더 */
    if (browse_dir()) {                                 /* 3.6.2 디렉토리 삽입 (blueMSX 폴더 찾아보기처럼). 3.7.2: I = 고른 폴더 넣기, Enter·→ = 열기 */
        int it = nlist ? list[menu_sel] : ITEM_UP;
        if (key == 0x1C || key == 0x9C) {               /* 3.7.2: Enter = 폴더 열기 */
            if (!nlist) return;
            if (it == ITEM_UP) { go_up(); return; }
            if (entries[it].is_dir) enter_dir(it);
            return;
        }
        if (key == 0x17 && browse_imgdir()) {           /* 3.8.1: I = 고른 폴더(.. 이면 지금 폴더)를 이미지 저장 폴더로 */
            if (!cur_path[0] && (!nlist || it == ITEM_UP || !entries[it].is_dir)) return;
            if (nlist && it != ITEM_UP && entries[it].is_dir) {
                char dp[PATH_MAX_FE];
                entry_path(it, dp, sizeof dp);
                copy_str(cur_path, sizeof cur_path, dp);
                if (cur_disp[0]) str_append(cur_disp, sizeof cur_disp, " > ");
                str_append(cur_disp, sizeof cur_disp, entries[it].key[0] ? entries[it].key : entries[it].name);
            }
            browse_remember();
            open_dialog(11);
            return;
        }
        if (key == 0x17) {                              /* 3.7.2: I = 고른 폴더를 디렉토리 삽입 */
            if (!nlist || it == ITEM_UP || !entries[it].is_dir || !cur_path[0]) return;
            char dp[PATH_MAX_FE], dd[PATH_MAX_FE];
            entry_path(it, dp, sizeof dp);
            copy_str(dd, sizeof dd, entries[it].key[0] ? entries[it].key : entries[it].name);
            int d = browse_slot - 18;
            browse_remember();
            dialog_done();
            dir_as_disk(d, dp, dd);
            if (disk_size[d] && disk_reset_after_insert) power_on();
            return;
        }
        if (key == 0xCD) { if (nlist && it >= 0 && entries[it].is_dir) enter_dir(it); return; }   /* → 폴더 열기 */
        if (key == 0xCB) { go_up(); return; }                                                    /* ← 위 폴더 */
    }
    switch (key) {
    case 0xC8: if (menu_sel > 0) menu_sel--; else brw_move(-1); break;
    case 0xD0: if (menu_sel < nlist - 1) menu_sel++; else brw_move(1); break;
    case 0xC9: if (menu_sel == 0 && brw_move(-1)) break; menu_sel -= rows; if (menu_sel < 0) menu_sel = 0; break;
    case 0xD1: if (menu_sel >= nlist - 1 && brw_move(1)) break; menu_sel += rows; if (menu_sel > nlist - 1) menu_sel = nlist - 1; if (menu_sel < 0) menu_sel = 0; break;
    case 0xC7: menu_sel = 0; break;
    case 0xCF: menu_sel = nlist ? nlist - 1 : 0; break;
    case 0x0E: go_up(); break;
    case 0xCB: case 0xCD: {
        if (!nlist) break;
        if (!(mods & 0x04)) {                           /* ←/→: 한 페이지씩 */
            int rows2 = list_rows();
            menu_sel += key == 0xCD ? rows2 : -rows2;
            if (menu_sel < 0) menu_sel = 0;
            if (menu_sel > nlist - 1) menu_sel = nlist - 1;
            break;
        }
        int it = list[menu_sel];                        /* LCtrl+←/→: 매퍼 바꾸기 */
        if (it >= 0 && !entries[it].is_dir) {
            FeDirEntry *e = &entries[it];
            probe_type(it);
            e->type = (MapperType)((e->type + (key == 0xCD ? 1 : MAPPER_COUNT - 1)) % MAPPER_COUNT);
            e->user_type = 1;
        }
        break;
    }
    case 0x1C: case 0x9C: {
        if (!nlist) break;
        int it = list[menu_sel];
        if (it == ITEM_UP) go_up();
        else if (entries[it].is_dir) enter_dir(it);
        else if (!zin && !browse_hd() && is_zip(entries[it].key[0] ? entries[it].key : entries[it].name) && zin_enter(it, (mods & 0x10) != 0)) {}   /* 4.0.34: zip 안에 고를 파일이 둘 이상이면 안으로 들어가 고름 (5.2.72: 하나면 zip 그대로) */
        else insert_selected(it);
        break;
    }
    case 0x01: dialog_back(); break;
    default:
        if (key < 0x40 && sc_ascii[key]) {
            for (int k = 1; k <= nlist; k++) {
                int i = (menu_sel + k) % nlist;
                if (list[i] >= 0 && lower_c(entries[list[i]].name[0]) == sc_ascii[key]) { menu_sel = i; break; }
            }
        }
        break;
    }
    if (browse_bsave()) {                               /* 3.2.7: 옮기면 그 파일 이름을 이름 칸으로 (폴더면 이름 그대로, Enter = 폴더 열기) */
        bas_typed = 0; bas_over = 0;
        if (menu_sel < nlist && list[menu_sel] >= 0 && !entries[list[menu_sel]].is_dir)
            copy_str(bas_fname, sizeof bas_fname, entries[list[menu_sel]].key[0] ? entries[list[menu_sel]].key : entries[list[menu_sel]].name);
    }
}

static void draw_icon(uint32_t x, uint32_t y, int dir, uint8_t bg)
{   /* 3.0.1: Icons\FOLDER.png / FILE.png (없으면 본체 그림). 부르는 쪽은 16점 그림 기준으로 y + LH/8을 넘김 */
    uint32_t sz = LH;
    y = y > LH / 8 ? y - LH / 8 : 0;
    if (dir) draw_icon_hd(x, y, sz, ICON_FOLDER, C_WARN, 11, bg);
    else draw_icon_hd(x, y, sz, ICON_FILE, 14, C_ACC, bg);
}

static void draw_browser(void)
{
    UiRect o = dlg_outer();
    uint32_t rh = list_rh();
    /* 제목 */
    char title[96] = "", chip[24] = "";
    if (browse_tape()) str_append(title, sizeof title, T(S_SELECT_TAPE));
    else if (browse_bas()) str_append(title, sizeof title, T(S_SELECT_BAS));
    else if (browse_hd()) str_append(title, sizeof title, T(S_SELECT_HD));
    else if (browse_disk()) { str_append(title, sizeof title, T(S_SELECT_DISK)); str_append(title, sizeof title, browse_slot == 11 ? " B" : " A"); }
    else if (browse_imgdir()) str_append(title, sizeof title, T(S_SELECT_IMGDIR));
    else if (browse_dir()) { str_append(title, sizeof title, T(S_SELECT_DIR)); str_append(title, sizeof title, browse_slot == 19 ? " B" : " A"); }
    else if (browse_vhd()) { char dl[5] = { ' ', (char)('C' + browse_slot - 20), ':', 0, 0 }; str_append(title, sizeof title, T(S_VHD_TITLE)); str_append(title, sizeof title, dl); }
    else { char d[4] = { ' ', (char)('1' + browse_slot), 0, 0 }; str_append(title, sizeof title, T(S_SELECT_ROM)); str_append(title, sizeof title, d); }
    num_append(chip, sizeof chip, (uint32_t)dir_total(), 0, ' '); if (cx_big && cx_cut) str_append(chip, sizeof chip, "+");
    str_append(chip, sizeof chip, lang == 0 ? "개 항목" : lang == 2 ? " 項目" : " items");
    const char *filter = browse_vhd() ? T(S_VHD_SUB) : browse_imgdir() ? T(S_IMGDIR_SUB) : browse_dir() ? T(S_DIR_FILTER) : browse_bas() ? T(S_BAS_FILTER) : browse_tape() ? T(S_TAPE_FILTER) : browse_hd() ? T(S_HD_FILTER) : browse_disk() ? T(S_DISK_FILTER) : T(S_ROM_FILTER);
    int card_full = browse_full || force_redraw;            /* 3.9.5: 카드를 새로 그렸는데 두 줄만 그려 폴더가 사라져 보이던 것 고침 */
    if (card_full) { ui_full_card(title, filter, chip, 0); browse_full = 0; }
    UiRect b = { o.x + S, dlg_body_y(), o.w - 2 * S, 0 };
    uint32_t pad = 14 * S, X = b.x + pad, W = b.w - 2 * pad;
    /* 경로 (빵 부스러기) */
    char path[PATH_MAX_FE + 32] = "";
    str_append(path, sizeof path, T(S_DRIVES));
    if (cur_disp[0]) { str_append(path, sizeof path, "  ›  "); str_append(path, sizeof path, cur_disp); }
    uint32_t py = b.y + 10 * S, ph = LH + 8 * S;
    fe_fill(X - 2 * S, py - 2 * S, W + 4 * S, ph + 4 * S, C_CARD);
    uint32_t px0 = X;
    if (browse_bas()) {                                 /* 3.2.9: 넣기 / 저장 탭 (Tab 또는 누르기) */
        for (int m = 0; m < 2; m++) {
            const char *lab = T(m ? S_BAS_TAB_SAVE : S_BAS_TAB_LOAD);
            uint32_t tw = text_w(lab) + 28 * S, on = m == bas_mode_save;
            ui_rrect(px0, py, tw, ph, 5 * S, on ? C_ACC : C_FIELD);
            text_f(F, S, px0 + 14 * S, ui_ty(py, ph), lab, on ? 15 : C_MUTED, 0xFF, 0, 1);
            hit_add(hc_bas_mode, 0, m, 0, 0, px0, py, tw, ph);
            px0 += tw + 8 * S;
        }
    }
    ui_rrect(px0, py, X + W - px0, ph, 5 * S, C_FIELD);
    text_clip(px0 + 10 * S, ui_ty(py, ph), path, C_TEXT, 0xFF, X + W - px0 - 20 * S);
    /* 열 위치 (오른쪽부터). 이름 칸이 좁으면 매퍼 → 형식 → 날짜 순으로 뺌 */
#define MAXW(a, b) ((a) > (b) ? (a) : (b))
    uint32_t w_date = MAXW(text_w("0000/00/00 00:00"), ui_sw(T(S_COL_DATE))) + 16 * S;
    uint32_t w_size = MAXW(text_w("0000.00 kB"), ui_sw(T(S_COL_SIZE))) + 20 * S;
    uint32_t w_map = MAXW(text_w("1DD 360K*")>text_w("KonamiSCC*")?text_w("1DD 360K*"):text_w("KonamiSCC*"), ui_sw(T(S_COL_MAPPER))) + 16 * S;
    uint32_t w_type = MAXW(text_w(".rom"), ui_sw(T(S_COL_TYPE))) + 16 * S;
    int show_map = !(browse_hd() || browse_vhd()), show_type = 1, show_date = 1;
    uint32_t min_name = W * 35 / 100;
    if (W < w_date + w_size + w_map + w_type + min_name) show_map = 0;
    if (W < w_date + w_size + (show_map ? w_map : 0) + w_type + min_name) show_type = 0;
    if (W < w_date + w_size + min_name) show_date = 0;
    uint32_t c_date = X + W - 10 * S - (show_date ? w_date : 0);
    uint32_t c_size = c_date - w_size;
    uint32_t c_map = c_size - (show_map ? w_map : 0);
    uint32_t c_type = c_map - (show_type ? w_type : 0);
    uint32_t ly = list_y0(), hy = ly - SLH - 12 * S, ix = X + 10 * S + LH;
    fe_fill(X, hy, W, SLH + 10 * S, C_CARD);
    text_f(SF, 1, ix + 6 * S, hy + 4 * S, T(S_COL_NAME), C_MUTED, 0xFF, 0, 1);
    if (!browse_vhd()) {                                /* 3.7.2: 가상 하드 창은 열 이름 없음 */
        if (show_type) text_f(SF, 1, c_type, hy + 4 * S, T(S_COL_TYPE), C_MUTED, 0xFF, 0, 1);
        if (show_map) text_f(SF, 1, c_map, hy + 4 * S, (browse_disk() || browse_tape()) ? (lang == 0 ? "구분" : lang == 2 ? "種別" : "Kind") : T(S_COL_MAPPER), C_MUTED, 0xFF, 0, 1);
        text_f(SF, 1, c_size + w_size - 20 * S - ui_sw(T(S_COL_SIZE)), hy + 4 * S, T(S_COL_SIZE), C_MUTED, 0xFF, 0, 1);
        if (show_date) text_f(SF, 1, c_date, hy + 4 * S, T(S_COL_DATE), C_MUTED, 0xFF, 0, 1);
    }
    /* 목록 */
    int rows = list_rows();
    if (menu_sel < menu_top) menu_top = menu_sel;
    if (menu_sel >= menu_top + rows) menu_top = menu_sel - rows + 1;
    for (int r = (nlist - menu_top < rows ? nlist - menu_top : rows); r < rows; r++)
        if (r >= 0) fe_fill(X, ly + (uint32_t)r * rh, W, rh, C_CARD);
    /* 화면이 스크롤되지 않았으면 커서가 떠난 줄과 새로 온 줄만 다시 그림 (빠르고 부드럽게) */
    static int prev_top = -1, prev_sel = -1, prev_n = -1;
    int only2 = !card_full && !force_redraw && !probe_redraw && prev_top == menu_top && prev_n == nlist && prev_sel >= 0;
    probe_redraw = 0; probe_next = -1;
    int was_sel = prev_sel;
    prev_top = menu_top; prev_sel = menu_sel; prev_n = nlist;
    for (int r = 0; r < rows && menu_top + r < nlist; r++) {
        int idx = menu_top + r, it = list[idx];
        uint32_t y = ly + (uint32_t)r * rh;
        hit_add(hc_sel, (intptr_t)&menu_sel, idx, 0, 0, X, y, W - 10 * S, rh);   /* 3.0 마우스: 고른 줄을 한 번 더 누르면 열기 */
        if (it >= 0 && !browse_vhd() && (browse_slot < MSX_CART_SLOTS || browse_disk() || browse_tape()) && !entries[it].is_dir && !entries[it].type_known) {
            if (probe_next < 0 || idx == menu_sel) probe_next = it;   /* 종류 알아내기는 키를 쉬는 동안 한 줄씩 (probe_tick) */
        }
        if (only2 && idx != menu_sel && idx != was_sel) continue;
        int sel = idx == menu_sel;
        fe_fill(X, y, W, rh, C_CARD);
        ui_list_row_bg(X, y, W - 10 * S, rh, sel, idx & 1, 1);
        int dir = it < 0 || entries[it].is_dir;
        uint8_t fg = sel ? 15 : C_TEXT;
        int ftk = 0, fic = ICON_FILE; uint8_t fcol = C_TEXT;
        int isf = !dir && !browse_vhd();
        if (isf) { ftk = file_type_sz(entries[it].key[0] ? entries[it].key : entries[it].name, entries[it].size); file_look(ftk, &fic, &fcol); }
        if (isf) draw_icon_hd(X + 10 * S, y + (rh - LH) / 2, LH, fic, fcol, 15, sel ? C_ACC2 : (idx & 1) ? C_HEAD : C_CARD);
        else draw_icon(X + 10 * S, y + (rh - LH) / 2 + LH / 8, dir, sel ? C_ACC2 : (idx & 1) ? C_HEAD : C_CARD);
        text_ell(ix + 6 * S, ui_ty(y, rh), it == ITEM_UP ? ".." : it == ITEM_DIRDISK ? T(S_DIR_NEWDIR) : cur_path[0] && entries[it].key[0] ? entries[it].key : entries[it].name, dir && !sel ? C_WARN : ftk && !sel ? fcol : fg, 0xFF, c_type - ix - 14 * S);
        if (browse_vhd()) { vb_row_info(it, c_type, c_type, c_date + (show_date ? w_date : 0) - 4 * S, y, rh, sel); continue; }   /* 3.7.2 */
        if (it < 0) continue;
        FeDirEntry *x = &entries[it];
        uint8_t mc = sel ? 15 : C_MUTED;
        if (!x->is_dir) {
            const char *dot = 0;
            for (const char *p = x->key[0] ? x->key : x->name; *p; p++) if (*p == '.') dot = p;
            char ext[8] = "";
            if (dot) for (int i = 0; dot[i] && i < 5; i++) ext[i] = (char)lower_c(dot[i]);
            if (show_type) text_f(F, S, c_type, ui_ty(y, rh), ext, mc, 0xFF, 0, 1);
            char mp[24] = "";
            if (!x->type_known && (browse_slot < MSX_CART_SLOTS || browse_disk() || browse_tape())) str_append(mp, sizeof mp, "...");
            else if (browse_disk() || browse_tape()) {
                static const char *const dk[] = { "DSK", "1DD 360K", "2DD 720K", "1.44M", "320K", "180K", "160K", "DSK" };
                static const char *const tk[] = { "-", "BASIC", "ASCII", "BIN", "CAS" };
                int c = (int)x->type;
                if (browse_disk() && x->zdisk == 2) { /* 디스크 파일이 아니면 구분 칸은 비움 */ }
                else str_append(mp, sizeof mp, browse_disk() ? dk[c & 7] : tk[c % 5]);
            }
            else if (browse_slot >= MSX_CART_SLOTS) str_append(mp, sizeof mp, "-");
            else if (x->zdisk == 2) { for (const char *q = x->zx; *q; q++) { char u[2] = { (char)(*q >= 'a' && *q <= 'z' ? *q - 32 : *q), 0 }; str_append(mp, sizeof mp, u); } if (!x->zx[0]) str_append(mp, sizeof mp, "-"); }
            else if (x->zdisk) str_append(mp, sizeof mp, "Disk");   /* 5.2.133 */
            else str_append(mp, sizeof mp, x->type == MAPPER_PLAIN ? "ROM" : cart_mapper_name(x->type));
            if (x->user_type && x->type != x->guess) str_append(mp, sizeof mp, "*");
            if (show_map) text_ell(c_map, ui_ty(y, rh), mp, sel ? 15 : C_ACC, 0xFF, w_map - 6 * S);   /* 길면 ... */
            char sz[24] = "";
            num_append(sz, sizeof sz, x->size / 1024, 0, ' ');
            str_append(sz, sizeof sz, ".");
            num_append(sz, sizeof sz, (x->size % 1024) * 100 / 1024, 2, '0');
            str_append(sz, sizeof sz, " kB");
            text_f(F, S, c_size + w_size - 20 * S - text_w(sz), ui_ty(y, rh), sz, fg, 0xFF, 0, 1);
        }
        if (x->year && show_date) {
            char dt[24] = "";
            num_append(dt, sizeof dt, x->year, 4, '0'); str_append(dt, sizeof dt, "/");
            num_append(dt, sizeof dt, x->mon, 2, '0'); str_append(dt, sizeof dt, "/");
            num_append(dt, sizeof dt, x->day, 2, '0'); str_append(dt, sizeof dt, " ");
            num_append(dt, sizeof dt, x->hour, 2, '0'); str_append(dt, sizeof dt, ":");
            num_append(dt, sizeof dt, x->min, 2, '0');
            text_f(F, S, c_date, ui_ty(y, rh), dt, mc, 0xFF, 0, 1);
        }
    }
    fe_fill(X + W - 6 * S, ly, 6 * S, (uint32_t)rows * rh, C_CARD);
    ui_sbar(X + W - 5 * S, ly, (uint32_t)rows * rh, &menu_top, &menu_sel, rows, nlist);
    if (list_error) text_clip(ix, ly + rh, T(S_CANNOT_READ), C_WARN, 0xFF, W - LH);
    else if (nlist <= (cur_path[0] ? 1 : 0)) text_clip(ix, ly + (cur_path[0] ? rh : 0), T(zin ? S_EMPTY_ZIP : browse_bas() ? S_EMPTY_BAS : browse_tape() ? S_EMPTY_TAPE : browse_hd() ? S_EMPTY_HD : browse_disk() ? S_EMPTY_DISK : S_EMPTY_FOLDER), C_MUTED, 0xFF, W - LH);
    /* 아래: 고른 파일 + 명령 줄 */
    uint32_t cy = dlg_cmd_y(), fy = cy - LH - 16 * S, fh = LH + 8 * S;
    fe_fill(X, fy - 2 * S, W, fh + 4 * S, C_CARD);
    uint32_t lw = text_f(SF, 1, X + 4 * S, ui_sty(fy, fh), T(browse_dir() || browse_vhd() ? S_DIR_NAME : S_FILE_NAME), C_MUTED, 0xFF, 0, 1);
    const char *sn = (menu_sel < nlist && list[menu_sel] >= 0 && !entries[list[menu_sel]].is_dir) ? (entries[list[menu_sel]].key[0] ? entries[list[menu_sel]].key : entries[list[menu_sel]].name) : "";   /* 3.4.7: 긴 이름 그대로 */
    char snb[72];
    if (browse_bsave()) { copy_str(snb, sizeof snb, bas_fname); str_append(snb, sizeof snb, "_"); sn = snb; }   /* 3.2.7: 저장 창 = 이름 칸에 글을 침 */
    if (browse_dir() || browse_vhd()) {                 /* 3.6.2: 고른 폴더 이름, 새 폴더 이름을 칠 때는 그 글 */
        sn = (menu_sel < nlist && list[menu_sel] >= 0 && entries[list[menu_sel]].is_dir && cur_path[0]) ? (entries[list[menu_sel]].key[0] ? entries[list[menu_sel]].key : entries[list[menu_sel]].name) : "";
    }
    char cb[48] = ""; uint32_t cnt_w = 0;
    if (cur_path[0]) {                                  /* 4.0.122: 항목 개수 (파일+폴더) — 이름 칸 오른쪽 끝 */
        num_append(cb, sizeof cb, (uint32_t)dir_total(), 0, ' ');
        if (cx_big && cx_cut) str_append(cb, sizeof cb, "+");
        str_append(cb, sizeof cb, lang == 0 ? "개 항목" : lang == 2 ? " 項目" : " items");
        cnt_w = text_f(SF, 1, 0, 0, cb, 0, 0, 0, 0);
    }
    ui_rrect(X + lw + 14 * S, fy, W - lw - 14 * S, fh, 5 * S, C_FIELD);
    if (browse_bsave()) ui_rrect(X + lw + 14 * S, fy, 3 * S, fh, 1 * S, C_ACC);
    text_ell(X + lw + 24 * S, ui_ty(fy, fh), sn, C_TEXT, 0xFF, W - lw - 34 * S - (cur_path[0] ? cnt_w + 24 * S : 0));
    if (!cur_path[0] && P.browse_note) small_ell(X + 4 * S, fy - SLH - 6 * S, P.browse_note, C_WARN, C_CARD, W);
    if (cur_path[0]) text_f(SF, 1, X + W - cnt_w - 12 * S, ui_sty(fy, fh), cb, C_MUTED, C_FIELD, 0, 1);
    fe_fill(o.x + S, cy, o.w - 2 * S, ui_cmdbar_h(), C_CARD);
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, T(browse_vhd() ? S_VHD_BKEYS : browse_imgdir() ? S_IMGDIR_KEYS : browse_dir() ? S_DIR_KEYS : browse_bsave() ? S_BAS_SAVE_KEYS : browse_bas() ? S_BAS_LOAD_KEYS : zin ? (browse_slot < MSX_CART_SLOTS ? S_BROWSE_KEYS_ZIPROM : S_BROWSE_KEYS_ZIP) : browse_slot < MSX_CART_SLOTS ? S_BROWSE_KEYS_ROM : S_BROWSE_KEYS_NM));
    if (browse_vhd()) vb_draw_pop(X, W, ly + (uint32_t)rows * rh / 6);   /* 3.7.2 */
    if (dd_on && dd_id == 3000) {                       /* ROMtype 풀다운 — 고른 줄 아래, ROMtype 칸에 맞춤 */
        uint32_t dw = 380 * S; if (dw > W) dw = W;
        uint32_t dx = show_map ? c_map : X + W / 2; if (dx + dw > X + W) dx = X + W - dw;
        int rr = menu_sel - menu_top; if (rr < 0) rr = 0;
        dd_draw_at(dx, ly + (uint32_t)(rr + 1) * rh, dw, ly + (uint32_t)rows * rh);
    }
    if (dir_naming) {                                   /* 3.6.4: Ins → 새 폴더 팝업 (가운데) */
        uint32_t pw = 600 * S, ph = 4 * LH + SLH + ui_cmdbar_h() + 48 * S;
        if (pw > W) pw = W;
        uint32_t px = X + (W - pw) / 2, py = ly + (uint32_t)rows * rh / 4;
        ui_rrect(px + 5 * S, py + 6 * S, pw, ph, 10 * S, C_SHADOW);
        ui_rframe(px, py, pw, ph, 10 * S, C_ACC, C_CARD);
        uint32_t ty = py + 12 * S;
        text_f(F, S, px + 16 * S, ty, T(S_DIR_NEWDIR), C_TEXT, 0xFF, 0, 1);
        ty += LH + 6 * S;
        small_ell(px + 16 * S, ty, cur_disp, C_MUTED, C_CARD, pw - 32 * S);
        ty += SLH + 10 * S;
        uint32_t ih = LH + 10 * S;
        ui_rrect(px + 16 * S, ty, pw - 32 * S, ih, 5 * S, C_FIELD);
        ui_rrect(px + 16 * S, ty, 3 * S, ih, 1 * S, C_ACC);
        char nb[72]; copy_str(nb, sizeof nb, dir_newname); str_append(nb, sizeof nb, "_");
        text_ell(px + 28 * S, ui_ty(ty, ih), nb, C_TEXT, 0xFF, pw - 56 * S);
        ui_cmdbar(px + S, py + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(S_DIR_NEW_KEYS));
    }
    if (del_ask) {                                      /* 3.6.4: 지울까요? */
        uint32_t pw = 600 * S, ph = 3 * LH + 2 * SLH + ui_cmdbar_h() + 52 * S;
        if (pw > W) pw = W;
        uint32_t px = X + (W - pw) / 2, py = ly + (uint32_t)rows * rh / 4;
        ui_rrect(px + 5 * S, py + 6 * S, pw, ph, 10 * S, C_SHADOW);
        ui_rframe(px, py, pw, ph, 10 * S, C_BAD, C_CARD);
        uint32_t ty = py + 12 * S;
        text_f(F, S, px + 16 * S, ty, T(S_DEL_ASK), C_TEXT, 0xFF, 0, 1);
        ty += LH + 8 * S;
        draw_icon(px + 16 * S, ty + LH / 8, del_isdir, C_CARD);
        text_ell(px + 22 * S + LH, ty, del_name, del_isdir ? C_WARN : C_TEXT, 0xFF, pw - 44 * S - LH);
        ty += LH + 6 * S;
        if (del_isdir) small_ell(px + 16 * S, ty, T(S_DEL_DIR_NOTE), C_WARN, C_CARD, pw - 32 * S);
        ui_cmdbar(px + S, py + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(S_DEL_KEYS));
    }
}

/* 정보 줄: 이름 (흐림) + 값 */
static uint32_t info_kw;                                /* 3.7.0: 이름 칸 폭 (0 = 전체의 1/4) */
static uint32_t info_line(uint32_t x, uint32_t y, uint32_t w, const char *k, const char *v, uint8_t vc)
{
    uint32_t kw = info_kw ? info_kw : w / 4;
    if (k && k[0]) text_f(SF, 1, x, y + (LH - SLH) / 2, k, C_MUTED, 0xFF, 0, 1);
    text_clip(x + (k && k[0] ? kw : 0), y, v, vc, 0xFF, w - (k && k[0] ? kw : 0));
    return LH + 6 * S;
}


/* ---- 4.0: X86MSX에 대하여 = 위 머리 + 스크롤 되는 본문 (정보·Credits·라이선스, 라이선스 파일 내용 포함) ---- */
#include "gpl3.inc"
#include "../third_party/tinf/tinf.h"
static char about_txt[44 * 1024];
static int about_scroll, about_total;
static void about_add(const char *t) { str_append(about_txt, sizeof about_txt, t); about_len = (int)strlen(about_txt); }
static void about_build(void)
{
    about_txt[0] = 0; about_len = 0; about_scroll = 0;
    about_add("# "); about_add(T(S_EMULATION)); about_add("\n");
    char l[200];
#define ABL(k, v) do { copy_str(l, sizeof l, k); str_append(l, sizeof l, ":  "); str_append(l, sizeof l, v); str_append(l, sizeof l, "\n"); about_add(l); } while (0)
    ABL(T(S_MSX_MACHINE), "MSX1 · MSX2 · MSX2+ · MSX turbo R");
    ABL("CPU", "Z80 · R800");
    ABL(T(S_VIDEO), "TMS9918A · V9938 · V9958");
    ABL(T(S_AB_SND), "PSG · SCC · MSX-MUSIC · MSX-AUDIO · MoonSound · OPM/OPP · PCM · MIDI");
    ABL(T(S_OV_MEDIA), T(S_AB_MEDIA_V));
    about_add("\n# "); about_add(T(S_AB_PC)); about_add("\n");
    ABL(T(S_MSX_MACHINE), machine_name);
    char scr[32] = ""; num_append(scr, sizeof scr, D.w, 0, ' '); str_append(scr, sizeof scr, " x "); num_append(scr, sizeof scr, D.h, 0, ' ');
    ABL(T(S_AB_SCREEN), scr);
    if (wc_status[0]) ABL(T(S_VIDEO), wc_status);
    if (snd_status[0]) ABL(T(S_SOUND), snd_status);
#undef ABL
    about_add("\n# Credits\n");
    about_add("Font: Galmuri (c) Lee Minseo, SIL Open Font License 1.1\n");
    about_add("Machine profiles, ROM database and shortcuts follow blueMSX.\n");
    about_add(T(S_AB_TM)); about_add("\n");
    about_add("made in Kim taewoo\n");
    about_add("\n# "); about_add(T(S_AB_LICENSE)); about_add("\n");
    about_add("X86MSX \xC2\xA9 2026  Kim Taewoo\n\n");
    about_add("X86MSX는 GNU GPL 버전 3(또는 그 이후 버전)의 자유 소프트웨어입니다. 소스는 X86MSX_src 파일에 있고, 보증은 없습니다. 전문은 USB 맨 위의 LICENSE.txt, 함께 쓰는 다른 사람의 소프트웨어(emu2413·emu8950·tinf·Galmuri·DejaVu·C-BIOS·TimGM6mb·Nextor 등)와 그 조건은 THIRD-PARTY-NOTICES.txt에 있습니다. Nextor 파일은 GPL이 아니며 LICENSE-Nextor.txt 조건(무료 배포, 판매 금지)을 따릅니다. 도움말 → X86MSX에 대하여 맨 아래에도 요약이 있습니다.\n\n");
    about_add("# English\n");
    about_add("X86MSX \xC2\xA9 2026 Kim Taewoo\nFree software under the GNU GPL v3 or later. Provided WITHOUT ANY WARRANTY.\nMSX BIOS and game ROMs belong to their respective owners and are not covered by this license.\n\n");
    about_add("# THIRD-PARTY-NOTICES\nTHIRD-PARTY-NOTICES 파일은 USB memory에 있습니다.\n\n");
    about_add("# LICENSE-Nextor\nLICENSE-Nextor 파일은 USB memory에 있습니다.\n\n");
    about_add("# LICENSE (GNU GPL v3)\n");
    { unsigned int dl = (unsigned)(sizeof about_txt - 1 - (size_t)about_len);     /* 원문은 줄여 두었다가 여기서 펴서 붙임 */
      if (dl >= GPL3_RAW && tinf_uncompress(about_txt + about_len, &dl, gpl3_z, sizeof gpl3_z) == TINF_OK) { about_len += (int)dl; about_txt[about_len] = 0; } }
}
static void about_draw(UiRect b, uint32_t x, uint32_t w, uint32_t y, uint32_t bottom)
{
    if (!about_len) about_build();
    uint32_t top = y;
    about_total = (int)help_render(x, w - 14 * S, top, bottom, 0, about_txt, 0);
    int maxs = about_total > (int)(bottom - top) ? about_total - (int)(bottom - top) : 0;
    if (about_scroll > maxs) about_scroll = maxs;
    if (about_scroll < 0) about_scroll = 0;
    help_render(x, w - 14 * S, top, bottom, about_scroll, about_txt, 1);
    if (maxs) ui_sbar(b.x + b.w - 14 * S, top, bottom - top, &about_scroll, 0, (int)(bottom - top), maxs + (int)(bottom - top));
}
static void about_key(int key)
{
    menu_dirty = 1;
    int page = (int)(D.h / 2);
    switch (key) {
    case 0x01: case 0x1C: case 0x9C: dialog_back(); return;
    case 0xC8: about_scroll -= (int)(LH * 2); return;
    case 0xD0: about_scroll += (int)(LH * 2); return;
    case 0xC9: about_scroll -= page; return;
    case 0xD1: case 0x39: about_scroll += page; return;
    case 0xC7: about_scroll = 0; return;
    case 0xCF: about_scroll = 1 << 20; return;
    }
}

static void draw_info(int about)
{
    UiRect o;
    UiRect b = ui_full_card(about ? T(S_ABOUT) : T(S_DIAG_TITLE), "X86MSX " MSXEMU_VERSION " (" MSXEMU_BUILD ")", about ? 0 : machine_name, &o);
    uint32_t pad = 24 * S, x = b.x + pad, w = b.w - 2 * pad, y = b.y + 16 * S, bottom = dlg_cmd_y() - 8 * S;
    if (about) {                                        /* 3.7.0: 로고 머리 + 에뮬레이션·이 PC·만든 것 */
        uint32_t hh = 2 * LH + SLH + 30 * S;
        ui_rrect(x, y, w, hh, 8 * S, C_HEAD);
        static const uint8_t band[4] = { C_BAD, C_WARN, C_OK, C_ACC };
        uint32_t bw = 5 * S, bx = x + 18 * S, by = y + 14 * S, bh = hh - 28 * S;
        for (int i = 0; i < 4; i++) ui_rrect(bx + (uint32_t)i * (bw + 3 * S), by, bw, bh, 2 * S, band[i]);
        uint32_t tx = bx + 4 * (bw + 3 * S) + 14 * S;
        uint32_t tw = text_f(F, 2 * S, tx, by - 2 * S, "X86MSX", C_TEXT, 0xFF, 0, 1);
        char ver[64]; copy_str(ver, sizeof ver, T(S_AB_VER)); str_append(ver, sizeof ver, " " MSXEMU_VERSION "  ·  " MSXEMU_BUILD);
        text_f(SF, 1, tx + tw + 14 * S, by + LH * 2 - SLH - 4 * S, ver, C_MUTED, 0xFF, 0, 1);
        text_clip(tx, by + 2 * LH + 2 * S, T(S_ABOUT_TEXT), C_TEXT, 0xFF, x + w - tx - 12 * S);
        y += hh + 12 * S;
        about_draw(b, x, w, y, bottom);
    } else {
        ui_rrect(x, y, w, LH + 14 * S, 6 * S, C_HEAD);
        text_clip(x + 12 * S, ui_ty(y, LH + 14 * S), T(S_DIAG_HINT), C_WARN, 0xFF, w - 24 * S);
        y += LH + 26 * S;
        y += ui_section(x, y, w, "System");
        char t[64] = "";
        num_append(t, sizeof t, (uint32_t)measured_speed, 0, ' ');
        str_append(t, sizeof t, " %   busy ");
        num_append(t, sizeof t, (uint32_t)measured_busy, 0, ' ');
        str_append(t, sizeof t, " %");
        y += info_line(x, y, w, "speed", t, C_TEXT);
        char t3[32] = "";
        num_append(t3, sizeof t3, gap_show, 0, ' ');
        str_append(t3, sizeof t3, " ms");
        y += info_line(x, y, w, "frame gap max", t3, gap_show > 40 ? C_WARN : C_TEXT);
        { char pt[160]; pf_text(pt, sizeof pt, 0); y += info_line(x, y, w, "time share", pt, C_TEXT); pf_text(pt, sizeof pt, 1); y += info_line(x, y, w, "worst/s", pt, C_TEXT); }
        y += info_line(x, y, w, "video", wc_status, C_TEXT);
        if (vs_status[0]) y += info_line(x, y, w, "vsync", vs_status, C_TEXT);   /* 5.2.17 */
        y += info_line(x, y, w, "sound", snd_status, C_TEXT);
        if (usb_status[0]) y += info_line(x, y, w, "usb", usb_status, C_TEXT);
        char t2[96] = "msxromdb.xml ";
        str_append(t2, sizeof t2, romdb_len ? "OK" : "-");
        str_append(t2, sizeof t2, "   msxsysromdb.xml ");
        str_append(t2, sizeof t2, sysdb_len ? "OK" : "-");
        y += info_line(x, y, w, "ROM DB", t2, romdb_len && sysdb_len ? C_OK : C_WARN);
        static char lines[40][64];
        int n = P.info ? P.info(lines, 40) : 0;
        if (n) y += 6 * S + ui_section(x, y + 6 * S, w, "Devices");
        for (int i = 0; i < n && y + LH < bottom; i++) y += info_line(x, y, w, 0, lines[i], C_TEXT);
    }
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, about ? "↑/↓ 스크롤  PgUp/PgDn 쪽  Home/End 처음·끝  Esc 돌아가기" : T(S_PRESS_ESC));
}

/* ---- 소리 진단 (2.8.1): 칩 상태를 0.5초마다 다시 읽고, T로 시험음 ---- */
static uint32_t tone_until, tone_phase;
static void draw_sound_diag(void)
{
    UiRect o;
    int tone = (int32_t)(tone_until - P.ms()) > 0;
    UiRect b = ui_full_card(T(S_SOUND_DIAG), snd_status, tone ? "TEST TONE" : 0, &o);
    uint32_t pad = 24 * S, x = b.x + pad, w = b.w - 2 * pad, y = b.y + 16 * S, bottom = dlg_cmd_y() - LH * 2 - 16 * S;
    {
        char t[64] = "";
        num_append(t, sizeof t, (uint32_t)volume, 0, ' ');
        str_append(t, sizeof t, muted ? " % (mute)" : " %");
        y += info_line(x, y, w, "volume", t, muted ? C_WARN : C_TEXT);
        uint32_t bw = w / 2, k = bw * (uint32_t)volume / 100;
        ui_rrect(x + w / 4, y, bw, 4 * S, 2 * S, C_LINE);
        if (k) ui_rrect(x + w / 4, y, k, 4 * S, 2 * S, tone ? C_OK : C_ACC);
        y += 16 * S;
    }
    static char lines[40][64];
    int n = P.info ? P.info(lines, 40) : 0;                 /* HDA 장치 줄만 */
    y += ui_section(x, y, w, "HD Audio");
    for (int i = 0; i < n && y + LH < bottom; i++)
        if (lines[i][0] == 'H' && lines[i][1] == 'D' && lines[i][2] == 'A') y += info_line(x, y, w, 0, lines[i], C_TEXT);
    n = P.sound_diag ? P.sound_diag(lines, 40) : 0;
    if (n) y += 6 * S + ui_section(x, y + 6 * S, w, "Codec");
    for (int i = 0; i < n && y + LH < bottom; i++) y += info_line(x, y, w, 0, lines[i], C_TEXT);
    small_ell(x, dlg_cmd_y() - SLH - 12 * S, T(S_SOUND_DIAG_NOTE), C_WARN, 0xFF, w);
    ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, T(S_SOUND_DIAG_KEYS));
}

/* ---- USB 전체 용량 사용 ---- */

static void size_text(char *d, int max, uint64_t bytes)
{
    d[0] = 0;
    if (bytes >= (1ull << 30)) {
        uint64_t t = bytes * 10 / (1ull << 30);
        num_append(d, max, (uint32_t)(t / 10), 0, ' ');
        str_append(d, max, ".");
        num_append(d, max, (uint32_t)(t % 10), 0, ' ');
        str_append(d, max, " GB");
    } else { num_append(d, max, (uint32_t)(bytes >> 20), 0, ' '); str_append(d, max, " MB"); }
}

static char grow_what[16];
/* 확장 단계(usbgrow.c 가 progress 로 알려 주는 이름)마다 지금 무엇을 하는지 한 줄로 안내 (한·일 외는 영어) */
static const char *grow_stage_text(const char *w)
{
    int k = !strcmp(w, "read") ? 0 : !strcmp(w, "format") ? 1 : !strcmp(w, "FAT") ? 2 : !strcmp(w, "files") ? 3 : !strcmp(w, "mbr") ? 4 : !strcmp(w, "done") ? 5 : -1;
    static const char *const ko[] = { "1/5  USB 안의 파일을 읽어 두는 중입니다 (메모리에 보관)", "2/5  새 파일 시스템의 시작 영역을 만드는 중입니다", "3/5  파일 위치표(FAT)를 쓰는 중입니다 (가장 오래 걸릴 수 있음)", "4/5  폴더와 파일을 새 자리에 옮겨 쓰는 중입니다", "5/5  파티션 크기를 바꾸고 확인하는 중입니다 (거의 끝)", "끝났습니다" };
    static const char *const ja[] = { "1/5  USB内のファイルを読み込んでいます(メモリに保管)", "2/5  新しいファイルシステムの先頭領域を作っています", "3/5  ファイル配置表(FAT)を書いています(最も時間がかかる場合あり)", "4/5  フォルダとファイルを新しい位置へ書き込んでいます", "5/5  パーティションサイズを変更して確認しています(もうすぐ完了)", "完了しました" };
    static const char *const en[] = { "1/5  Reading the files on the USB into memory", "2/5  Creating the start of the new file system", "3/5  Writing the file allocation table (FAT) - this can take the longest", "4/5  Writing folders and files to their new places", "5/5  Updating and verifying the partition size (almost done)", "Finished" };
    if (k < 0) return "";
    return lang == 0 ? ko[k] : lang == 2 ? ja[k] : en[k];
}
static void draw_grow(void)
{
    UiRect o;
    UiRect b = ui_full_card(T(S_USB_GROW), 0, grow_state == 1 ? "..." : 0, &o);
    uint32_t pad = 24 * S, lx = b.x + pad, lw = b.w - 2 * pad, y = b.y + 18 * S;
    uint64_t part = 0, disk = 0;
    if (!P.grow_query || P.grow_query(&part, &disk)) { text_clip(lx, y, T(S_GROW_UNSUPPORTED), C_TEXT, 0xFF, lw); y += LH * 2; }
    else {
        char v[32];
        y += ui_section(lx, y, lw, "USB");
        size_text(v, sizeof v, part); y += info_line(lx, y, lw, T(S_GROW_PART), v, C_TEXT);
        size_text(v, sizeof v, disk); y += info_line(lx, y, lw, T(S_GROW_DISK), v, C_OK);
        if (disk) {                                       /* 쓰는 부분 막대 */
            uint32_t k = (uint32_t)((uint64_t)lw * part / disk);
            ui_rrect(lx, y + 4 * S, lw, 8 * S, 4 * S, C_LINE);
            ui_rrect(lx, y + 4 * S, k < 8 * S ? 8 * S : k, 8 * S, 4 * S, C_ACC);
            y += 28 * S;
        }
        y += info_line(lx, y, lw, 0, T(S_GROW_EXPLAIN), C_TEXT);
        y += info_line(lx, y, lw, 0, T(S_GROW_WARN), C_WARN) + LH;
    }
    if (grow_state == 1) {
        text_clip(lx, y, T(S_GROW_RUNNING), C_TEXT, 0xFF, lw); y += LH + 10 * S;
        ui_rrect(lx, y, lw, LH, LH / 2, C_FIELD);
        uint32_t k = (lw - 4 * S) * (uint32_t)grow_pct / 100;
        if (k) ui_rrect(lx + 2 * S, y + 2 * S, k, LH - 4 * S, (LH - 4 * S) / 2, C_OK);
        y += LH + 8 * S;                                   /* 4.0.28: 몇 %인지, 어느 단계인지 글자로도 보여 줌 */
        { char pt[48]; pt[0] = 0; num_append(pt, sizeof pt, (uint32_t)grow_pct, 0, ' '); str_append(pt, sizeof pt, " %   "); str_append(pt, sizeof pt, grow_what); text_clip(lx, y, pt, C_TEXT, 0xFF, lw); }
        y += LH + 6 * S;
        text_clip(lx, y, grow_stage_text(grow_what), C_OK, 0xFF, lw); y += LH + 6 * S;      /* 지금 하는 일 */
        text_clip(lx, y, T(S_GROW_WARN), C_WARN, 0xFF, lw);
    } else if (grow_state == 2) text_clip(lx, y, grow_msg, C_TEXT, 0xFF, lw);
    if (grow_state != 1) ui_cmdbar(o.x + S, dlg_cmd_y(), o.w - 2 * S, grow_state == 0 ? T(S_GROW_CONFIRM) : grow_done ? T(S_GROW_ANYKEY) : T(S_PRESS_ESC));
}

static void grow_progress(int pct, const char *what)
{
    copy_str(grow_what, sizeof grow_what, what ? what : "");
    grow_pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    draw_grow();
}

static int grow_w_stub(const char *a, const uint8_t *d, uint32_t n) { (void)a; (void)d; (void)n; return -1; }
static int grow_wat_stub(const char *a, uint32_t o, const uint8_t *d, uint32_t n) { (void)a; (void)o; (void)d; (void)n; return -1; }
static int grow_cr_stub(const char *a, int dir) { (void)a; (void)dir; return -1; }
static int grow_del_stub(const char *a) { (void)a; return -1; }
static int grow_mk_stub(const char *a, uint32_t n, const uint8_t *p, uint32_t pl, void (*pr)(int)) { (void)a; (void)n; (void)p; (void)pl; (void)pr; return -1; }
static int grow_hdw_stub(int u, uint32_t l, const uint8_t *b) { (void)u; (void)l; (void)b; return -1; }
static void grow_start(void)
{
    if (!P.grow_run || grow_state != 0) return;
    grow_state = 1; grow_pct = 0; grow_done = 0;
    draw_grow();
    /* 확장하는 동안과 확장 뒤에는 USB 쓰기를 모두 막는다. 펌웨어(UEFI)·FAT 캐시는 옛 FAT·FSInfo·폴더를 기억하고 있어서,
     * 확장 뒤에 설정·상태 저장 같은 쓰기가 한 번이라도 나가면 옛 내용이 새 파일 시스템 위에 덮여 Windows 가 "손상"으로 본다 (실기에서 발견).
     * 먼저 밀린 저장을 모두 끝낸 뒤 막는다 */
    bgw_finish(); vhd_sync_all(); prn_flush(0); midi_flush(); rec_stop(); vrec_stop(); sram_save_all(); resume_save(); disk_flush(0); disk_flush(1); tape_flush(); hd_flush(); settings_flush();
    FePlatform sv = P;
    P.write_file = grow_w_stub; P.write_at = grow_wat_stub; P.append = grow_w_stub; P.create = grow_cr_stub; P.delete_file = grow_del_stub; P.make_file = grow_mk_stub; P.hd_write = grow_hdw_stub; P.hd_flush = 0;
    char err[96] = "";
    int r = P.grow_run(grow_progress, err, sizeof err);
    if (r == 1) { P.write_file = sv.write_file; P.write_at = sv.write_at; P.append = sv.append; P.create = sv.create; P.delete_file = sv.delete_file; P.make_file = sv.make_file; P.hd_write = sv.hd_write; P.hd_flush = sv.hd_flush; }   /* 아무것도 안 바꿨으면 되돌림 */
    grow_state = 2;
    if (r == 0) {
        copy_str(grow_msg, sizeof grow_msg, T(S_GROW_DONE));
        grow_done = 1;                                    /* 4.0.28: 자동 재부팅 대신 아무 키나 누를 때까지 기다림 */
        menu_dirty = 1;
        draw_grow();
    } else if (r == 1) copy_str(grow_msg, sizeof grow_msg, T(S_GROW_ALREADY));
    else { copy_str(grow_msg, sizeof grow_msg, T(S_GROW_FAIL)); str_append(grow_msg, sizeof grow_msg, " "); str_append(grow_msg, sizeof grow_msg, err); }
    menu_dirty = 1;
}

/* ---- 기종 선택 (설계서 §5.4) ---- */
#define MACH_MAX 1000                        /* 4.0.28: 400 → 1000 (blueMSX 전체 기종 436개가 넘어 뒤쪽 기종이 잘려 안 보이던 문제) */
static char mach[MACH_MAX][64];
static int nmach, mach_sel, mach_top;
static int mach_ci[MACH_MAX];                /* 목록 줄 → 검사 결과 칸 (2.11: 세대를 바꿔도 결과를 기억) */
static char mc_name[MACH_MAX][64];
static uint8_t mc_st[MACH_MAX];             /* 0 아직 검사 안 함, 1 켤 수 있음, 2 BIOS 없음, 3 미지원 VDP, 4 설정 오류 */
static char mc_miss[MACH_MAX][72];
static int mc_n;
#define mach_st(i) mc_st[mach_ci[i]]
#define mach_miss(i) mc_miss[mach_ci[i]]

static int mc_find(const char *name)        /* 이름의 검사 칸 (없으면 새로) */
{
    for (int k = 0; k < mc_n; k++) if (!strcmp(mc_name[k], name)) return k;
    if (mc_n >= MACH_MAX) return MACH_MAX - 1;
    copy_str(mc_name[mc_n], sizeof mc_name[0], name);
    mc_st[mc_n] = 0; mc_miss[mc_n][0] = 0;
    return mc_n++;
}

static void mach_check(int i)               /* 한 번 검사하고 기억 */
{
    if (i < 0 || i >= nmach || mach_st(i)) return;
    int r = P.read_file ? machine_check(P.read_file, mach[i], mach_miss(i), sizeof mach_miss(i)) : 0;
    mach_st(i) = (uint8_t)(r == 0 ? 1 : r == 1 ? 2 : r == 2 ? 3 : 4);
}
static int mach_ok(int i)
{
    if (i < 0 || i >= nmach) return 0;
    mach_check(i);
    return mach_st(i) == 1;
}
static char mach_msg[200];

static const char *device_name(int t)
{
    switch (t) {
    case 64: return "Disk(MB8877A)"; case 65: return "Disk(WD2793)"; case 57: case 143: return "Disk(TC8566AF)";
    case 63: return "Disk(Microsol)"; case 24: case 30: return "Kanji"; case 32: return "S1985"; case 26: return "SRAM";
    case 78: return "MSX-MUSIC"; case 79: return "MoonSound"; case 80: return "MSX-AUDIO"; case 19: return "Mirrored ROM";
    default: return 0;
    }
}

static void mach_insert_sorted(const char *name)
{
    if (nmach >= MACH_MAX) return;
    int i = nmach++;
    while (i > 0 && name_cmp(mach[i - 1], name) > 0) { memcpy(mach[i], mach[i - 1], 64); i--; }
    copy_str(mach[i], 64, name);
}

/* 세대: 0 MSX1, 1 MSX2, 2 MSX2+, 3 MSXturboR — 기종 폴더 이름 맨 앞("MSX2+ - …")으로만 (2.11: 이름 안의 "Turbo"로 잘못 나누던 것 고침) */
static char mach_all[MACH_MAX][64];
static int nmach_all, mach_gen;
static int mach_gen_of(const char *n)
{
    char p[16];
    int k = 0;
    while (n[k] && n[k] != ' ' && k < 15) { p[k] = (char)lower_c((uint8_t)n[k]); k++; }   /* 첫 낱말: msx, msx2, msx2+, msxturbor */
    p[k] = 0;
    if (!strcmp(p, "msxturbor") || !strcmp(p, "msxtr")) return 3;
    if (!strcmp(p, "msx2+")) return 2;
    if (!strcmp(p, "msx2")) return 1;
    return 0;
}

static void mach_filter(void)                           /* 고른 세대의 기종만 목록에 */
{
    nmach = 0; mach_sel = 0; mach_top = 0;
    for (int i = 0; i < nmach_all; i++)
        if (mach_gen_of(mach_all[i]) == mach_gen) { mach_ci[nmach] = mc_find(mach_all[i]); copy_str(mach[nmach++], sizeof mach[0], mach_all[i]); }
    for (int i = 0; i < nmach; i++) if (!strcmp(mach[i], machine_name)) { mach_sel = i; mach_st(i) = 1; }
    /* 2.11: 여기서 모든 기종을 검사하지 않는다 (BIOS가 없는 기종이 많으면 느렸음). 보이는 줄을 한 프레임에 하나씩 검사 */
}

static void open_machines(void);
static void open_machines_edit(void) { open_medit_name(machine_name); }   /* 2.16: 켜진 머신으로 바로 열고, 다른 머신은 편집기의 "머신 이름" 콤보에서 고름 (blueMSX와 같음) */
static void mach_scan(void)                              /* Machines 폴더의 기종 이름을 정렬해 mach[]에 (전체) */
{
    nmach = 0; mach_sel = 0; mach_top = 0; mach_msg[0] = 0;
    int n = P.list_dir ? P.list_dir("0:\\Machines", entries, DIR_MAX) : -1;
    for (int i = 0; i < n; i++)
        if (entries[i].is_dir && !strncmp(entries[i].key[0] ? entries[i].key : entries[i].name, "MSX", 3))
            mach_insert_sorted(entries[i].key[0] ? entries[i].key : entries[i].name);
    nmach_all = nmach;
    for (int i = 0; i < nmach; i++) copy_str(mach_all[i], sizeof mach_all[0], mach[i]);
}
static void open_machines(void)
{
    mach_scan();
    mach_gen = mach_gen_of(machine_name);
    mach_filter();
    open_dialog(5);
}
/* 3.8.2: 속성>에뮬레이션>MSX 기종에서 ←/→ = 앞/다음 기종 (머신 편집기의 머신 이름과 같은 순서, 켤 수 없는 기종은 건너뜀) */
static void mach_step(int dir)
{
    mach_scan();
    if (nmach < 2) return;
    int cur = 0;
    for (int i = 0; i < nmach; i++) if (!strcmp(mach[i], machine_name)) cur = i;
    for (int k = 1; k < nmach; k++) {
        int i = ((cur + dir * k) % nmach + nmach) % nmach;
        if (!mach_ok(i)) continue;
        char err[160];
        if (!switch_machine(mach[i], err, sizeof err)) { resume_enter(); menu_dirty = 1; return; }
    }
}

/* 기종 키보드 (2.13): BIOS 0x002C + config.ini KeyLayout + 이름으로 판별 */
static int name_has(const char *s, const char *w)
{
    for (; *s; s++) { int i = 0; while (w[i] && s[i] == w[i]) i++; if (!w[i]) return 1; }
    return 0;
}
static void kbd_apply(void)
{
    const uint8_t *b = M->slot_rd[0][0][0];
    int b2c = (b && b[0] == 0xF3 && b[1] == 0xC3) ? b[0x2C] : -1;     /* BIOS는 DI; JP xxxx로 시작 */
    const char *kl = M->prof.key_layout;
    if (name_has(machine_name, "Russian")) kl = "russian";
    else if (name_has(machine_name, "Arabic")) kl = "arabic";
    else if (name_has(machine_name, "Polish")) kl = "polish";
    keymap_machine(keymap_detect(kl, b2c));
    input_init(&in);
}
static int cmos_capture(void);
static void update_has_fw(void)
{
    machine_has_fw = M->fw.kind ? 1 : 0;
    for (int i = 0; i < M->prof.region_count; i++) if (M->prof.region[i].blue_type == 27 || M->prof.region[i].blue_type == 68) machine_has_fw = 2;
}
static int switch_machine(const char *name, char *err, int errlen)
{
    if (strcmp(name, machine_name)) resume_save();     /* 4.0.34: 떠나는 기종의 상태를 그 기종 이름으로 저장 */
    cmos_capture();                                     /* 지금 기종의 배터리 백업 내용 */
    env_save_current();
    for (int q = MSX_INT_CART; q < MSX_CART_ALL; q++) sram_save(q);   /* 3.1.0: 내장 PAC (3.6.8: 내장 자리 모두) */
    fw_sram_save();                                     /* 3.4.0 */
    static MachineProfile prof;
    uint32_t tape_keep = M->tape_size;                  /* 4.0.34: 기종을 바꾸면 M을 새로 만들어 테이프 길이가 0이 되던 것 (RUN"CAS:" 장치 오류) */
    if (machine_load(M, P.read_file, name, &prof, err, errlen)) return -1;
    for (int q = MSX_INT_CART; q < MSX_CART_ALL; q++) sram_load(q);
    copy_str(machine_name, sizeof machine_name, name);
    fw_sram_load();
    update_has_fw();
    layout();
    fe_fill(0, 0, D.w, D.h, 1);
    env_apply();                                        /* 새 기종의 환경 (일반 롬팩은 빠짐) */
    x86midi_check(1);                                   /* GT 가 아닌 기종 + MIDI 기계 → X86MSX MIDI 카트리지 */
    if (tape_path[0]) msx_tape_insert(M, tape_buf, tape_keep, TAPE_MAX);   /* 처음 위치부터 */
    disks_reinsert();
    run_state = RUN_ON;
    msx_reset(M);
    kbd_apply();
    rtc_sync(1);
    /* 빠진 장치 알림 */
    char t[96] = "";
    for (int i = 0; i < prof.unsupported_count; i++) {
        const char *d = device_name(prof.unsupported[i]);
        if (!d) continue;
        str_append(t, sizeof t, t[0] ? ", " : "");
        str_append(t, sizeof t, d);
    }
    for (int i = 0; i < prof.missing_count; i++) {
        str_append(t, sizeof t, t[0] ? ", " : "");
        str_append(t, sizeof t, prof.missing[i]);
    }
    (void)t;                                            /* 기종 이름·빠진 장치 알림은 띄우지 않음 (기종 선택 창에 표시) */
    settings_changed();
    full_redraw();                                      /* 2.9: 배지·기종 이름 바로 다시 그림 */
    return 0;
}

static void machines_key(int key)
{
    int rows = list_rows();
    menu_dirty = 1;
    switch (key) {
    case 0xCB: case 0xCD: mach_gen = (mach_gen + (key == 0xCD ? 1 : 3)) % 4; mach_filter(); full_redraw(); break;   /* ←/→ 세대 */
    case 0x02: case 0x03: case 0x04: case 0x05: mach_gen = key - 0x02; mach_filter(); full_redraw(); break;          /* 2.16: 1~4 세대 */
    case 0x0F: mach_gen = (mach_gen + ((mods & 0x03) ? 3 : 1)) % 4; mach_filter(); full_redraw(); break;           /* 2.17: Tab / Shift+Tab 세대 */
    case 0xC8: if (mach_sel > 0) mach_sel--; break;     /* 2.11: 켤 수 없는 기종에도 커서가 감 (검사 때문에 느리지 않게) */
    case 0xD0: if (mach_sel < nmach - 1) mach_sel++; break;
    case 0xC9: case 0xD1: {
        int t = mach_sel + (key == 0xC9 ? -rows : rows);
        if (t < 0) t = 0;
        if (t > nmach - 1) t = nmach - 1;
        mach_sel = t < 0 ? 0 : t;
        break;
    }
    case 0x01: dialog_back(); break;
    case 0x1C: case 0x9C: {
        if (!nmach) break;
        if (!mach_ok(mach_sel)) {                       /* 켤 수 없는 기종: 이유를 아래에 */
            copy_str(mach_msg, sizeof mach_msg, T(S_MACHINE_FAIL)); str_append(mach_msg, sizeof mach_msg, " ");
            str_append(mach_msg, sizeof mach_msg, mach_st(mach_sel) == 2 ? T(S_BIOS_MISSING) : mach_st(mach_sel) == 3 ? T(S_NEXT_VER) : T(S_CFG_ERROR));
            str_append(mach_msg, sizeof mach_msg, " "); str_append(mach_msg, sizeof mach_msg, mach_miss(mach_sel));
            break;
        }
        char err[160];
        if (switch_machine(mach[mach_sel], err, sizeof err)) {
            copy_str(mach_msg, sizeof mach_msg, T(S_MACHINE_FAIL));
            str_append(mach_msg, sizeof mach_msg, " ");
            str_append(mach_msg, sizeof mach_msg, err);
        } else { dialog_done(); resume_enter(); }
        break;
    }
    default:
        if (key < 0x40 && sc_ascii[key] && nmach) {          /* 글자로 이동 ("MSX - " 뒤 첫 글자) */
            for (int k = 1; k <= nmach; k++) {
                int i = (mach_sel + k) % nmach;
                const char *nm = mach[i];
                const char *dash = strchr(nm, '-');
                const char *c = dash && dash[1] == ' ' ? dash + 2 : nm;
                if (lower_c(*c) == sc_ascii[key]) { mach_sel = i; break; }
            }
        }
        break;
    }
}

static void draw_machines(void)
{
    UiRect o;
    char chip[32] = "";
    num_append(chip, sizeof chip, (uint32_t)nmach, 0, ' ');
    str_append(chip, sizeof chip, " machines");
    UiRect b = ui_full_card(T(S_MACHINE_SELECT), machine_name, chip, &o);
    static const char *const gn[4] = { "MSX1", "MSX2", "MSX2+", "MSXturboR" };
    uint32_t pad = 14 * S, X = b.x + pad, W = b.w - 2 * pad, rh = list_rh();
    ui_tabs(b.x, b.y + 6 * S, b.w, gn, 4, mach_gen, 1);
    uint32_t ly = list_y0();
    int rows = list_rows();
    if (mach_sel < mach_top) mach_top = mach_sel;
    if (mach_sel >= mach_top + rows) mach_top = mach_sel - rows + 1;
    for (int r = 0; r < rows && mach_top + r < nmach; r++) {
        int i = mach_top + r, sel = i == mach_sel;
        uint32_t y = ly + (uint32_t)r * rh;
        ui_list_row_bg(X, y, W - 10 * S, rh, sel, i & 1, 1);
        hit_add(hc_sel, (intptr_t)&mach_sel, i, 0, 0, X, y, W - 10 * S, rh);   /* 3.0 마우스 */
        int cur = !strcmp(mach[i], machine_name);
        if (cur) ui_rrect(X + 12 * S, y + rh / 2 - 4 * S, 8 * S, 8 * S, 4 * S, C_OK);
        int okm = !mach_st(i) || mach_st(i) == 1;          /* 아직 검사 전이면 그냥 보임 (fe_frame이 하나씩 검사) */
        uint32_t nw = text_clip(X + 30 * S, ui_ty(y, rh), mach[i], sel ? 15 : okm ? C_TEXT : C_FAINT, 0xFF, W * 6 / 10);
        if (!okm) {
            char why[160] = "";
            if (mach_st(i) == 2) { str_append(why, sizeof why, T(S_BIOS_MISSING)); str_append(why, sizeof why, " "); str_append(why, sizeof why, mach_miss(i)); }
            else if (mach_st(i) == 3) { str_append(why, sizeof why, mach_miss(i)); str_append(why, sizeof why, " "); str_append(why, sizeof why, T(S_NEXT_VER)); }
            else str_append(why, sizeof why, T(S_CFG_ERROR));
            uint32_t cx = X + 30 * S + nw + 14 * S, ch = SLH + 6 * S;
            if (cx + 40 * S < X + W) {
                uint32_t cw = ui_sw(why) + 16 * S;
                if (cx + cw > X + W - 14 * S) cw = X + W - 14 * S - cx;
                ui_rrect(cx, y + (rh - ch) / 2, cw, ch, ch / 2, C_HEAD);
                small_ell(cx + 8 * S, ui_sty(y + (rh - ch) / 2, ch), why, mach_st(i) == 2 ? C_WARN : C_BAD, 0xFF, cw - 16 * S);
            }
        }
    }
    ui_sbar(X + W - 5 * S, ly, (uint32_t)rows * rh, &mach_top, &mach_sel, rows, nmach);
    if (!nmach) text_clip(X + 30 * S, ly, T(S_EMPTY_FOLDER), C_MUTED, 0xFF, W);
    uint32_t cy = dlg_cmd_y();
    if (mach_msg[0]) small_ell(X + 4 * S, cy - SLH - 10 * S, mach_msg, C_WARN, 0xFF, W);
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, T(S_MACHINE_KEYS));
}

/* ---- 속성 창 (blueMSX 속성의 탭 구성, 설계서 §14) ---- */
static int live_mixer(void);
static int key_layout;                          /* 0 한국, 1 미국, 2 일본 */
                                                /* joy_mode: 0 없음, 1 키보드 (위에서 선언) */
static int props_tab, props_sel[10], mix_sel;
static int outg_is(int tab) { return tab == 1 || tab == 2 || tab == 4; }          /* 화면·효과·소리 출력 = 출력장치 창의 탭 3개 */
static int outg_idx(int tab) { return tab == 1 ? 0 : tab == 2 ? 1 : 2; }
static int outg_tab(int idx) { static const int t[3] = { 1, 2, 4 }; return t[(idx % 3 + 3) % 3]; }
/* 믹서를 먼저 열 때 하던 곳(메뉴·팝업)을 적어 두었다가 믹서를 닫으면 되돌림 */
static struct { int dialog, tab, bar_visible, depth, bar_sel, pop_mode, menu_under, popup_act, popup_dlg, popup_tab, snap_valid, snap_bar, snap_depth, snap_sel[6], open_sel[6]; const Menu *open_menu[6], *snap_menu[6]; } ui_ret;
static void ui_ret_save(void)
{
    ui_ret.dialog = dialog; ui_ret.tab = props_tab; ui_ret.bar_visible = bar_visible; ui_ret.depth = depth; ui_ret.bar_sel = bar_sel;
    ui_ret.pop_mode = pop_mode; ui_ret.menu_under = menu_under; ui_ret.popup_act = popup_act; ui_ret.popup_dlg = popup_dlg; ui_ret.popup_tab = popup_tab;
    ui_ret.snap_valid = snap_valid; ui_ret.snap_bar = snap_bar; ui_ret.snap_depth = snap_depth;
    for (int k = 0; k < 6; k++) { ui_ret.snap_sel[k] = snap_sel[k]; ui_ret.snap_menu[k] = snap_menu[k]; ui_ret.open_sel[k] = open_sel[k]; ui_ret.open_menu[k] = open_menu[k]; }
    ui_ret_valid = 1;
}
static int ui_ret_restore(void)
{
    ui_ret_valid = 0;
    dd_on = 0;
    dialog = ui_ret.dialog; props_tab = ui_ret.tab; bar_visible = ui_ret.bar_visible; depth = ui_ret.depth; bar_sel = ui_ret.bar_sel;
    pop_mode = ui_ret.pop_mode; menu_under = ui_ret.menu_under; popup_act = ui_ret.popup_act; popup_dlg = ui_ret.popup_dlg; popup_tab = ui_ret.popup_tab;
    snap_valid = ui_ret.snap_valid; snap_bar = ui_ret.snap_bar; snap_depth = ui_ret.snap_depth;
    for (int k = 0; k < 6; k++) { snap_sel[k] = ui_ret.snap_sel[k]; snap_menu[k] = ui_ret.snap_menu[k]; open_sel[k] = ui_ret.open_sel[k]; open_menu[k] = ui_ret.open_menu[k]; }
    if (dialog == 6) props_fix_sel();
    input_init(&in);
    last_menu_key = P.ms();
    full_redraw(); menu_dirty = 1;
    return 1;
}
/* 4.0: 창마다 알맞은 크기 (S 단위 최대 가로·세로, 0 = 화면 가득). 목록 창(파일·기종·편집기)은 가득, 나머지는 줄임 */
static int mix_visible(int *vis);
/* 출력 장치 이름 — 1 내부 모니터, 2 내부 + 외부 복제, 3 외부 모니터 (플랫폼이 준 영어 줄을 알아보기 쉽게). 외부 출력이 없으면 그대로 */
static void vo_pretty(char (*ln)[64], int n)
{
    int hd = 0;
    for (int i = 1; i < n; i++) for (const char *t = ln[i]; *t; t++) if ((t[0] == ' ' && t[1] == 'c' && t[2] == 'o' && t[3] == 'p' && t[4] == 'y' && t[5] == ':') || (t[0] == ' ' && t[1] == 'o' && t[2] == 'n' && t[3] == 'l' && t[4] == 'y' && t[5] == ':') || (t[0] == ' ' && t[1] == 'm' && t[2] == 'i' && t[3] == 'r' && t[4] == 'r')) { hd = 1; break; }
    if (!hd) return;
    const char *nm[3][3] = { { "내부 모니터", "Internal monitor", "内蔵モニター" }, { "내부 + 외부 복제", "Internal + external (copy)", "内蔵 + 外部 (複製)" }, { "외부 모니터", "External monitor", "外部モニター" } };
    int L = lang == 0 ? 0 : lang == 2 ? 2 : 1;
    for (int i = 0; i < n && i < 8; i++) {
        char src[64], d[64]; copy_str(src, sizeof src, ln[i]);
        int kind = i == 0 ? 0 : 3;
        for (const char *t = src; *t; t++) {
            if (t[0] == ' ' && t[1] == 'c' && t[2] == 'o' && t[3] == 'p' && t[4] == 'y' && t[5] == ':') { kind = 1; break; }
            if (t[0] == ' ' && t[1] == 'm' && t[2] == 'i' && t[3] == 'r' && t[4] == 'r') { kind = 1; break; }
            if (t[0] == ' ' && t[1] == 'o' && t[2] == 'n' && t[3] == 'l' && t[4] == 'y' && t[5] == ':') { kind = 2; break; }
        }
        if (kind == 3) continue;                         /* 그 밖의 화면 출력은 그대로 */
        int q = 0; while (src[q] && src[q] != ':') q++;
        d[0] = 0; char num[4] = { (char)('0' + (i + 1) % 10), 0, 0, 0 }; str_append(d, sizeof d, num); str_append(d, sizeof d, ": "); str_append(d, sizeof d, nm[kind][L]);
        const char *det = src[q] ? src + q + 2 : src;    /* 장치 설명 (해상도·GPU·모니터 이름) */
        char dt[40]; int k = 0;
        for (; det[k] && k < 36; k++) {
            if (det[k] == ' ' && (det[k + 1] == 'c' || det[k + 1] == 'o' || det[k + 1] == 'm') && (det[k + 2] == 'o' || det[k + 2] == 'n' || det[k + 2] == 'i') && kind) { if ((det[k + 1] == 'c' && det[k + 2] == 'o') || (det[k + 1] == 'o' && det[k + 2] == 'n') || (det[k + 1] == 'm' && det[k + 2] == 'i')) break; }
            dt[k] = det[k];
        }
        dt[k] = 0;
        if (dt[0]) { str_append(d, sizeof d, " ("); str_append(d, sizeof d, dt); str_append(d, sizeof d, ")"); }
        copy_str(ln[i], 64, d);
    }
}
static void card_caps(uint32_t *w, uint32_t *h)
{
    uint32_t cw = 0, ch = 0;
    switch (dialog) {
    case 2: cw = 1000; break;                           /* 진단 */
    case 3: cw = 920; break;                            /* X86MSX에 대하여 */
    case 4: cw = 700; ch = 420; break;                  /* USB 전체 용량 */
    case 6:
        if (outg_is(props_tab)) cw = 720;               /* 출력장치 창 (시안 720 폭) */
        else if (props_tab == 5) cw = 1280;                  /* MIDI 창은 시안 너비 (5.2.197: 창 폭은 그대로, 줄만 좁힘) (좁은 화면이면 화면에 맞춰 줄어듦) */
        else if (props_tab < 6) cw = 1040;
        else if (props_tab == 6) { int vv[MIX_N + 2]; int n = mix_visible(vv); cw = (uint32_t)n * 112 + 28 + 70; if (cw < 460) cw = 460; if (cw > 980) cw = 980; }   /* 믹서: 채널 수만큼만 */
        else cw = 630;
        {   /* 내용만큼만 높이 (빈 공간 줄임). 0 = 화면 가득 (에뮬레이션 탭 = 슬롯 구성, MIDI 탭 = 아래 단추줄) */
            static const uint16_t hh[10] = { 660, 610, 520, 640, 340, 0, 0, 520, 480, 680 };
            ch = outg_is(props_tab) ? 640 : hh[props_tab];
        }
        break;   /* 속성 / 키보드·조이스틱·마우스 */
    case 7: cw = 860; ch = 580; break;                  /* 소리 진단 */
    case 10: cw = 920; break;                           /* 상태 목록 */
    case 11: cw = 880; ch = 700; break;                 /* 이미지 만들기 */
    case 12: cw = 1080; break;                          /* 도움말 */
    case 15: cw = 640; ch = 300; break;
    case 20: cw = 640; ch = 300; break;                 /* 5.2.192 언어 · 글꼴 */                 /* 끝내기·재시작 확인 */
    case 18: cw = 760; ch = 600; break;                 /* 네트워크 — 가운데 팝업 (MSX 화면이 뒤에 계속 보임) */
    default: break;
    }
    *w = cw * S; *h = ch * S;
}
/* 탭 0~5 = 옵션 속성 창(blueMSX 순서 + 4.0.89 출력 장치 탭), 6~9 = 도구 팝업(믹서, 키보드, 조이스틱, 마우스) */
static const StrId tab_label[10] = { S_TAB_EMU, S_VIDEO, S_EFFECTS, S_SOUND, S_OUTPUT, S_MIDI, S_TAB_MIXER, S_TAB_KEYBOARD, S_TAB_JOY, S_TAB_MOUSE };
#define PROPS_TABS 6
static int mt_fx;                                       /* fe_miditab.inc 에서 정의 (-1 = 효과 카드 초점 없음) */

static int mix_present(int c) { return mix_shown(c); }

static void mix_apply(void)
{
    volume = mix_vol[MIX_MASTER];
    muted = mix_mute[MIX_MASTER];
    { static int was_muted; if (muted && !was_muted) silence_all(); was_muted = muted; }   /* 4.0.34: 무음 = 내장·USB MIDI도 (울리던 음 끄기 + 새 음 세기 0) */
    for (int c = 0; c < MSX_CH_N; c++) {               /* 본체 채널 = 음량·좌우 한 줄로 (5.2.14: 번호가 같음) */
        M->ch_vol[c] = mix_mute[c] ? 0 : mix_vol[c];
        M->pan[c] = (int8_t)(msx_ch_info[c].pan_ok ? mix_pan[c] : 0);
    }
    msx_midi_int_vol = (mix_mute[MIX_MIDI_INT] || muted) ? 0 : mix_vol[MIX_MIDI_INT];   /* 3.7.0 내장 MIDI (Note On 세기) */
    usb_midi_vol = (mix_mute[MIX_MIDI_USB] || muted) ? 0 : mix_vol[MIX_MIDI_USB];   /* 3.7.0 USB MIDI */
    M->stereo = snd_stereo;                             /* 3.3.3 */
}
/* MIDI 탭(5)도 MSX 를 멈추지 않음 — 멈추면 음원이 안 돌아 시험음·소리 막대가 안 나왔음 */
static int live_mixer(void) { return (dialog == 6 && props_tab >= 5) || dialog == 18 || dialog == 19; }
static int live_pop(void) { return pop_mode && depth > 0 && !dialog && open_menu[0] == &m_media; }   /* 미디어 팝업(RCtrl+RShift+D)도 MSX 를 멈추지 않음 */
static int live_ui(void) { return live_mixer() || live_pop(); }   /* MIDI 믹서도 */   /* 4.0: 입력장치 팝업도 MSX 화면을 뒤에 계속 보임 */   /* 3.3.3: 믹서 창은 MSX를 멈추지 않음 (계기·소리 확인) */
static int mix_has_pan(int c) { return c >= 0 && c < MSX_CH_N && msx_ch_info[c].pan_ok; }   /* 본체 채널 표 (내장·USB MIDI·마스터·KEY 는 좌우 없음) */   /* 3.7.0: 내장·USB MIDI·기타는 좌우 없음 */
/* 3.7.1: 진폭 → 0~1000, 48 dB 눈금 (16384 = 0 dB 위 끝, 64 = -48 dB 아래 끝). log2를 1/256 단위로 */
static int meter_db(int v)
{
    if (v <= 64) return 0;
    if (v >= 16384) return 1000;
    int e = 31 - __builtin_clz((unsigned)v);           /* 정수 부분 */
    int f = (int)(((uint32_t)v << (16 - e)) & 0xFFFF);   /* 소수 부분 (0~65535) → 1 + f 의 log2 ≈ f·(1 + 0.34·(1 − f)) */
    int fr = (int)((int64_t)f * (65536 + (int64_t)(65536 - f) * 22282 / 65536) / 65536) >> 8;   /* 0~255 */
    int l = e * 256 + fr - 6 * 256;                     /* log2(v) − log2(64), 0 ~ 8·256 */
    return l * 1000 / (8 * 256);
}
/* 계기: 이번 프레임 최대 진폭 → 0~1000 (빨리 오르고 천천히 내려감) */
static void mix_meter_frame(void)
{
    static uint32_t last_int, last_usb;                 /* 3.7.0: 내장·USB MIDI 계기 = 이번 프레임 보낸 바이트 */
    static int vel_int, vel_usb;
    {                                                   /* 쓰인 채널 — 칩이 소리를 낸 적이 있음 (음량·끄기와 무관). 리셋·전원·카트리지 빼기 때 지움 */
        static uint32_t ep, li;
        if (ep != msx_snd_epoch) { ep = msx_snd_epoch; mix_used = 0; }
        for (int c = 0; c < MSX_CH_N; c++) if (snd_active(c)) mix_used |= 1u << c;
        if (msx_midi_int_bytes != li) { li = msx_midi_int_bytes; mix_used |= 1u << MIX_MIDI_INT; }
        if (M->midi_n) mix_used |= 1u << MIX_MIDI_SOFT | 1u << MIX_MIDI_USB;   /* 내장 MSX-MIDI·SFG 가 MIDI 를 보냄 */
    }
    int peak = 0;
    for (int i = 0; i < M->psg.buf_n; i++) { int a = M->psg.buf[i] < 0 ? -M->psg.buf[i] : M->psg.buf[i]; if (a > peak) peak = a; }
    for (int c = 0; c < MIX_N; c++) {
        int v = c == MIX_MASTER ? peak * volume / 100 : c < MSX_CH_N ? M->lvl[c] : 0;
        if (c == MIX_MIDI_INT) { if (msx_midi_int_bytes != last_int) { last_int = msx_midi_int_bytes; vel_int = 12000 * msx_midi_int_vol / 100; } else vel_int = vel_int * 7 / 8; v = vel_int; }
        if (c == MIX_MIDI_USB) { if (usb_midi_sent != last_usb) { last_usb = usb_midi_sent; vel_usb = 12000 * usb_midi_vol / 100; } else vel_usb = vel_usb * 7 / 8; if (vel_usb > v) v = vel_usb; }   /* MIDI 띠 = 소프트웨어 음원 진폭과 USB 보낸 세기 중 큰 것 */
        int t = meter_db(v);                            /* 3.7.1: dB 눈금 (선형이면 보통 음량에서 한두 칸만 켜졌음) */
        mix_meter[c] = t > mix_meter[c] ? t : mix_meter[c] * 7 / 8;
    }
    { int t1 = meter_db(M->scc1_lvl), t2 = meter_db(M->scc2_lvl);   /* 5.2.159 */
      scc1_meter = t1 > scc1_meter ? t1 : scc1_meter * 7 / 8; scc2_meter = t2 > scc2_meter ? t2 : scc2_meter * 7 / 8; }
}

static int opt_prop_change(int id, int dir);           /* 5.2.38 설정 표 (아래) */
static int opt_prop_slide(int id, uint32_t f);
static void open_props(int tab)
{
    dd_on = 0;
    props_tab = tab;
    mix_vol[MIX_MASTER] = volume;
    mix_mute[MIX_MASTER] = muted;
    if (tab == 6) mix_sel = MIX_MASTER;                 /* 믹서를 열면 마스터 볼륨이 먼저 골라져 있음 */
    open_dialog(6);
}

/* 항목 표: 종류 0 머리글, 1 선택형(값), 2 체크, 3 막대, 4 안내(흐림) */
typedef struct { int kind, id; char text[96], value[64]; int on, enabled, grp; } PropItem;   /* grp = 에뮬레이션 탭 카드(0 기종·슬롯, 1·2 속도 왼쪽·오른쪽, 3 상태 저장, 4 기능, 5 플래시) */
static int prop_grp;
static int opt_prop_item(PropItem *q, int id, int enabled);   /* 5.2.41 설정 표 (아래) */
#include "fe_sel.inc"                                   /* 5.2.121 선택형 설정 표 */
#define ADDSEL(i, e) do { const FeSel *s_ = sel_find(i); ADD(1, (i), T(s_->name), sel_label(s_, sel_cur(s_)), 0, (e)); } while (0)
#define ADDSLIDE(i, e) do { const FeSlide *s_ = slide_find(i); char v_[64]; int p_ = 0; s_->item(v_, sizeof v_, &p_); ADD(3, (i), T(s_->name), v_, p_, (e)); } while (0)
#define ADDO(i, e) do { PropItem *q = &it[n]; if (opt_prop_item(q, (i), (e))) { q->grp = prop_grp; n++; } } while (0)

static int props_items(int tab, PropItem *it)
{
    int n = 0;
    prop_grp = 0;
#define ADD(k, i, t, v, o, e) do { PropItem *q = &it[n++]; q->kind = (k); q->id = (i); copy_str(q->text, sizeof q->text, (t)); copy_str(q->value, sizeof q->value, (v)); q->on = (o); q->enabled = (e); q->grp = prop_grp; } while (0)
    switch (tab) {
    case 1:                                             /* 비디오 */
        ADD(0, 0, T(S_VIDEO), "", 0, 1);                /* 3.6.1: 모니터 유형은 효과 창 (스프라이트 아래) */
        {   /* 3.2.3: 모니터 에뮬레이션 (blueMSX 속성 ▶ 비디오와 같은 자리). 8비트 색 화면에서는 쓸 수 없음 */
            static const StrId mn[6] = { S_MEMU_NONE, S_MEMU_MON, S_MEMU_YC, S_MEMU_YCN, S_MEMU_COMP, S_MEMU_COMPN };
            const char *mv = mon_emu == 6 ? "Scale 2x" : mon_emu == 7 ? "Hq2x" : T(mn[mon_emu % 6]);
            ADD(1, 41, T(S_MON_EMU), D.bpp == 1 ? T(S_MEMU_NONE) : mv, 0, D.bpp != 1);
        }
        ADDSEL(57, 1);                                  /* 3.2.15 */
        {   /* 화면 소스 (V9990 이 꽂혀 있을 때만 고를 수 있음) */
            char v[64]; copy_str(v, sizeof v, vsrc_pref < 0 ? T(S_VSRC_AUTO) : sel_lab_vsrc(vsrc_pref >= VSRC_N + 2 ? 0 : vsrc_pref + 1));
            if (vsrc_pref < 0 && M->v9) { str_append(v, sizeof v, " ("); str_append(v, sizeof v, msx_vsrc[vsrc_cur].key); str_append(v, sizeof v, ")"); }
            ADD(1, 160, T(S_VSRC), v, 0, M->v9 != 0);
        }
        {                                               /* 2.14: 주사 주파수 (자동 = 기종대로) */
            char v[32];
            if (M->vsync_force) copy_str(v, sizeof v, M->vsync_force == 2 ? "50 Hz" : "60 Hz");
            else { copy_str(v, sizeof v, T(S_VSYNC_AUTO)); str_append(v, sizeof v, msx_is_pal(M) ? " (50 Hz)" : " (60 Hz)"); }
            ADD(1, 50, T(S_VSYNC), v, 0, 1);
        }
        ADD(0, 0, T(S_VID_ADJUST), "", 0, 1);           /* 2.12: 밝기·대비·채도·감마 */
        ADDO(45, 1); ADDO(46, 1); ADDO(47, 1); ADDO(48, 1);   /* 밝기·대비·채도·감마 — 설정 표 */
        ADD(1, 49, T(S_VIDEO_RESET), "", 0, 1);
        ADD(0, 0, "VDP", "", 0, 1);
        ADDO(58, M->vdp.v9938);                         /* 3.2.15 VID-04 (V9938 이상) */
        break;
    case 2:                                             /* 효과 */
        ADD(0, 0, T(S_EFFECTS), "", 0, 1);
        ADDO(53, D.bpp != 1); ADDO(54, D.bpp != 1); ADDO(55, D.bpp != 1);   /* 3.2.11: openMSX처럼 흐림·잔상·잡음 세기 (32비트 색 화면). 5.2.41 설정 표 */
        ADD(0, 0, T(S_SPR_GROUP), "", 0, 1);              /* 3.2.0 VID-03 */
        ADDO(51, 1); ADDO(52, 1);
        ADD(0, 0, T(S_MON_GROUP), "", 0, 1);              /* 3.6.1: 모니터 유형·스캔라인 (스프라이트 아래) */
        ADDSEL(40, 1);
        ADDSLIDE(42, 1);
        break;
    case 3:                                             /* 사운드 */
        ADD(0, 0, T(S_CHIP_EMU), "", 0, 1);
        {   /* 3.0: 음원 켜기/끄기 = 믹서 채널 음소거 (장치는 그대로 돌고 소리만). 지금 없는 음원은 흐림. 3.7.0: 음원 표 11종 (+ 기타) */
            int cc = 0;                                 /* 칩이 많아서 두 칸 (왼쪽 = 종류 2, 오른쪽 = 종류 5) */
            for (int c = 0; c < MIX_N; c++) if (snd_is_src(c)) {
                ADD((cc++ & 1) ? 5 : 2, 140 + c, c == MIX_SCC && msx_scc_count(M) >= 2 ? "SCC1 (Konami)" : snd_full(c), "", snd_present(c) && !mix_mute[c], snd_present(c));
                if (c == MIX_SCC && snd_present(MIX_SCC2)) ADD((cc++ & 1) ? 5 : 2, 156, "SCC2 (Konami)", "", !mix_mute[MIX_SCC], 1);   /* SCC 카트리지가 둘이면 SCC1·SCC2 (켜고 끄기는 같이: 믹서 채널은 하나) */
            }
        }
        if (msx_dev(M, MDEV_MIDI_HW)) { ADD(0, 0, T(S_MIDI_INT), "", 0, 1); ADD(2, 116, T(S_MIDI_INT_PORT), "", midi_int, 1); ADD(4, 0, T(S_MIDI_INT_NOTE), "", 0, 1); }   /* GT 내장 MIDI 켜기는 사운드 탭 (마스터님 결정) */
        ADDO(110, P.audio2 != 0);                       /* 3.3.3 SND3-01 (RAlt+F6) */
        ADD(1, 0, "48 kHz", snd_stereo && P.audio2 ? T(S_STEREO_ON) : T(S_MONO), 0, 0);
        break;
    case 4:                                             /* 4.0.89: 출력 장치 탭 (MIDI 앞) */
        if (P.video_outputs) {                          /* 4.0.91: 화면 출력은 출력 장치 탭으로 옮김 (항상 보임) */
            char ln[8][64]; int act = 0, vn = P.video_outputs(ln, 8, &act); vo_pretty(ln, vn);
            if (vn <= 1) { ADD(1, 59, T(S_VID_OUT), T(S_VID_NONE), 0, 1); ADD(4, 0, T(S_VID_NOHDMI), "", 0, 1); }   /* 밑에 안 꽂혀 있다고 알림 */
            else { int sel = video_out >= 0 && video_out < vn ? video_out : 0; char v[96]; copy_str(v, sizeof v, ln[sel]); if (sel != act) { str_append(v, sizeof v, "  ("); str_append(v, sizeof v, T(S_VID_OUT_NOTE)); str_append(v, sizeof v, ")"); } ADD(1, 59, T(S_VID_OUT), v, 0, 1); }
        }
        ADD(0, 0, T(S_BACKEND), "", 0, 1);
        ADD(1, 43, T(S_SND_OUT), T(snd_out_str(audio_out)), 0, P.audio_select != 0);
        ADD(2, 44, T(S_AUDIO_RESCAN), "", 0, P.audio_select != 0);
        ADD(1, 0, T(S_AUDIO_USB), "", 0, 0);
        ADD(1, 0, "HD Audio", snd_status, 0, 0);
        break;
    case 5: break;                                      /* MIDI = 5.2.54 부터 새 탭(fe_miditab.inc)이 그림 — 옛 줄 목록은 5.2.121 에서 지움 */
    case 0: {                                           /* 카드 배치 — 순서 = 위아래 키 이동 순서 (draw_emu_tab) */
        static char fsw[64], psw[64];
        copy_str(fsw, sizeof fsw, T(S_FRONT_SW));
        copy_str(psw, sizeof psw, T(S_PAUSE_SW));
        prop_grp = 0;
        ADD(1, 1, T(S_MSX_MACHINE), machine_name, 0, 1);
        prop_grp = 6;                                   /* 슬롯 확장 = 기종 아래 한 줄, 세 칸 버튼 */
        ADDSEL(8, 1);
        prop_grp = 1;
        prop_grp = 1;
        ADDSLIDE(3, 1);
        if (msx_dev(M, MDEV_S1990)) ADDSLIDE(9, 1);      /* turbo R: R800 = R800 % × 배수 (실제 클럭 7.159 MHz 기준) */
        prop_grp = 2;
        ADD(2, 4, T(S_SPEED_MAX), "", speed_max, 1);
        ADDSLIDE(117, 1);                               /* 3.6.6: 속도 배수 게이지 1배·2배·4배·최대 */
        prop_grp = 3;
        ADDO(172, 1);
        ADDO(15, 1);                                    /* 2.11 */
        prop_grp = 4;
        ADDO(171, 1);                                   /* 4.0.31 */
        ADDO(170, 1);
        ADDO(175, 1);   /* blueMSX의 'FDD 접근 시 가속'처럼 — FDD·HDD를 읽고 쓰는 동안 제한 없이 */
        if ((msx_dev(M, MDEV_MATSU) || machine_has_fw) && !pana_has(LED_PFRONT)) ADD(2, 112, fsw, "", fw_switch, 1);   /* 3.4.0 내셔널 등. 4.0: 프런트 스위치가 있는 기종은 아래 항목과 같은 스위치라 하나만 */
        if (pana_has(LED_PFRONT)) ADD(2, 5, fsw, "", fw_switch, 1);
        if (pana_has(LED_PPAUSE)) ADD(2, 6, psw, "", pana_pause, 1);
        prop_grp = 5;
        ADD(2, 173, T(S_FLASH_YAM), "", cart_flash_auto & 1, 1);   /* 플래시 카트리지 자동 인식 스위치 (기본 켜짐) */
        ADD(2, 174, T(S_FLASH_A16X), "", (cart_flash_auto >> 1) & 1, 1);
        break;
    }
    case 7:
        ADD(0, 0, T(S_KB_LAYOUT), "", 0, 1);
        ADD(2, 10, T(S_KB_KR), "", key_layout == 0, 1);
        ADD(2, 11, T(S_KB_US), "", key_layout == 1, 1);
        ADD(2, 12, T(S_KB_JP), "", key_layout == 2, 1);
        ADD(2, 16, T(S_KB_DE), "", key_layout == KL_DE, 1);
        ADD(2, 17, T(S_KB_FR), "", key_layout == KL_FR, 1);
        ADD(2, 18, T(S_KB_UK), "", key_layout == KL_UK, 1);
        ADD(4, 0, T(S_KB_NOTE), "", 0, 0);
        ADD(4, 0, T(S_KB_NOTE2), "", 0, 0);
        {                                               /* 2.13: 기종의 MSX 키보드 (자동 판별) */
            static char kbt[64];
            copy_str(kbt, sizeof kbt, T(S_MSX_KBD));
            str_append(kbt, sizeof kbt, ": ");
            str_append(kbt, sizeof kbt, keymap_name(keymap_machine_get()));
            ADD(4, 0, kbt, "", 0, 0);
        }
        break;
    case 8:
        ADD(0, 0, T(S_TAB_JOY), "", 0, 1);
        for (int p = 0; p < 2; p++) {                   /* 2.13: 없음 / 키보드 / 게임패드 1 / 게임패드 2 */
            static char jt[2][48];
            int jm = joy_mode[p];
            if (jm >= 2) {
                char nm[24] = "";
                int st = P.pad_read ? P.pad_read(jm - 2, nm, sizeof nm) : -1;
                copy_str(jt[p], sizeof jt[p], T(jm == 2 ? S_JOY_PAD1 : S_JOY_PAD2));
                str_append(jt[p], sizeof jt[p], " ");
                str_append(jt[p], sizeof jt[p], st < 0 ? T(S_PAD_NONE) : nm);
            } else copy_str(jt[p], sizeof jt[p], T(jm ? S_JOY_KEYS : S_JOY_NONE));
            ADD(1, 20 + p, T(p ? S_PORT2 : S_PORT1), jt[p], 0, 1);
        }
        for (int p = 0; p < 2; p++) {                   /* 2.9: 버튼마다 키 */
            if (joy_mode[p] != 1) continue;
            static const StrId jn[6] = { S_JOY_UP, S_JOY_DOWN, S_JOY_LEFT, S_JOY_RIGHT, S_JOY_BA, S_JOY_BB };
            ADD(0, 0, T(p ? S_PORT2_KEYS : S_PORT1_KEYS), "", 0, 1);
            for (int k = 0; k < 6; k++) {
                char kn[24];
                int id = 60 + p * 6 + k;
                if (joy_cap == id) copy_str(kn, sizeof kn, T(S_JOY_PRESS)); else scan_name(joy_keys[p][k], kn, sizeof kn);
                ADD(1, id, T(jn[k]), kn, 0, 1);
            }
        }
        ADD(4, 0, T(S_JOY_NOTE), "", 0, 0);
        break;
    case 9:                                             /* 3.0 PC 마우스 */
        ADD(0, 0, T(S_MOUSE_LIST), "", 0, 1);
        {   /* 3.8.2: 연결된 마우스·터치패드를 모두 보이고, 앞의 단추로 사용/비사용 */
            char dn[40]; int on, nd = 0;
            for (int k = 0; k < 6 && P.mouse_dev && P.mouse_dev(k, dn, sizeof dn, &on); k++, nd++) ADD(2, 200 + k, dn, "", on, 1);
            if (!nd) ADD(1, 0, T(S_MOUSE_DEV), T(S_JOY_NONE), 0, 0);
        }
        ADD(0, 0, T(S_TAB_MOUSE), "", 0, 1);
        { char st[24] = ""; if (mouse_count) { num_append(st, sizeof st, (uint32_t)mouse_count, 0, ' '); str_append(st, sizeof st, " "); str_append(st, sizeof st, T(S_MOUSE_CONNECTED)); } else copy_str(st, sizeof st, T(S_JOY_NONE));
          ADD(1, 0, T(S_MOUSE_STATE), st, 0, 0); }
        ADDO(94, 1);                                    /* 3.4.3 MOUSE-05 */
        ADDO(92, mouse_on);
        ADDO(93, mouse_on);
        ADDSEL(98, mouse_on);
        ADDO(99, mouse_on && mouse_menu);
        { char h[96]; copy_str(h, sizeof h, T(S_MOUSE_MSXGRP)); str_append(h, sizeof h, machine_name); ADD(0, 0, h, "", 0, 1); }   /* 3.4.4: 아래 넷은 기종별 */
        ADDSEL(90, mouse_on);
        ADDO(95, mouse_on && mouse_port);
        ADDO(96, mouse_on && mouse_port);
        ADDSEL(97, mouse_on && mouse_port);
        ADD(4, 0, T(S_MOUSE_NOTE), "", 0, 0);
        break;
    }
#undef ADD
    return n;
}

static int item_selectable(const PropItem *q) { return q->enabled && (q->kind == 1 || q->kind == 2 || q->kind == 3 || q->kind == 5 || q->kind == 6); }   /* 종류 5 = 앞 항목 줄의 오른쪽 반에 놓는 켜기/끄기 */

static void props_fix_sel(void)
{
    static PropItem it[32];
    int n = props_items(props_tab, it), *sel = &props_sel[props_tab];
    if (*sel < 0 || *sel >= n || !item_selectable(&it[*sel]))
        for (*sel = 0; *sel < n && !item_selectable(&it[*sel]); (*sel)++) ;
    if (props_tab == 6 && !mix_present(mix_sel)) mix_sel = MIX_PSG;
}

/* 항목 높이 */
static uint32_t item_h_of(const PropItem *q, int first)
{
    if (q->kind == 0) return (first ? 0 : 4 * S) + SLH + 8 * S;   /* 제목 줄 간격을 줄여 한눈에 */
    if (q->kind == 4) return SLH + 10 * S;
    if (q->kind >= 5) return 0;                         /* 종류 5·6 = 앞 줄의 오른쪽 반 */
    return ui_row_h();
}
/* 에뮬레이션 탭 = 카드 배치. 항목의 grp로 자리를 정함 — 0 기종·슬롯(두 칸), 1·2 속도(왼쪽·오른쪽 열), 3 상태 저장, 4 기능, 5 플래시 카드 */
static void set_expand(int want);
static void hc_slide(const UiHit *h, int ev);
static void hc_fx(const UiHit *h, int ev);
static void hc_segexp(const UiHit *h, int ev)           /* 슬롯 확장 세 칸 버튼을 누르면 바로 그 값으로 */
{
    props_sel[0] = (int)h->a;
    if (ev == 1 && h->b != slot_expand) set_expand(h->b);
}
typedef struct { uint32_t x, y, w, h; int col, ord; } EmuR;
static EmuR emu_r[32];
static uint32_t emu_top_x, emu_top_y, emu_top_w, emu_top_h;   /* 기종·슬롯 확장 한 카드 */
static uint32_t emu_card_x[3], emu_card_w[3], emu_card_y, emu_card_h, emu_spd_y, emu_total, emu_rh;
static void emu_layout(const PropItem *it, int n, uint32_t X, uint32_t W, uint32_t top, uint32_t avail)
{
    uint32_t rh = ui_row_h(), gap = 8 * S, ct = SLH + 12 * S, sec = SLH + 10 * S, q4 = 4 * S, q8 = 8 * S;
    int cnt[7] = { 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < n && i < 32; i++) { int g = it[i].grp < 0 || it[i].grp > 6 ? 0 : it[i].grp; emu_r[i].ord = cnt[g]++; emu_r[i].col = g; }
    int cr = cnt[3] > cnt[4] ? cnt[3] : cnt[4]; if (cnt[5] > cr) cr = cnt[5];
    int rows = 2 + cnt[1] + (cnt[2] ? 1 : 0) + cr;      /* 줄 수 (일반 1 + 속도 + 카드) */
    uint32_t cth = 2 * rh + 6 * S;                      /* 기종·확장 카드 높이 (두 줄) */
    uint32_t base = cth + gap + sec + (uint32_t)(cnt[1] + (cnt[2] ? 1 : 0)) * rh + gap + q4 + ct + (uint32_t)cr * rh + q8;
    if (avail && avail < base) {                        /* 낮은 화면: 간격을 줄여 슬롯 구성을 가리지 않게 */
        gap = 2 * S; ct = SLH + 2 * S; sec = SLH + 1 * S; q4 = 0; q8 = 4 * S; cth = 2 * rh + 2 * S;
        base = cth + gap + sec + (uint32_t)(cnt[1] + (cnt[2] ? 1 : 0)) * rh + gap + q4 + ct + (uint32_t)cr * rh + q8;
    }
    uint32_t extra = avail > base + 10 * S ? avail - base - 10 * S : 0;       /* 남는 높이를 줄·간격에 나눠 줌 (위로 붙지 않게) */
    uint32_t rb = extra * 40 / 100 / (uint32_t)rows; if (rb > 12 * S) rb = 12 * S;
    uint32_t rem = extra - rb * (uint32_t)rows, gx = rem / 4; if (gx > 26 * S) gx = 26 * S;
    uint32_t rhh = rh + rb, y = top + gx / 2;
    emu_rh = rh;
    uint32_t hw = (W - gap) / 2;
    {   /* 위 카드 = 왼쪽 기종, 오른쪽 카트리지 슬롯 확장 (세 칸 알약) */
        uint32_t ch = 2 * rhh + (cth - 2 * rh);
        emu_top_x = X; emu_top_y = y; emu_top_w = W; emu_top_h = ch;
        uint32_t lw = W * 50 / 100;
        for (int i = 0; i < n && i < 32; i++) {
            if (it[i].grp == 0) { emu_r[i].x = X + 6 * S; emu_r[i].y = y + 3 * S; emu_r[i].w = lw - 12 * S; emu_r[i].h = ch - 6 * S; }
            else if (it[i].grp == 6) { emu_r[i].x = X + lw; emu_r[i].y = y + 3 * S; emu_r[i].w = W - lw - 6 * S; emu_r[i].h = ch - 6 * S; }
        }
        y += ch + gap + gx;
    }
    emu_spd_y = y; y += sec + rb / 2;
    for (int i = 0; i < n && i < 32; i++) if (it[i].grp == 1) { emu_r[i].x = X; emu_r[i].y = y + (uint32_t)emu_r[i].ord * rhh; emu_r[i].w = W; emu_r[i].h = rhh; }
    y += (uint32_t)cnt[1] * rhh;
    for (int i = 0; i < n && i < 32; i++) if (it[i].grp == 2) { emu_r[i].x = X + (uint32_t)emu_r[i].ord * (hw + gap); emu_r[i].y = y; emu_r[i].w = hw; emu_r[i].h = rhh; }
    y += (cnt[2] ? rhh : 0) + gap + gx + q4;
    static const uint32_t wt[3] = { 9, 12, 11 };
    uint32_t tw = W - 2 * gap, sx = X;
    for (int c = 0; c < 3; c++) { emu_card_x[c] = sx; emu_card_w[c] = tw * wt[c] / 32; sx += emu_card_w[c] + gap; }
    emu_card_w[2] = X + W - emu_card_x[2];
    uint32_t cpad = gx / 2;
    emu_card_y = y; emu_card_h = ct + cpad + (uint32_t)cr * rhh + q8 + cpad / 2;
    for (int i = 0; i < n && i < 32; i++) if (it[i].grp >= 3 && it[i].grp <= 5) {
        int c = it[i].grp - 3;
        emu_r[i].x = emu_card_x[c] + 6 * S; emu_r[i].y = y + ct + cpad + (uint32_t)emu_r[i].ord * rhh; emu_r[i].w = emu_card_w[c] - 12 * S; emu_r[i].h = rhh;
    }
    emu_total = y + emu_card_h - top;
}
static void draw_emu_tab(PropItem *it, int n, int selidx, uint32_t X, uint32_t W, uint32_t top, uint32_t bottom)
{
    emu_layout(it, n, X, W, top, bottom > top ? bottom - top : 0);
    ui_rframe(emu_top_x, emu_top_y, emu_top_w, emu_top_h, 8 * S, C_LINE, C_HEAD);
    ui_section(X + 6 * S, emu_spd_y, W - 12 * S, T(S_GRP_SPEED));
    static const StrId ct[3] = { S_GRP_STATE, S_GRP_FUNC, S_GRP_FLASH };
    for (int c = 0; c < 3; c++) {
        ui_rframe(emu_card_x[c], emu_card_y, emu_card_w[c], emu_card_h, 8 * S, C_LINE, C_HEAD);
        ui_section(emu_card_x[c] + 6 * S, emu_card_y + 2 * S, emu_card_w[c] - 12 * S, T(ct[c]));
    }
    for (int i = 0; i < n && i < 32; i++) {
        PropItem *q = &it[i]; EmuR *r = &emu_r[i];
        int sl = i == selidx && item_selectable(q);
        if (q->grp == 0 || q->grp == 6) {               /* 위 카드 — 이름(작게) 위, 값(크게) 아래 */
            uint32_t r1 = emu_rh, h2 = r->h > 2 * r1 ? r->h / 2 : r1, y1 = r->y, y2 = r->y + h2;
            if (sl) { ui_rrect(r->x, r->y, r->w, r->h, 6 * S, C_ROWSEL); ui_rrect(r->x, r->y + 6 * S, 4 * S, r->h > 12 * S ? r->h - 12 * S : r->h, 2 * S, C_ACC); }
            text_clip(r->x + 16 * S, ui_ty(y1, h2), q->text, sl ? C_TEXT : C_MUTED, 0xFF, r->w > 26 * S ? r->w - 26 * S : 0);
            if (q->grp == 0) {                          /* 기종 — 값은 파란 글씨, 선택하면 오른쪽 'Enter ▼' */
                uint32_t ch = SLH + 6 * S, cw = ui_sw("Enter ▼") + 26 * S;
                text_clip(r->x + 16 * S, ui_ty(y2, h2), q->value, C_ACC, 0xFF, r->w > cw + 40 * S ? r->w - cw - 40 * S : r->w);
                if (sl && r->w > cw + 8 * S) ui_keychip(r->x + r->w - cw, y2 + (h2 - ch) / 2, ch, "Enter ▼", 0);
                hit_add(hc_val, (intptr_t)&props_sel[0], i, 0, 0x1C, r->x, r->y, r->w, r->h);
            } else {
                uint32_t sx = r->x + 12 * S, sw = r->w > 24 * S ? r->w - 24 * S : 0, gp = 6 * S;
                uint32_t bw = sw > 2 * gp ? (sw - 2 * gp) / 3 : 0; if (bw > 150 * S) bw = 150 * S;
                const char *sn[3] = { T(S_NONE), T(S_EXP_S1), T(S_EXP_S2) };
                for (int k = 0; k < 3; k++) {
                    uint32_t bx = sx + (uint32_t)k * (bw + gp), by = y2 + 2 * S, bh = h2 > 4 * S ? h2 - 4 * S : h2, on = k == slot_expand, tw = text_w(sn[k]);
                    if (on) ui_rrect(bx, by, bw, bh, bh / 2, C_ACC); else ui_rframe(bx, by, bw, bh, bh / 2, C_LINE, C_CHIP);
                    text_clip(bx + (bw > tw ? (bw - tw) / 2 : 0), ui_ty(y2, h2), sn[k], on ? 15 : C_MUTED, 0xFF, bw);
                    hit_add(hc_segexp, (intptr_t)i, k, 0, 0, bx, by, bw, bh);
                }
                hit_add(hc_val, (intptr_t)&props_sel[0], i, 0, 0x1C, r->x, r->y, r->w, h2);
            }
            continue;
        }
        uint32_t vx = r->x + (q->grp == 1 ? r->w * 40 / 100 : q->grp == 2 ? r->w * 30 / 100 : q->kind == 1 ? r->w * 38 / 100 : 0);
        uint32_t dy = r->y + (r->h > emu_rh ? (r->h - emu_rh) / 2 : 0);
        if (item_selectable(q)) hit_add(hc_val, (intptr_t)&props_sel[0], i, q->kind == 2 ? 0 : (int)vx, 0x1C, r->x, r->y, r->w, r->h);
        if (q->kind == 2) ui_row(r->x, dy, r->w, vx, q->text, "", 0, UV_TOGGLE, q->on, sl, q->enabled);
        else if (q->kind == 3) {
            ui_row(r->x, dy, r->w, vx, q->text, q->value, 0, UV_SLIDER, q->on * 2, sl, q->enabled);
            if (q->enabled && !RTL) {                   /* 막대를 누르거나 끌면 그 자리 값으로 */
                uint32_t vw = r->x + r->w > vx + 14 * S ? r->x + r->w - vx - 14 * S : 0, bw = vw * 55 / 100;
                if (bw > 20 * S) hit_add(hc_slide, q->id, i, (int)bw, 0, vx > 8 * S ? vx - 8 * S : 0, r->y, bw + 16 * S, r->h);
            }
        }
        else ui_row(r->x, dy, r->w, vx, q->text, q->value, 0, UV_DROP, 0, sl, q->enabled);
    }
}
/* 에뮬레이션 탭 키 — ↑/↓ = 목록 차례(카드 안 위→아래, 다음 카드), ←/→ = 스위치면 옆 카드의 같은 줄, 선택·막대면 값 바꾸기 */
static void props_change(int id, int dir);
static int emu_key(PropItem *it, int n, int *sel, int key)
{
    if (key == 0xC8 || key == 0xD0) {
        int d = key == 0xC8 ? -1 : 1;
        for (int k = *sel + d; k >= 0 && k < n; k += d) if (item_selectable(&it[k])) { *sel = k; break; }
        return 1;
    }
    if (key == 0xCB || key == 0xCD) {
        int d = key == 0xCB ? -1 : 1, cur = *sel;
        if (cur < 0 || cur >= n) return 1;
        if (it[cur].grp == 0) { if (d > 0 && cur + 1 < n && it[cur + 1].grp == 6) *sel = cur + 1; return 1; }   /* 기종 → 오른쪽 슬롯 확장 */
        if (it[cur].grp == 6 && d < 0 && slot_expand == 0 && cur > 0 && it[cur - 1].grp == 0) { *sel = cur - 1; return 1; }   /* 확장 '없음'에서 ← = 기종 */
        if (it[cur].kind != 2) { props_change(it[cur].id, d); return 1; }
        if (it[cur].grp == 2) { if (d > 0 && cur + 1 < n && it[cur + 1].grp == 2 && item_selectable(&it[cur + 1])) *sel = cur + 1; return 1; }   /* 최대 속도 → 오른쪽 속도 배수 */
        int best = -1, bg = it[cur].grp, ord = emu_r[cur].ord;
        for (int g = bg + d; g >= 3 && g <= 5 && best < 0; g += d) {   /* 옆 열: 같은 줄, 없으면 그 열의 마지막 줄 */
            int pick = -1;
            for (int k = 0; k < n; k++) if (it[k].grp == g && it[k].kind == 2 && item_selectable(&it[k]) && (pick < 0 || emu_r[k].ord <= ord)) pick = k;
            if (pick >= 0) best = pick;
        }
        if (best >= 0) *sel = best;
        return 1;
    }
    return 0;
}

/* 항목 목록 그리기 (속성 창 공용, 2.16 새 모양). bottom까지, 고른 줄이 보이게 위를 넘김 */
/* 출력장치 창 '소리 출력' 탭 = 시안의 장치 카드 (같은 항목·키·마우스, 그림만 카드) */
static void draw_out_cards(PropItem *it, int n, int selidx, uint32_t X, uint32_t W, uint32_t top, uint32_t bottom)
{
    uint32_t y = top, ch = LH + SLH + 22 * S, gap = 8 * S;
    for (int i = 0; i < n; i++) {
        PropItem *q = &it[i];
        if (q->kind == 0) { if (i) y += 4 * S; ui_section(X + 6 * S, y, W - 12 * S, q->text); y += item_h_of(q, i == 0); continue; }
        if (q->kind == 4) { if (y + SLH < bottom) small_ell(X + 22 * S, y + 2 * S, q->text, C_MUTED, 0xFF, W - 40 * S); y += SLH + 6 * S; continue; }
        if (y + ch > bottom) break;
        int sel = i == selidx && item_selectable(q), btn = q->kind == 2;
        uint32_t h = btn ? LH + 18 * S : ch, bw = btn ? ui_sw(q->text) + 28 * S : W;
        if (item_selectable(q)) hit_add(hc_val, (intptr_t)&props_sel[props_tab], i, 0, 0x1C, X, y, bw, h);
        if (sel) { ui_rrect(X, y, bw, h, 10 * S, C_ACC); ui_rrect(X + 2 * S, y + 2 * S, bw - 4 * S, h - 4 * S, 8 * S, C_ROWSEL); } else ui_rframe(X, y, bw, h, 10 * S, C_LINE, btn ? C_HEAD : C_CARD);
        if (btn) { text_f(SF, 1, X + 14 * S, ui_sty(y, h), q->text, q->enabled ? C_TEXT : C_FAINT, 0xFF, bw, 1); y += h + gap; continue; }
        uint32_t ix = X + 14 * S, is = 36 * S, iy = y + (h - is) / 2;                  /* 아이콘: 소리 막대 세 개 */
        ui_rrect(ix, iy, is, is, 9 * S, C_CHIP);
        for (int b = 0; b < 3; b++) { uint32_t bh = (b == 1 ? 18 : 10) * S; fe_fill(ix + 9 * S + (uint32_t)b * 7 * S, iy + (is - bh) / 2, 4 * S, bh, q->enabled ? C_ACC : C_FAINT); }
        uint32_t tx = ix + is + 14 * S;
        text_f(&font_bold, 1, tx, y + 8 * S, q->text, q->enabled ? C_TEXT : C_MUTED, 0xFF, W - 200 * S, 1);
        if (q->value[0]) text_f(SF, 1, tx, y + 8 * S + LH + 2 * S, q->value, C_MUTED, 0xFF, W - 200 * S, 1);
        { const char *bt = q->enabled ? nt("쓸 수 있음", "Available", "利用可") : nt("지원 안 함", "Unsupported", "非対応"); uint8_t bc = q->enabled ? C_ACC2 : C_MUTED;
          uint32_t bwid = ui_sw(bt) + 24 * S, bh2 = SLH + 6 * S; ui_rframe(X + W - bwid - 14 * S, y + (h - bh2) / 2, bwid, bh2, bh2 / 2, bc, C_CARD);
          text_f(SF, 1, X + W - bwid - 2 * S, ui_sty(y + (h - bh2) / 2, bh2), bt, bc, 0xFF, bwid, 1); }
        y += h + gap;
    }
}
static void draw_item_list2(PropItem *it, int n, int selidx, uint32_t X, uint32_t W, uint32_t top, uint32_t bottom)
{
    uint32_t total = 0, upto = 0;
    for (int i = 0; i < n; i++) { if (i == selidx) upto = total + item_h_of(&it[i], i == 0); total += item_h_of(&it[i], i == 0); }
    uint32_t avail = bottom > top ? bottom - top : 0, skip = 0;
    if (total > avail && upto > avail) skip = upto - avail;
    int32_t y = (int32_t)top - (int32_t)skip;
    uint32_t vx = X + W * 46 / 100;
    int32_t py = 0; uint32_t ph = 0; int pvis = 0;      /* 바로 앞 줄 (종류 5가 같은 줄 오른쪽 반에 앉음) */
    for (int i = 0; i < n; i++) {
        PropItem *q = &it[i];
        uint32_t h = item_h_of(q, i == 0);
        if (q->kind >= 5) {
            if (pvis) {
                uint32_t hx = X + W / 2, hw = W - W / 2, hv = hx + hw * 30 / 100;
                int sl = i == selidx && item_selectable(q);
                if (item_selectable(q)) hit_add(hc_val, (intptr_t)&props_sel[props_tab], i, q->kind == 6 ? (int)hv : 0, 0x1C, hx, (uint32_t)py, hw, ph);
                if (q->kind == 6) ui_row(hx, (uint32_t)py, hw, hv, q->text, q->value, 0, UV_SLIDER, q->on * 2, sl, q->enabled);
                else ui_row(hx, (uint32_t)py, hw, hx, q->text, "", 0, UV_TOGGLE, q->on, sl, q->enabled);
            }
            continue;
        }
        pvis = 0;
        if (y < (int32_t)top || y + (int32_t)h > (int32_t)bottom) { y += (int32_t)h; continue; }
        uint32_t yy = (uint32_t)y;
        int sel = i == selidx && item_selectable(q);
        py = y; ph = h; pvis = 1;
        int pair = i + 1 < n && it[i + 1].kind >= 5;
        if (item_selectable(q))                         /* 3.0 마우스: 값 자리 왼쪽 반 ←, 오른쪽 반 →, 켜기/끄기·이름은 Enter */
            hit_add(hc_val, (intptr_t)&props_sel[props_tab], i, q->kind == 2 || q->kind == 1 ? 0 : (int)vx, 0x1C, X, yy, pair ? W / 2 : W, h);
        if (q->kind == 0) ui_section(X + 6 * S, yy + (i ? 4 * S : 0), W - 12 * S, q->text);
        else if (q->kind == 4) small_ell(X + 22 * S, yy + 4 * S, q->text, C_MUTED, 0xFF, W - 40 * S);
        else if (q->kind == 2) ui_row(X, yy, pair ? W / 2 : W, vx, q->text, "", 0, UV_TOGGLE, q->on, sel, q->enabled);
        else if (q->kind == 3) {
            ui_row(X, yy, W, vx, q->text, q->value, 0, UV_SLIDER, q->on * 2, sel, q->enabled);
            if (q->enabled && q->id != 3 && q->id != 9 && q->id != 117) { uint32_t sbw = (X + W - vx - 14 * S) * 55 / 100; hit_add(hc_slide, q->id, i, (int)sbw, 0, vx - 8 * S, yy, sbw + 16 * S, h); }   /* 5.0.31 */
        }
        else ui_row(X, yy, W, vx, q->text, q->value, 0, UV_DROP, 0, sel, q->enabled);
        y += (int32_t)h;
    }
    if (total > avail) ui_scrollbar(X + W - 2 * S, top, avail, (int)skip, (int)avail, (int)total);
}

/* 3.0 마우스: 믹서 칸. 막대를 누르면 그 높이로 음량, 이름을 누르면 끄기/켜기 (a = 채널, b = 막대 위, c = 막대 높이) */
static void mix_apply(void);
static void settings_changed(void);
static int mix_present(int c);
static void hc_mix(const UiHit *h, int ev)
{
    int c = (int)h->a;
    if (!mix_present(c)) return;
    if (mix_sel != c) { mix_sel = c; menu_dirty = 1; }
    if (ev == 0) return;
    uint32_t ty = (uint32_t)h->b, bh = (uint32_t)h->c;
    if (ev == 2) {                                      /* 3.4.4: 막대를 누른 채 끌기 */
        uint32_t py = ptr_y < ty ? ty : ptr_y >= ty + bh ? ty + bh - 1 : ptr_y;
        mix_vol[c] = (int)((ty + bh - py) * 100 / bh); mix_mute[c] = 0;
    } else if (ptr_y >= ty && ptr_y < ty + bh) { mix_vol[c] = (int)((ty + bh - ptr_y) * 100 / bh); mix_mute[c] = 0; }
    else if (ptr_y >= ty + bh) mix_mute[c] = !mix_mute[c];
    mix_apply(); settings_changed(); menu_dirty = 1;
}
/* 3.6.5: 믹서 — 모든 채널을 콘솔처럼 (없는 음원도 흐리게 자리), 아이콘·값 알약·눈금 페이더·LED 계기·좌우·켜기 단추 */
/* 3.7.0: 채널 차례 = 음원 표 차례 (MIX_ 번호와 같음). 지금 동작하는 채널만 보임 */
static int mix_visible(int *out)                        /* 보이는 채널, 무리 경계(키 클릭 앞·마스터 앞)는 out[MIX_N], out[MIX_N+1] */
{
    int n = 0;
    out[n++] = MIX_PSG;                                 /* 고정 PSG 가 맨 앞 */
    for (int c = 0; c < MIX_MASTER; c++) if (snd_is_src(c) && c != MIX_PSG && mix_shown(c)) out[n++] = c;   /* 칩 채널 (쓰인 것만, 본체 표 차례 + 내장·USB MIDI) */
    out[MIX_N] = n;
    for (int c = MIX_KBD; c <= MIX_IO; c++) out[n++] = c;   /* 고정 KEY·I-O */
    out[MIX_N + 1] = n;
    out[n++] = MIX_MASTER;
    return n;
}
static void mix_step(int dir)                           /* ←/→: 화면 차례대로, 없는 채널은 건너뜀 */
{
    int v[MIX_N + 2], n = mix_visible(v), k = 0;
    for (int i = 0; i < n; i++) if (v[i] == mix_sel) k = i;
    k = (k + dir + n) % n;
    mix_sel = v[k];
}
static int mix_popup_fit;
static const char *mix_short(int c)                     /* 짧은 이름 (띠가 좁을 때) */
{
    if (c >= 0 && c < MSX_CH_N) return msx_ch_info[c].short_name;   /* 본체 채널 표 */
    return c == MIX_MIDI_INT ? "GTMIDI" : c == MIX_MASTER ? "MASTER" : "";
}                               /* 4.0: 믹서 팝업 = 보이는 채널만큼 칸을 나눔 */
static int mix_hid_l, mix_hid_r;                        /* 좌우로 넘겨 안 보이는 칩 띠 수, 칩 구역 x 범위 */
static uint32_t mix_hx1, mix_hx2;
static void mix_desc(uint32_t x, uint32_t y, uint32_t w)   /* 고른 띠의 전체 이름 (띠에는 줄인 이름) */
{
    char hb[16];
    if (mix_hid_l) { copy_str(hb, sizeof hb, "< "); num_append(hb, sizeof hb, (uint32_t)mix_hid_l, 0, ' '); small_ell(mix_hx1, ui_sty(y, SLH), hb, C_WARN, 0xFF, 60 * S); }
    if (mix_hid_r) { hb[0] = 0; num_append(hb, sizeof hb, (uint32_t)mix_hid_r, 0, ' '); str_append(hb, sizeof hb, " >"); uint32_t tw = ui_sw(hb); small_ell(mix_hx2 > tw + 4 * S ? mix_hx2 - tw - 4 * S : mix_hx2, ui_sty(y, SLH), hb, C_WARN, 0xFF, 60 * S); }
    const char *t = mix_sel == MIX_KBD ? msx_ch_info[MSX_CH_KEY].full : mix_sel == MIX_IO ? T(S_SND_DRIVE) : snd_full(mix_sel);
    if ((mix_hid_l || mix_hid_r) && mix_hx2 + 10 * S < x + w) { w = x + w - mix_hx2 - 10 * S; x = mix_hx2 + 10 * S; }   /* 넘기는 중: 설명은 칩 구역 오른쪽 (겹치지 않게) */
    uint32_t tw = ui_sw(t);
    small_ell(x + (w > tw ? (w - tw) / 2 : 0), ui_sty(y, SLH), t, C_MUTED, 0xFF, w);
}
static void draw_mixer(uint32_t X, uint32_t Y, uint32_t W, uint32_t H)
{
    uint32_t gap = 14 * S;                              /* 무리 사이 (음원 | 키 클릭·드라이브 | 마스터) */
    int vis[MIX_N + 2], nv = mix_visible(vis), g1 = vis[MIX_N], g2b = vis[MIX_N + 1];
    if (!mix_shown(mix_sel)) mix_sel = vis[0];
    /* 띠가 폭보다 많으면 칩 채널 구역만 좌우로 넘김 (PSG·KBD·I-O·MASTER 는 늘 자리). 고른 띠가 보이게 따라감 */
    static int mix_off;
    int hid_l = 0, hid_r = 0;
    {
        uint32_t minw = 64 * S;
        int maxn = (int)((W > 2 * gap ? W - 2 * gap : W) / minw), nc = g1 - 1, nf = nv - nc;
        if (nv > maxn && nc > 1) {
            int K = maxn - nf; if (K < 1) K = 1;
            int si = -1; for (int i = 1; i < g1; i++) if (vis[i] == mix_sel) si = i - 1;
            if (si >= 0) { if (si < mix_off) mix_off = si; if (si >= mix_off + K) mix_off = si - K + 1; }
            if (mix_off > nc - K) mix_off = nc - K;
            if (mix_off < 0) mix_off = 0;
            int w2[MIX_N + 2], n2 = 0;
            w2[n2++] = vis[0];
            for (int i = 0; i < K; i++) w2[n2++] = vis[1 + mix_off + i];
            for (int i = g1; i < nv; i++) w2[n2++] = vis[i];
            hid_l = mix_off; hid_r = nc - mix_off - K;
            g2b = g2b - nc + K; g1 = 1 + K; nv = n2;
            for (int i = 0; i < n2; i++) vis[i] = w2[i];
        } else mix_off = 0;
    }
    uint32_t cw = (W - 2 * gap) / (uint32_t)(mix_popup_fit ? nv : MIX_N);      /* 3.7.1: 한 칸 = 모든 채널(음원·MIDI·키 클릭·드라이브·기타·MASTER)이 다 들어가는 폭, 켜진 것만 가운데로 */
    if (cw > 120 * S) cw = 120 * S;
    if (cw < 52 * S && (W - 2 * gap) / (uint32_t)nv > cw) { cw = (W - 2 * gap) / (uint32_t)nv; if (cw > 52 * S) cw = 52 * S; }   /* 아주 좁은 화면 */
    uint32_t x0 = X + (W - cw * (uint32_t)nv - 2 * gap) / 2;
    uint32_t isz = LH + LH / 2;
    if (isz > cw - 28 * S) isz = cw > 28 * S + 8 ? cw - 28 * S : 8;
    uint32_t pill_h = LH + 6 * S, btn_h = LH + 8 * S, pan_h = snd_stereo ? SLH + 14 * S : 0;
    uint32_t top = Y + 12 * S + isz + 6 * S + SLH + 10 * S + pill_h + 14 * S;  /* 페이더 위  */
    uint32_t bot = Y + H - 12 * S - btn_h - 10 * S - pan_h;                          /* 페이더 아래 */
    uint32_t bar_h = bot > top + 40 * S ? bot - top : 40 * S;
    for (int k = 0; k < nv; k++) {
        int c = vis[k], pres = 1, sel = c == mix_sel, mute = mix_mute[c];
        uint32_t cx = x0 + (uint32_t)k * cw + (k >= g1 ? gap : 0) + (k >= g2b ? gap : 0);
        uint32_t sx = cx + 4 * S, sw = cw - 8 * S;
        if ((k == g1 && g1 > 0) || (k == g2b && g2b > g1)) ui_rrect(cx - gap / 2 - S, Y + 20 * S, 2 * S, H - 40 * S, S, C_LINE);   /* 무리 나눔 */
        /* 띠 */
        ui_rrect(sx + 3 * S, Y + 4 * S, sw, H, 12 * S, C_SHADOW);
        ui_rframe(sx, Y, sw, H, 12 * S, sel ? C_ACC : C_LINE, !pres ? C_HEAD : sel ? C_ROWSEL : c == MIX_MASTER ? C_HEAD : C_CARD);
        if (sel) ui_rrect(sx + sw / 4, Y + 3 * S, sw / 2, 3 * S, S, C_ACC);
        /* 아이콘 + 이름 */
        uint32_t iy = Y + 12 * S;
        int on = pres && !mute && mix_meter[c] > 30;
        draw_icon_hd(cx + (cw - isz) / 2, iy, isz, mix_icon(c), !pres ? C_FAINT : on ? C_WARN : C_TEXT, !pres ? C_FAINT : on ? C_WARN : c == MIX_MASTER ? C_WARN : C_MUTED, !pres ? C_HEAD : sel ? C_ROWSEL : c == MIX_MASTER ? C_HEAD : C_CARD);
        /* 3.7.2: 이름 — 칸보다 길면 작은 글꼴로 (한 줄) */
        /* 띠 폭에 맞게 — 긴 이름 → 짧은 이름 → 작은 글꼴 → 가로로 줄임 (전체 이름은 고르면 아래 설명 줄) */
        const char *nm = snd_name(c);   /* 띠 이름 = 본체 채널 표 (KEY·I-O 는 마스터님 결정 이름) */
        uint32_t lim = sw > 12 * S ? sw - 12 * S : sw;   /* 띠 안쪽 */
        const UiFont *nf = SF;
        uint32_t nw = text_f(nf, 1, 0, 0, nm, 0, 0, 0, 0);
        if (nw > lim) { nm = mix_short(c); nw = text_f(nf, 1, 0, 0, nm, 0, 0, 0, 0); }
        if (nw > lim && SF != &font_small) { nf = &font_small; nw = text_f(nf, 1, 0, 0, nm, 0, 0, 0, 0); }
        if (nw > lim) nw = lim;                         /* 그래도 길면 가로로 줄임 */
        uint32_t ny = iy + isz + 6 * S;
        uint8_t ncol = !pres ? C_FAINT : sel ? C_TEXT : C_MUTED;
        text_fit(nf, cx + (cw > nw ? (cw - nw) / 2 : 0), ny + (SLH - (uint32_t)nf->height) / 2, nm, ncol, lim);
        /* 값 알약 */
        uint32_t py = ny + SLH + 10 * S, pw = sw - 16 * S;
        char v[12] = "";
        if (!pres) copy_str(v, sizeof v, "-");
        else if (mute) copy_str(v, sizeof v, "MUTE");
        else num_append(v, sizeof v, (uint32_t)mix_vol[c], 0, ' ');
        ui_rframe(sx + 8 * S, py, pw, pill_h, pill_h / 2, !pres ? C_LINE : mute ? C_BAD : sel ? C_ACC : C_LINE, C_FIELD);
        uint32_t vw = mute ? ui_sw(v) : text_w(v);
        if (mute) text_f(SF, 1, sx + 8 * S + (pw > vw ? (pw - vw) / 2 : 0), ui_sty(py, pill_h), v, C_BAD, 0xFF, 0, 1);
        else text_f(F, S, sx + 8 * S + (pw > vw ? (pw - vw) / 2 : 0), ui_ty(py, pill_h), v, !pres ? C_FAINT : C_TEXT, 0xFF, 0, 1);
        /* 페이더: 눈금 + 홈 + 채움 + 손잡이, 오른쪽에 LED 계기 */
        uint32_t tw = 8 * S, tx = cx + cw / 2 - tw - 4 * S, ty = top;
        hit_add(hc_mix, c, (int)ty, (int)bar_h, 0, cx, Y, cw, H);
        for (int t = 0; t <= 4; t++) {
            uint32_t yy = ty + bar_h - bar_h * (uint32_t)t / 4;
            ui_rrect(tx - 12 * S, yy > ty + S ? yy - S : ty, (t == 0 || t == 4 || t == 2) ? 8 * S : 5 * S, 2 * S, S, C_LINE);
        }
        ui_rrect(tx, ty, tw, bar_h, tw / 2, !pres ? C_HEAD : C_FIELD);
        if (pres) {
            uint32_t fh = bar_h * (uint32_t)mix_vol[c] / 100;
            if (fh) ui_rrect(tx, ty + bar_h - fh, tw, fh, tw / 2, mute ? C_FAINT : c == MIX_MASTER ? C_WARN : C_ACC);
            /* LED 계기: 아래부터 초록 → 노랑 → 빨강 */
            int segs = (int)(bar_h / (6 * S)); if (segs > 24) segs = 24; if (segs < 6) segs = 6;
            uint32_t sh = bar_h / (uint32_t)segs, mxx = tx + tw + 8 * S;
            int lit = mute ? 0 : mix_meter[c] * segs / 1000;
            for (int g2 = 0; g2 < segs; g2++) {
                uint32_t yy = ty + bar_h - (uint32_t)(g2 + 1) * sh;
                uint8_t col = g2 >= lit ? C_FIELD : g2 >= segs * 85 / 100 ? C_BAD : g2 >= segs * 65 / 100 ? C_WARN : C_OK;
                ui_rrect(mxx, yy + S, 7 * S, sh > 2 * S ? sh - 2 * S : 1, S, col);
            }
            uint32_t kh = 14 * S, kw = cw * 3 / 10, ky = ty + bar_h - fh;
            ky = ky > ty + kh / 2 ? ky - kh / 2 : ty;
            uint32_t kx = tx + tw / 2 - kw / 2;
            ui_rrect(kx + 2 * S, ky + 3 * S, kw, kh, 4 * S, C_SHADOW);
            ui_rrect(kx, ky, kw, kh, 4 * S, sel ? 15 : C_TEXT);
            ui_rrect(kx + 4 * S, ky + kh / 2 - S, kw - 8 * S, 2 * S, S, mute ? C_FAINT : c == MIX_MASTER ? C_WARN : C_ACC);   /* 손잡이 가운데 줄 */
        }
        /* 좌우 (스테레오) */
        uint32_t by = Y + H - 12 * S - btn_h;
        if (snd_stereo) {
            uint32_t pyy = by - 10 * S - pan_h, pxw = sw - 24 * S, pxx = sx + 12 * S;
            if (pres && mix_has_pan(c)) {
                ui_rrect(pxx, pyy + SLH / 2, pxw, 4 * S, 2 * S, C_FIELD);
                ui_rrect(pxx + pxw / 2 - S, pyy + SLH / 2 - 3 * S, 2 * S, 10 * S, S, C_LINE);
                uint32_t dx = pxx + (uint32_t)((int)pxw / 2 + mix_pan[c] * (int)pxw / 200);
                ui_rrect(dx > 5 * S ? dx - 5 * S : dx, pyy + SLH / 2 - 3 * S, 10 * S, 10 * S, 5 * S, mix_pan[c] ? C_WARN : C_MUTED);
                char pn[8] = "";
                if (!mix_pan[c]) copy_str(pn, sizeof pn, "C");
                else { copy_str(pn, sizeof pn, mix_pan[c] < 0 ? "L" : "R"); num_append(pn, sizeof pn, (uint32_t)(mix_pan[c] < 0 ? -mix_pan[c] : mix_pan[c]), 0, ' '); }
                uint32_t pnw = ui_sw(pn);
                text_f(SF, 1, cx + (cw > pnw ? (cw - pnw) / 2 : 0), pyy + SLH / 2 + 8 * S, pn, mix_pan[c] ? C_WARN : C_FAINT, 0xFF, 0, 1);
            }
        }
        /* 켜기 단추 */
        const char *bt = !pres ? "" : mute ? "OFF" : "ON";
        ui_rframe(sx + 8 * S, by, sw - 16 * S, btn_h, 7 * S, !pres ? C_LINE : mute ? C_BAD : C_OK, !pres ? C_HEAD : C_FIELD);
        if (pres) {                                     /* ● ON / ● OFF (작은 글) */
            uint32_t bw = ui_sw(bt), dd = 8 * S, tot = dd + 6 * S + bw, bx = sx + 8 * S + (sw - 16 * S > tot ? (sw - 16 * S - tot) / 2 : 0);
            ui_rrect(bx, by + (btn_h - dd) / 2, dd, dd, dd / 2, mute ? C_BAD : C_OK);
            text_f(SF, 1, bx + dd + 6 * S, ui_sty(by, btn_h), bt, mute ? C_BAD : C_OK, 0xFF, 0, 1);
        }
    }
    mix_hid_l = hid_l; mix_hid_r = hid_r;               /* 넘긴 칩 띠 수 → 설명 줄 양 끝 (mix_desc) */
    mix_hx1 = x0 + cw; mix_hx2 = x0 + cw * (uint32_t)g1;
}

/* 슬롯 배치 표 (요구 8, 2.7.6 개정): 기본 슬롯 0~3, 나뉜 슬롯은 서브슬롯 4칸.
 * 칸마다 BIOS·RAM·ROM·DISK·SLOTn(카트리지 자리), 무엇이 있으면 회색 바탕, 모르는 장치는 ?.
 * 확장기 안 칸도 카트리지가 꽂히면 바로(실시간) 회색 */
/* 반환: 0 비어 있음, 1 무엇이 있음, 2 카트리지 자리(비어 있음), 3 카트리지가 꽂힘, 4 모르는 장치 */
/* 경로에서 파일 이름만 */
static const char *path_base(const char *p)
{
    const char *b = p;
    for (const char *q = p; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}
/* 2.9: 빈 곳은 "Blank", ROM은 config.ini의 ROM 파일 이름 그대로, 카트리지는 꽂은 파일 이름. cart_n = 카트리지 자리 번호(없으면 -1) */
static int slot_label(int ps, int ss, char *out, int max, int *cart_n)
{
    out[0] = 0;
    *cart_n = -1;
    for (int n = 0; n < MSX_CART_SLOTS; n++)
        if (M->cart_slot[n] == ps && (!M->prof.subslotted[ps] || M->cart_ss[n] == ss)) {
            *cart_n = n;
            copy_str(out, max, M->cart[n].present && cart_name[n][0] ? cart_name[n] : M->cart[n].present ? "ROM" : "Blank");
            return M->cart[n].present ? 3 : 2;
        }
    if (M->mram_size && M->mapper_ps == ps && (!M->prof.subslotted[ps] || M->mapper_ss == ss)) { copy_str(out, max, "RAM"); return 1; }
    if (M->xram_size && M->xram_ps == ps && (!M->prof.subslotted[ps] || M->xram_ss == ss)) { copy_str(out, max, "RAM"); return 1; }
    int k = 0;
    for (int i = 0; i < M->prof.region_count; i++) {
        const ProfileRegion *r = &M->prof.region[i];
        if (r->slot != ps || (M->prof.subslotted[ps] && r->subslot != ss)) continue;
        if (r->type == REGION_RAM || r->type == REGION_RAM_MAPPER) { if (!k) { copy_str(out, max, "RAM"); return 1; } continue; }
        const char *b = r->file[0] ? path_base(r->file) : (r->type == REGION_DISK_ROM ? "DISK" : r->type == REGION_PAC ? "PAC (SRAM)" : "ROM");
        if (k && strstr_ci(out, b)) continue;               /* 같은 파일이 여러 구역이면 한 번만 */
        if (k) str_append(out, max, " + ");
        str_append(out, max, b);
        k = 1;
    }
    if (k) return 1;
    if (M->prof.unknown_at[ps][M->prof.subslotted[ps] ? ss : 0]) { copy_str(out, max, "?"); return 4; }
    copy_str(out, max, "Blank");
    return 0;
}

/* 표가 바뀌었는지 (대화 창이 열린 동안 실시간 갱신) */
static uint32_t slot_map_sig(void)
{
    uint32_t h = (uint32_t)M->expand * 7u + (uint32_t)M->mram_size;
    for (int n = 0; n < MSX_CART_SLOTS; n++) h = h * 31u + (uint32_t)(M->cart[n].present ? 1 + M->cart[n].mapper : 0) * 5u + (uint32_t)(M->cart_slot[n] + 1) * 3u + (uint32_t)M->cart_ss[n];
    for (int ps = 0; ps < 4; ps++) h = h * 3u + (uint32_t)M->prof.subslotted[ps];
    return h;
}

static void draw_slot_map(uint32_t x, uint32_t y, uint32_t w)
{
    uint32_t rh = SLH + 10 * S;
    /* 칸 너비: 나뉜 슬롯은 서브슬롯마다 1, 안 나뉜 슬롯은 2 (확장기 칸에 "SLOTn"이 잘리지 않게) */
    uint32_t units = 0;
    for (int ps = 0; ps < 4; ps++) units += M->prof.subslotted[ps] ? 4 : 2;
    uint32_t unit = w / units, px = x;
    for (int ps = 0; ps < 4; ps++) {
        int sub = M->prof.subslotted[ps];
        int nc = sub ? 4 : 1;
        uint32_t sw = sub ? unit : 2 * unit;
        for (int ss = 0; ss < nc; ss++) {
            uint32_t cx = px + (uint32_t)ss * sw;
            char head[16], lab[160];
            head[0] = (char)('0' + ps); head[1] = 0;
            if (sub) { head[1] = '-'; head[2] = (char)('0' + ss); head[3] = 0; }
            int cn;
            int k = slot_label(ps, ss, lab, sizeof lab, &cn);
            if (cn >= 0) { head[sub ? 3 : 1] = 0; str_append(head, sizeof head, " SLOT"); char d[2] = { (char)('1' + cn), 0 }; str_append(head, sizeof head, d); }
            uint8_t bg = k == 3 ? C_ACC2 : k == 2 ? C_FIELD : k == 1 ? C_ROWSEL : C_HEAD;
            uint8_t fg = k == 3 ? 15 : k == 2 ? C_WARN : k == 4 ? C_BAD : k == 1 ? C_TEXT : C_FAINT;
            ui_rrect(cx + 2 * S, y, sw - 4 * S, 2 * rh, 5 * S, bg);
            if (k == 2) ui_rframe(cx + 2 * S, y, sw - 4 * S, 2 * rh, 5 * S, C_WARN, C_FIELD);
            small_ell(cx + 8 * S, ui_sty(y, rh) + 2 * S, head, k == 3 ? 15 : C_MUTED, 0xFF, sw - 14 * S);
            small_ell(cx + 8 * S, ui_sty(y + rh, rh) - 2 * S, lab, fg, 0xFF, sw - 14 * S);
        }
        px += (uint32_t)nc * sw;
    }
}

/* 3.7.0: 믹서 = 화면 아래 절반의 팝업 (MSX는 계속 돌고 위쪽에 보임) */
static UiRect mixpop_rect(void)
{
    uint32_t m = 10 * S, w = out_w > 2 * m ? out_w - 2 * m : out_w, h = D.h / 2;
    {                                                   /* 4.0: 가로 = 보이는 채널 수에 맞춤 (칸 104, 무리 사이 14, 안쪽 여백) */
        int vis_[MIX_N + 2], nv_ = mix_visible(vis_);
        uint32_t want = (uint32_t)nv_ * 104 * S + 2 * 14 * S + 2 * 14 * S + 8 * S;
        if (want < 520 * S) want = 520 * S;             /* 명령 줄 글이 들어가는 폭 */
        if (want < w) w = want;
    }
    uint32_t minh = ui_head_h() + 12 * S + LH * 3 + 14 * S + LH + 6 * S + LH + 8 * S + 160 * S + ui_cmdbar_h();   /* 아이콘·이름·알약·페이더·단추·명령 줄 */
    if (h < minh) h = minh;
    if (h > D.h - 2 * m) h = D.h - 2 * m;
    UiRect o = { x0 + m + (out_w > 2 * m ? (out_w - 2 * m - w) / 2 : 0), D.h - h - m, w, h };
    return o;
}
static void draw_mixpop(void)
{
    UiRect o = mixpop_rect();
    char sub[96] = "";
    copy_str(sub, sizeof sub, machine_name);
    UiRect b = ui_card(o.x, o.y, o.w, o.h, T(tab_label[6]), sub, 0);
    uint32_t pad = 14 * S, cy = o.y + o.h - ui_cmdbar_h() - 2 * S;
    hit_add(hc_none, 0, 0, 0, 0, o.x, o.y, o.w, o.h);  /* 팝업 밑(MSX 화면)을 누르지 않게 */
    mix_popup_fit = 1;
    uint32_t dy = cy - 6 * S - SLH;                     /* 설명 줄 = 고른 띠의 전체 이름 */
    draw_mixer(b.x + pad, b.y + 6 * S, b.w - 2 * pad, dy - 6 * S - (b.y + 6 * S));
    mix_popup_fit = 0;
    mix_desc(b.x + pad, dy, b.w - 2 * pad);
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, T(S_MIXER_KEYS));
}

/* 4.0: 입력장치 탭 순서 = 키보드(6), 마우스(8), 조이스틱(7) */
static int input_idx(int tab) { return tab == 7 ? 0 : tab == 9 ? 1 : 2; }
static int input_tab(int idx) { static const int t[3] = { 7, 9, 8 }; return t[(idx % 3 + 3) % 3]; }
/* 4.0.34: 드롭다운 — 화면 출력·모니터 에뮬레이션·SoundFont 파일에서 Enter를 치면 목록이 펼쳐짐 (↑/↓ 고르기, Enter 정하기, Esc 닫기) */
#define DD_MAX 64
static int dd_on, dd_id, dd_n, dd_sel, dd_top;
static char dd_sub[64][96]; static uint8_t dd_two;       /* 두 줄 항목 (이름 + 작은 파일 이름 줄) — MIDI 탭 롬 목록 (dd_id 5103) 만 */
static char dd_txt[DD_MAX][64], dd_key[DD_MAX][FE_KEY_MAX];
static uint8_t dd_hd[DD_MAX]; static int dd_val[DD_MAX], rt_it;   /* 제목 줄(고를 수 없음), 항목 값, 고르는 파일 */
#define DD_HDR(i) (dd_id == 3000 && dd_hd[i])
static void dd_open(int id)
{
    dd_n = 0; dd_sel = 0; dd_top = 0; dd_id = id;
    if (id == 59 && P.video_outputs) {
        if (P.video_rescan) P.video_rescan();
        char ln[8][64]; int act = 0, n = P.video_outputs(ln, 8, &act); vo_pretty(ln, n);
        for (int i = 0; i < n; i++) copy_str(dd_txt[i], sizeof dd_txt[i], ln[i]);
        dd_n = n; dd_sel = video_out >= 0 && video_out < n ? video_out : 0;
    } else if (id == 41 && D.bpp != 1) {
        static const StrId mn[6] = { S_MEMU_NONE, S_MEMU_MON, S_MEMU_YC, S_MEMU_YCN, S_MEMU_COMP, S_MEMU_COMPN };
        for (int i = 0; i < MON_EMU_N; i++) copy_str(dd_txt[i], sizeof dd_txt[i], i == 6 ? "Scale 2x" : i == 7 ? "Hq2x" : T(mn[i % 6]));
        dd_n = MON_EMU_N; dd_sel = mon_emu >= 0 && mon_emu < MON_EMU_N ? mon_emu : 0;
    } else if (id == 43 && P.audio_select) {
        int hd = P.hdmi_active && P.hdmi_active();
        dd_n = hd ? 3 : 2;                                  /* 4.0.93: HDMI 화면이 아니면 HDMI 출력 항목은 목록에 없음 */
        for (int i = 0; i < dd_n; i++) copy_str(dd_txt[i], sizeof dd_txt[i], T(snd_out_str(snd_out_order[i])));
        dd_sel = snd_out_pos(audio_out); if (dd_sel >= dd_n) dd_sel = 0;
    } else if (id == 115) {
        static FeDirEntry se[DD_MAX];
        int n = P.list_dir ? P.list_dir(midi_dir(MD_SF), se, DD_MAX) : 0;
        for (int i = 0; i < n && dd_n < DD_MAX; i++) {
            const char *nm = se[i].key[0] ? se[i].key : se[i].name; int l = (int)strlen(nm);
            if (se[i].is_dir || l <= 4 || nm[l - 4] != '.' || (nm[l - 3] | 32) != 's' || (nm[l - 2] | 32) != 'f' || nm[l - 1] != '2') continue;
            copy_str(dd_key[dd_n], sizeof dd_key[dd_n], nm); copy_str(dd_txt[dd_n], sizeof dd_txt[dd_n], se[i].name[0] ? se[i].name : nm);
            if (!strcmp(nm, sf_name)) dd_sel = dd_n;
            dd_n++;
        }
    }
    dd_on = dd_n > 0;
    if (id == 59 && dd_n <= 1) { dd_on = 0; toast(T(S_VID_NONE_T), 0); }
}
static int dd_cur;
static void rt_dd_open(void)                            /* ROM 이미지 선택 창에서 R — ROMtype(매퍼) 풀다운. 종류별로 묶어서 */
{
    int it = list[menu_sel];
    probe_type(it);
    static const struct { const char *ko, *en, *ja; } cat[5] = {
        { "메모리 매퍼 (게임 카트리지)", "Memory mappers (game cartridges)", "メモリマッパー (ゲームカートリッジ)" },
        { "일반 ROM", "Plain ROM", "通常ROM" }, { "RAM", "RAM", "RAM" }, { "사운드 음원", "Sound", "サウンド音源" },
        { "디스크·저장 장치", "Disk / storage", "ディスク・記憶装置" } };
    static const uint8_t grp[5][27] = {
        { MAPPER_ASCII8, MAPPER_ASCII16, MAPPER_ASCII8_SRAM, MAPPER_ASCII16_SRAM, MAPPER_ASCII16X, MAPPER_KONAMI, MAPPER_KONAMI_SCC, MAPPER_SCCPLUS, MAPPER_ESESCC,
          MAPPER_MFRSCC, MAPPER_MFRSCCP, MAPPER_FRSCC, MAPPER_YAMANOOTO, MAPPER_RTYPE, MAPPER_HARRYFOX, MAPPER_CROSSBLAIM, MAPPER_LODERUNNER, MAPPER_NETTOU, MAPPER_MATRAINK, MAPPER_QURAN,
          MAPPER_ZEMINA80, MAPPER_ZEMINA90, MAPPER_ZEMINA126, MAPPER_GAMEMASTER2, MAPPER_NEO8, MAPPER_NEO16, 255 },
        { MAPPER_PLAIN, 255 }, { MAPPER_MEGARAM, MAPPER_RAM, MAPPER_ESERAM, 255 }, { MAPPER_FMPAC, MAPPER_PAC, MAPPER_FSCA1, MAPPER_SFG, 255 },
        { MAPPER_DOS2, MAPPER_SUNRISE, MAPPER_BEERIDE, MAPPER_GIDE, MAPPER_GOUDA, MAPPER_MEGASCSI, MAPPER_WAVESCSI, MAPPER_VHD, MAPPER_NET, 255 } };
    dd_n = 0; dd_sel = 0; dd_top = 0; rt_it = it;
    for (int c = 0; c < 5; c++) {
        dd_hd[dd_n] = 1; dd_val[dd_n] = -1;
        copy_str(dd_txt[dd_n], sizeof dd_txt[dd_n], "- "); str_append(dd_txt[dd_n], sizeof dd_txt[dd_n], lang == 0 ? cat[c].ko : lang == 2 ? cat[c].ja : cat[c].en);
        dd_n++;
        for (int k = 0; grp[c][k] != 255 && dd_n < DD_MAX; k++) {
            dd_hd[dd_n] = 0; dd_val[dd_n] = grp[c][k];
            copy_str(dd_txt[dd_n], sizeof dd_txt[dd_n], grp[c][k] == MAPPER_PLAIN ? "ROM" : cart_mapper_name((MapperType)grp[c][k]));
            if (grp[c][k] == (int)entries[it].type) dd_sel = dd_n;
            dd_n++;
        }
    }
    dd_id = 3000; dd_on = 1;
    if (dd_sel == 0) dd_sel = 1;
    dd_top = dd_sel > 5 ? dd_sel - 5 : 0;
    if (dd_sel > 0 && dd_hd[dd_sel - 1] && dd_top > dd_sel - 1) dd_top = dd_sel - 1;
}
static int dd_gen(int id)                               /* ‹ › 로 고르던 값은 모두 풀다운. 목록을 dd_txt에 채움 (값 순서 = 한 칸씩 넘기는 순서) */
{
    int n = 0, cur = 0;
    const FeSel *fs = sel_find(id);                     /* 선택형 설정 표 (fe_sel.inc) */
    dd_n = 0;
    if (fs) { n = sel_n(fs); cur = sel_cur(fs); for (int i = 0; i < n; i++) copy_str(dd_txt[i], sizeof dd_txt[i], sel_label(fs, i)); }
    else return 0;
    dd_n = n; dd_sel = cur; dd_cur = cur; dd_top = 0; dd_id = id; dd_on = 1;
    return 1;
}
static void img_dd_apply(int i);
static void dg_pc_apply(int i);
static void dd_apply(void)
{
    int i = dd_sel;
    if (i < 0 || i >= dd_n) return;
    if (dd_id == 5100 || dd_id == 5101 || dd_id == 5103 || dd_id == 5104) { mt_dd_apply(i); return; }   /* 5.2.54 MIDI 탭 */
    if (dd_id == 4000) { dg_pc_apply(i); return; }   /* 5.2.26 진단 시험 PC */
    if (dd_id == 3001) { vb_dd_apply(i); return; }   /* 5.0.38 가상 하드 형식 */
    if (dd_id == 3000) { if (!dd_hd[i] && rt_it >= 0) { entries[rt_it].type = (MapperType)dd_val[i]; entries[rt_it].user_type = 1; } return; }   /* 5.0.33 */
    if (dd_id >= 2000) { img_dd_apply(i); return; }
    { const FeSel *fs = sel_find(dd_id); if (fs) { if (i != sel_cur(fs)) sel_set(fs, i); return; } }   /* 선택형 설정 표 — 고른 값으로 바로 */
    switch (dd_id) {
    case 59: { int vs_r = -1; video_out = i; settings_changed();
        if (P.video_switch) vs_r = P.video_switch(i);
        if (P.video_switch && !vs_r) { toast(T(S_VID_SWITCHED), 0); vid_snd_follow(i); }
        else toast(vs_r == -2 ? (lang == 0 ? "화면 출력을 바꾸지 못했습니다 (LOG\\VIDSW.TXT)" : "Could not switch video output (LOG\\VIDSW.TXT)") : T(S_VID_OUT_NOTE), 0); }
        rep_key = -1; break;
    case 41: mon_emu = i; full_redraw(); settings_changed(); break;
    case 43:                                            /* 4.0.66: 소리 출력 풀다운 */
        audio_out = snd_out_order[i & 3];                    /* 4.0.93: 목록 위치 → 값 */
        if (P.audio_select && P.audio_select(audio_out, snd_status, sizeof snd_status)) { audio_out = 0; P.audio_select(0, snd_status, sizeof snd_status); toast(T(S_AUDIO_FALLBACK), 0); }
        settings_changed(); rep_key = -1; break;
    case 115: copy_str(sf_name, sizeof sf_name, dd_key[i]); sf_state = 0; sf_load(0); settings_changed(); break;
    }
}
static int snd_auto_hdmi;                 /* 4.0.128: 화면 출력 선택이 소리를 HDMI로 바꿔 둔 상태 (화면을 다른 걸로 바꾸면 자동으로 되돌림) */
static void vid_snd_follow(int i)         /* 화면 출력 3번(HDMI만)이면 소리 출력도 HDMI로. 2번(복사)은 소리를 그대로 둠 */
{
    if (!P.audio_select || !P.video_outputs) return;
    char ln[8][64]; int act = 0, vn = P.video_outputs(ln, 8, &act), only = 0;
    if (i >= 0 && i < vn) for (const char *t = ln[i]; *t; t++) if (t[0] == 'o' && t[1] == 'n' && t[2] == 'l' && t[3] == 'y' && t[4] == ':') { only = 1; break; }
    if (only) { snd_auto_hdmi = 1; audio_out = 2; P.audio_select(0, snd_status, sizeof snd_status); menu_dirty = 1; }   /* 실제 소리는 그래픽 소리가 켜지는 몇 초 뒤 플랫폼이 켬 (못 켜면 video_lost 2로 내장 소리 복귀) */
    else if (snd_auto_hdmi) { snd_auto_hdmi = 0; audio_out = 0; P.audio_select(0, snd_status, sizeof snd_status); menu_dirty = 1; }
}
static void dd_key_event(int key)
{
    if (key == 0x01) dd_on = 0;
    else if (key == 0xC8) { int s = dd_sel - 1; while (s >= 0 && DD_HDR(s)) s--; if (s >= 0) dd_sel = s; }
    else if (key == 0xD0) { int s = dd_sel + 1; while (s < dd_n && DD_HDR(s)) s++; if (s < dd_n) dd_sel = s; }
    else if (key == 0xC9) { int s = dd_sel - 8; if (s < 0) s = 0; while (s < dd_n && DD_HDR(s)) s++; if (s < dd_n) dd_sel = s; }
    else if (key == 0xD1) { int s = dd_sel + 8; if (s > dd_n - 1) s = dd_n - 1; while (s >= 0 && DD_HDR(s)) s--; if (s >= 0) dd_sel = s; }
    else if (key == 0xC7) { int s = 0; while (s < dd_n && DD_HDR(s)) s++; if (s < dd_n) dd_sel = s; }       /* 5.2.26 Home/End */
    else if (key == 0xCF) { int s = dd_n - 1; while (s >= 0 && DD_HDR(s)) s--; if (s >= 0) dd_sel = s; }
    else if (key == 0x1C || key == 0x9C || key == 0x39) { dd_apply(); dd_on = 0; }
    if (!dd_on && dd_id >= 3000) full_redraw();         /* 닫히면 목록을 다시 그림 */
    int vm = dd_id == 5103 && dd_two ? 6 : 10, vis = dd_n < vm ? dd_n : vm;
    if (dd_sel > 0 && DD_HDR(dd_sel - 1) && dd_top > dd_sel - 1) dd_top = dd_sel - 1;
    if (dd_sel < dd_top) dd_top = dd_sel;
    if (dd_sel >= dd_top + vis) dd_top = dd_sel - vis + 1;
}
static void hc_ddsel(const UiHit *h, int ev)           /* 풀다운 항목 — 가리키면 고름 표시, 누르면 적용하고 닫기 */
{
    if (!dd_on || DD_HDR(h->b)) return;
    dd_sel = h->b; menu_dirty = 1;
    if (ev == 1) { dd_apply(); dd_on = 0; if (dd_id >= 3000) full_redraw(); }
}
static void hc_ddclose(const UiHit *h, int ev) { (void)h; if (ev == 1) { dd_on = 0; menu_dirty = 1; if (dd_id >= 3000) full_redraw(); } }   /* 풀다운 밖을 누르면 닫기 */
static void dd_draw_at(uint32_t x, uint32_t y0, uint32_t w, uint32_t bottom);
static void dd_draw(uint32_t x, uint32_t top, uint32_t bottom, int row, uint32_t w) { dd_draw_at(x, top + (uint32_t)(row + 1) * list_rh(), w, bottom); }
static void dd_draw_at(uint32_t x, uint32_t y0, uint32_t w, uint32_t bottom)
{
    int two = dd_id == 5103 && dd_two;                  /* 두 줄 항목 */
    uint32_t rh = list_rh() + (two ? SLH + 8 * S : 0);
    int vm = two ? 6 : 10, vis = dd_n < vm ? dd_n : vm;
    uint32_t h = (uint32_t)vis * rh + 12 * S, y = y0;
    if (y + h > bottom + 40 * S) y = bottom + 40 * S > h ? bottom + 40 * S - h : y0;
    hit_add(hc_ddclose, 0, 0, 0, 0, 0, 0, D.w, D.h);
    ui_rframe(x, y, w, h, 8 * S, C_ACC, C_HEAD);
    for (int r = 0; r < vis; r++) {
        int it = dd_top + r;
        uint32_t ry = y + 6 * S + (uint32_t)r * rh;
        if (DD_HDR(it)) { text_clip(x + 10 * S, ui_ty(ry, rh), dd_txt[it], C_ACC, 0xFF, w - 20 * S); continue; }
        hit_add(hc_ddsel, 0, it, 0, 0, x + 4 * S, ry, w - 8 * S, rh);
        if (it == dd_sel) ui_rrect(x + 4 * S, ry, w - 8 * S, rh, 6 * S, C_ROWSEL);
        if (two) {
            text_f(SF, 1, x + 12 * S, ry + 5 * S + (LH - SLH) / 2, dd_sub[it][0] ? "\xE2\x97\x8F" : "\xC2\xB7", dd_sub[it][0] ? C_OK : C_MUTED, 0xFF, 14 * S, 1);   /* 시안: 쓸 수 있는 롬 = 초록 표 */
            text_clip(x + 32 * S, ry + 4 * S, dd_txt[it], C_TEXT, 0xFF, w - 44 * S);
            if (dd_sub[it][0]) text_f(SF, 1, x + 32 * S, ry + 6 * S + LH, dd_sub[it], C_MUTED, 0xFF, w - 44 * S, 1);
        } else text_clip(x + 14 * S, ui_ty(ry, rh), dd_txt[it], it == dd_sel ? C_TEXT : C_MUTED, 0xFF, w - 28 * S);
    }
}

static void draw_props(void)
{
    if (props_tab == 6) { draw_mixpop(); return; }      /* 3.7.0 */
    UiRect o;
    char sub[96] = "";
    copy_str(sub, sizeof sub, machine_name);
    if (outg_is(props_tab) && lang == 0) { copy_str(sub, sizeof sub, "화면 · 효과 · 소리 — "); str_append(sub, sizeof sub, machine_name); }
    UiRect b = ui_full_card(outg_is(props_tab) ? T(S_OUTPUT) : props_tab < PROPS_TABS ? T(tab_label[props_tab]) : T(S_INPUT_DEV), sub, props_tab < PROPS_TABS ? "X86MSX" : 0, &o);
    uint32_t pad = 14 * S, top = b.y + 8 * S;
    if (outg_is(props_tab)) {                           /* 화면 · 효과 · 소리 출력 탭 */
        const char *lab[3] = { T(S_VIDEO), T(S_EFFECTS), lang == 0 ? "소리 출력" : T(S_OUTPUT) };
        ui_tabs(b.x, b.y + 4 * S, b.w, lab, 3, outg_idx(props_tab), 0);
        top = b.y + 4 * S + ui_tabs_h() + 10 * S;
    }
    if (props_tab > 6) {                                /* 4.0: 입력장치 = 키보드(6)·마우스(8)·조이스틱(7) 탭 */
        const char *lab[3] = { T(S_TAB_KEYBOARD), T(S_TAB_MOUSE), T(S_TAB_JOY) };
        ui_tabs(b.x, b.y + 4 * S, b.w, lab, 3, input_idx(props_tab), 0);
        top = b.y + 4 * S + ui_tabs_h() + 10 * S;
    }
    uint32_t cy = dlg_cmd_y(), bottom = cy - 10 * S;
    if (props_tab == 0) bottom -= 2 * (SLH + 10 * S) + 30 * S;
    if (props_tab == 6) { draw_mixer(b.x + pad, top, b.w - 2 * pad, bottom - top - SLH - 6 * S); mix_desc(b.x + pad, bottom - SLH, b.w - 2 * pad); }
    else if (props_tab == 5) draw_miditab(b.x + pad, b.w - 2 * pad, top, bottom);   /* 5.2.54 */
    else {
        static PropItem it[32];
        int n = props_items(props_tab, it);
        if (props_tab == 0) {                           /* 카드 배치 */
            draw_emu_tab(it, n, props_sel[0], b.x + pad, b.w - 2 * pad, top, bottom);
            if (dd_on) {
                int sx = props_sel[0] >= 0 && props_sel[0] < n ? props_sel[0] : 0;
                dd_draw_at(emu_r[sx].x + 16 * S, emu_r[sx].y + emu_r[sx].h, emu_r[sx].w * 90 / 100, bottom);
            }
        } else {
        if (props_tab == 4) draw_out_cards(it, n, props_sel[props_tab], b.x + pad, b.w - 2 * pad, top, bottom);
        else draw_item_list2(it, n, props_sel[props_tab], b.x + pad, b.w - 2 * pad, top, bottom);
        if (dd_on) dd_draw(b.x + b.w / 2 - pad, top, bottom, props_sel[props_tab], b.w / 2);
        }
    }
    if (props_tab == 0) {
        ui_section(b.x + pad + 6 * S, bottom + 6 * S, b.w - 2 * pad - 12 * S, T(S_SLOT_MAP));
        draw_slot_map(b.x + pad, bottom + 8 * S + SLH + 12 * S, b.w - 2 * pad);
    }
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, props_tab == 6 ? T(S_MIXER_KEYS) : props_tab > 6 ? T(S_POPUP_KEYS) : props_tab == 5 ? (mt_fx >= 0 ? nt("1/2/Tab 탭 이동   ↑/↓ 항목 이동   +/- 값   Enter 켜기·기본값·시험음   T 시험음   Esc 닫기", "1/2/Tab switch tab   Up/Down move   +/- value   Enter toggle/default/test   T test   Esc close", "1/2/Tab タブ移動   ↑/↓ 移動   +/- 値   Enter 切替   T テスト   Esc 閉じる") : nt("1/2/Tab 탭 이동   ↑/↓ 기계   ←/→ 칸   Enter 열기·조절   Esc 나오기   T 시험음   M 믹서   Ins 꽂기   Del 빼기   F5 다시 찾기", "1/2/Tab switch tab   Up/Down device   Left/Right field   Enter open/adjust   Esc leave   T test   M mixer   Ins plug   Del remove   F5 rescan", "1/2/Tab タブ移動   ↑/↓ 機器   ←/→ 欄   Enter 開く·調整   Esc 戻る   T テスト   M ミキサー   Ins 接続   Del 外す   F5 再検索")) : T(S_PROPS_KEYS));
}

static void hc_slide(const UiHit *h, int ev)             /* 속도 막대 누르기·끌기 (a = 항목 번호, b = 줄 번호, c = 막대 너비) */
{
    props_sel[0] = h->b;
    if (ev == 0) return;
    int id = (int)h->a; uint32_t bw = (uint32_t)h->c, bx = h->x + 8 * S;
    if (!bw) return;
    uint32_t px = ptr_x < bx ? bx : ptr_x > bx + bw ? bx + bw : ptr_x, f = (px - bx) * 1000 / bw;   /* 0..1000 */
    if (slide_press(id, f)) {                           /* 막대형 설정 표 (fe_sel.inc) */
    } else opt_prop_slide(id, f);                       /* 설정 표의 막대 (밝기·대비·채도·감마·효과·마우스) */
    settings_changed();
}
static void set_expand(int want)                       /* 카트리지 슬롯 확장 (0 없음, 1 슬롯 1, 2 슬롯 2) */
{
    for (int n = 1; n < MSX_CART_SLOTS; n++) if (M->cart[n].present) { cart_remove(n);   /* SLOT2~4는 자리가 바뀜 */ cart_path[n][0] = 0; cart_name[n][0] = 0; cart_special[n] = 0; }
    if (msx_set_expand(M, want)) { toast(T(S_EXPAND_FAIL), 0); msx_set_expand(M, 0); want = 0; }
    slot_expand = want;
    power_on();
    settings_changed();
}
static void props_change(int id, int dir)
{
    if (sel_change(id, dir)) return;                    /* 선택형 설정 표 */
    if (slide_change(id, dir)) return;                  /* 막대형 설정 표 */
    if (opt_prop_change(id, dir)) return;               /* 설정 표에 있는 항목 */
    switch (id) {
    case 1: if (dir == 0) { mach_from_props = 1; open_machines(); } else mach_step(dir); break;
    case 200: case 201: case 202: case 203: case 204: case 205:   /* 3.8.2 마우스별 사용/비사용 */
        if (dir == 0 && P.mouse_dev && P.mouse_set) {
            char dn[40]; int on = 0, k = id - 200;
            if (P.mouse_dev(k, dn, sizeof dn, &on)) {
                P.mouse_set(k, !on);
                if (mouse_is_ps2(dn)) touchpad_pref = !on; else mouse_offmask = on ? (mouse_offmask | 1 << k) : (mouse_offmask & ~(1 << k));
                settings_changed();
            }
        }
        break;
    case 41: if (dir == 0) { dd_open(41); break; } if (D.bpp != 1) { mon_emu = (mon_emu + (dir < 0 ? MON_EMU_N - 1 : 1)) % MON_EMU_N; full_redraw(); settings_changed(); } break;
    case 140: case 141: case 142: case 143: case 144: case 145: case 146: case 147: case 148: case 149: case 150: case 151: case 152: case 153: case 154: case 155: case 156:   /* 3.7.0: 사운드 탭 음원 켜기/끄기 (= 믹서 채널 음소거) */
        if (dir == 0) { int c = id - 140; if (c == 16) c = MIX_SCC; if (snd_present(c)) { mix_mute[c] = !mix_mute[c]; mix_apply(); settings_changed(); } }
        break;
    case 53: case 54: case 55: {                        /* 3.2.11 흐림·잔상·잡음: ←/→ 10 %씩, Enter 끔 ↔ 50 % */
        if (D.bpp == 1) break;
        int *v = id == 53 ? &fx_blur : id == 54 ? &fx_glow : &fx_noise;
        if (dir) { *v += dir * 10; if (*v < 0) *v = 0; if (*v > 100) *v = 100; } else *v = *v ? 0 : 50;
        full_redraw(); settings_changed(); break;
    }
    case 49: if (dir == 0) do_action(A_VIDEO_RESET); break;
    case 59: if (dir == 0) { dd_open(59); break; } if (P.video_outputs) { char ln[8][64]; int act = 0, vn = P.video_outputs(ln, 8, &act); if (vn > 1) { video_out = ((video_out >= 0 && video_out < vn ? video_out : 0) + (dir < 0 ? vn - 1 : 1)) % vn; settings_changed(); if (P.video_switch && !P.video_switch(video_out)) { toast(T(S_VID_SWITCHED), 0); vid_snd_follow(video_out); } else toast(T(S_VID_OUT_NOTE), 0); rep_key = -1; } } break;   /* 4.0.34 */
    case 112: if (dir == 0) fw_toggle(); break;
    case 173: if (dir == 0) { cart_flash_auto ^= 1; settings_changed(); } break;   /* 5.0.10 */
    case 174: if (dir == 0) { cart_flash_auto ^= 2; settings_changed(); } break;
    case 116: midi_int = !midi_int; msx_set_midi(M, midi_io_on()); midi_synth_apply(); msx_io_rebuild(M); settings_changed(); break;   /* 3.6.1 내장 MIDI */
    case 119: if (dir == 0) open_midimix(); break;      /* 5.2.53 */
    case 115: {                                         /* 다음/앞 .sf2 */
        if (dir == 0) { dd_open(115); break; }
        static FeDirEntry se[64];
        int n = P.list_dir ? P.list_dir(midi_dir(MD_SF), se, 64) : 0, k = 0, cur = -1;
        int idx[64];
        for (int i = 0; i < n; i++) {
            const char *nm = se[i].key[0] ? se[i].key : se[i].name; int l = (int)strlen(nm);
            if (!se[i].is_dir && l > 4 && nm[l - 4] == '.' && (nm[l - 3] | 32) == 's' && (nm[l - 2] | 32) == 'f' && nm[l - 1] == '2') { if (!strcmp(nm, sf_name)) cur = k; idx[k++] = i; }
        }
        if (!k) { sf_state = -1; break; }
        cur = cur < 0 ? 0 : (cur + (dir < 0 ? k - 1 : 1)) % k;
        copy_str(sf_name, sizeof sf_name, se[idx[cur]].key[0] ? se[idx[cur]].key : se[idx[cur]].name);
        sf_state = 0; sf_load(0); settings_changed();
        break;
    }
    case 44:                                            /* 소리 장치 다시 찾기 */
        if (dir == 0 && P.audio_select && P.audio_select(audio_out, snd_status, sizeof snd_status)) {
            P.audio_select(0, snd_status, sizeof snd_status);
            toast(T(S_AUDIO_FALLBACK), 0);
        }
        break;
    case 43:
        if (!P.audio_select) break;
        if (dir == 0) { dd_open(43); break; }               /* 4.0.66: Enter = 풀다운 */
        { int hd = P.hdmi_active && P.hdmi_active(), np = snd_out_pos(audio_out) + (dir < 0 ? -1 : 1);   /* 4.0.76: 끝에서 돌지 않고 멈춤 */
          if (np < 0 || np > (hd ? 2 : 1)) break;
          audio_out = snd_out_order[np]; }
        rep_key = -1;                                          /* 4.0.76: 전환이 느려도 키 반복이 계속 돌지 않게 */
        if (P.audio_select(audio_out, snd_status, sizeof snd_status)) {
            audio_out = 0; P.audio_select(0, snd_status, sizeof snd_status);  /* 이번만 자동(내장 사운드 우선) */
            toast(T(S_AUDIO_FALLBACK), 0);
        }
        settings_changed();
        break;
    case 4: if (dir == 0) do_action(A_SPEED_MAX); break;
    case 5: if (dir == 0) do_action(A_PANA_FRONT); break;
    case 6: if (dir == 0) do_action(A_PANA_PAUSE); break;
    case 10: case 11: case 12: case 16: case 17: case 18:  /* 3.2.12: 16~18 = 독일·프랑스·영국 */
        if (dir == 0) { key_layout = id >= 16 ? id - 13 : id - 10; keymap_layout(key_layout); input_init(&in); settings_changed(); }
        break;
    case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 71:
        if (dir == 0) joy_cap = id;                     /* 다음에 누르는 키로 (2.9) */
        break;
    default: break;
    }
}

static int mt_edit;                                     /* MIDI 창 소리·좌우 조절 중 (정의는 fe_miditab.inc) */
static void props_key(int key)
{
    menu_dirty = 1;
    if (dd_on) { dd_key_event(key); return; }
    if (key == 0x01 && props_tab == 5 && mt_edit) { mt_edit = 0; return; }   /* 소리·좌우 조절에서만 빠져나옴 */
    if (key == 0x01) { dialog_back(); return; }
    /* 속성 창의 탭이 없어짐 — 옵션 메뉴의 항목마다 자기 창 */
    if (props_tab > 6) {                                    /* 4.0: 입력장치 탭 바꾸기 */
        int ix = input_idx(props_tab);
        if (key == 0x0F) { props_tab = input_tab(ix + ((mods & 0x03) ? 2 : 1)); props_fix_sel(); return; }
        if (key == 0xD1) { props_tab = input_tab(ix + 1); props_fix_sel(); return; }
        if (key == 0xC9) { props_tab = input_tab(ix + 2); props_fix_sel(); return; }
        if (key >= 0x02 && key <= 0x04) { props_tab = input_tab(key - 0x02); props_fix_sel(); return; }
    }
    if (outg_is(props_tab)) {                               /* 출력장치 창 탭 바꾸기 */
        int ix = outg_idx(props_tab), nt = -1;
        if (key == 0x0F) nt = outg_tab(ix + ((mods & 0x03) ? 2 : 1));
        else if (key == 0xD1) nt = outg_tab(ix + 1);
        else if (key == 0xC9) nt = outg_tab(ix + 2);
        else if (key >= 0x02 && key <= 0x04) nt = outg_tab(key - 0x02);
        if (nt >= 0) { dd_on = 0; props_tab = nt; props_fix_sel(); return; }
    }
    if (props_tab == 5) { miditab_key(key); return; }      /* 5.2.54 새 MIDI 탭 */
    if (props_tab == 6) {                                   /* 믹서 */
        switch (key) {
        case 0xCB: mix_step(-1); break;                 /* 3.6.5: 화면 차례 */
        case 0xCD: mix_step(1); break;
        case 0xC8: mix_vol[mix_sel] = mix_vol[mix_sel] + 5 > 100 ? 100 : mix_vol[mix_sel] + 5; mix_mute[mix_sel] = 0; break;
        case 0xD0: mix_vol[mix_sel] = mix_vol[mix_sel] - 5 < 0 ? 0 : mix_vol[mix_sel] - 5; break;
        case 0x39: case 0x1C: case 0x9C: mix_mute[mix_sel] = !mix_mute[mix_sel]; break;
        case 0x1A: if (mix_has_pan(mix_sel)) mix_pan[mix_sel] = mix_pan[mix_sel] - 10 < -100 ? -100 : mix_pan[mix_sel] - 10; break;   /* [ 왼쪽으로 */
        case 0x1B: if (mix_has_pan(mix_sel)) mix_pan[mix_sel] = mix_pan[mix_sel] + 10 > 100 ? 100 : mix_pan[mix_sel] + 10; break;     /* ] 오른쪽으로 */
        case 0x2B: if (mix_has_pan(mix_sel)) mix_pan[mix_sel] = 0; break;                                                           /* \ 가운데 */
        default: return;
        }
        mix_apply();
        settings_changed();
        return;
    }
    static PropItem it[32];
    int n = props_items(props_tab, it), *sel = &props_sel[props_tab];
    if (props_tab == 0 && (key == 0xC8 || key == 0xD0 || key == 0xCB || key == 0xCD)) { emu_layout(it, n, 0, 1000, 0, 0); if (emu_key(it, n, sel, key)) return; }
    switch (key) {
    case 0xC8: case 0xD0: {                             /* 줄 단위 이동 — 화면 줄 순서대로, 두 칸 줄은 같은 칸(왼쪽·오른쪽) 우선 */
        int d = key == 0xC8 ? -1 : 1, cur = *sel, right = cur < n && it[cur].kind >= 5;
        int row = cur; while (row > 0 && it[row].kind >= 5) row--;
        for (int r = d > 0 ? row + 1 : row - 1; r >= 0 && r < n; r += d) {
            if (it[r].kind >= 5) continue;              /* 줄의 첫 항목만 줄 머리로 */
            int L = r, R = (r + 1 < n && it[r + 1].kind >= 5) ? r + 1 : -1;
            int okL = item_selectable(&it[L]), okR = R >= 0 && item_selectable(&it[R]);
            if (!okL && !okR) continue;
            *sel = (right && okR) ? R : okL ? L : R;
            break;
        }
        break;
    }
    case 0xCB: case 0xCD: {
        int d = key == 0xCB ? -1 : 1;
        if (*sel < n && it[*sel].kind == 5) { if (d < 0 && *sel > 0) *sel -= 1; break; }   /* 오른쪽 칸에서 ← = 왼쪽 칸 (슬라이더(6)는 값 바꾸기) */
        if (*sel < n && it[*sel].kind == 2 && *sel + 1 < n && it[*sel + 1].kind >= 5) { if (d > 0 && item_selectable(&it[*sel + 1])) *sel += 1; break; }
        if (*sel < n && !(it[*sel].kind == 1 && it[*sel].id != 8)) props_change(it[*sel].id, d);   /* 풀다운 항목은 ←/→로 바꾸지 않음 (Enter·클릭 = 풀다운) */
        break;
    }
    case 0x1C: case 0x9C: case 0x39: if (*sel < n && item_selectable(&it[*sel])) { if (it[*sel].kind == 1 && dd_gen(it[*sel].id)) break; props_change(it[*sel].id, 0); } break;
    default: break;
    }
}

static void draw_sced(void);
static void draw_image(void);
static void draw_help(void);
static void image_key(int key);
static void help_key(int key);
static void draw_disk_copy(void);
static void draw_diag(void);
static void diag_key(int key);
static void disk_copy_key(int key);
static void draw_medit(void);
static void draw_keyed(void);
static void keyed_key(int key);
static void draw_power_ask(void);
static void draw_pv(void);                              /* 4.0.5 인쇄 미리보기 (창 16) */
static void draw_edit(void);
static void draw_net(void);                            /* 4.0.29 텍스트 편집기 (창 17) */
static void ed_key(int key);
static void pv_key(int key);
static void draw_state_list(void);
static void state_list_key(int key);
static void draw_dialog(void)
{
    fe_trace("dd hit_clear");
    hit_clear();
    fe_trace("dd go");                                        /* 3.0 마우스: 창마다 그리면서 자리를 다시 남김 */
    if (dialog == 6) { draw_props(); return; }
    if (dialog == 5) { draw_machines(); return; }
    if (dialog == 4) { draw_grow(); return; }
    if (dialog == 1) draw_browser();
    else if (dialog == 2) draw_diag();
    else if (dialog == 3) draw_info(1);
    else if (dialog == 7) draw_sound_diag();
    else if (dialog == 8) draw_sced();
    else if (dialog == 9) draw_medit();
    else if (dialog == 10) draw_state_list();
    else if (dialog == 11) draw_image();
    else if (dialog == 12) draw_help();
    else if (dialog == 13) draw_disk_copy();
    else if (dialog == 14) draw_keyed();
    else if (dialog == 15) draw_power_ask();
    else if (dialog == 16) draw_pv();
    else if (dialog == 17) draw_edit();
    else if (dialog == 18) draw_net();
    else if (dialog == 19) draw_midimix();              /* 5.2.53 */
    else if (dialog == 20) draw_langwin();              /* 5.2.192 */
}

/* ------------------------------------------------------------------ */
/* 키 처리                                                              */
/* ------------------------------------------------------------------ */
static volatile uint8_t kq[256];
extern uint8_t fe_usb_kbd_seen;
static void klog_note(const char *what);                 /* 5.0.180 키 기록 */
static void klog_raw(uint8_t c);
static volatile uint8_t kq_head;
static uint8_t kq_tail;

int fe_list_skip, fe_list_next = -1;
void fe_push_key(uint8_t c)
{
    uint8_t h = kq_head;
    if ((uint8_t)(h + 1) != kq_tail) { kq[h] = c; kq_head = (uint8_t)(h + 1); }
}

/* MSX로 보낼 바이트 (input.c의 3프레임 규칙 때문에 미뤄질 수 있음) */
static uint8_t mq[256];
static uint8_t mq_head, mq_tail;
static uint8_t msx_held[256];                   /* 3.2.1: MSX로 보낸 눌림 (창이 열린 동안 뗀 키도 MSX에서 떼게) */
static void send_msx(int key, int press)
{
    if (press && msx_held[key & 0xFF]) return;          /* 4.0.14: 이미 눌린 키의 자동 반복은 큐에 쌓지 않음 (여러 키를 누른 채 있으면 큐가 넘쳐 뗌을 잃고 키가 계속 반복됨) */
    if ((uint8_t)(mq_head + 3) == mq_tail || (uint8_t)(mq_head + 2) == mq_tail) { if (press) return; }   /* 큐가 거의 차면 눌림만 버림 (뗌은 지킴) */
    msx_held[key & 0xFF] = (uint8_t)(press != 0);
    if (press) klog_note("MSX로 키 보냄");
    if (key & 0x80) mq[mq_head++] = 0xE0;
    mq[mq_head++] = (uint8_t)((key & 0x7F) | (press ? 0 : 0x80));
}

/* 수정 키 (blueMSX 비트: 01 LShift, 02 RShift, 04 LCtrl, 08 RCtrl, 10 LAlt, 20 RAlt) */
static int mod_bit(int key)
{
    switch (key) {
    case 0x2A: return 0x01; case 0x36: return 0x02; case 0x1D: return 0x04;
    case 0x9D: return 0x08; case 0x38: return 0x10; case 0xB8: return 0x20;
    default: return 0;
    }
}

/* 스캔 코드 → Windows 가상 키 (blueMSX 단축키 파일과 같은 값) */
static int key_vk(int key)
{
    if (key >= 0x3B && key <= 0x44) return 0x70 + (key - 0x3B);    /* F1~F10 */
    switch (key) {
    case 0x57: return 0x7A;                                         /* F11 */
    case 0x58: return 0x7B;                                         /* F12 */
    case 0x46: return 0x91;                                         /* Scroll Lock */
    case 0xC6: return (mods & 0x0C) ? 0x03 : 0x13;                  /* Ctrl+Break / Pause */
    case 0x7F: return 0x13;                                         /* Pause (E1 순서) */
    case 0xB7: return 0x2C;                                         /* PrtScr */
    case 0x1C: case 0x9C: return 0x0D;
    case 0xDD: return 0x5D;                                         /* 메뉴(≣) 키 */
    case 0xC8: return 0x26;                                         /* ↑ */
    case 0xD0: return 0x28;                                         /* ↓ */
    case 0xCB: return 0x25;                                         /* ← (2.9: 단축키 편집기) */
    case 0xCD: return 0x27;                                         /* → */
    case 0xD2: return 0x2D; case 0xD3: return 0x2E;                 /* Ins, Del */
    case 0xC7: return 0x24; case 0xCF: return 0x23;                 /* Home, End */
    case 0xC9: return 0x21; case 0xD1: return 0x22;                 /* PgUp, PgDn */
    case 0x0C: case 0x4A: return 0xBD;                              /* - (숫자판 - 도) */
    case 0x0D: case 0x4E: return 0xBB;                              /* =/+ (숫자판 + 도) */
    }
    if (key >= 0x02 && key <= 0x0B) return key == 0x0B ? '0' : '1' + (key - 2);   /* 숫자 */
    if (key < 0x40 && sc_ascii[key] >= 'a') return sc_ascii[key] - 32;
    return 0;
}

typedef struct { uint8_t vk, mods; uint8_t action; } Shortcut;
/* blueMSX 기본 단축키 (Shortcut Profiles\blueMSX.shortcuts) + X86MSX 메뉴 키 */
static const Shortcut sc_def[] = {
    { 0x79, 0x00, A_MENU }, { 0x5D, 0x00, A_MENU },
    { 0x13, 0x00, A_RUN_PAUSE }, { 0, 0, A_NONE }, { 0x71, 0x10, A_HARD_RESET },   /* 4.0.34: Pause = 실행/일시 정지, LAlt+F2 = 하드 리셋 */
    { 0x71, 0x04, A_SOFT_RESET }, { 0x2E, 0x14, A_FULL_RESET },   /* 4.0.34: LCtrl+F2 = 소프트 리셋 */
    { 0x26, 0x10, A_VOL_UP }, { 0x28, 0x10, A_VOL_DOWN }, { 0, 0, A_MUTE },   /* 음량 = LAlt+↑/↓ (옛 LCtrl) */
    { 0x00, 0x00, A_NONE }, { 0x00, 0x00, A_NONE },                /* 3.7.1: 옛 LCtrl+↑/↓ 2% 자리 (저장된 표의 번호를 지키려고 남김) */
    { 0x77, 0x01, A_TURBO_HOLD }, { 0, 0, A_SPEED_NORMAL }, { 0xBD, 0x10, A_SPEED_DOWN }, { 0xBB, 0x10, A_SPEED_UP },
    { 0xBB, 0x05, A_SPEED_MAX },
    { 0x75, 0x00, A_CART1_INSERT }, { 0x76, 0x00, A_CART2_INSERT }, { 0x75, 0x04, A_CART1_EJECT }, { 0x76, 0x04, A_CART2_EJECT },
    { 0, 0, A_EXIT }, { 0, 0, A_DIAG }, { 0, 0, A_ABOUT },   /* 끝내기(옛 LAlt+F9)·진단(옛 LCtrl+F6)·에 대하여는 키 없음 */
    { 0, 0, A_PANA_FRONT }, { 0, 0, A_PANA_PAUSE },   /* 옛 Scroll Lock·RAlt+Pause 없앰 */
    /* blueMSX에 있지만 아직 없는 기능: 키를 먹고 알림만 */
    { 0x77, 0x08, A_QUICK_LOAD }, { 0x78, 0x08, A_QUICK_SAVE }, { 0, 0, A_STATE_LOAD }, { 0, 0, A_STATE_SAVE },   /* 옛 LAlt+F7/F8 없앰 */   /* 2.10 */
    { 0x77, 0x10, A_DISKA_NEXT }, { 0x75, 0x08, A_AUDIO_REC }, { 0x2C, 0x00, A_SCREENSHOT },
    { 0x77, 0x00, A_DISKA_INSERT }, { 0x78, 0x00, A_DISKB_INSERT }, { 0, 0, A_TAPE_INSERT },     /* 3.2.1: LCtrl·RCtrl+F11 = 테이프 */
    { 0x77, 0x04, A_DISKA_EJECT }, { 0x78, 0x04, A_DISKB_EJECT }, { 0x78, 0x20, A_TAPE_EJECT },
    { 0, 0, A_NONE }, { 0, 0, A_NONE }, { 0x7B, 0x00, A_PROPS_MIX },   /* 4.0.34: 특수 카트리지 키(#107·#108) 없앰, 믹서 = F12 */ /* 3.2.14 MED-11 옛 자리:   /* 3.2.14 MED-11: LCtrl+LAlt+F7/F8 특수 카트리지 */
    { 0, 0, A_PROPS_EMU }, { 0, 0, A_PROPS_VIDEO }, { 0, 0, A_PROPS_SOUND }, { 0, 0, A_PROPS_KB },   /* 입력장치 옛 F5 없앰 */
    { 0x78, 0x14, A_TAPE_REWIND },                     /* 3.2.1: LCtrl+LAlt+F11 테이프 되감기 */
    { 0x7A, 0x04, A_ICONS_TOGGLE },                    /* 3.2.2: F6 아이콘 숨기기/보이기 */
    { 0x71, 0x08, A_RESET_CTRL },                      /* 3.2.2: RCtrl+F12 CTRL 누른 채 리셋 */
    { 0, 0, A_NONE }, { 0, 0, A_NONE },                /* 3.2.10에 있던 자리(작은·큰 화면 저장, 3.2.12에서 없앰). 저장된 단축키 순서를 지키려고 빈 자리만 둠 */
    { 0, 0, A_RESET_AFTER_INSERT },                   /* 옛 LCtrl+LShift+F8 없앰 */
    { 0, 0, A_DISK_RESET_AFTER },                     /* 옛 LCtrl+LAlt+F9 없앰 */
    { 0, 0, A_TAPE_POSMENU }, { 0x78, 0x0A, A_TAPE_NEW },   /* 옛 LCtrl+LAlt+F10 없앰 */   /* 3.2.14 MED-09: LCtrl+LAlt+F10 테이프 위치, LCtrl+LShift+F11 새 테이프 */
    { 0x76, 0x20, A_PRN_END },                          /* 3.3.0 MENU3-01: LCtrl+LShift+P 인쇄 끝내기 */
    { 0, 0, A_PROPS_EFFECTS }, { 0, 0, A_PROPS_MIDI }, { 0, 0, A_NONE },   /* 4.0.34: 조이스틱 = F12 */   /* 3.3.1 MENU3-05: RShift+F10/F11/F12 */
    { 0, 0, A_LANG_MENU }, { 0, 0, A_MACHINE_EDIT }, { 0, 0, A_SC_EDIT }, { 0, 0, A_NONE },   /* RCtrl+RShift+L/M/S (4.0.30: 포트 메뉴 P 없앰, 저장된 단축키 순서를 지키려고 빈 자리) */
    { 0x76, 0x08, A_SPR_TOGGLE },                       /* 3.3.1 VID-03: LCtrl+LShift+S 스프라이트 보이기/숨기기 */
    { 0, 0, A_NONE },                                  /* 3.6.6: 디스크 시간 흉내 없앰 (저장된 단축키 순서를 지키려고 빈 자리) */
    { 0x7B, 0x20, A_STEREO },                          /* 3.3.3 SND3-01: RAlt+F6 모노/스테레오 */                      /* 3.3.1 MENU3-06: LCtrl+LShift+F 디스크 시간 흉내 */
    { 0, 0, A_NONE },                                  /* 3.6.6: 되감기 없앰 (빈 자리) */
    { 0, 0, A_MEDIA },                                         /* 3.5.0: RCtrl+RShift+D 미디어 팝업 */
    { 0, 0, A_REBOOT },                                          /* PC 다시 시작 옛 LCtrl+F9 없앰 */
    { 0x2C, 0x01, A_VID_REC },                                    /* 4.0.31: LShift+PrtSc 영상 녹화 */
    { 0x7A, 0x00, A_DISK_COPY },                                  /* 4.0.34: F9 파일 관리자 */
    { 0x75, 0x20, A_PRN_RAW },                                    /* 4.0.34: LShift+F6 프린터 원본 파일로 (.prn) */
    { 0x7B, 0x04, A_MIDI_MIX },                                   /* LCtrl+F12 MIDI 믹서 (팝업) */
    { 0x78, 0x10, A_DISKB_NEXT },                                 /* 새 단축키 배치 (B 드라이브 다음 디스크 칸 추가) */
};

static int ralt_down, ralt_other;
static int rctrl_down, rctrl_other;                 /* 한글 기종의 RCtrl(한자) 혼자 누름 */
static int e0, e1;

static int ui_active(void);
static uint8_t key_down[256];
static uint8_t consumed[256];

/* 2.10.1: 키 상태를 처음으로 (오래 막혔거나 키보드 버퍼가 넘친 뒤).
 * 그동안 잃은 뗌 신호 때문에 MSX 키가 눌린 채로 남지 않게 모두 놓고, 아직 누르고 있는 키는 자동 반복으로 다시 들어온다 */
static void key_resync(void)
{
    klog_note("resync (모든 키 뗌)");
    e0 = e1 = 0;
    mods = 0;
    ralt_down = 0; rctrl_down = 0;
    memset(key_down, 0, sizeof key_down);
    memset(consumed, 0, sizeof consumed);
    mq_head = mq_tail = 0;
    input_init(&in);
    memset(M->kbd, 0xFF, sizeof M->kbd);
}
static void update_joystick(void)
{
    static uint8_t pad_prev[2];
    for (int p = 0; p < 2; p++) {                       /* 포트마다 정한 키 (2.9): 위 아래 왼쪽 오른쪽 A B */
        uint8_t j = 0;
        if (joy_mode[p] == 1)
            for (int k = 0; k < 6; k++) if (joy_keys[p][k] && key_down[joy_keys[p][k]]) j |= (uint8_t)(1 << k);
        if (joy_mode[p] >= 2 && P.pad_read) {           /* 2.13: USB 게임패드 */
            int b = P.pad_read(joy_mode[p] - 2, 0, 0);
            if (b > 0) {
                j = (uint8_t)(b & 0x3F);
                uint8_t press = (uint8_t)(b & ~pad_prev[p]);  /* 3.0: Start = 메뉴 열기·닫기, Select = 일시 정지 */
                if (dg_joy_tab()) press = 0;                    /* 3.8.1: 진단 조이스틱 시험 중에는 Start·Select를 표시만 */
                if (press & 0x40) { if (ui_active()) menu_close_all(); else { input_init(&in); mq_head = mq_tail = 0; menu_show(); } }
                if ((press & 0x80) && !ui_active()) do_action(A_RUN_PAUSE);
                pad_prev[p] = (uint8_t)b;
            } else pad_prev[p] = 0;
        }
        M->joy[p] = joy_mode[p] && !ui_active() ? j : 0;
    }
}

/* 바꿀 수 있는 단축키 표 (2.9 단축키 편집기). sc_def와 같은 순서, vk 0 = 지움 */
#define SC_MAX 128
static Shortcut sc_tab[SC_MAX];
static int sc_n;
static void sc_reset(void)
{
    sc_n = (int)(sizeof sc_def / sizeof sc_def[0]);
    if (sc_n > SC_MAX) sc_n = SC_MAX;
    for (int i = 0; i < sc_n; i++) sc_tab[i] = sc_def[i];
}
static void sc_ensure(void) { if (!sc_n) sc_reset(); }

static int find_shortcut(int vk)
{
    sc_ensure();
    for (int i = 0; i < sc_n; i++)
        if (sc_tab[i].vk && sc_tab[i].vk == vk && sc_tab[i].mods == mods) return sc_tab[i].action;
    return A_NONE;
}

static void ini_put(char *b, const char *key, const char *val);
static void ini_num(char *b, const char *key, int v);
#include "fe_tools.inc"
#include "fe_keyed.inc"

static int ui_active(void) { return dialog || bar_visible || depth; }
static int status_show_all(void) { return icons_always || ui_active() || run_state != RUN_ON; }
static int status_rom_game(void)
{
    for (int n = 0; n < MSX_CART_SLOTS; n++) if (M->cart[n].present && !cart_special[n]) return 1;
    return 0;
}

/* 2.16: 메뉴·대화 창의 키 반복. USB 키보드는 반복 신호(typematic)가 없고 PS/2는 0.5초 뒤 초당 10회라 느렸음.
 * 하드웨어 반복은 무시하고, 방향키·PgUp/Dn·Home/End·BS·Del은 0.3초 뒤 초당 25회로 직접 반복한다 */
static int rep_now;
static uint32_t rep_due;
static int dc_ask, dc_ren;   /* 4.0.31: 이미지파일관리 상태 (정의는 fe_sys.inc) */
static int rep_able(int key)
{
    if (dialog == 13 && !dc_ask) {                      /* 4.0.31: 이미지파일관리 — Space(고르기)·이름 글자도 꾹 누르면 반복 */
        if (key == 0x39) return 1;
        if (dc_ren && key != 0x01 && key != 0x1C && key != 0x9C && !mod_bit(key)) return 1;
    }
    switch (key) {
    case 0xC8: case 0xD0: case 0xCB: case 0xCD: case 0xC9: case 0xD1: case 0xC7: case 0xCF: case 0xD3:   /* E0 방향키 등 */
    case 0x48: case 0x50: case 0x4B: case 0x4D: case 0x49: case 0x51: case 0x0E:                        /* 숫자판·BS */
        return 1;
    }
    return 0;
}


/* 키 지연 기록 (LOG\KEY_X<버전>.TXT) — 키가 계속 움직이는 현상의 원인을 찾기 위해 키를 누르는 동안의 프레임 간격·처리 시간·구간별 시간을 남김.
 * 키 신호가 온 뒤 3초 동안만 쓰고, 키를 멈춘 지 3초 뒤 USB에 한 번 저장 (저장 때문에 키가 느려지지 않게) */
static char klog_buf[24576];
static uint32_t klog_len, klog_act, klog_last_frame, klog_pf_ms;
static uint8_t klog_dirty, klog_wrap, klog_force;   /* klog_force: 프레임이 0.7초 넘게 멈췄으면 바로 저장 (멈춤 현상 기록) */
static void klog_add(const char *line)
{
    uint32_t n = 0; while (line[n]) n++;
    if (klog_len + n + 2 > sizeof klog_buf) { klog_len = 0; klog_wrap++; }
    if (!klog_len) { static const char h[] = "X86MSX KEY LOG " MSXEMU_VERSION "\r\nt=ms(부팅 후) I=파일 읽기(R)·쓰기(W) 걸린 ms K=키 신호(v 누름 ^ 뗌 r 프로그램 반복 h 키보드 자동 반복 w 마우스 휠) F=프레임(gap 앞 프레임과 간격 q 읽기 전 밀린 신호 byte dt 키 처리 ms ui 메뉴·창) P=구간별(1초: % / 한 번 최대 ms)\r\n"; for (uint32_t i = 0; i < sizeof h - 1; i++) klog_buf[klog_len++] = h[i]; }
    for (uint32_t i = 0; i < n; i++) klog_buf[klog_len++] = line[i];
    klog_buf[klog_len++] = '\r'; klog_buf[klog_len++] = '\n';
    klog_dirty = 1;
}
static void klog_key(int key, int press, int was)
{
    uint32_t now = P.ms();
    if (!rep_now) klog_act = now ? now : 1;
    if (!klog_act || now - klog_act >= 3000) return;
    char l[96]; l[0] = 0;
    str_append(l, sizeof l, "t="); num_append(l, sizeof l, now, 0, ' ');
    { char h[8] = { ' ', 'K', ' ', '0', 'x', "0123456789ABCDEF"[(key >> 4) & 15], "0123456789ABCDEF"[key & 15], 0 }; str_append(l, sizeof l, h); }
    str_append(l, sizeof l, rep_now ? " r" : ui_pressing ? " w" : press ? (was ? " h" : " v") : " ^");
    klog_add(l);
}
static void klog_note(const char *what)
{
    uint32_t now = P.ms();
    klog_act = now ? now : 1;
    char l[120]; l[0] = 0;
    str_append(l, sizeof l, "t="); num_append(l, sizeof l, now, 0, ' ');
    str_append(l, sizeof l, " N "); str_append(l, sizeof l, what);
    klog_add(l);
}
static void klog_raw(uint8_t c)                         /* 키보드에서 읽은 날 바이트 (갑자기 눌린 키가 어디서 왔는지: 날 바이트가 없는데 K 줄만 있으면 프로그램 안쪽에서 만든 것) */
{
    uint32_t now = P.ms();
    klog_act = now ? now : 1;
    char l[96]; l[0] = 0;
    str_append(l, sizeof l, "t="); num_append(l, sizeof l, now, 0, ' ');
    { char h[8] = { ' ', 'B', ' ', '0', 'x', "0123456789ABCDEF"[c >> 4], "0123456789ABCDEF"[c & 15], 0 }; str_append(l, sizeof l, h); }
    str_append(l, sizeof l, fe_usb_kbd_seen ? " usb" : " ps2");
    klog_add(l);
}
void fe_klog_io(const char *path, int w, uint32_t ms)   /* 키를 누르는 동안의 USB 파일 읽기·쓰기 (I 줄) — 무엇이 프레임을 늦추는지 */
{
    if (!klog_act || P.ms() - klog_act >= 3000 || !path) return;
    for (const char *q = path; *q; q++) if (q[0] == 'K' && q[1] == 'E' && q[2] == 'Y' && q[3] == '_') return;   /* 이 기록 파일 자체 */
    char l[200]; l[0] = 0;
    str_append(l, sizeof l, "t="); num_append(l, sizeof l, P.ms(), 0, ' ');
    str_append(l, sizeof l, w ? " I W " : " I R "); num_append(l, sizeof l, ms, 0, ' '); str_append(l, sizeof l, "ms ");
    str_append(l, sizeof l, path);
    klog_add(l);
}
static void klog_frame_begin(uint32_t now, uint32_t *t0)
{
    *t0 = now;
    uint32_t gap = klog_last_frame ? now - klog_last_frame : 0;
    klog_last_frame = now;
    if (!klog_act || now - klog_act >= 3000) return;
    if (gap >= 700) klog_force = 1;
    char l[96]; l[0] = 0;
    str_append(l, sizeof l, "t="); num_append(l, sizeof l, now, 0, ' ');
    str_append(l, sizeof l, " F gap="); num_append(l, sizeof l, gap, 0, ' ');
    str_append(l, sizeof l, " q="); num_append(l, sizeof l, (uint8_t)(kq_head - kq_tail), 0, ' ');
    klog_add(l);
}
static void klog_frame_end(uint32_t now, uint32_t t0)
{
    if (!klog_act) return;
    uint32_t t1 = P.ms();
    if (now - klog_act < 3000) {
        if (t1 - t0 >= 2) {
            char l[96]; l[0] = 0;
            str_append(l, sizeof l, "t="); num_append(l, sizeof l, t1, 0, ' ');
            str_append(l, sizeof l, " F dt="); num_append(l, sizeof l, t1 - t0, 0, ' ');
            str_append(l, sizeof l, ui_active() ? " ui" : " run");
            klog_add(l);
        }
        if (pf_ms0 != klog_pf_ms && klog_pf_ms) {
            char l[360], w[160]; l[0] = 0;
            str_append(l, sizeof l, "t="); num_append(l, sizeof l, t1, 0, ' '); str_append(l, sizeof l, " P ");
            pf_text(w, sizeof w, 0); str_append(l, sizeof l, w);
            str_append(l, sizeof l, "| worst ");
            pf_text(w, sizeof w, 1); str_append(l, sizeof l, w);
            klog_add(l);
        }
    }
    klog_pf_ms = pf_ms0;
    if (klog_dirty && (now - klog_act >= 3000 || klog_force) && P.write_file && P.create) {
        klog_dirty = 0; klog_force = 0;
        P.create("0:\\LOG", 1);
        P.create("0:\\LOG\\KEY_X" MSXEMU_VERSION ".TXT", 0);
        P.write_file("0:\\LOG\\KEY_X" MSXEMU_VERSION ".TXT", (const uint8_t *)klog_buf, klog_len);
    }
}

uint8_t fe_usb_kbd_seen;                                 /* USB 키보드가 한 번이라도 쓰였으면 1 (usbkbd.c) */
static uint8_t tm_seen;                                  /* PS/2 키보드의 자동 반복(키를 누르고 있는 동안 계속 오는 눌림 신호)을 본 적 있으면 1 */
static uint32_t key_ev_ms;                               /* 마지막 키 신호가 온 시각 */
static uint32_t key_hw_ms;                               /* 키보드에서 마지막 바이트를 받은 시각 */
static int tm_key = -1;                                  /* 키보드가 자동 반복하는 키 (PS/2는 마지막에 누른 키 하나만 반복) */
static uint8_t tm_rep;                                   /* 그 키의 자동 반복을 본 적 있으면 1 */
static void key_event(int key, int press)
{
    int was = key_down[key & 0xFF];
    if (press) { char kx[4] = { "0123456789ABCDEF"[(key >> 4) & 15], "0123456789ABCDEF"[key & 15], 0, 0 }; fe_trace_str("key", kx); }   /* 5.2.24 멈춤 추적 */
    {   /* 뗌 신호를 잃어 '눌린 채'로 남은 키는 다음에 누를 때 반복 신호로 오해해 무시되어 두 번 눌러야 했음 — 마지막 신호가 1.1초(자동 반복 첫 지연 최대 1초) 넘게 지났으면 새로 누른 것으로 본다 */
        static uint32_t key_t[256];
        uint32_t tn = P.ms();
        if (press && was && !rep_now && !ui_pressing && tn - key_t[key & 0xFF] > 1100) { was = 0; klog_note("눌린 채로 남은 키 → 새로 누른 것으로 처리"); }
        if (!rep_now) key_t[key & 0xFF] = tn;
    }
    if (!rep_now) key_ev_ms = P.ms();                   /* 프로그램이 만든 반복은 '키 신호'가 아님 */
    if (press && was && !rep_now && !fe_usb_kbd_seen) tm_seen = 1;
    key_down[key & 0xFF] = (uint8_t)press;
    klog_key(key, press, was);
    if (press && was && !rep_now && ui_active()) return;   /* 하드웨어 반복 무시 (아래 rep_*가 대신) */
    if (!press && key == rep_key) rep_key = -1;
    if (press && !rep_now) {
        if (ui_active() && rep_able(key)) { rep_key = key; rep_due = P.ms() + 300; }
        else rep_key = -1;
    }
    int mb = mod_bit(key);
    if (mb) { if (press) mods |= (uint8_t)mb; else mods &= (uint8_t)~mb; }
    if (press && ralt_down && key != 0xB8) ralt_other = 1;
    if (press && rctrl_down && key != 0x9D) rctrl_other = 1;
    if (press && (!dialog || dialog == 11 || dialog == 13 || dialog == 17 || dialog == 18 || dialog == 19 || dialog == 20) && (key == 0xC8 || key == 0xD0) && (mods & 0x10) && key_down[key == 0xC8 ? 0xD0 : 0xC8]) {   /* 글자를 먼저 받는 창에서도 무음 */   /* 4.0.34: LCtrl+↑↓ 같이 = 무음 */
        volume = vol_undo; muted = !muted_undo; consumed[key] = 1; settings_changed(); vol_osd(); return;
    }
    if (press && !mb && key != 0xB8 && !(dialog == 6 && joy_cap) && !(dialog == 8 && sced_cap) && !(dialog == 14 && km_cap) && !(dialog == 2 && diag_kbd_tab())) {
        int pvk = key_vk(key), pa = pvk ? find_shortcut(pvk) : A_NONE;
        if (dialog == 15 && pa != A_HARD_RESET && pa != A_SOFT_RESET && pa != A_FULL_RESET && pa != A_RESET_CTRL) pa = A_NONE;      /* 4.0.34: PC 재부팅·끝내기·리셋 키는 어느 창에서나 먼저 */
        if (pa != A_NONE && pa == popup_act && dialog && dialog == popup_dlg && (dialog != 6 || props_tab == popup_tab)) { consumed[key] = 1; dialog_back(); return; }   /* 단축키로 연 팝업은 같은 단축키로 닫음 */
        if (dialog && (pa == A_VOL_UP || pa == A_VOL_DOWN || pa == A_MUTE)) { consumed[key] = 1; do_action(pa); return; }   /* 음량 키는 글자를 먼저 받는 창(파일 관리자 F11 등)에서도, 창은 그대로 둔 채 */
        if (pa == A_PROPS_MIX) {                        /* 믹서는 어느 창·메뉴가 떠 있어도 먼저 열고, 닫으면 하던 곳으로 */
            consumed[key] = 1;
            if (ui_active()) ui_ret_save(); else ui_ret_valid = 0;
            do_action(A_PROPS_MIX);
            popup_act = A_PROPS_MIX; popup_dlg = dialog; popup_tab = props_tab;
            return;
        }
        if (pa == A_REBOOT || pa == A_EXIT || pa == A_HARD_RESET || pa == A_SOFT_RESET || pa == A_FULL_RESET || pa == A_RESET_CTRL) { consumed[key] = 1; if (ui_active() && pa != A_REBOOT && pa != A_EXIT) menu_close_all(); do_action(pa); return; }   /* 리셋 4종은 메뉴·창·확인창 어디서든 먼저 — 열린 것을 모두 닫고 실행 */   /* 파일 관리자(F11)가 떠 있으면 리셋 때 창을 닫아 MSX 화면이 보이게 */
        if (pa == A_MENU && dialog) { consumed[key] = 1; mach_from_props = 0; menu_close_all(); input_init(&in); mq_head = mq_tail = 0; menu_show(); return; }   /* F10 은 글자·키를 먼저 받는 창(이미지파일관리·편집기 등)에서도 메뉴를 처음부터 다시 엶 */
    }
    if (dialog == 15) {                                 /* 4.0: 끝내기·재시작 확인 */
        if (press) {
            if (key == 0xCB || key == 0xCD || key == 0x0F) pq_sel = !pq_sel;
            else if (key == 0x01 || key == 0x31) dialog_back();                          /* Esc, N */
            else if (key == 0x15 || ((key == 0x1C || key == 0x9C) && pq_sel == 0)) do_action(pq_act == A_EXIT ? A_EXIT_NOW : A_REBOOT_NOW);   /* Y, Enter(예) */
            else if (key == 0x1C || key == 0x9C) dialog_back();
            menu_dirty = 1;
        }
        return;
    }
    if (dialog == 16) { if (press) pv_key(key); return; }   /* 4.0.5 인쇄 미리보기 */
    if (dialog == 14 && press) { keyed_key(key); return; }   /* 3.2.12 키 편집기: 모든 키를 먼저 (오른쪽 Alt 포함) */

    /* 오른쪽 Alt: 혼자 눌렀다 떼면 한글/KANA 변환 (MSX CODE 키) */
    if (key == 0xB8 && keymap_altgr_layout()) {          /* 3.2.12: 독일·프랑스 배열은 오른쪽 Alt = AltGr (MSX 쪽 키 배치로 보냄) */
        if (press) { if (!ui_active()) send_msx(0xB8, 1); }
        else if (msx_held[0xB8]) send_msx(0xB8, 0);
        return;
    }
    if (key == 0xB8) {
        if (press) { if (!ralt_down) { ralt_down = 1; ralt_other = 0; } return; }
        ralt_down = 0;
        if (!ralt_other && !ui_active()) { send_msx(0xB8, 1); send_msx(0xB8, 0); }
        return;
    }
    /* 대우 CPC-400·400S (한글 폰트 롬 256KB): RCtrl(한자)을 혼자 눌렀다 떼면 MSX Shift+Select = 한자 변환.
       다른 키와 함께 누르면(RCtrl+F6 같은 단축키) 아무것도 보내지 않음. 이 기종에서는 RCtrl이 MSX CTRL로 가지 않음 (CTRL은 LCtrl) */
    if (key == 0x9D && M && M->kanji_hi_mask == 0x7F) {
        if (press) { if (!rctrl_down) { rctrl_down = 1; rctrl_other = 0; } return; }
        rctrl_down = 0;
        if (!rctrl_other && !ui_active()) { send_msx(0x2A, 1); send_msx(0xC9, 1); send_msx(0xC9, 0); send_msx(0x2A, 0); }
        return;
    }

    if (!press) {
        if (turbo_hold && (key == 0x42 || key == 0x2A)) { turbo_hold = 0; margin_bd = -1; }
        if (consumed[key]) { consumed[key] = 0; if (msx_held[key & 0xFF]) send_msx(key, 0); return; }   /* 3.2.1: LCtrl+F9처럼 단축키 앞에 MSX로 간 Ctrl도 뗌 */
        if (!ui_active() || msx_held[key & 0xFF]) send_msx(key, 0);
        return;
    }

    if (dialog == 8) { sced_key(key); return; }        /* 단축키 편집기: 단축키보다 먼저 (2.9) */
    if (dialog == 9) { medit_key(key); return; }       /* 머신 편집기 */
    if (dialog == 2 && diag_kbd_tab()) { diag_key(key); return; }   /* 3.0.2 진단 키보드 탭: 모든 키를 시험으로 */
    if (dialog == 17) { ed_key(key); return; }
    if (dialog == 20) { langwin_key(key); return; }          /* 5.2.192 언어 · 글꼴 */
    if (dialog == 19) { midimix_key(key); return; }          /* 5.2.53 MIDI 믹서 */
    if (dialog == 18) { netdlg_key(key); return; }          /* 5.0.57 네트워크 설정 (글자를 쓰므로 단축키보다 먼저) */          /* 4.0.29 텍스트 편집기 (글자를 쓰므로 단축키보다 먼저) */
    if (dialog == 1 && (key == 0x3D || key == 0x3E || (key == 0x3F && browse_slot < MSX_CART_SLOTS)) && !browse_vhd() && !browse_dir()) { browser_key(key); return; }   /* 4.0.29: F3 모든 파일, F4 편집 */
    if (dialog == 13) { disk_copy_key(key); return; }   /* 3.0.2 디스크 파일 복사 (F5·Del이 단축키보다 먼저) */
    if (dialog == 11) { image_key(key); return; }      /* 3.0.2 이미지 만들기 (이름에 글자를 쓰므로 단축키보다 먼저) */
    if (dialog == 6 && joy_cap) {                       /* 조이스틱 키 잡기 */
        if (key == 0x01) joy_cap = 0;
        else { int i = joy_cap - 60; joy_keys[i / 6][i % 6] = (uint8_t)key; joy_cap = 0; settings_changed(); }
        consumed[key] = 1;
        menu_dirty = 1;
        return;
    }
    if (bas_len && key == 0x01 && !ui_active()) { bas_stop(1); consumed[key] = 1; return; }   /* 3.2.7: Esc = 넣기 멈춤 */
    int vk = key_vk(key);
    int a = vk ? find_shortcut(vk) : A_NONE;
    if (a != A_NONE) {
        consumed[key] = 1;
        if (a == A_MENU) {
            if (dialog || menu_under) { mach_from_props = 0; menu_close_all(); input_init(&in); mq_head = mq_tail = 0; menu_show(); }   /* F10 = 열려 있는 창·팝업·부메뉴를 모두 닫고 메뉴를 처음부터 다시 */
            else if (ui_active()) menu_close_all();
            else { input_init(&in); mq_head = mq_tail = 0; menu_show(); }
        } else {
            int d0 = dialog, t0 = props_tab;
            do_action(a);
            if (dialog && (dialog != d0 || (dialog == 6 && props_tab != t0))) { popup_act = a; popup_dlg = dialog; popup_tab = props_tab; }   /* 새로 뜬 팝업 = 같은 단축키로 닫을 수 있음 */
        }
        return;
    }
    if (dialog == 1) { browser_key(key); return; }
    if (dialog == 6) { props_key(key); return; }
    if (dialog == 10) { state_list_key(key); return; }
    if (dialog == 12) { help_key(key); return; }
    if (dialog == 3) { about_key(key); return; }
    if (dialog == 5) { machines_key(key); return; }
    if (dialog == 4) {
        if (grow_done) { if (!(key & 0x80)) { silence_all(); P.reboot(); } return; }   /* 4.0.28 */
        if (key == 0x01 && grow_state != 1) dialog_back();
        else if ((key == 0x1C || key == 0x9C) && grow_state == 0) { uint64_t cur_b, usb_b; if (P.grow_query && !P.grow_query(&cur_b, &usb_b)) grow_start(); }
        else menu_dirty = 1;
        return;
    }
    if (dialog == 2) { diag_key(key); return; }        /* 3.0.2 진단 탭 */
    if (dialog == 7) {                                  /* 소리 진단: T 시험음, R 다시 찾기 */
        if (key == 0x14) { tone_until = P.ms() + 1000; menu_dirty = 1; }
        else if (key == 0x13 && P.audio_select) { if (P.audio_select(audio_out, snd_status, sizeof snd_status)) P.audio_select(0, snd_status, sizeof snd_status); menu_dirty = 1; }
        else if (key == 0x22) { if (P.snd_gpio && P.snd_gpio() >= 0) tone_until = P.ms() + 1500; menu_dirty = 1; }   /* G = GPIO 값을 다음으로 돌리고 시험음 */
        else if (key == 0x01) dialog_back();
        return;
    }
    if (dialog) { if (key == 0x01 || key == 0x1C) dialog_back(); return; }
    if (bar_visible || depth) { if (!ui_pressing) menu_mouse = 0; menu_key(key); return; }
    send_msx(key, 1);
}

/* 앞 바이트 E0를 잃은 신호 바로잡기 — 방향키(E0 50)를 누르고 있는데 50만 오면 숫자판 2가 새로 눌린 것이 되어
 * 뗌(E0 D0)이 와도 숫자판 2는 계속 눌린 채 남아 목록이 계속 움직였음 (KEY_X5.0.180 기록). 짝 키가 눌려 있으면 그 키로 본다 */
static int e0_twin(int k)
{
    switch (k & 0x7F) {
    case 0x1C: case 0x1D: case 0x35: case 0x38: case 0x47: case 0x48: case 0x49: case 0x4B: case 0x4D:
    case 0x4F: case 0x50: case 0x51: case 0x52: case 0x53: return 1;
    }
    return 0;
}
static int e0_fix(int key, int press)
{
    if (!e0_twin(key)) return key;
    int tw = key ^ 0x80;
    if (press && (key & 0x80)) return key;              /* 진짜 E0 키 누름은 그대로 */
    if (!key_down[key] && key_down[tw]) {
        klog_note(key & 0x80 ? "E0가 잘못 붙은 신호 → 짝 키로" : "E0를 잃은 신호 → 짝 키(방향키 등)로");
        return tw;
    }
    return key;
}

static void process_keys(void)
{
    while (kq_tail != kq_head) {
        uint8_t c = kq[kq_tail++];
        klog_raw(c);
        if (e1) { if (--e1 == 0) { key_event(0x7F, 1); key_event(0x7F, 0); } continue; }
        if (c == 0xE1) { e1 = 5; continue; }
        if (c == 0xE0) { e0 = 1; continue; }
        if (c == 0x00 || c == 0xFF) { key_resync(); return; } /* 키보드 버퍼 넘침: 바이트를 잃었음 (2.10.1) */
        if (c == 0xFA || c == 0xFE || c == 0xEE) { e0 = 0; continue; }
        int key = (c & 0x7F) | (e0 ? 0x80 : 0);
        e0 = 0;
        if (key == 0xAA || key == 0xB6) continue;          /* E0 2A/E0 36: 가짜 Shift */
        key = e0_fix(key, !(c & 0x80));
        key_hw_ms = P.ms();
        if (!(c & 0x80)) { if (key != tm_key) { tm_key = key; tm_rep = 0; } else tm_rep = 1; }
        else if (key == tm_key) tm_key = -1;
        key_event(key, !(c & 0x80));
    }
    update_joystick();
    dg_joy_poll();                                      /* 3.8.1 */
    dg_tp_poll();                                       /* 3.8.8 */
    if (!ui_active() && run_state == RUN_ON) {
        while (mq_tail != mq_head) {
            if (input_byte(&in, M, mq[mq_tail]) == INPUT_DEFER) break;
            mq_tail++;
        }
        input_frame(&in, M);
        if (ctrl_hold) { msx_key(M, 6, 1, 1); if (!--ctrl_hold) msx_key(M, 6, 1, 0); }   /* 3.2.0 */
    }
}

/* ------------------------------------------------------------------ */
/* 디스크: 바뀐 이미지를 원래 파일에 덮어쓰기 (같은 크기)               */
/* ------------------------------------------------------------------ */
static void disk_flush(int d)
{
    if (!disk_size[d] || !M->fdc.drv[d].dirty) return;
    FdcDrive *v = &M->fdc.drv[d];
    v->dirty = 0;
    if (!disk_path[d][0]) return;                       /* 3.2.1: 폴더로 만든 디스크는 메모리에만 */
    if (is_zip(disk_path[d])) return;                   /* 4.0.34: zip에서 읽은 디스크는 저장하지 않음 */
    uint32_t lo = v->dlo & ~511u, hi = (v->dhi + 511) & ~511u;   /* 3.5.5: 바뀐 섹터 범위만 (USB 쓰기를 짧게) */
    if (hi > disk_size[d]) hi = disk_size[d];
    if (P.write_at && lo < hi && hi - lo < disk_size[d] && !P.write_at(disk_path[d], lo, disk_buf[d] + lo, hi - lo)) return;
    if (!P.write_file || P.write_file(disk_path[d], disk_buf[d], disk_size[d])) toast(T(S_DISK_SAVE_FAIL), disk_name[d]);
}

static void disk_eject(int d)
{
    disk_flush(d);
    fdc_eject(&M->fdc, d);
    disk_size[d] = 0;
    disk_path[d][0] = disk_name[d][0] = 0;
    settings_changed();
}

static void tape_flush(void)
{
    if (!M->tape || !M->tape_dirty) return;
    M->tape_dirty = 0;
    if (is_zip(tape_path)) return;                      /* 3.9.6: zip에서 읽은 테이프는 저장하지 않음 */
    if (!P.write_file || P.write_file(tape_path, tape_buf, M->tape_size)) toast(T(S_DISK_SAVE_FAIL), tape_name);
}

static void tape_eject(void)
{
    tape_title[0] = tape_start[0] = 0;
    tape_flush();
    msx_tape_insert(M, 0, 0, 0);
    tape_path[0] = tape_name[0] = 0;
}

/* 3.2.1 MED-06: .cas 안 파일 목록 — 8바이트 경계의 블록 머리(1F A6 DE BA CC 13 7D 74) 뒤 10바이트가 같은 종류 바이트면 파일 머리 */
static void tape_scan(void)
{
    tape_nfile = 0;
    if (!M->tape) return;
    static const uint8_t hd[8] = { 0x1F, 0xA6, 0xDE, 0xBA, 0xCC, 0x13, 0x7D, 0x74 };
    for (uint32_t p = 0; p + 24 <= M->tape_size && tape_nfile < TAPE_POS_N; p += 8) {
        if (memcmp(M->tape + p, hd, 8)) continue;
        const uint8_t *b = M->tape + p + 8;
        uint8_t t = b[0];
        if (t != 0xD3 && t != 0xEA && t != 0xD0) continue;
        int same = 1;
        for (int i = 1; i < 10; i++) if (b[i] != t) same = 0;
        if (!same) continue;
        int k = tape_nfile++;
        tape_fpos[k] = p; tape_ftype[k] = t;
        for (int i = 0; i < 6; i++) tape_fname[k][i] = (char)(b[10 + i] >= 32 && b[10 + i] < 127 ? b[10 + i] : '?');
        tape_fname[k][6] = 0;
        p += 16;
    }
}

/* 3.2.1 MED-07: 새 테이프 — USB\CAS\NEWTAPE_<날짜시각>.cas (빈 파일)를 만들어 넣음. CSAVE·SAVE·BSAVE로 쓰면 이 파일에 저장 */
static void tape_new(void)
{
    if (!P.create || !P.write_file) { toast(T(S_LOAD_ERROR), "CAS"); return; }
    char p[160] = FE_CAS "\\NEWTAPE", nm[64];
    int t[6];
    if (P.now && !P.now(t)) {
        str_append(p, sizeof p, "_"); num_append(p, sizeof p, (uint32_t)t[0], 4, '0'); num_append(p, sizeof p, (uint32_t)t[1], 2, '0'); num_append(p, sizeof p, (uint32_t)t[2], 2, '0');
        str_append(p, sizeof p, "_"); num_append(p, sizeof p, (uint32_t)t[3], 2, '0'); num_append(p, sizeof p, (uint32_t)t[4], 2, '0'); num_append(p, sizeof p, (uint32_t)t[5], 2, '0');
    }
    str_append(p, sizeof p, ".cas");
    tape_flush();
    if (P.create(FE_CAS, 1) || P.create(p, 0)) { toast(T(S_LOAD_ERROR), p); return; }
    copy_str(tape_path, sizeof tape_path, p);
    copy_str(nm, sizeof nm, path_base(p));
    copy_str(tape_name, sizeof tape_name, nm);
    msx_tape_insert(M, tape_buf, 0, TAPE_MAX);
    recent_add(2, p, nm);
    toast(T(S_TAPE_NEW_DONE), nm);
}

static void disks_reinsert(void)                        /* 기종을 바꾼 뒤 */
{
    for (int d = 0; d < 2; d++) if (disk_size[d]) fdc_insert(&M->fdc, d, disk_buf[d], disk_size[d], disk_wp[d]);
}

/* 특수 카트리지 꽂기. kind: 0 내셔널 디스크, 1 필립스 디스크, 2 SCC, 3 FM-PAC, 4 MSX-DOS2, 5 메모리 매퍼 512 KB.
 * quiet = 1이면 알림·리셋 없이 (마지막 상태 복원·기종 전환) */
/* 특수 카트리지 표는 src/spcat.c (이름·묶음·차례·롬 후보·만드는 법). 여기는 메뉴 만들기·꽂기만 */

/* 3.2.14 MED-10: 이름이 달라도 롬을 찾기 — Machines\Shared Roms의 파일을 SHA-1로 blueMSX 롬 DB(msxsysromdb·msxromdb)에서 찾아
 * 그 종류(type)가 맞으면 씀. 종류를 모르는 것(Nextor처럼 같은 종류가 여럿)은 이름으로만. 한 번 잰 파일은 기억. */
static const char *sp_types(int kind) { const SpCart *d = spcat_kind(kind); return d ? d->romtypes : 0; }
#define SPC_MAX 256
static struct { char name[64]; char type[24]; uint32_t size; char sha[41]; } spc[SPC_MAX];   /* 3.3.2: DB에 없는 파일도 SHA-1 기억 */
static int spc_n = -1;                                  /* -1 = 아직 안 잼 */
static FeDirEntry spc_ent[SPC_MAX];
static void sp_scan(void)
{
    if (spc_n >= 0) return;
    spc_n = 0;
    if (!P.list_dir || !P.read_file) return;
    romdb_load();
    int n = P.list_dir("0:\\Machines\\Shared Roms", spc_ent, SPC_MAX);
    uint8_t *tmp = P.scratch_size >= (10u << 20) ? P.scratch + (8u << 20) : 0;   /* 앞 10 MiB 중 뒤 2 MiB (zip과 같은 자리, 3.4.4) */
    for (int i = 0; i < n && spc_n < SPC_MAX; i++) {
        if (spc_ent[i].is_dir || !tmp) continue;
        char path[160] = "0:\\Machines\\Shared Roms\\";
        str_append(path, sizeof path, spc_ent[i].key[0] ? spc_ent[i].key : spc_ent[i].name);
        uint32_t sz = 0;
        if (P.read_file(path, tmp, MSX_ROM_MAX, &sz) || sz < 8192 || sz > MSX_ROM_MAX) continue;
        char hex[41]; sha1_hex(tmp, sz, hex);
        RomDbInfo ri;
        if (!romdb_find(sysdb_buf, sysdb_len, hex, &ri) || !ri.type[0]) { if (!romdb_find(romdb_buf, romdb_len, hex, &ri) || !ri.type[0]) ri.type[0] = 0; }
        copy_str(spc[spc_n].sha, sizeof spc[spc_n].sha, hex);
        copy_str(spc[spc_n].name, sizeof spc[spc_n].name, spc_ent[i].key[0] ? spc_ent[i].key : spc_ent[i].name);
        copy_str(spc[spc_n].type, sizeof spc[spc_n].type, ri.type);
        spc[spc_n].size = sz;
        spc_n++;
    }
}
/* 3.3.2 ACC-04: 기종 롬 SHA-1 기억 (Databases\x86msx_romsha.txt, 한 줄 "sha1 파일이름").
 * 기종을 켤 때 읽은 롬의 SHA-1을 적어 두고, 나중에 그 이름의 파일이 없으면 Shared Roms에서 같은 SHA-1인 파일(이름이 달라도)을 씀 */
#define RSH_MAX 128
#define RSH_PATH "0:\\Databases\\x86msx_romsha.txt"
static char rsh_name[RSH_MAX][64], rsh_sha[RSH_MAX][41];
static int rsh_n = -1;
static int name_ieq(const char *a, const char *b) { while (*a && *b && lower_c(*a) == lower_c(*b)) { a++; b++; } return !*a && !*b; }
static void rsh_load(void)
{
    if (rsh_n >= 0 || !P.read_file) return;
    rsh_n = 0;
    static char t[RSH_MAX * 112];
    uint32_t n = 0;
    if (P.read_file(RSH_PATH, (uint8_t *)t, sizeof t - 1, &n)) return;
    t[n < sizeof t ? n : sizeof t - 1] = 0;
    for (char *l = t; *l && rsh_n < RSH_MAX; ) {
        char *e = l; while (*e && *e != '\n') e++;
        char sv = *e; *e = 0;
        int k = 0; while (k < 40 && ((l[k] >= '0' && l[k] <= '9') || (l[k] >= 'a' && l[k] <= 'f'))) k++;
        if (k == 40 && l[40] == ' ') {
            memcpy(rsh_sha[rsh_n], l, 40); rsh_sha[rsh_n][40] = 0;
            char *nm = l + 41; int m = 0;
            while (nm[m] && nm[m] != '\r' && m < 63) { rsh_name[rsh_n][m] = nm[m]; m++; }
            rsh_name[rsh_n][m] = 0;
            if (m) rsh_n++;
        }
        *e = sv; l = *e ? e + 1 : e;
    }
}
static void rsh_save(void)
{
    if (!P.create || !P.write_file) return;
    static char t[RSH_MAX * 112];
    t[0] = 0;
    for (int i = 0; i < rsh_n; i++) { str_append(t, sizeof t, rsh_sha[i]); str_append(t, sizeof t, " "); str_append(t, sizeof t, rsh_name[i]); str_append(t, sizeof t, "\r\n"); }
    P.create("0:\\Databases", 1);
    if (!P.create(RSH_PATH, 0)) P.write_file(RSH_PATH, (const uint8_t *)t, (uint32_t)strlen(t));
}
static void sp_scan(void);
static int rom_find_cb(const char *base, uint8_t *buf, uint32_t max, uint32_t *size)
{
    if (!P.read_file) return -1;
    rsh_load();
    int k = -1;
    for (int i = 0; i < rsh_n; i++) if (name_ieq(rsh_name[i], base)) k = i;
    if (k < 0) return -1;
    sp_scan();
    for (int i = 0; i < spc_n; i++)
        if (!strcmp(spc[i].sha, rsh_sha[k])) {
            char path[160] = "0:\\Machines\\Shared Roms\\";
            str_append(path, sizeof path, spc[i].name);
            return P.read_file(path, buf, max, size);
        }
    return -1;
}
static void rom_seen_cb(const char *base, const uint8_t *d, uint32_t n)
{
    if (!P.read_file || n < 1024) return;
    rsh_load();
    char hex[41]; sha1_hex(d, n, hex);
    int k = -1;
    for (int i = 0; i < rsh_n; i++) if (name_ieq(rsh_name[i], base)) k = i;
    if (k >= 0 && !strcmp(rsh_sha[k], hex)) return;
    if (k < 0) { if (rsh_n >= RSH_MAX) return; k = rsh_n++; copy_str(rsh_name[k], sizeof rsh_name[k], base); }
    copy_str(rsh_sha[k], sizeof rsh_sha[k], hex);
    rsh_save();
}

static int type_in(const char *t, const char *list)
{
    for (const char *p = list; *p; ) {
        const char *q = p; while (*q && *q != '|') q++;
        if ((size_t)(q - p) == strlen(t) && !memcmp(p, t, (size_t)(q - p))) return 1;
        p = *q ? q + 1 : q;
    }
    return 0;
}
/* 종류가 맞는 롬 하나의 경로 (없으면 0) */
static int sp_sha_path(int kind, char *path, int max)
{
    const char *tl = sp_types(kind);
    if (!tl) return 0;
    sp_scan();
    for (int i = 0; i < spc_n; i++)
        if (type_in(spc[i].type, tl)) { copy_str(path, max, "0:\\Machines\\Shared Roms\\"); str_append(path, max, spc[i].name); return 1; }
    return 0;
}
/* 메뉴에서 흐리게: 롬이 필요한데 이름으로도 SHA-1로도 없으면 0 (한 번 재고 기억) */
static int8_t sp_avail[128];
static int sp_has_rom(int kind)
{
    const SpCart *sd = spcat_kind(kind);
    if (!sd || !sd->files || kind >= 128) return 1;
    if (sp_avail[kind]) return sp_avail[kind] > 0;
    int ok = 0;
    static FeDirEntry e1[1];
    for (const char *f = sd->files; *f && !ok; ) {
        char one[96]; int k = 0;
        while (*f && *f != '|' && k < (int)sizeof one - 1) one[k++] = *f++;
        one[k] = 0;
        if (*f == '|') f++;
        char path[160] = "0:\\Machines\\"; str_append(path, sizeof path, one);
        uint32_t sz = 0;
        ok = P.read_file && !P.read_file(path, (uint8_t *)e1, 1, &sz);    /* 1바이트만 읽어 있는지 */
    }
    if (!ok) { char p2[160]; ok = sp_sha_path(kind, p2, sizeof p2); }
    sp_avail[kind] = (int8_t)(ok ? 1 : -1);
    return ok;
}
static int sp_code(int kind) { return kind == 0 ? 64 : kind == 1 ? 65 : 100 + kind; }
static uint8_t sp_ff[16384];                            /* PAC: 롬 자리 (0xFF) */

static int special_apply(int n, int kind, int quiet)
{
    const SpCart *d = spcat_kind(kind);
    if (n < 0 || n >= MSX_CART_SLOTS || M->cart_slot[n] < 0 || !d || d->how == SPK_NOT_YET) return -1;
    if (!quiet && cart_special[n] && cart_special[n] == sp_code(kind)) {
        menu_close_all();                               /* 2.14: 같은 특수 카트리지면 그대로 (리셋 안 함) */
        toast(T(S_SAME_CART), d->name);
        return 0;
    }
    uint32_t size = 0;
    char path[160] = "";
    if (d->files) {                                     /* 후보 롬을 앞에서부터 찾음 */
        int ok = 0;
        const char *f = d->files;
        while (*f && !ok) {
            char one[96]; int k = 0;
            while (*f && *f != '|' && k < (int)sizeof one - 1) one[k++] = *f++;
            one[k] = 0;
            if (*f == '|') f++;
            copy_str(path, sizeof path, "0:\\Machines\\");
            str_append(path, sizeof path, one);
            ok = P.read_file && !P.read_file(path, cart_buf[n], MSX_ROM_MAX, &size) && size && size <= MSX_ROM_MAX;
        }
        if (!ok && sp_sha_path(kind, path, sizeof path))    /* 3.2.14: 이름이 달라도 SHA-1로 */
            ok = P.read_file && !P.read_file(path, cart_buf[n], MSX_ROM_MAX, &size) && size && size <= MSX_ROM_MAX;
        if (!ok) {
            if (!quiet) {                               /* 첫 후보 이름을 알림 */
                char first[96]; int k = 0;
                for (const char *q = d->files; *q && *q != '|' && k < (int)sizeof first - 1; q++) first[k++] = *q;
                first[k] = 0;
                char t[120] = "Machines\\"; str_append(t, sizeof t, first);
                toast(T(S_LOAD_ERROR), t);
            }
            return -1;
        }
    }
    if (M->cart[n].present && CART_IS_IDE(M->cart[n].mapper) && d->how != SPK_IDE && d->how != SPK_BEER && d->how != SPK_GIDE && d->how != SPK_GOUDA && d->how != SPK_MEGASCSI && d->how != SPK_WAVESCSI) hd_eject();   /* IDE 카트리지를 빼면 하드 디스크도 */
    cart_remove(n);
    int r = -1;
    int ext = ext_insert(d->how, n, kind, quiet, &size, &r);   /* 외부장치 표 (fe_extdev.inc) */
    if (ext && r == EXT_ABORT) return -1;
    if (!ext)
    switch (d->how) {
    case SPK_DISK: r = msx_insert_disk_interface(M, n, cart_buf[n], size, d->arg); break;
    case SPK_SCC: size = 131072; memset(cart_buf[n], 0xFF, size); r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_KONAMI_SCC); break;
    case SPK_FMPAC: r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_FMPAC); break;
    case SPK_DOS2: r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_DOS2); break;
    case SPK_SCCI: size = 131072; memset(cart_buf[n], 0, size); r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_SCCPLUS); break;
    case SPK_IDE: r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_SUNRISE); break;
    case SPK_VHD:                                       /* 3.7.1: Nextor 커널 + X86MSX 드라이버 */
        if (vhd_build_rom(cart_buf[n], &size)) { if (!quiet) toast(T(S_VHD_NOROM), path); return -1; }
        for (int k = 0; k < MSX_CART_SLOTS; k++) if (k != n && M->cart[k].present && M->cart[k].mapper == MAPPER_VHD) cart_remove(k);   /* 하나만 */
        r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_VHD);
        if (!r) vhd_mount_all(quiet);
        break;
    case SPK_NET: r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_NET); break;               /* 5.0.57 */
    case SPK_BEER: r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_BEERIDE); break;            /* 3.4.1 */
    case SPK_SFG: r = (size == 16384 || size == 32768) ? msx_insert_cart(M, n, cart_buf[n], size, MAPPER_SFG) : -1; break;
    case SPK_GOUDA: {                                   /* 3.4.1: Novaxis BIOS 고침 (blueMSX와 같은 두 곳) */
        static const uint8_t id15964[16] = { 0x4B,0x4D,0x63,0x73,0x02,0x01,0x59,0xB0,0x34,0x64,0x00,0x37,0x00,0x00,0x00,0x00 };
        static const uint8_t bug[5] = { 0xC1,0x16,0x02,0xC1,0xC9 };
        uint8_t *rb = cart_buf[n];
        if (size != 16384) break;
        if (!memcmp(rb + 0x3FF0, id15964, 16) && !memcmp(rb + 0x91C, bug, 5)) rb[0x91F] = 0;   /* 1.59.64: 스택 어긋남 */
        for (uint32_t i = 0; i + 2 < 0x3FF0; i++)       /* CALL F365h → IN A,(A8h) : NOP (설정 메뉴) */
            if (rb[i] == 0xCD && rb[i + 1] == 0x65 && rb[i + 2] == 0xF3) { rb[i] = 0xDB; rb[i + 1] = 0xA8; rb[i + 2] = 0x00; i += 2; }
        r = msx_insert_cart(M, n, rb, size, MAPPER_GOUDA);
        break;
    }
    case SPK_GIDE: size = 16384; memset(cart_buf[n], 0xFF, size); r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_GIDE); break;
    case SPK_ESESCC: size = (uint32_t)d->arg * 1024; memset(cart_buf[n], 0xFF, size); r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_ESESCC); break;
    case SPK_MEGASCSI: size = (uint32_t)d->arg * 1024; memset(cart_buf[n], 0xFF, size); r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_MEGASCSI); break;   /* 3.9.14: SRAM 내용은 USB\SRAM\MegaSCSI_<크기>KB.sram */
    case SPK_WAVESCSI: size = (uint32_t)d->arg * 1024; memset(cart_buf[n], 0xFF, size); r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_WAVESCSI); break;
    case SPK_PAC: memset(sp_ff, 0xFF, sizeof sp_ff); size = sizeof sp_ff; r = msx_insert_cart(M, n, sp_ff, size, MAPPER_PAC); break;
    case SPK_RAM: size = (uint32_t)d->arg * 1024; memset(cart_buf[n], 0, size); r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_RAM); break;
    case SPK_MEGARAM: {                                 /* 1 MB까지는 카트리지 버퍼, 2 MB는 외장 매퍼 버퍼(쓰지 않을 때) */
        size = (uint32_t)d->arg * 1024;
        uint8_t *buf = cart_buf[n];
        if (size > MSX_ROM_MAX) {
            buf = P.xram;
            if (!buf || M->xram_size) { if (!quiet) toast(T(S_MAPPER_EXISTS), 0); return -1; }
        }
        memset(buf, 0xFF, size);
        r = msx_insert_cart(M, n, buf, size, MAPPER_MEGARAM);
        break;
    }
    case SPK_ESERAM:                                    /* 3.1.1: 배터리 SRAM (USB\\SRAM에 저장) */
        size = (uint32_t)d->arg * 1024; memset(cart_buf[n], 0xFF, size);
        r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_ESERAM);
        break;
    case SPK_MFRP:                                      /* Mega Flash ROM SCC+ — 플래시 1 MiB (지운 상태 = 0xFF), SRAM\MegaFlashROM_SCCplus.flash */
        size = 1024u * 1024; memset(cart_buf[n], 0xFF, size);
        r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_MFRSCCP);
        break;
    case SPK_MFR:                                       /* 3.1.1: 플래시 512 KiB (지운 상태 = 0xFF) */
        size = 512u * 1024; memset(cart_buf[n], 0xFF, size);
        r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_MFRSCC);
        break;
    case SPK_FLASH: {                                   /* 빈 플래시 카트리지 (ASCII16-X·Yamanooto, 8 MiB 칩) — 2 MiB 슬롯 버퍼에 안 들어가 큰 버퍼(cart_big) 하나를 씀 */
        size = msx_cart_alloc_size((MapperType)d->arg, 0);
        uint8_t *buf = cart_buf[n];
        if (size > MSX_ROM_MAX) {
            if (!cart_big || cart_big_size < size || (cart_big_owner >= 0 && cart_big_owner != n && M->cart[cart_big_owner].present)) { if (!quiet) toast(T(S_LOAD_ERROR), d->name); return -1; }   /* 다른 슬롯이 큰 버퍼를 씀 */
            buf = cart_big;
        }
        memset(buf, 0xFF, size);
        r = msx_insert_cart(M, n, buf, size, (MapperType)d->arg);
        if (!r && buf == cart_big) cart_big_owner = n;
        break;
    }
    case SPK_RS232:                                     /* Philips NMS1210 RS-232C — 메모리 창 없음, I/O 80h~87h (BIOS 는 디스크에서) */
        size = 16384; memset(cart_buf[n], 0xFF, size);
        r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_RS232);
        break;
    case SPK_HBI55:                                     /* Sony HBI-55 — 메모리 창 없음, I/O B0h~B3h + SRAM 4 KiB (SRAM\HBI-55.sram) */
        size = 16384; memset(cart_buf[n], 0xFF, size);
        r = msx_insert_cart(M, n, cart_buf[n], size, MAPPER_HBI55);
        break;
    default:
        M->xram = P.xram;                               /* 내장 매퍼가 있는 기종은 이 버퍼로 두 번째 매퍼 (2.12) */
        r = msx_insert_mapper_ram(M, n, d->arg);
        if (r && !quiet) { toast(T(S_MAPPER_EXISTS), 0); return -1; }
        break;
    }
    if (r) { if (!quiet) toast(T(S_LOAD_ERROR), d->name); return -1; }
    cart_special[n] = sp_code(kind);
    cart_size[n] = size;
    if (d->how == SPK_FMPAC || d->how == SPK_PAC || d->how == SPK_ESERAM || d->how == SPK_MFR || d->how == SPK_ESESCC || d->how == SPK_CA1 || d->how == SPK_FLASH || d->how == SPK_HBI55 || d->how == SPK_MFRP) sram_load(n);     /* 배터리 백업 (2.11), 3.1.1 Ese-RAM·플래시 */
    cart_mapper[n] = -1;
    copy_str(cart_path[n], sizeof cart_path[n], d->files ? path : "");
    copy_str(cart_name[n], sizeof cart_name[n], d->name);
    disks_reinsert();
    if (!quiet) {
        menu_close_all();
        after_cart_insert(n);
        settings_changed();
    }
    return 0;
}

static int insert_special(int n, int kind) { return special_apply(n, kind, 0); }
/* GT 처럼 MIDI 단자가 든 기종이 아니면, MIDI 기계를 꽂을 때 X86MSX MIDI 카트리지(MSX-MIDI 호환)를 자동으로 꽂음.
 * 자리 = 기종의 마지막 슬롯, 거기 뭐가 있으면 그 앞의 빈 슬롯 (free_cart_slot). 이미 MIDI 단자(SFG·Music Module·외장 MSX-MIDI)가 있으면 안 꽂음.
 * 기계를 모두 빼면 자동으로 꽂았던 카트리지는 뺌 */
static void x86midi_check(int quiet)
{
    if (x86midi_auto >= 0 && (!M->cart[x86midi_auto].present || cart_special[x86midi_auto] != sp_code(67))) x86midi_auto = -1;   /* 사용자가 뺐거나 바꿈 */
    if (x86midi_auto < 0) for (int k = 0; k < MSX_CART_SLOTS; k++) if (M->cart[k].present && cart_special[k] == sp_code(67)) x86midi_auto = k;   /* 기종 환경에서 되살아난 것 */
    if (!midi_bus.n) {
        if (x86midi_auto >= 0) { cart_remove(x86midi_auto); x86midi_auto = -1; msx_set_midi(M, midi_io_on()); msx_io_rebuild(M); settings_changed(); }
        return;
    }
    if (msx_dev(M, MDEV_MIDI_HW) || msx_midi_jacks(M)) return;  /* GT·이미 MIDI 단자가 있음 */
    int k = free_cart_slot();
    if (k < 0) { if (!quiet) toast(T(S_X86MIDI_NOSLOT), 0); return; }
    if (special_apply(k, 67, 1)) return;
    x86midi_auto = k;
    settings_changed();
    if (!quiet) { char t[24] = "Slot "; char d[4] = { (char)('1' + k), 0 }; str_append(t, sizeof t, d); toast(T(S_X86MIDI_AUTO), t); }
}
static int special_kind(int code) { return code == 64 ? 0 : code == 65 ? 1 : code >= 102 && spcat_kind(code - 100) ? code - 100 : -1; }

#include "fe_state.inc"
#include "fe_video.inc"
#include "fe_sys.inc"
#include "fe_vhd.inc"                                  /* 3.7.1 가상 하드 */
#include "fe_basic.inc"
#include "fe_prnview.inc"
#include "fe_prn.inc"
#include "fe_edit.inc"                                  /* 4.0.29 텍스트 편집기 */
#include "fe_net.inc"                                   /* 5.0.57 네트워크 설정 창 (창 18) */
#include "fe_langwin.inc"                               /* 5.2.192 언어 · 글꼴 창 (창 20) */
#include "fe_midimix.inc"                               /* 5.2.53 MIDI 믹서 창 (창 19) */
#include "fe_romsets.inc"                              /* 5.2.171 MT-32·SC-55 롬 묶음 찾기 */
#include "fe_miditab.inc"                               /* 5.2.54 속성 ▶ MIDI 탭 새 디자인 */

/* ------------------------------------------------------------------ */
/* 기종별 실행 환경 (요구 8): 슬롯 확장, 특수 카트리지 (하드 디스크는 2.8)   */
/* ------------------------------------------------------------------ */
typedef struct {
    char machine[64]; int expand; int special[MSX_CART_SLOTS];
    int has_cmos; uint8_t rtc[3][13], s1985[16];        /* 배터리 백업 내용 (RP5C01 블록 1~3, S1985 RAM) */
    char hd[FE_PATH_MAX];                               /* 하드 디스크 이미지 경로 (2.8) */
    char hd2[FE_PATH_MAX];                                     /* 3.4.3: 슬레이브 */
    int mport;                                          /* 3.4.3 MOUSE-05: MSX 마우스 포트 + 1 (0 = 정하지 않음). ini에는 expand + 10 × mport */
    int mflags;                                         /* 3.4.4: MSX 마우스 X 반전(1)·Y 반전(2)·수동 잠금(4), 8 = 저장됨. ini에는 + 100 × mflags */
} Env;
static Env envs[8];
static int nenv;

static Env *env_find(const char *name, int create)
{
    for (int e = 0; e < nenv; e++) if (!strcmp(envs[e].machine, name)) return &envs[e];
    if (!create) return 0;
    if (nenv < 8) { memset(&envs[nenv], 0, sizeof envs[0]); copy_str(envs[nenv].machine, sizeof envs[0].machine, name); return &envs[nenv++]; }
    memmove(&envs[0], &envs[1], sizeof envs[0] * 7);    /* 가장 오래된 것을 밀어냄 */
    memset(&envs[7], 0, sizeof envs[0]);
    copy_str(envs[7].machine, sizeof envs[0].machine, name);
    return &envs[7];
}

static void env_save_current(void)
{
    Env *e = env_find(machine_name, 1);
    e->expand = slot_expand;
    e->mport = mouse_port + 1;
    e->mflags = 8 | (mouse_invx ? 1 : 0) | (mouse_invy ? 2 : 0) | (mouse_lock ? 4 : 0);   /* 3.4.4 */
    for (int n = 0; n < MSX_CART_SLOTS; n++) e->special[n] = cart_special[n];
    copy_str(e->hd, sizeof e->hd, hd_secs ? hd_path : "");
    copy_str(e->hd2, sizeof e->hd2, hd2_secs ? hd2_path : "");
}

/* 지금 기종의 환경을 적용 (특수 카트리지 다시 꽂기). 일반 롬팩은 모두 빠진 상태가 된다 */
static void env_apply(void)
{
    Env *e = env_find(machine_name, 0);
    if (e && e->has_cmos) {                             /* 배터리 백업 내용 되살림 (아이큐2000 SET SYSTEM 등) */
        if (M->rtc_on && msx_dev(M, MDEV_CMOS_BAT)) memcpy(M->rtc[1], e->rtc, sizeof e->rtc);
        if (M->s1985) memcpy(M->s1985_ram, e->s1985, sizeof e->s1985);
    }
    slot_expand = e ? e->expand : 0;
    if (e && e->mport) mouse_port = e->mport - 1;      /* 3.4.3: 기종별 마우스 포트 */
    else if (!e && msx_dev(M, MDEV_TRPAUSE)) mouse_port = 1;    /* 4.0: turbo R은 MSX-View가 마우스를 쓰므로 처음엔 포트 1 (바꾼 적 있으면 저장한 값) */
    if (e && (e->mflags & 8)) { mouse_invx = e->mflags & 1; mouse_invy = (e->mflags >> 1) & 1; mouse_lock = (e->mflags >> 2) & 1; }   /* 3.4.4 */
    if (msx_set_expand(M, slot_expand)) { slot_expand = 0; msx_set_expand(M, 0); }
    for (int n = 0; n < MSX_CART_SLOTS; n++) {
        if (M->cart[n].present) cart_remove(n);
        cart_special[n] = 0; cart_path[n][0] = 0; cart_name[n][0] = 0; cart_mapper[n] = -1; cart_size[n] = 0;
        int k = e ? special_kind(e->special[n]) : -1;
        if (k >= 0) special_apply(n, k, 1);
    }
    hd_eject();
    if (e && e->hd[0]) {                                /* 하드 디스크 (2.8): 기종 환경으로 되살림 */
        const char *nm = e->hd;
        for (const char *q = e->hd; *q; q++) if (*q == '\\') nm = q + 1;
        hd_insert(e->hd, nm, 1);
        if (e->hd2[0]) { const char *n2 = e->hd2; for (const char *q = e->hd2; *q; q++) if (*q == '\\') n2 = q + 1; hd2_insert(e->hd2, n2, 1); }
    }
}

static void env_parse(const char *v)
{
    if (nenv >= 8) return;
    Env *e = &envs[nenv];
    memset(e, 0, sizeof *e);
    int f = 0, k = 0;
    for (const char *p = v; ; p++) {
        if (*p == '|' || !*p) {
            if (f == 0) e->machine[k < 63 ? k : 63] = 0;
            f++; k = 0;
            if (!*p) break;
            continue;
        }
        if (f == 0) { if (k < 63) e->machine[k++] = *p; }
        else if (f == 1) e->expand = e->expand * 10 + (*p - '0');
        else if (f - 2 < 4) e->special[f - 2] = e->special[f - 2] * 10 + (*p - '0');   /* 카트리지 1~4 (자리 번호는 3.6.6까지와 같음) */
        else if (f == 6 && k < (int)sizeof e->hd - 1) { e->hd[k++] = *p; e->hd[k] = 0; }   /* 하드 디스크 경로 */
        else if (f == 7 && k < (int)sizeof e->hd2 - 1) { e->hd2[k++] = *p; e->hd2[k] = 0; }   /* 3.4.3 슬레이브 */
        else if (f == 8) e->special[4] = e->special[4] * 10 + (*p - '0');   /* 3.6.7: 카트리지 5 (맨 뒤) */
    }
    e->mport = e->expand / 10 % 10; e->mflags = e->expand / 100 % 16; e->expand %= 10;   /* 3.4.3, 3.4.4 */
    if (e->machine[0]) nenv++;
}

static int hex_val(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0; }

/* 배터리 백업 내용: 지금 기계의 값을 환경에 담음. 바뀌었으면 1 */
static int cmos_backed(void) { return (M->rtc_on && msx_dev(M, MDEV_CMOS_BAT)) || M->s1985; }
static int cmos_capture(void)
{
    if (!cmos_backed()) return 0;
    Env *e = env_find(machine_name, 1);
    uint8_t rtc[3][13], sr[16];
    memset(rtc, 0, sizeof rtc); memset(sr, 0, sizeof sr);
    if (M->rtc_on && msx_dev(M, MDEV_CMOS_BAT)) memcpy(rtc, M->rtc[1], sizeof rtc);
    if (M->s1985) memcpy(sr, M->s1985_ram, sizeof sr);
    if (e->has_cmos && !memcmp(rtc, e->rtc, sizeof rtc) && !memcmp(sr, e->s1985, sizeof sr)) return 0;
    if (!e->has_cmos) {                                 /* 처음: 기본 환경도 지금 값으로 */
        e->expand = slot_expand;
        for (int n = 0; n < MSX_CART_SLOTS; n++) e->special[n] = cart_special[n];
    }
    e->has_cmos = 1;
    memcpy(e->rtc, rtc, sizeof rtc); memcpy(e->s1985, sr, sizeof sr);
    return 1;
}
/* "Cmos1=기종|RTC 39자(16진 한 자리씩)|S1985 32자" */
static void cmos_parse(const char *v)
{
    const char *bar = 0;
    for (const char *p = v; *p; p++) if (*p == '|') { bar = p; break; }
    if (!bar || bar - v >= 64) return;
    char name[64]; int k = (int)(bar - v);
    memcpy(name, v, (size_t)k); name[k] = 0;
    Env *e = env_find(name, 1);
    const char *p = bar + 1;
    for (int i = 0; i < 39 && *p && *p != '|'; i++, p++) e->rtc[i / 13][i % 13] = (uint8_t)hex_val(*p);
    if (*p == '|') p++;
    for (int i = 0; i < 16 && p[0] && p[1]; i++, p += 2) e->s1985[i] = (uint8_t)(hex_val(p[0]) << 4 | hex_val(p[1]));
    e->has_cmos = 1;
}

/* ------------------------------------------------------------------ */
/* 설정과 마지막 상태: 0:\x86msx.ini (4 KiB 고정, 같은 자리 덮어쓰기). 없으면 예전 이름 msxemulator.ini */
/* ------------------------------------------------------------------ */
static const char *ini_path = "0:\\x86msx.ini";
#define INI_PATH ini_path
#define INI_SIZE 65536                           /* 3.4.7: 긴 경로(최근 파일 24개 등) */
static int settings_dirty;
static uint32_t settings_due;
static uint8_t ini_buf[INI_SIZE];
static uint8_t ini_prev[INI_SIZE];                     /* 3.5.5: 장치에 있는 설정 파일 내용 (바뀐 섹터만 쓰기) */
static int ini_prev_ok;

static void settings_changed(void) { settings_dirty = 1; settings_due = P.ms() + 2000; }

static void ini_put(char *b, const char *key, const char *val)
{
    str_append(b, INI_SIZE, key);
    str_append(b, INI_SIZE, "=");
    str_append(b, INI_SIZE, val);
    str_append(b, INI_SIZE, "\n");
}
static void ini_num(char *b, const char *key, int v)
{
    char t[16] = "";
    if (v < 0) { str_append(t, sizeof t, "-"); v = -v; }
    num_append(t, sizeof t, (uint32_t)v, 0, ' ');
    ini_put(b, key, t);
}

static void midi_path(char *out, int max, int k, const char *name) { copy_str(out, max, midi_dir(k)); str_append(out, max, "\\"); str_append(out, max, name); }
/* 3.4.5 SND3-07: 소프트웨어 MIDI 음원. SoundFont는 USB\MidiSound\SoundFont\*.sf2를 (5.2.61) 영상 녹화 버퍼 끝(1/4, 64 MiB까지)에 읽음 */
static int sf_reserve(FeSf *f)
{
    if (f->mem) return 0;
    bgw_finish();                                       /* 영상 저장 중이면 마저 (같은 버퍼) */
    if (!P.vbuf || vrec_on) return -1;
    uint32_t cap = P.vbuf_size / 4;
    if (cap > (64u << 20)) cap = 64u << 20;
    if (cap < (8u << 20)) return -1;
    P.vbuf_size -= cap;                                 /* 되감기·영상 녹화는 남은 자리로 */
    f->mem = P.vbuf + P.vbuf_size; f->cap = cap;
    return 0;
}
static int sf_rd_slot;
static int sf_stream_rd0(uint32_t off, uint8_t *buf, uint32_t len) { return P.read_at ? P.read_at(fe_sf[0].path, off, buf, len) : -1; }
static int sf_stream_rd1(uint32_t off, uint8_t *buf, uint32_t len) { return P.read_at ? P.read_at(fe_sf[1].path, off, buf, len) : -1; }
static int is_sf2(const char *nm) { int l = (int)strlen(nm); return l > 4 && nm[l - 4] == '.' && (nm[l - 3] | 32) == 's' && (nm[l - 2] | 32) == 'f' && nm[l - 1] == '2'; }
static int sf_is_mt32(const char *nm)
{
    for (; *nm; nm++) if ((nm[0] | 32) == 'm' && (nm[1] | 32) == 't' && (nm[2] == '3' || (nm[2] == '-' && nm[3] == '3'))) return 1;
    return 0;
}
static int sf_has_gm(void)                              /* MT-32 용이 아닌 .sf2 가 폴더에 있나 */
{
    static FeDirEntry se[64];
    if (!P.list_dir) return 0;
    int n = P.list_dir(midi_dir(MD_SF), se, 64);
    for (int i = 0; i < n; i++) { const char *nm = se[i].key[0] ? se[i].key : se[i].name; if (!se[i].is_dir && is_sf2(nm) && !sf_is_mt32(nm)) return 1; }
    return 0;
}
static void sf_load_slot(int slot, int quiet)
{
    FeSf *f = &fe_sf[slot];
    (void)sf_rd_slot;
    gm_sf_slot(slot); gm_unload_sf2();
    f->size = 0;
    if (!P.list_dir || !P.read_file) { f->state = -1; return; }
    if (!f->name[0]) {                                  /* 이름이 없으면 첫 .sf2 (칸 1 은 이름 있을 때만 씀) */
        if (slot) { f->state = -1; return; }
        static FeDirEntry se[64];
        int n = P.list_dir(midi_dir(MD_SF), se, 64);
        for (int i = 0; i < n && !f->name[0]; i++) {
            const char *nm = se[i].key[0] ? se[i].key : se[i].name;
            if (!se[i].is_dir && is_sf2(nm) && !sf_is_mt32(nm)) copy_str(f->name, sizeof f->name, nm);   /* MT-32 용 SoundFont 는 자동으로 고르지 않음 (번개·munt 같은 소리가 GM 기본으로 나오던 원인) */
        }
        if (!f->name[0]) { f->state = -1; return; }
    }
    if (sf_reserve(f)) { f->state = -4; if (!quiet) toast(T(S_SF_NOMEM), 0); return; }
    char path[FE_PATH_MAX];
    midi_path(path, sizeof path, MD_SF, f->name);
    uint32_t n = 0;
    gm_sf_slot(slot);
    {   /* 4.0.133: 64 MiB 넘는 큰 SoundFont는 악기 목록만 읽고 샘플은 소리 낼 때 읽음 (자리 128 MiB까지) */
        uint8_t hb[16]; uint32_t fsz = 0;
        if (!P.read_file(path, hb, 12, &fsz) && fsz > (64u << 20) && P.read_at) {
            if (f->cap < (128u << 20) && !vrec_on && P.vbuf_size >= (192u << 20) && f->mem == P.vbuf + P.vbuf_size) { P.vbuf_size -= 64u << 20; f->mem -= 64u << 20; f->cap += 64u << 20; }
            copy_str(f->path, sizeof f->path, path);
            if (gm_load_sf2_stream(slot ? sf_stream_rd1 : sf_stream_rd0, fsz, f->mem, f->cap)) { f->state = -3; if (!quiet) toast(T(S_SF_BAD), f->name); return; }
            f->size = fsz; f->state = 1;
            if (!quiet) toast(T(S_SF_LOADED), f->name);
            return;
        }
    }
    if (P.read_file(path, f->mem, f->cap, &n) || n < 12) {
        static int retry;                               /* 3.6.2: 저장된 이름의 파일이 없으면 폴더의 첫 .sf2로 (칸 0) */
        if (!retry && !slot) { retry = 1; f->name[0] = 0; sf_load_slot(slot, quiet); retry = 0; return; }
        f->state = -1; if (!quiet) toast(T(S_LOAD_ERROR), f->name); return;
    }
    uint32_t want = 8 + ((uint32_t)f->mem[4] | (uint32_t)f->mem[5] << 8 | (uint32_t)f->mem[6] << 16 | (uint32_t)f->mem[7] << 24);
    if (want > f->cap) { f->state = -2; if (!quiet) toast(T(S_SF_BIG), 0); return; }
    if (gm_load_sf2(f->mem, n)) { f->state = -3; if (!quiet) toast(T(S_SF_BAD), f->name); return; }
    f->size = n; f->state = 1;
    if (!quiet) toast(T(S_SF_LOADED), f->name);
}
static int sf_presets(int slot) { gm_sf_slot(slot); return gm_sf2_presets(); }
/* Roland SC-55 (Nuked-SC55) — USB\MidiSound\SC55\ 의 롬. SC-55mkII (rom1·rom2·waverom1·waverom2·rom_sm.bin) 를 먼저, 없으면 SC-55 (sc55_rom1·2·waverom1·2·3.bin) */
static void sc55_load(int quiet)
{
    sc55w_close(); sc55_info[0] = 0;
    if (!sc55w_tmp()) { sc55_state = -9; return; }        /* 레거시 판에는 없음 */
    if (!P.read_file) { sc55_state = -1; return; }
    int model = -1;
    for (int m = 0; m < 2 && model < 0; m++) {
        char path[FE_PATH_MAX]; uint8_t hb[4]; uint32_t fs = 0;
        if (sc55_set && m != sc55_set - 1) continue;      /* MIDI 탭에서 고른 묶음 (0 = 자동: mkII 먼저) */
        midi_path(path, sizeof path, MD_SC55, sc55w_files[m][0]);
        if (!P.read_file(path, hb, 1, &fs) && fs) model = m;
    }
    if (model < 0) { sc55_state = -1; if (!quiet) toast(nt("SC-55 롬이 없습니다", "SC-55 ROM not found", "SC-55 ROMがありません"), "USB\\MidiSound\\SC55"); return; }
    for (int f = 0; f < SC55_NFILE; f++) {
        char path[FE_PATH_MAX]; uint32_t n = 0;
        midi_path(path, sizeof path, MD_SC55, sc55w_files[model][f]);
        if (P.read_file(path, sc55w_tmp(), SC55_TMP_SIZE, &n)) { sc55_state = -1; if (!quiet) toast(nt("SC-55 롬 파일이 없습니다", "SC-55 ROM file missing", "SC-55 ROMファイルなし"), sc55w_files[model][f]); return; }
        if (sc55w_load(model, f, n)) { sc55_state = -2; if (!quiet) toast(nt("SC-55 롬 크기가 맞지 않습니다", "SC-55 ROM size mismatch", "SC-55 ROMサイズ不一致"), sc55w_files[model][f]); return; }
    }
    if (sc55w_start(model)) { sc55_state = -3; return; }
    copy_str(sc55_info, sizeof sc55_info, model == SC55_MK2 ? "SC-55mkII" : "SC-55");
    str_append(sc55_info, sizeof sc55_info, nt(" 롬", " ROM", " ROM"));
    sc55_state = 1;
    if (!quiet) toast(nt("SC-55 롬을 읽었습니다", "SC-55 ROM loaded", "SC-55 ROMを読み込みました"), sc55_info);
}
/* Roland MT-32 / CM-32L — USB\MT32\ 의 롬(MT32_CONTROL.ROM + MT32_PCM.ROM, 또는 CM32L_CONTROL.ROM + CM32L_PCM.ROM)을 읽어 엔진을 엶 */
static int mt_dev_n(int slot)                          /* 슬롯 번째 MT-32/CM-32L 기계의 자리 (없으면 -1) */
{
    for (int i = 0, k = 0; i < midi_bus.n; i++) if (midi_bus.dev[i].kind == MDK_MT32) { if (k == slot) return i; k++; }
    return -1;
}
static int mt_slot_of(int di)                          /* 기계 di 의 엔진 슬롯 (엔진이 모자라면 -1) */
{
    if (di < 0 || di >= midi_bus.n || midi_bus.dev[di].kind != MDK_MT32) return -1;
    int k = 0; for (int i = 0; i < di; i++) if (midi_bus.dev[i].kind == MDK_MT32) k++;
    return k < MT_N ? k : -1;
}
static int mt_reserve(int slot)
{
    if (mt_mem_s[slot]) return 0;
    if (!P.vbuf || vrec_on || P.vbuf_size < ((slot ? 8u : 16u) << 20)) return -1;
    bgw_finish();                                       /* 5.2.69 */
    P.vbuf_size -= 6u << 20;
    mt_mem_s[slot] = P.vbuf + P.vbuf_size;
    return 0;
}
static void mt_sig(int slot, char *out, int max)       /* 이 슬롯 기계가 바라는 롬 (모델·제어·PCM) */
{
    int di = mt_dev_n(slot); out[0] = 0;
    if (di < 0) return;
    const MidiDev *d = &midi_bus.dev[di];
    out[0] = d->opt ? 'C' : 'M'; out[1] = 0; str_append(out, max, d->rom_ctl); str_append(out, max, "|"); str_append(out, max, d->rom_pcm);
}
/* 롬 이름이 "CM32L_CONTROL.1989-12-05.v1.02.ROM" 처럼 뒤에 날짜·판이 붙어도 찾음 (기본 이름이 있으면 그것) */
static void mt_rom_name(char *out, int max, const char *base)
{
    char nm[64]; copy_str(nm, sizeof nm, base); str_append(nm, sizeof nm, ".ROM");
    midi_path(out, max, MD_MT32, nm);
    uint8_t hb[4]; uint32_t fs = 0;
    if (!P.read_file(out, hb, 1, &fs) && fs) return;
    if (!P.list_dir) return;
    static FeDirEntry se[64];
    int n = P.list_dir(midi_dir(MD_MT32), se, 64), bl = (int)strlen(base);
    for (int i = 0; i < n; i++) {
        const char *e = se[i].key[0] ? se[i].key : se[i].name; int l = (int)strlen(e);
        if (se[i].is_dir || l < bl + 4) continue;
        int ok = 1; for (int k = 0; k < bl; k++) if ((e[k] | 32) != (base[k] | 32)) ok = 0;
        if (ok && e[bl] == '.' && (e[l - 4] == '.') && (e[l - 3] | 32) == 'r' && (e[l - 2] | 32) == 'o' && (e[l - 1] | 32) == 'm') { midi_path(out, max, MD_MT32, e); return; }
    }
}
static int has_ci(const char *h, const char *n)        /* 대소문자 가리지 않고 들어 있는지 */
{
    int hl = (int)strlen(h), nl = (int)strlen(n);
    for (int i = 0; i + nl <= hl; i++) { int ok = 1; for (int k = 0; k < nl; k++) if ((h[i + k] | 32) != (n[k] | 32)) { ok = 0; break; } if (ok) return 1; }
    return 0;
}
static void mt_load(int slot, int quiet)
{
    uint32_t t_a = P.ms();                               /* 롬 읽기 단계별 시간 (KEY 로그 N 줄: 준비·이름 찾기 / 제어 / PCM / 열기) */
    gm_mt32_close(slot);
    mt_inf[slot][0] = 0;
    mt_sig(slot, mt_sigl[slot], sizeof mt_sigl[slot]);
    int di = mt_dev_n(slot);
    if (!MT32_OK) { mt_st[slot] = -9; return; }
    if (di < 0 || !P.read_file) { mt_st[slot] = -1; return; }
    const MidiDev *dv = &midi_bus.dev[di];
    int model = dv->opt;
    if (mt_reserve(slot)) { mt_st[slot] = -4; if (!quiet) toast(nt("MT-32 음원 메모리가 모자랍니다", "Not enough memory for MT-32", "MT-32用のメモリ不足"), 0); return; }
    uint8_t *ctl = mt_mem_s[slot] + (4u << 20), *pcm = ctl + (128u << 10);
    uint32_t nc = 0, np = 0;
    char cp[FE_PATH_MAX], pp[FE_PATH_MAX];               /* USB\MidiSound\MT32 */
    if (dv->rom_ctl[0]) midi_path(cp, sizeof cp, MD_MT32, dv->rom_ctl); else mt_rom_name(cp, sizeof cp, model ? "CM32L_CONTROL" : "MT32_CONTROL");   /* 고른 파일이 먼저 */
    if (dv->rom_pcm[0]) midi_path(pp, sizeof pp, MD_MT32, dv->rom_pcm); else mt_rom_name(pp, sizeof pp, model ? "CM32L_PCM" : "MT32_PCM");
    uint32_t t_b = P.ms();
    int rf1 = P.read_file(cp, ctl, 128u << 10, &nc); uint32_t t_c = P.ms();
    int rf2 = rf1 || nc < 1024 ? -1 : P.read_file(pp, pcm, 1u << 20, &np); uint32_t t_d = P.ms();
    { char l[120]; l[0] = 0; str_append(l, sizeof l, "MT-32 롬: 이름 "); num_append(l, sizeof l, t_b - t_a, 0, ' ');
      str_append(l, sizeof l, "ms 제어 "); num_append(l, sizeof l, t_c - t_b, 0, ' '); str_append(l, sizeof l, "ms/"); num_append(l, sizeof l, nc, 0, ' ');
      str_append(l, sizeof l, "B PCM "); num_append(l, sizeof l, t_d - t_c, 0, ' '); str_append(l, sizeof l, "ms/"); num_append(l, sizeof l, np, 0, ' '); str_append(l, sizeof l, "B"); klog_note(l); }
    if (rf1 || nc < 1024 || rf2 || np < 1024) {
        mt_st[slot] = -1;
        if (!quiet) toast(nt("MidiSound\\MT32 폴더에 롬 파일이 없습니다", "ROM files not found in MidiSound\\MT32", "MidiSound\\MT32にROMがありません"), 0);
        return;
    }
    uint32_t t_e = P.ms();
    int r = gm_mt32_open(slot, mt_mem_s[slot], 4u << 20, ctl, nc, pcm, np);
    { char l[80]; l[0] = 0; str_append(l, sizeof l, "MT-32 열기 "); num_append(l, sizeof l, P.ms() - t_e, 0, ' '); str_append(l, sizeof l, "ms 결과 "); num_append(l, sizeof l, (uint32_t)(r + 10), 0, ' '); klog_note(l); }
    mt_st[slot] = r == 0 ? 1 : r == -1 ? -2 : r == -4 ? -4 : r == -9 ? -9 : -3;
    if (r == 0) fx_apply();              /* 새로 연 엔진에도 리버브 이득 */
    if (r == 0) { copy_str(mt_inf[slot], sizeof mt_inf[slot], gm_mt32_name(slot)); if (!quiet) toast(nt("MT-32 롬을 불러왔습니다", "MT-32 ROM loaded", "MT-32 ROMを読み込みました"), mt_inf[slot]); }
    else if (!quiet) toast(r == -1 ? nt("지원하지 않는 롬 파일입니다", "Unsupported ROM file", "未対応のROMです") : nt("MT-32 음원을 열지 못했습니다", "Could not start MT-32", "MT-32を開始できません"), 0);
}
/* 설정 변수(MIDI 출력 방식·음원·롬 종류) ↔ 단자에 꽂힌 기계 목록. 지금 설정 창은 예전 그대로(기계 하나)라 둘을 맞춰 둠 (새 MIDI 탭은 4단계) */
static void midi_bus_from_ui(void)
{
    MidiDev keep = midi_bus.n ? midi_bus.dev[0] : (MidiDev){0};   /* 기계 하나 설정 창에서 바꿔도 음량·좌우·채널은 이어감 */
    midibus_clear();
    int di = -1;
    if (midi_out == 1) di = midibus_plug(midi_synth == GM_MT32 ? MDK_MT32 : MDK_GM, midi_synth == GM_MT32 ? mt_model : midi_synth == GM_FM);
    else if (midi_out == 2) di = midibus_plug(MDK_USB, 0);
    else if (midi_out == 3) di = midibus_plug(MDK_FILE, 0);
    if (di >= 0 && keep.kind) { MidiDev *d = &midi_bus.dev[di]; d->vol = keep.vol; d->mute = keep.mute; d->pan = keep.pan; d->rx_mask = keep.rx_mask ? keep.rx_mask : 0xFFFF; d->port = keep.port; }
}
static void midi_ui_from_bus(void)
{
    const MidiDev *d = midi_bus.n ? &midi_bus.dev[0] : 0;
    if (!d) { midi_out = 0; return; }
    if (d->kind == MDK_USB) midi_out = 2;
    else if (d->kind == MDK_FILE) midi_out = 3;
    else { midi_out = 1; midi_synth = d->kind == MDK_MT32 ? (MT32_OK ? GM_MT32 : GM_SF2) : d->kind == MDK_GM && d->opt ? GM_FM : GM_SF2; if (d->kind == MDK_MT32) mt_model = d->opt; }
}
static void midi_synth_apply(void)                     /* 꽂힌 소프트웨어 기계마다 음원 인스턴스 i 의 방식을 정함 */
{
    fx_apply();                                         /* 5.2.159 */
    { const MidiDev *m32 = midibus_find(MDK_MT32); if (m32) mt_model = m32->opt; }   /* 첫 MT-32 기계의 롬 종류 (옛 설정 변수) */
    for (int sl = 0; sl < MT_N; sl++) {                 /* 엔진 슬롯마다 — 기계가 바라는 롬이 지금 불러온 것과 다르면 다시 읽음, 기계가 없어졌으면 엔진을 닫음 */
        char sg[140]; mt_sig(sl, sg, sizeof sg);
        if (strcmp(sg, mt_sigl[sl])) { if (!sg[0]) { gm_mt32_close(sl); mt_sigl[sl][0] = 0; } mt_st[sl] = 0; }
    }
    midi_bus.def_on = (uint8_t)(msx_dev(M, MDEV_MIDI_HW) && midi_int && !mix_mute[MIX_MIDI_INT]);   /* 사운드 탭 "GT 내장 미디" 를 끄면 GT 내장 음원도 멈춤 */   /* 기계가 있어도. 5.2.70: GT 내장 MIDI 인데 꽂힌 기계가 없으면 기본 GM 음원 (마스터님: 내장 미디가 켜져 있으면 소리가 나야) */
    for (int i = 0; i < GM_INST_MAX; i++) {
        int want = i < midi_bus.n ? midibus_gm_mode(&midi_bus.dev[i]) : (i == GM_INST_GT && midi_bus.def_on ? (midi_synth == GM_FM || (!sf_name[0] && sf_state != 1 && !sf_has_gm()) ? GM_FM : GM_SF2) : GM_OFF);
        if (want == GM_OFF) { gmi_set_mode(i, GM_OFF); continue; }   /* 3.5.4: 소프트웨어 음원 기계가 아니면 끔 */
        if (want == GM_MT32) {                             /* 롬이 안 되면 SoundFont → FM 차례로 */
            int sl = mt_slot_of(i);
            if (sl >= 0) {
                if (mt_st[sl] != 1) mt_load(sl, 1);
                if (mt_st[sl] == 1) { gmi_set_mt32(i, sl); gmi_set_mode(i, GM_MT32); if (gmi_mode(i) == GM_MT32) continue; }
            }
            want = GM_FM;                                  /* 고른 음원이 안 되면 다른 롬·SoundFont 로 몰래 바꾸지 않고 FM (+알림) */
            { static int w32[GM_INST_MAX]; int sl2 = mt_slot_of(i), stv = sl2 >= 0 ? mt_st[sl2] : -8; if (w32[i] != stv + 100 + midi_bus.dev[i].opt) { w32[i] = stv + 100 + midi_bus.dev[i].opt; toast(nt("MT-32/CM-32L 롬을 쓸 수 없어 FM 으로 냅니다", "MT-32/CM-32L ROM unavailable - using FM", "MT-32/CM-32L ROMが使えずFMで鳴らします"), "USB\\MidiSound\\MT32"); } }
        }
        int gs = i < midi_bus.n && midi_bus.dev[i].kind == MDK_GS, slot = gs ? gs_slot() : 0;   /* GS 기계 = 자기 SoundFont 칸 (없으면 칸 0), 리듬 파트 SysEx 받음 */
        if (want == GM_SC55) {                             /* SC-55 실기 롬 — 안 되면 GS SoundFont → FM */
            if (sc55_state == 0) sc55_load(1);
            gmi_set_sf(i, slot, 1);
            if (sc55_state == 1) { gmi_set_mode(i, GM_SC55); if (gmi_mode(i) == GM_SC55) continue; }
            want = GM_FM;
            { static int w55; if (w55 != sc55_state + 100) { w55 = sc55_state + 100; toast(nt("SC-55 롬을 쓸 수 없어 FM 으로 냅니다", "SC-55 ROM unavailable - using FM", "SC-55 ROMが使えずFMで鳴らします"), "USB\\MidiSound\\SC55"); } }
        }
        if (want != GM_FM && fe_sf[slot].state != 1) sf_load_slot(slot, 1);
        if (slot && fe_sf[1].state != 1) { slot = 0; if (sf_state != 1) sf_load(1); }   /* GS 파일이 안 되면 GM 쪽 SoundFont 로 */
        gmi_set_sf(i, slot, gs);
        gmi_set_mode(i, want != GM_FM && fe_sf[slot].state == 1 ? GM_SF2 : GM_FM);
    }
}

/* 설정 표 — 숫자 하나로 된 설정은 이 표 한 줄 (ini 키·변수·읽을 때 검사·기종별 저장 여부).
 * ini 쓰기(settings_flush)·읽기(ini_parse_lines)·기종별 옵션 파일(opt_key) 이 표를 같이 씀. 읽을 때 따로 할 일이 있는 설정(KeyLayout 등)은 아래 손 코드 */
enum { OPT_BOOL, OPT_INT, OPT_RANGE, OPT_CLAMP, OPT_MASK };
enum { OU_PAL = 1, OU_REDRAW = 2, OU_MIX = 4, OU_SPR = 8, OU_ANYDIR = 16, OU_C32 = 32, OU_HAND = 64 };   /* 5.2.38 */
enum { OF_NONE, OF_NUM, OF_PCT, OF_SIGNED, OF_GAMMA, OF_PCT_OFF, OF_INV };   /* 5.2.41 값 글자: 숫자 / N% / +N·-N / 1.30 / N% 또는 끔 / 켜기끄기 뒤집어 보임 */
typedef struct { const char *key; int *var; uint8_t kind; int lo, hi, bad; uint8_t per_machine; uint16_t prop; uint8_t step, ui; uint16_t str; uint8_t fmt; } FeOpt;   /* RANGE: 밖이면 bad, CLAMP: lo~hi 로 자름, MASK: & hi */
/* 속성 창 — prop = 속성 항목 번호 (0 = 창에 없음), step = 막대 한 칸 (0 = 켜기/끄기), ui = 바꾼 뒤 할 일 (OU_*)
 * str = 줄 이름(StrId), fmt = 값 글자 꼴 (OF_*). 이 둘이 있으면 속성 창 줄도 표가 그림 (opt_prop_item) */
static const FeOpt fe_opts[] = {
    { "Mute", &muted, OPT_BOOL, 0, 0, 0, 0, 0, 0, 0 },
    { "Volume", &volume, OPT_CLAMP, 0, 100, 0, 1, 0, 0, 0 },
    { "ResetAfterInsert", &reset_after_insert, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
    { "HdResetEject", &hd_reset_eject, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
    { "HdResetInsert", &hd_reset_insert, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
    { "VhdResetInsert", &vhd_reset_insert, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
    { "Monitor", &monitor_kind, OPT_MASK, 0, 3, 0, 1, 0, 0, 0 },
    { "MonitorEmu2", &mon_emu, OPT_RANGE, 0, MON_EMU_N - 1, 0, 1, 0, 0, 0 },
    { "FxBlur", &fx_blur, OPT_CLAMP, 0, 100, 0, 1, 53, 10, OU_C32|OU_REDRAW|OU_HAND, S_FX_BLUR, OF_PCT_OFF },
    { "FxGlow", &fx_glow, OPT_CLAMP, 0, 100, 0, 1, 54, 10, OU_C32|OU_REDRAW|OU_HAND, S_FX_GLOW, OF_PCT_OFF },
    { "FxNoise", &fx_noise, OPT_CLAMP, 0, 100, 0, 1, 55, 10, OU_C32|OU_REDRAW|OU_HAND, S_FX_NOISE, OF_PCT_OFF },
    { "Aspect", &aspect_mode, OPT_RANGE, 0, 2, 0, 1, 0, 0, 0 },
    { "VdpCmdTiming", &msx_vdp_cmd_timing, OPT_BOOL, 0, 0, 0, 1, 58, 0, 0, S_VDP_TIMING, OF_NONE },
    { "PrnMode", &prn_mode, OPT_RANGE, 0, 2, 0, 1, 0, 0, 0 },
    { "PrnPreview", &prn_preview, OPT_RANGE, 0, 2, 1, 0, 0, 0, 0 },
    { "SpriteHide", &spr_hide, OPT_BOOL, 0, 0, 0, 1, 51, 0, OU_SPR, S_SPR_SHOW, OF_INV },
    { "HideIcons", &icons_off, OPT_BOOL, 0, 0, 0, 0, 0, 0, 0 },
    { "IconsAlways", &icons_always, OPT_BOOL, 0, 0, 0, 0, 0, 0, 0 },   /* 5.2.141 */
    { "SpriteNoLimit", &spr_nolimit, OPT_BOOL, 0, 0, 0, 1, 52, 0, OU_SPR, S_SPR_NOLIMIT, OF_NONE },
    { "RomTop", &rom_top, OPT_INT, 0, 0, 0, 0, 0, 0, 0 },
    { "DiskTop", &disk_top, OPT_INT, 0, 0, 0, 0, 0, 0, 0 },
    { "BasTop", &bas_top, OPT_INT, 0, 0, 0, 0, 0, 0, 0 },
    { "BasMode", &bas_mode_save, OPT_BOOL, 0, 0, 0, 0, 0, 0, 0 },
    { "TapeTop", &tape_top, OPT_INT, 0, 0, 0, 0, 0, 0, 0 },
    { "Joy1", &joy_mode[0], OPT_RANGE, 0, 3, 0, 1, 0, 0, 0 },
    { "Joy2", &joy_mode[1], OPT_RANGE, 0, 3, 0, 1, 0, 0, 0 },
    { "MousePort", &mouse_port, OPT_RANGE, 0, 2, 0, 0, 0, 0, 0 },
    { "MouseSens", &mouse_sens, OPT_RANGE, 25, 400, 100, 0, 92, 25, 0, S_MOUSE_SENS, OF_PCT },
    { "MouseMenu", &mouse_menu, OPT_BOOL, 0, 0, 0, 0, 93, 0, 0, S_MOUSE_MENUUSE, OF_NONE },
    { "MouseUse", &mouse_on, OPT_BOOL, 0, 0, 0, 0, 94, 0, 0, S_MOUSE_USE, OF_NONE },
    { "MouseInvX", &mouse_invx, OPT_BOOL, 0, 0, 0, 0, 95, 0, 0, S_MOUSE_INVX, OF_NONE },
    { "MouseInvY", &mouse_invy, OPT_BOOL, 0, 0, 0, 0, 96, 0, 0, S_MOUSE_INVY, OF_NONE },
    { "MouseLock", &mouse_lock, OPT_BOOL, 0, 0, 0, 0, 97, 0, OU_ANYDIR },
    { "MousePointer", &mouse_ptr, OPT_MASK, 0, 3, 0, 0, 0, 0, 0 },
    { "MouseWheel", &mouse_wheel, OPT_RANGE, 1, 5, 1, 0, 99, 1, 0, S_MOUSE_WHEEL, OF_NUM },
    { "GlobalSave", &global_save, OPT_BOOL, 0, 0, 0, 0, 172, 0, 0, S_GLOBAL_SAVE, OF_NONE },
    { "Resume", &resume_on, OPT_BOOL, 0, 0, 0, 0, 15, 0, 0, S_RESUME, OF_NONE },
    { "BootCtrl", &boot_ctrl, OPT_BOOL, 0, 0, 0, 1, 170, 0, 0, S_BOOT_CTRL, OF_NONE },
    { "AutoCtrl", &auto_ctrl, OPT_BOOL, 0, 0, 0, 1, 171, 0, 0, S_AUTO_CTRL, OF_NONE },
    { "FlashAuto", &cart_flash_auto, OPT_MASK, 0, 3, 0, 0, 0, 0, 0 },
    { "DiskAccel", &disk_accel, OPT_BOOL, 0, 0, 0, 0, 175, 0, 0, S_DISK_ACCEL, OF_NONE },
    { "Bright", &vid_bright, OPT_RANGE, -50, 50, 0, 1, 45, 5, OU_PAL, S_BRIGHT, OF_SIGNED },
    { "Contrast", &vid_contrast, OPT_RANGE, 50, 150, 100, 1, 46, 5, OU_PAL, S_CONTRAST, OF_PCT },
    { "Satur", &vid_satur, OPT_RANGE, 0, 200, 100, 1, 47, 10, OU_PAL, S_SATUR, OF_PCT },
    { "Gamma", &vid_gamma, OPT_RANGE, 50, 200, 100, 1, 48, 10, OU_PAL, S_GAMMA, OF_GAMMA },
    { "Stereo", &snd_stereo, OPT_BOOL, 0, 0, 0, 1, 110, 0, OU_MIX, S_STEREO, OF_NONE },
    { "MidiInternal", &midi_int, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
    { "MidiIn", &midi_in, OPT_BOOL, 0, 0, 0, 1, 113, 0, OU_ANYDIR },
    { "Mt32Model", &mt_model, OPT_BOOL, 0, 0, 0, 0, 0, 0, 0 },
    { "MidiRev", &fx_rev, OPT_CLAMP, 0, 200, 0, 0, 0, 0, 0 },   /* 5.2.159 */
    { "MidiCho", &fx_cho, OPT_CLAMP, 0, 200, 0, 0, 0, 0, 0 },
    { "MidiLen", &fx_len, OPT_CLAMP, 0, 200, 0, 0, 0, 0, 0 },
    { "MidiFxOn", &fx_on, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
    { "AcpiMode", &acpi_mode, OPT_BOOL, 0, 0, 0, 0, 0, 0, 0 },   /* ini 전용 (실험) */
    { "MidiInPort", &midi_in_port, OPT_CLAMP, 0, MDP_N - 1, 0, 0, 0, 0, 0 },   /* 5.2.56 */
    { "PanaFront", &pana_front, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
    { "PanaPause", &pana_pause, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
    { "DiskResetAfterInsert", &disk_reset_after_insert, OPT_BOOL, 0, 0, 0, 1, 0, 0, 0 },
};
#define FE_NOPTS ((int)(sizeof fe_opts / sizeof fe_opts[0]))
static const FeOpt *opt_of_prop(int id) { for (int i = 0; i < FE_NOPTS; i++) if (id && fe_opts[i].prop == id) return &fe_opts[i]; return 0; }
static void opt_after(const FeOpt *o)                   /* 속성 창에서 바꾼 뒤 */
{
    if (o->ui & OU_PAL) { recompute_palette(); if (P.set_palette) P.set_palette(); full_redraw(); }
    if (o->ui & OU_REDRAW) full_redraw();
    if (o->ui & OU_MIX) mix_apply();
    if (o->ui & OU_SPR) { M->vdp.spr_hide = (uint8_t)spr_hide; M->vdp.spr_nolimit = (uint8_t)spr_nolimit; }
    settings_changed();
}
/* 속성 창 항목 바꾸기 — 켜기/끄기는 Enter(OU_ANYDIR 면 ←/→ 도), 막대는 ←/→ 한 칸. 표에 없거나 손 코드면 0 */
static int opt_prop_change(int id, int dir)
{
    const FeOpt *o = opt_of_prop(id);
    if (!o || (o->ui & OU_HAND)) return 0;
    if (!o->step) { if (dir && !(o->ui & OU_ANYDIR)) return 1; *o->var = !*o->var; }
    else { if (!dir) return 1; int v = *o->var + dir * o->step; *o->var = v < o->lo ? o->lo : v > o->hi ? o->hi : v; }
    opt_after(o);
    return 1;
}
/* 속성 창 줄 — 표의 이름(str)·값 꼴(fmt)로 PropItem 을 채움. 켜기/끄기면 체크(2), 막대면 막대(3) + 자리 0..500. 표에 없거나 이름이 없으면 0 */
static int opt_prop_item(PropItem *q, int id, int enabled)
{
    const FeOpt *o = opt_of_prop(id);
    if (!o || !o->str) return 0;
    int v = *o->var;
    copy_str(q->text, sizeof q->text, T((StrId)o->str)); q->value[0] = 0; q->id = id; q->enabled = enabled;
    if (!o->step) { q->kind = 2; q->on = o->fmt == OF_INV ? !v : v != 0; return 1; }
    q->kind = 3; q->on = o->hi > o->lo ? (v - o->lo) * 500 / (o->hi - o->lo) : 0;
    char *b = q->value; int n = (int)sizeof q->value;
    switch (o->fmt) {
    case OF_SIGNED: if (v < 0) str_append(b, n, "-"); else if (v > 0) str_append(b, n, "+"); num_append(b, n, (uint32_t)(v < 0 ? -v : v), 0, ' '); break;
    case OF_PCT: num_append(b, n, (uint32_t)v, 0, ' '); str_append(b, n, "%"); break;
    case OF_PCT_OFF: if (v) { num_append(b, n, (uint32_t)v, 0, ' '); str_append(b, n, "%"); } else copy_str(b, n, T(S_OFF)); break;
    case OF_GAMMA: num_append(b, n, (uint32_t)v / 100, 0, ' '); str_append(b, n, "."); num_append(b, n, (uint32_t)v % 100, 2, '0'); break;
    default: num_append(b, n, (uint32_t)v, 0, ' '); break;
    }
    return 1;
}
/* 막대 누르기·끌기 (f = 0..1000). 표에 없으면 0 */
static int opt_prop_slide(int id, uint32_t f)
{
    const FeOpt *o = opt_of_prop(id);
    if (!o || !o->step) return 0;
    int lo = o->lo, hi = o->hi, st = o->step;
    int raw = lo + (int)((f * (uint32_t)(hi - lo) + 500) / 1000);
    int v = lo + ((raw - lo + st / 2) / st) * st; if (v > hi) v = hi; if (v < lo) v = lo;
    if ((o->ui & OU_C32) && D.bpp == 1) return 1;
    *o->var = v;
    opt_after(o);
    return 1;
}
static void opt_tab_save(char *b) { for (int i = 0; i < FE_NOPTS; i++) ini_num(b, fe_opts[i].key, *fe_opts[i].var); }
static int opt_tab_find(const char *k) { for (int i = 0; i < FE_NOPTS; i++) if (!strcmp(k, fe_opts[i].key)) return i; return -1; }
static int opt_tab_parse(const char *k, const char *v)
{
    int i = opt_tab_find(k);
    if (i < 0) return 0;
    const FeOpt *o = &fe_opts[i]; long x = ini_long(v);
    switch (o->kind) {
    case OPT_BOOL:  *o->var = x != 0; break;
    case OPT_INT:   *o->var = (int)x; break;
    case OPT_RANGE: *o->var = x < o->lo || x > o->hi ? o->bad : (int)x; break;
    case OPT_CLAMP: *o->var = x < o->lo ? o->lo : x > o->hi ? o->hi : (int)x; break;
    case OPT_MASK:  *o->var = (int)(x & o->hi); break;
    }
    return 1;
}

static void settings_flush(void)
{
    if (!settings_dirty || !P.write_file) return;
    char *b = (char *)ini_buf;
    b[0] = 0;
    str_append(b, INI_SIZE, "; X86MSX settings and last state (written automatically)\n[State]\n");
    ini_put(b, "Machine", machine_name);
    ini_num(b, "VideoOut", video_out);
    
    opt_tab_save(b);                                    /* 5.2.35 설정 표 */
    if (fx_seen || fx_name[0]) ini_put(b, "Font", fx_pref[0] ? fx_pref : "-");
    ini_put(b, "Language", lang == 1 ? "en" : lang == 2 ? "ja" : lang >= 3 && lang <= 9 ? lang2_codes[lang - 3] : lang == 10 ? "ar" : "ko");
    
    
    
    
    
    ini_num(b, "AudioOut", audio_out);
    
    ini_num(b, "Scanlines", scanlines);
    ini_num(b, "VideoSource", vsrc_pref + 1); ini_num(b, "DiagPC", diag_pc_sel + 1); ini_num(b, "FirmwareSwitch", fw_switch);
    
    
    
    ini_put(b, "RomSel", rom_sel);
    
    
    ini_put(b, "BasDir", bas_dir); ini_put(b, "BasDirName", bas_dir_disp); ini_put(b, "BasSel", bas_sel); ini_put(b, "BasSaveName", bas_fname); 
    ini_put(b, "TapeDir", tape_dir); ini_put(b, "TapeDirName", tape_dir_disp); ini_put(b, "TapeSel", tape_sel); 
    ini_put(b, "DiskSel", disk_sel);
    ini_num(b, "KeyLayout", key_layout);
    
    
    ini_num(b, "MouseDriver", mouse_own);
    ini_num(b, "MouseOff", mouse_offmask); if (touchpad_pref >= 0) ini_num(b, "TouchpadUse", touchpad_pref);   /* 3.4.3 */
    ini_put(b, "DcPath0", dc_ini_path[0]); ini_put(b, "DcPath1", dc_ini_path[1]); ini_put(b, "DcDisp0", dc_ini_disp[0]); ini_put(b, "DcDisp1", dc_ini_disp[1]); 
    
    ini_num(b, "Vsync", M->vsync_force);
    joy_save(b);
    sc_save(b);
    ini_num(b, "ShortcutsVer", sc_ver);                 /* 3.7.2 */
    km_save(b);
    vhd_save(b);                                        /* 3.7.1 */
    net_save_keys(b);                                   /* 5.0.57 */
    {
        char k[16];
        for (int c = 0; c < MIX_N - 1; c++) {
            copy_str(k, sizeof k, "Mix"); str_append(k, sizeof k, mix_key_of(c)); ini_num(b, k, mix_vol[c]);
            if (c != MIX_IO) { copy_str(k, sizeof k, "Mute"); str_append(k, sizeof k, mix_key_of(c)); ini_num(b, k, mix_mute[c]); }
            if (mix_has_pan(c)) { copy_str(k, sizeof k, "Balance"); str_append(k, sizeof k, mix_key_of(c)); ini_num(b, k, mix_pan[c]); }   /* 3.3.3 */
        }
        ini_num(b, "MidiOutMode", midi_out); ini_num(b, "MidiSynth", midi_synth); ini_put(b, "SoundFont", sf_name); ini_put(b, "GsSoundFont", gs_name); { int d0 = mt_dev_n(0), d1 = mt_dev_n(1); ini_put(b, "MtCtlRom", d0 >= 0 ? midi_bus.dev[d0].rom_ctl : ""); ini_put(b, "MtPcmRom", d0 >= 0 ? midi_bus.dev[d0].rom_pcm : ""); if (d1 >= 0) { char t[140]; copy_str(t, sizeof t, midi_bus.dev[d1].rom_ctl); str_append(t, sizeof t, "|"); str_append(t, sizeof t, midi_bus.dev[d1].rom_pcm); ini_put(b, "MtRom2", t); } }   /* 기계마다 롬 (첫 MT-32 는 예전 키 그대로) */ ini_num(b, "Sc55Set", sc55_set); ini_num(b, "DriveSound", !mix_mute[MIX_IO]);
        for (int i = 0; i < midi_bus.n; i++) {          /* 단자에 꽂힌 기계 목록 — MidiDev=종류,설정,받는채널,음량,좌우,끄기 (읽을 때 이게 있으면 MidiOutMode·MidiSynth 보다 우선) */
            const MidiDev *d = &midi_bus.dev[i];
            char vv[64]; copy_str(vv, sizeof vv, midi_dev_class[d->kind].key);
            str_append(vv, sizeof vv, ","); num_append(vv, sizeof vv, d->opt, 0, ' ');
            str_append(vv, sizeof vv, ","); num_append(vv, sizeof vv, d->rx_mask, 0, ' ');
            str_append(vv, sizeof vv, ","); num_append(vv, sizeof vv, d->vol, 0, ' ');
            str_append(vv, sizeof vv, ","); if (d->pan < 0) str_append(vv, sizeof vv, "-"); num_append(vv, sizeof vv, (uint32_t)(d->pan < 0 ? -d->pan : d->pan), 0, ' ');
            str_append(vv, sizeof vv, ","); num_append(vv, sizeof vv, d->mute, 0, ' ');
            str_append(vv, sizeof vv, ","); num_append(vv, sizeof vv, d->port, 0, ' ');   /* 단자 (255 = 모두) */
            ini_put(b, "MidiDev", vv);
        }
    }
    
    
    
    /* 롬팩·디스크·카세트는 저장하지 않는다 (요구 3). 기종별 환경(슬롯 확장, 특수 카트리지)만 */
    env_save_current();
    for (int e = 0; e < nenv; e++) {
        char k[8] = "Env1", v[2 * FE_PATH_MAX + 64];
        k[3] = (char)('1' + e);
        copy_str(v, sizeof v, envs[e].machine);
        for (int q = 0; q < 1 + 4; q++) {
            str_append(v, sizeof v, "|");
            int x = q ? envs[e].special[q - 1] : envs[e].expand + 10 * envs[e].mport + 100 * envs[e].mflags;
            num_append(v, sizeof v, (uint32_t)x, 0, ' ');
        }
        int s5 = envs[e].special[4];                    /* 3.6.7: 카트리지 5는 하드 디스크 두 칸 뒤 (옛 ini와 자리 맞춤) */
        if (envs[e].hd[0] || s5) { str_append(v, sizeof v, "|"); str_append(v, sizeof v, envs[e].hd); }
        if ((envs[e].hd[0] && envs[e].hd2[0]) || s5) { str_append(v, sizeof v, "|"); str_append(v, sizeof v, envs[e].hd[0] ? envs[e].hd2 : ""); }
        if (s5) { str_append(v, sizeof v, "|"); num_append(v, sizeof v, (uint32_t)s5, 0, ' '); }
        ini_put(b, k, v);
    }
    for (int e = 0; e < nenv; e++) {                    /* 배터리 백업 내용 (기종별) */
        if (!envs[e].has_cmos) continue;
        static const char hx[] = "0123456789ABCDEF";
        char k[8] = "Cmos1", v[160];
        k[4] = (char)('1' + e);
        copy_str(v, sizeof v, envs[e].machine);
        str_append(v, sizeof v, "|");
        char t[2] = "0";
        for (int i = 0; i < 39; i++) { t[0] = hx[envs[e].rtc[i / 13][i % 13] & 15]; str_append(v, sizeof v, t); }
        str_append(v, sizeof v, "|");
        for (int i = 0; i < 16; i++) { t[0] = hx[envs[e].s1985[i] >> 4]; str_append(v, sizeof v, t); t[0] = hx[envs[e].s1985[i] & 15]; str_append(v, sizeof v, t); }
        ini_put(b, k, v);
    }
    ini_put(b, "ImgDir", img_dir); ini_put(b, "ImgDirName", img_dir_disp);   /* 3.8.1 이미지 만들기 저장 폴더 */
    ini_put(b, "DirInsDir", dir_dir); ini_put(b, "DirInsDirName", dir_dir_disp); ini_put(b, "DirInsSel", dir_sel); ini_num(b, "DirInsTop", dir_top);   /* 3.8.1 */
    ini_put(b, "VhdDirPos", vbr_dir); ini_put(b, "VhdDirPosName", vbr_dir_disp); ini_put(b, "VhdSel", vbr_sel); ini_num(b, "VhdTop", vbr_top);
    ini_put(b, "DiskDir", disk_dir);
    ini_put(b, "DiskDirName", disk_dir_disp);
    ini_put(b, "RomDir", rom_path);
    ini_put(b, "RomDirName", rom_disp);
    recent_save(b);                                     /* 2.12: 최근 파일 (맨 뒤: 넘쳐도 앞 설정은 남게) */
    sc_file_save();                                     /* 3.3.1 KBD-03 */
    int len = (int)strlen(b);
    for (int i = len; i < INI_SIZE - 1; i++) b[i] = ' ';    /* 고정 크기: 나머지는 공백 */
    b[INI_SIZE - 1] = '\n';
    settings_dirty = 0;                                 /* 실패해도 계속 다시 쓰지 않음 */
    if (ini_prev_ok) {                                  /* 3.5.5: 그대로면 안 씀, 바뀐 섹터만 */
        int i0 = 0, i1 = INI_SIZE - 1;
        while (i0 < INI_SIZE && ini_buf[i0] == ini_prev[i0]) i0++;
        if (i0 == INI_SIZE) return;
        while (ini_buf[i1] == ini_prev[i1]) i1--;
        uint32_t off = (uint32_t)i0 & ~511u, end = ((uint32_t)i1 + 512) & ~511u;
        if (end > INI_SIZE) end = INI_SIZE;
        if (P.write_at && !P.write_at(INI_PATH, off, ini_buf + off, end - off)) { memcpy(ini_prev + off, ini_buf + off, end - off); return; }
    }
    ini_prev_ok = !P.write_file(INI_PATH, ini_buf, INI_SIZE);
    if (ini_prev_ok) memcpy(ini_prev, ini_buf, INI_SIZE);
}

static long ini_long(const char *v)
{
    long x = 0; int neg = 0;
    if (*v == '-') { neg = 1; v++; }
    while (*v >= '0' && *v <= '9') x = x * 10 + (*v++ - '0');
    return neg ? -x : x;
}

static char want_machine[128];
void fe_set_default_machine(const char *name) { copy_str(machine_name, sizeof machine_name, name); }

/* 4.0.13: 옛 ini의 경로(0:\\DISK\\..., 표시 "Drive 1 (boot) > DISK > ...")를 새 위치(Storage Device 아래)로 바꿔 줌. 해당 없으면 v 그대로 */
static const char *ini_mig(const char *v)
{
    static char out[FE_PATH_MAX + 240];
    static const char *const nm[6] = { "DISK", "VMFolder", "CAS", "HARDDISK", "BASIC", "ROMS" };
    const char *rest; int disp;
    if (v[0] == '0' && v[1] == ':' && v[2] == '\\') { rest = v + 3; disp = 0; }
    else if (!strncmp(v, "Drive 1 (boot) > ", 17)) { rest = v + 17; disp = 1; }
    else if (!strncmp(v, "0: > ", 5)) { rest = v + 5; disp = 1; }          /* 옛 표시 이름 형식 */
    else return v;
    for (int i = 0; i < 6; i++) {
        int n = (int)strlen(nm[i]), ok = 1;
        for (int j = 0; j < n && ok; j++) ok = rest[j] && lower_c(rest[j]) == lower_c(nm[i][j]);
        if (!ok) continue;
        char c = rest[n];
        if (c && c != '|' && c != (disp ? ' ' : '\\')) continue;
        copy_str(out, sizeof out, disp ? FE_DISP_STORE " > " : FE_STORE "\\");
        if ((int)(strlen(out) + strlen(rest)) >= (int)sizeof out) return v;
        str_append(out, sizeof out, rest);
        return out;
    }
    return v;
}

static void ini_parse_lines(char *line)                /* 4.0.34: ini 본문(또는 기종별 옵션 파일)을 읽어 값에 넣음 */
{
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char save = *end;
        *end = 0;
        int cr = end > line && end[-1] == '\r'; if (cr) end[-1] = 0;   /* 윈도우 메모장으로 고친 ini (CRLF) — 글자 값 끝의 CR 떼기 */
        char *eq = strchr(line, '=');
        if (eq && line[0] != ';') {
            *eq = 0;
            const char *k = line, *v = eq + 1;
            v = ini_mig(v);                                     /* 4.0.13: 옛 폴더 위치 → Storage Device 아래 */
            if (opt_tab_parse(k, v)) {}                     /* 5.2.35 설정 표 */
            else if (!strcmp(k, "Machine")) copy_str(want_machine, sizeof want_machine, v);
            else if (!strcmp(k, "VideoOut")) video_out = 0;                 /* 4.0.71: 화면·소리 출력은 항상 기본으로 시작 (저장값 무시) */

            else if (!strcmp(k, "DiskA")) copy_str(disk_path[0], sizeof disk_path[0], v);
            else if (!strcmp(k, "DiskB")) copy_str(disk_path[1], sizeof disk_path[1], v);
            else if (!strcmp(k, "DiskAName")) copy_str(disk_name[0], sizeof disk_name[0], v);
            else if (!strcmp(k, "DiskBName")) copy_str(disk_name[1], sizeof disk_name[1], v);
            else if (!strcmp(k, "DiskAWP")) disk_wp[0] = ini_long(v) != 0;
            else if (!strcmp(k, "DiskBWP")) disk_wp[1] = ini_long(v) != 0;
            else if (!strcmp(k, "ImgDir") && v[0]) copy_str(img_dir, sizeof img_dir, v);
            else if (!strcmp(k, "ImgDirName") && v[0]) copy_str(img_dir_disp, sizeof img_dir_disp, v);
            else if (!strcmp(k, "DirInsDir") && v[0]) copy_str(dir_dir, sizeof dir_dir, v);       /* 3.8.1 */
            else if (!strcmp(k, "DirInsDirName") && v[0]) copy_str(dir_dir_disp, sizeof dir_dir_disp, v);
            else if (!strcmp(k, "DirInsSel")) copy_str(dir_sel, sizeof dir_sel, v);
            else if (!strcmp(k, "DirInsTop")) dir_top = (int)ini_long(v);
            else if (!strcmp(k, "VhdDirPos") && v[0]) copy_str(vbr_dir, sizeof vbr_dir, v);
            else if (!strcmp(k, "VhdDirPosName") && v[0]) copy_str(vbr_dir_disp, sizeof vbr_dir_disp, v);
            else if (!strcmp(k, "VhdSel")) copy_str(vbr_sel, sizeof vbr_sel, v);
            else if (!strcmp(k, "VhdTop")) vbr_top = (int)ini_long(v);
            else if (!strcmp(k, "DiskDir") && v[0]) copy_str(disk_dir, sizeof disk_dir, v);
            else if (!strcmp(k, "DiskDirName") && v[0]) copy_str(disk_dir_disp, sizeof disk_dir_disp, v);
            else if (!strcmp(k, "AudioOut")) audio_out = 0;
            else if (!strcmp(k, "Scanlines")) { long x = ini_long(v); scanlines = x == 1 ? 100 : x < 0 ? 0 : x > 100 ? 100 : (int)x; if (scanlines) scan_last = scanlines; }   /* 3.2.9까지 1 = 켬(검정) */
            else if (!strcmp(k, "MonitorEmu")) { static const int old[6] = { 0, 1, 1, 5, 6, 7 }; long x = ini_long(v); mon_emu = x < 0 || x > 5 ? 0 : old[x]; }   /* 3.2.10까지: 없음·LED·RGB·OLD·Scale2x·Hq2x */
            else if (!strcmp(k, "FirmwareSwitch")) { fw_switch = ini_long(v) != 0; msx_fw_turbor = fw_switch; }
            else if (!strcmp(k, "DiagPC")) { long x = ini_long(v); diag_pc_sel = x < 1 || x > 200 ? -1 : (int)x - 1; }   /* 진단 시험 PC (0 자동) */
            else if (!strcmp(k, "VideoSource")) { long x = ini_long(v); vsrc_pref = x < 1 || x > VSRC_N + 2 ? -1 : (int)x - 1; }   /* 0 자동, 1 VDP, 2 V9990 */
            else if (!strcmp(k, "RomSel")) copy_str(rom_sel, sizeof rom_sel, v);
            else if (!strcmp(k, "TapeDir") && v[0]) copy_str(tape_dir, sizeof tape_dir, v);
            else if (!strcmp(k, "BasDir") && v[0]) copy_str(bas_dir, sizeof bas_dir, v);
            else if (!strcmp(k, "BasDirName") && v[0]) copy_str(bas_dir_disp, sizeof bas_dir_disp, v);
            else if (!strcmp(k, "BasSel")) copy_str(bas_sel, sizeof bas_sel, v);
            else if (!strcmp(k, "BasSaveName") && v[0]) copy_str(bas_fname, sizeof bas_fname, v);
            else if (!strcmp(k, "TapeDirName") && v[0]) copy_str(tape_dir_disp, sizeof tape_dir_disp, v);
            else if (!strcmp(k, "TapeSel")) copy_str(tape_sel, sizeof tape_sel, v);
            else if (!strcmp(k, "DiskSel")) copy_str(disk_sel, sizeof disk_sel, v);
            else if (!strcmp(k, "KeyLayout")) { key_layout = (int)ini_long(v); if (key_layout < 0 || key_layout >= KL_COUNT) key_layout = 0; keymap_layout(key_layout); input_init(&in); }
            else if (!strcmp(k, "TouchpadUse")) { touchpad_pref = ini_long(v) != 0; mouse_ndev = -1; }
            else if (!strcmp(k, "MouseOff")) { mouse_offmask = (int)ini_long(v) & 0xFF; mouse_ndev = -1; }
            else if (!strcmp(k, "MouseDriver")) { mouse_own = (int)ini_long(v); if (mouse_own < 0 || mouse_own > 2) mouse_own = 0; if (P.mouse_driver) mouse_own = P.mouse_driver(mouse_own); }
            else if (!strcmp(k, "Joy1Keys")) joy_load(0, v);
            else if (!strcmp(k, "DcPath0")) copy_str(dc_ini_path[0], sizeof dc_ini_path[0], v);
            else if (!strcmp(k, "DcPath1")) copy_str(dc_ini_path[1], sizeof dc_ini_path[1], v);
            else if (!strcmp(k, "DcDisp0")) copy_str(dc_ini_disp[0], sizeof dc_ini_disp[0], v);
            else if (!strcmp(k, "DcDisp1")) copy_str(dc_ini_disp[1], sizeof dc_ini_disp[1], v);
            else if (!strcmp(k, "Vsync")) { M->vsync_force = (int)ini_long(v); if (M->vsync_force < 0 || M->vsync_force > 2) M->vsync_force = 0; }
            else if (!strncmp(k, "Recent", 6)) recent_load(k + 6, v);
            else if (!strcmp(k, "Joy2Keys")) joy_load(1, v);
            else if (!strcmp(k, "Shortcuts2")) sc_load(v);
            else if (!strcmp(k, "ShortcutsVer")) sc_ver = (int)ini_long(v);
            else if (!strcmp(k, "KeyRemap")) { km_load(v); input_init(&in); }
            else if (!strcmp(k, "KeyViewDef")) copy_str(km_defset, sizeof km_defset, v);
            else if (vhd_load_key(k, v)) {}                 /* 3.7.1 */
            else if (net_load_key(k, v)) {}                 /* 5.0.57 */
            else if (!strcmp(k, "Shortcuts")) sc_load_old(v);
            else if (!strncmp(k, "Balance", 7)) { for (int c = 0; c < MIX_N - 1; c++) if (mix_key_is(k + 7, c)) { long x = ini_long(v); mix_pan[c] = x < -100 ? -100 : x > 100 ? 100 : (int)x; } }
            else if (!strcmp(k, "MidiOutMode")) { long x = ini_long(v); midi_out = x < 0 || x > 3 ? 1 : (int)x; ini_mo_seen = 1; }   /* 3.5.4 */
            else if (!strcmp(k, "MidiDev")) {           /* 종류,설정,받는채널,음량,좌우,끄기 */
                if (!ini_mididev_seen) { ini_mididev_seen = 1; midibus_clear(); }
                char f[7][16]; int nf = 0, fl = 0; f[0][0] = 0;
                for (const char *q = v; *q && nf < 7; q++) { if (*q == ',') { f[nf][fl] = 0; nf++; fl = 0; if (nf < 7) f[nf][0] = 0; } else if (fl < 15) { f[nf][fl++] = *q; } }
                if (nf < 7) { f[nf][fl] = 0; nf++; }
                int kind = -1; for (int t = 1; t < MDK_N; t++) if (!strcmp(f[0], midi_dev_class[t].key)) kind = t;
                int di = kind > 0 ? midibus_plug(kind, nf > 1 ? (int)ini_long(f[1]) & 0xFF : 0) : -1;
                if (di >= 0 && kind == MDK_GS && midi_bus.dev[di].opt > 1) midi_bus.dev[di].opt = 1;   /* 예전 SC-88·88Pro → GS SoundFont */
                if (di >= 0) { MidiDev *d = &midi_bus.dev[di];
                    if (nf > 2) d->rx_mask = (uint16_t)ini_long(f[2]);
                    if (nf > 3) { long x = ini_long(f[3]); d->vol = (uint8_t)(x < 0 ? 0 : x > 100 ? 100 : x); }
                    if (nf > 4) { long x = ini_long(f[4]); d->pan = (int8_t)(x < -100 ? -100 : x > 100 ? 100 : x); }
                    if (nf > 5) d->mute = ini_long(f[5]) != 0;
                    if (nf > 6) { long x = ini_long(f[6]); d->port = (uint8_t)(x == MDP_MSXMIDI || x == MDP_SFG ? x : MDP_ANY); } }
            }
            else if (!strcmp(k, "MidiOut")) ini_old_out = ini_long(v) != 0;   /* 3.4.x: 1 = 파일 */
            else if (!strcmp(k, "MidiSynth")) { long x = ini_long(v); ini_old_syn = x < 0 || x > 3 ? GM_FM : (int)x; midi_synth = ini_old_syn == GM_MT32 && MT32_OK ? GM_MT32 : ini_old_syn == GM_SF2 || ini_old_syn == GM_MT32 ? GM_SF2 : GM_FM; }
            else if (!strcmp(k, "SoundFont")) copy_str(sf_name, sizeof sf_name, v);
            else if (!strcmp(k, "GsSoundFont")) copy_str(gs_name, sizeof gs_name, v);
            else if (!strcmp(k, "MtCtlRom")) copy_str(ini_mtc, sizeof ini_mtc, v);   /* 5.2.159 */
            else if (!strcmp(k, "MtPcmRom")) copy_str(ini_mtp, sizeof ini_mtp, v);   /* 5.2.57 */
            else if (!strcmp(k, "MtRom2")) copy_str(ini_mt2, sizeof ini_mt2, v);   /* 5.2.173 */
            else if (!strcmp(k, "Sc55Set")) { long n = ini_long(v); sc55_set = n >= 0 && n <= 2 ? (int)n : 0; }   /* 5.2.171 */
            else if (!strcmp(k, "DriveSound")) mix_mute[MIX_IO] = ini_long(v) == 0;
            else if (!strncmp(k, "Mix", 3) || !strncmp(k, "Mute", 4)) {
                int mute = k[1] == 'u';
                for (int c = 0; c < MIX_N - 1; c++)
                    if (mix_key_is(k + (mute ? 4 : 3), c)) {
                        long x = ini_long(v);
                        if (mute) { if (c != MIX_IO) mix_mute[c] = x != 0; } else mix_vol[c] = x < 0 ? 0 : x > 100 ? 100 : (int)x;   /* I-O 끄기는 DriveSound (3.4.3) */
                    }
            }
            else if (!strcmp(k, "Font")) { copy_str(fx_pref, sizeof fx_pref, !strcmp(v, "-") ? "" : v); fx_seen = 1; }
            else if (!strcmp(k, "Language")) {
                lang = !strcmp(v, "en") ? 1 : !strcmp(v, "ja") ? 2 : 0;
                for (int q = 0; q < 7; q++) if (!strcmp(v, lang2_codes[q])) lang = 3 + q;
                if (!strcmp(v, "ar")) lang = 10;
            }
            else if (!strncmp(k, "Cart", 4) && k[4] >= '1' && k[4] <= '5') {   /* 3.6.7: 5 */
                int n = k[4] - '1';
                const char *t = k + 5;
                if (!t[0]) copy_str(cart_path[n], sizeof cart_path[n], v);
                else if (!strcmp(t, "Name")) copy_str(cart_name[n], sizeof cart_name[n], v);
                else if (!strcmp(t, "Mapper")) cart_mapper[n] = (int)ini_long(v);
                else if (!strcmp(t, "Special")) cart_special[n] = (int)ini_long(v);
            }
            else if (!strncmp(k, "Env", 3) && k[3] >= '1' && k[3] <= '8' && !k[4]) env_parse(v);
            else if (!strncmp(k, "Cmos", 4) && k[4] >= '1' && k[4] <= '8' && !k[5]) cmos_parse(v);
            else if (!strcmp(k, "SlotExpand")) { slot_expand = (int)ini_long(v); if (slot_expand < 0 || slot_expand > 2) slot_expand = 0; }
            else if (!strcmp(k, "RomDir") && v[0]) copy_str(rom_path, sizeof rom_path, v);
            else if (!strcmp(k, "RomDirName") && v[0]) copy_str(rom_disp, sizeof rom_disp, v);
        }
        if (cr) end[-1] = '\r';
        *end = save;
        line = *end ? end + 1 : end;
    }
}

/* 4.0.34: 기종별 속성 옵션 — 기종 상태 저장이 켜져 있으면 기종마다 States\\Resume\\<기종>.opt 에 보관 */
static int opt_key(const char *k)
{
    { int i = opt_tab_find(k); if (i >= 0) return fe_opts[i].per_machine; }   /* 5.2.35 설정 표 */
    static const char *const ex[] = { "ResetAfterInsert", "HdResetEject", "HdResetInsert", "VhdResetInsert", "DiskResetAfterInsert", "Monitor", "Scanlines", "MonitorEmu2",
        "FxBlur", "FxGlow", "FxNoise", "Aspect", "VdpCmdTiming", "PrnMode", "SpriteHide", "SpriteNoLimit", "Bright", "Contrast", "Satur", "Gamma", "Vsync", "Joy1", "Joy2",
        "Volume", "Stereo", "MidiOutMode", "MidiInternal", "MidiIn", "MidiSynth", "SoundFont", "GsSoundFont", "DriveSound", "PanaFront", "PanaPause", "AutoCtrl", "BootCtrl", "KeyLayout", 0 };
    for (int i = 0; ex[i]; i++) if (!strcmp(k, ex[i])) return 1;
    if (!strncmp(k, "Mix", 3) || !strncmp(k, "Balance", 7)) return 1;
    if (!strncmp(k, "Mute", 4) && k[4]) return 1;
    return 0;
}
static void opts_path(char *out, int max)
{
    copy_str(out, max, "0:\\States\\Resume\\"); str_append(out, max, machine_name); str_append(out, max, ".opt");
}
static void opts_save(void)
{
    if (!P.write_file || !P.create) return;
    settings_dirty = 1; settings_flush();               /* ini_buf = 지금 값 전체 */
    static char ob[INI_SIZE];
    int o = 0;
    const char *l = (const char *)ini_buf;
    while (*l && o < INI_SIZE - 300) {
        const char *e = l; while (*e && *e != '\n') e++;
        const char *q = l; while (q < e && *q != '=') q++;
        if (q < e && q - l < 40) {
            char k[48]; int n = (int)(q - l); memcpy(k, l, n); k[n] = 0;
            if (opt_key(k)) { int len = (int)(e - l); memcpy(ob + o, l, len); o += len; ob[o++] = '\n'; }
        }
        l = *e ? e + 1 : e;
        if (*l == ' ') break;                           /* 고정 크기 파일의 뒤쪽 공백 */
    }
    ob[o] = 0;
    if (P.create("0:\\States", 1) || P.create("0:\\States\\Resume", 1)) return;
    char p[300]; opts_path(p, sizeof p);
    P.write_file(p, (const uint8_t *)ob, (uint32_t)o);
}
static void opts_load(void)
{
    static uint8_t ob[INI_SIZE];
    uint32_t n = 0; char p[300];
    opts_path(p, sizeof p);
    if (!P.read_file || P.read_file(p, ob, INI_SIZE - 1, &n) || !n) return;
    ob[n] = 0;
    ini_parse_lines((char *)ob);
    kbd_apply(); recompute_palette(); if (P.set_palette) P.set_palette();
    layout(); fe_fill(0, 0, D.w, D.h, 1); full_redraw();
    midi_synth_apply();
    settings_changed();
}

void fe_restore(void)
{
    icons_load();
    badges_load();
    romdb_load();                                       /* 첫 롬 삽입 때 멈추지 않게 미리 읽음 */
    vhd_make_dirs();                                    /* 3.7.1: DISK\\DirFolder, VMFolder 기본 폴더 */
    uint32_t size = 0;
    if (!P.read_file) return;
    if (P.read_file(ini_path, ini_buf, INI_SIZE - 1, &size) || !size) {
        ini_path = "0:\\msxemulator.ini";                /* 2.1~2.2.0 USB 호환 */
        if (P.read_file(ini_path, ini_buf, INI_SIZE - 1, &size) || !size) { ini_path = "0:\\x86msx.ini"; fx_startup(); midi_bus_from_ui(); midi_synth_apply(); settings_changed(); return; }   /* 4.0.34: ini가 없으면 바로 새로 만듦 */   /* 3.6.2: 처음 쓸 때도 SoundFont 읽기 */
    }
    if (size == INI_SIZE && ini_path[3] == 'x') {       /* 3.5.5: 고정 크기 파일이면 내용을 기억 (끝 바이트는 늘 줄바꿈) */
        memcpy(ini_prev, ini_buf, INI_SIZE - 1);
        ini_prev[INI_SIZE - 1] = '\n';
        ini_prev_ok = 1;
    }
    if (size > INI_SIZE - 1) size = INI_SIZE - 1;
    ini_buf[size] = 0;
    ini_parse_lines((char *)ini_buf);
    fx_startup();
    if (acpi_mode && P.acpi_mode) P.acpi_mode(1, acpi_line, sizeof acpi_line);   /* 5.2.162 */
    if (audio_out && P.audio_select && P.audio_select(audio_out, snd_status, sizeof snd_status)) {   /* 저장된 출력 장치 */
        P.audio_select(0, snd_status, sizeof snd_status);  /* 없으면 이번만 자동, 선택은 그대로 저장 */
        toast(T(S_AUDIO_FALLBACK), 0);
    }
    sc_file_load();                                     /* 3.3.1 KBD-03: 단축키 파일이 있으면 그것을 따름 */
    sc_force_vol();                                     /* 3.7.2: 한 번만 음량 = LCtrl+↑/↓ */
    sc_move_4034();                                     /* 4.0.34: 한 번만 새 단축키로 */
    sc_move_4035();                                     /* 4.0.34: 믹서 F12, 입력장치 F5, 스테레오 RAlt+F12, .prn F10 */
    sc_remove_5277();                                   /* 없앤 단축키 15개를 저장된 표에서도 한 번 지움 */
    sc_apply_5280();                                    /* 새 단축키 배치 (목록에 없는 기능은 그대로) */
    sc_apply_5281();                                    /* RCtrl/RAlt+F6~F9 새 배치, RCtrl+RShift+D 없앰 */
    sc_apply_5282();                                    /* 음량 LAlt+↑/↓, 속도 LAlt+-/+ (옛 LCtrl) */
    if (aspect_mode) { layout(); fe_fill(0, 0, D.w, D.h, 1); full_redraw(); }   /* 3.2.15: 저장된 가로 비율 */
    recompute_palette();
    if (P.set_palette) P.set_palette();
    /* 기종: 저장된 기종이 지금과 다르면 바꾼다 (실패하면 지금 기종 유지) */
    /* 전체 상태 저장이 꺼져 있어도 고른 기종은 늘 기억 (예전: OFF = 늘 C-BIOS 로 시작) */
    if (want_machine[0] && strcmp(want_machine, machine_name)) {
        char err[160];
        static MachineProfile prof;
        if (!machine_load(M, P.read_file, want_machine, &prof, err, sizeof err)) {
            copy_str(machine_name, sizeof machine_name, want_machine);
            layout();
            fe_fill(0, 0, D.w, D.h, 1);
        }
        else toast(T(S_MACHINE_FAIL), err);
    }
    /* 롬팩·디스크·카세트는 비어 있는 상태로 시작, 기종 환경(슬롯 확장·특수 카트리지)만 되살림 */
    int any = 1;
    env_apply();
    x86midi_check(1);                                   /* GT 가 아닌 기종 + MIDI 기계 → X86MSX MIDI 카트리지 */
    for (int q = MSX_INT_CART; q < MSX_CART_ALL; q++) sram_load(q);   /* 3.1.0: 내장 PAC 내용 (3.6.8: 내장 자리 모두) */
    fw_sram_load();                                     /* 3.4.0 */
    update_has_fw();
    for (int d = 0; d < 2; d++) { disk_path[d][0] = 0; disk_name[d][0] = 0; }
    if (any) { msx_reset(M); fw_boot_ctrl(); }
    kbd_apply();
    rtc_sync(1);
    full_redraw();
    if (!ini_mo_seen && (ini_old_out >= 0 || ini_old_syn >= 0))   /* 3.5.4: 3.4.x 설정 옮기기 (파일 > 음원 > 없음) */
        midi_out = ini_old_out == 1 ? 3 : ini_old_syn == 0 ? 0 : 1;
    if (ini_mididev_seen) midi_ui_from_bus(); else midi_bus_from_ui();
    {   /* ini 의 롬 이름을 기계에 넣음 (첫 MT-32 = MtCtlRom·MtPcmRom, 둘째 = MtRom2 "제어|PCM") */
        int d0 = mt_dev_n(0), d1 = mt_dev_n(1);
        if (d0 >= 0) { copy_str(midi_bus.dev[d0].rom_ctl, sizeof midi_bus.dev[d0].rom_ctl, ini_mtc); copy_str(midi_bus.dev[d0].rom_pcm, sizeof midi_bus.dev[d0].rom_pcm, ini_mtp); }
        if (d1 >= 0) { char *bar = ini_mt2; while (*bar && *bar != '|') bar++; char sv = *bar; *bar = 0; copy_str(midi_bus.dev[d1].rom_ctl, sizeof midi_bus.dev[d1].rom_ctl, ini_mt2); *bar = sv; copy_str(midi_bus.dev[d1].rom_pcm, sizeof midi_bus.dev[d1].rom_pcm, sv ? bar + 1 : ""); }
    }   /* MidiDev 줄이 있으면 그것이 기준, 없으면 예전 설정에서 기계 하나 */
    midi_synth_apply();                                 /* 3.4.5 SND3-07 */
    if (!P.boot_failed) resume_load();                  /* 2.11: 지난번 끝낼 때의 상태로 이어서 (4.0.34: 지난번 켜다 멈췄으면 건너뜀) */
    x86midi_check(1);                                   /* GT 가 아닌 기종 + MIDI 기계 → X86MSX MIDI 카트리지 */
}

/* ------------------------------------------------------------------ */
/* 공개 함수                                                            */
/* ------------------------------------------------------------------ */
void fe_set_status_text(const char *wc, const char *snd)
{
    if (wc) copy_str(wc_status, sizeof wc_status, wc);
    if (snd) copy_str(snd_status, sizeof snd_status, snd);
}

/* 화면 맞추기 상태 (1초마다) — src 0 = 없음. 진단 화면 System 칸에 "GPU pipe 0, 60.00 Hz, lock 60/60" */
void fe_set_vsync(const char *src, uint32_t hz100, int locked, int frames)
{
    char t[64] = "";
    if (!src || !hz100) { copy_str(vs_status, sizeof vs_status, "off (timer pacing)"); return; }
    str_append(t, sizeof t, src);
    str_append(t, sizeof t, ", ");
    num_append(t, sizeof t, hz100 / 100, 0, ' ');
    str_append(t, sizeof t, ".");
    num_append(t, sizeof t, hz100 % 100, 2, '0');
    str_append(t, sizeof t, " Hz, lock ");
    num_append(t, sizeof t, (uint32_t)locked, 0, ' ');
    str_append(t, sizeof t, "/");
    num_append(t, sizeof t, (uint32_t)frames, 0, ' ');
    copy_str(vs_status, sizeof vs_status, t);
}

void fe_set_usb_text(const char *usb) { copy_str(usb_status, sizeof usb_status, usb); }
void fe_set_speed(int speed_pct, int busy_pct) { measured_speed = speed_pct; measured_busy = busy_pct; }
int fe_speed_pct(void)
{
    if (disk_accel && run_state == RUN_ON) {            /* 플로피 모터가 돌며 접근했거나 하드 디스크를 읽고 쓰는 중이면 제한 없이 */
        for (int d = 0; d < 2; d++) if (disk_size[d] && M->fdc.motor && M->cpu.cycles - M->fdc.last_access[d] < 3579545u / 6) return 0;
        if ((hd_secs || hd2_secs) && P.ms() - hd_last_ms < 200) return 0;
    }
    return (turbo_hold || speed_max || (prn_printing() && run_state == RUN_ON)) ? 0 : speed;   /* 4.0.4: 인쇄하는 동안은 제한 없이 (프린터는 느릴 이유가 없음) */
}
/* 2.14: 한 프레임의 T-state (NTSC 262줄 x 228 = 59,736, PAL 313줄 = 71,364). 플랫폼이 기다릴 시간 */
uint32_t fe_frame_tstates(void) { return (uint32_t)msx_frame_lines(M) * MSX_TPL; }
static int frame_samples(void) { return (int)(48000u * fe_frame_tstates() / 3579545u); }

void fe_init(const FeDisplay *d, const FePlatform *p, MSX *m, const char *rom_dir, const char *rom_dir_disp)
{
    sp_menu_build();                                    /* 특수 카트리지 메뉴 (src/spcat.c 표) */
    D = *d;
    P = *p;
    M = m;
    mix_defaults();                                     /* 믹서 처음 값 (설정 파일이 덮어씀) */
    machine_rom_find = rom_find_cb; machine_rom_seen = rom_seen_cb;   /* 3.3.2 ACC-04 */
    copy_str(rom_path, sizeof rom_path, rom_dir);
    copy_str(rom_disp, sizeof rom_disp, rom_dir_disp);
    layout();

    if (D.h >= 1000) { F = &font_small; S = 2; } else { F = &font_large; S = 1; }
    LH = (uint32_t)F->height * S;
    SF = S >= 2 ? &font_large : &font_small; SLH = (uint32_t)SF->height;
    vdp_default_palette(&M->vdp);
    recompute_palette();
    if (P.set_palette) P.set_palette();
    input_init(&in);
    fe_fill(0, 0, D.w, D.h, 1);
    full_redraw();
}


/* 소리 상태 기록 — 소리가 갑자기 가청 밖으로 올라가거나(고주파 잡음·찢김), 계속 크게 부풀거나, MIDI는 계속 보내는데 출력이 무음이 되면
 * 그 순간 직전 48초의 초당 기록(최대 크기·평균 크기·고주파 비율·넘침·MIDI 바이트·MIDI 음량·음량)을 LOG\SNDLOG.TXT 에 남긴다 */
static char sl_ring[48][96]; static int sl_n;
static void snd_watch(const int16_t *s, int n)
{
    static int fr, peak, clip; static uint64_t sabs, sdif; static int16_t prev;
    static uint32_t mb0, last_peak_s, hf_run, clip_run, sil_run, last_save, secs; static int had_loud;
    for (int i = 0; i < n; i++) {
        int v = s[i], a = v < 0 ? -v : v, d = v - prev; if (d < 0) d = -d;
        prev = (int16_t)v; if (a > peak) peak = a; if (a >= 32000) clip++; sabs += (uint32_t)a; sdif += (uint32_t)d;
    }
    if (++fr < 60) return;
    fr = 0; secs++;
    uint32_t mb = msx_midi_int_bytes, dm = mb - mb0; mb0 = mb;
    int hfr = sabs ? (int)(sdif * 100 / sabs) : 0;      /* 100 = 인접 표본 차이가 평균 크기와 같음 (보통 음악은 10~60, 고주파 잡음은 150 이상) */
    char l[96]; l[0] = 0;
    str_append(l, sizeof l, "t "); num_append(l, sizeof l, secs, 0, ' ');
    str_append(l, sizeof l, "s peak "); num_append(l, sizeof l, (uint32_t)peak, 0, ' ');
    str_append(l, sizeof l, " avg "); num_append(l, sizeof l, (uint32_t)(sabs / (uint32_t)(60 * (n ? n : 1))), 0, ' ');
    str_append(l, sizeof l, " hf% "); num_append(l, sizeof l, (uint32_t)hfr, 0, ' ');
    str_append(l, sizeof l, " clip "); num_append(l, sizeof l, (uint32_t)clip, 0, ' ');
    str_append(l, sizeof l, " midi+ "); num_append(l, sizeof l, dm, 0, ' ');
    str_append(l, sizeof l, " lvl "); num_append(l, sizeof l, (uint32_t)gmi_level(GM_INST_GT), 0, ' ');
    str_append(l, sizeof l, " guard "); num_append(l, sizeof l, gm_guard_hits, 0, ' ');
    str_append(l, sizeof l, " vol "); num_append(l, sizeof l, (uint32_t)volume, 0, ' '); str_append(l, sizeof l, muted ? " mute" : "");
    copy_str(sl_ring[sl_n % 48], 96, l); sl_n++;
    int total = 60 * (n ? n : 1);
    hf_run = (peak > 4000 && hfr > 150) ? hf_run + 1 : 0;
    clip_run = clip * 4 > total ? clip_run + 1 : 0;
    if (peak > 1500) { had_loud = 1; last_peak_s = secs; }
    sil_run = (peak <= 2 && had_loud && dm > 0 && volume > 0 && !muted) ? sil_run + 1 : 0;
    const char *why = hf_run >= 2 ? "HIGH-FREQ NOISE (hf% > 150, peak > 4000)" : clip_run >= 2 ? "CLIPPING (over 25% of samples at full scale)" : sil_run >= 4 ? "SILENT while MIDI bytes keep coming" : 0;
    peak = clip = 0; sabs = sdif = 0;
    if (why && secs - last_save >= 20 && P.write_file && P.create) {
        last_save = secs; static char fb[48 * 100 + 400]; fb[0] = 0;
        str_append(fb, sizeof fb, "X86MSX " MSXEMU_VERSION " sound log\r\ntrigger: "); str_append(fb, sizeof fb, why);
        str_append(fb, sizeof fb, "\r\nlast loud second: t "); num_append(fb, sizeof fb, last_peak_s, 0, ' ');
        str_append(fb, sizeof fb, "\r\nmachine "); str_append(fb, sizeof fb, machine_name);
        str_append(fb, sizeof fb, "\r\nsound "); str_append(fb, sizeof fb, snd_status);
        str_append(fb, sizeof fb, "\r\n-- per-second (oldest first) --\r\n");
        int c = sl_n < 48 ? sl_n : 48;
        for (int k = 0; k < c; k++) { str_append(fb, sizeof fb, sl_ring[(sl_n - c + k) % 48]); str_append(fb, sizeof fb, "\r\n"); }
        P.create("0:\\LOG", 1); P.create("0:\\LOG\\SNDLOG.TXT", 0);
        P.write_file("0:\\LOG\\SNDLOG.TXT", (const uint8_t *)fb, (uint32_t)strlen(fb));
    }
}
static void play_audio(void)
{
    mix_vol[MIX_MASTER] = volume; mix_mute[MIX_MASTER] = muted;
    mix_apply();                                        /* 3.4.3 CLEAN-10: 모든 채널 음량·좌우를 매 프레임 (예전에는 일부만, 저장한 M-A·M-S·좌우가 믹서를 열 때까지 안 먹음) */
    static int16_t silence[1024], buf[2048];
    int fs = frame_samples();
    if (!P.audio) return;
    if ((int32_t)(tone_until - P.ms()) > 0) {           /* 시험음: 440 Hz 사인파 (믹서·소리 끔과 상관없이 음량 70%) */
        static const int16_t q[16] = { 0, 3135, 5793, 7568, 8191, 7568, 5793, 3135, 0, -3135, -5793, -7568, -8191, -7568, -5793, -3135 };
        for (int i = 0; i < fs; i++) {
            tone_phase += 440u * 16u * 65536u / 48000u;
            int16_t v = q[(tone_phase >> 16) & 15];
            buf[i] = (int16_t)(v * 3);
        }
        P.audio(buf, fs, 70, 0);
        return;
    }
    int sp = fe_speed_pct();
    if (run_state == RUN_ON && (!ui_active() || live_ui())) { rec_feed(M->psg.buf, M->psg.buf_n); vrec_audio(M->psg.buf, M->psg.buf_n); }   /* 소리 저장 (2.10), 영상 녹화 (3.2.13) */
    if (run_state != RUN_ON || (ui_active() && !live_ui()) || sp == 0 || sp > 200) { vrec_audio(silence, fs); P.audio(silence, fs, 0, 1); return; }   /* 멈춘 동안도 녹화에는 무음을 */
    const int16_t *s = M->psg.buf;
    int n = M->psg.buf_n, st = M->stereo && P.audio2;
    snd_watch(s, n);  /* 3.3.3 스테레오 */
    if (sp == 100) { if (st) P.audio2(M->out_l, M->out_r, n, volume, muted); else P.audio(s, n, volume, muted); return; }
    int out = n * 100 / sp;                             /* 빠르면 솎고, 느리면 늘림 */
    if (out > 2048) out = 2048;
    static int16_t buf_r[2048];
    for (int i = 0; i < out; i++) {
        long k = (long)i * sp / 100 < n ? (long)i * sp / 100 : n - 1;
        buf[i] = st ? M->out_l[k] : s[k];
        if (st) buf_r[i] = M->out_r[k];
    }
    if (st) P.audio2(buf, buf_r, out, volume, muted); else P.audio(buf, out, volume, muted);
}

/* ------------------------------------------------------------------ */
/* 3.0 PC 마우스: 메뉴·창은 포인터로, 포트를 정하면 MSX 화면에서는 MSX 마우스                */
/* ------------------------------------------------------------------ */
static void ui_press(int key, int shift)
{
    uint8_t m0 = mods;
    if (shift) mods |= 0x01;
    ui_pressing = 1;                                    /* 3.0.2: 마우스로 누른 키 (진단 키보드 탭은 이것만 탭 바꾸기로) */
    key_event(key, 1);
    key_event(key, 0);
    ui_pressing = 0;
    mods = m0;
}

/* 포인터 = Icons\POINTER.png (끝 = 왼쪽 위, 없으면 본체 그림). 화면 위에 실제 화소와 섞어 그리고,
 * 그리기 전 아래 화소를 저장해 다음 프레임 처음에 되돌린다 */
#define PTR_MAX 72
static uint8_t ptr_save[PTR_MAX * PTR_MAX * 4];
static uint32_t ptr_sx, ptr_sy, ptr_sw, ptr_sh;         /* 저장한 자리 */
static int ptr_drawn, ptr_used, ptr_btn_prev, ptr_init;
static int ptr_shown, ptr_margin;                /* 3.2.2: PC 포인터 보임(왼쪽 단추로), MSX 마우스 중 포인터가 좌우 여백에 있음 */
static uint32_t ptr_moved_ms;
static uint8_t ptr_builtin[48 * 48 * 4];
static int ptr_built;

static void ptr_restore(void)
{
    if (!ptr_drawn || !D.fb) return;
    uint32_t bpp = D.bpp == 1 ? 1 : 4;
    for (uint32_t r = 0; r < ptr_sh; r++) memcpy(D.fb + (uintptr_t)(ptr_sy + r) * D.pitch + ptr_sx * bpp, ptr_save + r * ptr_sw * bpp, ptr_sw * bpp);
    mark_dirty_rect(ptr_sx, ptr_sy, ptr_sw, ptr_sh);
    ptr_drawn = 0;
}
static void ptr_draw(void)
{
    if (!D.fb) return;
    const uint8_t *px;
    uint32_t sw, sh;
    if (icon_kind[ICON_POINTER] == 2 || icon_kind[ICON_POINTER] == 1) { px = icon_px[ICON_POINTER]; sw = icon_w[ICON_POINTER]; sh = icon_h[ICON_POINTER]; }
    else {
        if (!ptr_built) {                               /* 본체 그림: 바탕 = 테두리(어두움), 강조 = 안(흰색) */
            for (int i = 0; i < 48 * 48; i++) {
                uint32_t am = ((icon_hd[ICON_POINTER][0][i >> 1] >> ((i & 1) * 4)) & 15) * 17, aa = ((icon_hd[ICON_POINTER][1][i >> 1] >> ((i & 1) * 4)) & 15) * 17;
                uint32_t oa = aa + am * (255 - aa) / 255;
                uint8_t *d = &ptr_builtin[i * 4];
                uint32_t c = oa ? (255 * aa) / oa : 0;      /* 흰색 비율 */
                d[0] = d[1] = d[2] = (uint8_t)(c > 255 ? 255 : c); d[3] = (uint8_t)oa;
            }
            ptr_built = 1;
        }
        px = ptr_builtin; sw = sh = 48;
    }
    int mono = icon_kind[ICON_POINTER] == 1;
    uint32_t sz = 22 * (S > 3 ? 3 : S), bpp = D.bpp == 1 ? 1 : 4;
    if (sz > PTR_MAX) sz = PTR_MAX;
    if (ptr_x >= D.w || ptr_y >= D.h) return;
    uint32_t w = sz, h = sz;
    if (ptr_x + w > D.w) w = D.w - ptr_x;
    if (ptr_y + h > D.h) h = D.h - ptr_y;
    ptr_sx = ptr_x; ptr_sy = ptr_y; ptr_sw = w; ptr_sh = h;
    for (uint32_t r = 0; r < h; r++) memcpy(ptr_save + r * w * bpp, D.fb + (uintptr_t)(ptr_y + r) * D.pitch + ptr_x * bpp, w * bpp);
    for (uint32_t oy = 0; oy < h; oy++) {
        uint32_t sy0 = oy * sh / sz, sy1 = (oy + 1) * sh / sz;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        uint8_t *row = D.fb + (uintptr_t)(ptr_y + oy) * D.pitch;
        for (uint32_t ox = 0; ox < w; ox++) {
            uint32_t sx0 = ox * sw / sz, sx1 = (ox + 1) * sw / sz, t[4] = { 0, 0, 0, 0 }, n = 0;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            for (uint32_t sy = sy0; sy < sy1; sy++)
                for (uint32_t sx = sx0; sx < sx1; sx++, n++) {
                    const uint8_t *q = &px[(sy * sw + sx) * 4];
                    uint32_t a = q[3];
                    if (mono) { t[0] += 255 * a; t[1] += 255 * a; t[2] += 255 * a; }
                    else { t[0] += q[0] * a; t[1] += q[1] * a; t[2] += q[2] * a; }
                    t[3] += a;
                }
            uint32_t a = t[3] / n;
            if (!a) continue;
            uint32_t c[3] = { t[0] / t[3], t[1] / t[3], t[2] / t[3] };
            if (D.bpp == 1) {                           /* 8비트 화면: 반 넘게 덮인 점만 */
                if (a >= 128) row[ptr_x + ox] = (uint8_t)(16 + ((c[0] * 5 + 127) / 255) * 35 + ((c[1] * 6 + 127) / 255) * 5 + (c[2] * 4 + 127) / 255);
                continue;
            }
            uint32_t *pp = (uint32_t *)row + ptr_x + ox, old = *pp, o[3];
            if (D.bgr) { o[0] = (old >> 16) & 255; o[1] = (old >> 8) & 255; o[2] = old & 255; }
            else { o[2] = (old >> 16) & 255; o[1] = (old >> 8) & 255; o[0] = old & 255; }
            for (int k = 0; k < 3; k++) o[k] = (o[k] * (255 - a) + c[k] * a) / 255;
            *pp = D.bgr ? (o[0] << 16) | (o[1] << 8) | o[2] : (o[2] << 16) | (o[1] << 8) | o[0];
        }
    }
    mark_dirty_rect(ptr_x, ptr_y, w, h);
    ptr_drawn = 1;
}

/* 이번 프레임에 포인터를 보일지 */
static int ov_contains(uint32_t x, uint32_t y);
static int ptr_wanted(void)
{
    if (!mouse_count || !ptr_used || !mouse_on || mouse_ptr == 3) return 0;   /* 3.4.3: 끔·숨김 */
    if (ui_active()) return mouse_menu;
    if ((ov_on & 3) && ov_contains(ptr_x, ptr_y)) return 1;   /* 3.5.0: 떠 있는 막대 위에서는 늘 보임 */
    if (mouse_ptr == 1) return 0;                       /* 메뉴만 */
    if (mouse_ptr == 2 && !(mouse_port && !ptr_margin)) return 1;   /* 항상 (MSX 마우스로 쓰는 동안은 빼고) */
    if (mouse_port) return ptr_margin && P.ms() - ptr_moved_ms < 1000;                  /* MSX 마우스로 쓰는 중: 포인터가 MSX 화면 밖(좌우 여백)으로 나갔을 때만 */
    return ptr_shown && P.ms() - ptr_moved_ms < 1000;   /* 3.2.2: 왼쪽 단추를 누르면 나타나고, 3초 움직이지 않으면 숨김 */
}

/* 3.5.0 믹서 계기 (도구 막대 음원 계기): 0 PSG, 1 SCC, 2 MSX-MUSIC, 3 PCM */
static int mix_meter_of(int c) { static const int ch[4] = { MIX_PSG, MIX_SCC, MIX_MM, MIX_PCM }; return c >= 0 && c < 4 ? mix_meter[ch[c]] : 0; }
#include "fe_ov.inc"
static void ov_draw_fwd(void) { ov_draw(); }

static int drag_on;                                  /* 3.4.4: 끄는 중인 막대 (누른 자리의 hit 복사본) */
static UiHit drag_h;
static void mouse_frame(void)
{
    if (!P.mouse_read) { M->mouse_port = -1; return; }
    int dx = 0, dy = 0, dz = 0, b = 0;
    {   /* 3.8.2: 마우스가 꽂히거나 뽑히면 기억한 사용/비사용을 다시 적용 */
        char dn[40]; int on, nd = 0;
        while (nd < 8 && P.mouse_dev && P.mouse_dev(nd, dn, sizeof dn, &on)) nd++;
        if (nd != mouse_ndev) { mouse_ndev = nd; if (P.mouse_set) for (int k = 0; k < nd; k++) { P.mouse_dev(k, dn, sizeof dn, &on); P.mouse_set(k, !mouse_wanted_off(k, dn, nd)); } }
    }
    mouse_count = P.mouse_read(&dx, &dy, &dz, &b, mouse_name, sizeof mouse_name);
    if (!mouse_on) { M->mouse_port = -1; M->mouse_btn = 0; return; }   /* 3.4.3 MOUSE-05: 마우스 사용 끔 */
    if (!ptr_init) { ptr_init = 1; ptr_x = D.w / 2; ptr_y = D.h / 2; }
    int ui = ui_active(), now = (int)P.ms();
    int msx_mode = mouse_port && !ui;                   /* 3.2.4: 저절로 — MSX 화면 안 = MSX 마우스, 밖·메뉴·창 = PC 포인터 */
    M->mouse_port = (int8_t)(mouse_port ? mouse_port - 1 : -1);
    static int btn_hold;                                /* 메뉴·창에서 누른 단추는 뗄 때까지 MSX로 보내지 않음 */
    if (ui || !msx_mode) btn_hold = b != 0; else if (!b) btn_hold = 0;
    int press = b & ~ptr_btn_prev;
    ptr_btn_prev = b;
    if (dg_mouse_test()) { dg_feed(dx, dy, dz, b); press &= 1; dz = 0; }   /* 3.0.2 진단 마우스 탭: 오른쪽 단추·휠은 시험에만 */
    if (msx_mode) {                                     /* MSX 마우스: 이동량을 MSX 쪽으로 (왼쪽·위가 +) */
        /* 3.2.2: 보이지 않는 포인터도 같이 움직여, MSX 화면 좌우 바깥(여백)으로 나가면 PC 포인터로 — 거기서 왼쪽 단추 = 메뉴 */
        int nx = (int)ptr_x + dx * mouse_sens * (int)S / 100, ny = (int)ptr_y + dy * mouse_sens * (int)S / 100;
        int xmax = (int)D.w - 14 * (int)S;                 /* 여백에서 포인터가 화면 밖으로 숨지 않게 */
        ptr_x = (uint32_t)(nx < 0 ? 0 : nx >= xmax ? xmax : nx);
        ptr_y = (uint32_t)(ny < 0 ? 0 : ny >= (int)D.h ? (int)D.h - 1 : ny);
        int margin = !mouse_lock && (ptr_x < x0 || ptr_x >= x0 + out_w || ((ov_on & 3) && ov_contains(ptr_x, ptr_y)) || (ov_vdrag && (b & 1)));   /* 3.4.3: 수동 잠금이면 여백으로 나가지 않음. 3.5.0: 막대 위 = PC 포인터 */
        if (margin != ptr_margin) { ptr_margin = margin; ptr_used = 1; ptr_moved_ms = (uint32_t)now; }
        if (margin && (dx || dy)) ptr_moved_ms = (uint32_t)now;
        if (margin) {
            M->mouse_btn = 0;
            if (ov_mouse(dx || dy, press, dz, b)) return;                     /* 3.5.0: 막대·판 */
            if (press & 1) { input_init(&in); mq_head = mq_tail = 0; menu_show(); }   /* 3.2.5: 여백에서 왼쪽 단추 = 메뉴 (맨 위끝은 3.5.0부터 도구 막대) */
            return;
        }
        if (run_state == RUN_ON) {
            /* 4.0.4: 가로 비율(정사각·TV·화면 가득)이 달라도 MSX 포인터가 마우스를 따라 가로·세로로 같은 길이만큼 움직이게
             * 가로 이동량을 점 모양(가로폭/세로폭)으로 나눔. 소수는 버리지 않고 모아 두어 느린 움직임(1~2 카운트)도 빠짐없이 */
            static int64_t acc_x, acc_y;
            int64_t kx = out_w ? ((int64_t)out_h * (int64_t)vdp_hres(&M->vdp) * 65536) / ((int64_t)FRAME_LINES * out_w) : 65536;   /* 4.0.4: SCREEN 6·7은 점이 반 폭이라 가로로 두 배 */
            if (kx < 8192) kx = 8192; else if (kx > 524288) kx = 524288;
            acc_x += (int64_t)(mouse_invx ? -dx : dx) * mouse_sens * kx / 100;
            acc_y += (int64_t)(mouse_invy ? -dy : dy) * mouse_sens * 65536 / 100;
            int64_t wx = acc_x >> 16, wy = acc_y >> 16;
            acc_x -= wx * 65536; acc_y -= wy * 65536;
            M->mouse_cx -= (int)wx; M->mouse_cy -= (int)wy;
            M->mouse_btn = (uint8_t)(btn_hold ? 0 : b & 3);
        }
        return;
    }
    ptr_margin = 0;
    M->mouse_btn = 0;
    if (ui && !mouse_menu) return;
    if (dx || dy) {                                     /* PC 포인터 움직이기 (UI 배율만큼 빠르게) */
        int nx = (int)ptr_x + dx * mouse_sens * (int)S / 100, ny = (int)ptr_y + dy * mouse_sens * (int)S / 100;
        ptr_x = (uint32_t)(nx < 0 ? 0 : nx >= (int)D.w ? (int)D.w - 1 : nx);
        ptr_y = (uint32_t)(ny < 0 ? 0 : ny >= (int)D.h ? (int)D.h - 1 : ny);
        ptr_used = 1; ptr_moved_ms = (uint32_t)now;
        if (ui && !(drag_on && (b & 1))) {                /* 끄는 동안은 가리키는 줄을 바꾸지 않음 */
            if (bar_visible) last_menu_key = (uint32_t)now;
            const UiHit *h = hit_at(ptr_x, ptr_y);      /* 가리키면 그 줄을 고름 */
            if (h) h->fn(h, 0);
        }
    }
    if (press) { ptr_used = 1; ptr_moved_ms = (uint32_t)now; }
    if (ui && bar_visible && (dx || dy || press)) menu_mouse = 1;   /* 3.4.6 */
    if (!ui) {                                          /* MSX 화면 위 (3.2.2): 왼쪽 단추 = 포인터 보이기, 오른쪽 단추 = 메뉴. 3.5.0: 맨 위끝 = 도구 막대 */
        if (ov_mouse(dx || dy, press, dz, b)) { ptr_shown = 1; return; }
        if ((press & 1) || dx || dy) ptr_shown = 1;      /* 4.0.29: 움직이면 보이고 1초 멈추면 숨김 */
        if (now - (int)ptr_moved_ms >= 1000) ptr_shown = 0;
        if (press & 2) { ptr_shown = 1; input_init(&in); mq_head = mq_tail = 0; menu_show(); }   /* 3.8.1: 포인터를 숨기지 않음 (메뉴가 닫힌 뒤 마우스가 사라지던 것) */
        return;
    }
    if (press & 1) {
        const UiHit *h = hit_at(ptr_x, ptr_y);
        drag_on = h && (h->fn == hc_sbar || h->fn == hc_slide || h->fn == hc_fx || (h->fn == hc_mix && ptr_y >= (uint32_t)h->b && ptr_y < (uint32_t)(h->b + h->c)));   /* 3.4.4: 스크롤 막대·믹서 막대는 누른 채 끌기 */
        if (drag_on) drag_h = *h;
        if (h) h->fn(h, 1);
        else if (!dialog) menu_close_all();             /* 메뉴 밖을 누르면 닫기 */
    } else if (drag_on && (b & 1)) {
        if (dx || dy) drag_h.fn(&drag_h, 2);
    } else drag_on = 0;
    if ((press & 2) && (dialog || depth)) ui_press(0x01, 0);   /* 오른쪽 단추 = Esc (뒤로). 3.8.1: 맨 위 메뉴 막대에서는 눌러도 닫지 않음 (누를 때마다 열렸다 닫히던 것) */
    if (dz) {                                           /* 휠 = ↑/↓ */
        int n = (dz > 0 ? dz : -dz) * mouse_wheel;     /* 3.4.3: 휠 한 칸 = mouse_wheel줄 */
        if (n > 15) n = 15;
        for (int i = 0; i < n; i++) ui_press(dz > 0 ? 0xC8 : 0xD0, 0);
    }
}

#define TRC(x) fe_trace(x)
void fe_trace_ext(const char *x) { fe_trace_str("hda", x); }
void fe_trace_plat(const char *what, const char *x) { fe_trace_str(what, x); }   /* 플랫폼 줄 (느려짐 감지 등) */
void fe_hot_addrs(uintptr_t a[4]) { a[0] = (uintptr_t)D.fb; a[1] = (uintptr_t)pres_line32; a[2] = (uintptr_t)M; a[3] = M ? (uintptr_t)M->vdp.fb : 0; }   /* 메모리 종류 확인할 자리 */   /* 플랫폼·hda 가 진단 추적에 줄을 보탬 */
static void fe_frame_body(void);
static void fe_frame_core(void);
void fe_frame(void)
{
    dbl_begin();
    fe_frame_core();
    dbl_end();
    fe_trace("frame end (back to platform)"); fe_frame_count++;
}
static void fe_frame_core(void)
{
    fe_trace("frame begin");
    {   /* 5.2.21 사진 대신 LOG: 켠 뒤 10초에 한 번, 진단 창 탭을 바꾸거나 소리를 다시 찾은 뒤, 진단 창에서 프레임이 0.7초 넘게 멈췄으면 LOG\DIAG.TXT */
        static int boot_saved; static uint32_t last_f, last_periodic;
        uint32_t now = P.ms();
        if (!boot_saved && now > 10000) { boot_saved = 1; diag_save_now(); }
        else if (dg_save_due && (int32_t)(now - dg_save_due) >= 0) { dg_save_due = 0; diag_save_now(); }
        else if (boot_saved && now - last_periodic > 60000) { last_periodic = now; diag_save_now(); }   /* 1분마다 LOG\DIAG.TXT 갱신 — 소리가 갑자기 사라진 뒤 그 순간의 소리 장치 상태(stream RUN, peak, re-init, latency)를 남김 */
        else if (last_f && now - last_f > 700 && dialog == 2) dg_save_due = now + 100;
        if (last_f && now - last_f > 1000) stall_note(now - last_f);   /* 1초 넘게 멈췄다 다시 돌면 LOG\STALL.TXT */
        last_f = now;
    }
    TRC("core: after diag save");
    int vl = P.video_lost ? P.video_lost() : 0;
    TRC("core: after video_lost");
    if (vl == 1) {                                      /* 4.0.74: HDMI를 뽑으면 화면·소리 출력을 기본으로 */
        video_out = 0; audio_out = 0; snd_auto_hdmi = 0;
        if (P.audio_select) P.audio_select(0, snd_status, sizeof snd_status);
        toast("HDMI removed: internal screen + speaker", 0);
    } else if (vl == 2) {                               /* 4.0.83: HDMI 소리를 켤 수 없음 → 내장 소리 */
        audio_out = 0; snd_auto_hdmi = 0; menu_dirty = 1; toast(T(S_AUDIO_FALLBACK), 0);
    } else if (vl == 3) {                               /* 4.0.83: HDMI 소리 켜짐 */
        audio_out = 2; menu_dirty = 1;
    }
    if (P.hp_state) {                                   /* 4.0.34: 이어폰 꽂음/뺌 알림 */
        static int hp_last = -1;
        int h = P.hp_state();
        if (h >= 0) {
            if (hp_last >= 0 && h != hp_last)
                toast(lang == 1 ? (h ? "Headphones connected" : "Headphones disconnected") : lang == 2 ? (h ? "ヘッドホン接続" : "ヘッドホン取り外し") : (h ? "이어폰을 꽂았습니다" : "이어폰을 뺐습니다"), 0);
            hp_last = h;
        }
    }
    { static int ok_n; if (ok_n < 300 && ++ok_n == 300 && P.boot_ok) P.boot_ok(); }   /* 4.0.34: 5초쯤 잘 돌면 켜기 성공 */
    ptr_restore();                                      /* 지난 프레임의 포인터 자리를 되돌리고 */
    mouse_frame();
    TRC("core: after mouse_frame");
    fe_frame_body();
    TRC("core: after body");
    if (ptr_wanted()) ptr_draw();                       /* 이번 프레임 그림 위에 다시 그림 */
    TRC("core: after pointer");
}

static void auto_ctrl_check(void)
{
    if (!auto_ctrl || auto_tried || run_state != RUN_ON || ui_active() || ctrl_hold || boot_ctrl) { auto_stuck = 0; return; }
    if (++auto_age > 60 * 45) { auto_stuck = 0; return; }   /* 켜거나 리셋한 지 약 45초 안에서만 — 게임을 하는 도중에는 절대 재시작하지 않음 */
    uint16_t pc = M->cpu.pc;
    uint8_t b0 = M->cpu.mem_read(M->cpu.ctx, pc), b1 = M->cpu.mem_read(M->cpu.ctx, (uint16_t)(pc + 1)), b2 = M->cpu.mem_read(M->cpu.ctx, (uint16_t)(pc + 2));
    int self = (b0 == 0xC3 && b1 == (pc & 255) && b2 == (pc >> 8)) || (b0 == 0x18 && b1 == 0xFE);
    if (pc < 0x0100 || pc >= 0xF000) self = 0;
    if (self) {
        /* 같은 자리(JP $) 에서 150프레임 멈춘 뒤에도, 화면(VRAM)에 "CTRL" 글자가 있을 때만 재시작 (CTRL 을 누르라는 안내가 떠 있는 게임).
         * 전에는 글자를 못 찾아도 카운트만 올리고 다음 프레임에 그냥 재시작해서, C-BIOS 의 "No cartridge found" 같은 정상 대기 화면이 켠 뒤 몇 초 만에 다시 시작되었다 */
        if (auto_stuck < 150) auto_stuck++;
        else if ((auto_age % 30) == 0) {
            const uint8_t *v = M->vdp.vram; int hit = 0;
            for (uint32_t i = 0; i + 3 < 0x4000 && !hit; i++)   /* 화면 글자가 있는 앞쪽 16 KB만 */
                if ((v[i] | 32) == 'c' && (v[i + 1] | 32) == 't' && (v[i + 2] | 32) == 'r' && (v[i + 3] | 32) == 'l') hit = 1;
            if (hit) { auto_busy = 1; power_on(); ctrl_hold = CTRL_HOLD; toast(T(S_AUTO_CTRL_T), 0); }
        }
    }
    else if (auto_stuck > 0) auto_stuck--;
}

/* 파일 창의 매퍼·디스크 종류 알아내기를 그리기에서 떼어 냄.
 * 전에는 줄을 그릴 때마다 그 파일을 읽어(zip이면 통째로 풀어) 키를 누른 채 내리면 한 줄마다 수십~수백 ms가 걸려
 * 목록이 키를 뗀 뒤에도 따라 움직이고 멈춘 듯 보였음 (KEY_X5.0.180: file 89%, 한 번 224 ms).
 * 이제는 키를 0.25초 쉬는 동안 한 프레임에 한 파일씩 알아내고 그 줄을 다시 그린다 */
static void probe_tick(uint32_t now)
{
    if (probe_next < 0) return;
    if (dialog != 1 || rep_key >= 0 || kq_head != kq_tail || now - key_ev_ms < 250) return;
    int e = probe_next;
    probe_next = -1;
    probe_type(e);
    probe_redraw = 1;
    menu_dirty = 1;
}

static int midi_io_on(void)                           /* MSX-MIDI 입출력이 살아 있음 — GT 내장 MIDI 켬, 또는 GT 단자에 모듈이 꽂힘 (내장 끔과 따로), 또는 외장 MSX-MIDI 카트리지 */
{
    int port_dev = 0;
    for (int i = 0; i < midi_bus.n; i++) if (midi_bus.dev[i].port == MDP_ANY || midi_bus.dev[i].port == MDP_MSXMIDI) port_dev = 1;
    return (msx_dev(M, MDEV_MIDI_HW) && (midi_int || port_dev)) || (sp_msxmidi_slot >= 0 && M->cart[sp_msxmidi_slot].present);
}
static void fe_frame_body(void)
{
    auto_ctrl_check();
    msx_set_midi(M, midi_io_on());
    if (midi_bus.def_on != (uint8_t)(msx_dev(M, MDEV_MIDI_HW) && midi_int && !mix_mute[MIX_MIDI_INT])) midi_synth_apply();   /* 기종·내장 MIDI 켜기가 바뀌면 기본 음원도 */   /* 3.6.1: 내장 MIDI 설정 (기종을 바꿔도 늘 맞춤). 5.2.56: 외장 MSX-MIDI 카트리지 */
    msx_io_rebuild(M);                                  /* MIDI 포트 사슬 */
    TRC("body: after io_rebuild");
    uint32_t kl_t0, now = P.ms();
    klog_frame_begin(now, &kl_t0);
    process_keys();
    klog_frame_end(P.ms(), kl_t0);
    TRC("body: after keys");
    probe_tick(now);
    if (rep_key >= 0) {                                 /* 메뉴·대화 창 키 반복 (2.16) */
        if (!ui_active() || !key_down[rep_key]) rep_key = -1;
        else if ((int32_t)(now - rep_due) >= 0) { rep_due = now + 40; rep_now = 1; key_event(rep_key, 1); rep_now = 0; }
    }
    {   /* 뗌 신호를 잃은 키 풀기. PS/2 키보드는 마지막에 누른 키 하나만 자동 반복하므로 그 키만 본다
         * (5.0.180의 '1.5초 신호 없으면 모두 뗌'은 방향키를 누른 채 다른 키를 눌렀다 떼면 방향키까지 떼는 문제가 있어 바꿈).
         * 반복이 오던 키가 0.4초 끊기거나, 누르고 1.1초가 지나도 반복이 시작되지 않으면 뗀 것으로 본다 */
        static uint32_t prev_frame;
        uint32_t fg = prev_frame ? now - prev_frame : 0;
        prev_frame = now;
        if (!fe_usb_kbd_seen && tm_key >= 0 && key_down[tm_key] && fg < 150 && kq_head == kq_tail &&
            (tm_rep || tm_seen) && now - key_hw_ms >= (tm_rep ? 400u : 1100u)) {
            int k = tm_key;
            tm_key = -1;
            klog_note(tm_rep ? "자동 반복이 끊김 → 뗀 것으로" : "자동 반복이 오지 않음 → 뗀 것으로");
            key_event(k, 0);
        }
    }
    if (gap_last) { uint32_t g = now - gap_last; if (g > gap_max) gap_max = g; }   /* 프레임 사이 최대 간격 (1초마다) */
    gap_last = now;
    if (now - gap_reset >= 1000) { gap_show = gap_max; gap_max = 0; gap_reset = now; }
    pf_window(now);
    if (settings_dirty && (int32_t)(now - settings_due) >= 0 && !dialog) settings_flush();
    TRC("body: after settings");
    if (M->tape && M->tape_dirty && !M->tape_motor) {   /* 테이프: 모터가 멈추고 2초 뒤 저장 */
        if (!tape_due) tape_due = now + 2000;
        if ((int32_t)(now - tape_due) >= 0) { tape_flush(); tape_due = 0; }
    } else tape_due = 0;
    if (hd_dirty && now - hd_write_ms >= 2000) hd_flush();   /* 하드 디스크: 마지막 쓰기 2초 뒤 장치에 */
    for (int d = 0; d < 2; d++) {                       /* 디스크: 마지막 쓰기 2초 뒤 저장 */
        if (!M->fdc.drv[d].dirty) { disk_due[d] = 0; continue; }   /* (가상 디스크가 쓴 플로피도 같은 길) */
        if (!disk_due[d]) disk_due[d] = now + 2000;
        if ((int32_t)(now - disk_due[d]) >= 0 && !M->fdc.motor) { disk_flush(d); disk_due[d] = 0; }
    }
    {                                                   /* 2.14: RTC를 PC 시계에 맞춤 (1초마다 확인) */
        static uint32_t rtc_last;
        if (now - rtc_last >= 1000) { rtc_last = now; rtc_sync(0); }
    }
    if (pend_act) {                                     /* 키를 모두 놓았으면(또는 1.5초 지나면) 오래 걸리는 동작 */
        int down = 0;
        for (int k = 0; k < 256 && !down; k++) down = key_down[k];
        if (!down || now - pend_since >= 1500) { int a = pend_act; pend_act = 0; run_pending(a); key_resync(); }
    }
    TRC("body: after pending");
    bgw_step(6);                                        /* 화면·소리·영상 파일을 프레임마다 6 ms 만큼 조금씩 */
    menu_out_ms = 0;                                    /* 4.0.15: 메뉴는 저절로 닫히지 않음 (옛 5초 자동 숨김·포인터가 밖에 0.8초면 닫힘 제거) — Esc나 선택으로만 닫음 */
    int running = run_state == RUN_ON && (((!dialog || live_mixer()) && (menu_under || (!depth && !bar_visible))) || live_pop());   /* 미디어 팝업도 */   /* 메뉴가 보이는 동안 정지 (3.3.3: 믹서 창은 계속) */
    M->vdp.spr_hide = (uint8_t)spr_hide; M->vdp.spr_nolimit = (uint8_t)spr_nolimit;   /* 3.2.0: 기종을 바꾸거나 상태를 불러와도 설정대로 */
    prn_frame(now);                                    /* 3.3.0 프린터 → 파일 */
    M->fw.mat_switch = (uint8_t)fw_switch;             /* 3.4.0 */
    if (running && bas_len) bas_feed();                /* 3.2.7: BASIC 텍스트를 키 버퍼로 */
    if (running && midi_in && msx_midi_jacks(M) && P.midi_read) {   /* 3.4.4 DEV-35: USB MIDI → MIDI IN 단자 받기 (프레임 앞에서). 5.2.56: 고른 단자 (MSX-MIDI·SFG·Music Module) */
        uint8_t mb[256];
        int n = P.midi_read(mb, sizeof mb, midi_in_name, sizeof midi_in_name), j = msx_midi_jacks(M), port = midi_in_port;
        if (!(j >> port & 1)) for (port = 0; port < MDP_N && !(j >> port & 1); port++) ;
        midi_in_dev = n;
        if (n > 0) msx_midi_in_to(M, port, mb, n);
    }
    { PF_BEGIN; vhd_frame(now); PF_END(PF_IO); }      /* 3.7.1 가상 하드 (하이퍼콜·디스크 롬 숨김·USB 반영) */
    TRC("body: after vhd");
    stall_flush();
    if (running) { PF_BEGIN; msx_run_frame(M);
#ifdef FE_UEFI
        { extern int uefi_net_poll(void); extern void uefi_net_rehost(NetCard *); uefi_net_rehost(&M->net); if (M->net.enabled && uefi_net_poll()) msx_net_event(M); }   /* 5.0.57 / 5.0.157 무선이면 호스트 교체 */
#else
        { extern int nethost_poll(void) __attribute__((weak)); if (nethost_poll && M->net.enabled && nethost_poll()) msx_net_event(M); }   /* 5.0.108 */
#endif
        PF_END(PF_CORE); { extern uint64_t msx_synth_cyc; uint64_t sy_ = msx_synth_cyc; msx_synth_cyc = 0; if (sy_ > pf_acc[PF_CORE]) sy_ = pf_acc[PF_CORE]; pf_acc[PF_CORE] -= sy_; fe_pf_add(PF_SYNTH, sy_); } mix_meter_frame(); { PF_BEGIN; midi_frame(now); PF_END(PF_MIDI); } }
    vrec_frame();                                       /* 영상 녹화는 보이는 화면 전체 — 창이 열려 MSX가 멈춰 있어도 */
    TRC("body: before audio");
    if (live_mixer()) { static uint32_t lm; if (now - lm >= 66) { lm = now; menu_dirty = 1; } }   /* 계기 다시 그리기 */
    { PF_BEGIN; play_audio(); PF_END(PF_AUDIO); }
    TRC("body: after audio");

    read_leds();
    md_tick(now);                                      /* 3.5.3 */
    cpu_tick(now);
    v9_tick(now);                                      /* 5.0.178 */
    net_tick();                                          /* 인터넷 아이콘 */
    TRC("body: after ticks");
    {
        {   /* 3.2.2: PSG는 4프레임(약 70 ms) 넘게 이어질 때만 소리로 봄 — 키 누를 때 짧게 나는 소리(클릭·삑)로 아이콘이 켜지지 않게 */
            static int psg_run;
            psg_run = (M->chip_act & 1) ? psg_run + 1 : 0;
            chip_eff = (uint8_t)((M->chip_act & ~1) | (psg_run >= 4 ? 1 : 0));
        }
        for (int c = 0; c < MSX_CH_N; c++) if (snd_active(c)) snd_evt[c] = now ? now : 1;   /* 3.7.0 음원 표 */
        if (M->scc2_act) scc2_evt = now ? now : 1;   /* 5.2.157 */
        static uint32_t chip_sig = 0xFFFFFFFFu;         /* 음원 구성 바뀜 */
        uint32_t cs = (uint32_t)M->opll_on | ((uint32_t)msx_dev(M, MDEV_PCM) << 1) | ((uint32_t)M->audio_on << 2) | ((uint32_t)M->ms_on << 3) | ((uint32_t)M->sfg_on << 28) | ((uint32_t)M->joyrex << 29);
        for (int n = 0; n < MSX_CART_SLOTS; n++) if (M->cart[n].present) cs |= (uint32_t)(M->cart[n].mapper + 1) << (4 + n * 4);
        if (cs != chip_sig) { chip_sig = cs; margin_bd = -1; }
        static uint32_t vis_sig;
        uint32_t vs = 0;
        for (int i = 0; i < LED_COUNT; i++) if (led_vis(i)) vs |= 1u << i;
        if (badge_vis()) vs |= 1u << 30;
        static uint32_t chip_vsig;
        uint32_t cv = 0;
        for (int c = 0; c < MIX_MASTER; c++) if (snd_is_src(c)) {   /* 3.7.0: 음원 표 (보임·노랑), 따로 서명 */
            if (snd_present(c) && chip_vis(c)) cv |= 1u << c;
            if (chip_on(c)) cv |= 1u << (16 + c);       /* 노랑 ↔ 기본색 */
        }
        if (snd_present(MIX_SCC2) && chip_vis(MIX_SCC2)) cv |= 1u << 13;   /* 5.2.157 SCC2 */
        if (chip_on(MIX_SCC2)) cv |= 1u << 29;
        if (vs != vis_sig || cv != chip_vsig) { vis_sig = vs; chip_vsig = cv; margin_bd = -1; }
    }
    if (memcmp(led_now, led_drawn, sizeof led_now)) margin_bd = -1;
    uint8_t bd = run_state == RUN_STOPPED ? 1 : vdp_backdrop(&M->vdp);

    if (running && (M->frame & 31) == 0 && cmos_capture()) settings_changed();   /* 배터리 백업 내용이 바뀌면 저장 */
    {   /* 슬롯 표·카트리지 구성이 바뀌면 열린 대화 창을 다시 그림 (실시간) */
        static uint32_t slot_sig;
        uint32_t sg = slot_map_sig();
        if (sg != slot_sig) { slot_sig = sg; menu_dirty = 1; }
    }
    sram_tick(now);                                     /* 2.11: SRAM 바뀐 내용 저장 */
    TRC("body: after sram");
    if (dialog == 5) {                                  /* 기종 선택: 보이는 줄 중 아직 검사 안 한 것을 프레임마다 몇 개씩 (2.11) */
        int rows = list_rows(), done = 0;
        for (int r = 0; r < rows && mach_top + r < nmach && done < 3; r++) if (!mach_st(mach_top + r)) { mach_check(mach_top + r); done++; }
        if (done) menu_dirty = 1;
    }
    if (rec_on || vrec_on) { static uint32_t last_rec; if (now - last_rec >= 1000) { last_rec = now; margin_bd = -1; } }   /* 녹음 시간 표시 */
    if (dialog == 7 || dialog == 2) { static uint32_t last_sd; if (now - last_sd >= 500) { last_sd = now; menu_dirty = 1; } }
    if (live_mixer()) {                                 /* 3.7.0: 믹서 팝업 — 위쪽은 MSX 화면을 계속 그림 */
        UiRect mo = dialog == 6 && props_tab == 6 ? mixpop_rect() : card_rect();
        { static UiRect last_mo; if (memcmp(&mo, &last_mo, sizeof mo)) { if (last_mo.w) { force_redraw = 1; margin_bd = -1; } last_mo = mo; } }   /* 믹서 채널이 늘거나 줄어 팝업 크기가 바뀌면 뒤(MSX 화면)를 다 다시 그림 */
        int redraw = force_redraw || menu_dirty || margin_bd < 0;
        pres_clip_y = mo.y; pres_clip_x = mo.x; pres_clip_w = mo.w;
        pres_clip_y2 = dialog == 6 && props_tab == 6 ? 0xFFFFFFFFu : mo.y + mo.h;    /* 가운데 팝업: 그림자까지 */
        if (!(dialog == 6 && props_tab == 6)) pres_clip_w = mo.w;
        int sp = fe_speed_pct();
        int pres = force_redraw || margin_bd < 0 || (running && (sp == 100 || now - last_present >= 16));
        if (pres) present();
        pres_clip_y = 0xFFFFFFFFu; pres_clip_y2 = 0xFFFFFFFFu; pres_clip_w = 0;
        if (redraw || (menu_under && pres)) { if (menu_under) draw_menu_under(); draw_dialog(); menu_dirty = 0; }   /* 메뉴에서 연 믹서 — MSX 화면을 새로 그릴 때마다 메뉴도 다시 (팝업 밑) */
        draw_toast_center();
        draw_vol_osd();
        return;
    }
    if (dialog) {                                       /* 대화 창: MSX 화면 자리를 덮음 */
        TRC("body: dialog draw");
        if (margin_bd < 0) draw_margins(bd);
        if (force_redraw || menu_dirty) { if (menu_under && force_redraw) draw_menu_under(); draw_dialog_direct(); force_redraw = 0; menu_dirty = 0; }   /* 메뉴는 카드를 처음·전체로 그릴 때만 밑에 그림 — 키마다 다시 그리면 줄만 고쳐 그리는 창(파일 탐색기) 위에 메뉴가 덧씌워짐 */
        draw_toast_center();                            /* 대화 창 위에도 알림 */
        draw_vol_osd();
        return;
    }
    int sp = fe_speed_pct();
    int need = force_redraw || margin_bd < 0 || (running && (sp == 100 || now - last_present >= 16));
    TRC("body: screen");
    if (need) present();
    TRC("body: after present");
    ov_tick(0);                                        /* 3.5.0: 떠 있는 막대 (밑 줄을 다시 그렸으면 다시) */
    TRC("body: after ov_tick");
    if ((bar_visible || depth) && (need || menu_dirty)) draw_menus();
    TRC("body: after menus");
    if ((bar_visible || depth) && toast_msg[0]) draw_toast_center();   /* 메뉴 위에도 알림 */
    draw_state_osd();                                   /* 2.12: 일시 정지·중지는 가운데에 */
    TRC("body: after osd");
    if (!need && vol_osd_on) draw_vol_osd();            /* 멈춤·일시 정지 중에도 */
    else if (need && (bar_visible || depth) && vol_osd_on) draw_vol_osd();
}

/* 4.0.32: 시험 틀용 — 단축키 표의 r번째 줄을 "키<TAB>동작 이름"으로 (끝이면 0) */
int fe_dump_shortcut(int r, char *out, int max)
{
    int idx[200], n = sced_rows(idx, 200);
    if (r < 0 || r >= n) return 0;
    char k[64], a[160];
    combo_name(sc_tab[idx[r]].vk, sc_tab[idx[r]].mods, k, sizeof k);
    action_name(sc_tab[idx[r]].action, a, sizeof a);
    copy_str(out, max, k); str_append(out, max, "\t"); str_append(out, max, a);
    return 1;
}
