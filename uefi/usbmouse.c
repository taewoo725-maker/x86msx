/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * usbmouse.c - USB 마우스 (3.0, UEFI USB I/O 프로토콜 위)
 *
 * 키보드·게임패드처럼 펌웨어 USB 스택(허브 포함)은 그대로 쓰고, HID 마우스 인터페이스
 * (부트 마우스 03/01/02, 또는 보고 디스크립터에 Generic Desktop/Mouse 모음이 있는 03/00/00 — 무선 수신기)를 찾아
 * 펌웨어 마우스 드라이버를 떼고 인터럽트 IN 보고를 직접 받는다. 보고 디스크립터를 읽을 수 있으면 보고 프로토콜
 * (휠 포함), 아니면 부트 프로토콜. 해석은 kernel/hidmouse.c. 콜백은 이동량을 쌓기만 한다.
 * 뽑고 꽂기: 2초마다 USB 핸들 구성이 바뀌었을 때만 다시 찾는다 (2.16 방식).
 */
#include "usbgen.h"
#include "efi.h"
#include "usbmouse.h"
#include "../kernel/hidmouse.h"
#include <string.h>

uint32_t timer_ms(void);
static EFI_BOOT_SERVICES *BS;

#define MAX_MOUSE 4
typedef struct {
    int used;
    EFI_HANDLE handle;
    EFI_USB_IO_PROTOCOL *io;
    uint8_t ep, iface;
    UINTN interval, len;
    HidMouse hm;
    volatile int dx, dy, dz, btn, error;
    volatile uint32_t reports;
    volatile uint32_t rx, boot_fb;               /* 3.0.4 진단: 받은 묶음 수(해석 실패 포함), 부트 형식으로 대신 읽은 수 */
    volatile uint8_t last[8], last_len;
    uint32_t started;
    EFI_SIMPLE_POINTER_PROTOCOL *sp;             /* 3.0.4: 펌웨어 마우스 드라이버로 읽음 (NULL = 직접) */
    uint8_t sub, proto;
    uint8_t sync;                                 /* 3.0.6: 1 = 비동기 대신 매 프레임 동기 읽기 */
    uint32_t sync_err;
    /* 3.0.8 Logitech 수신기 DJ 통로 (HID++ 인터페이스, 보고 0x20/0x21) */
    uint8_t dj, dj_state, dj_mouse_bits, dj_paired;
    uint32_t dj_t0, dj_notes;
    uint8_t off;                                  /* 3.8.2: 1 = 사용 안 함 (속성>마우스) */
} Mouse;
static Mouse mice[MAX_MOUSE];
static uint32_t seen_gen;                           /* 3.5.5: 마지막으로 본 usb_gen */
static UINTN last_n;
static uintptr_t last_sig;
#define MAX_SKIP 32
static EFI_HANDLE skip[MAX_SKIP];
static int nskip;
static uint8_t desc[1024];
/* 3.0.2 진단: 본 HID 인터페이스 (무선 수신기가 보이는지 확인용) */
#define MAX_SEEN 12
static struct { uint16_t vid, pid; uint8_t iface, sub, proto, result; } seen[MAX_SEEN];   /* result 0 키보드, 1 마우스, 2 마우스 아님 */
static int nseen;
static int drv_mode;                                    /* 0 펌웨어 드라이버 먼저, 1 직접(비동기), 2 직접(매 프레임 동기 읽기) */
#define force_own (drv_mode != 0)
static void seen_add(EFI_USB_IO_PROTOCOL *io, const EFI_USB_INTERFACE_DESCRIPTOR *ifd, int result)
{
    EFI_USB_DEVICE_DESCRIPTOR dd;
    memset(&dd, 0, sizeof dd);
    if (io->UsbGetDeviceDescriptor) io->UsbGetDeviceDescriptor(io, &dd);
    for (int i = 0; i < nseen; i++)
        if (seen[i].vid == dd.IdVendor && seen[i].pid == dd.IdProduct && seen[i].iface == ifd->InterfaceNumber && seen[i].proto == ifd->InterfaceProtocol) { seen[i].result = (uint8_t)result; return; }
    if (nseen >= MAX_SEEN) return;
    seen[nseen].vid = dd.IdVendor; seen[nseen].pid = dd.IdProduct; seen[nseen].iface = ifd->InterfaceNumber;
    seen[nseen].sub = ifd->InterfaceSubClass; seen[nseen].proto = ifd->InterfaceProtocol; seen[nseen].result = (uint8_t)result;
    nseen++;
}

