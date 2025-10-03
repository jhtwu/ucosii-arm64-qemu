# Timer Interrupt 修復說明

## 問題描述

Timer interrupt 只觸發一次後就停止，導致無法持續產生 tick，系統無法正常進行時間管理和任務調度。

## 根本原因

### 問題1: 中斷上下文中 SPSR 保存錯誤

在 IRQ handler 的 `SAVE_CONTEXT` 宏中，錯誤地保存了**當前的 DAIF**（已經被硬體 mask 的狀態）：

```assembly
/* 錯誤的實現 */
mrs     x0, daif              /* 讀取當前 DAIF - IRQ 已被 mask! */
and     x0, x0, #0x3C0
mov     x1, #0x5
orr     x0, x0, x1
str     x0, [sp, #CTX_SPSR]   /* 保存錯誤的 SPSR */
```

**問題**: 當 IRQ 異常發生時，CPU 硬體自動執行：
1. 將當前 PSTATE 保存到 `SPSR_EL1`
2. **設置 DAIF.I = 1** (mask IRQ)
3. 跳轉到 IRQ handler

因此在 IRQ handler 中讀取 `DAIF`，得到的是**已經 mask 的狀態**。當執行 `ERET` 時：
- 從堆疊恢復 SPSR 到 PSTATE
- 因為保存的 SPSR 中 IRQ 被 mask
- 返回後 **IRQ 仍然被 mask**
- 後續的 timer interrupt **無法觸發**

### 正確的修復

應該從 `SPSR_EL1` 讀取中斷前的原始 PSTATE：

```assembly
/* 正確的實現 */
.macro SAVE_SPSR_FROM_IRQ
    mrs     x0, spsr_el1      /* 讀取中斷前的 PSTATE */
    str     x0, [sp, #CTX_SPSR]
    mrs     x0, elr_el1       /* 讀取返回地址 */
    str     x0, [sp, #CTX_ELR]
.endm
```

## ARMv8-A 異常處理機制

### 進入異常時硬體自動執行

```
1. SPSR_ELx ← PSTATE          // 保存當前處理器狀態
2. ELR_ELx  ← PC               // 保存返回地址
3. PSTATE.DAIF ← mask bits     // Mask 中斷 (D=1, A=1, I=1, F=1 或部分)
4. PC ← vector_base + offset   // 跳轉到異常向量
```

### ERET 返回時硬體自動執行

```
1. PSTATE ← SPSR_ELx          // 恢復處理器狀態（包含 DAIF）
2. PC ← ELR_ELx               // 返回
```

### 關鍵理解

- **SPSR_ELx**: 異常發生前的 PSTATE **快照**
- **當前 DAIF**: 異常處理期間的狀態（IRQ 已 mask）
- **必須保存 SPSR_ELx** 才能在 ERET 時恢復正確的中斷狀態

## Timer Interrupt 工作流程

### 1. Timer 初始化

```c
void BSP_OS_TmrTickInit(uint32_t tick_rate)
{
    // 1. 計算 reload value
    uint32_t cnt_freq = timer_cntfrq();      // 62,500,000 Hz
    uint32_t reload = cnt_freq / tick_rate;  // 62,500 (for 1000 Hz)
    BSP_OS_TmrReload = reload;

    // 2. 啟用 timer (unmasked)
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "rZ"(ARCH_TIMER_CTRL_ENABLE));

    // 3. 設置初始 timer value
    __asm__ volatile("msr cntv_tval_el0, %0" :: "rZ"(reload));
}
```

### 2. Timer Interrupt 觸發流程

