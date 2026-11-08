/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* mp4limitstest (8.0.140) - MP4 영상 크기 한계 시험 */
#include <stdio.h>
#include "../src/mp4limits.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("실패 줄 %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
    CHECK(mp4lim_size_ok(640, 360));
    CHECK(mp4lim_size_ok(1280, 720));
    CHECK(mp4lim_size_ok(1920, 1080));         /* 8.0.140: 이제 받음 */
    CHECK(mp4lim_size_ok(1920, 1088));
    CHECK(!mp4lim_size_ok(1921, 1080));
    CHECK(!mp4lim_size_ok(1920, 1089));
    CHECK(!mp4lim_size_ok(3840, 2160));        /* 4K 는 계속 거절 */
    CHECK(!mp4lim_size_ok(0, 720));
    CHECK(!mp4lim_size_ok(1280, 0));
    CHECK(mp4lim_picture_bytes(1920, 1080) == 1920u * 1080u * 3u / 2u + 4096u);
    CHECK(mp4lim_picture_bytes(1281, 721) == 1282u * 722u * 3u / 2u + 4096u);   /* 홀수는 짝수로 올림 */
    CHECK(mp4lim_picture_bytes(1920, 1088) <= MP4LIM_PICTURE_BYTES_MAX);
    CHECK(MP4LIM_SOURCE_COLUMNS > MP4LIM_MAX_WIDTH);
    CHECK(mp4lim_error_from_why(2) == 13 && mp4lim_error_from_why(10) == 13);      /* 크기 초과 → 13 */
    CHECK(mp4lim_error_from_why(5) == 14 && mp4lim_error_from_why(8) == 14 && mp4lim_error_from_why(9) == 14);   /* 해독 실패 → 14 (둘리: 이유 8) */
    CHECK(mp4lim_error_from_why(3) == 4 && mp4lim_error_from_why(4) == 4 && mp4lim_error_from_why(0) == 4);      /* 그 밖은 프로파일 4 */
    if (fails) { printf("mp4limitstest 실패 %d\n", fails); return 1; }
    printf("mp4limitstest 통과\n");
    return 0;
}
