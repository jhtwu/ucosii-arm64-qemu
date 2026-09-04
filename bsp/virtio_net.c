#include "virtio_net.h"

#include "mmio.h"
#include "uart.h"
#include "timer.h"
#include "lib.h"
#include "bsp_int.h"
#include "cache.h"

#include <ucos_ii.h>

#define VIRTIO_MMIO_MAGIC_VALUE         0x000u
#define VIRTIO_MMIO_VERSION             0x004u
#define VIRTIO_MMIO_DEVICE_ID           0x008u
#define VIRTIO_MMIO_VENDOR_ID           0x00Cu
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010u
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014u
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020u
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024u
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028u
#define VIRTIO_MMIO_QUEUE_SEL           0x030u
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034u
#define VIRTIO_MMIO_QUEUE_NUM           0x038u
#define VIRTIO_MMIO_QUEUE_READY         0x044u
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050u
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060u
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064u
#define VIRTIO_MMIO_STATUS              0x070u
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080u
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084u
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090u
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094u
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0A0u
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0A4u
#define VIRTIO_MMIO_CONFIG              0x100u

#define VIRTIO_STATUS_ACKNOWLEDGE       0x01u
#define VIRTIO_STATUS_DRIVER            0x02u
#define VIRTIO_STATUS_DRIVER_OK         0x04u
#define VIRTIO_STATUS_FEATURES_OK       0x08u
#define VIRTIO_STATUS_FAILED            0x80u

#define VIRTIO_ID_NET                   0x01u

#define VIRTIO_NET_F_CSUM               0u
#define VIRTIO_NET_F_GUEST_CSUM         1u
#define VIRTIO_NET_F_MAC                5u
#define VIRTIO_RING_F_EVENT_IDX         29u
#define VIRTIO_F_VERSION_1              32u

#define VIRTIO_NET_HDR_F_NEEDS_CSUM     0x01u
#define VIRTIO_NET_HDR_GSO_NONE         0u
#define VIRTIO_NET_TCP_CSUM_OFFSET      16u
#define VIRTIO_NET_UDP_CSUM_OFFSET      6u

#define VRING_DESC_F_NEXT               0x01u
#define VRING_DESC_F_WRITE              0x02u

#define VIRTIO_NET_RX_QUEUE             0u
#define VIRTIO_NET_TX_QUEUE             1u

#define VIRTIO_NET_QUEUE_SIZE           256u
#define VIRTIO_NET_BUFFER_SIZE          2048u
#ifndef VIRTIO_NET_TX_BATCH_SIZE
#define VIRTIO_NET_TX_BATCH_SIZE        32u   /* notify host every N queued TX frames */
#endif
#define VIRTIO_NET_USED_RING_STRIDE     4096u /* keep each used ring 4-byte aligned */

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed));

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_NET_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[VIRTIO_NET_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

struct virtio_queue {
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;
};

struct virtio_net_config {
    uint8_t mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
} __attribute__((packed));

struct virtio_net_device {
    uintptr_t base;
    uint32_t irq;
    uint16_t rx_queue_size;
    uint16_t tx_queue_size;
    uint16_t rx_last_used;
    uint16_t tx_last_used;
    uint8_t mac[6];
    uint8_t driver_ok;
    uint8_t tx_csum_offload;
    uint8_t rx_csum_offload;
    uint8_t event_idx;
    struct virtio_queue *rx_queue;
    struct virtio_queue *tx_queue;
    uint8_t *rx_buffers[VIRTIO_NET_QUEUE_SIZE];
    uint8_t *tx_buffers[VIRTIO_NET_QUEUE_SIZE];
    OS_EVENT *rx_sem;
    uint16_t tx_batch_count;   /* frames queued but host not yet notified */
    uint16_t rx_recycle_start;  /* first avail slot in pending recycle batch */
    uint16_t rx_recycle_count;  /* RX avail entries awaiting cache/notify */
};

/* Multiple device support */
static struct virtio_net_device g_devices[VIRTIO_NET_MAX_DEVICES];
static struct virtio_queue g_rx_queues[VIRTIO_NET_MAX_DEVICES];
static struct virtio_queue g_tx_queues[VIRTIO_NET_MAX_DEVICES];
static size_t g_device_count = 0u;

static struct vring_desc g_rx_desc[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_QUEUE_SIZE] __attribute__((aligned(16)));
static struct vring_desc g_tx_desc[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_QUEUE_SIZE] __attribute__((aligned(16)));
static struct vring_avail g_rx_avail[VIRTIO_NET_MAX_DEVICES] __attribute__((aligned(4096)));
static struct vring_avail g_tx_avail[VIRTIO_NET_MAX_DEVICES] __attribute__((aligned(4096)));
/* A packed used ring is 2054 bytes, so an array of structs misaligns the
 * second device's ring.  Reserve one page per device to satisfy vhost's
 * used-ring alignment requirement without changing the wire layout. */
static uint8_t g_rx_used_storage[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_USED_RING_STRIDE]
    __attribute__((aligned(4096)));
static uint8_t g_tx_used_storage[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_USED_RING_STRIDE]
    __attribute__((aligned(4096)));

static OS_EVENT *g_rx_global_sem = NULL;

static INT32U virtio_ms_to_ticks(INT16U timeout_ms)
{
    if (timeout_ms == 0u) {
        return 0u;
    }

    INT32U ticks = ((INT32U)timeout_ms * OS_TICKS_PER_SEC + 999u) / 1000u;
    if (ticks == 0u) {
        ticks = 1u;
    }
    return ticks;
}

static uint8_t g_rx_buffer_storage[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_QUEUE_SIZE][VIRTIO_NET_BUFFER_SIZE] __attribute__((aligned(64)));
static uint8_t g_tx_buffer_storage[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_QUEUE_SIZE][VIRTIO_NET_BUFFER_SIZE] __attribute__((aligned(64)));

struct rx_completion_entry {
    uint16_t desc_id;
    uint32_t total_len;
};

static struct rx_completion_entry g_rx_completions[VIRTIO_NET_MAX_DEVICES][VIRTIO_NET_QUEUE_SIZE];
static volatile uint16_t g_rx_completion_head[VIRTIO_NET_MAX_DEVICES] = {0u};
static volatile uint16_t g_rx_completion_tail[VIRTIO_NET_MAX_DEVICES] = {0u};
static volatile uint16_t g_rx_completion_count[VIRTIO_NET_MAX_DEVICES] = {0u};

/* Legacy single device pointer (points to device 0) */
static struct virtio_net_device *g_dev = NULL;

static void virtio_net_arm_rx_used_event(struct virtio_net_device *dev,
                                         uint16_t used_idx);

static inline uint32_t virtio_mmio_read32(uintptr_t base, uint32_t offset)
{
    uint32_t value = mmio_read32(base + offset);
    __asm__ volatile("dsb ish" ::: "memory");
    return value;
}

static inline void virtio_mmio_write32(uintptr_t base, uint32_t offset, uint32_t value)
{
    __asm__ volatile("dsb ishst" ::: "memory");
    mmio_write32(base + offset, value);
    __asm__ volatile("dsb ish" ::: "memory");
}

static inline uint32_t virtio_reg_read(const struct virtio_net_device *dev, uint32_t offset)
{
    return virtio_mmio_read32(dev->base, offset);
}

static inline void virtio_reg_write(const struct virtio_net_device *dev, uint32_t offset, uint32_t value)
{
    virtio_mmio_write32(dev->base, offset, value);
}

static void log_hex32(const char *prefix, uint32_t value)
{
    uart_puts(prefix);
    uart_write_hex((unsigned long)value);
    uart_putc('\n');
}

static void log_hex8(uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    uart_putc(digits[(value >> 4u) & 0xFu]);
    uart_putc(digits[value & 0xFu]);
}

static void log_status(const char *label, uint32_t status)
{
    uart_puts(label);
    uart_puts(" status=0x");
    uart_write_hex((unsigned long)status);
    uart_putc('\n');
}

static uint16_t virtio_net_read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8u) | (uint16_t)data[1]);
}

