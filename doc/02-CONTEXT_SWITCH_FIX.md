# Context Switch 修復說明

## 問題描述

任務調用 `OSTimeDlyHMSM()` 進入 delay 後無法被喚醒，timer interrupt 停止觸發，系統hang住。

## 根本原因

### 問題2: 任務層面 Context Switch 的 SPSR 設置錯誤

在 `OSCtxSw()` 中使用 `SAVE_CONTEXT` 宏時，錯誤地從 `SPSR_EL1` 讀取：

```assembly
/* 錯誤的實現 */
OSCtxSw:
    SAVE_CONTEXT
    /* ... */

/* SAVE_CONTEXT 包含 */
mrs     x0, spsr_el1    /* 在非異常上下文中，SPSR_EL1 未定義! */
str     x0, [sp, #CTX_SPSR]
```

**問題**: `OSCtxSw()` 是從**任務層面**調用的（通過 `OS_TASK_SW()` 宏），**不是異常**：
- 沒有異常發生，`SPSR_EL1` **沒有被硬體設置**
- 讀取到的是**未定義/舊的值**
- 可能包含 IRQ mask bit
- ERET 時恢復錯誤的 PSTATE，**IRQ 被 mask**
- Timer interrupt 無法觸發

### 正確的修復

為任務層面的 context switch **手動設置** SPSR：

```assembly
.macro SAVE_SPSR_FOR_TASK_SW
    /* 手動設置 SPSR 為 EL1h, IRQ/FIQ unmasked */
    mov     x0, #0x00000205
    /*
     * 0x205 = 0b0000_0010_0000_0101
     *   bit [3:0] = 0101 (M[3:0]) = EL1h (use SP_EL1)
     *   bit [6]   = 0    (F) = FIQ unmasked
     *   bit [7]   = 0    (I) = IRQ unmasked
     *   bit [8]   = 0    (A) = SError unmasked
     *   bit [9]   = 1    (D) = Debug masked
     */
    str     x0, [sp, #CTX_SPSR]
    /* 保存返回地址 (LR) */
    mov     x0, x30
    str     x0, [sp, #CTX_ELR]
.endm

OSCtxSw:
    SAVE_CONTEXT
    SAVE_SPSR_FOR_TASK_SW    /* 手動設置 SPSR */
    /* ... 切換任務 ... */
    RESTORE_AND_ERET
```

## Context Switch 的兩種情況

### 情況1: 中斷上下文切換 (OSIntCtxSw)

**觸發時機**: Timer interrupt handler 中，`OSIntExit()` 發現有更高優先級任務就緒

**調用路徑**:
```
Timer IRQ 觸發
    ↓
OS_CPU_ARM_ExceptIrqHndlr
    ↓
OSIntExit()
    ↓
OSIntCtxSw()  ← 如果需要切換
    ↓
返回 IRQ handler
    ↓
恢復新任務堆疊
    ↓
ERET 到新任務
```

**SPSR 來源**: `SPSR_EL1` (硬體自動設置)

**實現**:
```assembly
OSIntCtxSw:
    /* 只更新 TCB 指針 */
    ldr     x0, =OSPrioCur
    ldr     x1, =OSPrioHighRdy
    ldrb    w2, [x1]
    strb    w2, [x0]

    ldr     x0, =OSTCBCur
    ldr     x1, =OSTCBHighRdy
    ldr     x2, [x1]
    str     x2, [x0]

    ret    /* 返回到 IRQ handler，由它切換堆疊 */
```

### 情況2: 任務層面切換 (OSCtxSw)

**觸發時機**: 任務主動調用導致阻塞的函數

**調用路徑**:
```
Task 調用 OSTimeDlyHMSM()
    ↓
OSTimeDly()
    ↓
OSSched()
    ↓
OS_TASK_SW() → OSCtxSw()  ← 如果需要切換
    ↓
ERET 到新任務
```

**SPSR 來源**: **手動設置** (無硬體異常)

**實現**:
```assembly
OSCtxSw:
    SAVE_CONTEXT
    SAVE_SPSR_FOR_TASK_SW    /* 手動設置 SPSR = 0x205 */

    /* 保存當前任務 SP */
    ldr     x0, =OSTCBCur
    ldr     x1, [x0]
    mov     x2, sp
    str     x2, [x1]

    /* 更新優先級 */
    ldr     x0, =OSPrioCur
    ldr     x1, =OSPrioHighRdy
    ldrb    w2, [x1]
    strb    w2, [x0]

    /* 更新 TCB */
    ldr     x0, =OSTCBCur
    ldr     x1, =OSTCBHighRdy
    ldr     x2, [x1]
    str     x2, [x0]

    /* 切換到新任務堆疊 */
    ldr     x3, [x2]
    mov     sp, x3

    RESTORE_AND_ERET
```

## SPSR 值詳解

### 0x205 的含義

```
bit position:  9 8 7 6 5 4 3 2 1 0
binary:        0 0 0 0 0 0 0 1 0 1
hex:           0   x   2   0   5

bit [3:0] = M[3:0] = 0101b = 5
    → Exception level: EL1h (EL1 using SP_EL1)

bit [6] = F (FIQ mask)
    → 0 = FIQ enabled (unmasked)

bit [7] = I (IRQ mask)  ← 關鍵!
    → 0 = IRQ enabled (unmasked)

bit [8] = A (SError mask)
    → 0 = SError enabled (unmasked)

bit [9] = D (Debug mask)
    → 1 = Debug masked
```

