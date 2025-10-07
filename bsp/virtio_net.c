#include "virtio_net.h"

#include "mmio.h"
#include "uart.h"
#include "timer.h"
#include "lib.h"

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

static struct virtio_net_device g_dev;
static struct virtio_queue g_rx_queue;
static struct virtio_queue g_tx_queue;

static uint8_t g_rx_buffer_storage[VIRTIO_NET_QUEUE_SIZE][VIRTIO_NET_BUFFER_SIZE] __attribute__((aligned(64)));
static uint8_t g_tx_buffer_storage[VIRTIO_NET_QUEUE_SIZE][VIRTIO_NET_BUFFER_SIZE] __attribute__((aligned(64)));

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

static int virtio_net_scan(uintptr_t *base_out, uint32_t *irq_out)
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

    for (size_t i = 0u; i < (sizeof(candidates) / sizeof(candidates[0])); ++i) {
        uint32_t magic = virtio_mmio_read32(candidates[i], VIRTIO_MMIO_MAGIC_VALUE);
        if (magic == 0x74726976u) {
            uint32_t device_id = virtio_mmio_read32(candidates[i], VIRTIO_MMIO_DEVICE_ID);
            if (device_id == VIRTIO_ID_NET || device_id == 0u) {
                *base_out = candidates[i];
                *irq_out = irqs[i];
                uart_puts("[virtio-net] Detected device at base 0x");
                uart_write_hex((unsigned long)candidates[i]);
                uart_puts(", IRQ ");
                uart_write_dec(irqs[i]);
                uart_putc('\n');
                return 0;
            }
        }
    }

    return -1;
}

static void virtio_net_prepare_rx(struct virtio_net_device *dev)
{
    struct virtio_queue *queue = dev->rx_queue;
    for (uint16_t i = 0u; i < dev->rx_queue_size; ++i) {
        dev->rx_buffers[i] = &g_rx_buffer_storage[i][0];
        util_memset(dev->rx_buffers[i], 0, VIRTIO_NET_BUFFER_SIZE);
        queue->desc[i].addr = (uint64_t)(uintptr_t)dev->rx_buffers[i];
        queue->desc[i].len = VIRTIO_NET_BUFFER_SIZE;
        queue->desc[i].flags = VRING_DESC_F_WRITE;
        queue->desc[i].next = 0u;
        queue->avail.ring[i] = i;
    }
    queue->avail.idx = dev->rx_queue_size;
    dev->rx_last_used = 0u;
}

