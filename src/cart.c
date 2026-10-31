/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * cart.c - 매퍼 추정과 일반 ROM 배치
 *
 * 추정: ROM 안의 "LD (nnnn),A" (0x32 nn nn) 명령의 목적지 주소를 매퍼별 뱅크 레지스터
 * 주소와 대조해 점수를 매긴다 (에뮬레이터들이 널리 쓰는 방식).
 */
#include "cart.h"
#include <string.h>

static int has_ab(const uint8_t *d, uint32_t size, uint32_t off)
{
    return off + 1 < size && d[off] == 'A' && d[off + 1] == 'B';
}

const char *cart_mapper_name(MapperType m)
{
    switch (m) {
    case MAPPER_PLAIN: return "Plain";
    case MAPPER_KONAMI: return "Konami";
    case MAPPER_KONAMI_SCC: return "KonamiSCC";
    case MAPPER_ASCII8: return "ASCII8";
    case MAPPER_ASCII16: return "ASCII16";
    case MAPPER_FMPAC: return "FM-PAC";
    case MAPPER_DOS2: return "MSX-DOS2";
    case MAPPER_SCCPLUS: return "SCC-I";
    case MAPPER_SUNRISE: return "SunriseIDE";
    case MAPPER_VHD: return "X86MSX VHD";
    case MAPPER_ZEMINA80: return "Zemina80in1";
    case MAPPER_ZEMINA90: return "Zemina90in1";
    case MAPPER_ZEMINA126: return "Zemina126in1";
    case MAPPER_CROSSBLAIM: return "CrossBlaim";
    case MAPPER_LODERUNNER: return "SuperLodeRunner";
    case MAPPER_QURAN: return "AlQuran";
    case MAPPER_ASCII8_SRAM: return "ASCII8+SRAM";
    case MAPPER_ASCII16_SRAM: return "ASCII16+SRAM";
    case MAPPER_MEGARAM: return "MegaRAM";
    case MAPPER_RAM: return "RAM";
    case MAPPER_PAC: return "PAC";
    case MAPPER_RTYPE: return "R-Type";
    case MAPPER_HARRYFOX: return "HarryFox";
    case MAPPER_NETTOU: return "NettouYakyuu";
    case MAPPER_ESERAM: return "Ese-RAM";
    case MAPPER_MFRSCC: return "MegaFlashROM SCC";
    case MAPPER_BEERIDE: return "Beer IDE";
    case MAPPER_NET: return "Network";
    case MAPPER_HBI55: return "HBI-55";
    case MAPPER_RS232: return "RS-232C";
    case MAPPER_FRSCC: return "Flash-ROM SCC";
    case MAPPER_MFRSCCP: return "MegaFlashROM SCC+";
    case MAPPER_GIDE: return "GIDE";
    case MAPPER_ESESCC: return "Ese-SCC";
    case MAPPER_GOUDA: return "Gouda SCSI";
    case MAPPER_SFG: return "Yamaha SFG";
    case MAPPER_FSCA1: return "FS-CA1";
    case MAPPER_MEGASCSI: return "MEGA-SCSI";
    case MAPPER_WAVESCSI: return "WAVE-SCSI";
    case MAPPER_MATRAINK: return "MatraInk";
    case MAPPER_ASCII16X: return "ASCII16-X";
    case MAPPER_YAMANOOTO: return "Yamanooto";
    case MAPPER_GAMEMASTER2: return "GameMaster2";
    case MAPPER_NEO8: return "NEO-8";
    case MAPPER_NEO16: return "NEO-16";
    default: return "?";
    }
}

