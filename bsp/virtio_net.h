#ifndef BSP_VIRTIO_NET_H
#define BSP_VIRTIO_NET_H

#include <stdint.h>
#include <stddef.h>

/* Default VirtIO MMIO base for QEMU virt machine */
#define VIRTIO_NET_MMIO_BASE_DEFAULT   0x0A000000u

/* SPI interrupt base used by QEMU for VirtIO devices */
#define VIRTIO_NET_DEFAULT_IRQ         48u

#define VIRTIO_NET_MAX_FRAME_SIZE      1518u

/* Maximum number of VirtIO network devices supported */
#define VIRTIO_NET_MAX_DEVICES         2u

/* Device handle type */
typedef struct virtio_net_device* virtio_net_dev_t;

/* Initialize and discover all VirtIO network devices */
int virtio_net_init_all(void);

/* Legacy single-device init (initializes device 0) */
int virtio_net_init(uintptr_t base_addr, uint32_t irq);

/* Get device by index (0-based) */
virtio_net_dev_t virtio_net_get_device(size_t index);

/* Get number of initialized devices */
size_t virtio_net_get_device_count(void);

/* Device-specific operations */
int virtio_net_send_frame_dev(virtio_net_dev_t dev, const uint8_t *frame, size_t length);
int virtio_net_poll_frame_dev(virtio_net_dev_t dev, uint8_t *out_frame, size_t *out_length);
const uint8_t *virtio_net_get_mac_dev(virtio_net_dev_t dev);
void virtio_net_enable_interrupts_dev(virtio_net_dev_t dev);
int virtio_net_has_pending_rx_dev(virtio_net_dev_t dev);

/* Legacy single-device operations (operate on device 0) */
int virtio_net_self_test_registers(void);
int virtio_net_send_frame(const uint8_t *frame, size_t length);
int virtio_net_poll_frame(uint8_t *out_frame, size_t *out_length);
const uint8_t *virtio_net_get_mac(void);
void virtio_net_debug_dump_status(void);

/* Interrupt mode functions */
void virtio_net_interrupt_handler(uint32_t int_id);
int virtio_net_has_pending_rx(void);
void virtio_net_enable_interrupts(void);

#endif /* BSP_VIRTIO_NET_H */