/* Return the non-complemented one's-complement sum of an IPv4 L4 pseudo-header. */
static uint16_t virtio_net_ipv4_pseudo_sum(const uint8_t *ip,
                                           uint8_t protocol,
                                           uint16_t transport_len)
{
    uint32_t sum = 0u;

    sum += virtio_net_read_be16(ip + 12u);
    sum += virtio_net_read_be16(ip + 14u);
    sum += virtio_net_read_be16(ip + 16u);
    sum += virtio_net_read_be16(ip + 18u);
    sum += (uint32_t)protocol;
    sum += (uint32_t)transport_len;

    while ((sum >> 16u) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }

    return (uint16_t)sum;
}

/*
 * Prepare the legacy virtio-net header for a single-packet TX checksum
 * offload.  IPv4 checksum and NAT header updates remain guest-owned; the
 * host only fills the TCP/UDP checksum field described by this header.
 */
static void virtio_net_prepare_tx_csum(struct virtio_net_device *dev,
                                       struct virtio_net_hdr *hdr,
                                       uint8_t *payload,
                                       size_t payload_len)
{
    size_t l2_len = 14u;
    size_t ip_offset;
    size_t ip_header_len;
    size_t transport_offset;
    size_t transport_len;
    uint16_t ethertype;
    uint16_t ip_total_len;
    uint8_t protocol;
    uint16_t *checksum;

    if (dev == NULL || dev->tx_csum_offload == 0u || payload == NULL ||
        payload_len < l2_len + 20u) {
        return;
    }

    ethertype = virtio_net_read_be16(payload + 12u);
    if (ethertype == 0x8100u || ethertype == 0x88A8u) {
        if (payload_len < l2_len + 4u + 20u) {
            return;
        }
        l2_len += 4u;
        ethertype = virtio_net_read_be16(payload + 16u);
    }
    if (ethertype != 0x0800u) {
        return;
    }

    ip_offset = l2_len;
    ip_header_len = (size_t)(payload[ip_offset] & 0x0Fu) * 4u;
    if ((payload[ip_offset] >> 4u) != 4u || ip_header_len < 20u ||
        payload_len < ip_offset + ip_header_len) {
        return;
    }

    ip_total_len = virtio_net_read_be16(payload + ip_offset + 2u);
    if (ip_total_len < ip_header_len ||
        (size_t)ip_total_len != payload_len - ip_offset) {
        return;
    }

    /* A fragmented IPv4 packet has no safely addressable L4 checksum field. */
    if ((virtio_net_read_be16(payload + ip_offset + 6u) & 0x3FFFu) != 0u) {
        return;
    }

    protocol = payload[ip_offset + 9u];
    transport_offset = ip_offset + ip_header_len;
    transport_len = (size_t)ip_total_len - ip_header_len;
    if (protocol == 6u) {
        if (transport_len < 20u) {
            return;
        }
        size_t tcp_header_len = (size_t)(payload[transport_offset + 12u] >> 4u) * 4u;
        if (tcp_header_len < 20u || tcp_header_len > transport_len) {
            return;
        }
        checksum = (uint16_t *)(payload + transport_offset + 16u);
        hdr->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
        hdr->csum_start = (uint16_t)transport_offset;
        hdr->csum_offset = VIRTIO_NET_TCP_CSUM_OFFSET;
        /* VirtIO expects the pseudo-header sum in the checksum field. */
        *checksum = util_htons(virtio_net_ipv4_pseudo_sum(payload + ip_offset,
                                                          protocol,
                                                          (uint16_t)transport_len));
    } else if (protocol == 17u) {
        if (transport_len < 8u) {
            return;
        }
        if ((size_t)virtio_net_read_be16(payload + transport_offset + 4u) !=
            transport_len) {
            return;
        }
        checksum = (uint16_t *)(payload + transport_offset + 6u);
        /* An IPv4 UDP checksum of zero explicitly disables the checksum. */
        if (*checksum == 0u) {
            return;
        }
        hdr->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
        hdr->csum_start = (uint16_t)transport_offset;
        hdr->csum_offset = VIRTIO_NET_UDP_CSUM_OFFSET;
        /* VirtIO expects the pseudo-header sum in the checksum field. */
        *checksum = util_htons(virtio_net_ipv4_pseudo_sum(payload + ip_offset,
                                                          protocol,
                                                          (uint16_t)transport_len));
    }
}

static int virtio_net_scan(uintptr_t *base_out, uint32_t *irq_out, size_t start_index)
{
    static const uintptr_t candidates[] = {
        0x0A000000u, 0x0A000200u, 0x0A000400u, 0x0A000600u,
        0x0A000800u, 0x0A000A00u, 0x0A000C00u, 0x0A000E00u,
        0x0A001000u, 0x0A001200u, 0x0A001400u, 0x0A001600u,
        0x0A001800u, 0x0A001A00u, 0x0A001C00u, 0x0A001E00u
    };
    static const uint32_t irqs[] = {
        48u, 49u, 50u, 51u,
        52u, 53u, 54u, 55u,
        56u, 57u, 58u, 59u,
        60u, 61u, 62u, 63u
    };

    size_t found_count = 0u;
    for (size_t i = 0u; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
        uint32_t magic = virtio_mmio_read32(candidates[i], VIRTIO_MMIO_MAGIC_VALUE);
        if (magic == 0x74726976u) {
            uint32_t device_id = virtio_mmio_read32(candidates[i], VIRTIO_MMIO_DEVICE_ID);
            if (device_id == VIRTIO_ID_NET || device_id == 0u) {
                if (found_count == start_index) {
                    *base_out = candidates[i];
                    *irq_out = irqs[i];
                    uart_puts("[virtio-net] Detected device at base 0x");
                    uart_write_hex((unsigned long)candidates[i]);
                    uart_puts(", IRQ ");
                    uart_write_dec(irqs[i]);
                    uart_putc('\n');
                    return 0;
                }
                found_count++;
            }
        }
    }

    return -1;
}

