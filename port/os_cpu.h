#ifndef OS_CPU_H
#define OS_CPU_H

#include <cpu.h>

#ifdef OS_CPU_GLOBALS
#define OS_CPU_EXT
#else
#define OS_CPU_EXT extern
#endif

#ifndef OS_CPU_EXCEPT_STK_SIZE
#define OS_CPU_EXCEPT_STK_SIZE 1024u
#endif

#define OS_CPU_STK_ALIGN_BYTES 16u

typedef CPU_STK OS_STK;
typedef CPU_SR  OS_CPU_SR;

struct os_tcb;
typedef struct os_tcb OS_TCB;

#define OS_STK_GROWTH    1u
#define OS_TASK_SW()     OSCtxSw()
#define OS_CRITICAL_METHOD 3u

OS_CPU_SR OS_CPU_SR_Save(void);
void      OS_CPU_SR_Restore(OS_CPU_SR cpu_sr);

#if (OS_CRITICAL_METHOD == 3u)
#define OS_ENTER_CRITICAL() do { cpu_sr = OS_CPU_SR_Save(); } while (0)
#define OS_EXIT_CRITICAL()  do { OS_CPU_SR_Restore(cpu_sr); } while (0)
#endif

OS_CPU_EXT OS_STK  OS_CPU_ExceptStk[OS_CPU_EXCEPT_STK_SIZE];
OS_CPU_EXT OS_STK *OS_CPU_ExceptStkBase;

void   OSCtxSw(void);
void   OSIntCtxSw(void);
void   OSStartHighRdy(void);

OS_STK *OSTaskStkInit(void (*task)(void *p_arg),
                      void        *p_arg,
                      OS_STK      *ptos,
                      INT16U       opt);

void   OSInitHookBegin(void);
void   OSInitHookEnd(void);
void   OSTaskCreateHook(OS_TCB *ptcb);
void   OSTaskDelHook(OS_TCB *ptcb);
void   OSTaskIdleHook(void);
void   OSTaskReturnHook(OS_TCB *ptcb);
void   OSTaskStatHook(void);
void   OSTCBInitHook(OS_TCB *ptcb);
void   OSTimeTickHook(void);
void   OS_CPU_ExceptHndlr(void);

#endif
