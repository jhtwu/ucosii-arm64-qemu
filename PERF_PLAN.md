# Throughput Optimization Plan — ucosii-arm64-qemu

**Goal**: Break 124 Mbps KVM ceiling. Target: 180–220 Mbps.
**Files to modify**: `bsp/virtio_net.c`, `bsp/virtio_net.h`, `src/net_demo.c`
**Do NOT modify**: bsp_os.c, gic.c, nat.c, mmu.c, linker.ld, Makefile

After each change, rebuild with `make -C /project/ucosii-arm64-qemu` and verify it compiles clean.

---

## Change 1: TX Batching [HIGHEST IMPACT — est. +40–60 Mbps]

### Problem
In `virtio_net_send_frame_dev()`, `virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, ...)` fires after
**every single packet**. Each QUEUE_NOTIFY is an MMIO write that causes a KVM VM-exit (expensive trap
to EL2). At 10K pps this is 10,000 KVM exits/sec just for TX notifications.

### Fix

#### Step 1a — Add define (top of virtio_net.c, near existing defines)
```c
#define VIRTIO_NET_TX_BATCH_SIZE    16u   /* notify host every N queued TX packets */
```

#### Step 1b — Add field to struct virtio_net_device (bsp/virtio_net.c ~line 119)
In the `struct virtio_net_device { ... }` block, add one field:
```c
    uint16_t tx_batch_count;   /* packets queued but not yet notified */
```

#### Step 1c — Change QUEUE_NOTIFY in virtio_net_send_frame_dev()
Find the line (currently ~line 742):
```c
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_TX_QUEUE);
```
Replace it with:
```c
    dev->tx_batch_count++;
    if (dev->tx_batch_count >= VIRTIO_NET_TX_BATCH_SIZE) {
        virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_TX_QUEUE);
        dev->tx_batch_count = 0u;
    }
```

#### Step 1d — Add flush function in virtio_net.c (add after virtio_net_send_frame_dev)
```c
void virtio_net_tx_flush_dev(size_t dev_idx)
{
    if (dev_idx >= g_device_count) {
        return;
    }
    struct virtio_net_device *dev = &g_devices[dev_idx];
    if (dev->tx_batch_count > 0u) {
        virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_TX_QUEUE);
        dev->tx_batch_count = 0u;
    }
}
```

#### Step 1e — Declare in bsp/virtio_net.h
Add to the header (near other function declarations):
```c
void virtio_net_tx_flush_dev(size_t dev_idx);
```

#### Step 1f — Call flush in net_demo.c net_rx_task loop
In `src/net_demo.c`, find `net_rx_task()` (around line 888).
The inner polling loop is:
```c
while (virtio_net_has_pending_rx_dev(iface->dev)) {
    int rc = virtio_net_poll_frame_dev(iface->dev, rx_buffer, &rx_length);
    if (rc > 0) {
        net_demo_process_frame(iface, rx_buffer, rx_length);
    } else if (rc < 0) {
        break;
    }
}
```
After this while-loop ends (i.e., after draining all pending RX), add TX flush for BOTH interfaces:
```c
        /* Flush pending TX batches after draining RX burst */
        virtio_net_tx_flush_dev(0u);   /* LAN device */
        virtio_net_tx_flush_dev(1u);   /* WAN device */
```

---

## Change 2: Eliminate Redundant memcpy in NAT Forwarding [HIGH IMPACT — est. +15–20 Mbps]

### Problem
Every forwarded packet has this pattern in `net_demo_process_frame()`:
```c
uint8_t forward_frame[VIRTIO_NET_MAX_FRAME_SIZE];
util_memcpy(forward_frame, frame, length);     // ← REDUNDANT COPY
// ... modify forward_frame headers ...
virtio_net_send_frame_dev(other_dev, forward_frame, ...);
```
The `frame` parameter is already a copy of the VirtIO RX buffer (copied in `virtio_net_poll_frame_dev`).
Copying again to `forward_frame` wastes ~1500 bytes × 10K pps = 15 MB/s of memcpy.

### Fix

#### Step 2a — Change function signature to mutable
In `src/net_demo.c`, find the static declaration of `net_demo_process_frame`. Change:
```c
static int net_demo_process_frame(struct net_interface *iface,
                                  const uint8_t *frame, size_t length)
```
To:
```c
static int net_demo_process_frame(struct net_interface *iface,
                                  uint8_t *frame, size_t length)
```
(remove `const`)

#### Step 2b — Remove all 6 forward_frame copy+allocate patterns
There are exactly 6 places in net_demo_process_frame where this pattern occurs.
For each one, do the following transformation:

