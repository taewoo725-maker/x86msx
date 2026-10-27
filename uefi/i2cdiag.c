/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * i2cdiag.c - 3.8.7 터치패드 진단 (읽기만 함, 하드웨어를 건드리지 않음)
 * 노트북 터치패드가 USB·PS/2·펌웨어 포인터 어디에도 안 잡힐 때, 어떤 길로 붙어 있는지 알아보는 정보:
 *   - CPU 종류
 *   - PCI의 I2C 컨트롤러 (클래스 0C80)
 *   - ACPI 표(DSDT/SSDT)에 적힌 터치패드 ID (PNP0C50, ELAN…, SYNA…)와 I2C 컨트롤러 ID (AMDI0010…)
 * 진단 정보(LCtrl+F6) 터치패드 탭에 "HID …" 줄로 나온다.
 *
 * 3.8.8: AMD DesignWare I2C(ACPI AMDI0010) 컨트롤러를 ACPI가 알려 준 주소·장치 주소로만 읽어 HID 정보를 알아낸다.
 *   - ACPI(AML)에서 AMDI0010 장치의 이름·UID·메모리 주소(Memory32Fixed)와 I2cSerialBus(장치 주소·속도·컨트롤러 이름)를 찾음
 *   - 그 컨트롤러로 HID 설명자(레지스터 0x0001)를 읽고, 보고서 설명자와 입력 보고를 받아 화면과 USB 루트의 I2C_TP.TXT에 적음
 *   - 마우스로는 아직 연결하지 않음 (3.8.9에서)
 *
 * 3.8.9: 읽은 보고서 설명자에서 상대 마우스(버튼·X·Y·휠) 위치를 찾아 "I2C touchpad"로 마우스 목록에 올림.
 *   터치패드는 새 보고가 있을 때만 INT 선(AMD GPIO, ACPI GpioInt 핀)을 낮춘다. 안 그러면 마지막 보고를 계속 돌려주므로
 *   핀이 낮을 때만 읽는다 (GPIO를 못 찾으면 같은 보고가 연달아 오면 버림).
 */
#include "efi.h"
#include <stdint.h>

static EFI_SYSTEM_TABLE *ST;
static EFI_BOOT_SERVICES *BS;
static int (*fwrite_cb)(const char *, const uint8_t *, uint32_t);
static int (*fcreate_cb)(const char *, int);
void i2c_diag_setup(EFI_SYSTEM_TABLE *st, int (*wr)(const char *, const uint8_t *, uint32_t), int (*cr)(const char *, int)) { ST = st; BS = st->BootServices; fwrite_cb = wr; fcreate_cb = cr; }
static int memcmp_(const void *a, const void *b, UINTN n) { const uint8_t *x = a, *y = b; for (UINTN i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i]; return 0; }
static int scmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (uint8_t)*a - (uint8_t)*b; }
static void cp_(void *d, const void *s, int n) { for (int i = 0; i < n; i++) ((uint8_t *)d)[i] = ((const uint8_t *)s)[i]; }
static void scpy(char *d, const char *s) { while ((*d++ = *s++)) ; }

static const EFI_GUID PCI_IO_GUID = { 0x4cf5b200, 0x68b8, 0x4ca5, { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } };
static const EFI_GUID ACPI20_GUID = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x59, 0x81 } };
static const EFI_GUID ACPI10_GUID = { 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };

typedef struct EFI_PCI_IO {
    void *PollMem, *PollIo, *MemRead, *MemWrite, *IoRead, *IoWrite;
    EFI_STATUS (*PciRead)(struct EFI_PCI_IO *, int width, uint32_t off, UINTN count, void *buf);
    void *PciWrite;
} EFI_PCI_IO;

#define MAXID 20
static char ids_touch[MAXID][9], ids_ctl[MAXID][9];
static int n_touch, n_ctl;
static uint16_t pci_i2c[8][2];
static int n_pci_i2c, n_pci_all, pci_done, acpi_tables, acpi_done;
static char cpu_line[64];

static void add_id(char (*arr)[9], int *n, const char *s)
{
    for (int i = 0; i < *n; i++) if (!scmp(arr[i], s)) return;
    if (*n >= MAXID) return;
    scpy(arr[(*n)++], s);
}
static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }
static int is_touch(const char *s)
{
    static const char *const t[] = { "PNP0C50", "ACPI0C50", "ELAN", "SYNA", "ALPS", "CYAP", "MSFT0001", "FTCS", "GXTP", "SYNP", "DLL", 0 };
    for (int i = 0; t[i]; i++) if (starts(s, t[i])) return 1;
    return 0;
}
static int is_ctl(const char *s)
{
    static const char *const t[] = { "AMDI001", "AMDI011", "AMDI051", "INT33C", "INT343", "INT34B", "INTC10", "80860F4", 0 };
    for (int i = 0; t[i]; i++) if (starts(s, t[i])) return 1;
    return 0;
}