static void dj_data(Mouse *m, const uint8_t *d, UINTN len)
{
    if (len < 3 || (d[0] != 0x20 && d[0] != 0x21)) return;           /* HID++ 0x10/0x11 등은 무시 */
    uint8_t idx = d[1], type = d[2];
    if (type == 0x41 && len >= 10 && idx >= 1 && idx <= 6) {        /* 짝지은 장치 알림: d[6..9] = 보내는 보고 종류 비트 */
        uint32_t rt = (uint32_t)d[6] | (uint32_t)d[7] << 8 | (uint32_t)d[8] << 16 | (uint32_t)d[9] << 24;
        m->dj_notes++;
        if (d[3] & 0x20) return;                        /* 목록 비어 있음 */
        m->dj_paired |= (uint8_t)(1u << (idx - 1));
        if (rt & 4) m->dj_mouse_bits |= (uint8_t)(1u << (idx - 1));  /* 표준 마우스 (보고 종류 2) */
        return;
    }
    if (type == 0x02 && len >= 9) {                                 /* DJ 마우스: 버튼 16, X·Y 12비트, 휠 */
        const uint8_t *p = d + 3;
        int x = p[2] | ((p[3] & 0x0F) << 8), y = (p[3] >> 4) | (p[4] << 4);
        if (x & 0x800) x -= 0x1000;
        if (y & 0x800) y -= 0x1000;
        m->dx += x; m->dy += y; m->dz += (int8_t)p[5];
        m->btn = (p[0] & 1) | (p[0] & 2) | ((p[0] & 4) ? 4 : 0);
        m->reports++;
    }
}

static void got_data(Mouse *m, const void *data, UINTN len)
{
    m->rx++;
    const uint8_t *d = data;
    m->last_len = (uint8_t)(len > 8 ? 8 : len);
    for (UINTN i = 0; i < m->last_len; i++) m->last[i] = d[i];
    if (m->dj) { dj_data(m, d, len); return; }
    int dx, dy, dz, b;
    if (!hidmouse_decode(&m->hm, data, (int)len, &dx, &dy, &dz, &b)) { m->dx += dx; m->dy += dy; m->dz += dz; m->btn = b; m->reports++; return; }
    if (!m->hm.boot && len >= 3 && len <= 4) {         /* 3.0.4: 보고 프로토콜로 바꾸지 못한 장치는 부트 형식(버튼, X, Y[, 휠])으로 보냄 */
        HidMouse bm;
        hidmouse_boot(&bm);
        if (!hidmouse_decode(&bm, data, (int)len, &dx, &dy, &dz, &b)) { m->dx += dx; m->dy += dy; m->dz += dz; m->btn = b; m->reports++; m->boot_fb++; }
    }
}

static EFI_STATUS callback(void *data, UINTN len, void *ctx, uint32_t status)
{
    Mouse *m = ctx;
    if (status) { m->error = 1; return EFI_SUCCESS; }
    if (!data || !len) return EFI_SUCCESS;
    got_data(m, data, len);
    return EFI_SUCCESS;
}

