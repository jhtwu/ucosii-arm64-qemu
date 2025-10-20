#include "virtio_net.h"

#include "mmio.h"
#include "uart.h"
#include "timer.h"
#include "lib.h"
#include "bsp_int.h"

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

#define VIRTIO_NET_F_MAC                5u

#define VRING_DESC_F_NEXT               0x01u
#define VRING_DESC_F_WRITE              0x02u

#define VIRTIO_NET_RX_QUEUE             0u
#define VIRTIO_NET_TX_QUEUE             1u

#define VIRTIO_NET_QUEUE_SIZE           8u
#define VIRTIO_NET_BUFFER_SIZE          2048u

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
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
    struct vring_desc desc[VIRTIO_NET_QUEUE_SIZE];
    struct vring_avail avail;
    struct vring_used used;
} __attribute__((aligned(4096)));

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
    struct virtio_queue *rx_queue;
    struct virtio_queue *tx_queue;
    uint8_t *rx_buffers[VIRTIO_NET_QUEUE_SIZE];
    uint8_t *tx_buffers[VIRTIO_NET_QUEUE_SIZE];
};

/* Multiple device support */
static struct virtio_net_device g_devices[VIRTIO_NET_MAX_DEVICES];
static struct virtio_queue g_rx_queues[VIRTIO_NET_MAX_DEVICES];
static struct virtio_queue g_tx_queues[VIRTIO_NET_MAX_DEVICES];
static size_t g_device_count = 0u;

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

static inline uint32_t virtio_mmio_read32(uintptr_t base, uint32_t offset)
{
    uint32_t value = mmio_read32(base + offset);
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    return value;
}

static inline void virtio_mmio_write32(uintptr_t base, uint32_t offset, uint32_t value)
{
    __asm__ volatile("dsb sy" ::: "memory");
    mmio_write32(base + offset, value);
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
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
    for (uint16_t i = 0u; i < dev->rx_queue_size; ++i) {
        dev->rx_buffers[i] = &g_rx_buffer_storage[dev_idx][i][0];
        util_memset(dev->rx_buffers[i], 0, VIRTIO_NET_BUFFER_SIZE);
        queue->desc[i].addr = (uint64_t)(uintptr_t)dev->rx_buffers[i];
        queue->desc[i].len = VIRTIO_NET_BUFFER_SIZE;
        queue->desc[i].flags = VRING_DESC_F_WRITE;
        queue->desc[i].next = 0u;
        queue->avail.ring[i] = i;
    }
    queue->avail.idx = dev->rx_queue_size;
    dev->rx_last_used = 0u;
    g_rx_completion_head[dev_idx] = 0u;
    g_rx_completion_tail[dev_idx] = 0u;
    g_rx_completion_count[dev_idx] = 0u;
}

static void virtio_net_prepare_tx(struct virtio_net_device *dev, size_t dev_idx)
{
    struct virtio_queue *queue = dev->tx_queue;
    for (uint16_t i = 0u; i < dev->tx_queue_size; ++i) {
        dev->tx_buffers[i] = &g_tx_buffer_storage[dev_idx][i][0];
        util_memset(dev->tx_buffers[i], 0, VIRTIO_NET_BUFFER_SIZE);
        queue->desc[i].addr = 0u;
        queue->desc[i].len = 0u;
        queue->desc[i].flags = 0u;
        queue->desc[i].next = 0u;
    }
    queue->avail.idx = 0u;
    dev->tx_last_used = 0u;
}

static void virtio_net_handle_rx_used(struct virtio_net_device *dev, size_t dev_idx)
{
    struct virtio_queue *queue = dev->rx_queue;
    uint16_t queue_size = dev->rx_queue_size;
    uint8_t notify_device = 0u;

    while (dev->rx_last_used != queue->used.idx) {
        uint16_t used_index = (uint16_t)(dev->rx_last_used % queue_size);
        struct vring_used_elem *elem = &queue->used.ring[used_index];
        uint16_t desc_id = (uint16_t)elem->id;

        if (desc_id >= queue_size) {
            uart_puts("[virtio-net] RX descriptor index out of range\n");
            dev->rx_last_used++;
            continue;
        }

        if (g_rx_completion_count[dev_idx] >= queue_size) {
            uart_puts("[virtio-net] RX completion queue full\n");
            queue->avail.ring[queue->avail.idx % queue_size] = desc_id;
            queue->avail.idx++;
            dev->rx_last_used++;
            notify_device = 1u;
            continue;
        }

        g_rx_completions[dev_idx][g_rx_completion_tail[dev_idx]].desc_id = desc_id;
        g_rx_completions[dev_idx][g_rx_completion_tail[dev_idx]].total_len = elem->len;
        g_rx_completion_tail[dev_idx] = (uint16_t)((g_rx_completion_tail[dev_idx] + 1u) % queue_size);
        g_rx_completion_count[dev_idx]++;

        dev->rx_last_used++;
    }

    if (notify_device != 0u) {
        virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_RX_QUEUE);
    }
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

    util_memset(queue, 0, sizeof(*queue));

    uintptr_t desc_addr = (uintptr_t)&queue->desc[0];
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));

    uintptr_t avail_addr = (uintptr_t)&queue->avail;
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail_addr);
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32));

    uintptr_t used_addr = (uintptr_t)&queue->used;
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)used_addr);
    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)(used_addr >> 32));

    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_READY, 1u);

    *queue_size_out = queue_size;
    return 0;
}

