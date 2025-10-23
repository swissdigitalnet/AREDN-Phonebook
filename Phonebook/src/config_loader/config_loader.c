// config_loader.c
#include "config_loader.h" // This includes common.h
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h> // For isspace

#define MODULE_NAME "CONFIG" // Define MODULE_NAME specifically for this C file

// Define global variables (DECLARED extern in config_loader.h and common.h)
// These are initialized with default values, which will be overwritten by the config file if present.
int g_pb_interval_seconds = 3600; // Default: 1 hour
int g_status_update_interval_seconds = 600; // Default: 10 minutes
int g_uac_test_interval_seconds = 60; // Default: 60 seconds
int g_uac_call_test_enabled = 0;
int g_uac_ping_count = 5;      // ICMP ping count (default: 5)
int g_uac_options_count = 5;   // SIP OPTIONS count (default: 5)
ConfigurableServer g_phonebook_servers_list[MAX_PB_SERVERS];
int g_num_phonebook_servers = 0; // Will be populated by the loader

// Health reporting configuration (Chapter 5: Health Reporting)
int g_health_local_reporting = 1;              // Default: enabled
int g_health_local_update_seconds = 60;        // Default: 60 seconds
int g_collector_enabled = 0;                   // Default: disabled
char g_collector_url[256] = "http://pi-collector.local.mesh:5000/ingest"; // Default collector URL
int g_collector_timeout_seconds = 10;          // Default: 10 seconds
int g_health_report_baseline_hours = 4;        // Default: 4 hours
float g_health_cpu_threshold_pct = 20.0f;      // Default: 20% CPU change
float g_health_memory_threshold_mb = 10.0f;    // Default: 10 MB memory increase
float g_health_score_threshold = 15.0f;        // Default: 15 point score drop
int g_crash_reporting_enabled = 1;             // Default: enabled

// Network topology mapping configuration
int g_uac_traceroute_enabled = 1;              // Default: enabled
int g_uac_traceroute_max_hops = 20;            // Default: 20 hops
int g_topology_fetch_locations = 1;            // Default: enabled
int g_topology_crawler_enabled = 1;            // Default: enabled
int g_topology_crawler_interval_seconds = 3600; // Default: 3600 seconds (1 hour)
int g_topology_node_timeout_seconds = 3600;    // Default: 3600 seconds (1 hour) - nodes expire if not seen

// Helper function to trim leading/trailing whitespace (static to this file)
static char* trim_whitespace(char *str) {
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)  // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';

    return str;
}

