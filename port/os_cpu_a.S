    .section .text
    .align  4

    /*
     * 中文：此檔案提供 uC/OS-II 在 ARMv8-A 上的組語移植，包含進入第一次任務、系統呼叫與中斷層級的
     *       情境切換，對應的堆疊框架需與 os_cpu_c.c 產生的初始堆疊內容一致。
     * English: Assembly port glue for uC/OS-II on ARMv8-A. It implements the initial task launch and both
     *          thread-level and interrupt-level context switches, matching the stack frame built in os_cpu_c.c.
     */

    .extern OSRunning
    .extern OSPrioCur
    .extern OSPrioHighRdy
    .extern OSTCBCur
    .extern OSTCBHighRdy

    .global OSStartHighRdy
    .global OSCtxSw
    .global OSIntCtxSw

    .equ CTX_SIZE,          (34*8)
    .equ CTX_SPSR,          0
    .equ CTX_ELR,           8
    .equ CTX_X30,           16

/*
 * 中文：SAVE_CONTEXT 會在切換前將所有通用暫存器與返回位址保存至 Task 堆疊，並額外保存 DAIF 與返回
 *       位址，以便 RESTORE 之後能夠恢復原本的執行狀態。
 * English: SAVE_CONTEXT stores general-purpose registers plus the task's return state (DAIF bits and ELR)
 *          onto the current stack to prepare for a context switch.
 */
.macro SAVE_CONTEXT
    sub     sp, sp, CTX_SIZE
    stp     x30, x29, [sp, #CTX_X30]
    stp     x28, x27, [sp, #(CTX_X30 + 16)]
    stp     x26, x25, [sp, #(CTX_X30 + 32)]
    stp     x24, x23, [sp, #(CTX_X30 + 48)]
    stp     x22, x21, [sp, #(CTX_X30 + 64)]
    stp     x20, x19, [sp, #(CTX_X30 + 80)]
    stp     x18, x17, [sp, #(CTX_X30 + 96)]
    stp     x16, x15, [sp, #(CTX_X30 + 112)]
    stp     x14, x13, [sp, #(CTX_X30 + 128)]
    stp     x12, x11, [sp, #(CTX_X30 + 144)]
    stp     x10, x9,  [sp, #(CTX_X30 + 160)]
    stp     x8,  x7,  [sp, #(CTX_X30 + 176)]
    stp     x6,  x5,  [sp, #(CTX_X30 + 192)]
    stp     x4,  x3,  [sp, #(CTX_X30 + 208)]
    stp     x2,  x1,  [sp, #(CTX_X30 + 224)]
    str     x0,         [sp, #(CTX_X30 + 240)]
    mrs     x0, daif
    and     x0, x0, #0x3C0
    mov     x1, #0x5
    orr     x0, x0, x1
    str     x0, [sp, #CTX_SPSR]
    mov     x0, x30
    str     x0, [sp, #CTX_ELR]
.endm

/*
 * 中文：RESTORE_AND_ERET 將堆疊上的暫存器與返回資訊取回，最後透過 ERET 回到任務。
 * English: RESTORE_AND_ERET reloads the saved registers and return state from the stack, finishing with ERET.
 */
.macro RESTORE_AND_ERET
    ldr     x9, [sp, #CTX_SPSR]
    msr     spsr_el1, x9
    ldr     x9, [sp, #CTX_ELR]
    msr     elr_el1, x9
    ldp     x30, x29, [sp, #CTX_X30]
    ldp     x28, x27, [sp, #(CTX_X30 + 16)]
    ldp     x26, x25, [sp, #(CTX_X30 + 32)]
    ldp     x24, x23, [sp, #(CTX_X30 + 48)]
    ldp     x22, x21, [sp, #(CTX_X30 + 64)]
    ldp     x20, x19, [sp, #(CTX_X30 + 80)]
    ldp     x18, x17, [sp, #(CTX_X30 + 96)]
    ldp     x16, x15, [sp, #(CTX_X30 + 112)]
    ldp     x14, x13, [sp, #(CTX_X30 + 128)]
    ldp     x12, x11, [sp, #(CTX_X30 + 144)]
    ldp     x10, x9,  [sp, #(CTX_X30 + 160)]
    ldp     x8,  x7,  [sp, #(CTX_X30 + 176)]
    ldp     x6,  x5,  [sp, #(CTX_X30 + 192)]
    ldp     x4,  x3,  [sp, #(CTX_X30 + 208)]
    ldp     x2,  x1,  [sp, #(CTX_X30 + 224)]
    ldr     x0,         [sp, #(CTX_X30 + 240)]
    add     sp, sp, CTX_SIZE
    eret
.endm

OSStartHighRdy:
    ldr     x0, =OSRunning
    mov     w1, #1
    strb    w1, [x0]

    ldr     x0, =OSPrioCur
    ldr     x1, =OSPrioHighRdy
    ldrb    w2, [x1]
    strb    w2, [x0]

    ldr     x0, =OSTCBCur
    ldr     x1, =OSTCBHighRdy
    ldr     x2, [x1]
    str     x2, [x0]

    ldr     x3, [x2]
    mov     sp, x3

    RESTORE_AND_ERET

OSCtxSw:
    SAVE_CONTEXT

    ldr     x0, =OSTCBCur
    ldr     x1, [x0]
    mov     x2, sp
    str     x2, [x1]

    ldr     x0, =OSPrioCur
    ldr     x1, =OSPrioHighRdy
    ldrb    w2, [x1]
    strb    w2, [x0]

    ldr     x0, =OSTCBCur
    ldr     x1, =OSTCBHighRdy
    ldr     x2, [x1]
    str     x2, [x0]

    ldr     x3, [x2]
    mov     sp, x3

    RESTORE_AND_ERET

OSIntCtxSw:
    b       OSCtxSw

    .global OS_CPU_ARM_ExceptIrqHndlr
OS_CPU_ARM_ExceptIrqHndlr:
    eret

    .end