static int start_mouse(EFI_HANDLE h, EFI_USB_IO_PROTOCOL *io, const EFI_USB_INTERFACE_DESCRIPTOR *ifd)
{
    Mouse *m = 0;
    for (int i = 0; i < MAX_MOUSE; i++) if (!mice[i].used) { m = &mice[i]; break; }
    if (!m) return -1;
    uint32_t st;
    HidMouse hm;
    int parsed = 0;
    memset(desc, 0, sizeof desc);
    EFI_USB_DEVICE_REQUEST rq = { 0x81, 6, 0x2200, ifd->InterfaceNumber, (uint16_t)sizeof desc };   /* 보고 디스크립터 */
    if (io->UsbControlTransfer(io, &rq, EfiUsbDataIn, 1000, desc, sizeof desc, &st) == EFI_SUCCESS) {
        int n = (int)sizeof desc;
        while (n > 0 && !desc[n - 1]) n--;
        parsed = !hidmouse_parse(&hm, desc, n);
    }
    int bootm = ifd->InterfaceSubClass == 1 && ifd->InterfaceProtocol == 2;
    if (!parsed && !bootm) return -1;                   /* 마우스가 아님 */
    uint8_t ep = 0;
    UINTN interval = 8, mps = 8;
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
    BS->DisconnectController(h, 0, 0);                  /* 펌웨어 마우스 드라이버 떼어내기 */
    if (!parsed || bootm) hidmouse_boot(&hm);
    if (bootm) {                                        /* 3.0.7: 부트 마우스는 BIOS처럼 부트 프로토콜(0)로.
                                                         * Logitech Unifying 수신기(M590)는 보고 프로토콜로 바꾸면 이 PC에서 아무것도 보내지 않았음 */
        EFI_USB_DEVICE_REQUEST pr = { 0x21, 0x0B, 0, ifd->InterfaceNumber, 0 };
        io->UsbControlTransfer(io, &pr, EfiUsbNoData, 1000, 0, 0, &st);
    }
    EFI_USB_DEVICE_REQUEST idle = { 0x21, 0x0A, 0, ifd->InterfaceNumber, 0 };   /* SET_IDLE(0) */
    io->UsbControlTransfer(io, &idle, EfiUsbNoData, 1000, 0, 0, &st);
    memset(m, 0, sizeof *m);
    m->handle = h; m->io = io; m->ep = ep; m->iface = ifd->InterfaceNumber; m->interval = interval; m->len = mps; m->hm = hm;
    m->started = timer_ms();
    m->sync = drv_mode == 2 && io->UsbSyncInterruptTransfer;
    if (!m->sync && io->UsbAsyncInterruptTransfer(io, ep, 1, interval, mps, callback, m) != EFI_SUCCESS) {
        BS->ConnectController(h, 0, 0, 1);
        return -1;
    }
    m->used = 1;
    return 0;
}

/* 3.0.8: Logitech Unifying·Nano 수신기의 HID++ 인터페이스(보고 0x20 DJ). 짝지은 장치 목록을 묻고,
 * 마우스인 칸만 DJ 모드로 바꾸면 그 마우스의 움직임이 이 인터페이스로 온다(Linux hid-logitech-dj와 같은 명령).
 * 키보드 칸은 바꾸지 않아 원래 키보드 인터페이스 그대로. */