**Before (repeated pattern):**
```c
uint8_t forward_frame[VIRTIO_NET_MAX_FRAME_SIZE];
if (length <= VIRTIO_NET_MAX_FRAME_SIZE && g_XXX_if.dev != NULL) {
    util_memcpy(forward_frame, frame, length);

    struct eth_header *fwd_eth = (struct eth_header *)forward_frame;
    struct ipv4_header *fwd_ip = (struct ipv4_header *)(forward_frame + sizeof(*fwd_eth));
    // ... more forward_frame casts ...
    virtio_net_send_frame_dev(other_dev, forward_frame, ...);
```

**After:**
```c
if (length <= VIRTIO_NET_MAX_FRAME_SIZE && g_XXX_if.dev != NULL) {
    // (no memcpy — modify frame in-place, it's already a private rx_buffer copy)
    struct eth_header *fwd_eth = (struct eth_header *)frame;
    struct ipv4_header *fwd_ip = (struct ipv4_header *)(frame + sizeof(*fwd_eth));
    // ... same casts but using frame instead of forward_frame ...
    virtio_net_send_frame_dev(other_dev, frame, ...);
```

Do this for all 6 occurrences (find them by searching for "forward_frame").

#### Step 2c — Update the call site in net_rx_task
In `net_rx_task()`, change:
```c
net_demo_process_frame(iface, rx_buffer, rx_length);
```
This is already correct since `rx_buffer` is `uint8_t []` (mutable). No change needed here.

---

## Change 3: Increase VirtIO Queue Depth 64 → 256 [MEDIUM IMPACT — est. +10–15 Mbps]

### Problem
RX/TX ring buffers are only 64 descriptors deep. At 10K pps, a single OS tick (1ms) sees ~10 packets.
Small ring causes back-pressure stalls: TX ring fills before host drains it, RX ring fills before
guest processes it.

### Fix

#### Step 3a — Change the queue size define (virtio_net.c ~line 54)
```c
/* Before: */
#define VIRTIO_NET_QUEUE_SIZE           64u

/* After: */
#define VIRTIO_NET_QUEUE_SIZE           256u
```

All static arrays (g_rx_desc, g_tx_desc, g_rx_buffer_storage, g_tx_buffer_storage, ring structs)
use this define and will resize automatically.

**Memory impact**: +~1.5 MB BSS (from ~512 KB to ~2 MB). VM has 256 MB — fine.

---

## Build & Test Procedure

```bash
# Build
make -C /project/ucosii-arm64-qemu clean && make -C /project/ucosii-arm64-qemu

# Deploy to BPI-R4
sshpass -p 'bananapi' scp -o PubkeyAuthentication=no \
  /project/ucosii-arm64-qemu/build/ucos_arm_demo.elf \
  root@192.168.1.1:/root/ucos_arm_demo.elf

# Restart VM on BPI-R4
sshpass -p 'bananapi' ssh -o PubkeyAuthentication=no root@192.168.1.1 \
  'pkill -9 qemu-system-aarch64; sleep 2; echo > /tmp/ucos-kvm.log; setsid /root/run_qemu_kvm.sh > /tmp/qemu-out.log 2>&1 < /dev/null &'

# Wait 8 seconds for VM boot
sleep 8

# Check boot log
sshpass -p 'bananapi' ssh -o PubkeyAuthentication=no root@192.168.1.1 'tail -30 /tmp/ucos-kvm.log'

# ARP warm-up (required before iperf3)
sshpass -p 'bananapi' ssh -o PubkeyAuthentication=no root@192.168.1.1 'ping -c 3 -I br-wan 10.3.5.99'
sudo ip route replace 10.3.5.104/32 via 192.168.1.2 dev br-lan src 192.168.1.103
ping -c 3 10.3.5.104 -I br-lan

# iperf3 test
iperf3 -c 10.3.5.104 -B 192.168.1.103 -t 10 -i 0        # TCP 1 stream
iperf3 -c 10.3.5.104 -B 192.168.1.103 -t 10 -P 4 -i 0   # TCP 4 parallel
iperf3 -c 10.3.5.104 -B 192.168.1.103 -u -b 200M -t 10  # UDP 200M (was 100M)
```

## Expected Results
| Test | Before | Target After |
|------|--------|-------------|
| TCP 1 stream | 111 Mbps | 160–180 Mbps |
| TCP 4 parallel | 124 Mbps | 180–220 Mbps |
| TCP reverse | 78.6 Mbps | 100–130 Mbps |
| UDP ceiling | 100 Mbps (0% loss) | 150–200 Mbps |

## Order of Implementation
Implement in this order, testing after each:
1. Change 3 (queue size) — simplest, least risk, test first
2. Change 1 (TX batching) — highest impact, test second
3. Change 2 (in-place modify) — clean up, test third
