/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_extdev.inc - 5.2.86: 특수 카트리지(외부장치)의 꽂기·빼기 표. 5.2.85 까지 fe.c 의 cart_remove 와 special_apply switch 에
 * 장치마다 흩어져 있던 코드(MSX-AUDIO·FS-CA1·MoonSound·Joyrex·외장 MSX-MIDI·Gfx9000)를 한 표로 모았다 (구조 개편: 새 외부장치 = 표 한 줄).
 *
 *   ExtDev { how, insert, eject }
 *     how    — src/spcat.h 의 SPK_* (특수 카트리지 메뉴 표 spcat.c 의 "만드는 법")
 *     insert — 이 장치를 슬롯 n 에 꽂음. 돌려주는 값: 0 성공, 양수·음수 = msx_insert_cart 의 실패값(공용 코드가 읽기 실패 알림),
 *              EXT_ABORT = 장치가 이미 알렸으니 공용 알림 없이 -1 로 끝냄. size 는 롬을 읽었으면 그 크기, 롬이 없으면 장치가 정함.
 *     eject  — 슬롯 n 의 카트리지를 뺄 때 이 장치 쪽 정리 (표의 모든 eject 가 차례로 불림, 각자 자기 것인지 확인). 없으면 0.
 * fe.c 는 ext_insert()·ext_eject_all() 만 부른다. 표 차례 = 빼기 차례(5.2.85 까지의 cart_remove 순서와 같음).
 * 장치 자체의 그리기·아이콘 등 프런트엔드 부품은 fe_<장치>.inc (예: fe_v9990.inc) 에 둔다. */

#define EXT_ABORT (-100)
typedef struct {
    uint8_t how;
    int (*insert)(int n, int kind, int quiet, uint32_t *size);
    void (*eject)(int n);
} ExtDev;

/* ---- MSX-AUDIO (Music Module): 롬 없음, 포트 C0h·C1h (2.14) ---- */
static int audio_insert(int n, int kind, int quiet, uint32_t *size)
{
    (void)kind; (void)quiet;
    *size = 16384; memset(cart_buf[n], 0xFF, *size);
    int r = msx_insert_cart(M, n, cart_buf[n], *size, MAPPER_PLAIN);
    if (!r) msx_audio_enable(M, 1);
    return r;
}
/* MSX-AUDIO + BIOS 롬 (Philips NMS-1205·Toshiba HX-MU900: 0x4000부터, blueMSX DB와 같음, 2.16) */
static int audiorom_insert(int n, int kind, int quiet, uint32_t *size)
{
    (void)quiet;
    int r = msx_insert_cart(M, n, cart_buf[n], *size, MAPPER_PLAIN);
    if (!r) { msx_cart_set_base(M, n, 2); msx_audio_enable(M, 1); if (kind == 25) msx_cart_set_uart(M, n, UART_MC6850); }   /* Philips NMS-1205 = MIDI IN/OUT (MC6850) */
    return r;
}
/* FS-CA1: 롬 128 KiB + Y8950 (3.4.1) */
static int ca1_insert(int n, int kind, int quiet, uint32_t *size)
{
    (void)kind; (void)quiet;
    int r = *size == 131072 ? msx_insert_cart(M, n, cart_buf[n], *size, MAPPER_FSCA1) : -1;
    if (!r) msx_audio_enable(M, 1);
    return r;
}
static void audio_eject(int n)       /* MSX-AUDIO 계열 카트리지 (kind 12·25·26·46 = 저장 번호 112·125·126·146) */
{
    if ((cart_special[n] == 112 || cart_special[n] == 125 || cart_special[n] == 126 || cart_special[n] == 146) && !msx_dev(M, MDEV_Y8950)) msx_audio_enable(M, 0);
}

/* ---- Joyrex: 두 번째 PSG (포트 10h~12h). 슬롯은 표시만 (메모리 없음, 2.16) ---- */
static int joyrex_insert(int n, int kind, int quiet, uint32_t *size)
{
    (void)kind; (void)quiet;
    *size = 16384; memset(cart_buf[n], 0xFF, *size);
    int r = msx_insert_cart(M, n, cart_buf[n], *size, MAPPER_PLAIN);
    if (!r) { msx_joyrex_enable(M, 1); sp_joyrex_slot = n; }
    return r;
}
static void joyrex_eject(int n) { if (n == sp_joyrex_slot) { msx_joyrex_enable(M, 0); sp_joyrex_slot = -1; } }

