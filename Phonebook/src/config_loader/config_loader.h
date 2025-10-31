// config_loader.h
#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "common.h" // Include common.h to get definitions like ConfigurableServer, MAX_PB_SERVERS

// These global variables are DECLARED here (extern) and DEFINED in config_loader.c
extern int g_pb_interval_seconds;
extern int g_status_update_interval_seconds;
extern int g_phone_test_interval_seconds;
extern int g_phone_call_test_enabled;
extern int g_phone_ping_count;      // ICMP ping count
extern int g_phone_options_count;   // SIP OPTIONS count
extern ConfigurableServer g_phonebook_servers_list[MAX_PB_SERVERS];
extern int g_num_phonebook_servers;

// Health reporting configuration
extern int g_health_local_reporting;        // Enable local file updates
extern int g_health_local_update_seconds;   // Local update interval
extern int g_collector_enabled;             // Enable remote collector POST
extern char g_collector_url[256];           // Collector URL
extern int g_collector_timeout_seconds;     // HTTP POST timeout
extern int g_health_report_baseline_hours;  // Baseline heartbeat interval
extern float g_health_cpu_threshold_pct;    // CPU spike threshold
extern float g_health_memory_threshold_mb;  // Memory increase threshold
extern float g_health_score_threshold;      // Health score drop threshold
extern int g_crash_reporting_enabled;       // Enable crash detection

// Network topology mapping configuration
extern int g_network_traceroute_enabled;        // Enable traceroute-based topology mapping
extern int g_network_traceroute_max_hops;       // Maximum hops for traceroute
extern int g_topology_fetch_locations;      // Fetch location data from sysinfo.json
extern int g_topology_crawler_enabled;      // Enable mesh network crawler
extern int g_topology_crawler_interval_seconds; // Crawler interval in seconds
extern int g_topology_node_inactive_timeout_seconds; // Mark node INACTIVE after this many seconds unseen (default: 3600 = 1 hour)
extern int g_topology_node_delete_timeout_seconds;   // Delete node completely after this many seconds unseen (default: 2592000 = 30 days)

/**
 * @brief Loads configuration parameters from a specified file.
 *
 * This function reads key-value pairs from the configuration file.
 * It parses PB_INTERVAL_SECONDS, STATUS_UPDATE_INTERVAL_SECONDS,
 * and multiple PHONEBOOK_SERVER entries.
 * Default values are used if the file is not found or if specific
 * parameters are missing/malformed.
 *
 * @param config_filepath The path to the configuration file (e.g., "/etc/phonebook.conf").
 * @return 0 on successful loading (even if some defaults are used), 1 if a critical
 * error prevents any configuration from being loaded (e.g., memory allocation error).
 */
int load_configuration(const char *config_filepath);

#endif // CONFIG_LOADER_H