static void virtio_net_prepare_rx(struct virtio_net_device *dev, size_t dev_idx)
{
    struct virtio_queue *queue = dev->rx_queue;
    struct vring_desc *desc = queue->desc;
    struct vring_avail *avail = queue->avail;
    for (uint16_t i = 0u; i < dev->rx_queue_size; ++i) {
        dev->rx_buffers[i] = &g_rx_buffer_storage[dev_idx][i][0];
        util_memset(dev->rx_buffers[i], 0, VIRTIO_NET_BUFFER_SIZE);
        cache_clean_range(dev->rx_buffers[i], VIRTIO_NET_BUFFER_SIZE);
        desc[i].addr = (uint64_t)(uintptr_t)dev->rx_buffers[i];
        desc[i].len = VIRTIO_NET_BUFFER_SIZE;
        desc[i].flags = VRING_DESC_F_WRITE;
        desc[i].next = 0u;
        avail->ring[i] = i;
    }
    avail->idx = dev->rx_queue_size;
    dev->rx_last_used = 0u;
    dev->rx_recycle_start = 0u;
    dev->rx_recycle_count = 0u;
    g_rx_completion_head[dev_idx] = 0u;
    g_rx_completion_tail[dev_idx] = 0u;
    g_rx_completion_count[dev_idx] = 0u;

    cache_clean_range(desc, sizeof(struct vring_desc) * dev->rx_queue_size);
    cache_clean_range(avail, sizeof(*avail));
    cache_clean_range(queue->used, sizeof(*queue->used));
}

static void virtio_net_prepare_tx(struct virtio_net_device *dev, size_t dev_idx)
{
    struct virtio_queue *queue = dev->tx_queue;
    struct vring_desc *desc = queue->desc;
    struct vring_avail *avail = queue->avail;
    for (uint16_t i = 0u; i < dev->tx_queue_size; ++i) {
        dev->tx_buffers[i] = &g_tx_buffer_storage[dev_idx][i][0];
        util_memset(dev->tx_buffers[i], 0, VIRTIO_NET_BUFFER_SIZE);
        desc[i].addr = 0u;
        desc[i].len = 0u;
        desc[i].flags = 0u;
        desc[i].next = 0u;
    }
    avail->idx = 0u;
    dev->tx_last_used = 0u;

    cache_clean_range(desc, sizeof(struct vring_desc) * dev->tx_queue_size);
    cache_clean_range(avail, sizeof(*avail));
    cache_clean_range(queue->used, sizeof(*queue->used));
}

static uint16_t virtio_net_read_rx_used_idx(struct virtio_net_device *dev)
{
    struct vring_used *used = dev->rx_queue->used;

    /* The device publishes used entries by updating idx after the entries. */
    cache_invalidate_range(&used->idx, sizeof(used->idx));
    return used->idx;
}

static void virtio_net_flush_rx_recycle(struct virtio_net_device *dev)
{
    struct vring_avail *avail;
    uint16_t queue_size;
    uint16_t first;
    uint16_t count;
    uint16_t first_chunk;

    if (dev == NULL || dev->rx_queue == NULL) {
        return;
    }

    count = dev->rx_recycle_count;
    if (count == 0u) {
        return;
    }

    queue_size = dev->rx_queue_size;
    if (queue_size == 0u) {
        dev->rx_recycle_count = 0u;
        return;
    }
    if (count > queue_size) {
        /* This indicates corrupted recycle state; bound cache maintenance. */
        uart_puts("[virtio-net] RX recycle batch overrun\n");
        count = queue_size;
    }

    avail = dev->rx_queue->avail;
    first = dev->rx_recycle_start;
    if (first >= queue_size) {
        first = 0u;
    }

    /* The pending batch can straddle the end of the avail ring. */
    first_chunk = (uint16_t)(queue_size - first);
    if (first_chunk > count) {
        first_chunk = count;
    }
    cache_clean_range_nosync(&avail->ring[first],
                             (size_t)first_chunk * sizeof(uint16_t));
    if (first_chunk < count) {
        cache_clean_range_nosync(&avail->ring[0],
                                 (size_t)(count - first_chunk) * sizeof(uint16_t));
    }

    cache_sync();
    /* Publish all ring entries before exposing the new avail index. */
    cache_clean_range_nosync(&avail->idx, sizeof(avail->idx));
    cache_sync();
    /* Keep the existing batch notify policy; spurious notify is permitted. */
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_RX_QUEUE);
    dev->rx_recycle_count = 0u;
}

static void virtio_net_recycle_rx_desc(struct virtio_net_device *dev,
                                       uint16_t desc_id)
{
    struct vring_avail *avail;
    uint16_t queue_size;
    uint16_t avail_slot;

    if (dev == NULL || dev->rx_queue == NULL) {
        return;
    }

    queue_size = dev->rx_queue_size;
    if (queue_size == 0u) {
        return;
    }

    avail = dev->rx_queue->avail;
    avail_slot = (uint16_t)(avail->idx % queue_size);
    if (dev->rx_recycle_count == 0u) {
        dev->rx_recycle_start = avail_slot;
    }
    avail->ring[avail_slot] = desc_id;
    avail->idx++;
    dev->rx_recycle_count++;

    if (dev->rx_recycle_count >= VIRTIO_NET_RX_RECYCLE_BATCH_SIZE) {
        virtio_net_flush_rx_recycle(dev);
    }
}

static void virtio_net_invalidate_rx_used_entries(struct virtio_net_device *dev,
                                                  uint16_t first_used,
                                                  uint16_t pending)
{
    struct vring_used *used = dev->rx_queue->used;
    uint16_t queue_size = dev->rx_queue_size;
    uint16_t cursor = first_used;

    while (pending != 0u) {
        uint16_t ring_index = (uint16_t)(cursor % queue_size);
        uint16_t chunk = (uint16_t)(queue_size - ring_index);
        if (chunk > pending) {
            chunk = pending;
        }

        cache_invalidate_range(&used->ring[ring_index],
                               (size_t)chunk * sizeof(struct vring_used_elem));
        cursor = (uint16_t)(cursor + chunk);
        pending = (uint16_t)(pending - chunk);
    }
}

static int virtio_net_drain_rx_used(struct virtio_net_device *dev, size_t dev_idx)
{
    struct virtio_queue *queue = dev->rx_queue;
    uint16_t queue_size = dev->rx_queue_size;
    uint16_t used_idx;
    uint16_t pending;
    int enqueued = 0;
    struct vring_used *used = queue->used;

    if (queue_size == 0u) {
        return 0;
    }

    used_idx = virtio_net_read_rx_used_idx(dev);
    if (dev->rx_last_used == used_idx) {
        return 0;
    }

    pending = (uint16_t)(used_idx - dev->rx_last_used);
    if (pending > queue_size) {
        /* More completions than descriptors means the used ring was
         * overwritten or its index is corrupt; do not spin around it. */
        uart_puts("[virtio-net] RX used ring overrun\n");
        dev->rx_last_used = used_idx;
        virtio_net_arm_rx_used_event(dev, used_idx);
        return -1;
    }

    /* Invalidate only entries published since the previous drain. */
    virtio_net_invalidate_rx_used_entries(dev, dev->rx_last_used, pending);

    while (1) {
        if (dev->rx_last_used == used_idx) {
            break;
        }
        uint16_t used_index = (uint16_t)(dev->rx_last_used % queue_size);
        struct vring_used_elem *elem = &used->ring[used_index];
        uint16_t desc_id = (uint16_t)elem->id;

        if (desc_id >= queue_size) {
            uart_puts("[virtio-net] RX descriptor index out of range\n");
            dev->rx_last_used++;
            continue;
        }

        if (g_rx_completion_count[dev_idx] >= queue_size) {
            uart_puts("[virtio-net] RX completion queue full\n");
            virtio_net_recycle_rx_desc(dev, desc_id);
            virtio_net_flush_rx_recycle(dev);
            dev->rx_last_used++;
            continue;
        }

        g_rx_completions[dev_idx][g_rx_completion_tail[dev_idx]].desc_id = desc_id;
        g_rx_completions[dev_idx][g_rx_completion_tail[dev_idx]].total_len = elem->len;
        g_rx_completion_tail[dev_idx] = (uint16_t)((g_rx_completion_tail[dev_idx] + 1u) % queue_size);
        g_rx_completion_count[dev_idx]++;
        enqueued++;

        dev->rx_last_used++;
    }

    virtio_net_arm_rx_used_event(dev, used_idx);

    return enqueued;
}

