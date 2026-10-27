/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 장치 목록(src/devcat.c) 표 검사 (5.2.8) */
#include "devcat.h"
#include "profile.h"
#include "cart.h"
#include <stdio.h>
int main(void)
{
    int bad = 0, n = devcat_count(), ed = devcat_editor_count();
    for (int i = 0; i < n; i++) {
        const DevInfo *a = devcat_at(i);
        for (int j = i + 1; j < n; j++) if (devcat_at(j)->type == a->type) { printf("FAIL 형식 %d 두 번\n", a->type); bad++; }
        if (i >= ed && !(a->flags & DC_HIDDEN)) { printf("FAIL 형식 %d: 숨김 줄 뒤에 보이는 줄\n", a->type); bad++; }
        if (a->mem == DC_MEM_CART && a->mem_arg >= MAPPER_COUNT) { printf("FAIL 형식 %d: 매퍼 번호\n", a->type); bad++; }
        if (a->mem == DC_MEM_REGION && (a->mem_arg < REGION_RAM || a->mem_arg > REGION_CART)) { printf("FAIL 형식 %d: 영역 종류\n", a->type); bad++; }
        if (!a->name || !a->name[0]) { printf("FAIL 형식 %d: 이름 없음\n", a->type); bad++; }
        if ((a->flags & DC_FULLSLOT) && a->mem != DC_MEM_MAPRAM && a->mem != DC_MEM_CART) { printf("FAIL 형식 %d: 슬롯 전체인데 메모리 아님\n", a->type); bad++; }
    }
    int sup = 0; for (int i = 0; i < n; i++) sup += devcat_supported(devcat_at(i)->type);
    printf("devcattest: %d types (editor %d, supported %d), %s\n", n, ed, sup, bad ? "FAIL" : "OK");
    return bad != 0;
}
