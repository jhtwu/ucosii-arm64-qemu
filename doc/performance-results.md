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
| TX notify batch size 16 → 32 | 865.3 → 922.7 Mbps | 約 **+6.6%** | 1033.3 → 1036.7 Mbps | 約 +0.3% | config sweep 與第二輪確認支持 32；已設定為 default 並已 push（commit `ad272e6`） |
| TX TCP/UDP checksum offload step 1 | 902.2 → 910.7 Mbps | 約 **+0.9%** | 1027.0 → 1068.5 Mbps | 約 **+4.0%** | corrected BPI A/B 三次各方向均成功；只協商 `VIRTIO_NET_F_CSUM`，採用 |
| VirtIO used-event IRQ suppression step 2 | 920.3 → 941.4 Mbps | 約 **+2.3%** | 1032.0 → 1106.7 Mbps | 約 **+7.2%** | 只使用 `used_event` 抑制裝置 IRQ；BPI A/B 12 次全數完成，採用 |
| RX checksum offload step 3 | 904.9 → 940.0 Mbps | 約 **+3.9%** | 1036.6 → 1074.7 Mbps | 約 **+3.7%** | 協商 `VIRTIO_NET_F_GUEST_CSUM` 並驗證 RX metadata；BPI A/B 12 次全數完成，採用 |
| Mergeable RX buffers step 4A | 910.3 → 879.6 Mbps | **-3.4%** | 985.9 → 980.6 Mbps | **-0.5%** | 單 buffer fast path 已保留；本身沒有 throughput 收益，但作為 GSO RX 的必要 prerequisite |
| TCPv4 GSO pass-through step 5 | 826.6 → 1026.7 Mbps | **+24.2%** | 807.4 → 1294.6 Mbps | **+60.4%** | **採用；BPI vhost 3×3 A/B，12 次 iperf3 全通過，enabled case 0 retransmit** |
| Packed virtqueue candidate | 243.0 → 248.5 Mbps* | 約 +2.2%* | 242.1 → 211.1 Mbps* | 約 **-12.8%*** | 不採用；BPI `vhost=on` 不提供 packed feature，`vhost=off` 的單次對照 RX 反而下降 |
| RX IRQ minimal + task poll | full ISR 不可持續 → 835 Mbps | N/A† | full ISR 未完成 → 975 Mbps | N/A† | **採用候選；將 RX used-ring drain 移到 task，避免單 vCPU 被 IRQ drain 佔滿** |
| RX used-ring incremental invalidate | 850.0 → 852.0 Mbps | 約 +0.2% | 1003.5 → 988.7 Mbps | 約 -1.5% | 正確性與 cache 工作量改善，但 throughput 未見可辨識增益，暫不宣稱效能提升 |
| RX recycle/avail-ring batching | 886 → 858 Mbps | 約 -3.2% | 1037 → 1040 Mbps | 約 +0.3% | `agy` review PASS；降低重複 cache clean，但本次 BPI A/B 未證明 throughput 收益，暫保留作候選 |
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

### RX used-ring incremental invalidate 的 BPI A/B 數據

這組比較固定使用已採用的 task-poll 模式，只改變 RX used-ring cache invalidate 範圍：baseline 每次 invalidate 整個 used ring；新版本先讀取 `used->idx`，只 invalidate 自上次 drain 以來新增的 entries，並處理 ring wrap。Baseline 跑 2 次、新版本跑 3 次，結果分別為 TX **850.0 → 852.0 Mbps**、RX **1003.5 → 988.7 Mbps**；差異落在量測波動內，沒有可辨識的 throughput 改善。

### RX recycle/avail-ring batching 的 BPI A/B 數據

這組比較固定使用 task-poll 與 incremental used-ring invalidate，只改變 RX descriptor recycle 的 cache clean/notify 行為。新版本以 `VIRTIO_NET_RX_RECYCLE_BATCH_SIZE=16` 累積 avail ring 更新，並在 publish `avail->idx` 前加入 cache barrier；direct poll 與 completion queue full 路徑也會確實 flush。兩個版本各跑 3 次，每次 TX/RX 10 秒：

