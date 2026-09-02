#ifndef APP_CFG_H
#define APP_CFG_H

#define APP_CFG_STARTUP_TASK_PRIO        4u
#define APP_CFG_STARTUP_TASK_STK_SIZE  1024u

/*
 * QEMU/KVM provides a ready-to-use PL011 UART. Reprogramming UARTCR from
 * an EL1 guest can raise a synchronous external abort on BPI-R4 KVM.
 * Set to 1 only when the platform requires UART reconfiguration.
 */
#ifndef APP_CFG_UART_REINIT
#define APP_CFG_UART_REINIT               0u
#endif

#define APP_TRACE_LEVEL                   0u
#define APP_TRACE(x)           ((void)0)
#define APP_TRACE_INFO(x)      ((void)0)
#define APP_TRACE_DBG(x)       ((void)0)

#endif
