#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <ucos_ii.h>

#include "virtio_net.h"
#include "uart.h"
#include "lib.h"
#include "nat.h"

#define NET_DEMO_POLL_DELAY_MS  100u  /* Reduced polling frequency for interrupt mode */

/* Network interface configuration */
struct net_interface {
    virtio_net_dev_t dev;
    uint8_t local_ip[4];
    uint8_t peer_ip[4];
    uint8_t peer_mac[6];
    bool peer_mac_valid;
    const char *name;
};

/* Static addressing for dual network interfaces */
static struct net_interface g_lan_if = {
    .dev = NULL,
    .local_ip = {192u, 168u, 1u, 1u},
    .peer_ip = {192u, 168u, 1u, 103u},
    .peer_mac = {0},
    .peer_mac_valid = false,
    .name = "LAN"
};

static struct net_interface g_wan_if = {
    .dev = NULL,
    .local_ip = {10u, 3u, 5u, 99u},
    .peer_ip = {10u, 3u, 5u, 103u},
    .peer_mac = {0},
    .peer_mac_valid = false,
    .name = "WAN"
};

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

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset_reserved;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

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

/* TCP/UDP checksum with pseudo-header (RFC 793, RFC 768) */
static uint16_t tcp_udp_checksum(const struct ipv4_header *ip, const void *transport_hdr, size_t transport_len)
{
    uint32_t sum = 0u;
    const uint16_t *words;
    size_t i;

    /* Pseudo-header: Source IP (4 bytes) - already in network byte order in packet */
    /* Read as 16-bit words directly from the byte array (no byte swap) */
    words = (const uint16_t *)ip->src;
    sum += words[0];
    sum += words[1];

    /* Pseudo-header: Destination IP (4 bytes) - already in network byte order */
    words = (const uint16_t *)ip->dst;
    sum += words[0];
    sum += words[1];

    /* Pseudo-header: Zero + Protocol (convert from host variable to network order) */
    sum += util_htons((uint16_t)ip->protocol);

    /* Pseudo-header: TCP/UDP Length (convert from host variable to network order) */
    sum += util_htons((uint16_t)transport_len);

    /* TCP/UDP header and data - already in network byte order in packet */
    /* Read as 16-bit words directly (no byte swap) */
    words = (const uint16_t *)transport_hdr;
    for (i = 0; i < transport_len; i += 2) {
        if (i + 1 < transport_len) {
            sum += *words++;
        } else {
            /* Odd byte: pad with zero (convert to network order) */
            sum += util_htons((uint16_t)(*(const uint8_t *)words) << 8);
        }
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

static bool ip_equals(const uint8_t *lhs, const uint8_t *rhs)
{
    return (util_memcmp(lhs, rhs, 4u) == 0);
}

static void net_demo_send_arp_request(struct net_interface *iface)
{
    uint8_t frame[64];
    util_memset(frame, 0, sizeof(frame));

    const uint8_t *mac = virtio_net_get_mac_dev(iface->dev);
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
    util_memcpy(arp->spa, iface->local_ip, sizeof(arp->spa));
    util_memset(arp->tha, 0, sizeof(arp->tha));
    util_memcpy(arp->tpa, iface->peer_ip, sizeof(arp->tpa));

    uart_puts("[net-demo] ");
    uart_puts(iface->name);
    uart_puts(": Sending ARP who-has ");
    uart_putc((char)('0' + iface->peer_ip[0] / 100u));
    uart_putc((char)('0' + (iface->peer_ip[0] / 10u) % 10u));
    uart_putc((char)('0' + iface->peer_ip[0] % 10u));
    uart_putc('.');
    uart_putc((char)('0' + iface->peer_ip[1] / 100u));
    uart_putc((char)('0' + (iface->peer_ip[1] / 10u) % 10u));
    uart_putc((char)('0' + iface->peer_ip[1] % 10u));
    uart_putc('.');
    uart_putc((char)('0' + iface->peer_ip[2] / 100u));
    uart_putc((char)('0' + (iface->peer_ip[2] / 10u) % 10u));
    uart_putc((char)('0' + iface->peer_ip[2] % 10u));
    uart_putc('.');
    uart_putc((char)('0' + iface->peer_ip[3] / 100u));
    uart_putc((char)('0' + (iface->peer_ip[3] / 10u) % 10u));
    uart_putc((char)('0' + iface->peer_ip[3] % 10u));
    uart_putc('\n');
    virtio_net_send_frame_dev(iface->dev, frame, sizeof(*eth) + sizeof(*arp));
}

static void send_arp_request_for_ip(struct net_interface *iface, const uint8_t target_ip[4])
{
    uint8_t frame[64];
    util_memset(frame, 0, sizeof(frame));

    const uint8_t *mac = virtio_net_get_mac_dev(iface->dev);
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
    util_memcpy(arp->spa, iface->local_ip, sizeof(arp->spa));
    util_memset(arp->tha, 0, sizeof(arp->tha));
    util_memcpy(arp->tpa, target_ip, sizeof(arp->tpa));

    virtio_net_send_frame_dev(iface->dev, frame, sizeof(*eth) + sizeof(*arp));
}

static void send_arp_reply(struct net_interface *iface,
                           const struct eth_header *eth,
                           const struct arp_packet *request)
{
    uint8_t frame[64];
    struct eth_header *reply_eth = (struct eth_header *)frame;
    struct arp_packet *reply_arp = (struct arp_packet *)(frame + sizeof(*reply_eth));
    const uint8_t *local_mac = virtio_net_get_mac_dev(iface->dev);

    util_memcpy(reply_eth->dest, eth->src, sizeof(reply_eth->dest));
    util_memcpy(reply_eth->src, local_mac, sizeof(reply_eth->src));
    reply_eth->type = util_htons(0x0806u);

    reply_arp->htype = util_htons(1u);
    reply_arp->ptype = util_htons(0x0800u);
    reply_arp->hlen = 6u;
    reply_arp->plen = 4u;
    reply_arp->oper = util_htons(2u);
    util_memcpy(reply_arp->sha, local_mac, sizeof(reply_arp->sha));
    util_memcpy(reply_arp->spa, iface->local_ip, sizeof(reply_arp->spa));
    util_memcpy(reply_arp->tha, request->sha, sizeof(reply_arp->tha));
    util_memcpy(reply_arp->tpa, request->spa, sizeof(reply_arp->tpa));

    virtio_net_send_frame_dev(iface->dev, frame, sizeof(*reply_eth) + sizeof(*reply_arp));
}

static void send_icmp_echo_reply(struct net_interface *iface,
                                  const uint8_t *rx_frame, size_t length)
{
    if (length > VIRTIO_NET_MAX_FRAME_SIZE) {
        length = VIRTIO_NET_MAX_FRAME_SIZE;
    }

    uint8_t frame[VIRTIO_NET_MAX_FRAME_SIZE];
    util_memcpy(frame, rx_frame, length);

    struct eth_header *eth = (struct eth_header *)frame;
    struct ipv4_header *ip = (struct ipv4_header *)(frame + sizeof(*eth));
    size_t ip_header_len = (size_t)((ip->version_ihl & 0x0Fu) * 4u);
    struct icmp_header *icmp = (struct icmp_header *)((uint8_t *)ip + ip_header_len);
    size_t payload_len = util_ntohs(ip->total_length);
    if (payload_len < ip_header_len + sizeof(struct icmp_header)) {
        return;
    }
    size_t icmp_len = payload_len - ip_header_len;

    const uint8_t *local_mac = virtio_net_get_mac_dev(iface->dev);
    uint8_t original_src_mac[6];
    util_memcpy(original_src_mac, eth->src, sizeof(original_src_mac));

    util_memcpy(eth->dest, eth->src, sizeof(eth->dest));
    util_memcpy(eth->src, local_mac, sizeof(eth->src));

    uint8_t src_ip[4];
    uint8_t original_dst_ip[4];
    util_memcpy(src_ip, ip->src, sizeof(src_ip));
    util_memcpy(original_dst_ip, ip->dst, sizeof(original_dst_ip));

    /* Reply with the IP that was pinged (could be local_ip or wan_ip on LAN interface) */
    util_memcpy(ip->src, original_dst_ip, sizeof(ip->src));
    util_memcpy(ip->dst, src_ip, sizeof(ip->dst));
    ip->ttl = 64u;
    ip->header_checksum = 0u;
    uint16_t ip_checksum = checksum16(ip, ip_header_len);
    ip->header_checksum = util_htons(ip_checksum);

    icmp->type = 0u;
    icmp->code = 0u;
    icmp->checksum = 0u;
    uint16_t icmp_checksum = checksum16(icmp, icmp_len);
    icmp->checksum = util_htons(icmp_checksum);

    uart_puts("[net-demo] ");
    uart_puts(iface->name);
    uart_puts(": Replied to ICMP echo request (src=");
    uart_write_dec(original_dst_ip[0]); uart_putc('.');
    uart_write_dec(original_dst_ip[1]); uart_putc('.');
    uart_write_dec(original_dst_ip[2]); uart_putc('.');
    uart_write_dec(original_dst_ip[3]);
    uart_puts(")\n");
    virtio_net_send_frame_dev(iface->dev, frame, sizeof(*eth) + payload_len);

    /* Restore original src MAC in case buffer reused */
    util_memcpy(eth->src, original_src_mac, sizeof(original_src_mac));
}

static int net_demo_process_frame(struct net_interface *iface,
                                   const uint8_t *frame, size_t length)
{
    if (length < sizeof(struct eth_header)) {
        return 0;
    }

    const struct eth_header *eth = (const struct eth_header *)frame;
    uint16_t eth_type = util_ntohs(eth->type);

    if (eth_type == 0x0806u) {
        if (length < sizeof(struct eth_header) + sizeof(struct arp_packet)) {
            return 0;
        }
        const struct arp_packet *arp = (const struct arp_packet *)(frame + sizeof(*eth));
        uint16_t oper = util_ntohs(arp->oper);
        if (oper == 1u) {
            if (ip_equals(arp->tpa, iface->local_ip)) {
                send_arp_reply(iface, eth, arp);
                return 1;
            }
        }
        if (oper == 2u) {
            /* Learn from ARP reply and add to cache */
            arp_cache_add(arp->spa, arp->sha);

            if (ip_equals(arp->tpa, iface->local_ip) && ip_equals(arp->spa, iface->peer_ip)) {
                uart_puts("[net-demo] ");
                uart_puts(iface->name);
                uart_puts(": Received ARP reply from peer\n");
                util_memcpy(iface->peer_mac, arp->sha, sizeof(iface->peer_mac));
                iface->peer_mac_valid = true;
                uart_puts("[net-demo] ");
                uart_puts(iface->name);
                uart_puts(": Peer MAC ");
                print_mac("", iface->peer_mac);
            }
            return 1;
        }
        return 0;
    }

    if (eth_type == 0x0800u) {
        if (length < sizeof(struct eth_header) + sizeof(struct ipv4_header)) {
            return 0;
        }

        const struct ipv4_header *ip = (const struct ipv4_header *)(frame + sizeof(*eth));
        uint8_t version = (ip->version_ihl >> 4u);
        uint8_t ihl = (uint8_t)(ip->version_ihl & 0x0Fu);
        if (version != 4u || ihl < 5u) {
            return 0;
        }

        /* Learn source IP-MAC mapping from IP packets (for NAT reverse lookup) */
        arp_cache_add(ip->src, eth->src);


        /* Check if packet is from WAN destined for our WAN IP (NAT return traffic) - HANDLE FIRST */
        if (nat_is_wan_ip(ip->dst) && iface == &g_wan_if && ip->protocol == 1u) {
            uint16_t total_length = util_ntohs(ip->total_length);
            size_t ip_header_len = (size_t)((ip->version_ihl & 0x0Fu) * 4u);

            if (total_length >= ip_header_len + sizeof(struct icmp_header)) {
                struct icmp_header *icmp = (struct icmp_header *)((uint8_t *)ip + ip_header_len);

                if (icmp->type == 0u) {  /* ICMP Echo Reply */
                    uint16_t wan_port = util_ntohs(icmp->identifier);
                    uint8_t lan_ip[4];
                    uint16_t lan_port;

                    /* Perform reverse NAT translation */
                    if (nat_translate_inbound(NAT_PROTO_ICMP, wan_port,
                                             ip->src, 0, lan_ip, &lan_port) == 0) {
                        /* Copy and modify the packet */
                        uint8_t forward_frame[VIRTIO_NET_MAX_FRAME_SIZE];
                        if (length <= VIRTIO_NET_MAX_FRAME_SIZE && g_lan_if.dev != NULL) {
                            util_memcpy(forward_frame, frame, length);

                            struct eth_header *fwd_eth = (struct eth_header *)forward_frame;
                            struct ipv4_header *fwd_ip = (struct ipv4_header *)(forward_frame + sizeof(*fwd_eth));
                            struct icmp_header *fwd_icmp = (struct icmp_header *)((uint8_t *)fwd_ip + ip_header_len);

                            /* Update Ethernet header */
                            const uint8_t *lan_mac = virtio_net_get_mac_dev(g_lan_if.dev);
                            util_memcpy(fwd_eth->src, lan_mac, 6);

                            /* Try to get destination MAC from ARP cache */
                            if (!arp_cache_lookup(lan_ip, fwd_eth->dest)) {
                                return 1;
                            }

                            /* Update IP header */
                            util_memcpy(fwd_ip->dst, lan_ip, 4);
                            fwd_ip->ttl--;
                            fwd_ip->header_checksum = 0u;
                            fwd_ip->header_checksum = util_htons(checksum16(fwd_ip, ip_header_len));

                            /* Update ICMP identifier with original port */
                            fwd_icmp->identifier = util_htons(lan_port);
                            fwd_icmp->checksum = 0u;
                            size_t icmp_len = total_length - ip_header_len;
                            fwd_icmp->checksum = util_htons(checksum16(fwd_icmp, icmp_len));

                            /* Send on LAN interface */
                            virtio_net_send_frame_dev(g_lan_if.dev, forward_frame,
                                                    sizeof(*fwd_eth) + total_length);
                            return 1;
                        }
                    }
                } else if (icmp->type == 8u) {
                    /* ICMP Echo Request to our WAN IP - reply directly */
                    send_icmp_echo_reply(iface, frame, sizeof(struct eth_header) + total_length);
                    return 1;
                }
            }
        }

        /* Check if packet is from WAN destined for our WAN IP (NAT return traffic) */
        /* MUST be checked BEFORE "is_for_us" check, otherwise NAT return packets are dropped */
        if (nat_is_wan_ip(ip->dst) && iface == &g_wan_if && (ip->protocol == 6u || ip->protocol == 17u)) {

            uint16_t total_length = util_ntohs(ip->total_length);
            size_t ip_header_len = (size_t)((ip->version_ihl & 0x0Fu) * 4u);
            size_t min_transport_len = (ip->protocol == 6u) ? sizeof(struct tcp_header) : sizeof(struct udp_header);

            if (total_length >= ip_header_len + min_transport_len) {
                uint16_t wan_port, src_port;
                uint8_t lan_ip[4];
                uint16_t lan_port;

                if (ip->protocol == 6u) {
                    struct tcp_header *tcp = (struct tcp_header *)((uint8_t *)ip + ip_header_len);
                    wan_port = util_ntohs(tcp->dst_port);
                    src_port = util_ntohs(tcp->src_port);
                } else {
                    struct udp_header *udp = (struct udp_header *)((uint8_t *)ip + ip_header_len);
                    wan_port = util_ntohs(udp->dst_port);
                    src_port = util_ntohs(udp->src_port);
                }

                uint8_t proto = (ip->protocol == 6u) ? NAT_PROTO_TCP : NAT_PROTO_UDP;

                /* Perform reverse NAT translation */
                if (nat_translate_inbound(proto, wan_port,
                                         ip->src, src_port, lan_ip, &lan_port) == 0) {
                    /* Copy and modify the packet */
                    uint8_t forward_frame[VIRTIO_NET_MAX_FRAME_SIZE];
                    if (length <= VIRTIO_NET_MAX_FRAME_SIZE && g_lan_if.dev != NULL) {
                        util_memcpy(forward_frame, frame, length);

                        struct eth_header *fwd_eth = (struct eth_header *)forward_frame;
                        struct ipv4_header *fwd_ip = (struct ipv4_header *)(forward_frame + sizeof(*fwd_eth));

                        /* Update Ethernet header */
                        const uint8_t *lan_mac = virtio_net_get_mac_dev(g_lan_if.dev);
                        util_memcpy(fwd_eth->src, lan_mac, 6);

                        /* Try to get destination MAC from ARP cache */
                        if (!arp_cache_lookup(lan_ip, fwd_eth->dest)) {
                            uart_puts("[NAT] LAN destination MAC not in cache, dropping packet\n");
                            return 1;
                        }

                        /* Update IP header */
                        util_memcpy(fwd_ip->dst, lan_ip, 4);
                        fwd_ip->ttl--;
                        fwd_ip->header_checksum = 0u;
                        fwd_ip->header_checksum = util_htons(checksum16(fwd_ip, ip_header_len));

                        /* Update transport port and checksum */
                        if (ip->protocol == 6u) {
                            struct tcp_header *fwd_tcp = (struct tcp_header *)((uint8_t *)fwd_ip + ip_header_len);
                            fwd_tcp->dst_port = util_htons(lan_port);
                            fwd_tcp->checksum = 0u;
                            size_t tcp_len = total_length - ip_header_len;
                            fwd_tcp->checksum = tcp_udp_checksum(fwd_ip, fwd_tcp, tcp_len);  /* Direct assignment */
                        } else {
                            struct udp_header *fwd_udp = (struct udp_header *)((uint8_t *)fwd_ip + ip_header_len);
                            fwd_udp->dst_port = util_htons(lan_port);
                            fwd_udp->checksum = 0u;
                            size_t udp_len = total_length - ip_header_len;
                            fwd_udp->checksum = tcp_udp_checksum(fwd_ip, fwd_udp, udp_len);  /* Direct assignment */
                        }

                        /* Send on LAN interface */
                        virtio_net_send_frame_dev(g_lan_if.dev, forward_frame,
                                                sizeof(*fwd_eth) + total_length);
                        return 1;
                    }
                }
            }
        }

        /* Check if packet is destined for us (or our WAN IP on LAN interface for gateway) */
        bool is_for_us = ip_equals(ip->dst, iface->local_ip);

        /* LAN interface should also respond to WAN IP (acting as gateway) */
        if (iface == &g_lan_if && nat_is_wan_ip(ip->dst)) {
            is_for_us = true;
        }

        if (is_for_us) {
            uint16_t total_length = util_ntohs(ip->total_length);
            if (total_length < (uint16_t)(ihl * 4u + sizeof(struct icmp_header))) {
                return 0;
            }

            if (ip->protocol == 1u) {
                size_t ip_header_len = (size_t)ihl * 4u;
                const struct icmp_header *icmp = (const struct icmp_header *)((const uint8_t *)ip + ip_header_len);
                size_t icmp_len = (size_t)total_length - ip_header_len;

                if (icmp_len >= sizeof(struct icmp_header) && icmp->type == 8u) {
                    send_icmp_echo_reply(iface, frame, sizeof(struct eth_header) + total_length);
                    return 1;
                }
            }
        } else {
            /* Packet not for us - check if we should forward via NAT */

            /* Check if packet is from LAN network trying to reach outside */
            if (nat_is_lan_ip(ip->src) && iface == &g_lan_if) {

                /* Forward ICMP, TCP, and UDP */
                if (ip->protocol == 1u && g_wan_if.dev != NULL) {  /* ICMP */
                    uint16_t total_length = util_ntohs(ip->total_length);
                    size_t ip_header_len = (size_t)((ip->version_ihl & 0x0Fu) * 4u);

                    if (total_length >= ip_header_len + sizeof(struct icmp_header)) {
                        struct icmp_header *icmp = (struct icmp_header *)((uint8_t *)ip + ip_header_len);

                        if (icmp->type == 8u) {  /* ICMP Echo Request */
                            uint16_t icmp_id = util_ntohs(icmp->identifier);
                            uint16_t wan_port;

                            /* Perform NAT translation */
                            if (nat_translate_outbound(NAT_PROTO_ICMP, ip->src, icmp_id,
                                                      ip->dst, 0, &wan_port) == 0) {
                                /* Copy and modify the packet */
                                uint8_t forward_frame[VIRTIO_NET_MAX_FRAME_SIZE];
                                if (length <= VIRTIO_NET_MAX_FRAME_SIZE) {
                                    util_memcpy(forward_frame, frame, length);

                                    struct eth_header *fwd_eth = (struct eth_header *)forward_frame;
                                    struct ipv4_header *fwd_ip = (struct ipv4_header *)(forward_frame + sizeof(*fwd_eth));
                                    struct icmp_header *fwd_icmp = (struct icmp_header *)((uint8_t *)fwd_ip + ip_header_len);

                                    /* Update Ethernet header */
                                    const uint8_t *wan_mac = virtio_net_get_mac_dev(g_wan_if.dev);
                                    util_memcpy(fwd_eth->src, wan_mac, 6);

                                    /* Try to get destination MAC from ARP cache */
                                    if (!arp_cache_lookup(ip->dst, fwd_eth->dest)) {
                                        return 1;
                                    }

                                    /* Update IP header */
                                    util_memcpy(fwd_ip->src, g_wan_if.local_ip, 4);
                                    fwd_ip->ttl--;
                                    fwd_ip->header_checksum = 0u;
                                    fwd_ip->header_checksum = util_htons(checksum16(fwd_ip, ip_header_len));

                                    /* Update ICMP identifier with translated port */
                                    fwd_icmp->identifier = util_htons(wan_port);
                                    fwd_icmp->checksum = 0u;
                                    size_t icmp_len = total_length - ip_header_len;
                                    fwd_icmp->checksum = util_htons(checksum16(fwd_icmp, icmp_len));

                                    /* Send on WAN interface */
                                    virtio_net_send_frame_dev(g_wan_if.dev, forward_frame,
                                                            sizeof(*fwd_eth) + total_length);
                                    return 1;
                                }
                            }
                        }
                    }
                } else if ((ip->protocol == 6u || ip->protocol == 17u) && g_wan_if.dev != NULL) {  /* TCP or UDP */
                    uint16_t total_length = util_ntohs(ip->total_length);
                    size_t ip_header_len = (size_t)((ip->version_ihl & 0x0Fu) * 4u);
                    size_t min_transport_len = (ip->protocol == 6u) ? sizeof(struct tcp_header) : sizeof(struct udp_header);

                    if (total_length >= ip_header_len + min_transport_len) {
                        uint16_t src_port, dst_port, wan_port;

                        if (ip->protocol == 6u) {
                            struct tcp_header *tcp = (struct tcp_header *)((uint8_t *)ip + ip_header_len);
                            src_port = util_ntohs(tcp->src_port);
                            dst_port = util_ntohs(tcp->dst_port);
                        } else {
                            struct udp_header *udp = (struct udp_header *)((uint8_t *)ip + ip_header_len);
                            src_port = util_ntohs(udp->src_port);
                            dst_port = util_ntohs(udp->dst_port);
                        }

                        uint8_t proto = (ip->protocol == 6u) ? NAT_PROTO_TCP : NAT_PROTO_UDP;

                        /* Perform NAT translation */
                        if (nat_translate_outbound(proto, ip->src, src_port,
                                                  ip->dst, dst_port, &wan_port) == 0) {
                            /* Copy and modify the packet */
                            uint8_t forward_frame[VIRTIO_NET_MAX_FRAME_SIZE];
                            if (length <= VIRTIO_NET_MAX_FRAME_SIZE) {
                                util_memcpy(forward_frame, frame, length);

                                struct eth_header *fwd_eth = (struct eth_header *)forward_frame;
                                struct ipv4_header *fwd_ip = (struct ipv4_header *)(forward_frame + sizeof(*fwd_eth));

                                /* Update Ethernet header */
                                const uint8_t *wan_mac = virtio_net_get_mac_dev(g_wan_if.dev);
                                util_memcpy(fwd_eth->src, wan_mac, 6);

                                /* Try to get destination MAC from ARP cache */
                                if (!arp_cache_lookup(ip->dst, fwd_eth->dest)) {
                                    send_arp_request_for_ip(&g_wan_if, ip->dst);
                                    return 1;
                                }

                                /* Update IP header */
                                util_memcpy(fwd_ip->src, g_wan_if.local_ip, 4);
                                fwd_ip->ttl--;
                                fwd_ip->header_checksum = 0u;
                                fwd_ip->header_checksum = util_htons(checksum16(fwd_ip, ip_header_len));

                                /* Update transport port and checksum */
                                if (ip->protocol == 6u) {
                                    struct tcp_header *fwd_tcp = (struct tcp_header *)((uint8_t *)fwd_ip + ip_header_len);
                                    fwd_tcp->src_port = util_htons(wan_port);
                                    fwd_tcp->checksum = 0u;
                                    size_t tcp_len = total_length - ip_header_len;
                                    fwd_tcp->checksum = tcp_udp_checksum(fwd_ip, fwd_tcp, tcp_len);
                                } else {
                                    struct udp_header *fwd_udp = (struct udp_header *)((uint8_t *)fwd_ip + ip_header_len);
                                    fwd_udp->src_port = util_htons(wan_port);
                                    fwd_udp->checksum = 0u;
                                    size_t udp_len = total_length - ip_header_len;
                                    fwd_udp->checksum = tcp_udp_checksum(fwd_ip, fwd_udp, udp_len);  /* Direct assignment */
                                }

                                /* Send on WAN interface */
                                virtio_net_send_frame_dev(g_wan_if.dev, forward_frame,
                                                        sizeof(*fwd_eth) + total_length);
                                return 1;
                            }
                        }
                    }
                }
            }

            /* Check if packet is from WAN destined for our WAN IP (NAT return traffic) */
            if (nat_is_wan_ip(ip->dst) && iface == &g_wan_if && (ip->protocol == 1u || ip->protocol == 6u || ip->protocol == 17u)) {
                /* Debug: Show we're handling WAN return packet */
                if (ip->protocol == 6u || ip->protocol == 17u) {
                    uart_puts("[NAT] WAN return packet proto=");
                    uart_write_dec(ip->protocol);
                    uart_puts(" from ");
                    uart_write_dec(ip->src[0]); uart_putc('.');
                    uart_write_dec(ip->src[1]); uart_putc('.');
                    uart_write_dec(ip->src[2]); uart_putc('.');
                    uart_write_dec(ip->src[3]);
                    uart_puts(" to WAN IP\n");
                }

                uint16_t total_length = util_ntohs(ip->total_length);
                size_t ip_header_len = (size_t)((ip->version_ihl & 0x0Fu) * 4u);

                if (total_length >= ip_header_len + sizeof(struct icmp_header)) {
                    struct icmp_header *icmp = (struct icmp_header *)((uint8_t *)ip + ip_header_len);

                    if (icmp->type == 0u) {  /* ICMP Echo Reply */
                        uint16_t wan_port = util_ntohs(icmp->identifier);
                        uint8_t lan_ip[4];
                        uint16_t lan_port;

                        /* Perform reverse NAT translation */
                        if (nat_translate_inbound(NAT_PROTO_ICMP, wan_port,
                                                 ip->src, 0, lan_ip, &lan_port) == 0) {
                            /* Copy and modify the packet */
                            uint8_t forward_frame[VIRTIO_NET_MAX_FRAME_SIZE];
                            if (length <= VIRTIO_NET_MAX_FRAME_SIZE && g_lan_if.dev != NULL) {
                                util_memcpy(forward_frame, frame, length);

                                struct eth_header *fwd_eth = (struct eth_header *)forward_frame;
                                struct ipv4_header *fwd_ip = (struct ipv4_header *)(forward_frame + sizeof(*fwd_eth));
                                struct icmp_header *fwd_icmp = (struct icmp_header *)((uint8_t *)fwd_ip + ip_header_len);

                                /* Update Ethernet header */
                                const uint8_t *lan_mac = virtio_net_get_mac_dev(g_lan_if.dev);
                                util_memcpy(fwd_eth->src, lan_mac, 6);

                                /* Try to get destination MAC from ARP cache */
                                if (!arp_cache_lookup(lan_ip, fwd_eth->dest)) {
                                    uart_puts("[NAT] LAN destination MAC not in cache, dropping packet\n");
                                    return 1;
                                }

                                /* Update IP header */
                                util_memcpy(fwd_ip->dst, lan_ip, 4);
                                fwd_ip->ttl--;
                                fwd_ip->header_checksum = 0u;
                                fwd_ip->header_checksum = util_htons(checksum16(fwd_ip, ip_header_len));

                                /* Update ICMP identifier with original port */
                                fwd_icmp->identifier = util_htons(lan_port);
                                fwd_icmp->checksum = 0u;
                                size_t icmp_len = total_length - ip_header_len;
                                fwd_icmp->checksum = util_htons(checksum16(fwd_icmp, icmp_len));

                                /* Send on LAN interface */
                                virtio_net_send_frame_dev(g_lan_if.dev, forward_frame,
                                                        sizeof(*fwd_eth) + total_length);
                                return 1;
                            }
                        }
                    }
                } else if (ip->protocol == 6u || ip->protocol == 17u) {  /* TCP or UDP */
                    size_t min_transport_len = (ip->protocol == 6u) ? sizeof(struct tcp_header) : sizeof(struct udp_header);

                    if (total_length >= ip_header_len + min_transport_len) {
                        uint16_t wan_port, src_port;
                        uint8_t lan_ip[4];
                        uint16_t lan_port;

                        if (ip->protocol == 6u) {
                            struct tcp_header *tcp = (struct tcp_header *)((uint8_t *)ip + ip_header_len);
                            wan_port = util_ntohs(tcp->dst_port);
                            src_port = util_ntohs(tcp->src_port);
                        } else {
                            struct udp_header *udp = (struct udp_header *)((uint8_t *)ip + ip_header_len);
                            wan_port = util_ntohs(udp->dst_port);
                            src_port = util_ntohs(udp->src_port);
                        }

                        uint8_t proto = (ip->protocol == 6u) ? NAT_PROTO_TCP : NAT_PROTO_UDP;

                        /* Perform reverse NAT translation */
                        if (nat_translate_inbound(proto, wan_port,
                                                 ip->src, src_port, lan_ip, &lan_port) == 0) {
                            uart_puts("[NAT] ");
                            uart_puts((ip->protocol == 6u) ? "TCP" : "UDP");
                            uart_puts(" inbound: ");
                            uart_write_dec(ip->src[0]); uart_putc('.');
                            uart_write_dec(ip->src[1]); uart_putc('.');
                            uart_write_dec(ip->src[2]); uart_putc('.');
                            uart_write_dec(ip->src[3]);
                            uart_putc(':');
                            uart_write_dec(src_port);
                            uart_puts(" -> ");
                            uart_write_dec(lan_ip[0]); uart_putc('.');
                            uart_write_dec(lan_ip[1]); uart_putc('.');
                            uart_write_dec(lan_ip[2]); uart_putc('.');
                            uart_write_dec(lan_ip[3]);
                            uart_putc(':');
                            uart_write_dec(lan_port);
                            uart_puts(" (was WAN:");
                            uart_write_dec(wan_port);
                            uart_puts(")\n");
                            /* Copy and modify the packet */
                            uint8_t forward_frame[VIRTIO_NET_MAX_FRAME_SIZE];
                            if (length <= VIRTIO_NET_MAX_FRAME_SIZE && g_lan_if.dev != NULL) {
                                util_memcpy(forward_frame, frame, length);

                                struct eth_header *fwd_eth = (struct eth_header *)forward_frame;
                                struct ipv4_header *fwd_ip = (struct ipv4_header *)(forward_frame + sizeof(*fwd_eth));

                                /* Update Ethernet header */
                                const uint8_t *lan_mac = virtio_net_get_mac_dev(g_lan_if.dev);
                                util_memcpy(fwd_eth->src, lan_mac, 6);

                                /* Try to get destination MAC from ARP cache */
                                if (!arp_cache_lookup(lan_ip, fwd_eth->dest)) {
                                    return 1;
                                }

                                /* Update IP header */
                                util_memcpy(fwd_ip->dst, lan_ip, 4);
                                fwd_ip->ttl--;
                                fwd_ip->header_checksum = 0u;
                                fwd_ip->header_checksum = util_htons(checksum16(fwd_ip, ip_header_len));

                                /* Update transport port and checksum */
                                if (ip->protocol == 6u) {
                                    struct tcp_header *fwd_tcp = (struct tcp_header *)((uint8_t *)fwd_ip + ip_header_len);
                                    fwd_tcp->dst_port = util_htons(lan_port);
                                    fwd_tcp->checksum = 0u;
                                    size_t tcp_len = total_length - ip_header_len;
                                    fwd_tcp->checksum = tcp_udp_checksum(fwd_ip, fwd_tcp, tcp_len);  /* Direct assignment */
                                } else {
                                    struct udp_header *fwd_udp = (struct udp_header *)((uint8_t *)fwd_ip + ip_header_len);
                                    fwd_udp->dst_port = util_htons(lan_port);
                                    fwd_udp->checksum = 0u;
                                    size_t udp_len = total_length - ip_header_len;
                                    fwd_udp->checksum = tcp_udp_checksum(fwd_ip, fwd_udp, udp_len);  /* Direct assignment */
                                }

                                /* Send on LAN interface */
                                virtio_net_send_frame_dev(g_lan_if.dev, forward_frame,
                                                        sizeof(*fwd_eth) + total_length);
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}

static void net_demo_send_icmp_request(struct net_interface *iface, uint16_t sequence)
{
    if (!iface->peer_mac_valid) {
        return;
    }

    const uint8_t *local_mac = virtio_net_get_mac_dev(iface->dev);
    uint8_t frame[sizeof(struct eth_header) + sizeof(struct ipv4_header) + sizeof(struct icmp_header) + 16u];

    struct eth_header *eth = (struct eth_header *)frame;
    struct ipv4_header *ip = (struct ipv4_header *)(frame + sizeof(*eth));
    struct icmp_header *icmp = (struct icmp_header *)(frame + sizeof(*eth) + sizeof(*ip));
    uint8_t *payload = icmp->data;
    size_t payload_len = 16u;

    util_memcpy(eth->dest, iface->peer_mac, sizeof(eth->dest));
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
    util_memcpy(ip->src, iface->local_ip, sizeof(ip->src));
    util_memcpy(ip->dst, iface->peer_ip, sizeof(ip->dst));
    ip->header_checksum = util_htons(checksum16(ip, sizeof(struct ipv4_header)));

    icmp->type = 8u;
    icmp->code = 0u;
    icmp->identifier = util_htons(0x1234u);
    icmp->sequence = util_htons(sequence);
    for (size_t i = 0u; i < payload_len; ++i) {
        payload[i] = (uint8_t)(i + 1u);
    }
    icmp->checksum = 0u;
    icmp->checksum = util_htons(checksum16(icmp, sizeof(struct icmp_header) + payload_len));

    uart_puts("[net-demo] ");
    uart_puts(iface->name);
    uart_puts(": Sending ICMP echo request\n");
    size_t frame_len = sizeof(struct eth_header) + total_length;
    virtio_net_send_frame_dev(iface->dev, frame, frame_len);
}

void net_demo_run(void)
{
    uart_puts("[net-demo] Initialising VirtIO net driver for all devices\n");

    if (virtio_net_init_all() != 0) {
        uart_puts("[net-demo] Driver initialisation failed\n");
        return;
    }

    /* Initialize NAT subsystem */
    uart_puts("[net-demo] Initializing NAT subsystem\n");
    nat_init();
    uart_puts("[net-demo] NAT ready - LAN (192.168.1.0/24) <-> WAN (10.3.5.99)\n");

    /* Get available device count */
    uint32_t device_count = virtio_net_get_device_count();
    uart_puts("[net-demo] Found ");
    uart_putc((char)('0' + device_count));
    uart_puts(" VirtIO net device(s)\n");

    /* Assign devices to interfaces */
    if (device_count >= 1u) {
        g_lan_if.dev = virtio_net_get_device(0u);
        if (g_lan_if.dev != NULL) {
            const uint8_t *lan_mac = virtio_net_get_mac_dev(g_lan_if.dev);
            uart_puts("[net-demo] ");
            uart_puts(g_lan_if.name);
            uart_puts(" interface:\n");
            uart_puts("[net-demo]   MAC: ");
            print_mac("", lan_mac);
            uart_puts("[net-demo]   IP: ");
            uart_putc((char)('0' + g_lan_if.local_ip[0] / 100u));
            uart_putc((char)('0' + (g_lan_if.local_ip[0] / 10u) % 10u));
            uart_putc((char)('0' + g_lan_if.local_ip[0] % 10u));
            uart_putc('.');
            uart_putc((char)('0' + g_lan_if.local_ip[1] / 100u));
            uart_putc((char)('0' + (g_lan_if.local_ip[1] / 10u) % 10u));
            uart_putc((char)('0' + g_lan_if.local_ip[1] % 10u));
            uart_putc('.');
            uart_putc((char)('0' + g_lan_if.local_ip[2] / 100u));
            uart_putc((char)('0' + (g_lan_if.local_ip[2] / 10u) % 10u));
            uart_putc((char)('0' + g_lan_if.local_ip[2] % 10u));
            uart_putc('.');
            uart_putc((char)('0' + g_lan_if.local_ip[3] / 100u));
            uart_putc((char)('0' + (g_lan_if.local_ip[3] / 10u) % 10u));
            uart_putc((char)('0' + g_lan_if.local_ip[3] % 10u));
            uart_puts("/24\n");
        }
    }

    if (device_count >= 2u) {
        g_wan_if.dev = virtio_net_get_device(1u);
        if (g_wan_if.dev != NULL) {
            const uint8_t *wan_mac = virtio_net_get_mac_dev(g_wan_if.dev);
            uart_puts("[net-demo] ");
            uart_puts(g_wan_if.name);
            uart_puts(" interface:\n");
            uart_puts("[net-demo]   MAC: ");
            print_mac("", wan_mac);
            uart_puts("[net-demo]   IP: ");
            uart_putc((char)('0' + g_wan_if.local_ip[0] / 100u));
            uart_putc((char)('0' + (g_wan_if.local_ip[0] / 10u) % 10u));
            uart_putc((char)('0' + g_wan_if.local_ip[0] % 10u));
            uart_putc('.');
            uart_putc((char)('0' + g_wan_if.local_ip[1] / 100u));
            uart_putc((char)('0' + (g_wan_if.local_ip[1] / 10u) % 10u));
            uart_putc((char)('0' + g_wan_if.local_ip[1] % 10u));
            uart_putc('.');
            uart_putc((char)('0' + g_wan_if.local_ip[2] / 100u));
            uart_putc((char)('0' + (g_wan_if.local_ip[2] / 10u) % 10u));
            uart_putc((char)('0' + g_wan_if.local_ip[2] % 10u));
            uart_putc('.');
            uart_putc((char)('0' + g_wan_if.local_ip[3] / 100u));
            uart_putc((char)('0' + (g_wan_if.local_ip[3] / 10u) % 10u));
            uart_putc((char)('0' + g_wan_if.local_ip[3] % 10u));
            uart_puts("/24\n");
        }
    }

    /* Send initial ARP requests for both interfaces */
    if (g_lan_if.dev != NULL) {
        net_demo_send_arp_request(&g_lan_if);
    }
    if (g_wan_if.dev != NULL) {
        net_demo_send_arp_request(&g_wan_if);
    }

    uint8_t rx_buffer[VIRTIO_NET_MAX_FRAME_SIZE];
    size_t rx_length = 0u;
    uint32_t idle_ticks = 0u;
    uint16_t lan_icmp_sequence = 1u;
    uint16_t wan_icmp_sequence = 1u;
    uint32_t lan_echo_period = 0u;
    uint32_t wan_echo_period = 0u;

    for (;;) {
        /* Poll LAN interface for packets */
        if (g_lan_if.dev != NULL && virtio_net_has_pending_rx_dev(g_lan_if.dev)) {
            while (1) {
                int rc = virtio_net_poll_frame_dev(g_lan_if.dev, rx_buffer, &rx_length);
                if (rc < 0) {
                    uart_puts("[net-demo] ");
                    uart_puts(g_lan_if.name);
                    uart_puts(": RX error\n");
                    break;
                } else if (rc > 0) {
                    if (net_demo_process_frame(&g_lan_if, rx_buffer, rx_length) != 0) {
                        idle_ticks = 0u;
                        lan_echo_period = 0u;
                    }
                } else {
                    break;
                }
            }
        }

        /* Poll WAN interface for packets */
        if (g_wan_if.dev != NULL && virtio_net_has_pending_rx_dev(g_wan_if.dev)) {
            while (1) {
                int rc = virtio_net_poll_frame_dev(g_wan_if.dev, rx_buffer, &rx_length);
                if (rc < 0) {
                    uart_puts("[net-demo] ");
                    uart_puts(g_wan_if.name);
                    uart_puts(": RX error\n");
                    break;
                } else if (rc > 0) {
                    if (net_demo_process_frame(&g_wan_if, rx_buffer, rx_length) != 0) {
                        idle_ticks = 0u;
                        wan_echo_period = 0u;
                    }
                } else {
                    break;
                }
            }
        }

        /* Periodic tasks: send ARP requests for both interfaces */
        if (++idle_ticks >= 10u) {
            idle_ticks = 0u;
            if (g_lan_if.dev != NULL) {
                net_demo_send_arp_request(&g_lan_if);
            }
            if (g_wan_if.dev != NULL) {
                net_demo_send_arp_request(&g_wan_if);
            }
        }

        /* Send periodic ICMP pings for LAN interface */
        if (g_lan_if.dev != NULL && g_lan_if.peer_mac_valid) {
            if (++lan_echo_period >= 5u) {
                lan_echo_period = 0u;
                net_demo_send_icmp_request(&g_lan_if, lan_icmp_sequence++);
            }
        }

        /* Send periodic ICMP pings for WAN interface */
        if (g_wan_if.dev != NULL && g_wan_if.peer_mac_valid) {
            if (++wan_echo_period >= 5u) {
                wan_echo_period = 0u;
                net_demo_send_icmp_request(&g_wan_if, wan_icmp_sequence++);
            }
        }

        /* Sleep to avoid busy-waiting */
        OSTimeDlyHMSM(0u, 0u, 0u, NET_DEMO_POLL_DELAY_MS);
    }
}
