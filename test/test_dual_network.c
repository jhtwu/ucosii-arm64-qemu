#include <ucos_ii.h>
#include <stdint.h>
#include <stddef.h>
#include "gic.h"
#include "uart.h"
#include "timer.h"
#include "bsp_os.h"
#include "bsp_int.h"
#include "virtio_net.h"
#include "lib.h"

#define TASK_STK_SIZE 4096u

/* Network test task */
static OS_STK net_test_task_stk[TASK_STK_SIZE];

/* Network configuration for LAN (192.168.1.x) */
#define LAN_IP_0       192u
#define LAN_IP_1       168u
#define LAN_IP_2       1u
#define LAN_IP_3       1u
#define LAN_PEER_IP_3  103u

/* Network configuration for WAN (10.3.5.x) */
#define WAN_IP_0       10u
#define WAN_IP_1       3u
#define WAN_IP_2       5u
#define WAN_IP_3       99u
#define WAN_PEER_IP_3  103u

/* Ethernet frame structure */
struct eth_frame {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
    uint8_t payload[1500];
} __attribute__((packed));

/* ARP packet structure */
struct arp_packet {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_addr_len;
    uint8_t proto_addr_len;
    uint16_t operation;
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
} __attribute__((packed));

/* ICMP Echo packet */
struct icmp_echo {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
    uint8_t data[56];
} __attribute__((packed));

/* IP header */
struct ip_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint8_t src_ip[4];
    uint8_t dst_ip[4];
} __attribute__((packed));

