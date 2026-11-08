/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vhdplan.c - X86VHD V1.0.0 용량 계산. 설명은 vhdplan.h */
#include "vhdplan.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define VP_STD_COUNT 12
static const uint32_t standard_sizes[VP_STD_COUNT] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 4095 };   /* 에뮬레이터 설정창의 빠른 선택과 같은 값 */

/* ---------- 문자열 ---------- */
static void copy_text(char *dst, int cap, const char *src) { snprintf(dst, (size_t)cap, "%s", src); }
static int same_name(const char *a, const char *b)                 /* Windows 처럼 대소문자 무시 */
{
    for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; if (x != y) return 0; }
    return *a == *b;
}
static void join_path(char *out, int cap, const char *dir, const char *name) { snprintf(out, (size_t)cap, "%s\\%s", dir, name); }

/* ---------- 사진(VpTree) ---------- */
void vp_init(VpTree *t, const char *root) { memset(t, 0, sizeof *t); copy_text(t->root, VP_PATH, root); }
void vp_free(VpTree *t) { free(t->nodes); memset(t, 0, sizeof *t); }

static int reserve_node(VpTree *t)
{
    if (t->count < t->cap) return 0;
    int ncap = t->cap ? t->cap * 2 : 256;
    VpNode *p = realloc(t->nodes, sizeof(VpNode) * (size_t)ncap);
    if (!p) return -1;
    t->nodes = p; t->cap = ncap;
    return 0;
}
static int find_node(const VpTree *t, const char *dir, const char *name)
{
    for (int i = 0; i < t->count; i++) if (same_name(t->nodes[i].parent, dir) && same_name(t->nodes[i].ent.name, name)) return i;
    return -1;
}
int vp_add(VpTree *t, const char *dir, const char *name, uint32_t size, int is_dir)
{
    int at = find_node(t, dir, name);
    if (at >= 0) {
        if (t->nodes[at].ent.is_dir != (uint8_t)is_dir) return -2;
        if (!is_dir) t->nodes[at].ent.size = size;
        return 0;
    }
    if (reserve_node(t)) return -1;
    VpNode *n = &t->nodes[t->count++];
    memset(n, 0, sizeof *n);
    copy_text(n->parent, VP_PATH, dir);
    copy_text(n->ent.name, (int)sizeof n->ent.name, name);
    n->ent.is_dir = (uint8_t)is_dir; n->ent.size = is_dir ? 0 : size;
    return 0;
}
void vp_remove(VpTree *t, const char *dir, const char *name)
{
    int at = find_node(t, dir, name);
    if (at < 0) return;
    int was_dir = t->nodes[at].ent.is_dir;
    t->nodes[at] = t->nodes[--t->count];
    if (!was_dir) return;
    char inner[VP_PATH]; join_path(inner, sizeof inner, dir, name);
    size_t len = strlen(inner);
    for (int i = 0; i < t->count; ) {
        const char *p = t->nodes[i].parent;
        if (!strncmp(p, inner, len) && (p[len] == 0 || p[len] == '\\')) t->nodes[i] = t->nodes[--t->count];
        else i++;
    }
}
int vp_clone(const VpTree *src, VpTree *dst)
{
    vp_init(dst, src->root);
    if (!src->count) return 0;
    dst->nodes = malloc(sizeof(VpNode) * (size_t)src->count);
    if (!dst->nodes) return -1;
    memcpy(dst->nodes, src->nodes, sizeof(VpNode) * (size_t)src->count);
    dst->count = dst->cap = src->count;
    return 0;
}

int vp_scan(VpTree *t, const VfHost *real)
{
    static VfHostEnt ents[1024];
    char (*todo)[VP_PATH] = malloc((size_t)VF_MAX_DIRS * VP_PATH);
    if (!todo) return -1;
    int head = 0, tail = 0, result = 0;
    copy_text(todo[tail++], VP_PATH, t->root);
    while (head < tail) {
        char dir[VP_PATH]; copy_text(dir, VP_PATH, todo[head++]);
        int n = real->list(real->ctx, dir, ents, 1024);
        if (n < 0) { if (head == 1) result = -1; continue; }
        for (int i = 0; i < n; i++) {
            if (!ents[i].name[0] || ents[i].name[0] == '.') continue;     /* vf_mount 와 같은 규칙 */
            if (vp_add(t, dir, ents[i].name, ents[i].size, ents[i].is_dir)) { result = -1; continue; }
            if (ents[i].is_dir && tail < VF_MAX_DIRS) join_path(todo[tail++], VP_PATH, dir, ents[i].name);
        }
    }
    free(todo);
    return result;
}