static int virtio_net_find_device_index(virtio_net_dev_t dev, size_t *dev_idx_out)
{
    if (dev == NULL || dev_idx_out == NULL) {
        return -1;
    }

    for (size_t i = 0u; i < g_device_count; ++i) {
        if (&g_devices[i] == dev) {
            *dev_idx_out = i;
            return 0;
        }
    }

    return -1;
}

/* Drain device-written RX completions from task context. */
int virtio_net_poll_rx_dev(virtio_net_dev_t dev)
{
    size_t dev_idx;

    if (dev == NULL || !dev->driver_ok ||
        virtio_net_find_device_index(dev, &dev_idx) != 0) {
        return -1;
    }

    return virtio_net_drain_rx_used(dev, dev_idx);
}

static int virtio_net_configure_queue(struct virtio_net_device *dev,
                                      uint32_t queue_index,
                                      struct virtio_queue *queue,
                                      uint16_t *queue_size_out)
{
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_SEL, queue_index);
    uint32_t queue_max = virtio_reg_read(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_max == 0u) {
        uart_puts("[virtio-net] Queue not available\n");
        return -1;
    }

    uint16_t queue_size = (uint16_t)queue_max;
    if (queue_size > VIRTIO_NET_QUEUE_SIZE) {
        queue_size = VIRTIO_NET_QUEUE_SIZE;
    }

    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NUM, queue_size);

    util_memset(queue->desc, 0, sizeof(struct vring_desc) * queue_size);
    util_memset(queue->avail, 0, sizeof(struct vring_avail));
    util_memset(queue->used, 0, sizeof(struct vring_used));

    cache_clean_range(queue->desc, sizeof(struct vring_desc) * queue_size);
    cache_clean_range(queue->avail, sizeof(struct vring_avail));
    cache_clean_range(queue->used, sizeof(struct vring_used));

    uintptr_t desc_addr = (uintptr_t)queue->desc;
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));

    uintptr_t avail_addr = (uintptr_t)queue->avail;
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail_addr);
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32));

    uintptr_t used_addr = (uintptr_t)queue->used;
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)used_addr);
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)(used_addr >> 32));

    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_READY, 1u);

    *queue_size_out = queue_size;
    return 0;
}

/* The TX path only needs the device-written used index to reclaim queue
 * capacity.  Invalidating the complete used ring here costs dozens of cache
 * lines per packet and provides no information used by the driver. */
static uint16_t virtio_net_read_tx_used_idx(struct virtio_net_device *dev)
{
    struct vring_used *used = dev->tx_queue->used;

    cache_invalidate_range(&used->idx, sizeof(used->idx));
    uint16_t used_idx = used->idx;

    if (dev->event_idx != 0u && dev->tx_queue->avail->used_event != used_idx) {
        /* Arm the next TX completion interrupt only after observing this one. */
        dev->tx_queue->avail->used_event = used_idx;
        cache_clean_range_nosync(&dev->tx_queue->avail->used_event,
                                 sizeof(dev->tx_queue->avail->used_event));
        cache_sync();
    }

    return used_idx;
}

/*
 * The router does not implement RX segmentation.  It can, however, accept
 * a device-supplied partial checksum because forwarded TCP/UDP packets are
 * re-initialised by virtio_net_prepare_tx_csum() before TX offload.  Reject
 * unsupported metadata rather than exposing it to the protocol path.
 */
static int virtio_net_rx_hdr_accepted(struct virtio_net_device *dev,
                                      uint16_t desc_id,
                                      uint32_t total_len)
{
    struct virtio_net_hdr *hdr;
    size_t payload_len;
    size_t checksum_pos;

    if (dev == NULL || desc_id >= dev->rx_queue_size ||
        total_len <= sizeof(struct virtio_net_hdr)) {
        return 0;
    }

    hdr = (struct virtio_net_hdr *)dev->rx_buffers[desc_id];
    payload_len = (size_t)total_len - sizeof(struct virtio_net_hdr);

    if (hdr->gso_type != VIRTIO_NET_HDR_GSO_NONE) {
        return 0;
    }

    if ((hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) == 0u) {
        return 1;
    }

    if (dev->rx_csum_offload == 0u) {
        return 0;
    }

    checksum_pos = (size_t)hdr->csum_start + (size_t)hdr->csum_offset;
    if (payload_len < sizeof(uint16_t) ||
        checksum_pos > payload_len - sizeof(uint16_t)) {
        return 0;
    }

    return 1;
}

static void virtio_net_arm_rx_used_event(struct virtio_net_device *dev,
                                         uint16_t used_idx)
{
    struct vring_avail *avail;

    if (dev == NULL || dev->event_idx == 0u) {
        return;
    }

    avail = dev->rx_queue->avail;
    if (avail->used_event == used_idx) {
        return;
    }

    /* Ask the device to interrupt again only after a new used entry appears. */
    avail->used_event = used_idx;
    cache_clean_range_nosync(&avail->used_event, sizeof(avail->used_event));
    cache_sync();
}

