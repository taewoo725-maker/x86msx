/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * usbpad.c - USB 게임패드 (2.13, UEFI USB I/O 프로토콜 위)
 *
 * 키보드(usbkbd.c)처럼 펌웨어 USB 스택(허브 포함)을 그대로 쓰고, HID 인터페이스 가운데
 * 보고 디스크립터가 조이스틱·게임패드인 것과 Xbox 360 유선 패드(클래스 FF/5D/01)를 찾아
 * 인터럽트 IN 보고를 받는다. 해석은 kernel/hidpad.c. 콜백은 마지막 상태만 적는다.
 */
#include "usbgen.h"
#include "efi.h"
#include "usbpad.h"
#include "../kernel/hidpad.h"
#include <string.h>

uint32_t timer_ms(void);
static EFI_BOOT_SERVICES *BS;

#define MAX_PAD 4
typedef struct {
    int used;
    EFI_HANDLE handle;
    EFI_USB_IO_PROTOCOL *io;
    uint8_t ep, iface;
    UINTN interval, len;
    HidPad pad;
    volatile int state;             /* PAD_* 비트 */
    volatile int error;
    volatile uint32_t reports;
    uint8_t raw[64];                /* 3.8.1: 마지막 보고 (진단 조이스틱 시험) */
    volatile int rawlen;
} Pad;
static Pad pads[MAX_PAD];
static uint32_t seen_gen;                           /* 3.5.5: 마지막으로 본 usb_gen */
/* 2.16: 게임패드가 아닌 HID 인터페이스(키보드의 멀티미디어 키·무선 수신기 등)는 기억해 두고 다시 묻지 않는다.
 * 2.13~2.15는 2초마다 이런 인터페이스에 보고 디스크립터 요청(제어 전송)을 다시 보내, 그때마다 화면·키 입력이 잠깐 멈췄음 */
#define MAX_SKIP 32
static EFI_HANDLE skip[MAX_SKIP];
static int nskip;
static UINTN last_n;
static uintptr_t last_sig;
static uint8_t desc[1024];

static EFI_STATUS callback(void *data, UINTN len, void *ctx, uint32_t status)
{
    Pad *p = ctx;
    if (status) { p->error = 1; return EFI_SUCCESS; }
    if (!data || !len) return EFI_SUCCESS;
    int j = hidpad_decode(&p->pad, data, (int)len);
    if (j >= 0) { p->state = j; p->reports++; if (len > sizeof p->raw) len = sizeof p->raw; memcpy(p->raw, data, len); p->rawlen = (int)len; }
    return EFI_SUCCESS;
}

static int start_pad(EFI_HANDLE h, EFI_USB_IO_PROTOCOL *io, const EFI_USB_INTERFACE_DESCRIPTOR *ifd)
{
    Pad *p = 0;
    for (int i = 0; i < MAX_PAD; i++) if (!pads[i].used) { p = &pads[i]; break; }
    if (!p) return -1;
    HidPad hp;
    uint32_t st;
    if (ifd->InterfaceClass == 0xFF) hidpad_xinput(&hp);
    else {
        memset(desc, 0, sizeof desc);
        EFI_USB_DEVICE_REQUEST rq = { 0x81, 6, 0x2200, ifd->InterfaceNumber, (uint16_t)sizeof desc };  /* 보고 디스크립터 */
        if (io->UsbControlTransfer(io, &rq, EfiUsbDataIn, 1000, desc, sizeof desc, &st) != EFI_SUCCESS) return -1;
        int n = (int)sizeof desc;
        while (n > 0 && !desc[n - 1]) n--;                              /* 실제 길이 (끝은 End Collection 0xC0) */
        if (!hidpad_parse(desc, n, &hp)) return -1;
    }
    uint8_t ep = 0;
    UINTN interval = 8, mps = 64;
    for (uint8_t i = 0; i < ifd->NumEndpoints; i++) {
        EFI_USB_ENDPOINT_DESCRIPTOR epd;
        if (io->UsbGetEndpointDescriptor(io, i, &epd) != EFI_SUCCESS) continue;
        if ((epd.EndpointAddress & 0x80) && (epd.Attributes & 3) == 3) {
            ep = epd.EndpointAddress;
            interval = epd.Interval ? epd.Interval : 8;
            if (interval > 32) interval = 8;
            mps = epd.MaxPacketSize & 0x7FF;
            if (!mps || mps > 64) mps = 64;
            break;
        }
    }
    if (!ep) return -1;
    if (ifd->InterfaceClass == 3) {
        EFI_USB_DEVICE_REQUEST idle = { 0x21, 0x0A, 0, ifd->InterfaceNumber, 0 };   /* SET_IDLE(0) */
        io->UsbControlTransfer(io, &idle, EfiUsbNoData, 1000, 0, 0, &st);
    }
    memset(p, 0, sizeof *p);
    p->handle = h; p->io = io; p->ep = ep; p->iface = ifd->InterfaceNumber; p->interval = interval; p->len = mps;
    p->pad = hp;
    if (io->UsbAsyncInterruptTransfer(io, ep, 1, interval, mps, callback, p) != EFI_SUCCESS) return -1;
    p->used = 1;
    return 0;
}