| 狀態 | TX receiver runs | TX mean | RX receiver runs | RX mean |
|---|---|---:|---|---:|
| baseline | 880, 897, 881 Mbps | 886 Mbps | 1.05, 1.04, 1.02 Gbps | 約 1037 Mbps |
| recycle batching | 848, 865, 861 Mbps | 858 Mbps | 1.05, 1.03, 1.04 Gbps | 約 1040 Mbps |

相對 baseline 為 TX 約 **-3.2%**、RX 約 **+0.3%**。雙向 ping 均為 0% packet loss，6 次 iperf3 均正常完成；目前只能確認正確性與減少重複 cache clean，不能宣稱穩定 throughput 改善。

### TX notify batch size config sweep 的 BPI 數據

這組測試固定使用目前的 RX path，只以編譯 config `VIRTIO_NET_TX_BATCH_SIZE` 改變 TX `QUEUE_NOTIFY` 頻率；每次測試 10 秒，表中為 iperf3 receiver throughput。第一輪各設定跑 3 次，第二輪以不同順序重新確認 `16/32/64` 各 2 次：

| TX batch size | 第一輪 TX mean | 第一輪 RX mean | 第二輪 TX mean | 第二輪 RX mean |
|---:|---:|---:|---:|---:|
| 8 | 763 Mbps | 1010 Mbps | — | — |
| 16 | 865 Mbps | 1033 Mbps | 846 Mbps | 1050 Mbps |
| 32 | **923 Mbps** | **1037 Mbps** | **932 Mbps** | 1035 Mbps |
| 64 | 927 Mbps | 979 Mbps | 920 Mbps | 959 Mbps |

`32` 是目前 TX/RX 平衡最好的設定；`64` 雖然 TX 接近 32，但 RX 明顯下降，`8` 則兩個方向都較慢。依 sweep 與第二輪確認，default 已改為 32。

### TX checksum offload step 1 的 BPI A/B 數據

這組 corrected 測試固定使用 BPI-R4 KVM/vhost、1 vCPU、512 MiB、兩張 VirtIO-net；QEMU 只明確開啟 `csum=on`，並關閉 GSO/TSO、guest checksum、mergeable RX buffer，避免混入其他 offload。只比較編譯 config `VIRTIO_NET_TX_CSUM_OFFLOAD=0/1`，每個方向各跑 3 次、每次測試重新啟動一個 guest，避免 TX queue 狀態跨輪污染。

| 狀態 | TX runs（LAN → WAN） | TX mean | reverse-RX runs（WAN → LAN） | reverse-RX mean |
|---|---|---:|---|---:|
| checksum offload disabled | 899.1, 898.0, 909.5 Mbps | 902.2 Mbps | 1038.5, 1023.7, 1018.9 Mbps | 1027.0 Mbps |
| checksum offload enabled | 895.1, 915.7, 921.3 Mbps | 910.7 Mbps | 1090.0, 1039.9, 1075.8 Mbps | 1068.5 Mbps |

相對改善為 TX 約 **+0.9%**、reverse-RX 約 **+4.0%**。兩個版本的 12 次 iperf3 均正常完成，所有 LAN/WAN gateway ping 均為 0% packet loss；offload enabled 的每個 guest boot log 都顯示兩次 `TX checksum offload enabled`。這一項只把 IPv4 TCP/UDP transport checksum 交給 host，IPv4 header checksum 與 NAT header 更新仍由 guest 完成。

另以 offload enabled image 做 raw packet capture：在只屬於測試 namespace 的 WAN veth 關閉 TX checksum/GSO/GRO/TSO 後，guest → WAN 的 TCP 封包均被 tcpdump 判定為 `cksum correct`，UDP 封包均顯示 `[udp sum ok]`。未關閉該 veth offload 時看到的 pseudo-header partial checksum 是預期的 VirtIO 傳輸中間狀態，不是 wire checksum failure。測試只建立並清除 `ucsi5`/`ucsi7`/`ucsi8` namespace、bridge、TAP 與 veth，未修改既有 BPI network 或 DrayOS QEMU。

