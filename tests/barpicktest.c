/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* 4단계 시험(C 쪽): 어셈블리 b6_pick (X86MV/시험/pickh.c 가 만든 pick.bin) 와 mmvid.c 의 bar_pick 이 같은 팔레트에서 같은 칸을 고르는지.
   사용: ./build/barpicktest <pick.bin> */
#include <stdio.h>
#include "../src/mmvid.c"
int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "pick.bin", "rb"); if (!f) { perror("pick.bin"); return 2; }
    uint8_t rec[36]; int n = 0, bad = 0;
    while (fread(rec, 1, 36, f) == 36) {
        uint8_t pal[16][3]; int code[4];
        for (int k = 0; k < 16; k++) { pal[k][0] = rec[2 * k] >> 4; pal[k][1] = rec[2 * k + 1] & 7; pal[k][2] = rec[2 * k] & 7; }
        bar_pick(5, (const uint8_t (*)[3])pal, 16, code);
        for (int r = 0; r < 4; r++) if (code[r] != rec[32 + r]) { bad++; if (bad < 5) printf("다름: 팔레트 %d 역할 %d  C=%d Z80=%d\n", n, r, code[r], rec[32 + r]); break; }
        n++;
    }
    printf("barpicktest: %d 팔레트, 다른 것 %d\n", n, bad);
    if (bad) return 1; puts("barpicktest: all passed"); return 0;
}
