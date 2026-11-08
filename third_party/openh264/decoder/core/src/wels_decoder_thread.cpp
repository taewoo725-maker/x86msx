/* X86MSX 8.0.96: OpenH264 wels_decoder_thread.cpp 대체 — 스레드 없이 한 코어로만 해독 (이벤트는 늘 "신호 있음") */
#include "wels_decoder_thread.h"
int32_t GetCPUCount() { return 1; }
int EventCreate(SWelsDecEvent *e, int manualReset, int initialState) { e->manualReset = manualReset; e->isSignaled = initialState; e->c = 0; e->m = 0; return 0; }
void EventPost(SWelsDecEvent *e) { e->isSignaled = 1; }
int EventWait(SWelsDecEvent *e, int32_t) { if (!e->manualReset) e->isSignaled = 0; return WELS_DEC_THREAD_WAIT_SIGNALED; }
void EventReset(SWelsDecEvent *e) { e->isSignaled = 0; }
void EventDestroy(SWelsDecEvent *) {}
int SemCreate(SWelsDecSemphore *s, long value, long max) { s->v = value; s->max = max; s->e = 0; s->m = 0; return 0; }
int SemWait(SWelsDecSemphore *, int32_t) { return WELS_DEC_THREAD_WAIT_SIGNALED; }
void SemRelease(SWelsDecSemphore *s, long *prev) { if (prev) *prev = s->v; if (s->v < s->max) s->v++; }
void SemDestroy(SWelsDecSemphore *) {}
int ThreadCreate(SWelsDecThread *, LPWELS_THREAD_ROUTINE, void *) { return 1; }
int ThreadWait(SWelsDecThread *) { return 0; }