### VirtIO used-event IRQ suppression step 2 的 BPI A/B 數據

這組測試固定使用已採用的 RX task-poll、TX batch size 32、TX checksum offload、BPI-R4 KVM/vhost、1 vCPU、512 MiB，以及 QEMU `csum=on`、GSO/TSO/guest checksum/mergeable RX buffer 關閉；只比較編譯 config `VIRTIO_NET_EVENT_IDX=0/1`。每個 case 都重新啟動 guest，TX 與 reverse-RX 各跑 3 次。

step 2 最終只採用 split-ring 的 `used_event`：guest 在讀到新的 used index 後，更新 avail ring 的 `used_event`，讓 host 只在有新的 completion 時產生 guest IRQ；原本嘗試同時用 `avail_event` 抑制 guest→host 的 queue notify，在 BPI smoke 中造成 TX queue completion notification 遺失而 timeout，因此已撤回。保留原有 batch notify 是為了避免這個 correctness risk。

| 狀態 | TX runs（LAN → WAN） | TX mean | reverse-RX runs（WAN → LAN） | reverse-RX mean |
|---|---|---:|---|---:|
| `VIRTIO_NET_EVENT_IDX=0` | 922.8, 924.4, 913.7 Mbps | 920.3 Mbps | 990.3, 1030.3, 1075.4 Mbps | 1032.0 Mbps |
| `VIRTIO_NET_EVENT_IDX=1` | 984.3, 937.8, 902.2 Mbps | 941.4 Mbps | 1112.4, 1086.5, 1121.1 Mbps | 1106.7 Mbps |

相對改善為 TX 約 **+2.3%**、reverse-RX 約 **+7.2%**。12 次 iperf3 全部正常完成，所有 LAN/WAN gateway ping 均為 0% packet loss；測試只建立並清除 `u11q`/`u11c`/`u11s` namespace、bridge、TAP 與 veth，未修改既有 BPI network 或 DrayOS QEMU。

### RX checksum offload step 3 的 BPI A/B 數據

這組測試固定使用已採用的 RX task-poll、TX batch size 32、TX checksum offload、used-event IRQ suppression、BPI-R4 KVM/vhost、1 vCPU、512 MiB，以及 QEMU `csum=on`、GSO/TSO/mergeable RX buffer 關閉；只比較編譯 config `VIRTIO_NET_RX_CSUM_OFFLOAD=0/1`，並同步將 QEMU `guest_csum` 設為 off/on。每個 case 都重新啟動 guest，TX 與 reverse-RX 各跑 3 次。

RX path 原本沒有做軟體 transport-checksum validation，因此這項不是把既有 guest 計算搬到 host，而是讓 host 在需要時交付 VirtIO partial-checksum metadata；router 對支援的 forwarded IPv4 TCP/UDP 會在 TX path 重新建立 outgoing checksum，再使用既有 TX offload。driver 會拒絕 GSO metadata 或不合法的 checksum offset，避免尚未支援 segmentation 時誤轉送。

| 狀態 | TX runs（LAN → WAN） | TX mean | reverse-RX runs（WAN → LAN） | reverse-RX mean |
|---|---|---:|---|---:|
| `VIRTIO_NET_RX_CSUM_OFFLOAD=0` | 913.7, 902.0, 898.9 Mbps | 904.9 Mbps | 989.8, 1082.1, 1037.9 Mbps | 1036.6 Mbps |
| `VIRTIO_NET_RX_CSUM_OFFLOAD=1` | 933.1, 943.7, 943.1 Mbps | 940.0 Mbps | 1056.6, 1077.4, 1090.0 Mbps | 1074.7 Mbps |

