/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fe.h - 공통 프런트엔드 (레거시·UEFI 공용, 설계서 §7)
 *
 * 화면: MSX 화면을 정수배로 가운데에, 왼쪽 여백에 LED 표시판, 위쪽에 메뉴 막대(F11).
 * 키: 플랫폼이 스캔 코드 세트 1 바이트를 fe_push_key()로 넣으면 blueMSX 단축키 → 메뉴 → MSX 순으로 처리.
 * 시간: 플랫폼은 매 프레임 fe_frame()을 부르고, fe_speed_pct()가 돌려주는 속도에 맞춰 기다린다.
 */
#ifndef FE_H
#define FE_H
#include <stdint.h>
#include "../src/msx.h"
#include "padview.h"

typedef struct {
    uint8_t *fb;                /* 선형 프레임 버퍼 */
    uint32_t pitch;             /* 줄당 바이트 */
    uint32_t w, h;
    int bpp;                    /* 1 (8비트 팔레트) 또는 4 (32비트) */
    int bgr;                    /* bpp 4일 때 1이면 B,G,R,x 순서 */
    uint32_t scale_x, scale_y;  /* MSX 픽셀 확대 배율 (진단 표시용; 실제 배치는 fe가 계산) */
    uint32_t mm_h;              /* 화면 세로 실제 크기(mm, EDID). 모르면 0 */
    uint32_t mm_w;              /* 가로 실제 크기(mm, EDID). 모르면 0. 둘 다 있으면 점이 정사각이 아닌 화면 모드(예: 16:9 판에 1024x768)를 보정 */
} FeDisplay;

/* 3.4.7: 긴 파일 이름 — 이름 하나 UTF-16 255자(UTF-8 765바이트), 경로 전체는 Windows MAX_PATH(260자) 넘게 */
#define FE_KEY_MAX  768
#define FE_PATH_MAX 1024

/* 탐색기 항목 (플랫폼이 채움) */
typedef struct {
    char name[64];              /* 표시 이름 (UTF-8) */
    char key[FE_KEY_MAX];       /* 경로로 쓸 이름 (UTF-8, 비어 있으면 name). 드라이브는 "0:" 같은 값. 3.4.7: Windows 긴 이름 255자까지 */
    uint8_t is_dir;
    char zext[6];               /* 4.0.34: zip이면 안의 파일 확장자 (소문자, 점 포함) */
    uint8_t type_known, user_type, zdisk;   /* zdisk 1 = 롬 창의 zip 안이 디스크뿐, 2 = 디스크 창의 zip 안에 디스크 파일이 없음(zx = 안 파일 확장자) */
    char zx[6];
    MapperType type, guess;
    uint32_t size;
    uint16_t year;              /* 0이면 날짜 모름 */
    uint8_t mon, day, hour, min;
} FeDirEntry;