int cart_plain_base(const uint8_t *d, uint32_t size)
{
    /* 3.0.8: openMSX처럼 "AB" 머리가 가리키는 주소로 자리를 정한다.
     * 머리가 있는 16 KiB 쪽(p)의 INIT·STATEMENT·DEVICE·TEXT 중 처음 0이 아닌 주소의 쪽(T)에 그 쪽이 오도록 롬 시작 = T - p.
     * 예: 16 KiB인데 INIT = 0x8010 → 0x8000에 (Actman, Angelo 등). 머리 주소가 모두 0이면 머리 쪽을 0x4000에. */
    uint32_t pages = (size + 0x3FFF) / 0x4000;
    if (pages > 4) pages = 4;
    /* 3.0.9: 예약 칸(10~15)이 0인 깨끗한 머리를 먼저, 없으면 아무 머리나 (Leonard: 0쪽 "AB"는 데이터) */
    for (int pass = 0; pass < 2; pass++)
    for (uint32_t p = 0; p < pages; p++) {
        if (!has_ab(d, size, p * 0x4000) || p * 0x4000 + 16 > size) continue;
        if (!pass) { const uint8_t *r = d + p * 0x4000 + 10; if (r[0] | r[1] | r[2] | r[3] | r[4] | r[5]) continue; }
        const uint8_t *h = d + p * 0x4000;
        static const int order[4] = { 2, 8, 4, 6 };        /* INIT, TEXT(BASIC 롬), STATEMENT, DEVICE */
        int t = -1;
        for (int i = 0; i < 4 && t < 0; i++) {
            uint16_t a = (uint16_t)(h[order[i]] | (h[order[i] + 1] << 8));
            if (a) t = a >> 14;
        }
        if (t < 0) t = 1;
        int base = t - (int)p;
        if (base < 0 || base + (int)pages > 4) continue;        /* 맞지 않으면 다음 머리 */
        if (size <= 0x2000) return base * 2 == 0 ? 2 : base * 2;   /* 8 KiB: 0x4000(반복) 또는 0x8000 */
        return base * 2;
    }
    if (size <= 0x8000 && !has_ab(d, size, 0) && has_ab(d, size, 0x4000)) return 0;
    if (size > 0x8000 && has_ab(d, size, 0x4000) && !has_ab(d, size, 0)) return 0;
    return size <= 0x8000 ? 2 : (has_ab(d, size, 0) ? 2 : 0);
}

int cart_flash_auto = 3;   /* 자동 인식 사용 스위치 — 1 = Yamanooto, 2 = ASCII16-X (기본 둘 다 켜짐) */

MapperType cart_guess_mapper(const uint8_t *d, uint32_t size)
{
    return cart_guess_mapper_partial(d, size, size);
}