/* ---------- vf_mount 에 먹일 가짜 호스트 (사진을 읽기 전용 폴더처럼 보임) ---------- */
static int snap_list(void *ctx, const char *path, VfHostEnt *out, int max)
{
    const VpTree *t = ctx; int n = 0, found = !strcmp(path, t->root);
    for (int i = 0; i < t->count && n < max; i++) {
        if (strcmp(t->nodes[i].parent, path)) continue;
        out[n++] = t->nodes[i].ent; found = 1;
    }
    return found ? n : -1;
}
static int snap_read_at(void *c, const char *p, uint32_t o, uint8_t *b, uint32_t l) { (void)c; (void)p; (void)o; (void)b; (void)l; return -1; }
static int snap_write(void *c, const char *p, const uint8_t *d, uint32_t l) { (void)c; (void)p; (void)d; (void)l; return -1; }
static int snap_mkdir(void *c, const char *p) { (void)c; (void)p; return -1; }
static int snap_del(void *c, const char *p) { (void)c; (void)p; return -1; }

static uint8_t *mount_arena;
static int mount_snapshot(const VpTree *t, uint32_t size_mb, int kind, Vf *v, char *why, int whylen)
{
    static int pool_ready;
    if (!mount_arena) mount_arena = malloc(VF_ARENA);
    if (!mount_arena) { copy_text(why, whylen, "memory"); return -1; }
    if (!pool_ready) { uint32_t cap = 16u << 20; vf_pool_init(malloc(cap), cap); pool_ready = 1; }
    VfHost host = { (void *)t, snap_list, snap_read_at, snap_write, snap_mkdir, snap_del, 0 };
    int rc = vf_mount(v, &host, t->root, size_mb, kind, 15, mount_arena, why, whylen);
    return rc;
}

/* ---------- 판정 ---------- */
uint32_t vp_pick_size(int kind, uint32_t need_mb)
{
    uint32_t lo = vf_min_mb(kind), hi = vf_max_mb(kind);
    for (int i = 0; i < VP_STD_COUNT; i++) if (standard_sizes[i] >= need_mb && standard_sizes[i] >= lo && standard_sizes[i] <= hi) return standard_sizes[i];
    return need_mb <= hi ? hi : 0;
}
static uint32_t clamp_size(int kind, uint32_t mb)
{
    if (mb > vf_max_mb(kind)) mb = vf_max_mb(kind);
    if (mb < vf_min_mb(kind)) mb = vf_min_mb(kind);
    return mb;
}
int vp_check(const VpTree *t, uint32_t size_mb, int kind, VpFit *out)
{
    static Vf v;
    memset(out, 0, sizeof *out);
    char why[16] = "";
    int rc = mount_snapshot(t, size_mb, kind, &v, why, sizeof why);
    out->kind = v.kind;
    out->cap_mb = clamp_size(v.kind, size_mb);
    out->truncated = v.trunc;
    out->cap_kb = (uint32_t)(((uint64_t)v.nclus * v.clb) >> 10);
    out->fits = rc == 0;
    copy_text(out->why, (int)sizeof out->why, why);
    uint32_t clus = rc == 0 ? v.used_clus : v.need_clus;
    out->used_kb = (uint32_t)(((uint64_t)clus * v.clb) >> 10);
    out->need_mb = vf_need_mb(&v);
    if (rc == 0) { out->suggest_mb = out->cap_mb; return 1; }
    /* 모자라면 표준 용량 중 들어가는 가장 작은 것을 찾음 (형식 한도 안) */
    for (int i = 0; i < VP_STD_COUNT; i++) {
        uint32_t mb = standard_sizes[i];
        if (mb < out->cap_mb || mb < vf_min_mb(v.kind) || mb > vf_max_mb(v.kind)) continue;
        Vf probe; char w2[16];
        if (mount_snapshot(t, mb, v.kind, &probe, w2, sizeof w2) == 0) { out->suggest_mb = mb; break; }
    }
    return 0;
}

/* ---------- 복사 계획 ---------- */
/* 항목마다 "이미 사진에 있는 노드 번호"를 한 번만 찾아 두고(없으면 -1), 앞쪽 count 개를 더할 때는 찾지 않고 바로 더한다.
 * 파일이 수천 개여도 이분 탐색의 각 시도가 느려지지 않게 하려는 것 */
