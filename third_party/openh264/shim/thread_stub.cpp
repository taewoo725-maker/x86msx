/* X86MSX 8.0.96: OpenH264 WelsThreadLib.cpp 대체 — 스레드 없음 (해독 스레드 수 1 로만 씀) */
#include "WelsThreadLib.h"
WELS_THREAD_ERROR_CODE WelsMutexInit(WELS_MUTEX *m) { *m = 0; return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsMutexLock(WELS_MUTEX *) { return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsMutexUnlock(WELS_MUTEX *) { return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsMutexDestroy(WELS_MUTEX *) { return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsEventOpen(WELS_EVENT *e, const char *) { *e = 0; return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsEventClose(WELS_EVENT *, const char *) { return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsEventSignal(WELS_EVENT *, WELS_MUTEX *, int *) { return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsEventWait(WELS_EVENT *, WELS_MUTEX *, int &) { return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsEventWaitWithTimeOut(WELS_EVENT *, uint32_t, WELS_MUTEX *) { return WELS_THREAD_ERROR_WAIT_TIMEOUT; }
WELS_THREAD_ERROR_CODE WelsMultipleEventsWaitSingleBlocking(uint32_t, WELS_EVENT *, WELS_EVENT *, WELS_MUTEX *) { return WELS_THREAD_ERROR_WAIT_TIMEOUT; }
WELS_THREAD_ERROR_CODE WelsThreadCreate(WELS_THREAD_HANDLE *, LPWELS_THREAD_ROUTINE, void *, WELS_THREAD_ATTR) { return WELS_THREAD_ERROR_GENERAL; }
WELS_THREAD_ERROR_CODE WelsThreadSetName(const char *) { return WELS_THREAD_ERROR_OK; }
WELS_THREAD_ERROR_CODE WelsThreadJoin(WELS_THREAD_HANDLE) { return WELS_THREAD_ERROR_OK; }
WELS_THREAD_HANDLE WelsThreadSelf() { return 0; }
WELS_THREAD_ERROR_CODE WelsQueryLogicalProcessInfo(WelsLogicalProcessInfo *p) { p->ProcessorCount = 1; return WELS_THREAD_ERROR_OK; }
void WelsSleep(uint32_t) {}
