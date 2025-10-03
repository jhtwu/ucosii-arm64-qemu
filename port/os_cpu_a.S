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
    .extern irq_dispatch

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
.endm

/*
 * 中文：SAVE_SPSR_FROM_IRQ 從 SPSR_EL1 保存（用於中斷上下文）
 * English: SAVE_SPSR_FROM_IRQ saves from SPSR_EL1 (for interrupt context)
 */
.macro SAVE_SPSR_FROM_IRQ
    /* Save SPSR_EL1 (contains original PSTATE before IRQ) */
    mrs     x0, spsr_el1
    str     x0, [sp, #CTX_SPSR]
    /* Save ELR_EL1 (return address) */
    mrs     x0, elr_el1
    str     x0, [sp, #CTX_ELR]
.endm

/*
 * 中文：SAVE_SPSR_FOR_TASK_SW 設定 SPSR 為任務層面值（用於主動切換）
 * English: SAVE_SPSR_FOR_TASK_SW sets SPSR for task-level (for voluntary switch)
 */
.macro SAVE_SPSR_FOR_TASK_SW
    /* Set SPSR for EL1h mode with IRQs enabled */
    mov     x0, #0x00000205              /* EL1h, IRQ/FIQ unmasked */
    str     x0, [sp, #CTX_SPSR]
    /* Save return address (LR) */
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
    SAVE_SPSR_FOR_TASK_SW       /* Manual SPSR setup for task-level switch */

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

/*
 * 中文：OSIntCtxSw 在中斷結束時從 OSIntExit 調用，如果需要切換到更高優先級的任務。
 *       關鍵：只更新 TCB 指針，不做實際的堆疊切換和恢復（這在 IRQ handler 中處理）
 * English: OSIntCtxSw is called from OSIntExit when an interrupt completes and a higher priority
 *          task is ready. Key: Only update TCB pointers, actual stack switch and restore happens
 *          in the IRQ handler after OSIntExit returns.
 */
OSIntCtxSw:
    /* 更新 OSPrioCur = OSPrioHighRdy */
    ldr     x0, =OSPrioCur
    ldr     x1, =OSPrioHighRdy
    ldrb    w2, [x1]
    strb    w2, [x0]

    /* 更新 OSTCBCur = OSTCBHighRdy */
    ldr     x0, =OSTCBCur
    ldr     x1, =OSTCBHighRdy
    ldr     x2, [x1]
    str     x2, [x0]

    /* 返回到 IRQ handler，它會切換堆疊並恢復新任務 */
    /* Return to IRQ handler, which will switch stack and restore the new task */
    ret

/*
 * 中文：ARMv8-A IRQ 異常處理程序 - 正確實現 uC/OS-II 中斷處理
 * English: ARMv8-A IRQ Exception Handler - Proper uC/OS-II interrupt handling
 *
 * 實現要點 (Key Implementation Points):
 * 1. 管理 OSIntNesting 計數器
 * 2. 第一層中斷：保存任務上下文並切換到異常堆疊
 * 3. 調用中斷分派函數 (irq_dispatch)
 * 4. 調用 OSIntExit() 進行任務重新排程
 * 5. 恢復任務上下文 (可能切換到新任務)
 */
    .global OS_CPU_ARM_ExceptIrqHndlr
OS_CPU_ARM_ExceptIrqHndlr:
    /* 保存所有寄存器到當前堆疊 (任務或異常堆疊) */
    /* Save all registers to current stack (task or exception stack) */
    SAVE_CONTEXT
    SAVE_SPSR_FROM_IRQ          /* Save actual SPSR_EL1 from IRQ exception */

    /* 增加中斷嵌套計數器 OSIntNesting++ */
    /* Increment interrupt nesting counter */
    ldr     x0, =OSIntNesting
    ldrb    w1, [x0]
    add     w1, w1, #1
    strb    w1, [x0]

    /* 檢查是否為第一層中斷 (OSIntNesting == 1) */
    /* Check if this is the first-level interrupt */
    cmp     w1, #1
    bne     OS_CPU_ARM_ExceptIrqHndlr_NestedInt

    /* 第一層中斷：保存任務堆疊指針並切換到異常堆疊 */
    /* First-level interrupt: save task stack pointer and switch to exception stack */
    ldr     x0, =OSTCBCur           /* Get current task TCB */
    ldr     x1, [x0]
    mov     x2, sp                   /* Save current SP (task stack) */
    str     x2, [x1]

    /* 切換到異常堆疊 */
    /* Switch to exception stack */
    ldr     x0, =OS_CPU_ExceptStkBase
    ldr     x1, [x0]
    mov     sp, x1

OS_CPU_ARM_ExceptIrqHndlr_NestedInt:
    /* 調用中斷分派函數 */
    /* Call interrupt dispatch function */
    bl      irq_dispatch

    /* 調用 OSIntExit - 處理任務排程 */
    /* Call OSIntExit - handles task scheduling */
    /* 注意：如果需要切換任務，OSIntExit 會調用 OSIntCtxSw，然後 OSIntCtxSw 會切換堆疊並 ERET */
    /* Note: If task switch is needed, OSIntExit calls OSIntCtxSw, which switches stack and ERETurns */
    bl      OSIntExit

    /* OSIntExit 返回後，恢復任務堆疊 (可能已經被 OSIntCtxSw 切換到新任務) */
    /* After OSIntExit returns, restore task stack (may have been switched by OSIntCtxSw) */
    ldr     x0, =OSTCBCur
    ldr     x1, [x0]
    ldr     x2, [x1]
    mov     sp, x2

    /* 恢復任務上下文並返回 */
    /* Restore task context and return */
    RESTORE_AND_ERET

    .end