static void scan(void)
{
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &n, &hs) != EFI_SUCCESS) n = 0;
    uintptr_t sig = 0;                              /* USB 장치 구성이 그대로면 아무것도 안 함 */
    for (UINTN j = 0; j < n; j++) sig = sig * 31 + (uintptr_t)hs[j];
    if (n == last_n && sig == last_sig) { if (hs) BS->FreePool(hs); return; }
    last_n = n; last_sig = sig;
    for (int i = 0; i < nskip; i++) {               /* 없어진 장치는 기억에서 지움 */
        int found = 0;
        for (UINTN j = 0; j < n; j++) if (hs[j] == skip[i]) found = 1;
        if (!found) skip[i--] = skip[--nskip];
    }
    for (int i = 0; i < MAX_PAD; i++) {             /* 뽑힌 패드 정리 */
        if (!pads[i].used) continue;
        int found = 0;
        for (UINTN j = 0; j < n; j++) if (hs[j] == pads[i].handle) found = 1;
        if (!found) pads[i].used = 0;
    }
    for (UINTN j = 0; j < n; j++) {
        int known = 0;
        for (int i = 0; i < MAX_PAD; i++) if (pads[i].used && pads[i].handle == hs[j]) known = 1;
        for (int i = 0; i < nskip; i++) if (skip[i] == hs[j]) known = 1;
        if (known) continue;
        EFI_USB_IO_PROTOCOL *io;
        if (BS->HandleProtocol(hs[j], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
        EFI_USB_INTERFACE_DESCRIPTOR ifd;
        if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;
        int hid = ifd.InterfaceClass == 3 && !(ifd.InterfaceSubClass == 1 && (ifd.InterfaceProtocol == 1 || ifd.InterfaceProtocol == 2));
        int x360 = ifd.InterfaceClass == 0xFF && ifd.InterfaceSubClass == 0x5D && ifd.InterfaceProtocol == 0x01;
        if ((!(hid || x360) || start_pad(hs[j], io, &ifd)) && nskip < MAX_SKIP) skip[nskip++] = hs[j];
    }
    if (hs) BS->FreePool(hs);
}

int pad_usb_init(EFI_BOOT_SERVICES *bs)
{
    BS = bs;
    scan();
    seen_gen = usb_gen;
    return pad_usb_count();
}

int pad_usb_count(void)
{
    int c = 0;
    for (int i = 0; i < MAX_PAD; i++) if (pads[i].used) c++;
    return c;
}

void pad_usb_poll(void)
{
    for (int i = 0; i < MAX_PAD; i++) {
        Pad *p = &pads[i];
        if (!p->used || !p->error) continue;
        p->error = 0;                               /* 전송 오류: 다시 시작 */
        p->io->UsbAsyncInterruptTransfer(p->io, p->ep, 0, 0, 0, 0, 0);
        p->io->UsbAsyncInterruptTransfer(p->io, p->ep, 1, p->interval, p->len, callback, p);
    }
    if (seen_gen != usb_gen) { seen_gen = usb_gen; scan(); }
}

int pad_usb_read(int idx, char *name, int max)
{
    int k = 0;
    for (int i = 0; i < MAX_PAD; i++) {
        if (!pads[i].used) continue;
        if (k++ != idx) continue;
        if (name && max > 0) {
            const char *t = pads[i].pad.kind == 2 ? "Xbox 360" : "USB gamepad";
            int n = 0;
            while (t[n] && n < max - 1) { name[n] = t[n]; n++; }
            name[n] = 0;
        }
        return pads[i].state;
    }
    if (name && max > 0) name[0] = 0;
    return -1;
}

/* 3.8.1: idx번째 패드의 마지막 보고를 풀어 줌. 패드가 없으면 -1, 보고가 아직 없으면 0 (뷰는 0으로 채움) */
int pad_usb_view(int idx, PadView *v, char *name, int max)
{
    int k = 0;
    for (int i = 0; i < MAX_PAD; i++) {
        if (!pads[i].used) continue;
        if (k++ != idx) continue;
        if (name && max > 0) {
            const char *t = pads[i].pad.kind == 2 ? "Xbox 360" : "USB gamepad";
            int n = 0;
            while (t[n] && n < max - 1) { name[n] = t[n]; n++; }
            name[n] = 0;
        }
        memset(v, 0, sizeof *v); v->hat = -1; v->kind = pads[i].pad.kind;
        v->nbtn = pads[i].pad.kind == 2 ? 16 : pads[i].pad.nbtn;
        v->xmin = v->xmax = v->ymin = v->ymax = 0;
        int n = pads[i].rawlen;
        if (n <= 0) return 0;
        uint8_t tmp[64]; memcpy(tmp, pads[i].raw, sizeof tmp);
        return hidpad_view(&pads[i].pad, tmp, n, v) < 0 ? 0 : 1;
    }
    return -1;
}

int pad_usb_info(char (*lines)[64], int max)
{
    int n = 0;
    for (int i = 0; i < MAX_PAD && n < max; i++) {
        if (!pads[i].used) continue;
        char *q = lines[n++];
        const char *t = pads[i].pad.kind == 2 ? "Xbox 360 pad: iface " : "USB gamepad: iface ";
        while (*t) *q++ = *t++;
        *q++ = (char)('0' + pads[i].iface % 10);
        t = ", state ";
        while (*t) *q++ = *t++;
        *q++ = "0123456789ABCDEF"[(pads[i].state >> 4) & 15];
        *q++ = "0123456789ABCDEF"[pads[i].state & 15];
        *q = 0;
    }
    if (n < max) {
        char *q = lines[n++];
        const char *t = "USB gamepads: ";
        while (*t) *q++ = *t++;
        *q++ = (char)('0' + pad_usb_count());
        *q = 0;
    }
    return n;
}
