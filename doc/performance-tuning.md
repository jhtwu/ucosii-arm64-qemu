# uC/OS-II VirtIO Net 性能調校筆記

## 目標

- 讓資料快取 (D-cache) 與 DMA 裝置保持一致性。
- 降低 RX/TX 處理的輪詢延遲，改為事件驅動。
- 將行為對齊 `../armv8` 專案中已驗證的作法。

## MMU / Cache

- `boot/start.S` 啟動後呼叫 `mmu_init()`，`boot/linker.ld` 預留 `.mmutable`。
- `bsp/mmu.c` 以 1GB block 建立 flat mapping，設定 MAIR/TCR/TTBR0，並啟用 CR_M/CR_C/CR_I。
- `bsp/cache.c` 提供 `cache_clean_range()`、`cache_invalidate_range()`、`cache_clean_invalidate_range()`；所有 virtio 資料在寫入/讀取前都依此同步。

## VirtIO Net Driver

- vring descriptor/avail/used 改為靜態配置 (`bsp/virtio_net.c:120-168`)，避免 malloc 導致 cache 與對齊問題。
- IRQ 只更新 used ring/descriptor，並 `OSSemPost()` 喚醒 RX task 與等待中的應用邏輯。
- `virtio_net_wait_rx_dev()/virtio_net_wait_rx_any()` 使用 uC/OS-II semaphore/blocking 等待封包；timeout fallback 會以 `OSTimeDly(1)` 讓出 CPU。
- 每個傳送/回收都對 descriptor、ring entry、payload 做 cache clean/invalidate (`bsp/virtio_net.c:238-718`)，確保 DMA 資料正確。

## 任務架構

- 新增 `net_rx_task()` (LAN/WAN 各一)，由 `OSTaskCreate()` 以固定優先權啟動 (`src/net_demo.c:1026-1039`)。
- RX task 執行流程：`virtio_net_wait_rx_dev()` → draining `virtio_net_poll_frame_dev()` → 呼叫 `net_demo_process_frame()`。
- 主控迴圈只負責週期性的 ARP/Ping (`src/net_demo.c:1044-1076`)，不再做重複輪詢。
- 測試 (`test/test_network_ping.c:341-417`) 以事件驅動等待 ARP/Ping 回覆，縮短 RTT 計算延遲。

## uC/OS-II Kernel 設定

- `ucosii/include/os_cfg.h` 啟用 `OS_SEM_EN/OS_SEM_ACCEPT_EN`，並將 `OS_MAX_EVENTS` 提升至 8。
- 新增 `ucosii/source/os_sem.c`，提供最小但完整的 semaphore 實作 (Create/Pend/Post/Accept)。

## 驗證

```bash
make run
make test-all
```

兩者皆須成功，`test-all` 會運行 context-switch、TAP ping 與 dual NIC 測試；完成後確認 throughput 測試差距已縮小。

