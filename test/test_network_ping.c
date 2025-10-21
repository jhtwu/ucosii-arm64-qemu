/*
 * Test Case 2: Network TAP Ping Test with Response Time Reporting
 *
 * Purpose: Verify that VirtIO-net driver works correctly with TAP interface
 *          and measure ICMP ping response times
 *
 * Expected Behavior:
 * - VirtIO-net driver initializes successfully
 * - ARP resolution works (discovers peer MAC address)
 * - ICMP echo requests are sent
 * - ICMP echo replies are received
 * - Response times are measured and reported
 *
 * Success Criteria:
 * - Driver initialization successful
 * - ARP resolution completes
 * - At least 3 ping responses received
 * - Response times < 100ms (reasonable for TAP interface)
 * - No packet loss on successful responses
 *
 * Run Command: make run-tap
 * Prerequisites: TAP interface 'qemu-lan' must be configured with IP 192.168.1.103
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <ucos_ii.h>

#include "virtio_net.h"
#include "uart.h"
#include "lib.h"
#include "gic.h"
#include "timer.h"
#include "bsp_int.h"
#include "bsp_os.h"

#define TASK_STACK_SIZE         512u
#define TEST_NET_TASK_PRIO      3u
#define TEST_DURATION_PINGS     5u
#define ARP_TIMEOUT_MS          2000u
#define PING_TIMEOUT_MS         1000u
#define PING_INTERVAL_MS        1000u

/* Network configuration */
static const uint8_t g_local_ip[4] = {192u, 168u, 1u, 1u};
static const uint8_t g_peer_ip[4]  = {192u, 168u, 1u, 103u};

static uint8_t g_peer_mac[6];
static bool g_peer_mac_valid = false;

/* Test statistics */
static uint32_t g_pings_sent = 0;
static uint32_t g_pings_received = 0;
static uint32_t g_total_response_time_ms = 0;
static uint32_t g_min_response_ms = 0xFFFFFFFFu;
static uint32_t g_max_response_ms = 0;

/* Timing for current ping */
static uint32_t g_ping_start_time = 0;
static bool g_waiting_for_ping = false;
static uint16_t g_current_sequence = 0;

static OS_STK test_net_task_stack[TASK_STACK_SIZE];

/* Network packet structures */
struct eth_header {
    uint8_t dest[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed));

struct arp_packet {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} __attribute__((packed));

struct ipv4_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint8_t src[4];
    uint8_t dst[4];
} __attribute__((packed));

struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t data[];
} __attribute__((packed));

/* Helper functions */
static void print_mac(const char *label, const uint8_t mac[6])
{
    static const char digits[] = "0123456789ABCDEF";
    uart_puts(label);
    for (size_t i = 0u; i < 6u; ++i) {
        uart_putc(digits[(mac[i] >> 4u) & 0xFu]);
        uart_putc(digits[mac[i] & 0xFu]);
        if (i + 1u < 6u) {
            uart_putc(':');
        }
    }
    uart_putc('\n');
}

static uint16_t checksum16(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0u;

    while (length > 1u) {
        sum += ((uint32_t)bytes[0] << 8) | (uint32_t)bytes[1];
        bytes += 2u;
        length -= 2u;
    }

    if (length == 1u) {
        sum += ((uint32_t)bytes[0] << 8);
    }

    while ((sum >> 16u) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16u);
    }

    return (uint16_t)~sum;
}

static bool ip_equals(const uint8_t *lhs, const uint8_t *rhs)
{
    return (util_memcmp(lhs, rhs, 4u) == 0);
}

static uint32_t get_time_ms(void)
{
    return OSTime;  /* OS ticks as milliseconds (assuming 1ms tick) */
}