/* ---- 외장 MSX-MIDI 인터페이스: 메모리 창 없음, 포트 E8h~EFh (본체 MSX-MIDI 와 같은 8251·8253, 5.2.56) ---- */
static int msxmidi_insert(int n, int kind, int quiet, uint32_t *size)
{
    (void)kind; (void)quiet;
    *size = 16384; memset(cart_buf[n], 0xFF, *size);
    int r = msx_insert_cart(M, n, cart_buf[n], *size, MAPPER_PLAIN);
    if (!r) { sp_msxmidi_slot = n; msx_set_midi(M, 1); msx_io_rebuild(M); }
    return r;
}
static void msxmidi_eject(int n) { if (n == sp_msxmidi_slot) sp_msxmidi_slot = -1; }   /* 다음 프레임에 prof.midi 를 다시 정함 */

/* ---- MoonSound (OPL4, 포트 7Eh·7Fh·C4h~C7h). 슬롯은 표시만 (3.1.0) ---- */
static int moon_insert(int n, int kind, int quiet, uint32_t *size)
{
    (void)kind;
    static const char *const wr[2] = { "0:\\Machines\\Shared Roms\\MOONSOUND.rom", "0:\\Machines\\Shared Roms\\yrw801.rom" };
    uint32_t ws = 0;
    int ok = 0;
    for (int i = 0; i < 2 && !ok; i++) ok = opl4_memory() && P.read_file && !P.read_file(wr[i], opl4_memory(), 2u << 20, &ws) && ws >= (1u << 20);
    if (!ok) { if (!quiet) toast(T(S_LOAD_ERROR), "Machines\\Shared Roms\\MOONSOUND.rom"); return EXT_ABORT; }
    opl4_set_memory(opl4_memory(), ws > (2u << 20) ? 2u << 20 : ws);
    *size = 16384; memset(cart_buf[n], 0xFF, *size);
    int r = msx_insert_cart(M, n, cart_buf[n], *size, MAPPER_PLAIN);
    if (!r) msx_moonsound_enable(M, 1);
    return r;
}
static void moon_eject(int n) { if (cart_special[n] == 134 && !msx_dev(M, MDEV_MOON)) msx_moonsound_enable(M, 0); }

/* Gfx9000 (V9990) 은 fe_v9990.inc 의 v9_cart_insert·v9_cart_eject */
static int v9_ext_insert(int n, int kind, int quiet, uint32_t *size) { (void)kind; int ab; int r = v9_cart_insert(n, quiet, size, &ab); return ab ? EXT_ABORT : r; }

/* 표: 차례 = 빼기 차례 (예전 cart_remove: MSX-AUDIO → Joyrex → 외장 MSX-MIDI → Gfx9000 → MoonSound) */
static const ExtDev ext_devs[] = {
    { SPK_AUDIO,    audio_insert,    audio_eject },
    { SPK_AUDIOROM, audiorom_insert, 0 },
    { SPK_CA1,      ca1_insert,      0 },
    { SPK_JOYREX,   joyrex_insert,   joyrex_eject },
    { SPK_MSXMIDI,  msxmidi_insert,  msxmidi_eject },
    { SPK_V9990,    v9_ext_insert,   v9_cart_eject },
    { SPK_MOON,     moon_insert,     moon_eject },
};
#define EXT_N ((int)(sizeof ext_devs / sizeof ext_devs[0]))

/* how 에 맞는 장치가 표에 있으면 꽂고 1 을 돌려줌 (결과는 *r), 없으면 0 (fe.c 의 나머지 switch 가 처리) */
static int ext_insert(int how, int n, int kind, int quiet, uint32_t *size, int *r)
{
    for (int i = 0; i < EXT_N; i++) if (ext_devs[i].how == how) { *r = ext_devs[i].insert(n, kind, quiet, size); return 1; }
    return 0;
}
static void ext_eject_all(int n) { for (int i = 0; i < EXT_N; i++) if (ext_devs[i].eject) ext_devs[i].eject(n); }