```
Timer Counter 遞減到 0
    ↓
產生 IRQ (interrupt ID 27)
    ↓
CPU 硬體自動：
    - SPSR_EL1 ← PSTATE (保存 IRQ enabled 狀態)
    - DAIF.I ← 1 (mask IRQ)
    - 跳轉到 IRQ vector
    ↓
OS_CPU_ARM_ExceptIrqHndlr:
    - SAVE_CONTEXT (保存寄存器)
    - SAVE_SPSR_FROM_IRQ (保存 SPSR_EL1 → 堆疊)
    - OSIntNesting++
    - irq_dispatch() → BSP_OS_TmrTickHandler()
        - BSP_OS_VirtTimerReload() (重新設置 tval)
        - OSTimeTick() (更新系統 tick)
    - OSIntExit()
    - 恢復任務堆疊
    - RESTORE_AND_ERET
    ↓
ERET 執行：
    - PSTATE ← 堆疊的 SPSR (IRQ enabled!)
    - 返回任務
    ↓
Timer 繼續 countdown，下次到 0 時再次觸發
```

### 3. Timer Reload 機制

```c
static inline void BSP_OS_VirtTimerReload(void)
{
    uint32_t reload = BSP_OS_TmrReload;  // 62,500
    if (reload != 0u) {
        // 設置新的 timer value，timer 自動從這個值開始 countdown
        __asm__ volatile("msr cntv_tval_el0, %0" :: "rZ"(reload));
        // Timer enable bit 保持不變，不需要重新寫 control register
    }
}
```

**重要**:
- `CNTV_TVAL_EL0` 寫入後，timer 自動從這個值開始遞減
- Timer enable bit (CNTV_CTL_EL0.ENABLE) 保持為 1
- **不需要**每次都重寫 control register

## Timer 頻率配置

### Hardware Timer Frequency
- System counter: **62,500,000 Hz** (QEMU 模擬)
- Timer interrupt rate: **1000 Hz** (每秒 1000 次)
- Reload value: `62,500,000 / 1000 = 62,500`

### uC/OS-II Configuration
```c
#define OS_TICKS_PER_SEC  1000u   // 必須匹配 timer 頻率
```

### 時間計算
```
OSTimeDlyHMSM(0, 0, 1, 0);  // Delay 1 秒
    ↓
ticks = 1 * OS_TICKS_PER_SEC = 1000 ticks
    ↓
實際時間 = 1000 ticks / 1000 Hz = 1.0 秒 ✓
```

## 驗證方法

### 1. 檢查 Timer 持續觸發

添加 tick counter：
```c
void BSP_OS_TmrTickHandler(uint32_t cpu_id)
{
    static uint32_t tick_count = 0;
    tick_count++;
    if ((tick_count % 1000) == 0) {
        uart_puts("[TICK] ");
        uart_write_dec(tick_count / 1000);
        uart_puts("s\n");
    }

    BSP_OS_VirtTimerReload();
    OSTimeTick();
}
```

### 2. 觀察輸出

```
[TICK] 1s    // 1000 ticks = 1 秒
[TICK] 2s    // 2000 ticks = 2 秒
[TICK] 3s    // 3000 ticks = 3 秒
```

### 3. 驗證任務 Delay

```c
// Task A
for (;;) {
    uart_puts("[TASK A] Counter: ");
    uart_write_dec(counter++);
    uart_puts("\n");
    OSTimeDlyHMSM(0, 0, 1, 0);  // Delay 1 秒
}
```

輸出應該顯示：
```
[TASK A] Counter: 0
[TICK] 1s
[TASK A] Counter: 1    // 正好 1 秒後
[TICK] 2s
[TASK A] Counter: 2    // 正好 2 秒後
```

## 總結

Timer interrupt 正常工作的**三個關鍵**：

1. **正確保存/恢復 SPSR**:
   - 中斷上下文: 從 `SPSR_EL1` 讀取
   - 確保 ERET 後 IRQ 重新 enable

2. **Timer Reload 機制**:
   - 每次中斷時重新設置 `CNTV_TVAL_EL0`
   - Timer 自動繼續 countdown

3. **頻率配置匹配**:
   - Timer 頻率 = `OS_TICKS_PER_SEC`
   - 確保時間計算正確