static int virtio_net_init_device(size_t dev_idx, uintptr_t base_addr, uint32_t irq)
{
    struct virtio_net_device *dev = &g_devices[dev_idx];

    util_memset(dev, 0, sizeof(*dev));

    dev->base = base_addr;
    dev->irq = irq;

    struct virtio_queue *rx_queue = &g_rx_queues[dev_idx];
    struct virtio_queue *tx_queue = &g_tx_queues[dev_idx];

    rx_queue->desc = g_rx_desc[dev_idx];
    rx_queue->avail = &g_rx_avail[dev_idx];
    rx_queue->used = (struct vring_used *)&g_rx_used_storage[dev_idx][0];

    tx_queue->desc = g_tx_desc[dev_idx];
    tx_queue->avail = &g_tx_avail[dev_idx];
    tx_queue->used = (struct vring_used *)&g_tx_used_storage[dev_idx][0];

    dev->rx_queue = rx_queue;
    dev->tx_queue = tx_queue;
    dev->rx_sem = OSSemCreate(0u);
    if (dev->rx_sem == NULL) {
        uart_puts("[virtio-net] Warning: failed to create RX semaphore\n");
    }

    uart_puts("[virtio-net] Initialising device ");
    uart_write_dec(dev_idx);
    uart_putc('\n');
    uart_puts("[virtio-net] Base 0x");
    uart_write_hex((unsigned long)base_addr);
    uart_puts(", IRQ ");
    uart_write_dec(irq);
    uart_putc('\n');

    uint32_t magic = virtio_reg_read(dev, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != 0x74726976u) {
        uart_puts("[virtio-net] Invalid magic\n");
        return -1;
    }

    uint32_t version = virtio_reg_read(dev, VIRTIO_MMIO_VERSION);
    uart_puts("[virtio-net] Version ");
    uart_write_dec(version);
    uart_putc('\n');

    if (version <= 1u) {
        uart_puts("[virtio-net] Configuring guest page size for legacy virtio\n");
        virtio_reg_write(dev, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096u);
    }

    uint32_t device_id = virtio_reg_read(dev, VIRTIO_MMIO_DEVICE_ID);
    uint32_t vendor_id = virtio_reg_read(dev, VIRTIO_MMIO_VENDOR_ID);
    log_hex32("[virtio-net] Device ID ", device_id);
    log_hex32("[virtio-net] Vendor ID ", vendor_id);

    if (device_id != VIRTIO_ID_NET && device_id != 0u) {
        uart_puts("[virtio-net] Device is not virtio-net\n");
        return -1;
    }

    /* Reset device status before negotiation */
    virtio_reg_write(dev, VIRTIO_MMIO_STATUS, 0u);

    uint32_t status_value = VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_reg_write(dev, VIRTIO_MMIO_STATUS, status_value);
    uint32_t status = virtio_reg_read(dev, VIRTIO_MMIO_STATUS);
    log_status("[virtio-net] ACKNOWLEDGE", status);

    status_value |= VIRTIO_STATUS_DRIVER;
    virtio_reg_write(dev, VIRTIO_MMIO_STATUS, status_value);
    status = virtio_reg_read(dev, VIRTIO_MMIO_STATUS);
    log_status("[virtio-net] DRIVER", status);

    uint32_t features_lo;
    uint32_t features_hi;
    virtio_reg_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0u);
    features_lo = virtio_reg_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    virtio_reg_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1u);
    features_hi = virtio_reg_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    log_hex32("[virtio-net] Host features[31:0] ", features_lo);
    log_hex32("[virtio-net] Host features[63:32] ", features_hi);

    uint32_t driver_features_lo = 0u;
    uint32_t driver_features_hi = 0u;
#if VIRTIO_NET_TX_CSUM_OFFLOAD
    if (features_lo & (1u << VIRTIO_NET_F_CSUM)) {
        driver_features_lo |= (1u << VIRTIO_NET_F_CSUM);
        dev->tx_csum_offload = 1u;
        uart_puts("[virtio-net] TX checksum offload enabled\n");
    } else {
        uart_puts("[virtio-net] TX checksum offload unavailable\n");
    }
#endif
#if VIRTIO_NET_RX_CSUM_OFFLOAD
    if (features_lo & (1u << VIRTIO_NET_F_GUEST_CSUM)) {
        driver_features_lo |= (1u << VIRTIO_NET_F_GUEST_CSUM);
        dev->rx_csum_offload = 1u;
        uart_puts("[virtio-net] RX checksum offload enabled\n");
    } else {
        uart_puts("[virtio-net] RX checksum offload unavailable\n");
    }
#endif
#if VIRTIO_NET_EVENT_IDX
    if (features_lo & (1u << VIRTIO_RING_F_EVENT_IDX)) {
        driver_features_lo |= (1u << VIRTIO_RING_F_EVENT_IDX);
        dev->event_idx = 1u;
        uart_puts("[virtio-net] Virtqueue used-event IRQ suppression enabled\n");
    } else {
        uart_puts("[virtio-net] Virtqueue event-index unavailable\n");
    }
#endif
    if (features_lo & (1u << VIRTIO_NET_F_MAC)) {
        driver_features_lo |= (1u << VIRTIO_NET_F_MAC);
    }
    if (features_hi & (1u << (VIRTIO_F_VERSION_1 - 32u))) {
        driver_features_hi |= (1u << (VIRTIO_F_VERSION_1 - 32u));
    }

    virtio_reg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0u);
    virtio_reg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, driver_features_lo);
    virtio_reg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1u);
    virtio_reg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, driver_features_hi);

    status_value |= VIRTIO_STATUS_FEATURES_OK;
    virtio_reg_write(dev, VIRTIO_MMIO_STATUS, status_value);

    status = virtio_reg_read(dev, VIRTIO_MMIO_STATUS);
    if ((status & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        uart_puts("[virtio-net] Warning: FEATURES_OK not acknowledged\n");
    }
    log_status("[virtio-net] FEATURES_OK", status);

    struct virtio_net_config *config = (struct virtio_net_config *)(dev->base + VIRTIO_MMIO_CONFIG);
    for (size_t i = 0u; i < sizeof(dev->mac); ++i) {
        dev->mac[i] = config->mac[i];
    }

    uart_puts("[virtio-net] MAC ");
    for (size_t i = 0u; i < sizeof(dev->mac); ++i) {
        log_hex8(dev->mac[i]);
        if (i + 1u < sizeof(dev->mac)) {
            uart_putc(':');
        }
    }
    uart_putc('\n');

    if (virtio_net_configure_queue(dev, VIRTIO_NET_RX_QUEUE, dev->rx_queue, &dev->rx_queue_size) != 0) {
        return -1;
    }
    virtio_net_prepare_rx(dev, dev_idx);
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_RX_QUEUE);

    if (virtio_net_configure_queue(dev, VIRTIO_NET_TX_QUEUE, dev->tx_queue, &dev->tx_queue_size) != 0) {
        return -1;
    }
    virtio_net_prepare_tx(dev, dev_idx);

    status_value |= VIRTIO_STATUS_DRIVER_OK;
    virtio_reg_write(dev, VIRTIO_MMIO_STATUS, status_value);

    status = virtio_reg_read(dev, VIRTIO_MMIO_STATUS);
    log_status("[virtio-net] DRIVER_OK", status);

    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_NET_RX_QUEUE);
    uint32_t rx_queue_max = virtio_reg_read(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_NET_TX_QUEUE);
    uint32_t tx_queue_max = virtio_reg_read(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    uart_puts("[virtio-net] Queue sizes: RX=");
    uart_write_dec(rx_queue_max);
    uart_puts(" TX=");
    uart_write_dec(tx_queue_max);
    uart_putc('\n');

    dev->driver_ok = 1u;

    /* Register and enable VirtIO network interrupt */
    uart_puts("[virtio-net] Registering interrupt handler for IRQ ");
    uart_write_dec(irq);
    uart_putc('\n');

    BSP_IntVectSet(irq, 0u, 0u, virtio_net_interrupt_handler);
    BSP_IntSrcEn(irq);

    /* Enable interrupts on the device */
    uart_puts("[virtio-net] Interrupts enabled on device ");
    uart_write_dec(dev_idx);
    uart_putc('\n');

    return 0;
}