相對改善為 TX 約 **+3.9%**、reverse-RX 約 **+3.7%**。12 次 iperf3 全部正常完成，所有 LAN/WAN gateway ping 均為 0% packet loss；6 個 enabled guest boot log 均顯示兩張 NIC 成功協商 RX checksum offload，未出現 unsupported RX metadata。測試只建立並清除 `u14q`/`u14c`/`u14s` namespace、bridge、TAP 與 veth，未修改既有 BPI network 或 DrayOS QEMU。

### Mergeable RX buffers step 4A 的 BPI A/B 數據

這一項固定使用既有 task-poll、TX batch 32、TX/RX checksum offload、used-event suppression，以及 BPI-R4 KVM/vhost；只比較 `VIRTIO_NET_MRG_RXBUF=0/1`，GSO 保持關閉。driver 保留 2 KiB RX buffers，在 `num_buffers=1` 的一般封包走 fast path，只有真正跨 buffer 的封包才組合到 64 KiB merge storage。

| 狀態 | TX runs | TX mean | reverse-RX runs | reverse-RX mean |
|---|---|---:|---|---:|
| `VIRTIO_NET_MRG_RXBUF=0` | 914.0, 903.9, 913.1 Mbps | 910.3 Mbps | 978.1, 991.6, 988.0 Mbps | 985.9 Mbps |
| `VIRTIO_NET_MRG_RXBUF=1` | 890.0, 890.4, 858.4 Mbps | 879.6 Mbps | 977.6, 974.8, 989.4 Mbps | 980.6 Mbps |

相對為 TX **-3.4%**、reverse-RX **-0.5%**，因此不把 MRG_RXBUF 當成獨立 throughput 優化提交；它是後續接收 GSO superpacket 所需的相容性基礎。單 buffer fast path 已保留，避免一般 MTU 流量承擔 merged completion 的大筆複製成本。

### TCPv4 GSO pass-through step 5 的 BPI A/B 數據

這組正式比較固定使用 BPI-R4、KVM、1 vCPU、512 MiB、兩張 VirtIO-net、QEMU `vhost=on`，以及 `csum=on,guest_csum=on,gso=on,guest_tso4=on,host_tso4=on,mrg_rxbuf=on,event_idx=on,tx_queue_size=256,rx_queue_size=256`。只切換 compile-time `VIRTIO_NET_GSO_OFFLOAD=0/1`；兩個 case 都保留 MRG_RXBUF。每個 case 每方向 3 次、每次 10 秒，測試 guest 都重新啟動。

| 狀態 | TX runs（LAN → WAN） | TX mean | reverse-RX runs（WAN → LAN） | reverse-RX mean |
|---|---|---:|---|---:|
| `VIRTIO_NET_GSO_OFFLOAD=0` | 870.6, 796.5, 812.8 Mbps | 826.6 Mbps | 814.0, 808.7, 799.7 Mbps | 807.4 Mbps |
| `VIRTIO_NET_GSO_OFFLOAD=1` | 1024.5, 1021.0, 1034.7 Mbps | 1026.7 Mbps | 1274.4, 1308.3, 1301.1 Mbps | 1294.6 Mbps |

改善率為 TX **+24.2%**、reverse-RX **+60.4%**。這組 3×3 A/B 的 12 次 iperf3 全部 return 0，12 次 LAN/WAN ping 均為 0% packet loss；enabled case 的 TX chain unavailable、TX used-ring overrun、unsupported RX metadata 均為 0，iperf3 retransmit 為 0。disabled baseline 的 retransmit 為 TX 167/129/110、reverse-RX 52/119/41，仍可完成測試但 throughput 較低。修正後另以 enabled image 進行 60 秒長測：TX 1014.5 Mbps、reverse-RX 1303.5 Mbps，兩者均 0 retransmit、0 driver error。

