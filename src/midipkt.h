/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* midipkt.h - 3.5.4: MIDI 바이트열 → USB-MIDI 이벤트 패킷 (4바이트: 케이블·CIN, MIDI 1~3바이트)
 *   실행 상태(running status), 실시간 메시지(F8~FF, 중간에 끼어도 됨), 시스템 공통, SysEx(3바이트씩 CIN 4, 끝은 5·6·7)
 *   케이블 번호는 0. 헤더만으로 쓰는 작은 상태 기계 (UEFI usbmidi.c와 시험이 같이 씀) */
#ifndef MSX_MIDIPKT_H
#define MSX_MIDIPKT_H
#include <stdint.h>

typedef struct { uint8_t st, need, n, sx, d[3]; } MidiPk;

static inline void midipk_reset(MidiPk *p) { p->st = p->need = p->n = p->sx = 0; }

/* 바이트 하나. 패킷이 만들어지면 out[4]에 쓰고 1 */
static inline int midipk_feed(MidiPk *p, uint8_t b, uint8_t out[4])
{
    if (b >= 0xF8) {                                    /* 실시간: 바로 (상태를 건드리지 않음) */
        out[0] = 0x0F; out[1] = b; out[2] = out[3] = 0;
        return 1;
    }
    if (p->sx) {                                        /* SysEx 안 */
        if (b == 0xF7) {
            p->d[p->n++] = b;
            out[0] = (uint8_t)(0x04 + p->n);            /* 1바이트 5, 2바이트 6, 3바이트 7 */
            out[1] = p->d[0]; out[2] = p->n > 1 ? p->d[1] : 0; out[3] = p->n > 2 ? p->d[2] : 0;
            p->sx = 0; p->n = 0; p->st = 0;
            return 1;
        }
        if (b & 0x80) { p->sx = 0; p->n = 0; }          /* 끝 없이 다른 상태: SysEx 버림, 아래에서 새로 */
        else {
            p->d[p->n++] = b;
            if (p->n == 3) { out[0] = 0x04; out[1] = p->d[0]; out[2] = p->d[1]; out[3] = p->d[2]; p->n = 0; return 1; }
            return 0;
        }
    }
    if (b == 0xF0) { p->sx = 1; p->n = 0; p->d[p->n++] = b; p->st = 0; return 0; }
    if (b & 0x80) {
        p->n = 0;
        if (b < 0xF0) { p->st = b; p->need = (b & 0xE0) == 0xC0 ? 1 : 2; return 0; }   /* 채널: Cx·Dx는 데이터 1, 나머지 2 */
        p->st = 0;                                      /* 시스템 공통은 실행 상태를 지움 */
        if (b == 0xF6) { out[0] = 0x05; out[1] = b; out[2] = out[3] = 0; return 1; }   /* 튠 요청: 1바이트 */
        if (b == 0xF1 || b == 0xF3) { p->st = b; p->need = 1; return 0; }
        if (b == 0xF2) { p->st = b; p->need = 2; return 0; }
        return 0;                                       /* F4·F5·F7(혼자): 버림 */
    }
    if (!p->st) return 0;                               /* 상태 없는 데이터: 버림 */
    p->d[p->n++] = b;
    if (p->n < p->need) return 0;
    uint8_t cin = p->st >= 0xF0 ? (p->need == 1 ? 0x02 : 0x03) : (uint8_t)(p->st >> 4);
    out[0] = cin; out[1] = p->st; out[2] = p->d[0]; out[3] = p->need > 1 ? p->d[1] : 0;
    p->n = 0;
    if (p->st >= 0xF0) p->st = 0;                       /* 시스템 공통은 실행 상태 없음 */
    return 1;
}

/* 3.7.0: 음량 = Note On 세기(velocity) 비율. 실행 상태를 따라가며 Note On의 둘째 자료 바이트만 바꿈.
 * vol 0이면 세기 0 = Note Off (소리 끔), 그 밖에는 0이 되지 않게 1 이상. 믹서 내장 MIDI·USB MIDI 채널 */
typedef struct { uint8_t st, idx; } MidiVel;
static inline uint8_t midivel_feed(MidiVel *s, uint8_t b, int vol)
{
    if (b >= 0xF8) return b;                            /* 실시간 */
    if (b & 0x80) { s->st = b < 0xF0 ? b : 0; s->idx = 0; return b; }
    if (!s->st) return b;                               /* SysEx·시스템 공통 자료 */
    uint8_t hi = s->st & 0xF0;
    int need = hi == 0xC0 || hi == 0xD0 ? 1 : 2;
    s->idx++;
    if (hi == 0x90 && s->idx == 2 && b && vol < 100) {
        int v = b * (vol < 0 ? 0 : vol) / 100;
        b = (uint8_t)(vol <= 0 ? 0 : v < 1 ? 1 : v);
    }
    if (s->idx >= need) s->idx = 0;                     /* 실행 상태: 다음 메시지 */
    return b;
}
#endif