typedef struct {
    uint32_t (*ms)(void);                                   /* 밀리초 단위 단조 시간 */
    int (*hp_state)(void);                                  /* 4.0.34: 헤드폰 꽂힘 1 / 빠짐 0 / 모름 -1 */
    void (*key_poll)(void);                                /* 4.0.34: 막힌 반복 안에서 키보드를 읽음 (없으면 NULL — 인터럽트 방식) */
    int boot_failed;                                        /* 4.0.34: 지난번 켜다가 끝내 못 켜졌음 (표시가 남아 있었음) → 저장된 이어하기 상태는 건너뜀 */
    void (*boot_ok)(void);                                  /* 4.0.34: 잘 켜졌음 (표시를 지움). 없으면 NULL */
    void (*reboot)(void);
    void (*poweroff)(void);                                 /* 없으면 NULL (재부팅으로 대신) */
    void (*audio)(const int16_t *s, int n, int volume, int mute);   /* 없으면 NULL */
    void (*audio2)(const int16_t *l, const int16_t *r, int n, int volume, int mute);   /* 3.3.3 스테레오 (없으면 NULL → 모노) */
    int (*info)(char (*lines)[64], int max);                /* 진단 화면 내용 */
    void (*diag_save)(const char *head);                    /* 4.0.37: 진단 전체를 USB LOG\DIAG.TXT로 저장 (head = 시스템 줄들) */
    int (*video_lost)(void);
    int (*hdmi_active)(void);               /* 4.0.86: HDMI 화면 출력이 켜져 있으면 1 */
                                         /* 4.0.74: 1 = HDMI가 뽑혀 내부 모니터로 돌아왔음 (자주 불림) */
    int (*video_switch)(int idx);                                    /* 4.0.34: 재시작 없이 그 화면 출력으로 (0 = 성공) */
    int (*acpi_mode)(int on, char *out, int outsz);                  /* ACPI 모드 켜기 (SCI_EN) — 노트북 EC 키 스캔 실험. 0 = 켜짐, 결과 글 out (UEFI 전용, 없으면 NULL) */
    int (*gpu_try)(int run);                                            /* 4.0.34: 진단 Video 탭 Enter — GPU HDMI 실험 1회 (UEFI 전용, 없으면 NULL) */
    int (*video_diag)(char (*lines)[64], int max);                   /* 4.0.34: 진단 화면 출력 목록 (줄 수) */
    void (*video_rescan)(void);                                      /* 4.0.34: 모니터를 다시 찾음 (꽂은 뒤) */
    int (*video_outputs)(char (*lines)[64], int max, int *active);   /* 4.0.34 화면 출력 목록 (UEFI GOP 핸들마다 한 줄). 없으면 NULL */
    void (*vs_adj)(int d);                                  /* 4.0.29 화면 맞추기 시점 옮기기 (진단 Video 탭 ←/→, 없으면 NULL) */
    /* 폴더 목록: path ""는 최상위(드라이브 목록). 항목 수, 실패 -1 */
    int (*list_dir)(const char *path, FeDirEntry *out, int max);
    /* 3.0.2 디스크 파일 복사: 확장자를 가리지 않고 모든 파일. 없으면 list_dir를 씀 */
    int (*list_dir_all)(const char *path, FeDirEntry *out, int max);
    /* 파일을 buf에 최대 max바이트 읽기. *size = 실제 파일 크기. 성공 0 */
    int (*read_file)(const char *path, uint8_t *buf, uint32_t max, uint32_t *size);
    /* 이미 있는 파일을 len바이트로 다시 쓰기 (크기가 달라도 됨). 성공 0. 없으면 NULL */
    int (*write_file)(const char *path, const uint8_t *data, uint32_t len);
    int (*write_at)(const char *path, uint32_t off, const uint8_t *data, uint32_t len);   /* 3.5.5: 크기 그대로 일부만 (없어도 됨) */
    int (*append)(const char *path, const uint8_t *data, uint32_t len);   /* 파일 끝에 덧붙이기 (조금씩 저장 — MSX 를 멈추지 않으려고). 없어도 됨 */
    /* 새 빈 파일(is_dir 0)이나 폴더(1) 만들기, 이미 있으면 성공. 부모 폴더는 있어야 함. 성공 0, 없으면 NULL (2.9) */
    int (*create)(const char *path, int is_dir);
    /* 3.0.2 파일 지우기 (폴더 안 됨). 성공 0, 없으면 NULL */
    int (*delete_file)(const char *path);
    /* 소리 출력 장치 고르기 (0 자동, 1 아날로그, 2 HDMI). status에 결과. 성공 0, 없으면 NULL */
    int (*audio_select)(int pref, char *status, int len);
    int (*snd_gpio)(void);                          /* 소리 진단 G 키: 코덱 GPIO 값 돌리기 (-1 못 함, 없으면 0) */
    void (*flush)(void);                                    /* 뒤 버퍼를 지금 화면에 (UEFI Blt). 없으면 NULL */
    const char *browse_note;                                /* 탐색기 안내 (없으면 NULL) */
    void (*dac_set)(int first, int n, const uint8_t *rgb8);  /* 8비트 모드 DAC 일부 쓰기 (색 시험). first < 0 이면 기본 팔레트(근사색 포함)로 되돌림 */
    void (*set_palette)(void);                              /* 8비트 모드: M->vdp.palette 를 DAC에 (없으면 NULL) */
    int (*vsync_wait)(void);                                /* 뒤 버퍼를 화면에 복사하기 앞에 새 화면 시작까지 기다림 (레거시 BIOS 부팅, 없으면 NULL) */
    uint8_t *back; uint32_t back_size;                     /* 화면 뒤 버퍼 (BIOS 부팅 전용, 없으면 NULL) */
    void (*pal_get)(uint8_t *rgb);                          /* 8비트 모드: 지금 DAC 256색을 R,G,B 8비트로 (영상 녹화용, 없으면 NULL) */
    /* USB 전체 용량 사용: 현재 파티션·디스크 크기(바이트). 지원 안 하면 NULL 또는 -1 */
    int (*grow_query)(uint64_t *part_bytes, uint64_t *disk_bytes);
    /* 실행: 0 성공(다시 시작 필요), 1 이미 전체 크기, -1 실패 */
    int (*grow_run)(void (*progress)(int percent, const char *what), char *err, int errlen);
    /* 하드 디스크 이미지 (2.8): 파일을 열어 두고 512바이트 섹터 단위로 읽고 쓴다 (이미지 전체를 메모리에 올리지 않음).
     * 한 번에 하나. open 성공 0 (*sectors = 파일 크기 / 512), read·write 성공 0, flush는 쓴 내용을 장치에. 없으면 NULL */
    int (*hd_open)(int unit, const char *path, uint32_t *sectors);   /* 3.4.3 DEV-15: unit 0 마스터, 1 슬레이브 */
    void (*hd_close)(int unit);
    int (*hd_read)(int unit, uint32_t lba, uint8_t *buf512);
    int (*hd_write)(int unit, uint32_t lba, const uint8_t *buf512);
    void (*hd_flush)(void);
    int (*sound_diag)(char (*lines)[64], int max);          /* 소리 진단 화면 내용 (2.8.1). 없으면 NULL */
    void (*silence)(void);                                  /* 3.8.1: 재부팅·끄기 직전 소리를 완전히 멈춤 (HD Audio 버퍼 비우기). 없으면 NULL */
    int (*pad_view)(int idx, PadView *v, char *name, int max);   /* 3.8.1: 진단 조이스틱 시험 — 풀어 낸 마지막 보고. 패드 없음 -1, 보고 없음 0, 있음 1. 없으면 NULL */
    int (*pad_read)(int idx, char *name, int max);          /* USB 게임패드 idx의 PAD_* 비트, 없으면 -1 (2.13). 없으면 NULL */
    int (*now)(int t[6]);                                   /* PC 시계: 년 월 일 시 분 초, 실패 -1 (2.14). 없으면 NULL */
    /* 3.0 PC 마우스: 지난 호출 뒤 움직인 양(오른쪽·아래가 +), 휠(위로 굴리면 +), 지금 버튼(비트 0 왼쪽, 1 오른쪽, 2 가운데).
     * 마우스 개수를 돌려줌 (0 = 없음). name에 장치 설명. 없으면 NULL */
    int (*mouse_read)(int *dx, int *dy, int *dz, int *buttons, char *name, int max);
    /* 3.0.4 USB 마우스를 읽는 길: own < 0 묻기, 0 펌웨어 드라이버 먼저, 1 X86MSX가 직접. 지금 값. 없으면 NULL */
    int (*mouse_driver)(int own);
    int (*mouse_dev)(int n, char *name, int max, int *on);   /* 3.8.2 n번째 마우스 (없으면 0) */
    void (*mouse_set)(int n, int on);                         /* 3.8.2 사용/비사용 */
    /* 3.0.2 큰 파일 만들기 (하드 디스크 이미지): size 바이트, 앞부분 prefix(plen), 나머지는 0이거나 빈 자리.
     * progress(0~100)를 가끔 부른다. 이미 있으면 실패. 성공 0. 없으면 NULL */
    int (*make_file)(const char *path, uint32_t size, const uint8_t *prefix, uint32_t plen, void (*progress)(int pct));
    uint8_t *scratch;                                       /* 상태 저장·소리 녹음용 큰 작업 메모리 (2.10, 없으면 NULL) */
    uint32_t scratch_size;
    void *v9990;                                            /* 4.0.29 V9990(Gfx9000) 상태 메모리 2 MiB (없으면 NULL) */
    uint8_t *xram;                                          /* 외장 메모리 매퍼 RAM 4 MiB (2.12, 없으면 NULL) */
    uint8_t *vbuf;                                          /* 3.2.13 영상 녹화 버퍼 (없으면 NULL) */
    uint32_t vbuf_size;
    /* 3.4.4 DEV-35: MIDI 입력 장치에서 받은 바이트 (최대 max). 장치가 없으면 -1, name에 장치 설명. 없으면 NULL */
    int (*midi_read)(uint8_t *buf, int max, char *name, int nmax);
    int (*midi_write)(const uint8_t *buf, int n, char *name, int nmax);
    /* 3.7.1 가상 하드: 파일의 off부터 len바이트 (파일 끝 뒤는 0). 성공 0. 없으면 NULL */
    int (*read_at)(const char *path, uint32_t off, uint8_t *buf, uint32_t len);
    uint8_t *vhd_mem;                                       /* 3.7.1 가상 하드 작업 메모리 (24 MiB, 없으면 NULL) */
    uint32_t vhd_mem_size;   /* 3.5.4: USB MIDI 출력 (n = 0이면 확인만, 없으면 -1). 없으면 NULL */
} FePlatform;

