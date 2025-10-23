#define MODULE_NAME "UAC_BULK"

#include "uac_bulk_tester.h"
#include "uac_test_db.h"
#include "uac_traceroute.h"
#include "topology_db.h"
#include "../common.h"
#include "../config_loader/config_loader.h"
#include "../passive_safety/passive_safety.h"
#include "../software_health/software_health.h"
#include "uac.h"
#include "uac_ping.h"
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void *uac_bulk_tester_thread(void *arg) {
    (void)arg;

    LOG_INFO("UAC Bulk Tester thread started. Interval: %d seconds", g_uac_test_interval_seconds);

    // Register this thread for health monitoring
    int thread_index = health_register_thread(pthread_self(), "uac_bulk_tester");
    if (thread_index < 0) {
        LOG_WARN("Failed to register UAC bulk tester thread for health monitoring");
        // Continue anyway - health monitoring is not critical for operation
    }

    // If interval is 0, bulk testing is disabled
    if (g_uac_test_interval_seconds <= 0) {
        LOG_INFO("UAC bulk testing disabled (interval = %d). Thread exiting.", g_uac_test_interval_seconds);
        return NULL;
    }

    // Initialize shared memory database
    if (uac_test_db_init() != 0) {
        LOG_ERROR("Failed to initialize test database. Thread exiting.");
        return NULL;
    }

    // Wait for initial phonebook load (give phonebook_fetcher time to populate users)
    LOG_INFO("Waiting 60 seconds for initial phonebook load...");
    sleep(60);

    // Track previous cycle's dns_resolved count for UI display during testing
    // Persist across restarts for accurate display on first cycle
    static int prev_dns_resolved = 0;
    static int prev_phones_online = 48;  // Default to reasonable value

    // Try to load previous dns_resolved count from file
    FILE *dns_file = fopen("/tmp/uac_last_dns_resolved.txt", "r");
    if (dns_file) {
        fscanf(dns_file, "%d", &prev_dns_resolved);
        fclose(dns_file);
        LOG_INFO("Loaded previous testable phone count: %d", prev_dns_resolved);
    }

    // Try to load previous phones_online count from file
    FILE *online_file = fopen("/tmp/uac_last_phones_online.txt", "r");
    if (online_file) {
        fscanf(online_file, "%d", &prev_phones_online);
        fclose(online_file);
        LOG_INFO("Loaded previous online phone count: %d", prev_phones_online);
    }

    while (1) {
        // Passive Safety: Update heartbeat
        g_bulk_tester_last_heartbeat = time(NULL);

        // Health Monitoring: Update heartbeat
        if (thread_index >= 0) {
            health_update_heartbeat(thread_index);
        }

        LOG_INFO("=== Starting UAC bulk test cycle ===");

        // Initialize topology database for this cycle
        if (g_uac_traceroute_enabled) {
            topology_db_init();
            topology_db_reset();
            LOG_INFO("Topology database initialized for scan cycle");
        }

        // Open results file for writing (truncate existing)
        FILE *results_file = fopen("/tmp/uac_bulk_results.txt", "w");
        if (!results_file) {
            LOG_WARN("Failed to open /tmp/uac_bulk_results.txt for writing");
        }

        int total_users = 0;
        int dns_resolved = 0;
        int dns_failed = 0;
        int tests_triggered = 0;
        int phones_online = 0;      // Phones that responded to SIP OPTIONS or INVITE
        int phones_offline = 0;     // DNS resolved but no SIP response
        float total_avg_rtt = 0.0;  // Sum of average RTTs for calculating overall average
        int rtt_count = 0;          // Count of phones with valid RTT measurements

        // Lock the user table to count total users first
        pthread_mutex_lock(&registered_users_mutex);

        // Count total registered users (for estimation if no previous cycle data)
        for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
            if (registered_users[i].user_id[0] != '\0') {
                total_users++;
            }
        }

        pthread_mutex_unlock(&registered_users_mutex);

        // Initialize header with previous cycle's online phone count for accurate display
        // This shows correct "X of Y" during the test cycle
        uac_test_db_update_header(0, prev_phones_online, g_uac_test_interval_seconds);
        LOG_DEBUG("Initialized header with %d reachable phones (from previous cycle)", prev_phones_online);

        // Reset counters and lock for main testing loop
        total_users = 0;
        pthread_mutex_lock(&registered_users_mutex);

        for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
            RegisteredUser *user = &registered_users[i];

            // Skip empty slots
            if (user->user_id[0] == '\0') {
                continue;
            }

            total_users++;

            // Build hostname for DNS check: <phone_number>.local.mesh
            char hostname[MAX_USER_ID_LEN + sizeof(AREDN_MESH_DOMAIN) + 2];
            snprintf(hostname, sizeof(hostname), "%s.%s", user->user_id, AREDN_MESH_DOMAIN);

            // Check DNS resolution
            struct addrinfo hints = {0};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            struct addrinfo *res = NULL;

            int gai_status = getaddrinfo(hostname, NULL, &hints, &res);

            if (gai_status == 0) {
                // DNS resolved - node is reachable
                dns_resolved++;

                // Get IP address for logging
                char ip_str[INET_ADDRSTRLEN] = "unknown";
                if (res && res->ai_family == AF_INET) {
                    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
                    inet_ntop(AF_INET, &addr->sin_addr, ip_str, sizeof(ip_str));
                }

                LOG_INFO("[%d/%d] Testing %s (%s) - DNS resolved to %s",
                         dns_resolved, total_users, user->user_id, user->display_name, ip_str);

                freeaddrinfo(res);

                // Release mutex before testing (tests may take time)
                pthread_mutex_unlock(&registered_users_mutex);

                // Initialize result variables
                char ping_status[16] = "UNKNOWN";
                float ping_rtt = 0.0;
                float ping_jitter = 0.0;
                char options_status[16] = "UNKNOWN";
                float options_rtt = 0.0;
                float options_jitter = 0.0;

                // ====================================================
                // PHASE 1: Ping Test (ICMP - Network Layer)
                // ====================================================
                if (g_uac_ping_count > 0) {
                    LOG_INFO("Testing %s (%s) with ping (%d pings)...",
                             user->user_id, user->display_name, g_uac_ping_count);

                    uac_timing_result ping_result = uac_ping_test(
                        user->user_id, g_server_ip, g_uac_ping_count);

                    if (ping_result.online) {
                        snprintf(ping_status, sizeof(ping_status), "ONLINE");
                        ping_rtt = ping_result.avg_rtt_ms;
                        ping_jitter = ping_result.jitter_ms;

                        LOG_INFO("✓ Phone %s ONLINE (ping)", user->user_id);
                        LOG_INFO("  Packets: %d sent, %d received (%.1f%% loss)",
                                 ping_result.packets_sent, ping_result.packets_received,
                                 ping_result.packet_loss_pct);
                        LOG_INFO("  RTT: min=%.2f ms, avg=%.2f ms, max=%.2f ms, jitter=%.2f ms",
                                 ping_result.min_rtt_ms, ping_result.avg_rtt_ms,
                                 ping_result.max_rtt_ms, ping_result.jitter_ms);

                        // Track RTT stats for summary
                        total_avg_rtt += ping_result.avg_rtt_ms;
                        rtt_count++;
                    } else {
                        snprintf(ping_status, sizeof(ping_status), "OFFLINE");
                        LOG_WARN("✗ Phone %s no response to ping", user->user_id);

                        // Skip OPTIONS test if ping failed (no network connectivity)
                        snprintf(options_status, sizeof(options_status), "OFFLINE");
                        LOG_INFO("Skipping OPTIONS test for %s (ping failed, no network connectivity)", user->user_id);
                        phones_offline++;

                        // Write results to database
                        uac_test_result_t db_result = {0};
                        strncpy(db_result.phone_number, user->user_id, sizeof(db_result.phone_number) - 1);
                        strncpy(db_result.ping_status, ping_status, sizeof(db_result.ping_status) - 1);
                        db_result.ping_rtt = ping_rtt;
                        db_result.ping_jitter = ping_jitter;
                        strncpy(db_result.options_status, options_status, sizeof(db_result.options_status) - 1);
                        db_result.options_rtt = options_rtt;
                        db_result.options_jitter = options_jitter;
                        uac_test_db_write_result(&db_result);

                        // Write to file
                        if (results_file) {
                            fprintf(results_file, "%s|%s|%s|%.2f|%.2f|%s|%.2f|%.2f\n",
                                    user->user_id, user->display_name,
                                    ping_status, ping_rtt, ping_jitter,
                                    options_status, options_rtt, options_jitter);
                            fflush(results_file);
                        }

                        // Re-acquire mutex and continue to next user
                        pthread_mutex_lock(&registered_users_mutex);
                        continue;
                    }
                } else {
                    snprintf(ping_status, sizeof(ping_status), "DISABLED");
                }

                // ====================================================
                // PHASE 2: Options Test (SIP OPTIONS - Application Layer)
                // ====================================================
                // Only run if ping succeeded or ping is disabled
                if (g_uac_options_count > 0) {
                    LOG_INFO("Testing %s (%s) with options (%d requests)...",
                             user->user_id, user->display_name, g_uac_options_count);

                    uac_timing_result options_result = uac_options_test(
                        user->user_id, g_server_ip, g_uac_options_count);

                    if (options_result.online) {
                        phones_online++;
                        tests_triggered++;

                        snprintf(options_status, sizeof(options_status), "ONLINE");
                        options_rtt = options_result.avg_rtt_ms;
                        options_jitter = options_result.jitter_ms;

                        LOG_INFO("✓ Phone %s ONLINE (options)", user->user_id);
                        LOG_INFO("  Packets: %d sent, %d received (%.1f%% loss)",
                                 options_result.packets_sent, options_result.packets_received,
                                 options_result.packet_loss_pct);
                        LOG_INFO("  RTT: min=%.2f ms, avg=%.2f ms, max=%.2f ms, jitter=%.2f ms",
                                 options_result.min_rtt_ms, options_result.avg_rtt_ms,
                                 options_result.max_rtt_ms, options_result.jitter_ms);

                        // Track RTT stats for summary (only if ping wasn't counted)
                        if (g_uac_ping_count <= 0) {
                            total_avg_rtt += options_result.avg_rtt_ms;
                            rtt_count++;
                        }

                        // ====================================================
                        // PHASE 2.5: Traceroute Test (NEW - Network Topology Discovery)
                        // Run traceroute for online phones to map network topology
                        // ====================================================
                        if (g_uac_traceroute_enabled) {
                            LOG_INFO("Tracing route to %s (%s)...", user->user_id, user->display_name);

                            TracerouteHop hops[30];
                            int hop_count = 0;

                            if (uac_traceroute_to_phone(user->user_id, g_uac_traceroute_max_hops, hops, &hop_count) == 0) {
                                LOG_DEBUG("Traced %d hops to %s", hop_count, user->user_id);

                                // Get source IP for this route
                                char source_ip[INET_ADDRSTRLEN];
                                if (get_source_ip_for_target(ip_str, source_ip) == 0) {
                                    // Add destination node (the phone)
                                    topology_db_add_node(ip_str, "phone", user->display_name,
                                                       NULL, NULL, "ONLINE");

                                    // Process hops and build topology
                                    char prev_ip[INET_ADDRSTRLEN];
                                    strncpy(prev_ip, source_ip, sizeof(prev_ip));

                                    for (int h = 0; h < hop_count; h++) {
                                        if (hops[h].timeout) {
                                            // Timeout hop - break connection chain
                                            prev_ip[0] = '\0';
                                            continue;
                                        }

                                        // Determine node type
                                        const char *node_type;
                                        if (strcmp(hops[h].ip_address, ip_str) == 0) {
                                            node_type = "phone"; // Destination
                                        } else {
                                            node_type = "router"; // Intermediate hop (or source)
                                        }

                                        // Add this hop as a node
                                        topology_db_add_node(hops[h].ip_address, node_type,
                                                           hops[h].hostname, NULL, NULL, "ONLINE");

                                        // Add connection from previous hop to this hop
                                        if (prev_ip[0] != '\0') {
                                            topology_db_add_connection(prev_ip, hops[h].ip_address,
                                                                     hops[h].rtt_ms);
                                        }

                                        strncpy(prev_ip, hops[h].ip_address, sizeof(prev_ip));
                                    }

                                    LOG_INFO("Topology updated: %d hops added for %s",
                                           hop_count, user->user_id);
                                } else {
                                    LOG_WARN("Failed to determine source IP for %s", user->user_id);
                                }
                            } else {
                                LOG_WARN("Traceroute to %s failed", user->user_id);
                            }
                        }

                        // Write results to shared memory database
                        uac_test_result_t db_result = {0};
                        strncpy(db_result.phone_number, user->user_id, sizeof(db_result.phone_number) - 1);
                        strncpy(db_result.ping_status, ping_status, sizeof(db_result.ping_status) - 1);
                        db_result.ping_rtt = ping_rtt;
                        db_result.ping_jitter = ping_jitter;
                        strncpy(db_result.options_status, options_status, sizeof(db_result.options_status) - 1);
                        db_result.options_rtt = options_rtt;
                        db_result.options_jitter = options_jitter;
                        uac_test_db_write_result(&db_result);

                        // Write results to file before continuing (keep for backwards compatibility)
                        if (results_file) {
                            fprintf(results_file, "%s|%s|%s|%.2f|%.2f|%s|%.2f|%.2f\n",
                                    user->user_id, user->display_name,
                                    ping_status, ping_rtt, ping_jitter,
                                    options_status, options_rtt, options_jitter);
                            fflush(results_file);
                        }

                        // Re-acquire mutex and continue to next user
                        pthread_mutex_lock(&registered_users_mutex);
                        continue;
                    } else {
                        snprintf(options_status, sizeof(options_status), "OFFLINE");
                        LOG_WARN("✗ Phone %s no response to options", user->user_id);
                    }
                } else {
                    snprintf(options_status, sizeof(options_status), "DISABLED");
                }

                // ====================================================
                // PHASE 3: SIP INVITE Test (Optional - only if enabled)
                // ====================================================
                if (g_uac_call_test_enabled) {
                    LOG_INFO("Ping/OPTIONS failed, trying INVITE test for %s...",
                             user->user_id);

                    // Wait for UAC to return to IDLE state before making call
                    // The UAC only supports one call at a time
                    int wait_count = 0;
                    while (uac_get_state() != UAC_STATE_IDLE && wait_count < 10) {
                        sleep(1);
                        wait_count++;
                    }

                    if (uac_get_state() != UAC_STATE_IDLE) {
                        LOG_WARN("✗ UAC busy (state: %s), forcing reset before testing %s (%s)",
                                 uac_state_to_string(uac_get_state()), user->user_id, user->display_name);
                        uac_reset_state();
                    }

                    // Trigger UAC test call using global server IP
                    if (uac_make_call(user->user_id, g_server_ip) == 0) {
                        tests_triggered++;
                        LOG_INFO("✓ UAC INVITE test triggered for %s (%s)", user->user_id, user->display_name);

                        // Poll UAC state rapidly to minimize ring time
                        // Cancel as soon as we detect RINGING state
                        int poll_count = 0;
                        int max_polls = 20; // 20 * 50ms = 1 second max (phones respond in <100ms)
                        uac_call_state_t state = UAC_STATE_IDLE;

                        while (poll_count < max_polls) {
                            usleep(50000); // Sleep 50ms between polls
                            poll_count++;
                            state = uac_get_state();

                            if (state == UAC_STATE_RINGING || state == UAC_STATE_ESTABLISHED) {
                                // Phone responded - cancel/hangup immediately
                                break;
                            } else if (state == UAC_STATE_IDLE) {
                                // Error response (like 488) - already reset
                                break;
                            }
                        }

                        // Handle final state
                        if (state == UAC_STATE_CALLING) {
                            // Phone never responded - offline
                            LOG_WARN("✗ Phone %s OFFLINE (no INVITE response)", user->user_id);
                            phones_offline++;
                        } else if (state == UAC_STATE_RINGING) {
                            LOG_INFO("✓ Phone %s ONLINE (ringing) - canceling", user->user_id);
                            phones_online++;
                            uac_cancel_call();
                            sleep(1); // Wait for CANCEL to be sent
                        } else if (state == UAC_STATE_ESTABLISHED) {
                            LOG_INFO("✓ Phone %s ONLINE (answered) - hanging up", user->user_id);
                            phones_online++;
                            uac_hang_up();
                            sleep(1); // Wait for BYE to be sent
                        } else if (state == UAC_STATE_IDLE) {
                            // Got error response (like 488) - phone is online but rejected
                            LOG_INFO("✓ Phone %s ONLINE (rejected call)", user->user_id);
                            phones_online++;
                        }

                        // Force reset to IDLE after test (for error responses like 488)
                        if (uac_get_state() != UAC_STATE_IDLE) {
                            LOG_DEBUG("Force resetting UAC to IDLE after test (state: %s)",
                                      uac_state_to_string(uac_get_state()));
                            uac_reset_state();
                        }
                    } else {
                        LOG_WARN("✗ Failed to trigger UAC INVITE test for %s (%s)", user->user_id, user->display_name);
                        uac_reset_state(); // Reset even on failure
                    }
                } else {
                    // INVITE testing disabled and OPTIONS failed - phone is offline
                    LOG_WARN("✗ Phone %s OFFLINE (no OPTIONS response, INVITE test disabled)", user->user_id);
                    phones_offline++;
                }

                // Write results to shared memory database for offline phones
                uac_test_result_t db_result = {0};
                strncpy(db_result.phone_number, user->user_id, sizeof(db_result.phone_number) - 1);
                strncpy(db_result.ping_status, ping_status, sizeof(db_result.ping_status) - 1);
                db_result.ping_rtt = ping_rtt;
                db_result.ping_jitter = ping_jitter;
                strncpy(db_result.options_status, options_status, sizeof(db_result.options_status) - 1);
                db_result.options_rtt = options_rtt;
                db_result.options_jitter = options_jitter;
                uac_test_db_write_result(&db_result);

                // Write results to file for offline phones (keep for backwards compatibility)
                if (results_file) {
                    fprintf(results_file, "%s|%s|%s|%.2f|%.2f|%s|%.2f|%.2f\n",
                            user->user_id, user->display_name,
                            ping_status, ping_rtt, ping_jitter,
                            options_status, options_rtt, options_jitter);
                    fflush(results_file);
                }

                // Re-acquire mutex for next iteration
                pthread_mutex_lock(&registered_users_mutex);

            } else {
                // DNS failed - node not reachable (don't log to reduce noise)
                dns_failed++;
            }
        }

        pthread_mutex_unlock(&registered_users_mutex);

        // Close results file
        if (results_file) {
            fclose(results_file);
            LOG_INFO("Results written to /tmp/uac_bulk_results.txt");
        }

        LOG_INFO("=== UAC bulk test cycle complete ===");
        LOG_INFO("Total users: %d | DNS resolved: %d | DNS failed: %d | Tests triggered: %d",
                 total_users, dns_resolved, dns_failed, tests_triggered);
        LOG_INFO("Phones ONLINE: %d | Phones OFFLINE: %d (DNS ok, no SIP response)",
                 phones_online, phones_offline);

        if (rtt_count > 0) {
            float overall_avg_rtt = total_avg_rtt / rtt_count;
            LOG_INFO("Network Performance: Average RTT across %d phones: %.2f ms",
                     rtt_count, overall_avg_rtt);
        }

        // Update database header with reachable phone count (phones that are online/reachable)
        // User wants to see "X of X phones tested (all reachable telephones only)"
        int total_results = phones_online + phones_offline;
        uac_test_db_update_header(total_results, phones_online, g_uac_test_interval_seconds);
        LOG_DEBUG("Updated database header: %d results, %d reachable phones", total_results, phones_online);

        // ====================================================
        // POST-CYCLE TOPOLOGY PROCESSING
        // ====================================================
        if (g_uac_traceroute_enabled) {
            int node_count = topology_db_get_node_count();
            int connection_count = topology_db_get_connection_count();

            if (node_count > 0 || connection_count > 0) {
                LOG_INFO("Processing topology data: %d nodes, %d connections",
                         node_count, connection_count);

                // Fetch location data for all discovered nodes (if enabled)
                if (g_topology_fetch_locations) {
                    LOG_INFO("Fetching location data for %d unique nodes...", node_count);
                    topology_db_fetch_all_locations();
                }

                // Calculate aggregate statistics for all connections
                LOG_INFO("Calculating aggregate statistics for %d connections...", connection_count);
                topology_db_calculate_aggregate_stats();

                // Write topology to JSON file
                LOG_INFO("Writing topology to /tmp/arednmon/network_topology.json...");
                topology_db_write_to_file("/tmp/arednmon/network_topology.json");

                LOG_INFO("Topology mapping complete: %d nodes, %d connections",
                         node_count, connection_count);
            } else {
                LOG_WARN("No topology data collected this cycle (0 nodes, 0 connections)");
            }
        }

        // Save counts for next cycle's UI display during testing
        prev_dns_resolved = dns_resolved;
        prev_phones_online = phones_online;

        // Persist to file for accuracy across restarts
        FILE *dns_save = fopen("/tmp/uac_last_dns_resolved.txt", "w");
        if (dns_save) {
            fprintf(dns_save, "%d\n", dns_resolved);
            fclose(dns_save);
        }

        FILE *online_save = fopen("/tmp/uac_last_phones_online.txt", "w");
        if (online_save) {
            fprintf(online_save, "%d\n", phones_online);
            fclose(online_save);
        }

        // Wait for next cycle
        LOG_INFO("Next UAC bulk test in %d seconds...", g_uac_test_interval_seconds);
        sleep(g_uac_test_interval_seconds);
    }

    LOG_INFO("UAC Bulk Tester thread exiting");
    return NULL;
}