static int dj_send(Mouse *m, uint8_t cmd, uint8_t p0, uint8_t p1)
{
    uint8_t r[15];
    memset(r, 0, sizeof r);
    r[0] = 0x20; r[1] = 0xFF; r[2] = cmd; r[3] = p0; r[4] = p1;
    uint32_t st;
    EFI_USB_DEVICE_REQUEST rq = { 0x21, 0x09, 0x0220, m->iface, sizeof r };   /* SET_REPORT 출력, 번호 0x20 */
    return m->io->UsbControlTransfer(m->io, &rq, EfiUsbDataOut, 1000, r, sizeof r, &st) == EFI_SUCCESS ? 0 : -1;
}
static int start_dj(EFI_HANDLE h, EFI_USB_IO_PROTOCOL *io, const EFI_USB_INTERFACE_DESCRIPTOR *ifd)
{
    EFI_USB_DEVICE_DESCRIPTOR dd;
    memset(&dd, 0, sizeof dd);
    if (!io->UsbGetDeviceDescriptor || io->UsbGetDeviceDescriptor(io, &dd) != EFI_SUCCESS || dd.IdVendor != 0x046D) return -1;
    uint32_t st;
    memset(desc, 0, sizeof desc);
    EFI_USB_DEVICE_REQUEST rq = { 0x81, 6, 0x2200, ifd->InterfaceNumber, (uint16_t)sizeof desc };
    if (io->UsbControlTransfer(io, &rq, EfiUsbDataIn, 1000, desc, sizeof desc, &st) != EFI_SUCCESS) return -1;
    int has20 = 0;
    for (unsigned i = 0; i + 1 < sizeof desc; i++) if (desc[i] == 0x85 && desc[i + 1] == 0x20) has20 = 1;   /* Report ID 0x20 */
    if (!has20) return -1;
    Mouse *m = 0;
    for (int i = 0; i < MAX_MOUSE; i++) if (!mice[i].used) { m = &mice[i]; break; }
    if (!m) return -1;
    uint8_t ep = 0;
    UINTN interval = 8, mps = 32;
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
    BS->DisconnectController(h, 0, 0);
    memset(m, 0, sizeof *m);
    m->handle = h; m->io = io; m->ep = ep; m->iface = ifd->InterfaceNumber; m->interval = interval; m->len = mps;
    m->dj = 1; m->started = timer_ms();
    m->sync = drv_mode == 2 && io->UsbSyncInterruptTransfer;
    if (!m->sync && io->UsbAsyncInterruptTransfer(io, ep, 1, interval, mps, callback, m) != EFI_SUCCESS) { BS->ConnectController(h, 0, 0, 1); return -1; }
    m->used = 1;
    m->dj_t0 = timer_ms();
    dj_send(m, 0x81, 0, 0);                             /* 짝지은 장치 알려 줘 */
    return 0;
}

/* 3.0.4: 펌웨어 드라이버(Simple Pointer)가 이 인터페이스에 붙어 있으면 그대로 두고 상태만 읽는다.
 * BIOS 설정 화면에서 움직이는 마우스는 이 길로 반드시 움직인다 (드라이버를 떼지 않으므로). */
static int add_fw(EFI_HANDLE h, EFI_SIMPLE_POINTER_PROTOCOL *sp, EFI_USB_IO_PROTOCOL *io, int iface)
{
    for (int i = 0; i < MAX_MOUSE; i++) if (mice[i].used && mice[i].handle == h) return 0;   /* 이미 있음 */
    Mouse *m = 0;
    for (int i = 0; i < MAX_MOUSE; i++) if (!mice[i].used) { m = &mice[i]; break; }
    if (!m) return -1;
    memset(m, 0, sizeof *m);
    m->handle = h; m->io = io; m->iface = (uint8_t)iface; m->sp = sp;
    m->started = timer_ms();
    EFI_SIMPLE_POINTER_STATE st;
    sp->GetState(sp, &st);                              /* 쌓인 것 비우기 */
    m->used = 1;
    return 0;
}
static int start_fw(EFI_HANDLE h, EFI_USB_IO_PROTOCOL *io, const EFI_USB_INTERFACE_DESCRIPTOR *ifd)
{
    EFI_SIMPLE_POINTER_PROTOCOL *sp = 0;
    if (BS->HandleProtocol(h, &SIMPLE_POINTER_GUID, (void **)&sp) != EFI_SUCCESS || !sp || !sp->GetState) return -1;
    return add_fw(h, sp, io, ifd->InterfaceNumber);
}
/* 3.9.2: 8042(PS/2 컨트롤러)가 있나 */
static int has_8042(void)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"((uint16_t)0x64));
    return v != 0xFF;
}

