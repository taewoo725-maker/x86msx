/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * usbkbd.c - USB 키보드 (UEFI USB I/O 프로토콜 사용)
 *
 * 펌웨어의 USB 스택(xHCI/EHCI, 허브, 저장 장치)은 그대로 두고, 키보드 인터페이스에서만
 * 펌웨어 키보드 드라이버를 떼어낸 뒤 HID 부트 보고(수식 키 + 키 6개)를 비동기 인터럽트
 * 전송으로 직접 받는다. 그래서 누름과 뗌을 모두 알 수 있고, 허브 뒤 키보드와 무선 수신기도
 * 펌웨어가 처리한다. 부팅 USB 메모리도 펌웨어가 계속 다루므로 실행 중 파일 읽기가 가능하다.
 *
 * 콜백(펌웨어 타이머 문맥)은 보고를 장치별 작은 링에 넣기만 하고,
 * 메인 루프의 kbd_usb_poll()이 꺼내 PC 스캔 코드로 바꿔 fe_push_key()한다.
 */
#include "usbgen.h"
#include "efi.h"
#include "usbkbd.h"
#include "../kernel/fe.h"
#include <string.h>

uint32_t timer_ms(void);
static EFI_BOOT_SERVICES *BS;

#define MAX_KBD 8
#define RING 64
typedef struct {
    int used;
    EFI_HANDLE handle;
    EFI_USB_IO_PROTOCOL *io;
    uint8_t ep, iface;
    UINTN interval;
    volatile uint8_t rep[RING][8];
    volatile uint32_t head;
    uint32_t tail;
    volatile int error;
    uint8_t prev[8];
} Kbd;
static Kbd kbds[MAX_KBD];
static uint32_t seen_gen;                           /* 3.5.5: 마지막으로 본 usb_gen */
static int scans;
static UINTN last_n;                                /* 2.16: USB 구성이 그대로면 다시 찾지 않음 */
static uintptr_t last_sig;

/* ------------------------------------------------------------------ */
/* HID 사용 코드 → 스캔 코드 세트 1 (0xE0xx = 확장 키)                    */
/* ------------------------------------------------------------------ */
static const uint16_t hid2sc[0x66] = {
    0, 0, 0, 0,
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,  /* a-p */
    0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,                                      /* q-z */
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,                                      /* 1-0 */
    0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34,  /* 0x28-0x37 */
    0x35, 0x3A,                                                                                      /* / Caps */
    0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,                          /* F1-F12 */
    0xE037, 0x46, 0, 0xE052, 0xE047, 0xE049, 0xE053, 0xE04F, 0xE051, 0xE04D, 0xE04B, 0xE050, 0xE048, /* 0x46-0x52 */
    0x45, 0xE035, 0x37, 0x4A, 0x4E, 0xE01C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49,    /* 0x53-0x61 */
    0x52, 0x53, 0x56, 0xE05D                                                                         /* 0x62-0x65 */
};
static const uint16_t mod2sc[8] = { 0x1D, 0x2A, 0x38, 0xE05B, 0xE01D, 0x36, 0xE038, 0xE05C };

static uint16_t usage_sc(uint8_t u)
{
    if (u < 0x66) return hid2sc[u];
    if (u == 0x90) return 0xE038;                   /* LANG1 한/영 → 노트북 키보드와 같은 E0 38 */
    if (u == 0x91) return 0xE01D;                   /* LANG2 한자 → E0 1D */
    return 0;
}

extern uint8_t fe_usb_kbd_seen;
static void emit(uint16_t sc, int press)
{
    fe_usb_kbd_seen = 1;
    if (!sc) return;
    if (sc & 0xFF00) fe_push_key(0xE0);
    fe_push_key((uint8_t)((sc & 0x7F) | (press ? 0 : 0x80)));
}

static int has(const uint8_t *r, uint8_t u)
{
    for (int i = 2; i < 8; i++) if (r[i] == u) return 1;
    return 0;
}

