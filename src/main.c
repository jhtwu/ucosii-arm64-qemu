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

#include "uart.h"

#define BUSY_DELAY       (2000000u)
#define TASK_STACK_SIZE  512u
#define TASK_A_PRIO         3u
#define TASK_B_PRIO         4u

static OS_STK task_a_stack[TASK_STACK_SIZE];
static OS_STK task_b_stack[TASK_STACK_SIZE];

static void task_a(void *p_arg)
{
    (void)p_arg;

    uint32_t counter = 0u;

    /*
     * 中文：Task A 會列印計數值，執行短暫忙等待，接著喚醒 Task B 並自行掛起。
     * English: Task A prints its counter, performs a short busy wait, wakes Task B, then suspends itself.
     */
    for (;;) {
        uart_putc('A');
        uart_putc(':');
        uart_putc(' ');
        uart_write_dec(counter++);
        uart_putc('\n');

        for (volatile uint64_t i = 0; i < BUSY_DELAY; ++i) {
            __asm__ volatile("nop");
        }

        INT8U err;

        OSSchedLock();
        err = OSTaskResume(TASK_B_PRIO);
        if (err != OS_ERR_NONE) {
            uart_puts("[ERROR] resume B = ");
            uart_write_dec(err);
            uart_putc('\n');
        }
        err = OSTaskSuspend(OS_PRIO_SELF);
        OSSchedUnlock();
        if (err != OS_ERR_NONE) {
            uart_puts("[ERROR] suspend A = ");
            uart_write_dec(err);
            uart_putc('\n');
        }
    }
}

static void task_b(void *p_arg)
{
    (void)p_arg;

    uint32_t counter = 0u;

    /*
     * 中文：Task B 的行為與 Task A 對稱，同樣列印後喚醒對方並自行掛起，形成交錯輸出。
     * English: Task B mirrors Task A—printing, delaying, waking the peer, and suspending, producing the alternation.
     */
    for (;;) {
        uart_putc('B');
        uart_putc(':');
        uart_putc(' ');
        uart_write_dec(counter++);
        uart_putc('\n');

        for (volatile uint64_t i = 0; i < BUSY_DELAY; ++i) {
            __asm__ volatile("nop");
        }

        INT8U err;

        OSSchedLock();
        err = OSTaskResume(TASK_A_PRIO);
        if (err != OS_ERR_NONE) {
            uart_puts("[ERROR] resume A = ");
            uart_write_dec(err);
            uart_putc('\n');
        }
        err = OSTaskSuspend(OS_PRIO_SELF);
        OSSchedUnlock();
        if (err != OS_ERR_NONE) {
            uart_puts("[ERROR] suspend B = ");
            uart_write_dec(err);
            uart_putc('\n');
        }
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
    uart_puts("[BOOT] Task B suspend err = ");
    uart_write_dec(err);
    uart_puts("\n");

    uart_puts("[BOOT] Scheduler starting\n");

    OSStart();

    uart_puts("[ERROR] OSStart returned\n");
    for (;;) {
    }
}