/* 장치 경로에 USB 마디가 있는 펌웨어 마우스 모두 (Simple Pointer가 USB 핸들의 자식에 붙는 펌웨어도 있음). 찾은 수 */
static int scan_fw_pointers(void)
{
    EFI_HANDLE *hs = 0;
    UINTN n = 0;
    int c = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &SIMPLE_POINTER_GUID, 0, &n, &hs) != EFI_SUCCESS) return 0;
    for (UINTN i = 0; i < n; i++) {
        EFI_DEVICE_PATH *dp;
        EFI_SIMPLE_POINTER_PROTOCOL *sp;
        if (BS->HandleProtocol(hs[i], &DEVICE_PATH_GUID, (void **)&dp) != EFI_SUCCESS) continue;
        if (BS->HandleProtocol(hs[i], &SIMPLE_POINTER_GUID, (void **)&sp) != EFI_SUCCESS || !sp || !sp->GetState) continue;
        int usb = 0, ps2 = 0;
        for (int guard = 0; guard < 32 && !(dp->Type == 0x7F && dp->SubType == 0xFF); guard++) {
            uint16_t len = (uint16_t)(dp->Length[0] | (dp->Length[1] << 8));
            if (dp->Type == 3 && (dp->SubType == 5 || dp->SubType == 0x0F || dp->SubType == 0x10)) usb = 1;   /* USB, USB 클래스, USB WWID */
            if (dp->Type == 2 && dp->SubType == 1 && len >= 8) {    /* ACPI: PNP0Fxx = 펌웨어의 PS/2 마우스 (본체가 직접 읽으므로 뺌) */
                uint32_t hid = (uint32_t)((uint8_t *)dp)[4] | ((uint32_t)((uint8_t *)dp)[5] << 8) | ((uint32_t)((uint8_t *)dp)[6] << 16) | ((uint32_t)((uint8_t *)dp)[7] << 24);
                if ((hid & 0xFFFF) == 0x41D0 && ((hid >> 24) & 0xFF) == 0x0F) ps2 = 1;
            }
            if (len < 4) break;
            dp = (EFI_DEVICE_PATH *)((uint8_t *)dp + len);
        }
        if (usb) { if (!add_fw(hs[i], sp, 0, 0xFF)) c++; }
        else if (!ps2 && !has_8042() && !add_fw(hs[i], sp, 0, 0xFE)) c++;   /* 3.8.6: USB가 아닌 펌웨어 포인터 (I2C 터치패드 등). 3.9.2: 8042가 있으면 뺌 — 펌웨어 PS/2 터치패드 드라이버가 키보드 바이트를 가로챔 */
    }
    BS->FreePool(hs);
    return c;
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
    for (int i = 0; i < nskip; i++) {
        int found = 0;
        for (UINTN j = 0; j < n; j++) if (hs[j] == skip[i]) found = 1;
        if (!found) skip[i--] = skip[--nskip];
    }
    for (int i = 0; i < MAX_MOUSE; i++) {               /* 뽑힌 마우스 정리 */
        if (!mice[i].used) continue;
        int found = 0;
        if (mice[i].sp && mice[i].iface >= 0xFE) {      /* 펌웨어 마우스(다른 핸들): 프로토콜이 아직 있나 */
            void *sp;
            found = BS->HandleProtocol(mice[i].handle, &SIMPLE_POINTER_GUID, &sp) == EFI_SUCCESS && sp == (void *)mice[i].sp;
        } else for (UINTN j = 0; j < n; j++) if (hs[j] == mice[i].handle) found = 1;
        if (!found) { mice[i].used = 0; mice[i].btn = 0; mice[i].sp = 0; }
    }
    int fw_any = 0;
    if (!force_own) { scan_fw_pointers(); for (int i = 0; i < MAX_MOUSE; i++) if (mice[i].used && mice[i].sp) fw_any = 1; }
    for (UINTN j = 0; j < n; j++) {
        int known = 0;
        for (int i = 0; i < MAX_MOUSE; i++) if (mice[i].used && mice[i].handle == hs[j]) known = 1;
        for (int i = 0; i < nskip; i++) if (skip[i] == hs[j]) known = 1;
        if (known) continue;
        EFI_USB_IO_PROTOCOL *io;
        if (BS->HandleProtocol(hs[j], &USB_IO_GUID, (void **)&io) != EFI_SUCCESS) continue;
        EFI_USB_INTERFACE_DESCRIPTOR ifd;
        if (io->UsbGetInterfaceDescriptor(io, &ifd) != EFI_SUCCESS) continue;
        int cand = ifd.InterfaceClass == 3 && !(ifd.InterfaceSubClass == 1 && ifd.InterfaceProtocol == 1);   /* 키보드가 아닌 HID */
        int fw = cand && !force_own && !start_fw(hs[j], io, &ifd);
        int bootm = ifd.InterfaceSubClass == 1 && ifd.InterfaceProtocol == 2;
        if (cand && !fw && !force_own && fw_any && bootm) fw = 1;   /* 펌웨어가 다른 핸들로 읽고 있음: 떼지 않음 */
        int ok = fw || (cand && !start_mouse(hs[j], io, &ifd));
        if (cand && !ok && force_own && !start_dj(hs[j], io, &ifd)) ok = 4;   /* 3.0.8 Logitech DJ (직접 읽기일 때만) */
        if (ifd.InterfaceClass == 3) seen_add(io, &ifd, !cand ? 0 : fw ? 3 : ok == 4 ? 4 : ok ? 1 : 2);
        if (!ok && nskip < MAX_SKIP) skip[nskip++] = hs[j];
    }
    if (hs) BS->FreePool(hs);
}

