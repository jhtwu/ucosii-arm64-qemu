# uC/OS-II VirtIO Router Performance Results

本文件記錄 uC/OS-II AArch64 router 在 BPI-R4 上的實測結果，以及每項效能調整的結論。

## 測試條件

- Platform：BPI-R4，AArch64，Linux host，KVM enabled
- QEMU：`virt` machine，GICv3，`-cpu host`，1 vCPU，512 MiB RAM
- Network：兩張 VirtIO-net，QEMU `vhost=on`
- Traffic path：`LAN host -> uC/OS-II NAT -> WAN host`（TX），以及反向（RX）
- Tool：iperf3，TCP single stream
- Isolation：每次測試使用獨立的 temporary bridge、TAP 與 network namespace；測試完成後清除，未停止原有 DrayOS/OpenWrt QEMU
- 計算方式：`improve % = (after - before) / before * 100`

除非另有註明，數字為多次測試的平均值；單次測試只用來觀察趨勢，不作為採用依據。

## 結果總覽

| 優化項目 | TX baseline → result | TX improve | RX baseline → result | RX improve | 結論 |
|---|---:|---:|---:|---:|---|
| Incremental TCP/UDP checksum update | 261 → 255 Mbps | -2.3% | 332 → 411 Mbps | +23.8% | 單次 A/B 波動大；checksum equivalence test PASS，功能保留，但不能宣稱穩定 throughput 增益 |
| Cache clean no-sync + grouped barrier | 275.7 → 268.7 Mbps | -2.5% | 397.3 → 384.7 Mbps | -3.2% | fallback 狀態下沒有改善；保留作為正確的 cache ordering，並支援 vhost 路徑 |
| Used-ring page alignment，解除 vhost fallback | 273.0 → 856.3 Mbps | **+213.7%** | 430.7 → 983.3 Mbps | **+128.3%** | **採用；本次最大且穩定的改善** |
| TX used-index-only polling | 862.3 → 875.7 Mbps | 約 +1.5% | 1006.7 → 1050 Mbps | 約 +4.3% | 採用；降低每包 cache invalidate 範圍，但增益小於量測波動，需持續以多輪 A/B 觀察 |
| RX IRQ minimal + task poll | full ISR 不可持續 → 835 Mbps | N/A† | full ISR 未完成 → 975 Mbps | N/A† | **採用候選；將 RX used-ring drain 移到 task，避免單 vCPU 被 IRQ drain 佔滿** |
| RX buffer zero-copy forwarding candidate | 845.0 → 520.5 Mbps | -38.4% | 1030.0 → 346.0 Mbps | -66.4% | 不採用，已撤回未提交改動 |

† Full-ISR baseline 在本次同條件測試中前 4–5 秒約 770–895 Mbps，之後降為 0 並 timeout，因此沒有可與完整 10 秒 task-poll 結果直接計算的 steady-state baseline；N/A 不代表沒有改善，而是 baseline 沒有完成可比測量。

### Used-ring alignment 的可信 A/B 數據

這組比較只改變 used-ring storage alignment，其餘條件相同；每個方向各跑 3 次：

| 狀態 | TX runs | TX mean | RX runs | RX mean |
|---|---|---:|---|---:|
| vhost fallback | 275, 269, 275 Mbps | 273.0 Mbps | 437, 429, 426 Mbps | 430.7 Mbps |
| vhost active | 854, 870, 845 Mbps | 856.3 Mbps | 934, 1020, 996 Mbps | 983.3 Mbps |

### TX used-index-only polling 的 A/B 數據

本次只將 TX completion polling 與 interrupt path 的 cache invalidate 範圍縮小為 `used->idx`；每個版本各跑 3 次。iperf3 對 Gbits/sec 的輸出只有兩位小數，因此 RX 數值以約值表示：

| 狀態 | TX runs | TX mean | RX runs | RX mean |
|---|---|---:|---|---:|
| full used-ring invalidate | 830, 880, 877 Mbps | 862.3 Mbps | 1.01, 1.01, 1.00 Gbps | 約 1006.7 Mbps |
| used-index-only invalidate | 886, 849, 892 Mbps | 875.7 Mbps | 1.05, 1.05, 1.05 Gbps | 約 1050 Mbps |

原因是 `struct vring_used` 為 packed layout，單純宣告成 struct array 時，第二個 device 的 ring 起始位址會落在非 4-byte aligned 位址；Linux vhost 因此拒絕啟動並 fallback 到 userspace VirtIO。改成每個 device 保留一個 4096-byte stride 後，兩個 VirtIO device 都能使用 vhost。

### RX IRQ minimal + task poll 的 BPI A/B 數據

這組比較保留原本 full used-ring invalidate，唯一的功能變數是 `VIRTIO_NET_RX_DEFER_POLL`：

- `0`（baseline）：IRQ 直接 drain RX used ring，再 ACK interrupt。
- `1`（deferred）：IRQ 只讀 status、更新 TX used index、ACK 並 post semaphore；`net_rx_task()` 負責 poll/drain RX used ring。

同樣使用 BPI-R4 KVM/vhost、1 vCPU、TCP single stream、每方向 10 秒。Deferred image 的 TX/RX 分別為 **835 Mbps / 975 Mbps**，client/server 均正常完成；full-ISR baseline 在短暫高 throughput 後卡住，TX 測試 timeout，reverse-RX 無法完成。因此這項優化目前以「恢復可持續 throughput」為主要成果，不能用失敗的 baseline 直接宣稱固定百分比。

## 未獨立量測的既有調整

以下功能在本次量測開始前已存在於 branch，沒有相同條件的獨立 baseline，因此不填造改善百分比：

- TX batching：每 16 個 frame 才通知一次 host。
- RX peek/release interface：避免 RX path 立即複製到額外 buffer。
- VirtIO queue depth：guest 端上限為 256 descriptors；QEMU 實際 queue capability 以 boot log 為準。

這些功能仍可能影響整體結果，但本文件只對有可比 A/B 數據的項目計算百分比。

## Memory / build 狀態

目前 dual-device build 的 ELF section 結果約為：

- `.bss`：2,190,864 bytes，約 2.09 MiB
- guest RAM：512 MiB
- build：`make clean && make` PASS
- timer/context test：`make test-timer` PASS

## Current decision

目前保留並已 push 的主要效能修正是 used-ring alignment、cache ordering 調整與 checksum path；used-index-only polling 與 RX IRQ minimal + task poll 已完成本地與 BPI A/B 測試，尚未 commit/push。zero-copy forwarding 只完成候選實驗，因為在 BPI 實測明顯降低 throughput，已撤回，不會進入正式版本。
