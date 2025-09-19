# uC/OS-II ARM64 QEMU Demo / uC/OS-II ARM64 QEMU 範例

這個專案提供在 QEMU virt 平台上執行的 uC/OS-II ARMv8-A 範例，藉由兩個任務交替列印訊息來展示情境切換。

This repository showcases a uC/OS-II demo on the QEMU virt ARMv8-A machine. Two tasks cooperatively alternate UART output to highlight context switches.

## 需求 / Requirements

- GCC cross compiler for AArch64 (`aarch64-linux-gnu-gcc`)
- QEMU (`qemu-system-aarch64`)

## 建置 / Build

```bash
make
```

## 執行 / Run

```bash
make run
```

執行結果會顯示啟動訊息與兩個任務交錯輸出的計數，`make run` 會在 10 秒後自動結束，以免卡住終端機。

The demo prints boot logs followed by alternating task counters. `make run` times out after 10 seconds to avoid hanging the terminal.

## 專案結構 / Project Layout

- `src/` – application entry point與任務邏輯 / application entry and task logic
- `port/` – ARMv8-A uC/OS-II port (Assembly + C)
- `bsp/` – 簡易 UART/Timer/GIC 驅動 / basic drivers for UART, timer, GIC
- `ucosii/` – 核心 uC/OS-II 原始碼與設定 / core uC/OS-II sources and config
- `start.S` – 處理 EL 轉換與 BSS 清除的啟動碼 / low-level startup handling EL transitions and BSS

## 授權 / License

基於上游 uC/OS-II 開放源碼授權 (見 `ucosii/` 內各檔案)。其餘檔案可自由使用。
