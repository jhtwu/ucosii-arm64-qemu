#ifndef OS_CFG_H
#define OS_CFG_H

/* ---------------------- MISCELLANEOUS ----------------------- */
#define OS_APP_HOOKS_EN           0u
#define OS_ARG_CHK_EN             1u
#define OS_CPU_HOOKS_EN           1u

#define OS_DEBUG_EN               0u

#define OS_EVENT_MULTI_EN         0u
#define OS_EVENT_NAME_EN          0u

#define OS_LOWEST_PRIO           10u
#define OS_MAX_EVENTS             2u
#define OS_MAX_FLAGS              1u
#define OS_MAX_MEM_PART           0u
#define OS_MAX_QS                 0u
#define OS_MAX_TASKS              8u

#define OS_SCHED_LOCK_EN          1u

#define OS_TICK_STEP_EN           0u
#define OS_TICKS_PER_SEC        1000u   /* Match timer frequency: 1000 Hz */

#define OS_TLS_TBL_SIZE           0u

/* --------------------- TASK STACK SIZE ---------------------- */
#define OS_TASK_TMR_STK_SIZE    128u
#define OS_TASK_STAT_STK_SIZE   128u
#define OS_TASK_IDLE_STK_SIZE   256u

/* --------------------- TASK MANAGEMENT ---------------------- */
#define OS_TASK_CHANGE_PRIO_EN    0u
#define OS_TASK_CREATE_EN         1u
#define OS_TASK_CREATE_EXT_EN     0u
#define OS_TASK_DEL_EN            0u
#define OS_TASK_NAME_EN           1u
#define OS_TASK_PROFILE_EN        1u
#define OS_TASK_QUERY_EN          0u
#define OS_TASK_REG_TBL_SIZE      0u
#define OS_TASK_STAT_EN           0u
#define OS_TASK_STAT_STK_CHK_EN   0u
#define OS_TASK_SUSPEND_EN        1u
#define OS_TASK_SW_HOOK_EN        0u

/* ----------------------- EVENT FLAGS ------------------------ */
#define OS_FLAG_EN                0u
#define OS_FLAG_ACCEPT_EN         0u
#define OS_FLAG_DEL_EN            0u
#define OS_FLAG_NAME_EN           0u
#define OS_FLAG_QUERY_EN          0u
#define OS_FLAG_WAIT_CLR_EN       0u
#define OS_FLAGS_NBITS           16u

/* -------------------- MESSAGE MAILBOXES --------------------- */
#define OS_MBOX_EN                0u
#define OS_MBOX_ACCEPT_EN         0u
#define OS_MBOX_DEL_EN            0u
#define OS_MBOX_PEND_ABORT_EN     0u
#define OS_MBOX_POST_EN           0u
#define OS_MBOX_POST_OPT_EN       0u
#define OS_MBOX_QUERY_EN          0u

/* --------------------- MEMORY MANAGEMENT -------------------- */
#define OS_MEM_EN                 0u
#define OS_MEM_NAME_EN            0u
#define OS_MEM_QUERY_EN           0u

/* --------------- MUTUAL EXCLUSION SEMAPHORES ---------------- */
#define OS_MUTEX_EN               0u
#define OS_MUTEX_ACCEPT_EN        0u
#define OS_MUTEX_DEL_EN           0u
#define OS_MUTEX_QUERY_EN         0u

/* ---------------------- MESSAGE QUEUES ---------------------- */
#define OS_Q_EN                   0u
#define OS_Q_ACCEPT_EN            0u
#define OS_Q_DEL_EN               0u
#define OS_Q_FLUSH_EN             0u
#define OS_Q_PEND_ABORT_EN        0u
#define OS_Q_POST_EN              0u
#define OS_Q_POST_FRONT_EN        0u
#define OS_Q_POST_OPT_EN          0u
#define OS_Q_QUERY_EN             0u

/* ----------------------- SEMAPHORES ------------------------- */
#define OS_SEM_EN                 0u
#define OS_SEM_ACCEPT_EN          0u
#define OS_SEM_DEL_EN             0u
#define OS_SEM_PEND_ABORT_EN      0u
#define OS_SEM_QUERY_EN           0u
#define OS_SEM_SET_EN             0u

/* ------------------------- TIMERS --------------------------- */
#define OS_TMR_EN                 0u
#define OS_TMR_CFG_MAX            0u
#define OS_TMR_CFG_NAME_EN        0u
#define OS_TMR_CFG_WHEEL_SIZE     0u
#define OS_TMR_CFG_TICKS_PER_SEC  0u

/* ------------------------- TIME ----------------------------- */
#define OS_TIME_DLY_HMSM_EN       1u
#define OS_TIME_DLY_RESUME_EN     0u
#define OS_TIME_GET_SET_EN        1u
#define OS_TIME_TICK_HOOK_EN      0u

#endif
