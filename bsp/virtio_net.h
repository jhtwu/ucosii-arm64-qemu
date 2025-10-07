#ifndef BSP_VIRTIO_NET_H
#define BSP_VIRTIO_NET_H

#include <stdint.h>
#include <stddef.h>

/* Default VirtIO MMIO base for QEMU virt machine */
#define VIRTIO_NET_MMIO_BASE_DEFAULT   0x0A000000u

/* SPI interrupt base used by QEMU for VirtIO devices */
#define VIRTIO_NET_DEFAULT_IRQ         48u

#define VIRTIO_NET_MAX_FRAME_SIZE      1518u

int virtio_net_init(uintptr_t base_addr, uint32_t irq);
int virtio_net_self_test_registers(void);
int virtio_net_send_frame(const uint8_t *frame, size_t length);
int virtio_net_poll_frame(uint8_t *out_frame, size_t *out_length);
const uint8_t *virtio_net_get_mac(void);
void virtio_net_debug_dump_status(void);

#endif /* BSP_VIRTIO_NET_H */
