/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * msx_host.c - 호스트 빌드 전용: 프로필 폴더에서 config.ini와 ROM 파일을 읽어 기계 구성
 */
#include "msx_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_file(const char *path, RomImage *img, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "cannot open ROM %s", path); return -1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (unsigned long)n > (4ul << 20)) {
        fclose(f);
        snprintf(err, errlen, "bad ROM size %ld: %s", n, path);
        return -1;
    }
    img->data = malloc((size_t)n);
    img->size = (uint32_t)n;
    if (!img->data || fread(img->data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        snprintf(err, errlen, "read error: %s", path);
        return -1;
    }
    fclose(f);
    return 0;
}

int msx_host_init(MSX *m, const char *profile_dir, char *err, size_t errlen)
{
    MachineProfile prof;
    RomImage roms[MAX_REGIONS];
    memset(roms, 0, sizeof roms);
    if (profile_load(profile_dir, &prof, err, errlen)) return -1;
    for (int i = 0; i < prof.region_count; i++)
        if (prof.region[i].type != REGION_RAM && prof.region[i].file[0] && load_file(prof.region[i].file, &roms[i], err, errlen))
            return -1;
    int e = msx_setup(m, &prof, roms);
    if (e) { snprintf(err, errlen, "%s", msx_error(e)); return -1; }
    return 0;
}

void msx_host_free(MSX *m)
{
    for (int i = 0; i < MAX_REGIONS; i++) free(m->rom[i].data);
    memset(m->rom, 0, sizeof m->rom);
}
