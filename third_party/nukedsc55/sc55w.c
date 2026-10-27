/* X86MSX 5.2.60 - Nuked-SC55 감싸개: 롬 넣기, MIDI 바이트, 48 kHz 로 표본 만들기, LCD 읽기.
 * 원본의 SDL 작업 스레드 대신 render 가 필요한 만큼 MCU_Step 을 돌림 (한 스레드). SC-55mkII 66207 Hz / SC-55 64000 Hz → 48000 Hz 선형 보간.
 * GPL-2.0-or-later (Nuked-SC55 부분) — X86MSX(GPL-3.0-or-later) 와 함께 GPL-3 으로 배포 */
#include <string.h>
#include "sc55w.h"
#include "mcu.h"
#include "pcm.h"
#include "submcu.h"
#include "lcd.h"
#include "mcu_timer.h"

extern uint8_t rom1[], rom2[], ram[], sram[];
extern int rom2_mask;
extern uint8_t lcd_font[240][10];
void MCU_Init(void);
void MCU_Reset(void);
void MCU_PatchROM(void);
void MCU_Step(void);
void unscramble(uint8_t *src, uint8_t *dst, int len);
void TIMER_Reset(void);

const char *const sc55w_files[2][SC55_NFILE] = {
    { "rom1.bin", "rom2.bin", "waverom1.bin", "waverom2.bin", "rom_sm.bin" },
    { "sc55_rom1.bin", "sc55_rom2.bin", "sc55_waverom1.bin", "sc55_waverom2.bin", "sc55_waverom3.bin" },
};
static uint8_t tmp[SC55_TMP_SIZE];
static int ready, model, have;                 /* have = 넣은 파일 비트 */
static uint32_t steps;
#define RING 4096
static int16_t ring[RING][2];
static uint32_t rw, rr;                         /* 쓴·읽은 수 */
static int16_t prev[2], cur[2];
static uint32_t frac, step;                     /* Q16 */

uint8_t *sc55w_tmp(void) { return tmp; }
void sc55w_post(int l, int r) { if (rw - rr < RING) { ring[rw % RING][0] = (int16_t)l; ring[rw % RING][1] = (int16_t)r; rw++; } }

int sc55w_load(int m, int idx, uint32_t n)
{
    if (m < 0 || m > 1 || idx < 0 || idx >= SC55_NFILE) return -1;
    switch (idx) {
    case 0: if (n != 0x8000) return -1; memcpy(rom1, tmp, n); break;
    case 1: if (n != 0x80000 && n != 0x40000) return -1; memcpy(rom2, tmp, n); rom2_mask = (int)n - 1; break;
    case 2: if (n != (m == SC55_MK1 ? 0x100000u : 0x200000u)) return -1; unscramble(tmp, waverom1, (int)n); break;
    case 3: if (n != 0x100000) return -1; unscramble(tmp, waverom2, (int)n); break;
    case 4:
        if (m == SC55_MK1) { if (n != 0x100000) return -1; unscramble(tmp, waverom3, (int)n); }
        else { if (n != 0x1000) return -1; memcpy(sm_rom, tmp, n); }
        break;
    }
    have |= 1 << idx;
    return 0;
}

int sc55w_start(int m)
{
    ready = 0;
    if (have != (1 << SC55_NFILE) - 1) return -1;
    model = m;
    mcu_mk1 = m == SC55_MK1; mcu_cm300 = 0; mcu_st = 0; mcu_jv880 = 0; mcu_scb55 = 0; mcu_sc155 = 0;
    romset = m == SC55_MK1 ? ROM_SET_MK1 : ROM_SET_MK2;
    memset(ram, 0, 0x400); memset(sram, 0, 0x8000);
    uart_write_ptr = uart_read_ptr = 0;
    MCU_Init();
    MCU_PatchROM();
    MCU_Reset();
    SM_Reset();
    PCM_Reset();
    TIMER_Reset();
    rw = rr = 0; frac = 0; prev[0] = prev[1] = cur[0] = cur[1] = 0;
    step = (uint32_t)(((uint64_t)(m == SC55_MK1 ? 64000 : 66207) << 16) / 48000);
    steps = 0;
    ready = 1;
    return 0;
}
void sc55w_close(void) { ready = 0; have = 0; }
int  sc55w_ready(void) { return ready; }
int  sc55w_model(void) { return model; }
uint32_t sc55w_steps(void) { return steps; }
void sc55w_midi(uint8_t b) { if (ready) MCU_PostUART(b); }
void sc55w_reset(void)
{
    static const uint8_t gs[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
    for (unsigned i = 0; i < sizeof gs; i++) sc55w_midi(gs[i]);
}

static int pull(void)                           /* 표본 하나 더 (prev ← cur ← 새 것). 너무 오래 안 나오면 0 */
{
    uint32_t guard = 0;
    while (rw == rr) {
        MCU_Step(); steps++;
        if (++guard > 200000) { ready = 0; return 0; }   /* 롬이 엉뚱하면 멈춤 */
    }
    prev[0] = cur[0]; prev[1] = cur[1];
    cur[0] = ring[rr % RING][0]; cur[1] = ring[rr % RING][1]; rr++;
    return 1;
}
void sc55w_render(int32_t *l, int32_t *r, int n)
{
    for (int i = 0; i < n; i++) {
        if (!ready) { l[i] = r[i] = 0; continue; }
        frac += step;
        while (frac >= 0x10000) { frac -= 0x10000; if (!pull()) break; }
        int32_t f = (int32_t)frac;
        l[i] = prev[0] + (((cur[0] - prev[0]) * f) >> 16);
        r[i] = prev[1] + (((cur[1] - prev[1]) * f) >> 16);
    }
}

void sc55w_lcd_text(char *o)
{
    for (int i = 0; i < 16; i++) { uint8_t c = LCD_Data[3 + i]; o[i] = (char)(c >= 0x20 && c < 0x7F ? c : ' '); }
    o[16] = 0;
}
void sc55w_lcd_part(char *o)
{
    for (int i = 0; i < 3; i++) { uint8_t c = LCD_Data[i]; o[i] = (char)(c >= 0x20 && c < 0x7F ? c : ' '); }
    o[3] = 0;
}
static const uint8_t *glyph(uint8_t ch) { return ch >= 16 ? lcd_font[ch - 16] : &LCD_CG[(ch & 7) * 8]; }
void sc55w_lcd_dots(uint16_t dot[16])           /* 원본 LCD_FontRenderLevel: 두 줄 × 네 글자 (폭 5·5·5·1) = 16×16 */
{
    for (int y = 0; y < 16; y++) dot[y] = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 4; j++) {
            const uint8_t *f = glyph(LCD_Data[20 + j + i * 40]);
            int w = j == 3 ? 1 : 5;
            for (int row = 0; row < 8; row++)
                for (int c = 0; c < w; c++)
                    if (f[row] & (1 << (4 - c))) dot[i * 8 + row] |= (uint16_t)(0x8000 >> (j * 5 + c));
        }
}