static int virtio_net_init_device(size_t dev_idx, uintptr_t base_addr, uint32_t irq)
{
    struct virtio_net_device *dev = &g_devices[dev_idx];

    util_memset(dev, 0, sizeof(*dev));

    dev->base = base_addr;
    dev->irq = irq;
    dev->rx_queue = &g_rx_queues[dev_idx];
    dev->tx_queue = &g_tx_queues[dev_idx];

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

    uint32_t device_id = virtio_reg_read(dev, VIRTIO_MMIO_DEVICE_ID);
    uint32_t vendor_id = virtio_reg_read(dev, VIRTIO_MMIO_VENDOR_ID);
    log_hex32("[virtio-net] Device ID ", device_id);
    log_hex32("[virtio-net] Vendor ID ", vendor_id);

    if (device_id != VIRTIO_ID_NET && device_id != 0u) {
        uart_puts("[virtio-net] Device is not virtio-net\n");
        return -1;
    }

    uint64_t par;
    __asm__ volatile("at s1e1r, %0" :: "r"(dev->base));
    __asm__ volatile("mrs %0, par_el1" : "=r"(par));
    uart_puts("[virtio-net] PAR_EL1 = 0x");
    uart_write_hex((unsigned long)par);
    uart_putc('\n');

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

    uint32_t features;
    virtio_reg_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0u);
    features = virtio_reg_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    log_hex32("[virtio-net] Host features ", features);

    uint32_t driver_features = 0u;
    if (features & (1u << VIRTIO_NET_F_MAC)) {
        driver_features |= (1u << VIRTIO_NET_F_MAC);
    }

    virtio_reg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0u);
    virtio_reg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, driver_features);

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

    /* Reclaim completed TX descriptors before enqueueing */
    uint16_t used_idx = queue->used.idx;
    if (dev->tx_last_used != used_idx) {
        dev->tx_last_used = used_idx;
    }

    uint16_t outstanding = (uint16_t)(queue->avail.idx - dev->tx_last_used);
    if (outstanding >= dev->tx_queue_size) {
        uart_puts("[virtio-net] TX queue full\n");
        return -1;
    }

    uint16_t idx = (uint16_t)(queue->avail.idx % dev->tx_queue_size);
    uint8_t *buffer = dev->tx_buffers[idx];
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)buffer;

    util_memset(hdr, 0, sizeof(*hdr));
    util_memcpy(buffer + sizeof(*hdr), frame, length);

    queue->desc[idx].addr = (uint64_t)(uintptr_t)buffer;
    queue->desc[idx].len = (uint32_t)(length + sizeof(*hdr));
    queue->desc[idx].flags = 0u;
    queue->desc[idx].next = 0u;

    queue->avail.ring[idx] = idx;
    queue->avail.idx++;

    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_TX_QUEUE);

    return 0;
}

int virtio_net_poll_frame_dev(virtio_net_dev_t dev, uint8_t *out_frame, size_t *out_length)
{
    if (dev == NULL || !dev->driver_ok) {
        return -1;
    }

    /* Find device index */
    size_t dev_idx = 0u;
    for (dev_idx = 0u; dev_idx < g_device_count; ++dev_idx) {
        if (&g_devices[dev_idx] == dev) {
            break;
        }
    }
    if (dev_idx >= g_device_count) {
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

    dev->rx_queue->avail.ring[dev->rx_queue->avail.idx % dev->rx_queue_size] = desc_id;
    dev->rx_queue->avail.idx++;

    virtio_reg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_RX_QUEUE);

    return (payload_len > 0u) ? 1 : 0;
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

    /* Find device index */
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
    int result = virtio_net_send_frame_dev(g_dev, frame, length);
    if (result == 0) {
        uart_puts("[virtio-net] Frame transmitted\n");
    }
    return result;
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

    /* Find which device triggered the interrupt based on IRQ number */
    for (dev_idx = 0u; dev_idx < g_device_count; ++dev_idx) {
        if (g_devices[dev_idx].irq == int_id) {
            dev = &g_devices[dev_idx];
            break;
        }
    }

    if (dev == NULL || !dev->driver_ok) {
        return;
    }

    /* Read interrupt status */
    interrupt_status = virtio_reg_read(dev, VIRTIO_MMIO_INTERRUPT_STATUS);

    if (interrupt_status & 0x1u) {  /* Used buffer notification */
        if (dev->tx_queue != NULL) {
            dev->tx_last_used = dev->tx_queue->used.idx;
        }

        virtio_net_handle_rx_used(dev, dev_idx);
    }

    /* Acknowledge interrupt */
    virtio_reg_write(dev, VIRTIO_MMIO_INTERRUPT_ACK, interrupt_status);
}

/* Check if there are pending RX packets */
int virtio_net_has_pending_rx(void)
{
    if (g_dev == NULL) {
        return 0;
    }
    return virtio_net_has_pending_rx_dev(g_dev);
}

/* Enable interrupts on the VirtIO device */
void virtio_net_enable_interrupts(void)
{
    if (g_dev == NULL) {
        return;
    }
    virtio_net_enable_interrupts_dev(g_dev);
}