int mouse_usb_init(EFI_BOOT_SERVICES *bs)
{
    BS = bs;
    scan();
    seen_gen = usb_gen;
    return mouse_usb_count();
}

int mouse_usb_count(void)
{
    int c = 0;
    for (int i = 0; i < MAX_MOUSE; i++) if (mice[i].used) c++;
    return c;
}

void mouse_usb_poll(void)
{
    for (int i = 0; i < MAX_MOUSE; i++) {               /* 3.0.8: DJ 수신기 — 목록을 받은 뒤 마우스 칸만 DJ 모드로 */
        Mouse *m = &mice[i];
        if (!m->used || !m->dj) continue;
        uint32_t t = timer_ms() - m->dj_t0;
        if (!(m->dj_state & 1) && t >= 600) {
            m->dj_state |= 1;
            if (m->dj_mouse_bits) dj_send(m, 0x80, m->dj_mouse_bits, 0);
        } else if (!(m->dj_state & 0x81) && t >= 300 && !m->dj_notes) {
            m->dj_state |= 0x80;                        /* 알림이 없으면 한 번 더 묻기 */
            dj_send(m, 0x81, 0, 0);
        }
    }
    for (int i = 0; i < MAX_MOUSE; i++) {               /* 3.0.6: 동기 읽기 (펌웨어 타이머 이벤트가 돌지 않는 PC 대비) */
        Mouse *m = &mice[i];
        if (!m->used || !m->sync) continue;
        static uint8_t buf[64];
        for (int k = 0; k < 4; k++) {                   /* 한 번에 쌓인 보고 몇 개까지 */
            UINTN len = m->len;
            uint32_t st = 0;
            if (m->io->UsbSyncInterruptTransfer(m->io, m->ep, buf, &len, 1, &st) != EFI_SUCCESS || !len) { if (st & ~0x50u) m->sync_err++;   /* NAK(0x10)·시간 초과(0x40)는 보통 */ break; }
            got_data(m, buf, len);
        }
    }
    for (int i = 0; i < MAX_MOUSE; i++) {               /* 3.0.4: 펌웨어 드라이버로 읽는 마우스 */
        Mouse *m = &mice[i];
        if (!m->used || !m->sp) continue;
        EFI_SIMPLE_POINTER_STATE st;
        if (m->sp->GetState(m->sp, &st) == EFI_SUCCESS) {
            m->rx++; m->reports++;
            m->dx += st.RelativeMovementX; m->dy += st.RelativeMovementY; m->dz += st.RelativeMovementZ;   /* Z: 위로 굴리면 + (HID와 같음) */
            m->btn = (st.LeftButton ? 1 : 0) | (st.RightButton ? 2 : 0);
        }
    }
    for (int i = 0; i < MAX_MOUSE; i++) {
        Mouse *m = &mice[i];
        if (!m->used || !m->error || m->sp || m->sync) continue;
        m->error = 0;
        m->io->UsbAsyncInterruptTransfer(m->io, m->ep, 0, 0, 0, 0, 0);
        m->io->UsbAsyncInterruptTransfer(m->io, m->ep, 1, m->interval, m->len, callback, m);
    }
    if (seen_gen != usb_gen) { seen_gen = usb_gen; scan(); }
}

