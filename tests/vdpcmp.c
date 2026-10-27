/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* vdpcmp: openMSX 대조용 — 입력 파일(VRAM 131072 + R32..R46 15바이트 + 모드 레지스터 R0,R1,R8,R9)을 받아 VDP 명령 한 번 실행하고 VRAM을 출력 */
#include <stdio.h>
#include <string.h>
#include "../src/vdp.h"
static VDP v;
static void wr(int r, int val) { vdp_write_ctrl(&v, (uint8_t)val); vdp_write_ctrl(&v, (uint8_t)(0x80 | r)); }
int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    static uint8_t in[131072 + 32];
    FILE *f = fopen(argv[1], "rb"); if (!f) return 1; size_t n = fread(in, 1, sizeof in, f); fclose(f); (void)n;
    vdp_init(&v, 2);
    memcpy(v.vram, in, 131072);
    const uint8_t *p = in + 131072;      /* R0,R1,R8,R9, R32..R46 */
    wr(0, p[0]); wr(1, p[1]); wr(8, p[2]); wr(9, p[3]);
    for (int r = 32; r <= 45; r++) wr(r, p[4 + r - 32]);
    wr(46, p[4 + 14]);
    f = fopen(argv[2], "wb"); fwrite(v.vram, 1, 131072, f); fclose(f);
    return 0;
}