**關鍵**: bit [7] = 0 確保 **IRQ enabled**，timer interrupt 可以觸發！

### 為什麼需要 EL1h?

```
EL1h (0x5): EL1 using SP_EL1  ← 正確
EL1t (0x4): EL1 using SP_EL0  ← 錯誤
```

uC/OS-II 任務運行在 EL1，並且使用 `SP_EL1`（不是 `SP_EL0`），所以必須設置為 **EL1h mode**。

## Task Delay 完整流程

### 1. 任務調用 Delay

```c
void task_a(void *p_arg)
{
    for (;;) {
        uart_puts("[TASK A] Counter: 0\n");
        OSTimeDlyHMSM(0, 0, 1, 0);  // Delay 1 秒
        uart_puts("[TASK A] Counter: 1\n");
    }
}
```

### 2. OSTimeDlyHMSM 內部

```c
INT8U OSTimeDlyHMSM(INT8U hours, INT8U minutes, INT8U seconds, INT16U ms)
{
    // 計算總 ticks
    INT32U ticks = seconds * OS_TICKS_PER_SEC;  // 1 * 1000 = 1000 ticks

    // 調用 OSTimeDly
    OSTimeDly(ticks);
}

void OSTimeDly(INT32U ticks)
{
    OSTCBCur->OSTCBDly = ticks;     // 設置 delay counter
    OSTCBCur->OSTCBStat |= OS_STAT_DELAY;
    OSSched();                      // 重新調度
}
```

### 3. 調度器切換任務

```c
void OSSched(void)
{
    // 找到最高優先級就緒任務
    OSPrioHighRdy = /* 計算 */;
    OSTCBHighRdy = /* 獲取 TCB */;

    if (OSPrioHighRdy != OSPrioCur) {
        OS_TASK_SW();  // → OSCtxSw()
    }
}
```

### 4. OSCtxSw 執行

```
當前: Task A (OSTCBCur, delay=1000, NOT ready)
    ↓
OSCtxSw():
    SAVE_CONTEXT               // 保存 Task A 所有寄存器
    SAVE_SPSR_FOR_TASK_SW      // 設置 SPSR = 0x205 (IRQ enabled!)
    保存 Task A SP
    切換到 Task B
    載入 Task B SP
    RESTORE_AND_ERET           // 恢復 Task B，PSTATE.I = 0
    ↓
Task B 開始運行 (IRQ enabled!)
```

### 5. Timer Tick 喚醒

```
每個 timer tick (1ms):
    ↓
OSTimeTick():
    for (所有任務) {
        if (TCB->OSTCBDly > 0) {
            TCB->OSTCBDly--;           // Task A: 1000 → 999 → 998 → ...
            if (TCB->OSTCBDly == 0) {
                TCB->OSTCBStat &= ~OS_STAT_DELAY;  // Task A ready!
            }
        }
    }
    ↓
1000 ticks 後 (1 秒):
    Task A delay counter = 0
    Task A 變成 ready
    ↓
OSIntExit() 發現 Task A 優先級更高
    → OSIntCtxSw()
    → 切換回 Task A
    ↓
Task A 從 OSTimeDlyHMSM() 返回
uart_puts("[TASK A] Counter: 1\n");  // 繼續執行!
```

## 兩種 SPSR 設置的對比

| 特性 | IRQ 上下文 | Task 上下文 |
|------|-----------|------------|
| 函數 | `OSIntCtxSw` | `OSCtxSw` |
| 觸發方式 | 硬體異常 | 軟體調用 |
| SPSR 來源 | `SPSR_EL1` (硬體) | 手動設置 |
| SPSR 值 | 異常前的 PSTATE | `0x205` |
| IRQ 狀態 | 保留原狀態 | 強制 enabled |
| 宏 | `SAVE_SPSR_FROM_IRQ` | `SAVE_SPSR_FOR_TASK_SW` |

## 驗證方法

### 1. 檢查任務能否被喚醒

```c
void task_a(void *p_arg)
{
    for (;;) {
        uart_puts("[TASK A] Before delay\n");
        OSTimeDlyHMSM(0, 0, 1, 0);
        uart_puts("[TASK A] After delay\n");  // 應該在 1 秒後出現
    }
}
```

### 2. 觀察輸出

```
[TASK A] Before delay
[TICK] 1s
[TASK A] After delay    ← 如果出現，表示成功喚醒!
[TASK A] Before delay
[TICK] 2s
[TASK A] After delay
```

### 3. 檢查兩個任務交替執行

```
[TASK A] Counter: 0
[TASK B] Counter: 0
[TICK] 1s
[TASK A] Counter: 1    ← A 和 B 都能正常運行
[TASK B] Counter: 1
```

## 總結

Context Switch 正常工作的**關鍵**：

1. **區分兩種切換情況**:
   - IRQ 上下文: 從 `SPSR_EL1` 讀取
   - Task 上下文: 手動設置 `0x205`

2. **確保 IRQ enabled**:
   - SPSR bit[7] = 0
   - ERET 後 timer interrupt 可以觸發

3. **正確的 Exception Level**:
   - 設置為 EL1h (0x5)
   - 匹配任務運行的 EL 和 SP

4. **Task Delay 機制**:
   - 設置 delay counter
   - Timer tick 遞減 counter
   - Counter = 0 時任務 ready
   - 調度器切換回任務