int mouse_usb_read(int *dx, int *dy, int *dz, int *buttons, char *name, int max)
{
    int c = 0, b = 0, x = 0, y = 0, z = 0;
    for (int i = 0; i < MAX_MOUSE; i++) {
        Mouse *m = &mice[i];
        if (!m->used) continue;
        int a = m->dx, bb = m->dy, cc = m->dz;          /* 읽은 만큼만 빼서 콜백이 그사이 더한 것은 남김 */
        m->dx -= a; m->dy -= bb; m->dz -= cc;
        if (m->off) continue;                           /* 3.8.2: 사용 안 함 — 움직임은 버림 */
        c++;
        x += a; y += bb; z += cc; b |= m->btn;
    }
    *dx = x; *dy = y; *dz = z; *buttons = b;
    if (name && max > 0) {
        const char *t = c ? "USB mouse" : "";
        int n = 0;
        while (t[n] && n < max - 1) { name[n] = t[n]; n++; }
        if (c > 1 && n < max - 4) { name[n++] = ' '; name[n++] = 'x'; name[n++] = (char)('0' + c); }
        name[n] = 0;
    }
    return c;
}

/* 3.8.2: n번째 연결된 USB 마우스의 이름과 사용 여부 (없으면 0) */
int mouse_usb_dev(int n, char *name, int max, int *on)
{
    for (int i = 0; i < MAX_MOUSE; i++) {
        Mouse *m = &mice[i];
        if (!m->used || n--) continue;
        char t[40]; int k = 0;
        const char *s = m->dj ? "Logitech receiver" : m->sp && m->iface == 0xFE ? "Firmware touchpad" : m->sp ? "USB mouse (firmware)" : m->hm.boot ? "USB mouse (boot)" : "USB mouse";
        while (*s && k < 30) t[k++] = *s++;
        if (!m->dj && m->iface < 0xFE) { t[k++] = ' '; t[k++] = 'i'; t[k++] = 'f'; t[k++] = (char)('0' + m->iface % 10); }
        t[k] = 0;
        k = 0; while (t[k] && k < max - 1) { name[k] = t[k]; k++; }
        name[k] = 0;
        *on = !m->off;
        return 1;
    }
    return 0;
}
void mouse_usb_set(int n, int on)
{
    for (int i = 0; i < MAX_MOUSE; i++) {
        Mouse *m = &mice[i];
        if (!m->used || n--) continue;
        m->off = on ? 0 : 1;
        return;
    }
}

