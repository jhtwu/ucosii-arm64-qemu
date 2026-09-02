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
| RX buffer zero-copy forwarding candidate | 845.0 → 520.5 Mbps | -38.4% | 1030.0 → 346.0 Mbps | -66.4% | 不採用，已撤回未提交改動 |

### Used-ring alignment 的可信 A/B 數據

這組比較只改變 used-ring storage alignment，其餘條件相同；每個方向各跑 3 次：

| 狀態 | TX runs | TX mean | RX runs | RX mean |
|---|---|---:|---|---:|
| vhost fallback | 275, 269, 275 Mbps | 273.0 Mbps | 437, 429, 426 Mbps | 430.7 Mbps |
| vhost active | 854, 870, 845 Mbps | 856.3 Mbps | 934, 1020, 996 Mbps | 983.3 Mbps |

原因是 `struct vring_used` 為 packed layout，單純宣告成 struct array 時，第二個 device 的 ring 起始位址會落在非 4-byte aligned 位址；Linux vhost 因此拒絕啟動並 fallback 到 userspace VirtIO。改成每個 device 保留一個 4096-byte stride 後，兩個 VirtIO device 都能使用 vhost。

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

目前保留並已 push 的主要效能修正是 used-ring alignment、cache ordering 調整與 checksum path；zero-copy forwarding 只完成候選實驗，因為在 BPI 實測明顯降低 throughput，已撤回，不會進入正式版本。
