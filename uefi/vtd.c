/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * vtd.c - 부팅 전 DMA 보호(인텔 VT-d)가 켜진 PC에서 HD Audio DMA 를 허락받기.
 *
 * 최신 노트북(예: LG gram 16Z90S, Core Ultra)은 펌웨어가 부팅 중에도 IOMMU(VT-d)를 켜 둔다.
 * 그러면 장치는 펌웨어가 "허락한(Map)" 메모리만 DMA 로 읽고 쓸 수 있어, 우리가 잡은 소리 버퍼(CORB·RIRB·BDL·링)를
 * HDA 가 못 읽음 → 코덱 명령은 직접 명령(PIO)으로 되지만 소리 스트림은 멈춤("stream stuck").
 * UEFI 판은 부팅 서비스를 끝내지 않으므로(ExitBootServices 안 함) 보호가 계속 켜져 있다.
 *
 * 하는 일 (UEFI 표준 방법):
 *  1. ACPI DMAR 표에서 VT-d 장치(DRHD)를 찾아 상태(번역 켜짐 GSTS.TES, 보호 메모리 PMEN)를 읽음 — 진단 줄
 *  2. 모든 HD Audio PCI 장치(클래스 04:03, 04:01)의 EFI_PCI_IO 로 버스 마스터를 켜고,
 *     소리 DMA 영역을 Map(BusMasterCommonBuffer) → 펌웨어가 그 장치에 이 영역 읽기·쓰기를 허락함.
 *     장치 주소가 물리 주소와 다르면 그 차이를 hda 에 알려 줌 (hda_set_dma_dev)
 * 하드웨어 레지스터는 읽기만 하고, 허락은 펌웨어 함수로만 받음 (보호를 끄지 않음).
 */
#include "efi.h"
#include <stdint.h>

static const EFI_GUID PCI_IO_GUID_V = { 0x4cf5b200, 0x68b8, 0x4ca5, { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } };
static const EFI_GUID ACPI20_GUID_V = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x59, 0x81 } };

typedef struct EFI_PCI_IO_V {
    void *PollMem, *PollIo, *MemRead, *MemWrite, *IoRead, *IoWrite;
    EFI_STATUS (*PciRead)(struct EFI_PCI_IO_V *, int width, uint32_t off, UINTN count, void *buf);
    void *PciWrite, *CopyMem;
    EFI_STATUS (*Map)(struct EFI_PCI_IO_V *, int op, void *host, UINTN *bytes, uint64_t *dev, void **mapping);
    void *Unmap, *AllocateBuffer, *FreeBuffer, *Flush;
    EFI_STATUS (*GetLocation)(struct EFI_PCI_IO_V *, UINTN *seg, UINTN *bus, UINTN *dev, UINTN *fn);
    EFI_STATUS (*Attributes)(struct EFI_PCI_IO_V *, int op, uint64_t attr, uint64_t *result);
} EFI_PCI_IO_V;
#define PCIIO_OP_COMMON_BUFFER 2                /* EfiPciIoOperationBusMasterCommonBuffer */
#define PCIIO_ATTR_ENABLE      2                /* EfiPciIoAttributeOperationEnable */
#define PCIIO_ATTR_MEM_BM      (0x0200ull | 0x0400ull)   /* 메모리 + 버스 마스터 */

static char vtd_lines[4][64];
static int vtd_n;