static int *locate_existing(const VpTree *t, const VpItem *items, int n)
{
    int *at = malloc(sizeof(int) * (size_t)(n ? n : 1));
    if (!at) return NULL;
    for (int i = 0; i < n; i++) at[i] = find_node(t, items[i].dest_dir, items[i].name);
    return at;
}
static int apply_items(VpTree *t, const VpItem *items, const int *existing, int count)
{
    for (int i = 0; i < count; i++) {
        if (existing[i] >= 0) {
            VpNode *node = &t->nodes[existing[i]];
            if (node->ent.is_dir == (uint8_t)items[i].is_dir && !items[i].is_dir) node->ent.size = items[i].size;
            continue;
        }
        if (reserve_node(t)) return -1;
        VpNode *n = &t->nodes[t->count++];
        memset(n, 0, sizeof *n);
        copy_text(n->parent, VP_PATH, items[i].dest_dir);
        copy_text(n->ent.name, (int)sizeof n->ent.name, items[i].name);
        n->ent.is_dir = (uint8_t)items[i].is_dir; n->ent.size = items[i].is_dir ? 0 : items[i].size;
    }
    return 0;
}
static int prefix_fits(const VpTree *base, const VpItem *items, const int *existing, int count, uint32_t size_mb, int kind, VpFit *out)
{
    VpTree trial;
    if (vp_clone(base, &trial)) return 0;
    int ok = apply_items(&trial, items, existing, count) == 0 && vp_check(&trial, size_mb, kind, out);
    vp_free(&trial);
    return ok;
}
int vp_plan_fit(const VpTree *t, const VpItem *items, int n, uint32_t size_mb, int kind, VpFit *out_all)
{
    VpFit scratch;
    if (!out_all) out_all = &scratch;
    int *existing = locate_existing(t, items, n);
    if (!existing) return 0;
    int fitted;
    if (prefix_fits(t, items, existing, n, size_mb, kind, out_all)) fitted = n;
    else {
        int lo = 0, hi = n;                              /* lo 개는 들어가고 hi 개는 안 들어감 */
        while (hi - lo > 1) {
            int mid = (lo + hi) / 2;
            if (prefix_fits(t, items, existing, mid, size_mb, kind, &scratch)) lo = mid; else hi = mid;
        }
        fitted = lo;
    }
    free(existing);
    return fitted;
}

/* ---------- 이름 ---------- */
static void format_83(const uint8_t *raw, char *out)
{
    int k = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) out[k++] = (char)raw[i];
    if (raw[8] != ' ') { out[k++] = '.'; for (int i = 8; i < 11 && raw[i] != ' '; i++) out[k++] = (char)raw[i]; }
    out[k] = 0;
}
static int find_dir_index(const Vf *v, const char *rel_dir)    /* "A\B" → 폴더 번호. 루트 0. 없으면 -1 */
{
    int cur = 0; char part[256]; const char *p = rel_dir;
    while (*p) {
        int k = 0; while (*p && *p != '\\') { if (k < 255) part[k++] = *p; p++; }
        part[k] = 0; if (*p == '\\') p++;
        if (!k) continue;
        int next = -1;
        for (int j = 1; j < v->ndirs; j++) if (v->dirs[j].parent == cur && same_name(v->str + v->dirs[j].host, part)) { next = j; break; }
        if (next < 0) return -1;
        cur = next;
    }
    return cur;
}
int vp_names(const VpTree *t, uint32_t size_mb, int kind, const char *dir, VpName *out, int max)
{
    static Vf v; char why[16];
    mount_snapshot(t, size_mb, kind, &v, why, sizeof why);     /* 못 담아도 이름은 이미 만들어져 있음 */
    size_t rootlen = strlen(t->root);
    const char *rel = !strncmp(dir, t->root, rootlen) ? dir + rootlen : dir;
    while (*rel == '\\') rel++;
    int d = find_dir_index(&v, rel), n = 0;
    if (d < 0) return 0;
    for (int j = 1; j < v.ndirs && n < max; j++) if (v.dirs[j].alive && v.dirs[j].parent == d) {
        VpName *o = &out[n++]; copy_text(o->host_name, sizeof o->host_name, v.str + v.dirs[j].host);
        format_83(v.dirs[j].n83, o->msx_name); o->is_dir = 1; o->size = 0;
    }
    for (int j = 0; j < v.nfiles && n < max; j++) if (v.files[j].alive && v.files[j].parent == d) {
        VpName *o = &out[n++]; copy_text(o->host_name, sizeof o->host_name, v.str + v.files[j].host);
        format_83(v.files[j].n83, o->msx_name); o->is_dir = 0; o->size = v.files[j].size;
    }
    return n;
}