int virtio_net_init_all(void)
{
    uart_puts("[virtio-net] Scanning for devices...\n");

    if (g_rx_global_sem == NULL) {
        g_rx_global_sem = OSSemCreate(0u);
        if (g_rx_global_sem == NULL) {
            uart_puts("[virtio-net] Warning: failed to create global RX semaphore\n");
        }
    }

    g_device_count = 0u;

    for (size_t i = 0u; i < VIRTIO_NET_MAX_DEVICES; ++i) {
        uintptr_t base = 0u;
        uint32_t irq = 0u;

        if (virtio_net_scan(&base, &irq, i) == 0) {
            if (virtio_net_init_device(i, base, irq) == 0) {
                g_device_count++;
                uart_puts("[virtio-net] Device ");
                uart_write_dec(i);
                uart_puts(" initialized successfully\n");
            } else {
                uart_puts("[virtio-net] Failed to initialize device ");
                uart_write_dec(i);
                uart_putc('\n');
                break;
            }
        } else {
            break;
        }
    }

    if (g_device_count > 0u) {
        /* Set legacy pointer to device 0 for backward compatibility */
        g_dev = &g_devices[0];
        uart_puts("[virtio-net] Total devices initialized: ");
        uart_write_dec(g_device_count);
        uart_putc('\n');
        return 0;
    } else {
        uart_puts("[virtio-net] No devices found\n");
        return -1;
    }
}

int virtio_net_init(uintptr_t base_addr, uint32_t irq)
{
    uintptr_t detected_base = base_addr;
    uint32_t detected_irq = irq;

    if (g_rx_global_sem == NULL) {
        g_rx_global_sem = OSSemCreate(0u);
        if (g_rx_global_sem == NULL) {
            uart_puts("[virtio-net] Warning: failed to create global RX semaphore\n");
        }
    }

    if (virtio_net_scan(&detected_base, &detected_irq, 0u) == 0) {
        base_addr = detected_base;
        irq = detected_irq;
    } else {
        if (base_addr == 0u) {
            base_addr = VIRTIO_NET_MMIO_BASE_DEFAULT;
        }
        if (irq == 0u) {
            irq = VIRTIO_NET_DEFAULT_IRQ;
        }
        uart_puts("[virtio-net] Using default base/IRQ\n");
    }

    if (virtio_net_init_device(0u, base_addr, irq) != 0) {
        return -1;
    }

    g_device_count = 1u;
    g_dev = &g_devices[0];

    return 0;
}

virtio_net_dev_t virtio_net_get_device(size_t index)
{
    if (index >= g_device_count) {
        return NULL;
    }
    return &g_devices[index];
}

size_t virtio_net_get_device_count(void)
{
    return g_device_count;
}

int virtio_net_tx_csum_offload_enabled_dev(virtio_net_dev_t dev)
{
    return (dev != NULL && dev->driver_ok && dev->tx_csum_offload != 0u) ? 1 : 0;
}

int virtio_net_send_frame_dev(virtio_net_dev_t dev, const uint8_t *frame, size_t length)
{
    if (dev == NULL || !dev->driver_ok) {
        uart_puts("[virtio-net] Invalid device or driver not initialised\n");
        return -1;
    }

    if (length == 0u || length > VIRTIO_NET_MAX_FRAME_SIZE || frame == NULL) {
        uart_puts("[virtio-net] Invalid frame length\n");
        return -1;
    }

    struct virtio_queue *queue = dev->tx_queue;
    struct vring_avail *avail = queue->avail;
    struct vring_desc *desc = queue->desc;
    uint16_t in_flight, available_slots;

    /* Check and update completed TX descriptors */
    dev->tx_last_used = virtio_net_read_tx_used_idx(dev);

    /* Calculate in-flight packets (handle wrap-around) */
    in_flight = (uint16_t)((avail->idx - dev->tx_last_used) & 0xFFFFu);
    available_slots = (uint16_t)(dev->tx_queue_size - in_flight);

    /* If queue is critically full, poll for completions before giving up */
    if (available_slots < 4u) {
        uint16_t retries = 0u;
        while (available_slots < 4u && retries < 100u) {
            /* Force check the used ring again */
            dev->tx_last_used = virtio_net_read_tx_used_idx(dev);
            in_flight = (uint16_t)((avail->idx - dev->tx_last_used) & 0xFFFFu);
            available_slots = (uint16_t)(dev->tx_queue_size - in_flight);

            if (available_slots >= 4u) {
                break;
            }

            retries++;
            /* Small delay to let device process */
            if (retries % 10u == 0u) {
                /* Re-read used ring to catch any completions */
                dev->tx_last_used = virtio_net_read_tx_used_idx(dev);
                in_flight = (uint16_t)((avail->idx - dev->tx_last_used) & 0xFFFFu);
                available_slots = (uint16_t)(dev->tx_queue_size - in_flight);
            }
        }

        if (available_slots < 2u) {
            uart_puts("[virtio-net] TX queue full\n");
            return -1;
        }
    }

    uint16_t idx = (uint16_t)(avail->idx % dev->tx_queue_size);
    uint8_t *buffer = dev->tx_buffers[idx];
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)buffer;

    util_memset(hdr, 0, sizeof(*hdr));
    util_memcpy(buffer + sizeof(*hdr), frame, length);
    virtio_net_prepare_tx_csum(dev, hdr, buffer + sizeof(*hdr), length);
    cache_clean_range_nosync(buffer, length + sizeof(*hdr));

    desc[idx].addr = (uint64_t)(uintptr_t)buffer;
    desc[idx].len = (uint32_t)(length + sizeof(*hdr));
    desc[idx].flags = 0u;
    desc[idx].next = 0u;
    cache_clean_range_nosync(&desc[idx], sizeof(desc[idx]));

    avail->ring[idx] = idx;
    avail->idx++;
    cache_clean_range_nosync(&avail->ring[idx], sizeof(avail->ring[idx]));
    cache_clean_range_nosync(&avail->idx, sizeof(avail->idx));

    dev->tx_batch_count++;
    if (dev->tx_batch_count >= VIRTIO_NET_TX_BATCH_SIZE) {
        cache_sync();
        /* Keep the existing batch notify policy; spurious notify is permitted. */
        virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_TX_QUEUE);
        dev->tx_batch_count = 0u;
    }

    return 0;
}

void virtio_net_tx_flush_dev(size_t dev_idx)
{
    if (dev_idx >= g_device_count) {
        return;
    }
    struct virtio_net_device *dev = &g_devices[dev_idx];
    if (dev->tx_batch_count > 0u) {
        cache_sync();
        /* Keep the existing batch notify policy; spurious notify is permitted. */
        virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_TX_QUEUE);
        dev->tx_batch_count = 0u;
    }
}

void virtio_net_tx_flush_device(virtio_net_dev_t dev)
{
    if (dev == NULL) {
        return;
    }

    for (size_t dev_idx = 0u; dev_idx < g_device_count; ++dev_idx) {
        if (&g_devices[dev_idx] == dev) {
            virtio_net_tx_flush_dev(dev_idx);
            return;
        }
    }
}

void virtio_net_rx_flush_dev(size_t dev_idx)
{
    if (dev_idx >= VIRTIO_NET_MAX_DEVICES) {
        return;
    }
    struct virtio_net_device *dev = &g_devices[dev_idx];
    virtio_net_flush_rx_recycle(dev);
}