/* 4.0.13: DISK·VMFolder·CAS·HARDDISK·BASIC·ROMS는 USB의 Storage Device 폴더 아래 (옛 최상위 위치는 ini를 읽을 때 자동 변환) */
#define FE_STORE      "0:\\Storage Device"
#define FE_DISP_STORE "Drive 1 (boot) > Storage Device"
#define FE_DISK       FE_STORE "\\DISK"
#define FE_VMF        FE_STORE "\\VMFolder"
#define FE_CAS        FE_STORE "\\CAS"
#define FE_HDD        FE_STORE "\\HARDDISK"
#define FE_BAS        FE_STORE "\\BASIC"
#define FE_ROMS       FE_STORE "\\ROMS"
/* rom_dir: 카트리지 탐색기가 처음 열 폴더 (FE_ROMS, 표시 FE_DISP_STORE " > ROMS") */
void fe_init(const FeDisplay *d, const FePlatform *p, MSX *m, const char *rom_dir, const char *rom_dir_disp);
/* 설정 파일을 읽어 언어·음량·카트리지 등 마지막 상태를 되살린다 (기계 조립 뒤 한 번) */
void fe_restore(void);
void fe_set_default_machine(const char *name);   /* 4.0.28: 처음 켠 기종 이름 (저장된 기종이 없을 때 화면에 표시되는 이름) */
void fe_set_status_text(const char *wc, const char *snd);
void fe_set_usb_text(const char *usb);
void fe_set_vsync(const char *src, uint32_t hz100, int locked, int frames);   /* 5.2.17 */
void fe_set_speed(int speed_pct, int busy_pct);         /* 측정한 실제 속도 (진단 화면 표시용) */