static uint32_t rd32v(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64v(const uint8_t *p) { return (uint64_t)rd32v(p) | (uint64_t)rd32v(p + 4) << 32; }
static int same(const void *a, const void *b, int n) { const uint8_t *x = a, *y = b; for (int i = 0; i < n; i++) if (x[i] != y[i]) return 0; return 1; }
static char *vc(char *q, const char *s) { while (*s) *q++ = *s++; return q; }
static char *vh(char *q, uint64_t v, int d) { for (int i = d - 1; i >= 0; i--) *q++ = "0123456789ABCDEF"[(v >> (i * 4)) & 15]; return q; }
static char *vd(char *q, uint32_t v) { char nb[11]; int k = 0; do { nb[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) *q++ = nb[--k]; return q; }

/* 1. DMAR: VT-d 장치 상태 (읽기만) */
static void vtd_scan(EFI_SYSTEM_TABLE *st)
{
    typedef struct { EFI_GUID g; void *t; } CT;
    CT *ct = (CT *)st->ConfigurationTable;
    const uint8_t *rsdp = 0;
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) if (same(&ct[i].g, &ACPI20_GUID_V, sizeof(EFI_GUID))) { rsdp = (const uint8_t *)ct[i].t; break; }
    char *q = vtd_lines[vtd_n];
    q = vc(q, "VT-d: ");
    if (!rsdp || !same(rsdp, "RSD PTR ", 8) || rsdp[15] < 2 || !rd64v(rsdp + 24)) { q = vc(q, "no ACPI 2"); *q = 0; vtd_n++; return; }
    const uint8_t *x = (const uint8_t *)(uintptr_t)rd64v(rsdp + 24), *dmar = 0;
    uint32_t xl = rd32v(x + 4);
    for (uint32_t o = 36; o + 8 <= xl && xl < 0x10000; o += 8) { const uint8_t *t = (const uint8_t *)(uintptr_t)rd64v(x + o); if (t && same(t, "DMAR", 4)) { dmar = t; break; } }
    if (!dmar) { q = vc(q, "no DMAR (off)"); *q = 0; vtd_n++; return; }
    uint32_t dl = rd32v(dmar + 4), units = 0, tes = 0, pmr = 0;
    uint8_t flags = dmar[37];
    for (uint32_t o = 48; o + 16 <= dl && dl < 0x10000; ) {
        uint16_t ty = (uint16_t)(dmar[o] | dmar[o + 1] << 8), ln = (uint16_t)(dmar[o + 2] | dmar[o + 3] << 8);
        if (ln < 4) break;
        if (ty == 0) {                                  /* DRHD: 레지스터 주소 */
            volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)rd64v(dmar + o + 8);
            units++;
            if (r) { if (r[0x1C / 4] & 0x80000000u) tes++; if (r[0x64 / 4] & 1u) pmr++; }
        }
        o += ln;
    }
    q = vc(q, "units "); q = vd(q, units); q = vc(q, " translate "); q = vd(q, tes); q = vc(q, " pmr "); q = vd(q, pmr);
    q = vc(q, " flags "); q = vh(q, flags, 2);
    if (flags & 4) q = vc(q, " pre-boot");
    *q = 0; vtd_n++;
}

/* 2. HD Audio 장치마다 DMA 영역 허락받기. 돌려줌: 장치 주소 - 물리 주소 (0 = 같음) */
int64_t vtd_hda_map(EFI_SYSTEM_TABLE *st, uintptr_t host, uint32_t size)
{
    vtd_n = 0;
    vtd_scan(st);
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_HANDLE *hs = 0; UINTN n = 0;
    int64_t off = 0;
    char *q = vtd_lines[vtd_n]; q = vc(q, "HDA DMA map:");
    if (bs->LocateHandleBuffer(ByProtocol, &PCI_IO_GUID_V, 0, &n, &hs) != EFI_SUCCESS) { q = vc(q, " no PciIo"); *q = 0; vtd_n++; return 0; }
    int found = 0;
    for (UINTN i = 0; i < n && found < 3; i++) {
        EFI_PCI_IO_V *io;
        uint32_t cl = 0;
        if (bs->HandleProtocol(hs[i], &PCI_IO_GUID_V, (void **)&io) != EFI_SUCCESS || !io || !io->PciRead) continue;
        if (io->PciRead(io, 2, 8, 1, &cl) != EFI_SUCCESS) continue;
        uint32_t c = (cl >> 16) & 0xFFFF;
        if (c != 0x0403 && c != 0x0401) continue;
        found++;
        UINTN sg = 0, b = 0, d = 0, f = 0;
        if (io->GetLocation) io->GetLocation(io, &sg, &b, &d, &f);
        q = vc(q, " "); q = vh(q, b, 2); q = vc(q, ":"); q = vh(q, d, 2); q = vc(q, "."); q = vd(q, (uint32_t)f);
        uint64_t res = 0;
        EFI_STATUS ea = io->Attributes ? io->Attributes(io, PCIIO_ATTR_ENABLE, PCIIO_ATTR_MEM_BM, &res) : 1;
        UINTN bytes = size; uint64_t dev = 0; void *mp = 0;
        EFI_STATUS em = io->Map ? io->Map(io, PCIIO_OP_COMMON_BUFFER, (void *)host, &bytes, &dev, &mp) : 1;
        q = vc(q, ea == EFI_SUCCESS ? " bm" : " bm?");
        if (em != EFI_SUCCESS) { q = vc(q, " map err "); q = vh(q, (uint64_t)em & 0xFF, 2); }
        else if (dev == (uint64_t)host && bytes >= size) q = vc(q, " ok");
        else { q = vc(q, " dev "); q = vh(q, dev, 8); if (bytes < size) q = vc(q, " short"); else off = (int64_t)dev - (int64_t)host; }
    }
    if (!found) q = vc(q, " none");
    *q = 0; vtd_n++;
    bs->FreePool(hs);
    return off;
}

int vtd_info(char (*lines)[64], int max)
{
    int k = 0;
    for (int i = 0; i < vtd_n && k < max; i++, k++) { int j = 0; for (; vtd_lines[i][j] && j < 63; j++) lines[k][j] = vtd_lines[i][j]; lines[k][j] = 0; }
    return k;
}