int virtio_net_poll_frame_dev(virtio_net_dev_t dev, uint8_t *out_frame, size_t *out_length)
{
    if (dev == NULL || !dev->driver_ok) {
        return -1;
    }

    /* Preserve the direct polling API used by the standalone tests. */
    if (!virtio_net_has_pending_rx_dev(dev)) {
        (void)virtio_net_poll_rx_dev(dev);
    }

    size_t dev_idx;
    if (virtio_net_find_device_index(dev, &dev_idx) != 0) {
        return -1;
    }

    OS_CPU_SR cpu_sr;
    uint16_t desc_id;
    uint32_t total_len;

    OS_ENTER_CRITICAL();
    if (g_rx_completion_count[dev_idx] == 0u) {
        OS_EXIT_CRITICAL();
        return 0;
    }

    desc_id = g_rx_completions[dev_idx][g_rx_completion_head[dev_idx]].desc_id;
    total_len = g_rx_completions[dev_idx][g_rx_completion_head[dev_idx]].total_len;
    g_rx_completion_head[dev_idx] = (uint16_t)((g_rx_completion_head[dev_idx] + 1u) % dev->rx_queue_size);
    g_rx_completion_count[dev_idx]--;
    OS_EXIT_CRITICAL();

    if (desc_id >= dev->rx_queue_size) {
        uart_puts("[virtio-net] RX completion descriptor out of range\n");
        return -1;
    }

    size_t payload_len = 0u;
    if (total_len > sizeof(struct virtio_net_hdr)) {
        payload_len = total_len - sizeof(struct virtio_net_hdr);
        if (payload_len > VIRTIO_NET_MAX_FRAME_SIZE) {
            payload_len = VIRTIO_NET_MAX_FRAME_SIZE;
        }

        cache_invalidate_range(dev->rx_buffers[desc_id], total_len);

        if (!virtio_net_rx_hdr_accepted(dev, desc_id, total_len)) {
            uart_puts("[virtio-net] Unsupported RX checksum/GSO metadata\n");
            virtio_net_recycle_rx_desc(dev, desc_id);
            if (!virtio_net_has_pending_rx_dev(dev)) {
                virtio_net_flush_rx_recycle(dev);
            }
            if (out_length != NULL) {
                *out_length = 0u;
            }
            return -1;
        }

        if (out_frame != NULL && out_length != NULL) {
            util_memcpy(out_frame,
                        dev->rx_buffers[desc_id] + sizeof(struct virtio_net_hdr),
                        payload_len);
            *out_length = payload_len;
        }
    } else {
        if (out_length != NULL) {
            *out_length = 0u;
        }
    }

    virtio_net_recycle_rx_desc(dev, desc_id);
    /* Direct poll callers may not call rx_flush_dev() themselves. */
    if (!virtio_net_has_pending_rx_dev(dev)) {
        virtio_net_flush_rx_recycle(dev);
    }

    return (payload_len > 0u) ? 1 : 0;
}

uint8_t *virtio_net_peek_rx_buffer_dev(virtio_net_dev_t dev, size_t *out_len, uint16_t *out_desc_id)
{
    if (dev == NULL || !dev->driver_ok || out_len == NULL || out_desc_id == NULL) {
        return NULL;
    }

    /* Find device index */
    size_t dev_idx = 0u;
    for (dev_idx = 0u; dev_idx < g_device_count; ++dev_idx) {
        if (&g_devices[dev_idx] == dev) {
            break;
        }
    }
    if (dev_idx >= g_device_count) {
        return NULL;
    }

    OS_CPU_SR cpu_sr;
    uint16_t desc_id;
    uint32_t total_len;

    OS_ENTER_CRITICAL();
    if (g_rx_completion_count[dev_idx] == 0u) {
        OS_EXIT_CRITICAL();
        return NULL;
    }

    desc_id = g_rx_completions[dev_idx][g_rx_completion_head[dev_idx]].desc_id;
    total_len = g_rx_completions[dev_idx][g_rx_completion_head[dev_idx]].total_len;
    OS_EXIT_CRITICAL();

    if (desc_id >= dev->rx_queue_size) {
        return NULL;
    }

    size_t payload_len = 0u;
    if (total_len > sizeof(struct virtio_net_hdr)) {
        payload_len = total_len - sizeof(struct virtio_net_hdr);
        if (payload_len > VIRTIO_NET_MAX_FRAME_SIZE) {
            payload_len = VIRTIO_NET_MAX_FRAME_SIZE;
        }
        cache_invalidate_range(dev->rx_buffers[desc_id], total_len);

        if (!virtio_net_rx_hdr_accepted(dev, desc_id, total_len)) {
            uart_puts("[virtio-net] Unsupported RX checksum/GSO metadata\n");
            virtio_net_release_rx_buffer_dev(dev, desc_id);
            *out_len = 0u;
            *out_desc_id = desc_id;
            return NULL;
        }
    }

    *out_len = payload_len;
    *out_desc_id = desc_id;
    return dev->rx_buffers[desc_id] + sizeof(struct virtio_net_hdr);
}

void virtio_net_release_rx_buffer_dev(virtio_net_dev_t dev, uint16_t desc_id)
{
    if (dev == NULL || !dev->driver_ok) {
        return;
    }

    /* Find device index */
    size_t dev_idx = 0u;
    for (dev_idx = 0u; dev_idx < g_device_count; ++dev_idx) {
        if (&g_devices[dev_idx] == dev) {
            break;
        }
    }
    if (dev_idx >= g_device_count) {
        return;
    }

    OS_CPU_SR cpu_sr;

    OS_ENTER_CRITICAL();
    if (g_rx_completion_count[dev_idx] == 0u) {
        OS_EXIT_CRITICAL();
        return;
    }
    /* Verify the head matches the released descriptor */
    uint16_t head_desc_id = g_rx_completions[dev_idx][g_rx_completion_head[dev_idx]].desc_id;
    if (head_desc_id != desc_id) {
        OS_EXIT_CRITICAL();
        uart_puts("[virtio-net] RX release desc_id mismatch\n");
        return;
    }
    g_rx_completion_head[dev_idx] = (uint16_t)((g_rx_completion_head[dev_idx] + 1u) % dev->rx_queue_size);
    g_rx_completion_count[dev_idx]--;
    OS_EXIT_CRITICAL();

    virtio_net_recycle_rx_desc(dev, desc_id);
}

const uint8_t *virtio_net_get_mac_dev(virtio_net_dev_t dev)
{
    if (dev == NULL) {
        return NULL;
    }
    return dev->mac;
}

void virtio_net_enable_interrupts_dev(virtio_net_dev_t dev)
{
    if (dev == NULL) {
        return;
    }
    /* The device should automatically send interrupts when buffers are used */
    /* No additional configuration needed for basic VirtIO MMIO */
    uart_puts("[virtio-net] Interrupts enabled on device\n");
}

int virtio_net_has_pending_rx_dev(virtio_net_dev_t dev)
{
    if (dev == NULL) {
        return 0;
    }

    size_t dev_idx = 0u;
    for (dev_idx = 0u; dev_idx < g_device_count; ++dev_idx) {
        if (&g_devices[dev_idx] == dev) {
            break;
        }
    }
    if (dev_idx >= g_device_count) {
        return 0;
    }

    return (g_rx_completion_count[dev_idx] > 0u) ? 1 : 0;
}