實作上，RX 端接受並組合 IPv4 TCP GSO superpacket，NAT 更新 IP/port 後由 TX 端以 descriptor chain 傳回 host；TX reclaim、allocator 與 avail-ring publish 均以 critical section 保護 task/IRQ 競爭，descriptor 不足時先 notify host 並等待回收。`VIRTIO_NET_GSO_OFFLOAD` 已設為 default 1；若 host 沒有對應 TSO capability，driver 不會協商該 feature，仍走一般 frame path。測試使用 `uc41-*`、`ucsi-step5-*` temporary network resources，完成後清除，未修改既有 BPI network 或 DrayOS QEMU PID 779158。

### Packed virtqueue candidate 的 BPI 對照

這是相容性/效能候選，不是已採用的 offload。driver 新增 `VIRTIO_F_RING_PACKED` 的 opt-in，沒有 packed feature 時會 fallback 到既有 split ring。BPI 正式 `vhost=on` 的 host feature bitmap 為 `0x1c00101`，不包含 packed bit，因此實際仍使用 split ring，沒有可宣稱的 vhost throughput 改善。

為確認 packed path 本身，另在與既有環境隔離的 BPI KVM namespace 使用 `vhost=off` 測試。初版 packed `DESC` event suppression 在耗盡第一輪 RX descriptors 後會停住；改成 packed RX notification `ENABLE` 後，單次 3 秒 TX/RX 均完成，但與同條件 split 對照的 receiver throughput 如下：

| 狀態 | TX receiver | reverse-RX receiver |
|---|---:|---:|
| split | 243.0 Mbps | 242.1 Mbps |
| packed | 248.5 Mbps | 211.1 Mbps |

單次結果為 TX 約 **+2.2%**、reverse-RX 約 **-12.8%**；另一次重複腳本在第一個 run timeout，故不視為穩定改善。packed source 已全部撤回，沒有 commit/push；只保留上述測試結論，避免將不穩定且不適用 `vhost=on` 的候選帶入正式路徑。

## 未獨立量測的既有調整

以下功能在本次量測開始前已存在於 branch，沒有相同條件的獨立 baseline，因此不填造改善百分比：

- RX peek/release interface：避免 RX path 立即複製到額外 buffer。
- VirtIO queue depth：guest 端上限為 256 descriptors；QEMU 實際 queue capability 以 boot log 為準。

這些功能仍可能影響整體結果，但本文件只對有可比 A/B 數據的項目計算百分比。

## Memory / build 狀態

目前 dual-device build 的 ELF section 結果約為：

- `.bss`：2,519,600 bytes，約 2.40 MiB（GSO/MRG RX completion storage 已包含）
- guest RAM：512 MiB
- build：`make clean && make` PASS
- timer/context test：`make test-timer` PASS

## Current decision

目前保留並已 push 的主要效能修正是 used-ring alignment、cache ordering 調整、checksum path、used-index-only polling、RX IRQ minimal + task poll、RX used-ring incremental invalidate（commit `121ab18`），RX recycle/avail-ring batching（commit `c0817e7`），以及 TX notify batch size 32（commit `ad272e6`）。TX checksum offload step 1 已完成 corrected BPI A/B、TCP/UDP raw checksum 驗證與 agy review PASS，確認 reverse-RX 約 4.0% 改善、TX 約 0.9% 改善。step 2 的 used-event-only IRQ suppression 已完成 BPI A/B 與 agy review PASS，並已 commit/push（commit `8c639f2`）：TX 約 +2.3%、reverse-RX 約 +7.2%。step 3 RX checksum offload 已完成 final source BPI A/B、agy review PASS，並已 commit/push（commit `77bc1f0`）：TX 約 +3.9%、reverse-RX 約 +3.7%。step 4A MRG_RXBUF 本身沒有 throughput 收益，但已成為 GSO prerequisite；step 5 TCPv4 GSO pass-through 已完成修正版 BPI 3×3 A/B、60 秒雙向 stability test、fallback build 與 agy review PASS，TX 約 +24.2%、reverse-RX 約 +60.4%，source default 已開啟 `VIRTIO_NET_GSO_OFFLOAD=1`。zero-copy forwarding 只完成候選實驗，因為在 BPI 實測明顯降低 throughput，已撤回，不會進入正式版本。