static void scan_aml(const uint8_t *t)
{
    uint32_t len = (uint32_t)t[4] | (uint32_t)t[5] << 8 | (uint32_t)t[6] << 16 | (uint32_t)t[7] << 24;
    if (len < 44 || len > 0x1000000) return;
    acpi_tables++;
    for (uint32_t i = 36; i + 14 < len; i++) {
        if (t[i] != '_' || t[i + 1] != ((t[i + 2] == 'I') ? 'H' : 'C') || t[i + 3] != 'D') continue;
        if (!((t[i + 1] == 'H' && t[i + 2] == 'I') || (t[i + 1] == 'C' && t[i + 2] == 'I'))) continue;
        if (t[i + 4] != 0x0D) continue;
        char id[9]; int k = 0;
        while (k < 8 && t[i + 5 + k] >= 0x20 && t[i + 5 + k] < 0x7F) { id[k] = (char)t[i + 5 + k]; k++; }
        if (t[i + 5 + k] != 0 || k < 7) continue;
        id[k] = 0;
        if (is_touch(id)) add_id(ids_touch, &n_touch, id);
        else if (is_ctl(id)) add_id(ids_ctl, &n_ctl, id);
    }
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static void scan_acpi(void)
{
    typedef struct { EFI_GUID g; void *t; } CT;
    CT *ct = (CT *)ST->ConfigurationTable;
    const uint8_t *rsdp = 0;
    for (int pass = 0; pass < 2 && !rsdp; pass++)
        for (UINTN i = 0; i < ST->NumberOfTableEntries; i++)
            if (!memcmp_(&ct[i].g, pass ? &ACPI10_GUID : &ACPI20_GUID, sizeof(EFI_GUID))) { rsdp = (const uint8_t *)ct[i].t; break; }
    if (!rsdp || memcmp_(rsdp, "RSD PTR ", 8)) return;
    const uint8_t *root; int esz;
    if (rsdp[15] >= 2 && rd64(rsdp + 24)) { root = (const uint8_t *)(uintptr_t)rd64(rsdp + 24); esz = 8; }
    else { root = (const uint8_t *)(uintptr_t)rd32(rsdp + 16); esz = 4; }
    if (!root) return;
    uint32_t rl = rd32(root + 4);
    if (rl < 36 || rl > 0x100000) return;
    for (uint32_t o = 36; o + (uint32_t)esz <= rl; o += (uint32_t)esz) {
        const uint8_t *t = (const uint8_t *)(uintptr_t)(esz == 8 ? rd64(root + o) : rd32(root + o));
        if (!t) continue;
        if (!memcmp_(t, "SSDT", 4)) scan_aml(t);
        else if (!memcmp_(t, "FACP", 4)) {
            uint32_t fl = rd32(t + 4);
            uint64_t d = (fl >= 148 && rd64(t + 140)) ? rd64(t + 140) : (fl >= 44 ? rd32(t + 40) : 0);
            if (d) scan_aml((const uint8_t *)(uintptr_t)d);
        }
    }
}

static void scan_pci(void)
{
    EFI_HANDLE *hs = 0; UINTN n = 0;
    if (BS->LocateHandleBuffer(ByProtocol, &PCI_IO_GUID, 0, &n, &hs) != EFI_SUCCESS) return;
    for (UINTN i = 0; i < n; i++) {
        EFI_PCI_IO *io;
        uint32_t id = 0, cl = 0;
        if (BS->HandleProtocol(hs[i], &PCI_IO_GUID, (void **)&io) != EFI_SUCCESS || !io || !io->PciRead) continue;
        if (io->PciRead(io, 2, 0, 1, &id) != EFI_SUCCESS || (id & 0xFFFF) == 0xFFFF) continue;
        if (io->PciRead(io, 2, 8, 1, &cl) != EFI_SUCCESS) continue;
        n_pci_all++;
        if (((cl >> 16) & 0xFFFF) == 0x0C80 && n_pci_i2c < 8) { pci_i2c[n_pci_i2c][0] = (uint16_t)id; pci_i2c[n_pci_i2c][1] = (uint16_t)(id >> 16); n_pci_i2c++; }
    }
    BS->FreePool(hs);
}

static char *hex(char *q, uint32_t v, int digits) { for (int i = digits - 1; i >= 0; i--) *q++ = "0123456789ABCDEF"[(v >> (i * 4)) & 15]; return q; }
static char *dec(char *q, uint32_t v) { char nb[11]; int k = 0; do { nb[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) *q++ = nb[--k]; return q; }
static char *cat(char *q, const char *s) { while (*s) *q++ = *s++; return q; }

static void init_once(void)
{
    if (pci_done) return;
    pci_done = 1;
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    char v[13]; cp_(v, &b, 4); cp_(v + 4, &d, 4); cp_(v + 8, &c, 4); v[12] = 0;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    uint32_t fam = (a >> 8) & 15, mod = (a >> 4) & 15;
    if (fam == 15) fam += (a >> 20) & 255;
    if (fam == 6 || fam >= 15) mod |= ((a >> 16) & 15) << 4;
    char *q = cat(cpu_line, "HID CPU: "); q = cat(q, v); q = cat(q, " fam "); q = dec(q, fam); q = cat(q, " model "); q = hex(q, mod, 2); *q = 0;
    scan_pci();
    scan_acpi();
    acpi_done = 1;
}

static int list_ids(char (*lines)[64], int n, int max, const char *prefix, char (*ids)[9], int cnt)
{
    char *q = 0; int on = 0;
    if (!cnt) { if (n < max) { q = cat(lines[n], prefix); q = cat(q, "none"); *q = 0; n++; } return n; }
    for (int i = 0; i < cnt; i++) {
        if (!q || on == 4) { if (n >= max) return n; q = cat(lines[n++], on ? "HID      " : prefix); on = 0; }
        q = cat(q, ids[i]); *q++ = ' '; *q = 0; on++;
    }
    return n;
}

int i2c_diag_info(char (*lines)[64], int max)
{
    init_once();
    int n = 0;
    if (n < max) { scpy(lines[n], cpu_line); n++; }
    if (n < max) {
        char *q = cat(lines[n], "HID I2C PCI (class 0C80): ");
        if (!n_pci_i2c) q = cat(q, "none");
        for (int i = 0; i < n_pci_i2c && i < 3; i++) { q = hex(q, pci_i2c[i][0], 4); *q++ = ':'; q = hex(q, pci_i2c[i][1], 4); *q++ = ' '; }
        *q = 0; n++;
    }
    if (n < max) { char *q = cat(lines[n], "HID PCI devices: "); q = dec(q, (uint32_t)n_pci_all); q = cat(q, ", ACPI tables: "); q = dec(q, (uint32_t)acpi_tables); *q = 0; n++; }
    n = list_ids(lines, n, max, "HID I2C ctl ACPI: ", ids_ctl, n_ctl);
    n = list_ids(lines, n, max, "HID touchpad ACPI: ", ids_touch, n_touch);
    for (int i = 0; i < n; i++) lines[i][63] = 0;
    return n;
}

/* ================================================================== */
/* 3.8.8 ACPI에서 I2C 컨트롤러·터치패드 주소 찾기 + DesignWare I2C 읽기          */
/* ================================================================== */
typedef struct { char name[5]; int uid; uint32_t base; } DwCtl;
typedef struct { uint16_t addr; uint32_t speed; char src[5]; int gpin, gpol; } I2cDev;
static uint32_t gpio_base;
static DwCtl ctls[6]; static int nctls;
static I2cDev idevs[6]; static int nidevs;

static int nm_ok(const uint8_t *p) { for (int i = 0; i < 4; i++) if (!((p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= '0' && p[i] <= '9') || p[i] == '_')) return 0; return 1; }

static void scan_aml2(const uint8_t *t)
{
    uint32_t len = rd32(t + 4);
    if (len < 44 || len > 0x1000000) return;
    for (uint32_t i = 40; i + 24 < len; i++) {
        /* 장치: Name(_HID, "AMDI001x") */
        if (t[i] == '_' && t[i + 1] == 'H' && t[i + 2] == 'I' && t[i + 3] == 'D' && t[i + 4] == 0x0D && !memcmp_(t + i + 5, "AMDI001", 7) && nctls < 6) {
            DwCtl *c = &ctls[nctls];
            c->name[0] = '?'; c->name[1] = 0; c->uid = -1; c->base = 0;
            if (t[i - 1] == 0x08 && i > 6 && nm_ok(t + i - 5)) { cp_(c->name, t + i - 5, 4); c->name[4] = 0; }
            for (uint32_t j = i + 14; j + 8 < len && j < i + 220; j++) {
                if (t[j] == '_' && t[j + 1] == 'H' && t[j + 2] == 'I' && t[j + 3] == 'D') break;       /* 다음 장치 */
                if (t[j] == '_' && t[j + 1] == 'U' && t[j + 2] == 'I' && t[j + 3] == 'D' && c->uid < 0)
                    c->uid = t[j + 4] == 0x0A ? t[j + 5] : t[j + 4] == 0x01 ? 1 : t[j + 4] == 0x00 ? 0 : -1;
                if (t[j] == 0x86 && t[j + 1] == 0x09 && t[j + 2] == 0x00 && !c->base) c->base = rd32(t + j + 4);   /* Memory32Fixed */
            }
            nctls++;
        }
        /* AMD GPIO 컨트롤러: _HID "AMDI003x" / "AMD0030" → 메모리 주소 */
        if (t[i] == '_' && t[i + 1] == 'H' && t[i + 2] == 'I' && t[i + 3] == 'D' && t[i + 4] == 0x0D && !gpio_base && (!memcmp_(t + i + 5, "AMDI003", 7) || !memcmp_(t + i + 5, "AMD0030", 7))) {
            for (uint32_t j = i + 14; j + 8 < len && j < i + 220; j++) {
                if (t[j] == '_' && t[j + 1] == 'H' && t[j + 2] == 'I' && t[j + 3] == 'D') break;
                if (t[j] == 0x86 && t[j + 1] == 0x09 && t[j + 2] == 0x00) { gpio_base = rd32(t + j + 4); break; }
            }
        }
        /* I2cSerialBusV2: 8E len(2) rev(1) srcidx(1) type=1 flags(1) tflags(2) trev(1) tlen(2) speed(4) addr(2) ... 원본 이름 */
        if (t[i] == 0x8E && t[i + 3] == 0x01 && t[i + 5] == 0x01 && nidevs < 6) {
            uint32_t dl = (uint32_t)t[i + 1] | ((uint32_t)t[i + 2] << 8);
            uint32_t tl = (uint32_t)t[i + 10] | ((uint32_t)t[i + 11] << 8);
            if (dl < 14 || dl > 80 || tl < 6 || tl > dl) continue;
            uint16_t addr = (uint16_t)(t[i + 16] | (t[i + 17] << 8));
            uint32_t sp = rd32(t + i + 12);
            if (!addr || addr > 0x7F || sp < 10000 || sp > 3400000) continue;
            const uint8_t *src = t + i + 12 + tl;               /* 원본 문자열 "\_SB.I2CC" */
            int n = 0; while (n < 24 && src[n]) n++;
            if (src[n]) continue;
            I2cDev *d = &idevs[nidevs];
            d->addr = addr; d->speed = sp; d->src[0] = '?'; d->src[1] = 0; d->gpin = -1; d->gpol = 1;
            /* 같은 _CRS 근처의 GpioInt(0x8C): 8C len(2) rev type=0 gflags(2) iflags(2) cfg(1) drive(2) debounce(2) pintable-off(2) ... */
            for (int dir = 0; dir < 2 && d->gpin < 0; dir++)
                for (int k = 1; k < 200; k++) {
                    int64_t j = dir ? (int64_t)i - k : (int64_t)i + 20 + k;
                    if (j < 36 || j + 24 >= (int64_t)len) continue;
                    const uint8_t *g = t + j;
                    uint32_t gl = (uint32_t)g[1] | ((uint32_t)g[2] << 8);
                    if (g[0] != 0x8C || g[3] != 1 || g[4] != 0 || gl < 18 || gl > 60) continue;
                    uint32_t po = (uint32_t)g[14] | ((uint32_t)g[15] << 8);
                    if (po + 2 > gl + 3) continue;
                    int pin = g[po] | (g[po + 1] << 8);
                    if (pin >= 0 && pin < 256) { d->gpin = pin; d->gpol = (g[7] >> 1) & 3; break; }
                }
            if (n >= 4 && nm_ok(src + n - 4)) { cp_(d->src, src + n - 4, 4); d->src[4] = 0; }
            int dup = 0;
            for (int k = 0; k < nidevs; k++) if (idevs[k].addr == addr && !scmp(idevs[k].src, d->src)) dup = 1;
            if (!dup) nidevs++;
        }
    }
}
static void scan_acpi2(void)
{
    typedef struct { EFI_GUID g; void *t; } CT;
    CT *ct = (CT *)ST->ConfigurationTable;
    const uint8_t *rsdp = 0;
    for (int pass = 0; pass < 2 && !rsdp; pass++)
        for (UINTN i = 0; i < ST->NumberOfTableEntries; i++)
            if (!memcmp_(&ct[i].g, pass ? &ACPI10_GUID : &ACPI20_GUID, sizeof(EFI_GUID))) { rsdp = (const uint8_t *)ct[i].t; break; }
    if (!rsdp || memcmp_(rsdp, "RSD PTR ", 8)) return;
    const uint8_t *root; int esz;
    if (rsdp[15] >= 2 && rd64(rsdp + 24)) { root = (const uint8_t *)(uintptr_t)rd64(rsdp + 24); esz = 8; }
    else { root = (const uint8_t *)(uintptr_t)rd32(rsdp + 16); esz = 4; }
    if (!root) return;
    uint32_t rl = rd32(root + 4);
    if (rl < 36 || rl > 0x100000) return;
    for (uint32_t o = 36; o + (uint32_t)esz <= rl; o += (uint32_t)esz) {
        const uint8_t *t = (const uint8_t *)(uintptr_t)(esz == 8 ? rd64(root + o) : rd32(root + o));
        if (!t) continue;
        if (!memcmp_(t, "SSDT", 4)) scan_aml2(t);
        else if (!memcmp_(t, "FACP", 4)) {
            uint32_t fl = rd32(t + 4);
            uint64_t d = (fl >= 148 && rd64(t + 140)) ? rd64(t + 140) : (fl >= 44 ? rd32(t + 40) : 0);
            if (d) scan_aml2((const uint8_t *)(uintptr_t)d);
        }
    }
}

/* --- DesignWare I2C 마스터 --- */
static volatile uint32_t *dw;
static uint32_t rg(uint32_t off) { return dw[off / 4]; }
static void wg(uint32_t off, uint32_t v) { dw[off / 4] = v; }
static void us(UINTN n) { BS->Stall(n); }
static int dw_wait_en(uint32_t want)
{
    for (int i = 0; i < 2000; i++) { if ((rg(0x9C) & 1) == want) return 0; us(50); }
    return -1;
}
static int dw_setup(uint32_t addr)
{
    wg(0x6C, 0);
    if (dw_wait_en(0)) return -1;
    wg(0x00, 0x63);                                      /* 마스터, 표준 속도, 재시작 허용, 슬레이브 끔 */
    wg(0x30, 0);                                         /* 인터럽트 마스크 */
    wg(0x04, addr & 0x3FF);
    wg(0x14, 0x3A0); wg(0x18, 0x400);                    /* 표준 속도 SCL 높음·낮음 (입력 클럭 100~150MHz에서 50~80kHz) */
    wg(0x38, 0); wg(0x3C, 0);                            /* RX/TX 문턱 */
    rg(0x40); rg(0x54);                                  /* 인터럽트·중단 지우기 */
    wg(0x6C, 1);
    return dw_wait_en(1);
}
static uint32_t dw_abort;
/* 쓰기 nw바이트, 이어서 (재시작) 읽기 nr바이트. 0 = 성공 */
static int dw_xfer(const uint8_t *w, int nw, uint8_t *r, int nr)
{
    int sent = 0, got = 0, total = nw + nr, spin = 0;
    dw_abort = 0;
    while (got < nr || sent < total) {
        if (rg(0x34) & 0x40) { dw_abort = rg(0x80); rg(0x54); rg(0x40); return -1; }
        while (sent < total && rg(0x74) < 6) {
            uint32_t cmd;
            if (sent < nw) cmd = w[sent];
            else { cmd = 0x100; if (sent == nw && nw > 0) cmd |= 0x400; if (sent == total - 1) cmd |= 0x200; }
            if (sent == total - 1 && nr == 0) cmd |= 0x200;
            wg(0x10, cmd); sent++;
        }
        while (got < nr && rg(0x78) > 0) r[got++] = (uint8_t)(rg(0x10) & 0xFF);
        if (got >= nr && sent >= total) break;
        us(20);
        if (++spin > 20000) { dw_abort = 0xFFFF; return -2; }
    }
    for (int i = 0; i < 500 && (rg(0x70) & 1); i++) us(20);           /* 끝날 때까지 */
    if (rg(0x34) & 0x40) { dw_abort = rg(0x80); rg(0x54); rg(0x40); return -1; }
    return 0;
}

/* --- HID over I2C --- */
#define MAXRPT 64
static int tp_rst, tp_xerr, tp_short;                        /* 4.0.1 리셋 결과(1 응답 · 2 응답 없음 · 3 명령 실패), 읽기 실패·짧은 보고 수 */
static int tp_ctl = -1, tp_dev = -1, tp_state;              /* 0 아직 · 1 성공 · -1 실패 */
static char tp_err[64];
static uint32_t tp_comp;
static uint8_t hid_desc[30], rdesc[1024], rep_last[32], reps[MAXRPT][32];
static int rdesc_len, rep_n, rep_total, rep_saved, rep_len_last;
static uint16_t h_ver, h_rdlen, h_rdreg, h_inreg, h_maxin, h_cmdreg, h_vid, h_pid;
static char dump[12288];

static char *hexb(char *q, const uint8_t *b, int n) { for (int i = 0; i < n; i++) { q = hex(q, b[i], 2); *q++ = ' '; } return q; }

static void save_file(void)
{
    if (!fwrite_cb || !fcreate_cb) return;
    char *q = dump;
    q = cat(q, "X86MSX I2C touchpad dump\r\n");
    q = cat(q, "ctl "); q = cat(q, ctls[tp_ctl].name); q = cat(q, " base "); q = hex(q, ctls[tp_ctl].base, 8);
    q = cat(q, " dev addr "); q = hex(q, idevs[tp_dev].addr, 2); q = cat(q, " speed "); q = dec(q, idevs[tp_dev].speed); q = cat(q, "\r\n");
    q = cat(q, "HID desc: "); q = hexb(q, hid_desc, 30); q = cat(q, "\r\n");
    q = cat(q, "report desc ("); q = dec(q, (uint32_t)rdesc_len); q = cat(q, " bytes):\r\n");
    for (int i = 0; i < rdesc_len && q < dump + sizeof dump - 4096; i += 16) { q = hexb(q, rdesc + i, rdesc_len - i < 16 ? rdesc_len - i : 16); q = cat(q, "\r\n"); }
    q = cat(q, "input reports (");  q = dec(q, (uint32_t)rep_n); q = cat(q, "):\r\n");
    for (int i = 0; i < rep_n && q < dump + sizeof dump - 200; i++) { q = hexb(q, reps[i], 32); q = cat(q, "\r\n"); }
    *q = 0;
    fcreate_cb("0:\\I2C_TP.TXT", 0);
    fwrite_cb("0:\\I2C_TP.TXT", (const uint8_t *)dump, (uint32_t)(q - dump));
}

static int hid_probe(void)
{
    static const uint8_t rq[2] = { 0x01, 0x00 };         /* HID 설명자 레지스터 (ACPI _DSM이 다른 값을 줄 수도 있음) */
    for (int c = 0; c < nctls; c++) {
        if (!ctls[c].base) continue;
        if (idevs[0].src[0] != '?') {                    /* 이름이 맞는 컨트롤러만 */
            int m = 0; for (int d = 0; d < nidevs; d++) if (!scmp(idevs[d].src, ctls[c].name)) m = 1;
            if (!m && nidevs) continue;
        }
        dw = (volatile uint32_t *)(uintptr_t)ctls[c].base;
        tp_comp = rg(0xFC);
        if (tp_comp == 0xFFFFFFFFu || tp_comp == 0) { scpy(tp_err, "no response at controller address"); continue; }
        if (tp_comp != 0x44570140u) { scpy(tp_err, "not a DesignWare I2C (no write)"); continue; }   /* 3.9.2: 확인 전에는 쓰지 않음 */
        for (int d = 0; d < nidevs; d++) {
            if (scmp(idevs[d].src, ctls[c].name) && idevs[0].src[0] != '?') continue;
            if (dw_setup(idevs[d].addr)) { scpy(tp_err, "controller enable timeout"); continue; }
            int bad = dw_xfer(rq, 2, hid_desc, 30);
            if (bad) {                                   /* 일부 장치는 설명자 레지스터가 0x0020 */
                uint32_t a1 = dw_abort;
                static const uint8_t rq2[2] = { 0x20, 0x00 };
                if (dw_setup(idevs[d].addr) == 0) bad = dw_xfer(rq2, 2, hid_desc, 30);
                if (bad) { char *q = cat(tp_err, "no reply addr "); q = hex(q, idevs[d].addr, 2); q = cat(q, " abort "); q = hex(q, a1, 4); *q = 0; continue; }
            }
            uint16_t len = (uint16_t)(hid_desc[0] | (hid_desc[1] << 8)), ver = (uint16_t)(hid_desc[2] | (hid_desc[3] << 8));
            if (len != 30 || ver != 0x0100) { char *q = cat(tp_err, "not HID desc: "); q = hexb(q, hid_desc, 6); *q = 0; continue; }
            h_rdlen = (uint16_t)(hid_desc[4] | (hid_desc[5] << 8)); h_rdreg = (uint16_t)(hid_desc[6] | (hid_desc[7] << 8));
            h_inreg = (uint16_t)(hid_desc[8] | (hid_desc[9] << 8)); h_maxin = (uint16_t)(hid_desc[10] | (hid_desc[11] << 8));
            h_cmdreg = (uint16_t)(hid_desc[16] | (hid_desc[17] << 8));
            h_vid = (uint16_t)(hid_desc[20] | (hid_desc[21] << 8)); h_pid = (uint16_t)(hid_desc[22] | (hid_desc[23] << 8));
            h_ver = ver;
            tp_ctl = c; tp_dev = d;
            /* 보고서 설명자 */
            if (h_rdlen && h_rdlen <= sizeof rdesc) {
                uint8_t rr[2] = { (uint8_t)h_rdreg, (uint8_t)(h_rdreg >> 8) };
                if (!dw_xfer(rr, 2, rdesc, h_rdlen)) rdesc_len = h_rdlen;
            }
            /* 전원 켜기 (SET_POWER ON): 명령 레지스터, 명령 0x0800 */
            uint8_t pw[4] = { (uint8_t)h_cmdreg, (uint8_t)(h_cmdreg >> 8), 0x00, 0x08 };
            dw_xfer(pw, 4, 0, 0);
            /* 4.0.1: 리셋 (RESET 0x0100) 뒤 장치가 보내는 2바이트 0을 읽음. 찬 부팅(윈도를 거치지 않음)에서 터치패드가 리셋 전엔 조용한 경우가 있어 */
            us(5000);
            {
                uint8_t rs[4] = { (uint8_t)h_cmdreg, (uint8_t)(h_cmdreg >> 8), 0x00, 0x01 };
                if (!dw_xfer(rs, 4, 0, 0)) {
                    for (int t = 0; t < 60; t++) {            /* 최대 약 0.6초 */
                        uint8_t sb[2] = { 0xFF, 0xFF };
                        us(10000);
                        if (dw_xfer(0, 0, sb, 2)) { dw_setup(idevs[d].addr); continue; }
                        if (!sb[0] && !sb[1]) { tp_rst = 1; break; }
                    }
                    if (tp_rst != 1) tp_rst = 2;
                } else tp_rst = 3;
                dw_xfer(pw, 4, 0, 0);                         /* 리셋 뒤 다시 전원 켜기 */
                us(5000);
            }
            tp_state = 1; save_file();
            return 1;
        }
    }
    return 0;
}

/* --- HID 보고서 설명자에서 상대 마우스 위치 찾기 (3.8.9) --- */
typedef struct { int ok, rid, bpos[3], xpos, xsz, ypos, ysz, wpos, wsz, rel, off; } HidMouse;
static HidMouse hm;
static int hbits[256];
static void parse_rdesc(void)
{
    uint32_t up = 0; int rsz = 0, rcnt = 0, rid = 0; uint32_t us_[16]; int nus = 0, umin = -1, umax = -1;
    for (int k = 0; k < 256; k++) hbits[k] = 0;
    hm.ok = 0; hm.bpos[0] = hm.bpos[1] = hm.bpos[2] = -1; hm.xpos = hm.ypos = hm.wpos = -1;
    for (int i = 0; i < rdesc_len;) {
        uint8_t b = rdesc[i];
        if (b == 0xFE) { i += 3 + (i + 1 < rdesc_len ? rdesc[i + 1] : 0); continue; }
        int sz = b & 3; if (sz == 3) sz = 4;
        int type = (b >> 2) & 3, tag = b >> 4;
        uint32_t v = 0; for (int k = 0; k < sz && i + 1 + k < rdesc_len; k++) v |= (uint32_t)rdesc[i + 1 + k] << (8 * k);
        i += 1 + sz;
        if (type == 1) { if (tag == 0) up = v; else if (tag == 7) rsz = (int)v; else if (tag == 8) rid = (int)(v & 255); else if (tag == 9) rcnt = (int)v; }
        else if (type == 2) { if (tag == 0 && nus < 16) us_[nus++] = v; else if (tag == 1) umin = (int)v; else if (tag == 2) umax = (int)v; }
        else if (type == 0) {
            if (tag == 8) {                                    /* Input */
                for (int e = 0; e < rcnt; e++) {
                    uint32_t u = nus ? us_[e < nus ? e : nus - 1] : (umin >= 0 ? (uint32_t)(umin + e) : 0);
                    int pos = hbits[rid] + e * rsz;
                    if (!(v & 1) && (!hm.ok || hm.rid == rid)) {
                        if (up == 9 && u >= 1 && u <= 3 && rsz == 1) hm.bpos[u - 1] = pos;
                        else if (up == 1 && u == 0x30) { hm.xpos = pos; hm.xsz = rsz; hm.rel = (v & 4) != 0; hm.rid = rid; }
                        else if (up == 1 && u == 0x31) { hm.ypos = pos; hm.ysz = rsz; }
                        else if (up == 1 && u == 0x38) { hm.wpos = pos; hm.wsz = rsz; }
                    }
                }
                hbits[rid] += rcnt * rsz;
                if (hm.xpos >= 0 && hm.ypos >= 0 && hm.rel && !hm.ok) hm.ok = 1;
            }
            if (tag == 8 || tag == 9 || tag == 0xB || tag == 0xA || tag == 0xC) { nus = 0; umin = umax = -1; }
        }
    }
}
static int get_bits(const uint8_t *b, int pos, int sz, int sgn)
{
    uint32_t v = 0;
    for (int i = 0; i < sz && i < 32; i++) if ((b[(pos + i) >> 3] >> ((pos + i) & 7)) & 1) v |= 1u << i;
    if (sgn && sz < 32 && (v >> (sz - 1)) & 1) v |= ~0u << sz;
    return (int32_t)v;
}

/* --- AMD GPIO 핀 상태 (INT 선) --- */
static int gpio_level(int pin) { return (int)((*(volatile uint32_t *)(uintptr_t)(gpio_base + (uint32_t)pin * 4) >> 16) & 1); }
static uint32_t gpio_raw(int pin) { return *(volatile uint32_t *)(uintptr_t)(gpio_base + (uint32_t)pin * 4); }

static int tp_poll_n, tp_pend_n;
static int tp_acc_x, tp_acc_y, tp_acc_w, tp_btn;
static uint8_t tp_prev[32]; static int tp_prev_ok;
static int use_gpio(void) { return tp_state == 1 && gpio_base && idevs[tp_dev].gpin >= 0 && gpio_raw(idevs[tp_dev].gpin) != 0xFFFFFFFFu; }

/* 한 번 읽어 쌓음 (보고가 있으면 1) */
static int tp_read_one(void)
{
    if (!h_maxin) return 0;
    if (use_gpio()) {
        int lv = gpio_level(idevs[tp_dev].gpin), act = idevs[tp_dev].gpol == 0 ? 1 : 0;      /* 활성 낮음(1)이면 0일 때 보고 있음 */
        tp_poll_n++;
        if (lv != act) return 0;
        tp_pend_n++;
    }
    uint8_t b[32];
    if (h_maxin > 32) h_maxin = 32;
    if (dw_xfer(0, 0, b, h_maxin)) { tp_xerr++; dw_setup(idevs[tp_dev].addr); return 0; }
    int len = b[0] | (b[1] << 8);
    if (len < 3 || len > h_maxin) { tp_short++; return 0; }
    if (!use_gpio()) { int same = tp_prev_ok; for (int i = 0; i < len; i++) if (b[i] != tp_prev[i]) same = 0; cp_(tp_prev, b, 32); tp_prev_ok = 1; if (same) return 0; }
    for (int i = 0; i < 32; i++) rep_last[i] = i < h_maxin ? b[i] : 0;
    rep_len_last = len; rep_total++;
    if (rep_n < MAXRPT) { cp_(reps[rep_n++], rep_last, 32); }
    if (hm.ok && b[2] == hm.rid) {
        const uint8_t *d = b + 3;
        tp_acc_x += get_bits(d, hm.xpos, hm.xsz, 1); tp_acc_y += get_bits(d, hm.ypos, hm.ysz, 1);
        if (hm.wpos >= 0) tp_acc_w += get_bits(d, hm.wpos, hm.wsz, 1);
        int bt = 0;
        if (hm.bpos[0] >= 0) bt |= get_bits(d, hm.bpos[0], 1, 0);
        if (hm.bpos[1] >= 0) bt |= get_bits(d, hm.bpos[1], 1, 0) << 1;
        if (hm.bpos[2] >= 0) bt |= get_bits(d, hm.bpos[2], 1, 0) << 2;
        tp_btn = bt;
    }
    return 1;
}

static int tp_inited;
void i2c_tp_init(void)
{
    if (tp_inited) return;
    tp_inited = 1;
    {   /* 3.9.2: AMD 컨트롤러에서만 시험됨. 다른 CPU(인텔 등)는 건드리지 않음 */
        uint32_t a, b, c, d;
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
        if (!(b == 0x68747541u && d == 0x69746E65u && c == 0x444D4163u)) { scpy(tp_err, "skipped: not AMD CPU"); tp_state = -1; return; }
    }
    scan_acpi2();
    if (nctls && nidevs) { if (!hid_probe() && !tp_err[0]) scpy(tp_err, "probe failed"); if (!tp_state) tp_state = -1; }
    else scpy(tp_err, "ACPI: no controller/device found");
    if (tp_state == 1) parse_rdesc();
}
int i2c_tp_present(void) { return tp_state == 1 && hm.ok; }
int i2c_tp_off(void) { return hm.off; }
void i2c_tp_set_off(int off) { hm.off = off; }
/* 마우스 읽기: 쌓인 움직임을 돌려줌. 꺼져 있어도 읽어서 비움. 돌려주는 값: 1 = 이 장치가 있음 */
uint32_t timer_ms(void);
int i2c_tp_read(int *dx, int *dy, int *dz, int *btn)
{
    if (!i2c_tp_present()) return 0;
    /* 4.0.15: I2C 읽기는 한 번에 수 ms~수십 ms 걸릴 수 있음 (50~80 kHz 표준 속도 + 펌웨어 Stall). 보고가 없는 동안(2초 넘게)은 100 ms에 한 번만,
     * 읽기 오류 뒤에는 1초 쉼 → 터치패드가 보고를 안 주는 PC에서 프레임마다 읽다가 에뮬이 느려지던 것을 막음. 보고가 오는 동안은 예전처럼 매 프레임 */
    static uint32_t next_ms, act_ms, seen_total;
    uint32_t now = timer_ms();
    if (rep_total != (int)seen_total) { seen_total = (uint32_t)rep_total; act_ms = now; }
    if ((int32_t)(now - next_ms) >= 0) {
        int xe = tp_xerr;
        for (int k = 0; k < 4; k++) if (!tp_read_one()) break;
        uint32_t t1 = timer_ms();
        if (tp_xerr != xe) next_ms = t1 + 1000;
        else if (rep_total != (int)seen_total) { seen_total = (uint32_t)rep_total; act_ms = t1; }
        else if (t1 - act_ms > 2000 && !use_gpio()) next_ms = t1 + 100;
    }
    if (!hm.off) { *dx += tp_acc_x; *dy += tp_acc_y; *dz += tp_acc_w; *btn |= tp_btn; }
    tp_acc_x = tp_acc_y = tp_acc_w = 0;
    if (hm.off) tp_btn = 0;
    return !hm.off;
}

int i2c_diag_info2(char (*lines)[64], int max)
{
    i2c_tp_init();
    static int saved0;
    if (tp_state == 1 && !saved0) { saved0 = 1; save_file(); }
    if (rep_n == MAXRPT && !rep_saved) { rep_saved = 1; save_file(); }
    int n = 0;
    for (int c = 0; c < nctls && n < max; c++) {
        char *q = cat(lines[n], "HID I2C ctl "); q = cat(q, ctls[c].name); q = cat(q, " uid "); q = dec(q, (uint32_t)(ctls[c].uid < 0 ? 0 : ctls[c].uid));
        q = cat(q, " @"); q = hex(q, ctls[c].base, 8); *q = 0; n++;
    }
    for (int d = 0; d < nidevs && n < max; d++) {
        char *q = cat(lines[n], "HID I2C dev addr "); q = hex(q, idevs[d].addr, 2); q = cat(q, " on "); q = cat(q, idevs[d].src); q = cat(q, " "); q = dec(q, idevs[d].speed / 1000); q = cat(q, "kHz"); *q = 0; n++;
    }
    if (tp_state == 1) {
        if (n < max) { char *q = cat(lines[n], "HID I2C DW id "); q = hex(q, tp_comp, 8); q = cat(q, " HID ver "); q = hex(q, h_ver, 4); *q = 0; n++; }
        if (n < max) { char *q = cat(lines[n], "HID I2C vid:pid "); q = hex(q, h_vid, 4); *q++ = ':'; q = hex(q, h_pid, 4); q = cat(q, " rdesc "); q = dec(q, h_rdlen); q = cat(q, " in "); q = hex(q, h_inreg, 4); q = cat(q, " max "); q = dec(q, h_maxin); *q = 0; n++; }
        if (n < max) { char *q = cat(lines[n], "HID I2C reports: "); q = dec(q, (uint32_t)rep_total); q = cat(q, " len "); q = dec(q, (uint32_t)rep_len_last); q = cat(q, " rst "); q = dec(q, (uint32_t)tp_rst); q = cat(q, " err "); q = dec(q, (uint32_t)tp_xerr); q = cat(q, " short "); q = dec(q, (uint32_t)tp_short); *q = 0; n++; }
        if (n < max) { char *q = cat(lines[n], "HID I2C last: "); q = hexb(q, rep_last, 14); *q = 0; n++; }
        if (n < max) {
            char *q = cat(lines[n], "HID I2C gpio ");
            if (gpio_base && idevs[tp_dev].gpin >= 0) { q = cat(q, "pin "); q = dec(q, (uint32_t)idevs[tp_dev].gpin); q = cat(q, " @"); q = hex(q, gpio_base, 8); q = cat(q, " lvl "); q = dec(q, (uint32_t)gpio_level(idevs[tp_dev].gpin)); q = cat(q, idevs[tp_dev].gpol == 1 ? " lo-act" : idevs[tp_dev].gpol == 0 ? " hi-act" : " both"); q = cat(q, " pend "); q = dec(q, (uint32_t)tp_pend_n); q = cat(q, "/"); q = dec(q, (uint32_t)tp_poll_n); }
            else q = cat(q, "not found (dedupe mode)");
            *q = 0; n++;
        }
        if (n < max) { char *q = cat(lines[n], "HID I2C mouse map: "); if (hm.ok) { q = cat(q, "rid "); q = dec(q, (uint32_t)hm.rid); q = cat(q, " X@"); q = dec(q, (uint32_t)hm.xpos); q = cat(q, "/"); q = dec(q, (uint32_t)hm.xsz); q = cat(q, " Y@"); q = dec(q, (uint32_t)hm.ypos); q = cat(q, " btn@"); q = dec(q, (uint32_t)(hm.bpos[0] < 0 ? 0 : hm.bpos[0])); } else q = cat(q, "none (not a relative mouse)"); *q = 0; n++; }
        if (n < max) { char *q = cat(lines[n], "HID I2C file I2C_TP.TXT "); q = cat(q, rep_saved ? "saved with reports" : "saved (touch the pad)"); *q = 0; n++; }
    } else if (n < max) { char *q = cat(lines[n], "HID I2C probe: "); q = cat(q, tp_err); *q = 0; n++; }
    for (int i = 0; i < n; i++) lines[i][63] = 0;
    return n;
}
