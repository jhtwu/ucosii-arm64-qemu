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
    uint32_t counter = 0u;

    uart_puts("[TASK A] Starting\n");

    /* Configure CNTKCTL_EL1 for timer access */
    uint64_t val = 0x2;
    __asm__ volatile("msr cntkctl_el1, %0" :: "r"(val));
    val = 0xd6;
    __asm__ volatile("msr cntkctl_el1, %0" :: "r"(val));

    /* Register and initialize timer */
    BSP_IntVectSet(27u, 0u, 0u, BSP_OS_TmrTickHandler);
    BSP_IntSrcEn(27u);
    BSP_OS_TmrTickInit(1000u);

    uart_puts("[TASK A] Timer initialized, starting loop\n\n");

    /* Main loop - print every second */
    for (;;) {
        uart_puts("[TASK A] Counter: ");
        uart_write_dec(counter++);
        uart_puts("\n");

        /* Delay 1 second */
        OSTimeDlyHMSM(0, 0, 1, 0);
    }
}

static void task_b(void *p_arg)
{
    (void)p_arg;
    uint32_t counter = 0u;

    uart_puts("[TASK B] Starting\n\n");

    /* Main loop - print every second */
    for (;;) {
        uart_puts("[TASK B] Counter: ");
        uart_write_dec(counter++);
        uart_puts("\n");

        /* Delay 1 second */
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

    /* Don't suspend Task B - let both tasks run */
    /* err = OSTaskSuspend(TASK_B_PRIO); */
    uart_puts("[BOOT] Task B NOT suspended - both tasks will run\n");

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
