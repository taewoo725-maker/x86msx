/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vfat.h - 3.7.1 가상 하드: USB 폴더를 FAT12·FAT16 하드 디스크(MBR + 파티션 하나)로 보이게 (플랫폼과 무관, tests/vfattest.c)
 *
 * 이미지 파일을 만들지 않는다. 마운트할 때 폴더를 훑어 부트 섹터·FAT·디렉토리를 메모리에 만들고,
 * 파일 내용 섹터는 읽을 때마다 USB 파일에서 그 자리만 읽는다 (read_at).
 * MSX가 쓴 섹터: FAT·루트·부트 섹터는 메모리에서 바로 고치고, 자료 영역(파일·하위 디렉토리)은 덮개(overlay)에 둔다.
 * vf_sync: 지금 FAT·디렉토리를 다시 읽어 바뀐 파일만 USB에 다시 쓰고(새 이름은 8.3 이름 그대로), 없어진 파일은 지우고,
 *          새 디렉토리는 만든다. 다시 쓴 파일의 클러스터는 그 USB 파일을 가리키게 바꾸고 덮개에서 뺀다.
 * 배치(클러스터 번호)는 마운트하는 동안 그대로라 MSX가 기억한 FAT·디렉토리와 어긋나지 않는다. */
#ifndef X86MSX_VFAT_H
#define X86MSX_VFAT_H
#include <stdint.h>

typedef struct {
    char name[256];             /* USB 쪽 이름 (긴 이름 가능) */
    uint8_t is_dir;
    uint32_t size;
    uint16_t date, time;        /* FAT 날짜·시각 (0 = 모름) */
} VfHostEnt;

typedef struct {
    void *ctx;
    int (*list)(void *ctx, const char *path, VfHostEnt *out, int max);                  /* 항목 수, 실패 -1 */
    int (*read_at)(void *ctx, const char *path, uint32_t off, uint8_t *buf, uint32_t len);   /* 성공 0 */
    int (*write)(void *ctx, const char *path, const uint8_t *data, uint32_t len);        /* 만들거나 덮어씀, 성공 0 */
    int (*mkdir)(void *ctx, const char *path);
    int (*del)(void *ctx, const char *path);
    uint32_t (*now_ms)(void);    /* 8.0.42: 없어도 됨 (vf_sync2 의 시간 제한에 씀) */
} VfHost;

/* 형식 */
enum { VF_AUTO = 0, VF_DOS1, VF_FAT12, VF_FAT16 };
#define VF_MAX_FILES 16384                              /* 3.9.12: 4096 → 16384 (넘으면 새 파일을 USB에 못 만들고 일부는 안 보였음) */
#define VF_MAX_DIRS  2048                               /* 3.9.12: 512 → 2048 */
#define VF_ARENA     (2u << 20)                         /* 볼륨 하나에 필요한 작업 메모리 */

typedef struct { uint16_t parent; uint8_t n83[11], alive, seen; uint32_t size; uint16_t date, time; uint32_t host; } VfFile;
typedef struct { uint16_t parent; uint8_t n83[11], alive; uint32_t host; } VfDir;

typedef struct {
    const VfHost *h;
    char root[256];             /* USB 폴더 경로 (예: 0:\VMFolder\NEXTORA) */
    int id;                     /* 덮개 열쇠 (0~15) */
    int kind;                   /* VF_DOS1 / VF_FAT12 / VF_FAT16 (정해진 것) */
    int dos1geo;                /* FAT12 DOS2 볼륨인데 MSXDOS.SYS+COMMAND.COM도 있음 → DOS1이 읽는 모양(루트 112 등)으로 만들고 부트 섹터에 DOS1 부트 코드를 같이 넣음 */
    uint32_t part_start, part_secs, total;
    uint32_t spc, fatsz, root_ents, root_secs, first_fat, first_root, first_data, nclus, clb;
    uint8_t boot[512];
    uint8_t *fat, *rootd;       /* FAT 한 벌 (두 벌 다 이것을 보임), 루트 디렉토리 */
    uint16_t *own, *cidx;       /* 자료 클러스터 → USB 파일 번호 + 1, 그 파일 안 클러스터 번호 */
    VfFile *files; int nfiles;
    VfDir *dirs; int ndirs;
    char *str; uint32_t nstr, str_cap;
    int dirty;                  /* 마지막 sync 뒤 쓴 것이 있음 */
    int trunc;                  /* 3.9.12: 폴더가 너무 커서 일부를 못 담음 (파일·폴더 수 한도) */
    uint32_t used_clus;         /* 마운트 때 쓴 클러스터 */
    uint32_t need_clus;         /* 8.0.169: 폴더 전체가 차지할 클러스터 (마운트가 "full" 로 실패해도 채워짐) */
} Vf;

/* 덮개(쓴 자료 섹터) 공용 자리. 볼륨 여럿이 함께 씀. 섹터 수 = size / 516 */
void vf_pool_init(uint8_t *mem, uint32_t size);
uint32_t vf_pool_used(void), vf_pool_cap(void);

/* 폴더 root를 size_mb MB 하드로. arena = VF_ARENA 바이트. err에 이유. 성공 0 */
int vf_mount(Vf *v, const VfHost *h, const char *root, uint32_t size_mb, int kind, int id, uint8_t *arena, char *err, int errlen);
void vf_unmount(Vf *v);                                 /* 덮개에서 이 볼륨 것을 버림 (먼저 vf_sync) */
int vf_read(Vf *v, uint32_t lba, uint8_t *buf);         /* 성공 0 */
int vf_write(Vf *v, uint32_t lba, const uint8_t *buf);  /* 성공 0, 덮개가 가득이면 -1 */
/* 바뀐 것을 USB에 반영. tmp = 파일 하나를 모을 자리. 다시 쓴 파일 수, 실패 -1 (err에 이유) */
int vf_sync(Vf *v, uint8_t *tmp, uint32_t tmpsize, char *err, int errlen);
/* 8.0.42: budget_ms > 0 이면 USB 일을 하나 끝낼 때마다 시간을 보고, 넘으면 멈춤 (v->dirty 를 남겨 다음에 이어서). 최소 한 가지는 함 */
int vf_sync2(Vf *v, uint8_t *tmp, uint32_t tmpsize, char *err, int errlen, uint32_t budget_ms);
/* 폴더에 맞는 형식 (VF_AUTO일 때): MSXDOS.SYS만 있으면 DOS1, 그 밖 FAT16(작으면 FAT12) */
int vf_auto_kind(const VfHost *h, const char *root, uint32_t size_mb);
/* 형식마다 MB 범위 */
uint32_t vf_min_mb(int kind), vf_max_mb(int kind);
/* 8.0.169: 마운트한(실패한 "full" 포함) 폴더가 이 형식에서 차지할 MB (올림). 설정 용량과 견줘 모자란 만큼을 안내 */
uint32_t vf_need_mb(const Vf *v);
/* 폴더 안 파일 크기 합 (MB, 올림). 실패 -1 */
int vf_folder_mb(const VfHost *h, const char *root);
/* 8.0.62: DOS 짧은 이름 경로 → USB 쪽(긴) 이름. 찾으면 1 */
int vf_lfn(const Vf *v, const char *path, char *out, int max);
/* 8.0.66: DOS 짧은 경로의 파일 → USB 전체 경로·크기 (X86MV 소리 카트리지 파일 재생). 찾으면 1 */
int vf_find_file(const Vf *v, const char *path, char *host, int max, uint32_t *size);
#endif
