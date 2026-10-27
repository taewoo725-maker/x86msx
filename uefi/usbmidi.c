/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * usbmidi.c - 3.4.4 DEV-35: USB MIDI 입력 장치 (오디오 클래스 01h, 하위 클래스 03h MIDIStreaming)
 *
 * 펌웨어 USB 스택(허브 포함)의 인터페이스 핸들 가운데 MIDIStreaming을 찾아 IN 끝점에서 USB-MIDI 이벤트
 * 패킷(4바이트: 케이블·CIN, MIDI 1~3바이트)을 받아 MIDI 바이트열로 바꾼다.
 *   인터럽트 끝점: 비동기 인터럽트 전송(콜백이 고리 버퍼에 넣음)
 *   벌크 끝점(대부분): UEFI는 벌크를 비동기로 받을 수 없어, 읽을 때 1 ms 기다리는 동기 전송 (MIDI 입력을 켰을 때만)
 * 장치는 2초마다 다시 찾는다(꽂고 뽑기). 실기 확인 필요: QEMU에는 USB MIDI 장치가 없다.
 * 3.5.4: 출력도 — 같은 MIDIStreaming 인터페이스의 OUT 끝점으로 MSX-MIDI·SFG가 보낸 바이트를 USB-MIDI 패킷으로 (동기 벌크·인터럽트).
 *        입력만 또는 출력만 있는 장치도 잡는다.
 */
#include "usbgen.h"
#include "efi.h"
#include "usbmidi.h"
#include "../src/midipkt.h"
#include <string.h>

uint32_t timer_ms(void);
static EFI_BOOT_SERVICES *BS;

static struct {
    int used, bulk;
    EFI_HANDLE handle;
    EFI_USB_IO_PROTOCOL *io;
    uint8_t ep, ep_out, out_int;                        /* ep = IN (0이면 없음), ep_out = OUT (0이면 없음) */
    UINTN mps, interval, mps_out;
    uint32_t out_packets;
    int out_err;
    uint16_t vid, pid;
    volatile int error;
    uint32_t packets;
} dev;
static volatile uint8_t ring[1024];
static volatile uint16_t ring_w, ring_r;
static uint32_t seen_gen;                           /* 3.5.5: 마지막으로 본 usb_gen */
static UINTN last_n;
static uintptr_t last_sig;

static const uint8_t cin_len[16] = { 0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1 };   /* CIN → MIDI 바이트 수 */

static void put_packets(const uint8_t *d, UINTN len)
{
    for (UINTN i = 0; i + 4 <= len; i += 4) {
        int n = cin_len[d[i] & 15];
        if (!n || (!d[i] && !d[i + 1])) continue;       /* 빈 패킷 */
        dev.packets++;
        for (int k = 0; k < n; k++) {
            uint16_t w = (uint16_t)((ring_w + 1) % sizeof ring);
            if (w == ring_r) return;                    /* 가득 참: 버림 */
            ring[ring_w] = d[i + 1 + k];
            ring_w = w;
        }
    }
}

static EFI_STATUS callback(void *data, UINTN len, void *ctx, uint32_t status)
{
    (void)ctx;
    if (status) { dev.error = 1; return EFI_SUCCESS; }
    if (data && len) put_packets(data, len);
    return EFI_SUCCESS;
}

static int start(EFI_HANDLE h, EFI_USB_IO_PROTOCOL *io, const EFI_USB_INTERFACE_DESCRIPTOR *ifd)
{
    memset(&dev, 0, sizeof dev);
    dev.handle = h; dev.io = io;
    for (uint8_t i = 0; i < ifd->NumEndpoints; i++) {
        EFI_USB_ENDPOINT_DESCRIPTOR e;
        if (io->UsbGetEndpointDescriptor(io, i, &e) != EFI_SUCCESS) continue;
        int type = e.Attributes & 3;
        if (type != 2 && type != 3) continue;
        UINTN mps = e.MaxPacketSize & 0x7FF;
        if (!mps || mps > 512) mps = 64;
        if (e.EndpointAddress & 0x80) {
            if (dev.ep) continue;
            dev.ep = e.EndpointAddress; dev.mps = mps;
            dev.interval = e.Interval ? e.Interval : 1;
            if (dev.interval > 32) dev.interval = 8;
            dev.bulk = type == 2;
        } else if (!dev.ep_out) { dev.ep_out = e.EndpointAddress; dev.mps_out = mps; dev.out_int = type == 3; }
    }
    if (!dev.ep && !dev.ep_out) return -1;
    EFI_USB_DEVICE_DESCRIPTOR dd;
    memset(&dd, 0, sizeof dd);
    if (io->UsbGetDeviceDescriptor) io->UsbGetDeviceDescriptor(io, &dd);
    dev.vid = dd.IdVendor; dev.pid = dd.IdProduct;
    if (dev.ep && !dev.bulk && io->UsbAsyncInterruptTransfer(io, dev.ep, 1, dev.interval, dev.mps, callback, 0) != EFI_SUCCESS) dev.ep = 0;
    if (dev.ep && dev.bulk && !io->UsbBulkTransfer) dev.ep = 0;
    if (dev.ep_out && !(dev.out_int ? (void *)io->UsbSyncInterruptTransfer : (void *)io->UsbBulkTransfer)) dev.ep_out = 0;
    if (!dev.ep && !dev.ep_out) return -1;
    dev.used = 1;
    return 0;
}

