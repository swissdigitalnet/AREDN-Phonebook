// uac_traceroute.c
// ICMP Traceroute Implementation for Network Topology Discovery
// Traces network path to phones and measures hop-by-hop RTT

#define MODULE_NAME "UAC_TRACEROUTE"

#include "uac_traceroute.h"
#include "../common.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <netdb.h>

// Traceroute constants
#define TRACEROUTE_PORT_BASE 33434
#define TRACEROUTE_TIMEOUT_SEC 2
#define TRACEROUTE_PROBE_SIZE 40

// Helper: Get current time in milliseconds
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

/**
 * Reverse DNS lookup for an IP address
 */
int reverse_dns_lookup(const char *ip, char *hostname, size_t hostname_len) {
    if (!ip || !hostname || hostname_len == 0) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        LOG_WARN("Invalid IP address for reverse DNS: %s", ip);
        strncpy(hostname, "INVALID", hostname_len - 1);
        hostname[hostname_len - 1] = '\0';
        return -1;
    }

    char host[NI_MAXHOST];
    int result = getnameinfo((struct sockaddr*)&addr, sizeof(addr),
                            host, sizeof(host), NULL, 0, 0);

    if (result == 0) {
        // Strip .local.mesh suffix if present
        char *dot = strstr(host, ".local.mesh");
        if (dot) {
            *dot = '\0';
        }

        // Strip hostname prefix (mid1., mid2., dtdlink., etc.)
        // Import the stripping logic inline to avoid circular dependency
        const char *final_hostname = host;
        const char *prefix_dot = strchr(host, '.');
        if (prefix_dot) {
            int prefix_len = prefix_dot - host;
            // Check if it looks like a prefix (short, lowercase+digits)
            if (prefix_len > 0 && prefix_len < 10) {
                bool is_prefix = true;
                for (int i = 0; i < prefix_len; i++) {
                    char c = host[i];
                    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
                        is_prefix = false;
                        break;
                    }
                }
                if (is_prefix) {
                    final_hostname = prefix_dot + 1;  // Skip prefix
                }
            }
        }

        strncpy(hostname, final_hostname, hostname_len - 1);
        hostname[hostname_len - 1] = '\0';
        LOG_DEBUG("Reverse DNS: %s -> %s", ip, hostname);
        return 0;
    } else {
        // DNS lookup failed - use UNKNOWN
        strncpy(hostname, "UNKNOWN", hostname_len - 1);
        hostname[hostname_len - 1] = '\0';
        LOG_DEBUG("Reverse DNS failed for %s: %s", ip, gai_strerror(result));
        return -1;
    }
}

/**
 * Get source IP address used to reach a target
 */