/* 4.0.34: 폴더 목록을 '창'으로 나눠 읽기 — 플랫폼 list_dir이 fe_list_skip번째 항목부터 읽고 (한 번 쓰면 0으로), 못 읽은 나머지가 있으면 fe_list_next에 그 번호를, 없으면 -1을 둠 */
extern int fe_list_skip, fe_list_next;
void fe_push_key(uint8_t c);                            /* 인터럽트에서 불러도 됨 */
void fe_frame(void);
void fe_insert_disk(int d, const char *path);
void fe_insert_dir(int d, const char *dir);
const char *fe_test_disk_path(int d);
void fe_test_action(const char *name);
int fe_test_dialog(void);
void fe_test_menu_dump(void (*out)(const char *));                 /* 5.2.130 시험: 메뉴 끝 항목을 키 순서로 */
int fe_dump_shortcut(int r, char *out, int max);
void fe_test_hd(const char *path);
int fe_test_cart_special(int n);                      /* 3.9.4: 시험 틀 */
void fe_test_vhd(const char *dirs, int slot);         /* 3.7.1: 시험 틀 (가상 하드 연결, 콤마로 C, D, …) */                  /* 3.7.1: 시험 틀 (하드 디스크 이미지 넣기) */               /* 3.7.0: 시험 틀 (diskcopy, mixer, keymap, about) */
void fe_insert_special(int n, int kind);               /* 3.6.7: 특수 카트리지 (시험 틀) */
void fe_insert_rom(int n, const char *path);          /* 3.6.1: 롬 넣기 (시험 틀) */            /* 3.6.1: 디렉토리 삽입 + 리셋 (시험 틀) */         /* 3.5.5: 디스크 파일 넣기 (시험 틀) */
int  fe_speed_pct(void);                                /* 목표 속도 %. 0 = 최대 속도(기다리지 않음) */
uint32_t fe_frame_tstates(void);                       /* 2.14: 한 프레임 길이 (Z80 T-state, PAL이면 길어짐) */

/* 모니터 종류를 적용한 MSX 16색 (8비트 화면의 DAC용, [16][3]) */
const uint8_t *fe_palette_rgb(void);
void fe_fatal(const char *msg, const char *detail);
/* 지난번 이후 그린 줄 범위 [*y0, *y1). 없으면 *y1 <= *y0. 가져가면 비움 (UEFI GOP Blt 갱신용) */
void fe_take_dirty(uint32_t *y0, uint32_t *y1);
typedef struct { uint32_t x0, y0, x1, y1; } FeRect;
int fe_take_dirty_rects(FeRect *out, int max);       /* 3.5.5: 바뀐 사각형들 (가져가면 비움) */
void fe_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t c);
uint32_t fe_text(uint32_t x, uint32_t y, const char *utf8, uint8_t fg, uint8_t bg);   /* 그린 폭 반환 */
#endif
/* 4.0.15: 진단 화면의 구간별 시간 (플랫폼이 잰 TSC 클럭을 더함: 5 poll, 6 blit, 7 usbw, 8 idle, 9 file, 10 mouse) */
void fe_pf_add(int k, uint64_t d);
