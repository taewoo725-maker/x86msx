/* X86MSX 8.0.96: OpenH264 의 스레드 라이브러리 대체 (운영체제 없음 — 한 코어 해독만 쓰므로 스레드·이벤트는 자리만) */
#ifndef _WELS_THREAD_API_H_
#define _WELS_THREAD_API_H_
#include "typedefs.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void *WELS_THREAD_HANDLE;
typedef void *(*LPWELS_THREAD_ROUTINE)(void *);
typedef int WELS_MUTEX;
typedef int WELS_EVENT;
#define WELS_THREAD_ROUTINE_TYPE void *
#define WELS_THREAD_ROUTINE_RETURN(rc) return (void *)(intptr_t)rc;
typedef int32_t WELS_THREAD_ERROR_CODE;
typedef int32_t WELS_THREAD_ATTR;
typedef struct _WelsLogicalProcessorInfo { int32_t ProcessorCount; } WelsLogicalProcessInfo;
#define WELS_THREAD_ERROR_OK 0
#define WELS_THREAD_ERROR_GENERAL ((uint32_t)(-1))
#define WELS_THREAD_ERROR_WAIT_OBJECT_0 0
#define WELS_THREAD_ERROR_WAIT_TIMEOUT ((uint32_t)0x00000102L)
#define WELS_THREAD_ERROR_WAIT_FAILED WELS_THREAD_ERROR_GENERAL
WELS_THREAD_ERROR_CODE WelsMutexInit(WELS_MUTEX *mutex);
WELS_THREAD_ERROR_CODE WelsMutexLock(WELS_MUTEX *mutex);
WELS_THREAD_ERROR_CODE WelsMutexUnlock(WELS_MUTEX *mutex);
WELS_THREAD_ERROR_CODE WelsMutexDestroy(WELS_MUTEX *mutex);
WELS_THREAD_ERROR_CODE WelsEventOpen(WELS_EVENT *p_event, const char *event_name = NULL);
WELS_THREAD_ERROR_CODE WelsEventClose(WELS_EVENT *event, const char *event_name = NULL);
WELS_THREAD_ERROR_CODE WelsEventSignal(WELS_EVENT *event, WELS_MUTEX *pMutex, int *iCondition);
WELS_THREAD_ERROR_CODE WelsEventWait(WELS_EVENT *event, WELS_MUTEX *pMutex, int &iCondition);
WELS_THREAD_ERROR_CODE WelsEventWaitWithTimeOut(WELS_EVENT *event, uint32_t dwMilliseconds, WELS_MUTEX *pMutex = NULL);
WELS_THREAD_ERROR_CODE WelsMultipleEventsWaitSingleBlocking(uint32_t nCount, WELS_EVENT *event_list, WELS_EVENT *master_event = NULL, WELS_MUTEX *pMutex = NULL);
WELS_THREAD_ERROR_CODE WelsThreadCreate(WELS_THREAD_HANDLE *thread, LPWELS_THREAD_ROUTINE routine, void *arg, WELS_THREAD_ATTR attr);
WELS_THREAD_ERROR_CODE WelsThreadSetName(const char *thread_name);
WELS_THREAD_ERROR_CODE WelsThreadJoin(WELS_THREAD_HANDLE thread);
WELS_THREAD_HANDLE WelsThreadSelf();
WELS_THREAD_ERROR_CODE WelsQueryLogicalProcessInfo(WelsLogicalProcessInfo *pInfo);
void WelsSleep(uint32_t dwMilliSecond);
#ifdef __cplusplus
}
#endif
#endif
