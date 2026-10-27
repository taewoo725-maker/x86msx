/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vfctdet.c - ACPI VFCT에서 GPU VBIOS(ATOM)가 있는지 읽기만 해서 진단 줄로 보여 준다 (쓰기·하드웨어 접근 없음, UEFI 전용).
 * VFCT 구조(출처: Linux amdgpu_bios.c 구조 정의, MIT): 표 머리 36 + UUID 16 + vbios_image_offset u32 + lib1_image_offset u32 + reserved 16 = 76바이트,
 * 이미지 머리 28바이트(PCIBus,Dev,Fn u32 x3; Vendor,Device,SSVID,SSID u16 x4; Revision u32; ImageLength u32) 뒤에 VBIOS 이미지 */
#include "efi.h"
#include <stdint.h>
#include "vfctdet.h"

static EFI_SYSTEM_TABLE *ST;
static const EFI_GUID ACPI20_GUID = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
static const EFI_GUID ACPI10_GUID = { 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
static int gcmp(const EFI_GUID *a, const EFI_GUID *b) { const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b; for (int i = 0; i < 16; i++) if (x[i] != y[i]) return 1; return 0; }
static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t r64(const uint8_t *p) { return (uint64_t)r32(p) | (uint64_t)r32(p + 4) << 32; }
static void hx(char *o, uint32_t v, int digits)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = 0; i < digits; i++) o[i] = H[(v >> ((digits - 1 - i) * 4)) & 15];
}
static int put(char *l, const char *t) { int n = 0; while (*t) { l[n] = *t++; n++; } l[n] = 0; return n; }
static int puth(char *l, int n, uint32_t v, int d) { hx(l + n, v, d); return n + d; }

void vfct_setup(EFI_SYSTEM_TABLE *st) { ST = st; }

static char out[10][64];
static int nout, done;

static const uint8_t *find_vfct(uint32_t *len)
{
    typedef struct { EFI_GUID g; void *t; } CT;
    CT *ct = (CT *)ST->ConfigurationTable;
    const uint8_t *rsdp = 0;
    for (int pass = 0; pass < 2 && !rsdp; pass++)
        for (UINTN i = 0; i < ST->NumberOfTableEntries; i++)
            if (!gcmp(&ct[i].g, pass ? &ACPI10_GUID : &ACPI20_GUID)) { rsdp = (const uint8_t *)ct[i].t; break; }
    if (!rsdp) return 0;
    const uint8_t *root; int esz;
    if (rsdp[15] >= 2 && r64(rsdp + 24)) { root = (const uint8_t *)(uintptr_t)r64(rsdp + 24); esz = 8; }
    else { root = (const uint8_t *)(uintptr_t)r32(rsdp + 16); esz = 4; }
    if (!root) return 0;
    uint32_t rl = r32(root + 4);
    if (rl < 36 || rl > 0x100000) return 0;
    for (uint32_t o = 36; o + (uint32_t)esz <= rl; o += (uint32_t)esz) {
        const uint8_t *t = (const uint8_t *)(uintptr_t)(esz == 8 ? r64(root + o) : r32(root + o));
        if (t && t[0] == 'V' && t[1] == 'F' && t[2] == 'C' && t[3] == 'T') { *len = r32(t + 4); return t; }
    }
    return 0;
}

int vfct_info(char (*lines)[64], int max)
{
    if (!done) {
        done = 1;
        uint32_t tl = 0;
        const uint8_t *t = ST ? find_vfct(&tl) : 0;
        if (!t) { put(out[nout++], "amd vfct: none"); }
        else if (tl < 76 || tl > 0x800000) { int n = put(out[nout], "amd vfct: bad len "); puth(out[nout], n, tl, 8); out[nout][n + 8] = 0; nout++; }
        else {
            uint32_t off = r32(t + 48), cnt = 0;
            { char *l = out[nout++]; int n = put(l, "amd vfct: len="); n = puth(l, n, tl, 6); n += put(l + n, " off="); n = puth(l, n, off, 4); l[n] = 0; }
            while (off + 28 <= tl && cnt < 4) {
                const uint8_t *h = t + off;
                uint32_t il = r32(h + 24);
                uint16_t ven = r16(h + 12), dev = r16(h + 14);
                const uint8_t *img = h + 28;
                char *l = out[nout++]; int n = put(l, "amd vfct img ");
                n = puth(l, n, r32(h), 2); l[n++] = ':'; n = puth(l, n, r32(h + 4), 2); l[n++] = '.'; n = puth(l, n, r32(h + 8), 1);
                l[n++] = ' '; n = puth(l, n, ven, 4); l[n++] = ':'; n = puth(l, n, dev, 4);
                n += put(l + n, " len="); n = puth(l, n, il, 6); l[n] = 0;
                if (il >= 0x100 && off + 28 + il <= tl && img[0] == 0x55 && img[1] == 0xAA) {
                    uint16_t ro = r16(img + 0x48);          /* ATOM ROM header 위치 */
                    if (ro >= 0x40 && (uint32_t)ro + 0x28 <= il) {
                        const uint8_t *rh = img + ro;
                        char *m = out[nout++]; int k = put(m, "amd atom sig=");
                        for (int i = 0; i < 4; i++) m[k++] = (rh[4 + i] >= 0x20 && rh[4 + i] < 0x7F) ? (char)rh[4 + i] : '?';
                        k += put(m + k, " hdr="); k = puth(m, k, ro, 4);
                        k += put(m + k, " cmd="); k = puth(m, k, r16(rh + 0x1E), 4);
                        k += put(m + k, " data="); k = puth(m, k, r16(rh + 0x20), 4); m[k] = 0;
                        uint16_t co = r16(rh + 0x1E), dtab = r16(rh + 0x20);
                        if (co >= 0x40 && (uint32_t)co + 4 <= il) {   /* 명령 테이블 목록: 크기와 채워진 항목 수, 필요한 3개의 오프셋 */
                            const uint8_t *ct = img + co;
                            uint32_t sz = r16(ct), nz = 0, ne = sz > 4 ? (sz - 4) / 2 : 0;
                            if (ne > 128) ne = 128;
                            if ((uint32_t)co + 4 + ne * 2 <= il) {
                                for (uint32_t i = 0; i < ne; i++) if (r16(ct + 4 + i * 2)) nz++;
                                char *q = out[nout++]; int z = put(q, "amd atom cmd n="); z = puth(q, z, ne, 2);
                                z += put(q + z, " set="); z = puth(q, z, nz, 2);
                                z += put(q + z, " 0C:"); z = puth(q, z, ne > 0x0C ? r16(ct + 4 + 0x0C * 2) : 0, 4);
                                z += put(q + z, " 4C:"); z = puth(q, z, ne > 0x4C ? r16(ct + 4 + 0x4C * 2) : 0, 4); q[z] = 0;
                            }
                        }
                        if (dtab >= 0x40 && (uint32_t)dtab + 4 + 0x17 * 2 <= il) {   /* 데이터 테이블 중 Object_Header(색인 0x16) 오프셋 */
                            char *q = out[nout++]; int z = put(q, "amd atom data objhdr="); z = puth(q, z, r16(img + dtab + 4 + 0x16 * 2), 4); q[z] = 0;
                        }
                    }
                }
                off += 28 + il; cnt++;
                if (il == 0) break;
                if (nout >= 9) break;
            }
        }
    }
    int k = 0;
    for (int i = 0; i < nout && k < max; i++, k++) { int n = 0; while (out[i][n]) { lines[k][n] = out[i][n]; n++; } lines[k][n] = 0; }
    return k;
}