/* Network functions */
static void send_arp_request(void)
{
    uint8_t frame[64];
    util_memset(frame, 0, sizeof(frame));

    const uint8_t *mac = virtio_net_get_mac();
    const uint8_t broadcast[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    struct eth_header *eth = (struct eth_header *)frame;
    util_memcpy(eth->dest, broadcast, sizeof(eth->dest));
    util_memcpy(eth->src, mac, sizeof(eth->src));
    eth->type = util_htons(0x0806u);

    struct arp_packet *arp = (struct arp_packet *)(frame + sizeof(*eth));
    arp->htype = util_htons(1u);
    arp->ptype = util_htons(0x0800u);
    arp->hlen = 6u;
    arp->plen = 4u;
    arp->oper = util_htons(1u);
    util_memcpy(arp->sha, mac, sizeof(arp->sha));
    util_memcpy(arp->spa, g_local_ip, sizeof(arp->spa));
    util_memset(arp->tha, 0, sizeof(arp->tha));
    util_memcpy(arp->tpa, g_peer_ip, sizeof(arp->tpa));

    uart_puts("[TEST] Sending ARP request for 192.168.1.103\n");
    virtio_net_send_frame(frame, sizeof(*eth) + sizeof(*arp));
}

static void send_icmp_request(uint16_t sequence)
{
    if (!g_peer_mac_valid) {
        uart_puts("[TEST] Cannot send ICMP - peer MAC not resolved\n");
        return;
    }

    const uint8_t *local_mac = virtio_net_get_mac();
    uint8_t frame[sizeof(struct eth_header) + sizeof(struct ipv4_header) + sizeof(struct icmp_header) + 32u];

    struct eth_header *eth = (struct eth_header *)frame;
    struct ipv4_header *ip = (struct ipv4_header *)(frame + sizeof(*eth));
    struct icmp_header *icmp = (struct icmp_header *)(frame + sizeof(*eth) + sizeof(*ip));
    uint8_t *payload = icmp->data;
    size_t payload_len = 32u;

    util_memcpy(eth->dest, g_peer_mac, sizeof(eth->dest));
    util_memcpy(eth->src, local_mac, sizeof(eth->src));
    eth->type = util_htons(0x0800u);

    ip->version_ihl = (4u << 4u) | 5u;
    ip->tos = 0u;
    uint16_t total_length = (uint16_t)(sizeof(struct ipv4_header) + sizeof(struct icmp_header) + payload_len);
    ip->total_length = util_htons(total_length);
    ip->identification = util_htons(sequence);
    ip->flags_fragment = 0u;
    ip->ttl = 64u;
    ip->protocol = 1u;
    ip->header_checksum = 0u;
    util_memcpy(ip->src, g_local_ip, sizeof(ip->src));
    util_memcpy(ip->dst, g_peer_ip, sizeof(ip->dst));
    ip->header_checksum = util_htons(checksum16(ip, sizeof(struct ipv4_header)));

    icmp->type = 8u;
    icmp->code = 0u;
    icmp->identifier = util_htons(0xABCDu);
    icmp->sequence = util_htons(sequence);
    for (size_t i = 0u; i < payload_len; ++i) {
        payload[i] = (uint8_t)(0x20 + i);
    }
    icmp->checksum = 0u;
    icmp->checksum = util_htons(checksum16(icmp, sizeof(struct icmp_header) + payload_len));

    uart_puts("[TEST] Sending ICMP echo request seq=");
    uart_write_dec(sequence);
    uart_puts("\n");

    g_ping_start_time = get_time_ms();
    g_waiting_for_ping = true;
    g_current_sequence = sequence;
    g_pings_sent++;

    size_t frame_len = sizeof(struct eth_header) + total_length;
    virtio_net_send_frame(frame, frame_len);
}

static int process_frame(const uint8_t *frame, size_t length)
{
    if (length < sizeof(struct eth_header)) {
        return 0;
    }

    const struct eth_header *eth = (const struct eth_header *)frame;
    uint16_t eth_type = util_ntohs(eth->type);

    /* Process ARP */
    if (eth_type == 0x0806u) {
        if (length < sizeof(struct eth_header) + sizeof(struct arp_packet)) {
            return 0;
        }
        const struct arp_packet *arp = (const struct arp_packet *)(frame + sizeof(*eth));
        uint16_t oper = util_ntohs(arp->oper);

        if (oper == 2u && ip_equals(arp->spa, g_peer_ip)) {
            uart_puts("[TEST] ARP reply received from peer\n");
            util_memcpy(g_peer_mac, arp->sha, sizeof(g_peer_mac));
            g_peer_mac_valid = true;
            print_mac("[TEST] Peer MAC: ", g_peer_mac);
            return 1;
        }
        return 0;
    }

    /* Process ICMP echo reply */
    if (eth_type == 0x0800u) {
        if (length < sizeof(struct eth_header) + sizeof(struct ipv4_header)) {
            return 0;
        }

        const struct ipv4_header *ip = (const struct ipv4_header *)(frame + sizeof(*eth));
        if (!ip_equals(ip->src, g_peer_ip) || !ip_equals(ip->dst, g_local_ip)) {
            return 0;
        }

        if (ip->protocol == 1u) {
            size_t ip_header_len = (size_t)((ip->version_ihl & 0x0Fu) * 4u);
            const struct icmp_header *icmp = (const struct icmp_header *)((const uint8_t *)ip + ip_header_len);

            if (icmp->type == 0u && icmp->code == 0u) {  /* Echo reply */
                uint16_t seq = util_ntohs(icmp->sequence);

                if (g_waiting_for_ping && seq == g_current_sequence) {
                    uint32_t response_time = get_time_ms() - g_ping_start_time;
                    g_waiting_for_ping = false;
                    g_pings_received++;
                    g_total_response_time_ms += response_time;

                    if (response_time < g_min_response_ms) {
                        g_min_response_ms = response_time;
                    }
                    if (response_time > g_max_response_ms) {
                        g_max_response_ms = response_time;
                    }

                    uart_puts("[TEST] ICMP reply received seq=");
                    uart_write_dec(seq);
                    uart_puts(" time=");
                    uart_write_dec(response_time);
                    uart_puts("ms\n");
                    return 1;
                }
            }
        }
    }

    return 0;
}

static void test_network_task(void *p_arg)
{
    (void)p_arg;
    uint8_t rx_buffer[VIRTIO_NET_MAX_FRAME_SIZE];
    size_t rx_length = 0;
    uint32_t ping_wait = 0;
    uint16_t ping_sequence = 1;

    uart_puts("[TEST] Network test task started\n");

    /* Initialize timer interrupt INSIDE first task */
    BSP_IntVectSet(27u, 0u, 0u, BSP_OS_TmrTickHandler);
    BSP_IntSrcEn(27u);
    BSP_OS_TmrTickInit(1000u);
    uart_puts("[TEST] Timer initialized\n");

    /* Step 1: Initialize VirtIO-net driver */
    uart_puts("[TEST] Initializing VirtIO-net driver\n");
    if (virtio_net_init(0u, 0u) != 0) {
        uart_puts("[FAIL] Driver initialization failed\n");
        goto test_end;
    }
    uart_puts("[TEST] Driver initialized successfully\n");

    const uint8_t *mac = virtio_net_get_mac();
    print_mac("[TEST] Local MAC: ", mac);
    uart_puts("[TEST] Local IP: 192.168.1.1/24\n");
    uart_puts("[TEST] Peer IP: 192.168.1.103\n\n");

    /* Step 2: ARP resolution */
    uart_puts("[TEST] Starting ARP resolution\n");
    send_arp_request();
    INT32U arp_start_tick = OSTimeGet();
    INT32U last_arp_tick = arp_start_tick;

    while (!g_peer_mac_valid) {
        while (virtio_net_has_pending_rx()) {
            int rc = virtio_net_poll_frame(rx_buffer, &rx_length);
            if (rc > 0) {
                process_frame(rx_buffer, rx_length);
            } else {
                break;
            }
        }

        if (g_peer_mac_valid) {
            break;
        }

        INT32U now = OSTimeGet();
        if ((now - last_arp_tick) >= (OS_TICKS_PER_SEC / 2u)) {
            send_arp_request();
            last_arp_tick = now;
        }

        if ((now - arp_start_tick) >= (ARP_TIMEOUT_MS * OS_TICKS_PER_SEC / 1000u)) {
            break;
        }

        INT8U err = virtio_net_wait_rx_any(100u);
        if (err == OS_ERR_TIMEOUT) {
            continue;
        }
    }

    if (!g_peer_mac_valid) {
        uart_puts("[FAIL] ARP resolution timeout\n");
        goto test_end;
    }
    uart_puts("[TEST] ARP resolution successful\n\n");

    /* Step 3: Send ICMP echo requests and measure response times */
    uart_puts("[TEST] Starting ping test (");
    uart_write_dec(TEST_DURATION_PINGS);
    uart_puts(" pings)\n");

    while (ping_sequence <= TEST_DURATION_PINGS) {
        send_icmp_request(ping_sequence);
        ping_wait = 0;

        /* Wait for reply with timeout */
        while (g_waiting_for_ping) {
            while (virtio_net_has_pending_rx()) {
                int rc = virtio_net_poll_frame(rx_buffer, &rx_length);
                if (rc > 0) {
                    process_frame(rx_buffer, rx_length);
                } else {
                    break;
                }
            }

            if (!g_waiting_for_ping) {
                break;
            }

            if ((get_time_ms() - g_ping_start_time) >= PING_TIMEOUT_MS) {
                break;
            }

            INT8U err = virtio_net_wait_rx_any(10u);
            if (err == OS_ERR_TIMEOUT) {
                ping_wait += 10u;
            }
        }

        ping_wait = get_time_ms() - g_ping_start_time;
        if (ping_wait > PING_INTERVAL_MS) {
            ping_wait = PING_INTERVAL_MS;
        }

        if (g_waiting_for_ping) {
            uart_puts("[TEST] Ping timeout for seq=");
            uart_write_dec(ping_sequence);
            uart_puts("\n");
            g_waiting_for_ping = false;
        }

        ping_sequence++;

        /* Wait before next ping */
        if (ping_sequence <= TEST_DURATION_PINGS) {
            OSTimeDlyHMSM(0, 0, 0, PING_INTERVAL_MS - ping_wait);
        }
    }

test_end:
    /* Report test results */
    uart_puts("\n========================================\n");
    uart_puts("TEST CASE 2: RESULTS\n");
    uart_puts("========================================\n");
    uart_puts("Network Configuration:\n");
    uart_puts("  Local IP:  192.168.1.1\n");
    uart_puts("  Peer IP:   192.168.1.103\n");
    uart_puts("  ARP Status: ");
    if (g_peer_mac_valid) {
        uart_puts("Resolved\n");
    } else {
        uart_puts("Failed\n");
    }
    uart_puts("\nPing Statistics:\n");
    uart_puts("  Sent:     ");
    uart_write_dec(g_pings_sent);
    uart_puts("\n  Received: ");
    uart_write_dec(g_pings_received);
    uart_puts("\n");

    if (g_pings_received > 0) {
        uint32_t avg_ms = g_total_response_time_ms / g_pings_received;
        uint32_t loss_percent = ((g_pings_sent - g_pings_received) * 100) / g_pings_sent;

        uart_puts("  Loss:     ");
        uart_write_dec(loss_percent);
        uart_puts("%\n");
        uart_puts("\nResponse Times:\n");
        uart_puts("  Min:      ");
        uart_write_dec(g_min_response_ms);
        uart_puts(" ms\n");
        uart_puts("  Max:      ");
        uart_write_dec(g_max_response_ms);
        uart_puts(" ms\n");
        uart_puts("  Average:  ");
        uart_write_dec(avg_ms);
        uart_puts(" ms\n");
    }

    /* Evaluate test results */
    uint8_t test_passed = 1;

    if (!g_peer_mac_valid) {
        uart_puts("\n[FAIL] ARP resolution failed\n");
        test_passed = 0;
    }

    if (g_pings_received < 3) {
        uart_puts("[FAIL] Insufficient ping responses (expected >= 3)\n");
        test_passed = 0;
    }

    if (g_pings_received > 0) {
        uint32_t avg_ms = g_total_response_time_ms / g_pings_received;
        if (avg_ms > 100) {
            uart_puts("[FAIL] Average response time too high (expected < 100ms)\n");
            test_passed = 0;
        }
    }

    if (test_passed) {
        uart_puts("\n[PASS] ✓ Network ping test PASSED\n");
    } else {
        uart_puts("\n[FAIL] ✗ Network ping test FAILED\n");
    }
    uart_puts("========================================\n\n");

    /* Test completed */
    uart_puts("[TEST] Test completed successfully\n");

    /* Task complete - idle forever */
    for (;;) {
        OSTimeDlyHMSM(0, 0, 10, 0);
    }
}

int main(void)
{
    uart_puts("\n========================================\n");
    uart_puts("TEST CASE 2: Network TAP Ping Test\n");
    uart_puts("========================================\n");
    uart_puts("[BOOT] Initializing test environment\n");

    uart_init();
    gic_init();
    uart_puts("[BOOT] GICv3 initialized\n");

    /* Configure timer access */
    uint64_t val = 0xd6;
    __asm__ volatile("msr cntkctl_el1, %0" :: "r"(val));

    OSInit();
    uart_puts("[BOOT] uC/OS-II initialized\n");

    /* Create network test task */
    INT8U err = OSTaskCreate(test_network_task,
                             NULL,
                             &test_net_task_stack[TASK_STACK_SIZE - 1u],
                             TEST_NET_TASK_PRIO);
    if (err != OS_ERR_NONE) {
        uart_puts("[ERROR] Failed to create network test task\n");
        return 1;
    }
    uart_puts("[BOOT] Network test task created\n");

    /* Enable IRQs before OSStart (timer will be initialized in task) */
    __asm__ volatile("msr daifclr, #0x2");
    uart_puts("[BOOT] IRQs enabled\n");

    uart_puts("[BOOT] Starting test...\n");
    uart_puts("========================================\n\n");

    OSStart();

    /* Should never reach here */
    uart_puts("[ERROR] Returned from OSStart()!\n");
    while (1) { }
}