int get_source_ip_for_target(const char *target_ip, char *source_ip) {
    if (!target_ip || !source_ip) {
        return -1;
    }

    // Create a UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        LOG_ERROR("Failed to create socket for source IP detection: %s", strerror(errno));
        return -1;
    }

    // Set up target address (port doesn't matter, we won't actually send)
    struct sockaddr_in target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(9); // Discard port

    if (inet_pton(AF_INET, target_ip, &target_addr.sin_addr) != 1) {
        LOG_ERROR("Invalid target IP address: %s", target_ip);
        close(sockfd);
        return -1;
    }

    // Connect to target (doesn't actually send anything)
    if (connect(sockfd, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
        LOG_ERROR("Failed to connect socket for source IP detection: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Get local socket address (this is the source IP that would be used)
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sockfd, (struct sockaddr*)&local_addr, &addr_len) < 0) {
        LOG_ERROR("Failed to get local socket name: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Convert to string
    if (inet_ntop(AF_INET, &local_addr.sin_addr, source_ip, INET_ADDRSTRLEN) == NULL) {
        LOG_ERROR("Failed to convert source IP to string: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    close(sockfd);
    LOG_DEBUG("Source IP for target %s: %s", target_ip, source_ip);
    return 0;
}

/**
 * Send UDP probe with specific TTL and wait for ICMP response
 */
static int send_traceroute_probe(int send_sock, int recv_sock,
                                 struct sockaddr_in *dest_addr,
                                 int ttl, int seq,
                                 char *hop_ip, float *rtt_ms) {
    char send_buf[TRACEROUTE_PROBE_SIZE];
    char recv_buf[512];

    // Set TTL on send socket
    if (setsockopt(send_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
        LOG_WARN("Failed to set TTL=%d: %s", ttl, strerror(errno));
        return -1;
    }

    // Set destination port (increments with each probe)
    dest_addr->sin_port = htons(TRACEROUTE_PORT_BASE + ttl);

    // Send UDP probe
    memset(send_buf, 0, sizeof(send_buf));
    double start_time = get_time_ms();

    ssize_t sent = sendto(send_sock, send_buf, sizeof(send_buf), 0,
                         (struct sockaddr*)dest_addr, sizeof(*dest_addr));
    if (sent < 0) {
        LOG_WARN("Failed to send probe for TTL=%d: %s", ttl, strerror(errno));
        return -1;
    }

    // Wait for ICMP response
    struct timeval timeout;
    timeout.tv_sec = TRACEROUTE_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(recv_sock, &readfds);

    int ret = select(recv_sock + 1, &readfds, NULL, NULL, &timeout);
    if (ret <= 0) {
        // Timeout or error
        LOG_DEBUG("No response for TTL=%d (timeout)", ttl);
        return -1;
    }

    // Receive ICMP response
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received = recvfrom(recv_sock, recv_buf, sizeof(recv_buf), 0,
                               (struct sockaddr*)&from_addr, &from_len);
    if (received < 0) {
        LOG_WARN("Failed to receive ICMP response: %s", strerror(errno));
        return -1;
    }

    double end_time = get_time_ms();
    *rtt_ms = (float)(end_time - start_time);

    // Parse ICMP response
    struct ip *ip_hdr = (struct ip *)recv_buf;
    int ip_hdr_len = ip_hdr->ip_hl << 2;
    struct icmp *icmp_hdr = (struct icmp *)(recv_buf + ip_hdr_len);

    // Extract hop IP address
    if (inet_ntop(AF_INET, &from_addr.sin_addr, hop_ip, INET_ADDRSTRLEN) == NULL) {
        LOG_WARN("Failed to convert hop IP to string");
        return -1;
    }

    // Check ICMP message type
    if (icmp_hdr->icmp_type == ICMP_TIME_EXCEEDED) {
        // Intermediate hop responded
        LOG_DEBUG("TTL=%d: Got TIME_EXCEEDED from %s (%.2f ms)", ttl, hop_ip, *rtt_ms);
        return 0; // Intermediate hop
    } else if (icmp_hdr->icmp_type == ICMP_DEST_UNREACH) {
        // Destination reached (port unreachable - expected for UDP probe)
        LOG_DEBUG("TTL=%d: Got DEST_UNREACH from %s (%.2f ms) - destination reached",
                 ttl, hop_ip, *rtt_ms);
        return 1; // Destination reached
    } else {
        LOG_DEBUG("TTL=%d: Unexpected ICMP type %d from %s", ttl, icmp_hdr->icmp_type, hop_ip);
        return -1;
    }
}

/**
 * Perform ICMP traceroute to a phone
 */
int uac_traceroute_to_phone(const char *phone_number, int max_hops,
                           TracerouteHop *results, int *hop_count) {
    if (!phone_number || !results || !hop_count || max_hops <= 0 || max_hops > MAX_TRACEROUTE_HOPS) {
        LOG_ERROR("Invalid parameters for traceroute");
        return -1;
    }

    *hop_count = 0;

    LOG_INFO("Starting traceroute to %s (max %d hops)", phone_number, max_hops);

    // Resolve phone number to IP
    char hostname[128];
    snprintf(hostname, sizeof(hostname), "%s.%s", phone_number, AREDN_MESH_DOMAIN);

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = NULL;

    int status = getaddrinfo(hostname, NULL, &hints, &res);
    if (status != 0) {
        LOG_WARN("Failed to resolve %s: %s", hostname, gai_strerror(status));
        return -1;
    }

    struct sockaddr_in target_addr;
    memcpy(&target_addr, res->ai_addr, sizeof(target_addr));
    freeaddrinfo(res);

    char target_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &target_addr.sin_addr, target_ip, sizeof(target_ip));
    LOG_INFO("Resolved %s to %s", phone_number, target_ip);

    // Create send socket (UDP)
    int send_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sock < 0) {
        LOG_ERROR("Failed to create send socket: %s", strerror(errno));
        return -1;
    }

    // Create receive socket (RAW ICMP - requires root/CAP_NET_RAW)
    int recv_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (recv_sock < 0) {
        LOG_ERROR("Failed to create ICMP socket: %s (requires root/CAP_NET_RAW)", strerror(errno));
        close(send_sock);
        return -1;
    }

    // Traceroute loop
    bool reached_destination = false;
    for (int ttl = 1; ttl <= max_hops && !reached_destination; ttl++) {
        char hop_ip[INET_ADDRSTRLEN] = "";
        float rtt_ms = 0.0;

        int probe_result = send_traceroute_probe(send_sock, recv_sock, &target_addr,
                                                 ttl, *hop_count, hop_ip, &rtt_ms);

        if (probe_result < 0) {
            // Timeout or error - record as timeout hop
            results[*hop_count].hop_number = ttl;
            strcpy(results[*hop_count].ip_address, "*");
            strcpy(results[*hop_count].hostname, "TIMEOUT");
            results[*hop_count].rtt_ms = 0.0;
            results[*hop_count].timeout = true;
            (*hop_count)++;

            LOG_DEBUG("Hop %d: * (timeout)", ttl);
            continue;
        }

        // Got a response - perform reverse DNS lookup
        char hostname[256];
        reverse_dns_lookup(hop_ip, hostname, sizeof(hostname));

        // Store hop information
        results[*hop_count].hop_number = ttl;
        strncpy(results[*hop_count].ip_address, hop_ip, INET_ADDRSTRLEN - 1);
        results[*hop_count].ip_address[INET_ADDRSTRLEN - 1] = '\0';
        strncpy(results[*hop_count].hostname, hostname, 255);
        results[*hop_count].hostname[255] = '\0';
        results[*hop_count].rtt_ms = rtt_ms;
        results[*hop_count].timeout = false;
        (*hop_count)++;

        LOG_INFO("Hop %d: %s (%s) - %.2f ms", ttl, hostname, hop_ip, rtt_ms);

        // Check if we reached the destination
        if (probe_result == 1 || strcmp(hop_ip, target_ip) == 0) {
            LOG_INFO("Reached destination %s after %d hops", target_ip, ttl);
            reached_destination = true;
        }
    }

    close(send_sock);
    close(recv_sock);

    if (!reached_destination) {
        LOG_WARN("Traceroute to %s stopped after %d hops (destination not reached)",
                 phone_number, max_hops);
    }

    LOG_INFO("Traceroute complete: %s - %d hops discovered", phone_number, *hop_count);
    return 0;
}
