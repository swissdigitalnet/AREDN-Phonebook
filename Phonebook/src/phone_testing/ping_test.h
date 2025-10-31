// ping_test.h - SIP OPTIONS and PING testing with RTT/jitter measurement
#ifndef PING_TEST_H
#define PING_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

// Maximum number of ping samples to collect
#define MAX_PING_SAMPLES 20

// Timing test result structure (used for both ping and options tests)
typedef struct {
    bool online;              // Phone responded to at least one request
    int packets_sent;         // Total packets sent
    int packets_received;     // Total packets received
    float packet_loss_pct;    // Packet loss percentage
    float min_rtt_ms;         // Minimum RTT in milliseconds
    float max_rtt_ms;         // Maximum RTT in milliseconds
    float avg_rtt_ms;         // Average RTT in milliseconds
    float jitter_ms;          // Jitter (variance in RTT) in milliseconds
    float samples[MAX_PING_SAMPLES]; // Individual RTT samples
} ping_test_result_t;

/**
 * Send multiple ICMP ping requests to a phone and measure RTT/jitter
 * Uses real ICMP ECHO packets (network layer)
 * @param phone_number Target phone number (e.g., "441530") - used to resolve DNS
 * @param server_ip Not used (kept for API compatibility)
 * @param ping_count Number of ICMP ping requests to send
 * @return Timing test result
 */
ping_test_result_t ping_test_icmp(const char *phone_number,
                                   const char *server_ip,
                                   int ping_count);

/**
 * Send multiple SIP OPTIONS requests to a phone and measure RTT/jitter
 * Uses SIP OPTIONS method (application layer)
 * @param phone_number Target phone number (e.g., "441530")
 * @param server_ip SIP server/proxy IP address
 * @param ping_count Number of OPTIONS requests to send
 * @return Timing test result
 */
ping_test_result_t ping_test_options(const char *phone_number,
                                      const char *server_ip,
                                      int ping_count);

/**
 * Calculate timing statistics from RTT samples
 * @param samples Array of RTT samples in milliseconds
 * @param sample_count Number of samples
 * @param result Output statistics structure
 */
void ping_test_calculate_stats(float *samples, int sample_count, ping_test_result_t *result);

#endif // PING_TEST_H
