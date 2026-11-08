/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vhdplan.h - X86VHD V1.0.0: 가상 하드 폴더의 용량 계산 (플랫폼과 무관, tests/vhdplantest.c)
 *
 * 에뮬레이터가 폴더를 하드로 보이게 하는 vf_mount(kernel/vfat.c)를 그대로 써서 "이 폴더가 이 크기·형식에 들어가는가"를 판정한다.
 * 그래서 여기서 "들어간다"고 하면 에뮬레이터에서도 같은 판정이다.
 * 폴더 내용을 메모리에 사진(VpTree)으로 찍어 두고, 거기에 넣을 파일을 가상으로 더해 가며 판정하므로 디스크를 건드리지 않는다. */
#ifndef X86VHD_PLAN_H
#define X86VHD_PLAN_H
#include "../../kernel/vfat.h"

#define VP_PATH 512

typedef struct { char parent[VP_PATH]; VfHostEnt ent; } VpNode;     /* parent = 이 항목이 든 폴더의 전체 경로 */
typedef struct { VpNode *nodes; int count, cap; char root[VP_PATH]; } VpTree;

/* 판정 결과 */
typedef struct {
    int fits;                   /* 1 = vf_mount 가 성공 */
    int kind;                   /* 실제로 쓴 형식 (VF_DOS1 / VF_FAT12 / VF_FAT16) */
    uint32_t cap_mb;            /* 형식 한도에 맞춘 용량 */
    uint32_t cap_kb, used_kb;   /* 하드 안 자료 영역 크기와 쓴 크기 (KB). 못 담으면 used_kb = 필요한 크기 */
    uint32_t need_mb;           /* 필요한 MB (올림) */
    uint32_t suggest_mb;        /* 넣을 수 있는 가장 작은 용량 (이 형식 한도 안, 못 찾으면 0) */
    int truncated;              /* 파일·폴더 수 한도를 넘어 일부가 빠짐 */
    char why[16];               /* 실패 이유: "full" "root" "folder" "size" ... */
} VpFit;

/* 복사할 항목 하나 (폴더가 먼저, 그 안 파일이 뒤) */
typedef struct { char dest_dir[VP_PATH]; char name[256]; uint32_t size; int is_dir; } VpItem;

/* 항목 하나의 MSX 쪽 이름 */
typedef struct { char host_name[256]; char msx_name[16]; int is_dir; uint32_t size; } VpName;

void vp_init(VpTree *t, const char *root);
void vp_free(VpTree *t);
int vp_scan(VpTree *t, const VfHost *real);                                 /* 실제 폴더 훑기. 성공 0 */
int vp_add(VpTree *t, const char *dir, const char *name, uint32_t size, int is_dir);   /* 같은 이름은 바꿈. 이름이 폴더·파일로 엇갈리면 -2 */
void vp_remove(VpTree *t, const char *dir, const char *name);              /* 폴더면 안까지 */
int vp_clone(const VpTree *src, VpTree *dst);
int vp_check(const VpTree *t, uint32_t size_mb, int kind, VpFit *out);      /* 판정. 들어가면 1 */
/* 앞에서부터 몇 개까지 들어가는가 (이분 탐색). 모두 들어가면 n. out_all = 모두 넣었을 때의 판정 */
int vp_plan_fit(const VpTree *t, const VpItem *items, int n, uint32_t size_mb, int kind, VpFit *out_all);
/* 폴더 dir 안 항목의 MSX 이름 (8.3). 개수 */
int vp_names(const VpTree *t, uint32_t size_mb, int kind, const char *dir, VpName *out, int max);
uint32_t vp_pick_size(int kind, uint32_t need_mb);                          /* 표준 용량 중 need_mb 이상인 가장 작은 값 (없으면 0) */
#endif