static uint16_t checksum(const void *data, size_t len)
{
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0u;

    while (len > 1u) {
        sum += *ptr++;
        len -= 2u;
    }

    if (len > 0u) {
        sum += *((const uint8_t *)ptr);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

static uint16_t htons(uint16_t val)
{
    return ((val & 0xFFu) << 8) | ((val >> 8) & 0xFFu);
}

static int send_arp_request(virtio_net_dev_t dev, const uint8_t *src_mac,
                            const uint8_t *src_ip, const uint8_t *target_ip)
{
    struct eth_frame frame;
    struct arp_packet *arp;

    /* Broadcast MAC */
    util_memset(frame.dst_mac, 0xFF, 6);
    util_memcpy(frame.src_mac, src_mac, 6);
    frame.ethertype = htons(0x0806); /* ARP */

    arp = (struct arp_packet *)frame.payload;
    arp->hw_type = htons(1);
    arp->proto_type = htons(0x0800);
    arp->hw_addr_len = 6;
    arp->proto_addr_len = 4;
    arp->operation = htons(1); /* Request */
    util_memcpy(arp->sender_mac, src_mac, 6);
    util_memcpy(arp->sender_ip, src_ip, 4);
    util_memset(arp->target_mac, 0, 6);
    util_memcpy(arp->target_ip, target_ip, 4);

    return virtio_net_send_frame_dev(dev, (const uint8_t *)&frame,
                                     sizeof(frame.dst_mac) + sizeof(frame.src_mac) +
                                     sizeof(frame.ethertype) + sizeof(struct arp_packet));
}

static int send_icmp_echo(virtio_net_dev_t dev, const uint8_t *src_mac, const uint8_t *dst_mac,
                          const uint8_t *src_ip, const uint8_t *dst_ip, uint16_t seq)
{
    struct eth_frame frame;
    struct ip_header *ip;
    struct icmp_echo *icmp;

    util_memcpy(frame.dst_mac, dst_mac, 6);
    util_memcpy(frame.src_mac, src_mac, 6);
    frame.ethertype = htons(0x0800); /* IPv4 */

    ip = (struct ip_header *)frame.payload;
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_length = htons(sizeof(struct ip_header) + sizeof(struct icmp_echo));
    ip->identification = htons(seq);
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = 1; /* ICMP */
    ip->header_checksum = 0;
    util_memcpy(ip->src_ip, src_ip, 4);
    util_memcpy(ip->dst_ip, dst_ip, 4);
    ip->header_checksum = checksum(ip, sizeof(struct ip_header));

    icmp = (struct icmp_echo *)(frame.payload + sizeof(struct ip_header));
    icmp->type = 8; /* Echo request */
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(0x1234);
    icmp->sequence = htons(seq);
    util_memset(icmp->data, 0xAA, sizeof(icmp->data));
    icmp->checksum = checksum(icmp, sizeof(struct icmp_echo));

    return virtio_net_send_frame_dev(dev, (const uint8_t *)&frame,
                                     sizeof(frame.dst_mac) + sizeof(frame.src_mac) +
                                     sizeof(frame.ethertype) + sizeof(struct ip_header) +
                                     sizeof(struct icmp_echo));
}

static void net_test_task(void *p_arg)
{
    (void)p_arg;

    uart_puts("[TEST] Network test task started\n");

    /* Initialize timer first */
    BSP_IntVectSet(27u, 0u, 0u, BSP_OS_TmrTickHandler);
    BSP_IntSrcEn(27u);
    BSP_OS_TmrTickInit(1000u);
    uart_puts("[TEST] Timer initialized\n");

    /* Initialize all VirtIO network devices */
    uart_puts("[TEST] Initializing VirtIO-net drivers\n");
    if (virtio_net_init_all() < 0) {
        uart_puts("[FAIL] virtio_net_init_all() failed\n");
        goto fail;
    }

    size_t dev_count = virtio_net_get_device_count();
    uart_puts("[TEST] Detected ");
    uart_write_dec(dev_count);
    uart_puts(" network device(s)\n");

    if (dev_count < 2) {
        uart_puts("[FAIL] Need at least 2 network devices for dual NIC test\n");
        goto fail;
    }

    virtio_net_dev_t lan_dev = virtio_net_get_device(0);
    virtio_net_dev_t wan_dev = virtio_net_get_device(1);

    const uint8_t *lan_mac = virtio_net_get_mac_dev(lan_dev);
    const uint8_t *wan_mac = virtio_net_get_mac_dev(wan_dev);

    uart_puts("[TEST] LAN MAC: ");
    for (size_t i = 0; i < 6; i++) {
        uart_write_hex(lan_mac[i]);
        if (i < 5) uart_putc(':');
    }
    uart_putc('\n');

    uart_puts("[TEST] WAN MAC: ");
    for (size_t i = 0; i < 6; i++) {
        uart_write_hex(wan_mac[i]);
        if (i < 5) uart_putc(':');
    }
    uart_putc('\n');

    uart_puts("[TEST] LAN IP: 192.168.1.1/24\n");
    uart_puts("[TEST] WAN IP: 10.3.5.99/24\n");
    uart_puts("[TEST] LAN Peer: 192.168.1.103\n");
    uart_puts("[TEST] WAN Peer: 10.3.5.103\n\n");

    /* Test LAN interface */
    uart_puts("[TEST] ========== Testing LAN Interface ==========\n");

    uint8_t lan_ip[4] = {LAN_IP_0, LAN_IP_1, LAN_IP_2, LAN_IP_3};
    uint8_t lan_peer_ip[4] = {LAN_IP_0, LAN_IP_1, LAN_IP_2, LAN_PEER_IP_3};
    uint8_t lan_peer_mac[6] = {0};
    int lan_arp_resolved = 0;

    uart_puts("[TEST] Sending ARP request on LAN\n");
    for (int i = 0; i < 3 && !lan_arp_resolved; i++) {
        send_arp_request(lan_dev, lan_mac, lan_ip, lan_peer_ip);
        OSTimeDlyHMSM(0, 0, 0, 500);

        uint8_t rx_frame[1518];
        size_t rx_len;
        while (virtio_net_poll_frame_dev(lan_dev, rx_frame, &rx_len) > 0) {
            if (rx_len >= 42 && rx_frame[12] == 0x08 && rx_frame[13] == 0x06) {
                struct arp_packet *arp = (struct arp_packet *)&rx_frame[14];
                if (htons(arp->operation) == 2 &&
                    util_memcmp(arp->sender_ip, lan_peer_ip, 4) == 0) {
                    util_memcpy(lan_peer_mac, arp->sender_mac, 6);
                    lan_arp_resolved = 1;
                    uart_puts("[TEST] LAN ARP resolved\n");
                    break;
                }
            }
        }
    }

    int lan_ping_success = 0;
    if (lan_arp_resolved) {
        uart_puts("[TEST] Sending ping on LAN\n");
        send_icmp_echo(lan_dev, lan_mac, lan_peer_mac, lan_ip, lan_peer_ip, 1);
        OSTimeDlyHMSM(0, 0, 1, 0);

        uint8_t rx_frame[1518];
        size_t rx_len;
        while (virtio_net_poll_frame_dev(lan_dev, rx_frame, &rx_len) > 0) {
            if (rx_len >= 98 && rx_frame[12] == 0x08 && rx_frame[13] == 0x00) {
                struct ip_header *ip = (struct ip_header *)&rx_frame[14];
                if (ip->protocol == 1) {
                    struct icmp_echo *icmp = (struct icmp_echo *)&rx_frame[34];
                    if (icmp->type == 0) {
                        lan_ping_success = 1;
                        uart_puts("[PASS] LAN ping successful\n");
                        break;
                    }
                }
            }
        }
    }

    /* Test WAN interface */
    uart_puts("\n[TEST] ========== Testing WAN Interface ==========\n");

    uint8_t wan_ip[4] = {WAN_IP_0, WAN_IP_1, WAN_IP_2, WAN_IP_3};
    uint8_t wan_peer_ip[4] = {WAN_IP_0, WAN_IP_1, WAN_IP_2, WAN_PEER_IP_3};
    uint8_t wan_peer_mac[6] = {0};
    int wan_arp_resolved = 0;

    uart_puts("[TEST] Sending ARP request on WAN\n");
    for (int i = 0; i < 3 && !wan_arp_resolved; i++) {
        send_arp_request(wan_dev, wan_mac, wan_ip, wan_peer_ip);
        OSTimeDlyHMSM(0, 0, 0, 500);

        uint8_t rx_frame[1518];
        size_t rx_len;
        while (virtio_net_poll_frame_dev(wan_dev, rx_frame, &rx_len) > 0) {
            if (rx_len >= 42 && rx_frame[12] == 0x08 && rx_frame[13] == 0x06) {
                struct arp_packet *arp = (struct arp_packet *)&rx_frame[14];
                if (htons(arp->operation) == 2 &&
                    util_memcmp(arp->sender_ip, wan_peer_ip, 4) == 0) {
                    util_memcpy(wan_peer_mac, arp->sender_mac, 6);
                    wan_arp_resolved = 1;
                    uart_puts("[TEST] WAN ARP resolved\n");
                    break;
                }
            }
        }
    }

    int wan_ping_success = 0;
    if (wan_arp_resolved) {
        uart_puts("[TEST] Sending ping on WAN\n");
        send_icmp_echo(wan_dev, wan_mac, wan_peer_mac, wan_ip, wan_peer_ip, 1);
        OSTimeDlyHMSM(0, 0, 1, 0);

        uint8_t rx_frame[1518];
        size_t rx_len;
        while (virtio_net_poll_frame_dev(wan_dev, rx_frame, &rx_len) > 0) {
            if (rx_len >= 98 && rx_frame[12] == 0x08 && rx_frame[13] == 0x00) {
                struct ip_header *ip = (struct ip_header *)&rx_frame[14];
                if (ip->protocol == 1) {
                    struct icmp_echo *icmp = (struct icmp_echo *)&rx_frame[34];
                    if (icmp->type == 0) {
                        wan_ping_success = 1;
                        uart_puts("[PASS] WAN ping successful\n");
                        break;
                    }
                }
            }
        }
    }

    /* Print results */
    uart_puts("\n========================================\n");
    uart_puts("TEST CASE: Dual NIC Results\n");
    uart_puts("========================================\n");
    uart_puts("LAN Interface (192.168.1.1):\n");
    uart_puts("  ARP:  ");
    uart_puts(lan_arp_resolved ? "Resolved\n" : "Failed\n");
    uart_puts("  Ping: ");
    uart_puts(lan_ping_success ? "Success\n" : "Failed\n");
    uart_puts("\nWAN Interface (10.3.5.99):\n");
    uart_puts("  ARP:  ");
    uart_puts(wan_arp_resolved ? "Resolved\n" : "Failed\n");
    uart_puts("  Ping: ");
    uart_puts(wan_ping_success ? "Success\n" : "Failed\n");
    uart_puts("\n");

    if (lan_arp_resolved && lan_ping_success && wan_arp_resolved && wan_ping_success) {
        uart_puts("[PASS] ✓ Dual NIC test PASSED\n");
    } else {
        uart_puts("[FAIL] ✗ Dual NIC test FAILED\n");
        goto fail;
    }
    uart_puts("========================================\n\n");

    uart_puts("[TEST] Test completed successfully\n");

    for (;;) {
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }

fail:
    for (;;) {
        OSTimeDlyHMSM(0u, 0u, 1u, 0u);
    }
}

int main(void)
{
    INT8U err;

    uart_puts("\n========================================\n");
    uart_puts("TEST CASE: Dual Network Interface Test\n");
    uart_puts("========================================\n");
    uart_puts("[BOOT] Initializing test environment\n");

    /* Initialize hardware */
    uart_init();
    gic_init();
    uart_puts("[BOOT] GICv3 initialized\n");

    /* Configure timer access */
    uint64_t val = 0xd6;
    __asm__ volatile("msr cntkctl_el1, %0" :: "r"(val));

    /* Initialize uC/OS-II */
    OSInit();
    uart_puts("[BOOT] uC/OS-II initialized\n");

    /* Create network test task */
    err = OSTaskCreate(net_test_task,
                       (void *)0,
                       &net_test_task_stk[TASK_STK_SIZE - 1u],
                       5u);

    if (err != OS_ERR_NONE) {
        uart_puts("[ERROR] Failed to create network test task\n");
        return 1;
    }
    uart_puts("[BOOT] Network test task created\n");

    /* Enable IRQs */
    __asm__ volatile("msr daifclr, #2" ::: "memory");
    uart_puts("[BOOT] IRQs enabled\n");

    uart_puts("[BOOT] Starting test...\n");
    uart_puts("========================================\n\n");

    /* Start uC/OS-II scheduler */
    OSStart();

    /* Should never reach here */
    return 0;
}