static void scan(void)
{
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &n, &hs) != EFI_SUCCESS) n = 0;
    uintptr_t sig = 0;
    for (UINTN j = 0; j < n; j++) sig = sig * 31 + (uintptr_t)hs[j];
    if (n == last_n && sig == last_sig) { if (hs) BS->FreePool(hs); return; }
    last_n = n; last_sig = sig;
    if (dev.used) {                                     /* 뽑혔는지 */
        int found = 0;
        for (UINTN j = 0; j < n; j++) if (hs[j] == dev.handle) found = 1;
        if (!found) dev.used = 0;
    }
    for (UINTN j = 0; j < n && !dev.used; j++) {
        EFI_USB_IO_PROTOCOL *io;
        EFI_USB_INTERFACE_DESCRIPTOR ifd;
        if (BS->HandleProtocol(hs[j], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
        if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;   /* 디스크립터는 펌웨어가 기억한 것 (전송 없음) */
        if (ifd.InterfaceClass == 0x01 && ifd.InterfaceSubClass == 0x03) start(hs[j], io, &ifd);
    }
    if (hs) BS->FreePool(hs);
}

void midi_usb_init(EFI_BOOT_SERVICES *bs)
{
    BS = bs;
    scan();
    seen_gen = usb_gen;
}

static void dev_name(char *name, int nmax)
{
    if (!name || nmax <= 0) return;
    static const char hx[] = "0123456789ABCDEF";
    char t[32] = "USB MIDI ";
    int k = 9;
    for (int s = 12; s >= 0; s -= 4) t[k++] = hx[(dev.vid >> s) & 15];
    t[k++] = ':';
    for (int s = 12; s >= 0; s -= 4) t[k++] = hx[(dev.pid >> s) & 15];
    t[k] = 0;
    for (k = 0; t[k] && k < nmax - 1; k++) name[k] = t[k];
    name[k] = 0;
}

/* 3.5.4: MIDI 바이트열을 장치로 (n = 0이면 장치 확인만). 출력 끝점이 없으면 -1 */
static MidiPk opk;
int midi_usb_write(const uint8_t *buf, int n, char *name, int nmax)
{
    if (seen_gen != usb_gen) { seen_gen = usb_gen; scan(); }
    if (name && nmax > 0) name[0] = 0;
    if (!dev.used || !dev.ep_out) return -1;
    dev_name(name, nmax);
    static uint8_t pk[512];
    UINTN len = 0, cap = dev.mps_out >= 4 ? dev.mps_out & ~(UINTN)3 : 4;
    for (int i = 0; i <= n; i++) {
        uint8_t o[4];
        if (i < n && midipk_feed(&opk, buf[i], o)) { memcpy(pk + len, o, 4); len += 4; }
        if (len && (len + 4 > cap || i == n)) {         /* 패킷 묶음이 차면, 또는 끝에서 보냄 */
            UINTN l = len;
            uint32_t st = 0;
            EFI_STATUS r = dev.out_int ? dev.io->UsbSyncInterruptTransfer(dev.io, dev.ep_out, pk, &l, 10, &st)
                                       : dev.io->UsbBulkTransfer(dev.io, dev.ep_out, pk, &l, 10, &st);
            if (r == EFI_SUCCESS) dev.out_packets += (uint32_t)(len / 4); else dev.out_err++;
            len = 0;
        }
    }
    return 0;
}

int midi_usb_read(uint8_t *buf, int max, char *name, int nmax)
{
    if (seen_gen != usb_gen) { seen_gen = usb_gen; scan(); }
    if (name && nmax > 0) name[0] = 0;
    if (!dev.used || !dev.ep) return -1;
    if (dev.bulk) {                                     /* 1 ms 동기 벌크 (자료가 없으면 시간 초과) */
        static uint8_t pk[512];
        UINTN len = dev.mps;
        uint32_t st = 0;
        if (dev.io->UsbBulkTransfer(dev.io, dev.ep, pk, &len, 1, &st) == EFI_SUCCESS && len) put_packets(pk, len);
    } else if (dev.error) {
        dev.error = 0;
        dev.io->UsbAsyncInterruptTransfer(dev.io, dev.ep, 0, 0, 0, 0, 0);
        dev.io->UsbAsyncInterruptTransfer(dev.io, dev.ep, 1, dev.interval, dev.mps, callback, 0);
    }
    dev_name(name, nmax);
    int n = 0;
    while (n < max && ring_r != ring_w) { buf[n++] = ring[ring_r]; ring_r = (uint16_t)((ring_r + 1) % sizeof ring); }
    return n;
}

int midi_usb_info(char (*lines)[64], int max)
{
    if (max <= 0) return 0;
    char *q = lines[0];
    const char *t = dev.used && dev.ep ? (dev.bulk ? "USB MIDI in: bulk, packets " : "USB MIDI in: interrupt, packets ") : "USB MIDI in: none";
    while (*t) *q++ = *t++;
    if (dev.used && dev.ep) {
        char d[12]; int k = 0; uint32_t v = dev.packets;
        do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 10);
        while (k) *q++ = d[--k];
    }
    *q = 0;
    if (max < 2) return 1;
    q = lines[1];
    t = dev.used && dev.ep_out ? (dev.out_int ? "USB MIDI out: interrupt, packets " : "USB MIDI out: bulk, packets ") : "USB MIDI out: none";
    while (*t) *q++ = *t++;
    if (dev.used && dev.ep_out) {
        char d[12]; int k = 0; uint32_t v = dev.out_packets;
        do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 10);
        while (k) *q++ = d[--k];
        if (dev.out_err) { const char *e = ", errors"; while (*e) *q++ = *e++; }
    }
    *q = 0;
    return 2;
}
