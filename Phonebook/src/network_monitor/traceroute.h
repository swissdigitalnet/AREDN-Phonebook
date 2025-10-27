// traceroute.h
// ICMP Traceroute Implementation for Network Topology Discovery
// Traces network path to phones and measures hop-by-hop RTT

#ifndef UAC_TRACEROUTE_H
#define UAC_TRACEROUTE_H

#include <stdbool.h>
#include <netinet/in.h>

// Maximum number of hops to trace
#define MAX_TRACEROUTE_HOPS 30

/**
 * Traceroute hop information
 */
typedef struct {
    int hop_number;                    // 1-indexed hop count
    char ip_address[INET_ADDRSTRLEN]; // Hop IP address (e.g., "10.51.55.1")
    char hostname[256];                // Reverse DNS name (e.g., "hb9bla-mikrotik-1")
    float rtt_ms;                      // Round-trip time in milliseconds
    bool timeout;                      // True if hop didn't respond
} TracerouteHop;

/**
 * Perform ICMP traceroute to a phone
 *
 * Sends UDP probes with incrementing TTL values and listens for ICMP
 * TIME_EXCEEDED messages to discover the network path.
 *
 * @param phone_number Target phone number (e.g., "196330")
 * @param max_hops Maximum number of hops to trace (default: 20)
 * @param results Output array to store hop information
 * @param hop_count Output: number of hops discovered
 * @return 0 on success, -1 on error
 */
int traceroute_to_phone(
    const char *phone_number,
    int max_hops,
    TracerouteHop *results,
    int *hop_count
);

/**
 * Reverse DNS lookup for an IP address
 *
 * Converts IP address to hostname using getnameinfo().
 * Strips .local.mesh suffix if present.
 *
 * @param ip IP address string (e.g., "10.51.55.1")
 * @param hostname Output buffer for hostname
 * @param hostname_len Size of hostname buffer
 * @return 0 on success, -1 on error
 */
int reverse_dns_lookup(
    const char *ip,
    char *hostname,
    size_t hostname_len
);

/**
 * Get source IP address used to reach a target
 *
 * Creates a UDP socket, connects to target (doesn't send), and
 * queries the local address. This discovers which interface/IP
 * would be used for routing to the target.
 *
 * @param target_ip Destination IP address
 * @param source_ip Output buffer for source IP (INET_ADDRSTRLEN bytes)
 * @return 0 on success, -1 on error
 */
int get_source_ip_for_target(
    const char *target_ip,
    char *source_ip
);

#endif // UAC_TRACEROUTE_H
