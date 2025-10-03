/*
 * 中文：這個檔案透過兩個 uC/OS-II 任務（Task A 與 Task B）示範在 ARMv8-A 平台上的情境切換，
 *       兩個任務輪流列印訊息，並透過 uC/OS-II 的排程機制完成協調。啟動流程包含 UART 初始、
 *       作業系統初始化、建立任務與啟動排程器。
 * English: This file demonstrates a dual-task uC/OS-II context switch demo on ARMv8-A. Tasks A and B
 *          alternately print messages while coordinating via the scheduler primitives. The boot flow
 *          covers UART bring-up, OS init, task creation, and starting the kernel scheduler.
 */

#include <stddef.h>
#include <stdint.h>

#include <ucos_ii.h>

#include "gic.h"
#include "uart.h"
#include "timer.h"
#include "mmio.h"
#include "bsp_int.h"
#include "bsp_os.h"

#define BUSY_DELAY          (2000u)
#define TASK_STACK_SIZE     512u
#define TASK_A_PRIO            3u
#define TASK_B_PRIO            4u
#define ENABLE_TASK_LOG        1
#define TIMER_INTERRUPT_ID    27u

static OS_STK task_a_stack[TASK_STACK_SIZE];
static OS_STK task_b_stack[TASK_STACK_SIZE];

static void task_a(void *p_arg)
{
    (void)p_arg;

    uart_puts("[TASK A] Starting Task A\n");
    
    /*
     * 中文：初始化timer在第一個任務中，確保多任務已經開始
     * English: Initialize timer in first task to ensure multitasking has started
     */
    
    /* Configure CNTKCTL_EL1 like armv8 project */
    uart_puts("[TASK A] Configuring CNTKCTL_EL1...\n");
    uint64_t val = 0x2;
    __asm__ volatile("msr cntkctl_el1, %0" :: "r"(val));
    val = 0xd6;
    __asm__ volatile("msr cntkctl_el1, %0" :: "r"(val));
    uart_puts("[TASK A] CNTKCTL_EL1 configured\n");
    
    /* Register BSP interrupt handler for timer */
    uart_puts("[TASK A] Registering timer interrupt handler...\n");
    BSP_IntVectSet(27u, 0u, 0u, BSP_OS_TmrTickHandler);
    BSP_IntSrcEn(27u);
    uart_puts("[TASK A] Timer interrupt handler registered\n");
    
    /* Initialize timer using BSP OS function like armv8 */
    uart_puts("[TASK A] Initializing BSP OS timer...\n");
    BSP_OS_TmrTickInit(1000u);  /* 1000 Hz like armv8 */
    uart_puts("[TASK A] BSP OS timer initialized\n");
    
    /* Resume Task B to enable multitasking */
    uart_puts("[TASK A] Resuming Task B for timer-based multitasking\n");
    INT8U resume_err = OSTaskResume(TASK_B_PRIO);
    uart_puts("[TASK A] Task B resume result: ");
    uart_write_dec(resume_err);
    uart_putc('\n');
    
    /* Test: Force a short timer interrupt to verify interrupt system works */
    uart_puts("[TASK A] Testing software interrupt (SGI) first\n");
    
    /* Test if our interrupt handling works by generating a software interrupt */
    uart_puts("[TASK A] Generating SGI interrupt 1\n");
    
    /* Generate SGI 1 to self */
    uint64_t sgi_val = (0ull << 24) | (1ull << 16) | 1u;  /* SGI 1 */
    __asm__ volatile("msr ICC_SGI1R_EL1, %0" :: "r"(sgi_val));
    
    uart_puts("[TASK A] SGI sent, waiting for interrupt...\n");
    for (volatile int i = 0; i < 1000000; i++);
    
    uart_puts("[TASK A] After SGI test\n");
    
    uart_puts("[TASK A] Now testing timer interrupt\n");
    
    /* Force an immediate virtual timer interrupt by setting tval to 1 */
    __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(1u));
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1u));  /* Enable */
    
    uart_puts("[TASK A] Timer interrupt should fire immediately\n");
    for (volatile int i = 0; i < 10000000; i++);
    
    uart_puts("[TASK A] After timer test\n");

    uint32_t counter = 0u;

    /*
     * Test basic task execution with busy loops (no OS delays)
     */
    for (;;) {
        uart_puts("[TASK A] Running - Counter: ");
        uart_write_dec(counter);
        uart_puts(" (busy loop)\n");
        ++counter;

        /* Busy wait instead of OS delay to see if timer interrupts cause task switching */
        for (volatile int i = 0; i < 5000000; i++);
    }
}

static void task_b(void *p_arg)
{
    (void)p_arg;
    uint32_t counter = 0u;

    uart_puts("[TASK B] Starting Task B\n");

    /*
     * Main task loop - print every second with timer-based context switching
     */
    for (;;) {
        uart_puts("[TASK B] Running - Counter: ");
        uart_write_dec(counter);
        uart_puts(" (0.5s delay)\n");
        ++counter;

        /* Delay for 1 second - this allows timer interrupt to switch back to Task A */
        OSTimeDlyHMSM(0, 0, 1, 0);
    }
}

int main(void)
{
    /*
     * 中文：系統啟動步驟：初始化 UART、呼叫 OSInit、建立兩個任務並預先暫停 Task B，然後啟動排程器。
     * English: Boot steps: initialise UART, call OSInit, spawn both tasks with Task B initially suspended, then start the scheduler.
     */
    uart_puts("[BOOT] main enter\n");
    uart_init();
    uart_puts("\n[BOOT] uC/OS-II ARMv8 demo starting\n");

    uart_puts("[BOOT] Initialising GICv3\n");
    gic_init();
    uart_puts("[BOOT] GIC initialized\n");
    
    /* Initialize BSP interrupt system */
    uart_puts("[BOOT] Initializing BSP interrupt system\n");

    OSInit();

    INT8U err;

    err = OSTaskCreate(task_a,
                       NULL,
                       &task_a_stack[TASK_STACK_SIZE - 1u],
                       TASK_A_PRIO);
    uart_puts("[BOOT] Task A create err = ");
    uart_write_dec(err);
    uart_puts("\n");

    err = OSTaskCreate(task_b,
                       NULL,
                       &task_b_stack[TASK_STACK_SIZE - 1u],
                       TASK_B_PRIO);
    uart_puts("[BOOT] Task B create err = ");
    uart_write_dec(err);
    uart_puts("\n");

    if (err != OS_ERR_NONE) {
        uart_puts("[ERROR] Failed to create tasks\n");
        return 1;
    }

    err = OSTaskSuspend(TASK_B_PRIO);
    uart_puts("[BOOT] Task B suspended initially, err = ");
    uart_write_dec(err);
    uart_puts("\n");

    /* Enable IRQs and test timer interrupt */
    uart_puts("[BOOT] Current DAIF = ");
    uint64_t daif_val;
    __asm__ volatile("mrs %0, DAIF" : "=r"(daif_val));
    uart_write_hex((uint32_t)daif_val);
    uart_putc('\n');
    
    uart_puts("[BOOT] Enabling IRQs for timer interrupt test\n");
    __asm__ volatile("msr daifclr, #0x2");
    
    uart_puts("[BOOT] IRQs enabled - timer should now work\n");
    
    uart_puts("[BOOT] Starting scheduler...\n");
    OSStart();
    
    /* Should never reach here */
    uart_puts("[BOOT] ERROR: Returned from OSStart()!\n");
    while (1) {
        /* Hang */
    }
}
