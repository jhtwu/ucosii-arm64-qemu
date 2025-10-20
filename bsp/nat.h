/*
 * NAT (Network Address Translation) Header
 *
 * Provides SNAT (Source NAT) functionality for LAN to WAN translation
 * Supports ICMP, UDP, and TCP protocols with connection tracking
 */

#ifndef _NAT_H_
#define _NAT_H_

#include <stdint.h>
#include <stdbool.h>

/* NAT Configuration */
#define NAT_TABLE_SIZE          64      /* Maximum concurrent NAT sessions */
#define NAT_TIMEOUT_ICMP        60      /* ICMP session timeout (seconds) */
#define NAT_TIMEOUT_UDP         120     /* UDP session timeout (seconds) */
#define NAT_TIMEOUT_TCP_EST     300     /* TCP established timeout (seconds) */
#define NAT_TIMEOUT_TCP_INIT    60      /* TCP initial timeout (seconds) */

/* ARP Table Configuration */
#define ARP_TABLE_SIZE          32      /* Maximum ARP cache entries */
#define ARP_TIMEOUT             300     /* ARP entry timeout (seconds) */

/* Protocol types */
typedef enum {
    NAT_PROTO_ICMP = 1,
    NAT_PROTO_TCP  = 6,
    NAT_PROTO_UDP  = 17
} nat_proto_t;

/* NAT direction */
typedef enum {
    NAT_DIR_OUTBOUND,   /* LAN -> WAN (SNAT) */
    NAT_DIR_INBOUND     /* WAN -> LAN (reverse SNAT) */
} nat_dir_t;

/* NAT session entry */
struct nat_entry {
    bool     active;            /* Entry is in use */
    uint8_t  protocol;          /* Protocol: ICMP, TCP, UDP */

    /* Original (LAN side) */
    uint8_t  lan_ip[4];         /* LAN source IP */
    uint16_t lan_port;          /* LAN source port (or ICMP ID) */

    /* Translated (WAN side) */
    uint16_t wan_port;          /* WAN source port (or ICMP ID) */

    /* Destination (for reverse lookup) */
    uint8_t  dst_ip[4];         /* Destination IP */
    uint16_t dst_port;          /* Destination port */

    /* Timing */
    uint32_t last_activity;     /* Timestamp of last packet (in ticks) */
    uint16_t timeout_sec;       /* Timeout in seconds */
};

/* NAT statistics */
struct nat_stats {
    uint32_t translations_out;  /* Outbound translations */
    uint32_t translations_in;   /* Inbound translations */
    uint32_t table_full;        /* Table full errors */
    uint32_t no_match;          /* No matching entry found */
    uint32_t timeouts;          /* Expired entries */
};

/* ARP cache entry */
struct arp_entry {
    bool     active;            /* Entry is in use */
    uint8_t  ip[4];             /* IP address */
    uint8_t  mac[6];            /* MAC address */
    uint32_t last_update;       /* Timestamp of last update (in ticks) */
};

/* NAT configuration structure */
struct nat_config {
    uint8_t  lan_ip[4];         /* Gateway LAN IP (192.168.1.1) */
    uint8_t  wan_ip[4];         /* Gateway WAN IP (10.3.5.99) */
    uint16_t port_range_start;  /* Dynamic port allocation start */
    uint16_t port_range_end;    /* Dynamic port allocation end */
};

/* NAT Initialization and Configuration */

/**
 * nat_init() - Initialize NAT subsystem
 *
 * Must be called before any NAT operations.
 * Initializes the NAT translation table and statistics.
 */
void nat_init(void);

/**
 * nat_configure() - Configure NAT parameters
 * @lan_ip: Gateway LAN IP address (e.g., 192.168.1.1)
 * @wan_ip: Gateway WAN IP address (e.g., 10.3.5.99)
 *
 * Sets the IP addresses for NAT translation.
 */
