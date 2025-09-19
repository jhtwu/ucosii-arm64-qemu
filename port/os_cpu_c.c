/*
 * 中文：此檔案提供 uC/OS-II ARMv8-A 移植層的 C 語言支援，包括關鍵區保護、初始化 Hook 以及建立
 *       任務堆疊的流程。需和 os_cpu_a.S 的組語實作協同運作。
 * English: C-side support for the ARMv8-A uC/OS-II port. It handles critical section primitives, init hooks,
 *          and stack frame construction so that the assembly context switcher in os_cpu_a.S can operate correctly.
 */
#define OS_CPU_GLOBALS

#include <ucos_ii.h>

OS_STK  OS_CPU_ExceptStk[OS_CPU_EXCEPT_STK_SIZE];
OS_STK *OS_CPU_ExceptStkBase;

/*
 * 中文：儲存並關閉中斷，確保進入臨界區時 DAIF 狀態被保存。
 * English: Save DAIF and mask interrupts so critical regions preserve prior interrupt state.
 */
OS_CPU_SR OS_CPU_SR_Save(void)
{
    OS_CPU_SR sr;
    __asm__ volatile(
        "mrs %0, daif\n\t"
        "msr daifset, #0xf\n\t"
        : "=r"(sr)
        :
        : "memory");
    return sr;
}

/*
 * 中文：恢復先前儲存的 DAIF 狀態，結束臨界區。
 * English: Restore the saved DAIF bits to exit the critical section.
 */
void OS_CPU_SR_Restore(OS_CPU_SR sr)
{
    __asm__ volatile(
        "msr daif, %0\n\t"
        :
        : "r"(sr)
        : "memory");
}

/*
 * 中文：初始化 Hook，清空例外堆疊並記錄堆疊頂端位置。
 * English: Init hook clears the exception stack and records its top-of-stack pointer.
 */
void OSInitHookBegin(void)
{
    for (INT32U i = 0u; i < OS_CPU_EXCEPT_STK_SIZE; ++i) {
        OS_CPU_ExceptStk[i] = 0u;
    }

    OS_CPU_ExceptStkBase = &OS_CPU_ExceptStk[OS_CPU_EXCEPT_STK_SIZE - 1u];
}

void OSInitHookEnd(void)
{
}

void OSTaskCreateHook(OS_TCB *ptcb)
{
    (void)ptcb;
}

void OSTaskDelHook(OS_TCB *ptcb)
{
    (void)ptcb;
}

void OSTaskIdleHook(void)
{
}

void OSTaskReturnHook(OS_TCB *ptcb)
{
    (void)ptcb;
}

void OSTaskStatHook(void)
{
}

void OSTCBInitHook(OS_TCB *ptcb)
{
    (void)ptcb;
}

void OSTimeTickHook(void)
{
}

/*
 * 中文：建立任務初始堆疊，配置與 SAVE/RESTORE 宏一致的暫存器內容。
 * English: Build the initial task stack to mirror the SAVE/RESTORE macros' register layout.
 */
OS_STK *OSTaskStkInit(void (*task)(void *p_arg),
                      void        *p_arg,
                      OS_STK      *ptos,
                      INT16U       opt)
{
    (void)opt;

    enum {
        CTX_ENTRIES = 34u
    };

    OS_STK *p_aligned = ptos + 1u;
    p_aligned = (OS_STK *)((OS_STK)p_aligned & ~((OS_STK)0x0Fu));

    OS_STK *p_stk = p_aligned - CTX_ENTRIES;

    p_stk[0u]  = 0x0000000000000005ull;       /* SPSR */
    p_stk[1u]  = (OS_STK)task;                /* ELR  */
    p_stk[2u]  = (OS_STK)OS_TaskReturn;       /* X30  */
    p_stk[3u]  = 0u;                          /* X29  */
    p_stk[4u]  = 0u;                          /* X28  */
    p_stk[5u]  = 0u;                          /* X27  */
    p_stk[6u]  = 0u;                          /* X26  */
    p_stk[7u]  = 0u;                          /* X25  */
    p_stk[8u]  = 0u;                          /* X24  */
    p_stk[9u]  = 0u;                          /* X23  */
    p_stk[10u] = 0u;                          /* X22  */
    p_stk[11u] = 0u;                          /* X21  */
    p_stk[12u] = 0u;                          /* X20  */
    p_stk[13u] = 0u;                          /* X19  */
    p_stk[14u] = 0u;                          /* X18  */
    p_stk[15u] = 0u;                          /* X17  */
    p_stk[16u] = 0u;                          /* X16  */
    p_stk[17u] = 0u;                          /* X15  */
    p_stk[18u] = 0u;                          /* X14  */
    p_stk[19u] = 0u;                          /* X13  */
    p_stk[20u] = 0u;                          /* X12  */
    p_stk[21u] = 0u;                          /* X11  */
    p_stk[22u] = 0u;                          /* X10  */
    p_stk[23u] = 0u;                          /* X9   */
    p_stk[24u] = 0u;                          /* X8   */
    p_stk[25u] = 0u;                          /* X7   */
    p_stk[26u] = 0u;                          /* X6   */
    p_stk[27u] = 0u;                          /* X5   */
    p_stk[28u] = 0u;                          /* X4   */
    p_stk[29u] = 0u;                          /* X3   */
    p_stk[30u] = 0u;                          /* X2   */
    p_stk[31u] = 0u;                          /* X1   */
    p_stk[32u] = (OS_STK)p_arg;               /* X0   */
    p_stk[33u] = 0u;                          /* Padding */

    return p_stk;
}

void OS_CPU_ExceptHndlr(void)
{
    OSIntEnter();
    OSIntExit();
}
