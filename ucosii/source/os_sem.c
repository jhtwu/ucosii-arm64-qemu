#include <ucos_ii.h>

#if OS_SEM_EN > 0u

OS_EVENT *OSSemCreate(INT16U cnt)
{
    OS_EVENT *pevent;
#if OS_CRITICAL_METHOD == 3u
    OS_CPU_SR cpu_sr = 0u;
#endif

    if (OSIntNesting > 0u) {
        return (OS_EVENT *)0;
    }

    OS_ENTER_CRITICAL();
    pevent = OSEventFreeList;
    if (pevent == (OS_EVENT *)0) {
        OS_EXIT_CRITICAL();
        return (OS_EVENT *)0;
    }
    OSEventFreeList          = (OS_EVENT *)OSEventFreeList->OSEventPtr;
    OS_EXIT_CRITICAL();

    pevent->OSEventType      = OS_EVENT_TYPE_SEM;
    pevent->OSEventCnt       = cnt;
    pevent->OSEventPtr       = (void *)0;
#if OS_EVENT_NAME_EN > 0u
    pevent->OSEventName      = (INT8U *)"?";
#endif
    OS_EventWaitListInit(pevent);

    return pevent;
}

INT16U OSSemAccept(OS_EVENT *pevent)
{
    INT16U cnt;
#if OS_CRITICAL_METHOD == 3u
    OS_CPU_SR cpu_sr = 0u;
#endif

    if (pevent == (OS_EVENT *)0) {
        return 0u;
    }
    if (pevent->OSEventType != OS_EVENT_TYPE_SEM) {
        return 0u;
    }

    OS_ENTER_CRITICAL();
    cnt = pevent->OSEventCnt;
    if (cnt > 0u) {
        pevent->OSEventCnt--;
    }
    OS_EXIT_CRITICAL();
    return cnt;
}

void OSSemPend(OS_EVENT *pevent, INT32U timeout, INT8U *perr)
{
#if OS_CRITICAL_METHOD == 3u
    OS_CPU_SR cpu_sr = 0u;
#endif

    if (perr == (INT8U *)0) {
        return;
    }
    if (pevent == (OS_EVENT *)0) {
        *perr = OS_ERR_PEVENT_NULL;
        return;
    }
    if (pevent->OSEventType != OS_EVENT_TYPE_SEM) {
        *perr = OS_ERR_EVENT_TYPE;
        return;
    }
    if (OSIntNesting > 0u) {
        *perr = OS_ERR_PEND_ISR;
        return;
    }

    OS_ENTER_CRITICAL();
    if (pevent->OSEventCnt > 0u) {
        pevent->OSEventCnt--;
        OS_EXIT_CRITICAL();
        *perr = OS_ERR_NONE;
        return;
    }

    OSTCBCur->OSTCBStat     |= OS_STAT_SEM;
    OSTCBCur->OSTCBStatPend  = OS_STAT_PEND_OK;
    OSTCBCur->OSTCBDly       = timeout;
    OS_EventTaskWait(pevent);
    OS_EXIT_CRITICAL();
    OS_Sched();

    OS_ENTER_CRITICAL();
    if (OSTCBCur->OSTCBStatPend == OS_STAT_PEND_OK) {
        OSTCBCur->OSTCBStat     &= ~(OS_STAT_SEM | OS_STAT_PEND_ANY);
        OSTCBCur->OSTCBStatPend  = OS_STAT_PEND_OK;
        OS_EXIT_CRITICAL();
        *perr = OS_ERR_NONE;
        return;
    }

    OS_EventTaskRemove(OSTCBCur, pevent);
    OSTCBCur->OSTCBStat     &= ~(OS_STAT_SEM | OS_STAT_PEND_ANY);
    if (OSTCBCur->OSTCBStatPend == OS_STAT_PEND_TO) {
        OSTCBCur->OSTCBStatPend = OS_STAT_PEND_OK;
        OS_EXIT_CRITICAL();
        *perr = OS_ERR_TIMEOUT;
    } else {
        OSTCBCur->OSTCBStatPend = OS_STAT_PEND_OK;
        OS_EXIT_CRITICAL();
        *perr = OS_ERR_PEND_ABORT;
    }
}

INT8U OSSemPost(OS_EVENT *pevent)
{
#if OS_CRITICAL_METHOD == 3u
    OS_CPU_SR cpu_sr = 0u;
#endif

    if (pevent == (OS_EVENT *)0) {
        return OS_ERR_PEVENT_NULL;
    }
    if (pevent->OSEventType != OS_EVENT_TYPE_SEM) {
        return OS_ERR_EVENT_TYPE;
    }

    OS_ENTER_CRITICAL();
    if (pevent->OSEventGrp != 0u) {
        (void)OS_EventTaskRdy(pevent, (void *)0, OS_STAT_SEM, OS_STAT_PEND_OK);
        OS_EXIT_CRITICAL();
        OS_Sched();
        return OS_ERR_NONE;
    }

    if (pevent->OSEventCnt < 0xFFFFu) {
        pevent->OSEventCnt++;
        OS_EXIT_CRITICAL();
        return OS_ERR_NONE;
    }

    OS_EXIT_CRITICAL();
    return OS_ERR_SEM_OVF;
}

#endif /* OS_SEM_EN > 0u */
