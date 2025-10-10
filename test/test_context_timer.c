/*
 * Test Case 1: Context Switch and Timer Validation
 *
 * Purpose: Verify that uC/OS-II task context switching and timer interrupts work correctly
 *
 * Expected Behavior:
 * - Both Task A and Task B should run
 * - Task A should print counter every second (using OSTimeDlyHMSM)
 * - Task B should print network demo messages
 * - Timer interrupts should trigger regularly
 * - Context switching should occur without crashes
 *
 * Success Criteria:
 * - At least 3 successful task switches observed
 * - Timer interrupt count increases
 * - Both tasks execute and print messages
 * - No crashes or hangs for 10 seconds
 *
 * Run Command: make run
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

#define TASK_STACK_SIZE     512u
#define TEST_TASK_A_PRIO    3u
#define TEST_TASK_B_PRIO    4u
#define TEST_DURATION_SEC   8u

static OS_STK test_task_a_stack[TASK_STACK_SIZE];
static OS_STK test_task_b_stack[TASK_STACK_SIZE];

static volatile uint32_t task_a_switches = 0;
static volatile uint32_t task_b_switches = 0;
static volatile uint32_t timer_ticks = 0;

/* Test Task A - simulates CPU-bound task with delays */
static void test_task_a(void *p_arg)
{
    (void)p_arg;
    uint32_t local_counter = 0;

    uart_puts("[TEST-A] Task A started\n");

    for (;;) {
        task_a_switches++;

        uart_puts("[TEST-A] Iteration ");
        uart_write_dec(local_counter);
        uart_puts(" | Switches: ");
        uart_write_dec(task_a_switches);
        uart_puts(" | Timer ticks: ");
        uart_write_dec(OSTime);
        uart_puts("\n");

        local_counter++;

        /* Exit after test duration */
        if (local_counter >= TEST_DURATION_SEC) {
            uart_puts("\n[TEST-A] Test duration reached\n");
            break;
        }

        /* Delay 1 second to trigger context switch */
        OSTimeDlyHMSM(0, 0, 1, 0);
    }

    /* Report test results */
    uart_puts("\n========================================\n");
    uart_puts("TEST CASE 1: RESULTS\n");
    uart_puts("========================================\n");
    uart_puts("Task A switches: ");
    uart_write_dec(task_a_switches);
    uart_puts("\nTask B switches: ");
    uart_write_dec(task_b_switches);
    uart_puts("\nOS Timer ticks: ");
    uart_write_dec(OSTime);
    uart_puts("\n");

    /* Evaluate results */
    uint8_t test_passed = 1;

    if (task_a_switches < 3) {
        uart_puts("[FAIL] Task A insufficient switches (expected >= 3)\n");
        test_passed = 0;
    }

    if (task_b_switches < 3) {
        uart_puts("[FAIL] Task B insufficient switches (expected >= 3)\n");
        test_passed = 0;
    }

    if (OSTime < (TEST_DURATION_SEC * 10)) {  /* At least some timer ticks */
        uart_puts("[FAIL] Timer ticks too low (expected >= ");
        uart_write_dec(TEST_DURATION_SEC * 10);
        uart_puts(")\n");
        test_passed = 0;
    }

    if (test_passed) {
        uart_puts("\n[PASS] ✓ Context switch and timer test PASSED\n");
    } else {
        uart_puts("\n[FAIL] ✗ Context switch and timer test FAILED\n");
    }
    uart_puts("========================================\n\n");

    /* Infinite loop after test */
    for (;;) {
        OSTimeDlyHMSM(0, 0, 10, 0);
    }
}

/* Test Task B - simulates I/O-bound task */
static void test_task_b(void *p_arg)
{
    (void)p_arg;
    uint32_t local_counter = 0;

    uart_puts("[TEST-B] Task B started\n");

    for (;;) {
        task_b_switches++;

        uart_puts("[TEST-B] Iteration ");
        uart_write_dec(local_counter);
        uart_puts(" | Switches: ");
        uart_write_dec(task_b_switches);
        uart_puts("\n");

        local_counter++;

        /* Task B runs faster to test preemption */
        OSTimeDlyHMSM(0, 0, 0, 500);  /* 500ms delay */

        /* Task A controls test duration */
        if (local_counter >= (TEST_DURATION_SEC * 2)) {
            break;
        }
    }

    /* Wait for Task A to finish */
    for (;;) {
        OSTimeDlyHMSM(0, 0, 10, 0);
    }
}

int main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("TEST CASE 1: Context Switch & Timer\n");
    uart_puts("========================================\n");
    uart_puts("[BOOT] Initializing test environment\n");

    uart_init();
    gic_init();
    uart_puts("[BOOT] GICv3 initialized\n");

    /* Configure timer access */
    uint64_t val = 0xd6;
    __asm__ volatile("msr cntkctl_el1, %0" :: "r"(val));

    OSInit();
    uart_puts("[BOOT] uC/OS-II initialized\n");

    /* Create test tasks */
    INT8U err;

    err = OSTaskCreate(test_task_a,
                       NULL,
                       &test_task_a_stack[TASK_STACK_SIZE - 1u],
                       TEST_TASK_A_PRIO);
    if (err != OS_ERR_NONE) {
        uart_puts("[ERROR] Failed to create Task A\n");
        return 1;
    }
    uart_puts("[BOOT] Test Task A created\n");

    err = OSTaskCreate(test_task_b,
                       NULL,
                       &test_task_b_stack[TASK_STACK_SIZE - 1u],
                       TEST_TASK_B_PRIO);
    if (err != OS_ERR_NONE) {
        uart_puts("[ERROR] Failed to create Task B\n");
        return 1;
    }
    uart_puts("[BOOT] Test Task B created\n");

    /* Initialize timer interrupt */
    BSP_IntVectSet(27u, 0u, 0u, BSP_OS_TmrTickHandler);
    BSP_IntSrcEn(27u);
    BSP_OS_TmrTickInit(1000u);  /* 1000 Hz tick rate */
    uart_puts("[BOOT] Timer interrupt initialized\n");

    /* Enable IRQs */
    __asm__ volatile("msr daifclr, #0x2");
    uart_puts("[BOOT] IRQs enabled\n");

    uart_puts("[BOOT] Starting test...\n");
    uart_puts("========================================\n\n");

    OSStart();

    /* Should never reach here */
    uart_puts("[ERROR] Returned from OSStart()!\n");
    while (1) { }
}
