/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* pcdb.h - 시험 PC 판별 표 (영상칩 PCI 제조사:장치 → PC 번호·이름). 5.0.90
 * 규칙: 실기 시험에서 확인된 ID만 넣는다 (추측 금지). 표에 없으면 pc=0 (로그에 pc=?). 번호는 X86MSX_시험PC목록의 #번호.
 * 이 표는 로그의 pc=/name= 표시에만 쓰임 — 영상·소리 동작은 바꾸지 않음. 새 기종을 넣을 때: 한 줄 추가 + 문서 X86MSX_기종별_반영현황 갱신. */
#ifndef MSX_PCDB_H
#define MSX_PCDB_H
#include <stdint.h>
typedef struct { uint16_t ven, dev; uint8_t pc; const char *name; } PcEntry;
static const PcEntry pcdb[] = {
    { 0x1002, 0x1636, 1, "Lenovo V14-ARE (AMD Renoir)" },
    { 0x8086, 0x5916, 2, "Lenovo IdeaPad 310-15IKB (Intel HD 620)" },
    { 0x8086, 0x0166, 5, "Acer Aspire V5-171 (Intel HD 4000)" },
    /* #3 ASUS B860M + RTX 4060 Ti: 시험 결과가 없어 ID 미확정 — 넣지 않음 */
};
static inline const PcEntry *pcdb_find(uint32_t id)   /* id = PCI 설정 공간 0번 dword (장치<<16 | 제조사) */
{
    for (unsigned i = 0; i < sizeof pcdb / sizeof pcdb[0]; i++) if (pcdb[i].ven == (id & 0xFFFF) && pcdb[i].dev == (id >> 16)) return &pcdb[i];
    return 0;
}
/* 버스 0~7의 모든 장치를 읽어 표에 있는 첫 그래픽(클래스 03)을 찾는다. rd = PCI 설정 읽기(버스, 장치, 기능, 오프셋). 읽기만 함 */
static inline const PcEntry *pcdb_scan(uint32_t (*rd)(int, int, int, int), uint32_t *gid)
{
    const PcEntry *first = 0; uint32_t fid = 0;
    for (int b = 0; b < 8; b++) for (int d = 0; d < 32; d++) {
        uint32_t id = rd(b, d, 0, 0); if ((id & 0xFFFF) == 0xFFFF) continue;
        if ((rd(b, d, 0, 8) >> 24) != 0x03) continue;
        if (!first) fid = id;
        const PcEntry *e = pcdb_find(id); if (e) { *gid = id; return e; }
    }
    *gid = fid; return first;
}
/* 버스 0~7에서 클래스(상위 8비트 = 03 그래픽, 02 네트워크)가 같은 장치를 "NET.pci bdf=BB:DD.F id=VVVV:DDDD cls=CCSS" 줄로 모아 out에 이어 붙임 (읽기만). 반환 = 새 길이 */
static inline int pcdb_dump_class(uint32_t (*rd)(int, int, int, int), uint32_t cls, const char *tag, char *out, int n, int max)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int b = 0; b < 8; b++) for (int d = 0; d < 32; d++) for (int f = 0; f < 8; f++) {
        uint32_t id = rd(b, d, f, 0); if ((id & 0xFFFF) == 0xFFFF) { if (f == 0) break; continue; }
        uint32_t c = rd(b, d, f, 8) >> 16; if ((c >> 8) != cls) { if (f == 0 && !((rd(b, d, 0, 12) >> 16) & 0x80)) break; continue; }
        if (n > max - 90) return n;
        const char *t = tag; while (*t) out[n++] = *t++;
        const char *k = ".pci bdf="; while (*k) out[n++] = *k++;
        out[n++] = hx[(b >> 4) & 15]; out[n++] = hx[b & 15]; out[n++] = ':'; out[n++] = hx[(d >> 4) & 15]; out[n++] = hx[d & 15]; out[n++] = '.'; out[n++] = hx[f];
        k = " id="; while (*k) out[n++] = *k++;
        for (int i = 12; i >= 0; i -= 4) out[n++] = hx[(id >> i) & 15]; out[n++] = ':'; for (int i = 28; i >= 16; i -= 4) out[n++] = hx[(id >> i) & 15];
        k = " cls="; while (*k) out[n++] = *k++;
        for (int i = 12; i >= 0; i -= 4) out[n++] = hx[(c >> i) & 15];
        out[n++] = '\r'; out[n++] = '\n';
    }
    out[n] = 0; return n;
}
#endif
