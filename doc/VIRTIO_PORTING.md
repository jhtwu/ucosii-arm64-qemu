# VirtIO-Net Driver Porting / VirtIO-Net 驅動移植說明

## 背景 / Background
- 專案：uC/OS-II on QEMU ARM virt (GICv3)
- 來源：armv8 範例專案中的 VirtIO MMIO 網路驅動
- 目標：在此專案中實現可用的 virtio-net TX/RX，並方便以橋接 TAP 測試

## 核心修改 / Key Changes
1. **驅動移植 (`bsp/virtio_net.c` / `bsp/virtio_net.h`)**
   - 實作裝置偵測與 VirtIO MMIO 初始化流程 (status bits、features 協商、vRing 配置)
   - 建立 RX/TX 佇列與緩衝池，並整合 GIC 中斷處理
   - 加入大量 UART 訊息，協助追蹤啟動與封包流程
2. **lib 支援 (`include/lib.h`, `src/lib.c`)**
   - 針對 freestanding 環境提供基本 `memcpy/memset/memcmp` 與 endian 轉換工具
3. **網路示範任務 (`include/net_demo.h`, `src/net_demo.c`)**
   - Task B 啟動後設定客體 IP 192.168.1.1/24
   - 週期性送出 ARP 請求和 ICMP Echo，並處理主動/被動封包
   - 所有事件透過 UART 記錄 (ARP reply、ICMP echo request/reply)
4. **建置與啟動 (`Makefile`, `src/main.c`)**
   - 將新檔案納入 build system，新增 `make run-tap`
   - Task B 改為執行 `net_demo_run()`，確保系統啟動即進入網路測試

## 測試步驟 / Testing Steps
1. 交叉編譯：`make`
2. 使用橋接 TAP 測試：
   ```bash
   sudo tcpdump -ni br-lan 'icmp and host 192.168.1.1'
   sudo make run-tap
   ```
3. 另開終端執行 `ping -I br-lan 192.168.1.1` 觀察 ARP 菊花與 ICMP 往返

## 已知事項 / Notes
- 客體 IP 預設為 `192.168.1.1`，主機端預期位於同網段 (例如 `192.168.1.103`)
- `make run` 仍使用 user-mode netdev；橋接測試請改用 `make run-tap`
- 需要 root 權限才能對 TAP (`/dev/net/tun`) 與橋接介面進行操作