void nat_configure(const uint8_t lan_ip[4], const uint8_t wan_ip[4]);

/* NAT Translation Operations */

/**
 * nat_translate_outbound() - Perform outbound NAT translation (LAN -> WAN)
 * @protocol: Protocol type (ICMP, TCP, UDP)
 * @lan_ip: Original LAN source IP
 * @lan_port: Original LAN source port/ICMP ID
 * @dst_ip: Destination IP
 * @dst_port: Destination port
 * @wan_port: Output parameter for translated WAN port/ICMP ID
 *
 * Creates or updates a NAT session and returns the translated port.
 *
 * Returns: 0 on success, -1 on error (table full)
 */
int nat_translate_outbound(uint8_t protocol, const uint8_t lan_ip[4], uint16_t lan_port,
                          const uint8_t dst_ip[4], uint16_t dst_port, uint16_t *wan_port);

/**
 * nat_translate_inbound() - Perform inbound NAT translation (WAN -> LAN)
 * @protocol: Protocol type (ICMP, TCP, UDP)
 * @wan_port: WAN port/ICMP ID to look up
 * @src_ip: Source IP (must match original dst_ip)
 * @src_port: Source port (must match original dst_port)
 * @lan_ip: Output parameter for original LAN IP
 * @lan_port: Output parameter for original LAN port/ICMP ID
 *
 * Looks up an existing NAT session and returns the original LAN address.
 *
 * Returns: 0 on success, -1 if no matching entry found
 */
int nat_translate_inbound(uint8_t protocol, uint16_t wan_port,
                         const uint8_t src_ip[4], uint16_t src_port,
                         uint8_t lan_ip[4], uint16_t *lan_port);

/* NAT Table Management */

/**
 * nat_cleanup_expired() - Remove expired NAT entries
 * @current_ticks: Current system tick count
 *
 * Should be called periodically to clean up stale entries.
 *
 * Returns: Number of entries removed
 */
int nat_cleanup_expired(uint32_t current_ticks);

/**
 * nat_get_stats() - Get NAT statistics
 *
 * Returns: Pointer to NAT statistics structure
 */
const struct nat_stats *nat_get_stats(void);

/**
 * nat_reset_stats() - Reset NAT statistics counters
 */
void nat_reset_stats(void);

/**
 * nat_print_table() - Print NAT table for debugging
 *
 * Dumps the current NAT translation table to console.
 */
void nat_print_table(void);

/* Helper Functions */

/**
 * nat_is_lan_ip() - Check if IP is in LAN subnet
 * @ip: IP address to check
 *
 * Returns: true if IP is in configured LAN subnet
 */
bool nat_is_lan_ip(const uint8_t ip[4]);

/**
 * nat_is_wan_ip() - Check if IP is our WAN address
 * @ip: IP address to check
 *
 * Returns: true if IP matches configured WAN address
 */
bool nat_is_wan_ip(const uint8_t ip[4]);

/* ARP Cache Management */

/**
 * arp_cache_add() - Add or update an ARP cache entry
 * @ip: IP address
 * @mac: MAC address
 *
 * Adds a new ARP entry or updates an existing one.
 */
void arp_cache_add(const uint8_t ip[4], const uint8_t mac[6]);

/**
 * arp_cache_lookup() - Look up MAC address for an IP
 * @ip: IP address to look up
 * @mac: Output buffer for MAC address (6 bytes)
 *
 * Returns: true if entry found, false otherwise
 */
bool arp_cache_lookup(const uint8_t ip[4], uint8_t mac[6]);

/**
 * arp_cache_cleanup() - Remove expired ARP entries
 * @current_ticks: Current system tick count
 *
 * Returns: Number of entries removed
 */
int arp_cache_cleanup(uint32_t current_ticks);

/**
 * arp_cache_print() - Print ARP cache for debugging
 */
void arp_cache_print(void);

#endif /* _NAT_H_ */