int load_configuration(const char *config_filepath) {
    FILE *fp = fopen(config_filepath, "r");
    if (!fp) {
        LOG_WARN("Configuration file '%s' not found or cannot be opened: %s. Using default values.",
                 config_filepath, strerror(errno));
        // It's not a critical error if the file doesn't exist; we just use defaults.
        return 0;
    }

    char line[512];
    int current_server_idx = 0;
    LOG_INFO("Loading configuration from %s...", config_filepath);

    // Initialize the phonebook servers list to empty to ensure clean state
    memset(g_phonebook_servers_list, 0, sizeof(ConfigurableServer) * MAX_PB_SERVERS);

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed_line = trim_whitespace(line);

        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            continue; // Skip empty lines and comments
        }

        char *key = trimmed_line;
        char *value = strchr(trimmed_line, '=');
        if (!value) {
            LOG_WARN("Malformed line in config file (missing '='): '%s'. Skipping.", trimmed_line);
            continue;
        }
        *value = '\0'; // Null-terminate key
        value++;       // Move past '=' to start of value
        value = trim_whitespace(value); // Trim whitespace from the value as well

        if (strcmp(key, "PB_INTERVAL_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value > 0) {
                g_pb_interval_seconds = parsed_value;
                LOG_DEBUG("Config: PB_INTERVAL_SECONDS = %d", g_pb_interval_seconds);
            } else {
                LOG_WARN("Invalid PB_INTERVAL_SECONDS value '%s'. Using default %d.", value, g_pb_interval_seconds);
            }
        } else if (strcmp(key, "STATUS_UPDATE_INTERVAL_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value > 0) {
                g_status_update_interval_seconds = parsed_value;
                LOG_DEBUG("Config: STATUS_UPDATE_INTERVAL_SECONDS = %d", g_status_update_interval_seconds);
            } else {
                LOG_WARN("Invalid STATUS_UPDATE_INTERVAL_SECONDS value '%s'. Using default %d.", value, g_status_update_interval_seconds);
            }
        } else if (strcmp(key, "UAC_TEST_INTERVAL_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 0) { // Allow 0 to disable
                g_uac_test_interval_seconds = parsed_value;
                LOG_DEBUG("Config: UAC_TEST_INTERVAL_SECONDS = %d", g_uac_test_interval_seconds);
            } else {
                LOG_WARN("Invalid UAC_TEST_INTERVAL_SECONDS value '%s'. Using default %d.", value, g_uac_test_interval_seconds);
            }
        } else if (strcmp(key, "UAC_CALL_TEST_ENABLED") == 0) {
            int parsed_value = atoi(value);
            g_uac_call_test_enabled = (parsed_value != 0) ? 1 : 0;
            LOG_DEBUG("Config: UAC_CALL_TEST_ENABLED = %d", g_uac_call_test_enabled);
        } else if (strcmp(key, "UAC_PING_COUNT") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 0 && parsed_value <= 20) {
                g_uac_ping_count = parsed_value;
                LOG_DEBUG("Config: UAC_PING_COUNT = %d", g_uac_ping_count);
            } else {
                LOG_WARN("Invalid UAC_PING_COUNT value '%s'. Using default %d.", value, g_uac_ping_count);
            }
        } else if (strcmp(key, "UAC_OPTIONS_COUNT") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 0 && parsed_value <= 20) {
                g_uac_options_count = parsed_value;
                LOG_DEBUG("Config: UAC_OPTIONS_COUNT = %d", g_uac_options_count);
            } else {
                LOG_WARN("Invalid UAC_OPTIONS_COUNT value '%s'. Using default %d.", value, g_uac_options_count);
            }
        } else if (strcmp(key, "PHONEBOOK_SERVER") == 0) {
            if (current_server_idx < MAX_PB_SERVERS) {
                // strtok modifies the string, so it's good if value is a copy or you don't need it later.
                // Here, value is a pointer into 'line', which is fine as we're done with 'line' for this iteration.
                char *host_str = strtok(value, ",");
                char *port_str = strtok(NULL, ",");
                char *path_str = strtok(NULL, ",");

                if (host_str && port_str && path_str) {
                    strncpy(g_phonebook_servers_list[current_server_idx].host, host_str, MAX_SERVER_HOST_LEN - 1);
                    g_phonebook_servers_list[current_server_idx].host[MAX_SERVER_HOST_LEN - 1] = '\0';
                    strncpy(g_phonebook_servers_list[current_server_idx].port, port_str, MAX_SERVER_PORT_LEN - 1);
                    g_phonebook_servers_list[current_server_idx].port[MAX_SERVER_PORT_LEN - 1] = '\0';
                    strncpy(g_phonebook_servers_list[current_server_idx].path, path_str, MAX_SERVER_PATH_LEN - 1);
                    g_phonebook_servers_list[current_server_idx].path[MAX_SERVER_PATH_LEN - 1] = '\0';
                    LOG_DEBUG("Config: Added phonebook server %d: %s:%s%s", current_server_idx + 1,
                              g_phonebook_servers_list[current_server_idx].host,
                              g_phonebook_servers_list[current_server_idx].port,
                              g_phonebook_servers_list[current_server_idx].path);
                    current_server_idx++;
                } else {
                    LOG_WARN("Malformed PHONEBOOK_SERVER line: '%s'. Expected 'host,port,path'. Skipping.", value);
                }
            } else {
                LOG_WARN("Max phonebook servers (%d) reached. Ignoring additional PHONEBOOK_SERVER entries.", MAX_PB_SERVERS);
            }
        } else if (strcmp(key, "HEALTH_LOCAL_REPORTING") == 0) {
            int parsed_value = atoi(value);
            g_health_local_reporting = (parsed_value != 0) ? 1 : 0;
            LOG_DEBUG("Config: HEALTH_LOCAL_REPORTING = %d", g_health_local_reporting);
        } else if (strcmp(key, "HEALTH_LOCAL_UPDATE_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 1 && parsed_value <= 3600) {
                g_health_local_update_seconds = parsed_value;
                LOG_DEBUG("Config: HEALTH_LOCAL_UPDATE_SECONDS = %d", g_health_local_update_seconds);
            } else {
                LOG_WARN("Invalid HEALTH_LOCAL_UPDATE_SECONDS value '%s'. Using default %d.", value, g_health_local_update_seconds);
            }
        } else if (strcmp(key, "COLLECTOR_ENABLED") == 0) {
            int parsed_value = atoi(value);
            g_collector_enabled = (parsed_value != 0) ? 1 : 0;
            LOG_DEBUG("Config: COLLECTOR_ENABLED = %d", g_collector_enabled);
        } else if (strcmp(key, "COLLECTOR_URL") == 0) {
            if (strlen(value) > 0 && strlen(value) < sizeof(g_collector_url)) {
                strncpy(g_collector_url, value, sizeof(g_collector_url) - 1);
                g_collector_url[sizeof(g_collector_url) - 1] = '\0';
                LOG_DEBUG("Config: COLLECTOR_URL = %s", g_collector_url);
            } else {
                LOG_WARN("Invalid COLLECTOR_URL value '%s'. Using default.", value);
            }
        } else if (strcmp(key, "COLLECTOR_TIMEOUT_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 1 && parsed_value <= 60) {
                g_collector_timeout_seconds = parsed_value;
                LOG_DEBUG("Config: COLLECTOR_TIMEOUT_SECONDS = %d", g_collector_timeout_seconds);
            } else {
                LOG_WARN("Invalid COLLECTOR_TIMEOUT_SECONDS value '%s'. Using default %d.", value, g_collector_timeout_seconds);
            }
        } else if (strcmp(key, "HEALTH_REPORT_BASELINE_HOURS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 1 && parsed_value <= 24) {
                g_health_report_baseline_hours = parsed_value;
                LOG_DEBUG("Config: HEALTH_REPORT_BASELINE_HOURS = %d", g_health_report_baseline_hours);
            } else {
                LOG_WARN("Invalid HEALTH_REPORT_BASELINE_HOURS value '%s'. Using default %d.", value, g_health_report_baseline_hours);
            }
        } else if (strcmp(key, "HEALTH_CPU_THRESHOLD_PCT") == 0) {
            float parsed_value = atof(value);
            if (parsed_value >= 1.0f && parsed_value <= 100.0f) {
                g_health_cpu_threshold_pct = parsed_value;
                LOG_DEBUG("Config: HEALTH_CPU_THRESHOLD_PCT = %.1f", g_health_cpu_threshold_pct);
            } else {
                LOG_WARN("Invalid HEALTH_CPU_THRESHOLD_PCT value '%s'. Using default %.1f.", value, g_health_cpu_threshold_pct);
            }
        } else if (strcmp(key, "HEALTH_MEMORY_THRESHOLD_MB") == 0) {
            float parsed_value = atof(value);
            if (parsed_value >= 1.0f && parsed_value <= 100.0f) {
                g_health_memory_threshold_mb = parsed_value;
                LOG_DEBUG("Config: HEALTH_MEMORY_THRESHOLD_MB = %.1f", g_health_memory_threshold_mb);
            } else {
                LOG_WARN("Invalid HEALTH_MEMORY_THRESHOLD_MB value '%s'. Using default %.1f.", value, g_health_memory_threshold_mb);
            }
        } else if (strcmp(key, "HEALTH_SCORE_THRESHOLD") == 0) {
            float parsed_value = atof(value);
            if (parsed_value >= 1.0f && parsed_value <= 100.0f) {
                g_health_score_threshold = parsed_value;
                LOG_DEBUG("Config: HEALTH_SCORE_THRESHOLD = %.1f", g_health_score_threshold);
            } else {
                LOG_WARN("Invalid HEALTH_SCORE_THRESHOLD value '%s'. Using default %.1f.", value, g_health_score_threshold);
            }
        } else if (strcmp(key, "CRASH_REPORTING_ENABLED") == 0) {
            int parsed_value = atoi(value);
            g_crash_reporting_enabled = (parsed_value != 0) ? 1 : 0;
            LOG_DEBUG("Config: CRASH_REPORTING_ENABLED = %d", g_crash_reporting_enabled);
        } else if (strcmp(key, "UAC_TRACEROUTE_ENABLED") == 0) {
            int parsed_value = atoi(value);
            g_uac_traceroute_enabled = (parsed_value != 0) ? 1 : 0;
            LOG_DEBUG("Config: UAC_TRACEROUTE_ENABLED = %d", g_uac_traceroute_enabled);
        } else if (strcmp(key, "UAC_TRACEROUTE_MAX_HOPS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 1 && parsed_value <= 30) {
                g_uac_traceroute_max_hops = parsed_value;
                LOG_DEBUG("Config: UAC_TRACEROUTE_MAX_HOPS = %d", g_uac_traceroute_max_hops);
            } else {
                LOG_WARN("Invalid UAC_TRACEROUTE_MAX_HOPS value '%s'. Using default %d.", value, g_uac_traceroute_max_hops);
            }
        } else if (strcmp(key, "TOPOLOGY_FETCH_LOCATIONS") == 0) {
            int parsed_value = atoi(value);
            g_topology_fetch_locations = (parsed_value != 0) ? 1 : 0;
            LOG_DEBUG("Config: TOPOLOGY_FETCH_LOCATIONS = %d", g_topology_fetch_locations);
        } else if (strcmp(key, "TOPOLOGY_CRAWLER_ENABLED") == 0) {
            int parsed_value = atoi(value);
            g_topology_crawler_enabled = (parsed_value != 0) ? 1 : 0;
            LOG_DEBUG("Config: TOPOLOGY_CRAWLER_ENABLED = %d", g_topology_crawler_enabled);
        } else if (strcmp(key, "TOPOLOGY_CRAWLER_INTERVAL_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 60 && parsed_value <= 86400) { // 1 minute to 24 hours
                g_topology_crawler_interval_seconds = parsed_value;
                LOG_DEBUG("Config: TOPOLOGY_CRAWLER_INTERVAL_SECONDS = %d", g_topology_crawler_interval_seconds);
            } else {
                LOG_WARN("Invalid TOPOLOGY_CRAWLER_INTERVAL_SECONDS value '%s'. Using default %d.", value, g_topology_crawler_interval_seconds);
            }
        } else if (strcmp(key, "TOPOLOGY_NODE_TIMEOUT_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value >= 60 && parsed_value <= 86400) { // 1 minute to 24 hours
                g_topology_node_timeout_seconds = parsed_value;
                LOG_DEBUG("Config: TOPOLOGY_NODE_TIMEOUT_SECONDS = %d", g_topology_node_timeout_seconds);
            } else {
                LOG_WARN("Invalid TOPOLOGY_NODE_TIMEOUT_SECONDS value '%s'. Using default %d.", value, g_topology_node_timeout_seconds);
            }
        } else {
            LOG_WARN("Unknown configuration key: '%s'. Skipping.", key);
        }
    }
    fclose(fp);
    g_num_phonebook_servers = current_server_idx; // Set the actual count of loaded servers
    LOG_INFO("Configuration loaded. Total phonebook servers: %d.", g_num_phonebook_servers);
    return 0; // Success
}