static void virtio_net_prepare_tx(struct virtio_net_device *dev)
{
    struct virtio_queue *queue = dev->tx_queue;
    for (uint16_t i = 0u; i < dev->tx_queue_size; ++i) {
        dev->tx_buffers[i] = &g_tx_buffer_storage[i][0];
        util_memset(dev->tx_buffers[i], 0, VIRTIO_NET_BUFFER_SIZE);
        queue->desc[i].addr = 0u;
        queue->desc[i].len = 0u;
        queue->desc[i].flags = 0u;
        queue->desc[i].next = 0u;
    }
    queue->avail.idx = 0u;
    dev->tx_last_used = 0u;
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

int virtio_net_init(uintptr_t base_addr, uint32_t irq)
{
    util_memset(&g_dev, 0, sizeof(g_dev));

    uintptr_t detected_base = base_addr;
    uint32_t detected_irq = irq;

    if (virtio_net_scan(&detected_base, &detected_irq) == 0) {
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

    g_dev.base = base_addr;
    g_dev.irq = irq;
    g_dev.rx_queue = &g_rx_queue;
    g_dev.tx_queue = &g_tx_queue;

    uart_puts("[virtio-net] Initialising device\n");
    uart_puts("[virtio-net] Base 0x");
    uart_write_hex((unsigned long)base_addr);
    uart_puts(", IRQ ");
    uart_write_dec(irq);
    uart_putc('\n');

    uint32_t magic = virtio_reg_read(&g_dev, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != 0x74726976u) {
        uart_puts("[virtio-net] Invalid magic\n");
        return -1;
    }

    uint32_t version = virtio_reg_read(&g_dev, VIRTIO_MMIO_VERSION);
    uart_puts("[virtio-net] Version ");
    uart_write_dec(version);
    uart_putc('\n');

    uint32_t device_id = virtio_reg_read(&g_dev, VIRTIO_MMIO_DEVICE_ID);
    uint32_t vendor_id = virtio_reg_read(&g_dev, VIRTIO_MMIO_VENDOR_ID);
    log_hex32("[virtio-net] Device ID ", device_id);
    log_hex32("[virtio-net] Vendor ID ", vendor_id);

    if (device_id != VIRTIO_ID_NET && device_id != 0u) {
        uart_puts("[virtio-net] Device is not virtio-net\n");
        return -1;
    }

    uint64_t par;
    __asm__ volatile("at s1e1r, %0" :: "r"(g_dev.base));
    __asm__ volatile("mrs %0, par_el1" : "=r"(par));
    uart_puts("[virtio-net] PAR_EL1 = 0x");
    uart_write_hex((unsigned long)par);
    uart_putc('\n');

    /* Reset device status before negotiation */
    virtio_reg_write(&g_dev, VIRTIO_MMIO_STATUS, 0u);

    uint32_t status_value = VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_reg_write(&g_dev, VIRTIO_MMIO_STATUS, status_value);
    uint32_t status = virtio_reg_read(&g_dev, VIRTIO_MMIO_STATUS);
    log_status("[virtio-net] ACKNOWLEDGE", status);

    status_value |= VIRTIO_STATUS_DRIVER;
    virtio_reg_write(&g_dev, VIRTIO_MMIO_STATUS, status_value);
    status = virtio_reg_read(&g_dev, VIRTIO_MMIO_STATUS);
    log_status("[virtio-net] DRIVER", status);

    uint32_t features;
    virtio_reg_write(&g_dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0u);
    features = virtio_reg_read(&g_dev, VIRTIO_MMIO_DEVICE_FEATURES);
    log_hex32("[virtio-net] Host features ", features);

    uint32_t driver_features = 0u;
    if (features & (1u << VIRTIO_NET_F_MAC)) {
        driver_features |= (1u << VIRTIO_NET_F_MAC);
    }

    virtio_reg_write(&g_dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0u);
    virtio_reg_write(&g_dev, VIRTIO_MMIO_DRIVER_FEATURES, driver_features);

    status_value |= VIRTIO_STATUS_FEATURES_OK;
    virtio_reg_write(&g_dev, VIRTIO_MMIO_STATUS, status_value);

    status = virtio_reg_read(&g_dev, VIRTIO_MMIO_STATUS);
    if ((status & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        uart_puts("[virtio-net] Warning: FEATURES_OK not acknowledged\n");
    }
    log_status("[virtio-net] FEATURES_OK", status);

    struct virtio_net_config *config = (struct virtio_net_config *)(g_dev.base + VIRTIO_MMIO_CONFIG);
    for (size_t i = 0u; i < sizeof(g_dev.mac); ++i) {
        g_dev.mac[i] = config->mac[i];
    }

    uart_puts("[virtio-net] MAC ");
    for (size_t i = 0u; i < sizeof(g_dev.mac); ++i) {
        log_hex8(g_dev.mac[i]);
        if (i + 1u < sizeof(g_dev.mac)) {
            uart_putc(':');
        }
    }
    uart_putc('\n');

    if (virtio_net_configure_queue(&g_dev, VIRTIO_NET_RX_QUEUE, g_dev.rx_queue, &g_dev.rx_queue_size) != 0) {
        return -1;
    }
    virtio_net_prepare_rx(&g_dev);
    virtio_reg_write(&g_dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_RX_QUEUE);

    if (virtio_net_configure_queue(&g_dev, VIRTIO_NET_TX_QUEUE, g_dev.tx_queue, &g_dev.tx_queue_size) != 0) {
        return -1;
    }
    virtio_net_prepare_tx(&g_dev);

    status_value |= VIRTIO_STATUS_DRIVER_OK;
    virtio_reg_write(&g_dev, VIRTIO_MMIO_STATUS, status_value);

    status = virtio_reg_read(&g_dev, VIRTIO_MMIO_STATUS);
    log_status("[virtio-net] DRIVER_OK", status);

    virtio_reg_write(&g_dev, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_NET_RX_QUEUE);
    uint32_t rx_queue_max = virtio_reg_read(&g_dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    virtio_reg_write(&g_dev, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_NET_TX_QUEUE);
    uint32_t tx_queue_max = virtio_reg_read(&g_dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    uart_puts("[virtio-net] Queue sizes: RX=");
    uart_write_dec(rx_queue_max);
    uart_puts(" TX=");
    uart_write_dec(tx_queue_max);
    uart_putc('\n');

    g_dev.driver_ok = 1u;
    return 0;
}

int virtio_net_self_test_registers(void)
{
    if (!g_dev.driver_ok) {
        uart_puts("[virtio-net] Driver not initialised\n");
        return -1;
    }

    virtio_reg_write(&g_dev, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_NET_RX_QUEUE);
    uint32_t rx_max = virtio_reg_read(&g_dev, VIRTIO_MMIO_QUEUE_NUM_MAX);

    virtio_reg_write(&g_dev, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_NET_TX_QUEUE);
    uint32_t tx_max = virtio_reg_read(&g_dev, VIRTIO_MMIO_QUEUE_NUM_MAX);

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
    if (!g_dev.driver_ok) {
        uart_puts("[virtio-net] Driver not initialised\n");
        return -1;
    }

    if (length == 0u || length > VIRTIO_NET_MAX_FRAME_SIZE || frame == NULL) {
        uart_puts("[virtio-net] Invalid frame length\n");
        return -1;
    }

    struct virtio_queue *queue = g_dev.tx_queue;
    uint16_t idx = (uint16_t)(queue->avail.idx % g_dev.tx_queue_size);
    uint8_t *buffer = g_dev.tx_buffers[idx];
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)buffer;

    util_memset(hdr, 0, sizeof(*hdr));
    util_memcpy(buffer + sizeof(*hdr), frame, length);

    queue->desc[idx].addr = (uint64_t)(uintptr_t)buffer;
    queue->desc[idx].len = (uint32_t)(length + sizeof(*hdr));
    queue->desc[idx].flags = 0u;
    queue->desc[idx].next = 0u;

    queue->avail.ring[idx] = idx;
    queue->avail.idx++;

    virtio_reg_write(&g_dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_TX_QUEUE);

    uint32_t wait_ms = 100u;
    while (g_dev.tx_queue->used.idx == g_dev.tx_last_used && wait_ms-- > 0u) {
        timer_delay_ms(1u);
    }

    if (g_dev.tx_queue->used.idx == g_dev.tx_last_used) {
        uart_puts("[virtio-net] TX timeout\n");
        return -1;
    }

    g_dev.tx_last_used = g_dev.tx_queue->used.idx;
    uart_puts("[virtio-net] Frame transmitted\n");
    return 0;
}

int virtio_net_poll_frame(uint8_t *out_frame, size_t *out_length)
{
    if (!g_dev.driver_ok) {
        return -1;
    }

    if (g_dev.rx_last_used == g_dev.rx_queue->used.idx) {
        return 0;
    }

    uint16_t index = (uint16_t)(g_dev.rx_last_used % g_dev.rx_queue_size);
    struct vring_used_elem *elem = &g_dev.rx_queue->used.ring[index];
    uint32_t total_len = elem->len;
    uint16_t desc_id = (uint16_t)elem->id;

    if (desc_id >= g_dev.rx_queue_size) {
        uart_puts("[virtio-net] RX descriptor index out of range\n");
        g_dev.rx_last_used++;
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
                        g_dev.rx_buffers[desc_id] + sizeof(struct virtio_net_hdr),
                        payload_len);
            *out_length = payload_len;
        }
    } else {
        if (out_length != NULL) {
            *out_length = 0u;
        }
    }

    g_dev.rx_queue->avail.ring[g_dev.rx_queue->avail.idx % g_dev.rx_queue_size] = desc_id;
    g_dev.rx_queue->avail.idx++;
    g_dev.rx_last_used++;

    virtio_reg_write(&g_dev, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_NET_RX_QUEUE);

    return (payload_len > 0u) ? 1 : 0;
}

const uint8_t *virtio_net_get_mac(void)
{
    return g_dev.mac;
}

void virtio_net_debug_dump_status(void)
{
    uint32_t status = virtio_reg_read(&g_dev, VIRTIO_MMIO_STATUS);
    log_status("[virtio-net] STATUS", status);

    uint32_t interrupt_status = virtio_reg_read(&g_dev, VIRTIO_MMIO_INTERRUPT_STATUS);
    log_status("[virtio-net] INTERRUPT_STATUS", interrupt_status);
}
