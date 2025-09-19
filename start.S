    .section .text.boot
    .align  2
    .global _start

    .extern main
    .extern OS_CPU_ARM_ExceptIrqHndlr

_start:
        msr     daifset, #0xf
        mrs     x0, CurrentEL
        lsr     x0, x0, #2
        cmp     x0, #3
        b.eq    from_el3
        cmp     x0, #2
        b.eq    from_el2
        b       el1_entry

from_el3:
        ldr     x1, =_stack_top
        msr     sp_el1, x1
        mov     x1, #0x3c5
        msr     spsr_el3, x1
        adr     x1, el1_entry
        msr     elr_el3, x1
        mov     x1, #0x33
        msr     scr_el3, x1
        mov     x1, #0
        msr     hcr_el2, x1
        msr     cptr_el3, x1
        msr     cpacr_el1, x1
        eret

from_el2:
        ldr     x1, =_stack_top
        msr     sp_el1, x1
        mov     x1, #0x3c5
        msr     spsr_el2, x1
        adr     x1, el1_entry
        msr     elr_el2, x1
        mov     x1, #0
        msr     hcr_el2, x1
        msr     cptr_el2, x1
        mov     x1, #3
        msr     cnthctl_el2, x1
        msr     cntvoff_el2, xzr
        eret

el1_entry:
        msr     spsel, #1
        ldr     x0, =_stack_top
        mov     sp, x0
        mov     x0, #3
        msr     cntkctl_el1, x0
        bl      zero_bss
        ldr     x0, =vector_table
        msr     vbar_el1, x0
        isb
        bl      main
hang:
        wfi
        b       hang

    .align 3
zero_bss:
        ldr     x0, =__bss_start
        ldr     x1, =__bss_end
        cmp     x0, x1
        b.eq    zero_done
zero_loop:
        str     xzr, [x0], #8
        cmp     x0, x1
        b.lo    zero_loop
zero_done:
        ret

    .align 11
    .global vector_table
vector_table:
        /* Current EL with SP0 */
        b       default_handler
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        /* Current EL with SPx */
        b       default_handler
        nop
        nop
        nop
        b       OS_CPU_ARM_ExceptIrqHndlr
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        /* Lower EL using AArch64 */
        b       default_handler
        nop
        nop
        nop
        b       OS_CPU_ARM_ExceptIrqHndlr
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        /* Lower EL using AArch32 */
        b       default_handler
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop
        b       default_handler
        nop
        nop
        nop

    .align 2
default_handler:
        b       default_handler

    .section .bss
    .align  3

    .section .rodata

    .section .text

    .section .data

    .section .text.boot