static void report(Kbd *k, const uint8_t *r)
{
    if (r[2] == 0x01) return;                       /* 롤오버 오류 보고는 무시 */
    uint8_t mc = r[0] ^ k->prev[0];
    for (int b = 0; b < 8; b++)
        if (mc & (1u << b)) emit(mod2sc[b], (r[0] >> b) & 1);
    for (int i = 2; i < 8; i++)
        if (k->prev[i] > 3 && !has(r, k->prev[i])) emit(usage_sc(k->prev[i]), 0);
    for (int i = 2; i < 8; i++)
        if (r[i] > 3 && !has(k->prev, r[i])) emit(usage_sc(r[i]), 1);
    memcpy(k->prev, r, 8);
}

/* 펌웨어 타이머 문맥에서 불림: 링에 넣기만 */
static EFI_STATUS callback(void *data, UINTN len, void *ctx, uint32_t status)
{
    Kbd *k = ctx;
    if (status) { k->error = 1; return EFI_SUCCESS; }
    if (!data || len < 3) return EFI_SUCCESS;
    uint32_t h = k->head;
    if (h - k->tail >= RING) h--;                   /* 가득 차면 가장 새 보고를 버리지 않고 바로 앞 칸을 덮어씀 — 마지막 '모두 뗌' 보고를 잃으면 키가 눌린 채 남아 계속 동작했음 */
    {
        volatile uint8_t *d = k->rep[h % RING];
        const uint8_t *s = data;
        for (UINTN i = 0; i < 8; i++) d[i] = i < len ? s[i] : 0;
        k->head = h + 1;
    }
    return EFI_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* 키보드 찾기와 연결                                                   */
/* ------------------------------------------------------------------ */
static int start_kbd(EFI_HANDLE h, EFI_USB_IO_PROTOCOL *io, const EFI_USB_INTERFACE_DESCRIPTOR *ifd)
{
    Kbd *k = 0;
    for (int i = 0; i < MAX_KBD; i++) if (!kbds[i].used) { k = &kbds[i]; break; }
    if (!k) return -1;
    uint8_t ep = 0;
    UINTN interval = 8;
    for (uint8_t i = 0; i < ifd->NumEndpoints; i++) {
        EFI_USB_ENDPOINT_DESCRIPTOR epd;
        if (io->UsbGetEndpointDescriptor(io, i, &epd) != EFI_SUCCESS) continue;
        if ((epd.EndpointAddress & 0x80) && (epd.Attributes & 3) == 3) {
            ep = epd.EndpointAddress;
            interval = epd.Interval ? epd.Interval : 8;
            if (interval > 32) interval = 8;
            break;
        }
    }
    if (!ep) return -1;

    /* 펌웨어 키보드 드라이버 떼어내기 (USB 장치 자체는 그대로) */
    BS->DisconnectController(h, 0, 0);

    uint32_t st;
    EFI_USB_DEVICE_REQUEST req = { 0x21, 0x0B, 0, ifd->InterfaceNumber, 0 };    /* SET_PROTOCOL(부트) */
    io->UsbControlTransfer(io, &req, EfiUsbNoData, 1000, 0, 0, &st);
    EFI_USB_DEVICE_REQUEST idle = { 0x21, 0x0A, 0, ifd->InterfaceNumber, 0 };   /* SET_IDLE(0) */
    io->UsbControlTransfer(io, &idle, EfiUsbNoData, 1000, 0, 0, &st);

    memset(k, 0, sizeof *k);
    k->handle = h; k->io = io; k->ep = ep; k->iface = ifd->InterfaceNumber; k->interval = interval;
    if (io->UsbAsyncInterruptTransfer(io, ep, 1, interval, 8, callback, k) != EFI_SUCCESS) {
        BS->ConnectController(h, 0, 0, 1);          /* 실패하면 펌웨어 드라이버를 돌려줌 */
        return -1;
    }
    k->used = 1;
    return 0;
}

static void scan(void)
{
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &n, &hs) != EFI_SUCCESS) n = 0;
    uintptr_t sig = 0;
    for (UINTN j = 0; j < n; j++) sig = sig * 31 + (uintptr_t)hs[j];
    if (scans && n == last_n && sig == last_sig) { if (hs) BS->FreePool(hs); return; }
    last_n = n; last_sig = sig;
    /* 뽑힌 키보드 정리 */
    for (int i = 0; i < MAX_KBD; i++) {
        if (!kbds[i].used) continue;
        int found = 0;
        for (UINTN j = 0; j < n; j++) if (hs[j] == kbds[i].handle) found = 1;
        if (!found) kbds[i].used = 0;
    }
    for (UINTN j = 0; j < n; j++) {
        int known = 0;
        for (int i = 0; i < MAX_KBD; i++) if (kbds[i].used && kbds[i].handle == hs[j]) known = 1;
        if (known) continue;
        EFI_USB_IO_PROTOCOL *io;
        if (BS->HandleProtocol(hs[j], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
        EFI_USB_INTERFACE_DESCRIPTOR ifd;
        if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;
        if (ifd.InterfaceClass == 3 && ifd.InterfaceSubClass == 1 && ifd.InterfaceProtocol == 1)
            start_kbd(hs[j], io, &ifd);
    }
    if (hs) BS->FreePool(hs);
    scans++;
}

int kbd_usb_init(EFI_BOOT_SERVICES *bs)
{
    BS = bs;
    scan();
    seen_gen = usb_gen;
    return kbd_usb_count();
}

int kbd_usb_count(void)
{
    int c = 0;
    for (int i = 0; i < MAX_KBD; i++) if (kbds[i].used) c++;
    return c;
}

void kbd_usb_poll(void)
{
    for (int i = 0; i < MAX_KBD; i++) {
        Kbd *k = &kbds[i];
        if (!k->used) continue;
        while (k->tail != k->head) {
            uint8_t r[8];
            for (int b = 0; b < 8; b++) r[b] = k->rep[k->tail % RING][b];
            k->tail++;
            report(k, r);
        }
        if (k->error) {                             /* 전송 오류: 다시 시작 */
            k->error = 0;
            { static const uint8_t none[8]; report(k, none); }      /* 다시 시작하는 동안 뗀 키를 잃지 않게 모두 뗀 것으로 */
            k->io->UsbAsyncInterruptTransfer(k->io, k->ep, 0, 0, 0, 0, 0);
            k->io->UsbAsyncInterruptTransfer(k->io, k->ep, 1, k->interval, 8, callback, k);
        }
    }
    if (seen_gen != usb_gen) { seen_gen = usb_gen; scan(); }   /* USB 장치가 바뀌었을 때만 새로 꽂힌 키보드 찾기 */
}

int kbd_usb_info(char (*lines)[64], int max)
{
    int n = 0;
    for (int i = 0; i < MAX_KBD && n < max; i++) {
        if (!kbds[i].used) continue;
        char *q = lines[n++];
        const char *t = "USB keyboard: interface ";
        while (*t) *q++ = *t++;
        *q++ = (char)('0' + kbds[i].iface % 10);
        t = ", endpoint 0x";
        while (*t) *q++ = *t++;
        *q++ = "0123456789ABCDEF"[kbds[i].ep >> 4];
        *q++ = "0123456789ABCDEF"[kbds[i].ep & 15];
        t = ", OK";
        while (*t) *q++ = *t++;
        *q = 0;
    }
    if (n < max) {
        char *q = lines[n++];
        const char *t = "USB keyboards: ";
        while (*t) *q++ = *t++;
        *q++ = (char)('0' + kbd_usb_count());
        t = "  (firmware USB stack, hubs OK)";
        while (*t) *q++ = *t++;
        *q = 0;
    }
    return n;
}