MapperType cart_guess_mapper_partial(const uint8_t *d, uint32_t len, uint32_t size)
{
    if (len >= 0x18 && !memcmp(d + 0x10, "ROM_NEO8", 8)) return MAPPER_NEO8;     /* 5.0.44 */
    if (len >= 0x18 && !memcmp(d + 0x10, "ROM_NE16", 8)) return MAPPER_NEO16;
    if ((cart_flash_auto & 2) && len >= 0x18 && !memcmp(d + 0x10, "ASCII16X", 8)) return MAPPER_ASCII16X;   /* 4.0.129: 4010h 서명 */
    if ((cart_flash_auto & 1) && len >= 0x17 && !memcmp(d + 0x10, "ROM_YAM", 7)) return MAPPER_YAMANOOTO;   /* 4.0.129: Yamanooto 롬 서명 */
    if (size > (2u << 20) && size <= (16u << 20) && (cart_flash_auto & 2)) {   /* 서명 없는 2 MiB 초과 롬이 6000h·7000h 에만 뱅크를 쓰고(ASCII16 방식) 6800h·7800h 는 안 쓰면 ASCII16-X (9Fingers 3.5 MiB 등). 그 밖은 아래 Yamanooto */
        int w6 = 0, w7 = 0, w68 = 0, wk = 0;
        for (uint32_t i = 0; i + 2 < len; i++) {
            if (d[i] != 0x32) continue;
            uint16_t a = (uint16_t)(d[i + 1] | (d[i + 2] << 8));
            if (a == 0x6000) w6++; else if (a == 0x7000) w7++; else if (a == 0x6800 || a == 0x7800) w68++; else if (a == 0x5000 || a == 0x9000 || a == 0xB000) wk++;
        }
        if (w6 + w7 >= 8 && w6 + w7 >= 8 * w68 && 4 * wk < w6 + w7) return MAPPER_ASCII16X;   /* 5000h·9000h·B000h 에도 쓰면 코나미 SCC 8 KiB 뱅크 → 아래 Yamanooto */   /* 우연히 섞인 6800h 한두 개는 무시 */
    }
    if ((cart_flash_auto & 1) && size > (2u << 20) && size <= (16u << 20)) return MAPPER_YAMANOOTO;   /* 서명 없는 2 MiB 초과 롬(Yamanooto 용 합본·부라이 등)은 Yamanooto 로 */
    if ((cart_flash_auto & 1) && size == (2u << 20)) {   /* 서명 없는 2 MiB DSK2ROM(Jesus_KR 등) 은 Yamanooto 로 */
        uint32_t n = len < 0x10000 ? len : 0x10000;
        for (uint32_t i = 0; i + 7 <= n; i++) if (!memcmp(d + i, "DSK2ROM", 7)) return MAPPER_YAMANOOTO;
    }
    if (size <= 0x8000) return MAPPER_PLAIN;
    if (size <= 0xC000 && (has_ab(d, len, 0x4000) || !has_ab(d, len, 0))) return MAPPER_PLAIN;
    if (size == 0x10000 && has_ab(d, len, 0x4000) && !has_ab(d, len, 0)) return MAPPER_PLAIN;

    int k = 0, ks = 0, a8 = 0, a16 = 0, k8 = 0, kA = 0, s9 = 0, sB = 0, s5 = 0, s7 = 0;
    for (uint32_t i = 0; i + 2 < len; i++) {
        if (d[i] != 0x32) continue;
        uint16_t a = (uint16_t)(d[i + 1] | (d[i + 2] << 8));
        switch (a) {
        case 0x5000: ks++; s5++; break;
        case 0x9000: ks++; s9++; break;
        case 0xB000: ks++; sB++; break;
        case 0x4000: k++; break;
        case 0x8000: k++; k8++; break;
        case 0xA000: k++; kA++; break;
        case 0x6800: case 0x7800: a8++; break;
        case 0x6000: k++; a8++; a16++; break;
        case 0x7000: ks++; s7++; a8++; a16++; break;
        case 0x77FF: a16++; break;
        default: break;
        }
    }
    if ((cart_flash_auto & 1) && size == (2u << 20) && s7 && s9 && sB && !s5) return MAPPER_YAMANOOTO;   /* 2 MiB 에 5000 쓰기 없이 7000·9000·B000 만 쓰면(Pampas 4 등) Yamanooto */
    if (!k8 || !kA) k = 0;                              /* 4.0.29: Konami 뱅크는 8000과 A000 둘 다 씀 (Aleste 2는 6000·8000만 → ASCII8) */
    if (!s9 && !sB) ks = 0;
    if (!k && !ks && !a8 && !a16)
        return size <= 0x10000 ? MAPPER_PLAIN : MAPPER_ASCII16;
    /* ASCII8 만의 증거는 6800h·7800h 쓰기. 6000h·7000h 만 쓰는 롬(a8 == a16, Chase H.Q. V9990 1 MiB 등)은 ASCII16 (ASCII8 은 뱅크 레지스터 4개를 다 써야 함).
     * 그 밖 동점은 ASCII8 > Konami > Konami SCC 순 */
    if (size >= (1u << 20) && a16 >= 8 && (a8 - a16) * 3 < a16 && (s5 + s9 + sB) < 2 * a16)   /* 5.3.39: 5000h·9000h·B000h(코나미 SCC 8 KiB 뱅크) 쓰기가 6000h·7000h 의 2배 이상이면 ASCII16 이 아니라 아래 점수로 (메탈기어2 중국어판 1 MiB 가 ASCII16 으로 잘못 잡혀 계속 다시 시작하던 것) */   /* 1 MiB 이상 롬에서 6000h·7000h 쓰기가 6800h·7800h 의 3배보다 많으면(우연히 섞인 몇 개는 무시) ASCII16. 제믹스 126 합본(Tank Battalion 등)이 ASCII8 로 잘못 잡히던 것 */
        return MAPPER_ASCII16;
    MapperType best = MAPPER_ASCII8;
    int bestv = a8;
    if (a16 >= bestv && a16 > 0) { best = MAPPER_ASCII16; bestv = a16; }
    if (k > bestv) { best = MAPPER_KONAMI; bestv = k; }
    if (ks > bestv) { best = MAPPER_KONAMI_SCC; bestv = ks; }
    return best;
}
