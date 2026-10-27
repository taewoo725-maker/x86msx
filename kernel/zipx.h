/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* zipx.h - 3.2.0 zip 안의 파일 꺼내기 (요구사항 MED-05)
 * 중앙 디렉터리를 읽어 확장자가 맞는 첫 파일을 꺼낸다(저장 0 또는 deflate 8, tinf로 풂). 플랫폼과 무관.
 * exts = "rom|mx1|mx2" 처럼 '|'로 나눈 확장자 (빈 문자열 = 아무 파일). name = zip 안의 이름(폴더 뺌). */
#ifndef X86MSX_ZIPX_H
#define X86MSX_ZIPX_H
#include <stdint.h>
enum { ZIPX_OK = 0, ZIPX_BAD = -1, ZIPX_NONE = -2, ZIPX_BIG = -3, ZIPX_METHOD = -4 };
int zip_extract(const uint8_t *zip, uint32_t zlen, const char *exts, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax);

int zip_extract_nth(const uint8_t *zip, uint32_t zlen, const char *exts, int nth, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax, int *total);   /* 4.0.34 */
int zip_names_ex(const uint8_t *zip, uint32_t zlen, const char *exts, char (*names)[48], uint32_t *usize, uint32_t *dos, int max);   /* 4.0.136 */
int zip_names(const uint8_t *zip, uint32_t zlen, const char *exts, char (*names)[48], int max);   /* 4.0.34 */
int zip_peek(const uint8_t *zip, uint32_t zlen, const char *exts, char *name, int namemax, uint32_t *usize_out);   /* 4.0.34 */

/* 4.0.3: zip 안을 폴더처럼 보기 (이미지 파일 관리). 폴더 칸이 없는 zip도 경로에서 폴더를 만들어 보여 줌 */
typedef struct { char name[256]; uint8_t dir; uint32_t size; uint16_t year; uint8_t mon, day, hour, min; } ZipEnt;
/* dir = zip 안 폴더 ("" = 맨 위, "a/b" = 끝 빗금 없이). 바로 아래 항목을 하나씩 cb(ctx, 항목)로 알림 (같은 폴더 이름이 여러 번 올 수 있으니 받는 쪽이 걸러냄,
 * cb가 0이 아닌 값을 돌려주면 멈춤). 0 = 성공, 음수 = ZIPX_* 오류 */
typedef int (*ZipListCb)(void *ctx, const ZipEnt *e);
int zip_list(const uint8_t *zip, uint32_t zlen, const char *dir, ZipListCb cb, void *ctx);
/* path = zip 안 전체 경로 ("a/b/c.txt")의 파일을 out으로. 0 = 성공, 아니면 ZIPX_* */
int zip_read(const uint8_t *zip, uint32_t zlen, const char *path, uint8_t *out, uint32_t max, uint32_t *size);
#endif