int mouse_usb_info(char (*lines)[64], int max)
{
    int n = 0;
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < MAX_MOUSE && n < max; i++) {
        Mouse *m = &mice[i];
        if (!m->used) continue;
        char *q = lines[n++], *e = q + 63;
#define PUT(str) do { const char *t_ = (str); while (*t_ && q < e) *q++ = *t_++; } while (0)
#define NUM(v) do { char nb_[12]; int k_ = 0; uint32_t r_ = (v); do { nb_[k_++] = (char)('0' + r_ % 10); r_ /= 10; } while (r_ && k_ < 11); while (k_ && q < e) *q++ = nb_[--k_]; } while (0)
        if (m->dj) {
            PUT("Logitech DJ if"); NUM(m->iface); PUT(" paired "); *q++ = hx[(m->dj_paired >> 4) & 15]; *q++ = hx[m->dj_paired & 15];
            PUT(" mouse "); *q++ = hx[(m->dj_mouse_bits >> 4) & 15]; *q++ = hx[m->dj_mouse_bits & 15];
            PUT(!(m->dj_state & 1) ? " asking" : m->dj_mouse_bits ? " DJ on" : " no mouse paired"); if (m->sync) PUT(" sync");
        } else if (m->sp) {
            if (m->iface == 0xFE) PUT("Firmware pointer (non-USB, touchpad?)"); else if (m->iface == 0xFF) PUT("USB mouse via firmware pointer"); else { PUT("USB mouse if"); NUM(m->iface); PUT(" via firmware driver"); }
            if (m->sp->Mode) { PUT(" res "); NUM((uint32_t)m->sp->Mode->ResolutionX); }
        } else {
            PUT(m->hm.boot ? "USB mouse (boot) if" : "USB mouse if"); NUM(m->iface);
            if (m->hm.report_id) { PUT(" id"); NUM(m->hm.report_id); }
            PUT(" x"); NUM(m->hm.x.bit); PUT("/"); NUM(m->hm.x.size);
            if (m->hm.wheel.size) PUT(" wheel");
            PUT(" ep"); *q++ = hx[(m->ep >> 4) & 15]; *q++ = hx[m->ep & 15];
            PUT(" mps"); NUM((uint32_t)m->len);
            if (m->sync) PUT(" sync");
        }
        *q = 0;
        if (n >= max) break;
        q = lines[n++]; e = q + 63;
        PUT("  packets "); NUM(m->rx); PUT(", mouse reports "); NUM(m->reports);
        if (m->boot_fb) { PUT(" (boot "); NUM(m->boot_fb); PUT(")"); }
        if (m->error) PUT(", error");
        if (m->sync_err) { PUT(", sync err "); NUM(m->sync_err); }
        *q = 0;
        if (m->last_len && n < max) {
            q = lines[n++]; e = q + 63;
            PUT("  mouse last:");
            for (int k = 0; k < m->last_len && q + 3 < e; k++) { *q++ = ' '; *q++ = hx[m->last[k] >> 4]; *q++ = hx[m->last[k] & 15]; }
            *q = 0;
        }
#undef PUT
#undef NUM
    }
    for (int i = 0; i < nseen && n < max; i++) {        /* USB HID 인터페이스 목록: VID:PID iface 하위/규약 → 결과 */
        char *q = lines[n++];
        const char *t = "HID ";
        while (*t) *q++ = *t++;
        for (int s2 = 12; s2 >= 0; s2 -= 4) *q++ = hx[(seen[i].vid >> s2) & 15];
        *q++ = ':';
        for (int s2 = 12; s2 >= 0; s2 -= 4) *q++ = hx[(seen[i].pid >> s2) & 15];
        t = " iface "; while (*t) *q++ = *t++;
        *q++ = (char)('0' + seen[i].iface % 10);
        *q++ = ' '; *q++ = hx[seen[i].sub & 15]; *q++ = '/'; *q++ = hx[seen[i].proto & 15];
        t = seen[i].result == 4 ? " -> Logitech DJ" : seen[i].result == 3 ? " -> mouse (firmware)" : seen[i].result == 1 ? " -> mouse" : seen[i].result == 0 ? " -> keyboard" : " -> not a mouse";
        while (*t) *q++ = *t++;
        if (seen[i].vid == 0x046D) { t = " (Logitech)"; while (*t) *q++ = *t++; }
        *q = 0;
    }
    if (n < max) {
        char *q = lines[n++];
        const char *t = "USB mice: ";
        while (*t) *q++ = *t++;
        *q++ = (char)('0' + mouse_usb_count());
        *q = 0;
    }
    return n;
}

/* 3.0.4: 마우스 드라이버 고르기. own < 0 = 지금 값만, 0 = 펌웨어 드라이버 먼저, 1 = 직접 */
int mouse_usb_driver(int mode)
{
    if (mode < 0 || mode > 2 || mode == drv_mode) return drv_mode;
    for (int i = 0; i < MAX_MOUSE; i++) {               /* 지금 마우스를 모두 놓고 다시 찾음 */
        Mouse *m = &mice[i];
        if (!m->used) continue;
        if (!m->sp) {
            if (!m->sync) m->io->UsbAsyncInterruptTransfer(m->io, m->ep, 0, 0, 0, 0, 0);
            BS->ConnectController(m->handle, 0, 0, 1);  /* 펌웨어 드라이버를 다시 붙임 */
        }
        m->used = 0; m->sp = 0;
    }
    drv_mode = mode;
    nskip = 0; nseen = 0; last_n = 0; last_sig = 0;
    scan();
    return drv_mode;
}