INT8U virtio_net_wait_rx_dev(virtio_net_dev_t dev, INT16U timeout_ms)
{
    if (dev == NULL) {
        return OS_ERR_PEVENT_NULL;
    }

    if (dev->rx_sem == NULL) {
        if (timeout_ms == 0u) {
            while (!virtio_net_has_pending_rx_dev(dev)) {
                OSTimeDly(1u);
            }
            return OS_ERR_NONE;
        }

        INT32U start = OSTimeGet();
        INT32U timeout_ticks = virtio_ms_to_ticks(timeout_ms);
        if (timeout_ticks == 0u) {
            timeout_ticks = 1u;
        }

        while ((OSTimeGet() - start) < timeout_ticks) {
            if (virtio_net_has_pending_rx_dev(dev)) {
                return OS_ERR_NONE;
            }
            OSTimeDly(1u);
        }
        return OS_ERR_TIMEOUT;
    }

    INT8U err;
    INT32U ticks = virtio_ms_to_ticks(timeout_ms);
    OSSemPend(dev->rx_sem, ticks, &err);
    return err;
}

INT8U virtio_net_wait_rx_any(INT16U timeout_ms)
{
    if (g_rx_global_sem == NULL) {
        if (timeout_ms == 0u) {
            while (!virtio_net_has_pending_rx()) {
                OSTimeDly(1u);
            }
            return OS_ERR_NONE;
        }

        INT32U start = OSTimeGet();
        INT32U timeout_ticks = virtio_ms_to_ticks(timeout_ms);
        if (timeout_ticks == 0u) {
            timeout_ticks = 1u;
        }

        while ((OSTimeGet() - start) < timeout_ticks) {
            if (virtio_net_has_pending_rx()) {
                return OS_ERR_NONE;
            }
            OSTimeDly(1u);
        }
        return OS_ERR_TIMEOUT;
    }

    INT8U err;
    INT32U ticks = virtio_ms_to_ticks(timeout_ms);
    OSSemPend(g_rx_global_sem, ticks, &err);
    return err;
}

int virtio_net_self_test_registers(void)
{
    if (g_dev == NULL || !g_dev->driver_ok) {
        uart_puts("[virtio-net] Driver not initialised\n");
        return -1;
    }

    virtio_reg_write(g_dev, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_NET_RX_QUEUE);
    uint32_t rx_max = virtio_reg_read(g_dev, VIRTIO_MMIO_QUEUE_NUM_MAX);

    virtio_reg_write(g_dev, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_NET_TX_QUEUE);
    uint32_t tx_max = virtio_reg_read(g_dev, VIRTIO_MMIO_QUEUE_NUM_MAX);

    uart_puts("[virtio-net] Queue capability: RX max ");
    uart_write_dec(rx_max);
    uart_puts(", TX max ");
    uart_write_dec(tx_max);
    uart_putc('\n');

    if (rx_max == 0u || tx_max == 0u) {
        uart_puts("[virtio-net] Register read/write test failed\n");
        return -1;
    }

    uart_puts("[virtio-net] Register read/write test passed\n");
    return 0;
}

int virtio_net_send_frame(const uint8_t *frame, size_t length)
{
    if (g_dev == NULL) {
        return -1;
    }
    return virtio_net_send_frame_dev(g_dev, frame, length);
}

int virtio_net_poll_frame(uint8_t *out_frame, size_t *out_length)
{
    if (g_dev == NULL) {
        return -1;
    }
    return virtio_net_poll_frame_dev(g_dev, out_frame, out_length);
}

const uint8_t *virtio_net_get_mac(void)
{
    if (g_dev == NULL) {
        return NULL;
    }
    return virtio_net_get_mac_dev(g_dev);
}

void virtio_net_debug_dump_status(void)
{
    if (g_dev == NULL) {
        return;
    }

    uint32_t status = virtio_reg_read(g_dev, VIRTIO_MMIO_STATUS);
    log_status("[virtio-net] STATUS", status);

    uint32_t interrupt_status = virtio_reg_read(g_dev, VIRTIO_MMIO_INTERRUPT_STATUS);
    log_status("[virtio-net] INTERRUPT_STATUS", interrupt_status);
}

/* VirtIO network interrupt handler */
void virtio_net_interrupt_handler(uint32_t int_id)
{
    uint32_t interrupt_status;
    size_t dev_idx;
    struct virtio_net_device *dev = NULL;

    /* Find which device triggered the interrupt based on IRQ number.
     * Scan all slots (not just g_device_count) because the interrupt can fire
     * during device init before g_device_count is incremented by the caller. */
    for (dev_idx = 0u; dev_idx < VIRTIO_NET_MAX_DEVICES; ++dev_idx) {
        if (g_devices[dev_idx].irq == int_id) {
            dev = &g_devices[dev_idx];
            break;
        }
    }

    if (dev == NULL || !dev->driver_ok) {
        return;
    }

    /* Keep the RX ring out of the ISR in deferred mode.  The non-deferred
     * build intentionally retains the original drain-before-ACK ordering so
     * the benchmark compares only the RX work placement. */
    interrupt_status = virtio_reg_read(dev, VIRTIO_MMIO_INTERRUPT_STATUS);

    if (interrupt_status == 0u) {
        return;
    }

#if VIRTIO_NET_RX_DEFER_POLL
    if (interrupt_status & 0x1u) {  /* Used buffer notification */
        if (dev->tx_queue != NULL) {
            dev->tx_last_used = virtio_net_read_tx_used_idx(dev);
        }

        /* Acknowledge before waking the task; it owns RX used-ring polling. */
        virtio_reg_write(dev, VIRTIO_MMIO_INTERRUPT_ACK, interrupt_status);
        if (dev->rx_sem != NULL) {
            OSSemPost(dev->rx_sem);
        }
        if (g_rx_global_sem != NULL) {
            OSSemPost(g_rx_global_sem);
        }
    } else {
        virtio_reg_write(dev, VIRTIO_MMIO_INTERRUPT_ACK, interrupt_status);
    }
#else
    if (interrupt_status & 0x1u) {  /* Used buffer notification */
        if (dev->tx_queue != NULL) {
            dev->tx_last_used = virtio_net_read_tx_used_idx(dev);
        }

        /* Baseline: drain the RX used ring in the ISR before acknowledging. */
        if (virtio_net_drain_rx_used(dev, dev_idx) > 0) {
            if (dev->rx_sem != NULL) {
                OSSemPost(dev->rx_sem);
            }
            if (g_rx_global_sem != NULL) {
                OSSemPost(g_rx_global_sem);
            }
        }
    }
    virtio_reg_write(dev, VIRTIO_MMIO_INTERRUPT_ACK, interrupt_status);
#endif
}

/* Check if there are pending RX packets */
int virtio_net_has_pending_rx(void)
{
    for (size_t i = 0u; i < g_device_count; ++i) {
        if (g_rx_completion_count[i] > 0u) {
            return 1;
        }
    }
    return 0;
}

/* Enable interrupts on the VirtIO device */
void virtio_net_enable_interrupts(void)
{
    if (g_dev == NULL) {
        return;
    }
    virtio_net_enable_interrupts_dev(g_dev);
}
