// topology_db.c
// Network Topology Database Implementation (Hostname-based)

#define MODULE_NAME "TOPOLOGY_DB"
#define _GNU_SOURCE

#include "topology_db.h"
#include "http_client.h"
#include "../common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Global topology database
static TopologyNode g_nodes[MAX_TOPOLOGY_NODES];
static int g_node_count = 0;
static TopologyConnection g_connections[MAX_TOPOLOGY_CONNECTIONS];
static int g_connection_count = 0;
static pthread_mutex_t g_topology_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;

// BFS crawl persistent log file
static FILE *g_crawl_log = NULL;

// Forward declarations
static bool should_crawl_node(const char *hostname);

/**
 * Calculate angle from phone number (last digit * 36 degrees)
 */
static int get_phone_angle(const char *phone_name) {
    if (!phone_name || *phone_name == '\0') {
        return 0;
    }

    int last_digit = 0;
    for (const char *p = phone_name; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            last_digit = *p - '0';
        }
    }

    return last_digit * 36;
}

/**
 * Offset coordinates by distance/angle
 */
static void offset_coordinates(double lat, double lon, double distance_meters, int angle_degrees,
                                double *new_lat, double *new_lon) {
    const double R = 6378137.0;
    double angle_rad = angle_degrees * M_PI / 180.0;
    double dx = distance_meters * sin(angle_rad);
    double dy = distance_meters * cos(angle_rad);

    *new_lat = lat + (dy / R) * (180.0 / M_PI);
    *new_lon = lon + (dx / R) * (180.0 / M_PI) / cos(lat * M_PI / 180.0);
}

void topology_db_init(void) {
    if (g_initialized) {
        return;
    }

    pthread_mutex_lock(&g_topology_mutex);

    memset(g_nodes, 0, sizeof(g_nodes));
    g_node_count = 0;
    memset(g_connections, 0, sizeof(g_connections));
    g_connection_count = 0;
    g_initialized = true;

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_INFO("Topology database initialized (capacity: %d nodes, %d connections)",
             MAX_TOPOLOGY_NODES, MAX_TOPOLOGY_CONNECTIONS);
}

void topology_db_cleanup_stale_nodes(void) {
    extern int g_topology_node_inactive_timeout_seconds;
    extern int g_topology_node_delete_timeout_seconds;

    pthread_mutex_lock(&g_topology_mutex);

    time_t now = time(NULL);
    int removed_nodes = 0;
    int inactive_nodes = 0;
    int removed_connections = 0;
    int write_idx = 0;
    for (int read_idx = 0; read_idx < g_node_count; read_idx++) {
        time_t age = now - g_nodes[read_idx].last_seen;

        if (age > g_topology_node_delete_timeout_seconds) {
            // Too old - delete completely
            removed_nodes++;
        } else {
            // Keep node in database
            if (write_idx != read_idx) {
                memcpy(&g_nodes[write_idx], &g_nodes[read_idx], sizeof(TopologyNode));
            }

            // Check if node should be marked INACTIVE
            if (age > g_topology_node_inactive_timeout_seconds) {
                if (strcmp(g_nodes[write_idx].status, "INACTIVE") != 0) {
                    strncpy(g_nodes[write_idx].status, "INACTIVE", sizeof(g_nodes[write_idx].status) - 1);
                    g_nodes[write_idx].status[sizeof(g_nodes[write_idx].status) - 1] = '\0';
                    inactive_nodes++;
                }
            }

            write_idx++;
        }
    }
    g_node_count = write_idx;

    write_idx = 0;
    for (int read_idx = 0; read_idx < g_connection_count; read_idx++) {
        bool from_exists = false;
        bool to_exists = false;

        for (int i = 0; i < g_node_count; i++) {
            if (strcmp(g_nodes[i].name, g_connections[read_idx].from_name) == 0) {
                from_exists = true;
            }
            if (strcmp(g_nodes[i].name, g_connections[read_idx].to_name) == 0) {
                to_exists = true;
            }
            if (from_exists && to_exists) break;
        }

        if (from_exists && to_exists) {
            if (write_idx != read_idx) {
                memcpy(&g_connections[write_idx], &g_connections[read_idx], sizeof(TopologyConnection));
            }
            write_idx++;
        } else {
            removed_connections++;
        }
    }
    g_connection_count = write_idx;

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_INFO("Cleanup complete: removed %d stale nodes, marked %d nodes INACTIVE, removed %d orphaned connections",
             removed_nodes, inactive_nodes, removed_connections);
}

/**
 * Add or update a node
 */
/**
 * Helper: Normalize hostname to lowercase to avoid duplicate nodes
 */
static void normalize_hostname(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return;
    }

    size_t i;
    for (i = 0; i < output_size - 1 && input[i] != '\0'; i++) {
        output[i] = tolower(input[i]);
    }
    output[i] = '\0';

    LOG_DEBUG("NORMALIZE: '%s' -> '%s'", input, output);
}

int topology_db_add_node(const char *name, const char *type,
                         const char *lat, const char *lon, const char *status) {
    if (!name || !type || !status) {
        LOG_ERROR("Invalid parameters for topology_db_add_node");
        return -1;
    }

    // Normalize hostname to lowercase
    LOG_DEBUG("ADD_NODE: Before normalize: '%s' (type=%s)", name, type);
    char normalized_name[128];
    normalize_hostname(name, normalized_name, sizeof(normalized_name));
    LOG_DEBUG("ADD_NODE: After normalize: '%s'", normalized_name);

    pthread_mutex_lock(&g_topology_mutex);

    // Check if node exists
    TopologyNode *existing = NULL;
    for (int i = 0; i < g_node_count; i++) {
        if (strcmp(g_nodes[i].name, normalized_name) == 0) {
            existing = &g_nodes[i];
            break;
        }
    }

    if (existing) {
        // Node already exists - don't add duplicate, just update last_seen timestamp
        existing->last_seen = time(NULL);
        pthread_mutex_unlock(&g_topology_mutex);
        return 1;  // Node already existed, skipped
    }

    // Add new node
    if (g_node_count >= MAX_TOPOLOGY_NODES) {
        pthread_mutex_unlock(&g_topology_mutex);
        LOG_WARN("Topology database full: cannot add %s", normalized_name);
        return -1;
    }

    TopologyNode *node = &g_nodes[g_node_count];
    strncpy(node->name, normalized_name, sizeof(node->name) - 1);
    strncpy(node->type, type, sizeof(node->type) - 1);
    if (lat) strncpy(node->lat, lat, sizeof(node->lat) - 1);
    else node->lat[0] = '\0';
    if (lon) strncpy(node->lon, lon, sizeof(node->lon) - 1);
    else node->lon[0] = '\0';
    strncpy(node->status, status, sizeof(node->status) - 1);
    node->last_seen = time(NULL);

    g_node_count++;

    pthread_mutex_unlock(&g_topology_mutex);
    return 0;
}

TopologyNode* topology_db_find_node(const char *name) {
    if (!name) return NULL;

    char normalized_name[128];
    normalize_hostname(name, normalized_name, sizeof(normalized_name));

    pthread_mutex_lock(&g_topology_mutex);

    for (int i = 0; i < g_node_count; i++) {
        if (strcmp(g_nodes[i].name, normalized_name) == 0) {
            pthread_mutex_unlock(&g_topology_mutex);
            return &g_nodes[i];
        }
    }

    pthread_mutex_unlock(&g_topology_mutex);
    return NULL;
}

int topology_db_get_node_count(void) {
    pthread_mutex_lock(&g_topology_mutex);
    int count = g_node_count;
    pthread_mutex_unlock(&g_topology_mutex);
    return count;
}

int topology_db_add_connection(const char *from_name, const char *to_name, float rtt_ms) {
    if (!from_name || !to_name || rtt_ms < 0) {
        LOG_ERROR("Invalid parameters for topology_db_add_connection");
        return -1;
    }

    LOG_DEBUG("ADD_CONNECTION: Before normalize: '%s' -> '%s' (rtt=%.2fms)", from_name, to_name, rtt_ms);
    char normalized_from[128], normalized_to[128];
    normalize_hostname(from_name, normalized_from, sizeof(normalized_from));
    normalize_hostname(to_name, normalized_to, sizeof(normalized_to));
    LOG_DEBUG("ADD_CONNECTION: After normalize: '%s' -> '%s'", normalized_from, normalized_to);

    pthread_mutex_lock(&g_topology_mutex);

    TopologyConnection *existing = NULL;
    for (int i = 0; i < g_connection_count; i++) {
        if (strcmp(g_connections[i].from_name, normalized_from) == 0 &&
            strcmp(g_connections[i].to_name, normalized_to) == 0) {
            existing = &g_connections[i];
            break;
        }
    }

    if (existing) {
        int idx = existing->next_sample_index;
        existing->samples[idx].rtt_ms = rtt_ms;
        existing->samples[idx].timestamp = time(NULL);
        existing->next_sample_index = (idx + 1) % MAX_RTT_SAMPLES;

        if (existing->sample_count < MAX_RTT_SAMPLES) {
            existing->sample_count++;
        }

        existing->last_updated = time(NULL);

        pthread_mutex_unlock(&g_topology_mutex);
        return 0;
    }

    if (g_connection_count >= MAX_TOPOLOGY_CONNECTIONS) {
        pthread_mutex_unlock(&g_topology_mutex);
        LOG_WARN("Topology database full (connections): cannot add %s -> %s",
                normalized_from, normalized_to);
        return -1;
    }

    TopologyConnection *conn = &g_connections[g_connection_count];
    strncpy(conn->from_name, normalized_from, sizeof(conn->from_name) - 1);
    strncpy(conn->to_name, normalized_to, sizeof(conn->to_name) - 1);
    memset(conn->samples, 0, sizeof(conn->samples));
    conn->samples[0].rtt_ms = rtt_ms;
    conn->samples[0].timestamp = time(NULL);
    conn->sample_count = 1;
    conn->next_sample_index = 1;
    conn->rtt_avg_ms = rtt_ms;
    conn->rtt_min_ms = rtt_ms;
    conn->rtt_max_ms = rtt_ms;
    conn->last_updated = time(NULL);

    g_connection_count++;

    pthread_mutex_unlock(&g_topology_mutex);
    return 0;
}

/**
 * Find a connection
 */
TopologyConnection* topology_db_find_connection(const char *from_name, const char *to_name) {
    if (!from_name || !to_name) return NULL;

    // Normalize hostnames to lowercase
    char normalized_from[128], normalized_to[128];
    normalize_hostname(from_name, normalized_from, sizeof(normalized_from));
    normalize_hostname(to_name, normalized_to, sizeof(normalized_to));

    pthread_mutex_lock(&g_topology_mutex);

    for (int i = 0; i < g_connection_count; i++) {
        if (strcmp(g_connections[i].from_name, normalized_from) == 0 &&
            strcmp(g_connections[i].to_name, normalized_to) == 0) {
            pthread_mutex_unlock(&g_topology_mutex);
            return &g_connections[i];
        }
    }

    pthread_mutex_unlock(&g_topology_mutex);
    return NULL;
}

/**
 * Get connection count
 */
int topology_db_get_connection_count(void) {
    pthread_mutex_lock(&g_topology_mutex);
    int count = g_connection_count;
    pthread_mutex_unlock(&g_topology_mutex);
    return count;
}

/**
 * Fetch location data for all nodes
 */
void topology_db_fetch_all_locations(void) {
    LOG_INFO("Fetching location data for %d nodes...", g_node_count);

    int fetched = 0;
    int failed = 0;
    int propagated = 0;

    pthread_mutex_lock(&g_topology_mutex);

    // Phase 1: Fetch locations for routers and servers
    for (int i = 0; i < g_node_count; i++) {
        TopologyNode *node = &g_nodes[i];

        if (strcmp(node->type, "phone") == 0) continue;
        if (strlen(node->lat) > 0 && strlen(node->lon) > 0) continue;

        char url[512];
        snprintf(url, sizeof(url), "http://%s.local.mesh/cgi-bin/sysinfo.json", node->name);

        char lat[32] = "";
        char lon[32] = "";

        if (http_get_location(url, lat, sizeof(lat), lon, sizeof(lon)) == 0) {
            strncpy(node->lat, lat, sizeof(node->lat) - 1);
            strncpy(node->lon, lon, sizeof(node->lon) - 1);
            fetched++;
        } else {
            failed++;
        }
    }

    // Phase 2: Propagate router locations to phones
    for (int i = 0; i < g_node_count; i++) {
        TopologyNode *phone = &g_nodes[i];

        if (strcmp(phone->type, "phone") != 0) continue;
        if (strlen(phone->lat) > 0 && strlen(phone->lon) > 0) continue;

        // Find connection to router
        for (int c = 0; c < g_connection_count; c++) {
            TopologyConnection *conn = &g_connections[c];

            if (strcmp(conn->to_name, phone->name) == 0) {
                // Find source router (prefixes already stripped)
                for (int j = 0; j < g_node_count; j++) {
                    TopologyNode *router = &g_nodes[j];

                    // Compare router name (names are already normalized to lowercase)
                    if (strcmp(router->name, conn->from_name) == 0) {
                        if (strlen(router->lat) > 0 && strlen(router->lon) > 0) {
                            // Position phone 100m from router at deterministic angle
                            double router_lat = atof(router->lat);
                            double router_lon = atof(router->lon);

                            // Calculate deterministic angle based on phone name
                            int angle = get_phone_angle(phone->name);

                            LOG_INFO("Positioning phone %s at angle %d° from router %s",
                                   phone->name, angle, router->name);

                            // Offset 100 meters from router
                            double phone_lat, phone_lon;
                            offset_coordinates(router_lat, router_lon, 100.0, angle,
                                             &phone_lat, &phone_lon);

                            snprintf(phone->lat, sizeof(phone->lat), "%.7f", phone_lat);
                            snprintf(phone->lon, sizeof(phone->lon), "%.7f", phone_lon);

                            LOG_DEBUG("Phone %s positioned at (%.7f, %.7f)",
                                     phone->name, phone_lat, phone_lon);

                            propagated++;
                            goto next_phone;
                        }
                    }
                }
            }
        }

        next_phone:
        continue;
    }

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_INFO("Location fetch complete: %d routers fetched, %d failed, %d phones propagated",
             fetched, failed, propagated);
}

/**
 * Helper: Strip hostname prefix (mid1., mid2., dtdlink., etc.)
 * Since hostnames are unique, we always strip prefixes to avoid duplicates.
 */
static const char* strip_hostname_prefix_internal(const char *hostname, char *buffer, size_t buffer_size) {
    if (!hostname || !buffer || buffer_size == 0) {
        return hostname;
    }

    // Check if hostname contains a dot
    const char *dot = strchr(hostname, '.');
    if (!dot) {
        // No dot, return as-is
        strncpy(buffer, hostname, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return buffer;
    }

    // Check if prefix looks like a typical interface prefix (lowercase, short)
    const char *prefix = hostname;
    int prefix_len = dot - prefix;

    if (prefix_len > 0 && prefix_len < 10) {
        bool is_prefix = true;
        for (int i = 0; i < prefix_len; i++) {
            if (!islower(prefix[i]) && !isdigit(prefix[i])) {
                is_prefix = false;
                break;
            }
        }

        if (is_prefix) {
            // Strip prefix, use base hostname
            strncpy(buffer, dot + 1, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            return buffer;
        }
    }

    // Not a prefix pattern, return as-is
    strncpy(buffer, hostname, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return buffer;
}

/**
 * Calculate aggregate statistics
 */
void topology_db_calculate_aggregate_stats(void) {
    LOG_INFO("Calculating aggregate statistics for %d connections...", g_connection_count);

    pthread_mutex_lock(&g_topology_mutex);

    for (int i = 0; i < g_connection_count; i++) {
        TopologyConnection *conn = &g_connections[i];

        if (conn->sample_count == 0) {
            conn->rtt_avg_ms = 0.0;
            conn->rtt_min_ms = 0.0;
            conn->rtt_max_ms = 0.0;
            continue;
        }

        float sum = 0.0;
        float min = conn->samples[0].rtt_ms;
        float max = conn->samples[0].rtt_ms;

        for (int s = 0; s < conn->sample_count; s++) {
            float rtt = conn->samples[s].rtt_ms;
            sum += rtt;
            if (rtt < min) min = rtt;
            if (rtt > max) max = rtt;
        }

        conn->rtt_avg_ms = sum / conn->sample_count;
        conn->rtt_min_ms = min;
        conn->rtt_max_ms = max;
    }

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_INFO("Statistics calculation complete");
}

/**
 * Helper: Fetch hostname from IP
 */
static int fetch_hostname_from_ip(const char *ip, char *hostname, size_t hostname_len) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --connect-timeout 2 --max-time 5 'http://%s/cgi-bin/sysinfo.json' 2>/dev/null",
             ip);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[1024];
    hostname[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"node\":")) {
            char *val_start = strchr(line, ':');
            if (val_start) {
                val_start = strchr(val_start, '"');
                if (val_start) {
                    val_start++;
                    char *val_end = strchr(val_start, '"');
                    if (val_end) {
                        int len = val_end - val_start;
                        if (len > 0 && len < (int)hostname_len) {
                            strncpy(hostname, val_start, len);
                            hostname[len] = '\0';
                            // Normalize to lowercase
                            for (char *p = hostname; *p; p++) {
                                *p = tolower(*p);
                            }
                        }
                    }
                }
            }
        }
    }

    pclose(fp);

    if (hostname[0] != '\0') {
        return 0;
    }

    return -1;
}

/**
 * Helper: Fetch node details from hostname
 */
static int fetch_node_details(const char *hostname, char *lat, size_t lat_len,
                              char *lon, size_t lon_len) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --connect-timeout 2 --max-time 5 'http://%s.local.mesh/cgi-bin/sysinfo.json' 2>/dev/null",
             hostname);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[1024];
    lat[0] = '\0';
    lon[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"lat\":")) {
            char *val_start = strchr(line, ':');
            if (val_start) {
                val_start++;
                while (*val_start == ' ' || *val_start == '"') val_start++;
                char *val_end = val_start;
                while (*val_end && *val_end != ',' && *val_end != '"' && *val_end != '\n') val_end++;
                int len = val_end - val_start;
                if (len > 0 && len < (int)lat_len) {
                    strncpy(lat, val_start, len);
                    lat[len] = '\0';
                }
            }
        }

        if (strstr(line, "\"lon\":")) {
            char *val_start = strchr(line, ':');
            if (val_start) {
                val_start++;
                while (*val_start == ' ' || *val_start == '"') val_start++;
                char *val_end = val_start;
                while (*val_end && *val_end != ',' && *val_end != '"' && *val_end != '\n') val_end++;
                int len = val_end - val_start;
                if (len > 0 && len < (int)lon_len) {
                    strncpy(lon, val_start, len);
                    lon[len] = '\0';
                }
            }
        }
    }

    pclose(fp);

    if (lat[0] != '\0') {
        return 0;
    }

    return -1;
}

/**
 * Helper: Fetch LQM links from a hostname
 */
static int fetch_lqm_links_from_host(const char *hostname, char *neighbors_buf, size_t buf_size) {
    if (neighbors_buf && buf_size > 0) neighbors_buf[0] = '\0';
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --connect-timeout 2 --max-time 5 'http://%s.local.mesh/cgi-bin/sysinfo.json?lqm=1' 2>/dev/null",
             hostname);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }

    char line[4096];
    int link_count = 0;
    bool in_trackers = false;
    bool in_tracker_entry = false;
    int brace_depth = 0;  // Track nesting level for JSON braces
    char neighbor_hostname[256] = "";
    char neighbor_ip[INET_ADDRSTRLEN] = "";
    float ping_time_ms = 0.0;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"trackers\"")) {
            in_trackers = true;
            continue;
        }

        if (!in_trackers) continue;

        if (in_trackers && !in_tracker_entry && strchr(line, '{')) {
            in_tracker_entry = true;
            brace_depth = 1;  // Start tracking depth from opening brace
            neighbor_hostname[0] = '\0';
            neighbor_ip[0] = '\0';
            ping_time_ms = 0.0;
            continue;
        }

        if (in_tracker_entry) {
            if (strstr(line, "\"hostname\":")) {
                char *name_start = strchr(line, ':');
                if (name_start) {
                    name_start++;
                    while (*name_start == ' ' || *name_start == '"') name_start++;
                    char *name_end = strchr(name_start, '"');
                    if (name_end) {
                        int len = name_end - name_start;
                        if (len > 0 && len < (int)sizeof(neighbor_hostname)) {
                            strncpy(neighbor_hostname, name_start, len);
                            neighbor_hostname[len] = '\0';
                            // Normalize to lowercase
                            for (char *p = neighbor_hostname; *p; p++) {
                                *p = tolower(*p);
                            }
                        }
                    }
                }
            }

            if (strstr(line, "\"ip\":")) {
                char *ip_start = strchr(line, ':');
                if (ip_start) {
                    ip_start++;
                    while (*ip_start == ' ' || *ip_start == '"') ip_start++;
                    char *ip_end = strchr(ip_start, '"');
                    if (ip_end) {
                        int len = ip_end - ip_start;
                        if (len > 0 && len < INET_ADDRSTRLEN) {
                            strncpy(neighbor_ip, ip_start, len);
                            neighbor_ip[len] = '\0';
                        }
                    }
                }
            }

            if (strstr(line, "\"ping_success_time\":")) {
                char *time_start = strchr(line, ':');
                if (time_start) {
                    time_start++;
                    ping_time_ms = atof(time_start) * 1000.0;
                }
            }

            // Track brace depth to handle nested structures (e.g., babel_config)
            for (char *p = line; *p; p++) {
                if (*p == '{') brace_depth++;
                if (*p == '}') brace_depth--;
            }

            // Only exit tracker entry when we return to depth 0
            if (brace_depth == 0) {
                // Include both reachable (ping_time_ms > 0) and unreachable (ping_time_ms == 0) neighbors
                if (neighbor_hostname[0] != '\0') {
                    // Strip prefix from neighbor hostname
                    char clean_neighbor[256];
                    strip_hostname_prefix_internal(neighbor_hostname, clean_neighbor, sizeof(clean_neighbor));

                    // Filter: Only add HB* nodes and phones to topology
                    if (should_crawl_node(clean_neighbor)) {
                        // Use 0.0 RTT for unreachable neighbors to distinguish them
                        float rtt = ping_time_ms > 0.0 ? ping_time_ms : 0.0;
                        topology_db_add_connection(hostname, clean_neighbor, rtt);
                        if (neighbors_buf && buf_size > 0) {
                            size_t len = strlen(neighbors_buf);
                            snprintf(neighbors_buf + len, buf_size - len, "%s%s",
                                    len > 0 ? "," : "", clean_neighbor);
                        }
                        link_count++;
                    }
                } else if (neighbor_ip[0] != '\0') {
                    // Fallback: resolve IP to hostname
                    char resolved_hostname[256];
                    if (fetch_hostname_from_ip(neighbor_ip, resolved_hostname, sizeof(resolved_hostname)) == 0) {
                        // Strip prefix from resolved hostname
                        char clean_resolved[256];
                        strip_hostname_prefix_internal(resolved_hostname, clean_resolved, sizeof(clean_resolved));

                        // Filter: Only add HB* nodes and phones to topology
                        if (should_crawl_node(clean_resolved)) {
                            float rtt = ping_time_ms > 0.0 ? ping_time_ms : 0.0;
                            topology_db_add_connection(hostname, clean_resolved, rtt);
                            if (neighbors_buf && buf_size > 0) {
                                size_t len = strlen(neighbors_buf);
                                snprintf(neighbors_buf + len, buf_size - len, "%s%s",
                                        len > 0 ? "," : "", clean_resolved);
                            }
                            link_count++;
                        }
                    }
                }
                in_tracker_entry = false;
            }
        }
    }

    pclose(fp);
    return link_count;
}

/**
 * Helper: Fetch phones for a specific router from OLSR services
 * Positions phones 100m from their router at deterministic angles
 */
static int fetch_phones_for_router(const char *router_hostname, const char *router_lat, const char *router_lon) {
    int phone_count = 0;

    // Resolve router hostname to mesh IP for advertiser matching
    struct hostent *he = gethostbyname2((router_hostname + strspn(router_hostname, " \t")), AF_INET);
    if (!he) {
        return 0;
    }

    struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;
    if (addr_list[0] == NULL) {
        return 0;
    }

    char router_mesh_ip[32];
    snprintf(router_mesh_ip, sizeof(router_mesh_ip), "%s", inet_ntoa(*addr_list[0]));

    FILE *hosts_fp = fopen("/var/run/hosts_olsr", "r");
    if (!hosts_fp) {
        return 0;
    }

    char line[4096];

    while (fgets(line, sizeof(line), hosts_fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') continue;

        // Format: "IP\tHOSTNAME\t# ADVERTISER_IP" or "IP\tHOSTNAME\t# myself"
        char phone_ip[32], phone_name[64], advertiser[64];
        int parsed = sscanf(line, "%31s %63s # %63[^\n]", phone_ip, phone_name, advertiser);

        if (parsed != 3) continue;

        // Check if this is a phone (numeric hostname)
        bool is_numeric = true;
        bool has_digits = false;
        for (char *p = phone_name; *p; p++) {
            if (isdigit(*p)) {
                has_digits = true;
            } else if (*p != '-') {
                is_numeric = false;
                break;
            }
        }

        if (!is_numeric || !has_digits || strlen(phone_name) < 4) {
            continue;  // Not a phone
        }

        // Check if this phone is advertised by the current router
        bool advertised_by_router = false;
        if (strcmp(advertiser, "myself") == 0) {
            // localhost advertises this, check if router is localhost
            advertised_by_router = (strcmp(router_hostname, "hb9bla-vm-1") == 0);
        } else {
            // Extract IP from advertiser (may have extra text like "(mid #1)")
            char advertiser_ip[32];
            sscanf(advertiser, "%31s", advertiser_ip);
            advertised_by_router = (strcmp(advertiser_ip, router_mesh_ip) == 0);
        }

        if (advertised_by_router) {
            char phone_lat_str[32] = "";
            char phone_lon_str[32] = "";

            if (strlen(router_lat) > 0 && strlen(router_lon) > 0) {
                int angle = get_phone_angle(phone_name);
                double r_lat = atof(router_lat);
                double r_lon = atof(router_lon);
                double phone_lat, phone_lon;

                offset_coordinates(r_lat, r_lon, 100.0, angle, &phone_lat, &phone_lon);

                snprintf(phone_lat_str, sizeof(phone_lat_str), "%.7f", phone_lat);
                snprintf(phone_lon_str, sizeof(phone_lon_str), "%.7f", phone_lon);
            }

            int add_result = topology_db_add_node(phone_name, "phone",
                                                  strlen(phone_lat_str) > 0 ? phone_lat_str : NULL,
                                                  strlen(phone_lon_str) > 0 ? phone_lon_str : NULL,
                                                  "ONLINE");
            if (add_result == 0) {
                topology_db_add_connection(router_hostname, phone_name, 0.1);
                phone_count++;
            }
        }
    }

    fclose(hosts_fp);

    if (phone_count > 0) {
        LOG_INFO("Added %d phones for router %s from hosts_olsr", phone_count, router_hostname);
    }
    return phone_count;
}

/**
 * Fetch phones for all routers in topology (useful for routers added via traceroute)
 */
int topology_db_fetch_phones_for_all_routers(void) {
    int total_phones_added = 0;

    pthread_mutex_lock(&g_topology_mutex);

    // Iterate through all nodes and fetch phones for routers that have coordinates
    for (int i = 0; i < g_node_count; i++) {
        TopologyNode *node = &g_nodes[i];

        // Only process router nodes with coordinates
        if (strcmp(node->type, "router") == 0 &&
            strlen(node->lat) > 0 && strlen(node->lon) > 0) {

            pthread_mutex_unlock(&g_topology_mutex);

            // Fetch phones for this router
            int phones_added = fetch_phones_for_router(node->name, node->lat, node->lon);
            total_phones_added += phones_added;

            pthread_mutex_lock(&g_topology_mutex);
        }
    }

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_INFO("Fetched phones for all routers: %d phones added", total_phones_added);
    return total_phones_added;
}

/**
 * Check if hostname should be crawled (Swiss HB* nodes only)
 */
static bool should_crawl_node(const char *hostname) {
    if (!hostname || !hostname[0]) return false;

    // Check if hostname is numeric (phone) - always crawl phones
    bool is_numeric = true;
    for (const char *p = hostname; *p; p++) {
        if (!isdigit(*p)) {
            is_numeric = false;
            break;
        }
    }
    if (is_numeric) {
        return true;  // Phones are always crawled
    }

    // Check if hostname starts with "hb" (Swiss call sign prefix, normalized lowercase)
    if (hostname[0] == 'h' && hostname[1] == 'b') {
        return true;  // Swiss node - crawl it
    }

    return false;  // Non-Swiss node (international)
}

/**
 * Helper: Check if hostname is already visited
 */
static bool is_hostname_visited(const char *hostname, char visited[][64], int visited_count) {
    for (int i = 0; i < visited_count; i++) {
        if (strcmp(visited[i], hostname) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Helper: Add hostname to queue if not already visited or queued
 */
static void add_to_queue_if_new(const char *hostname, char queue[][64], int *queue_count,
                                 char visited[][64], int visited_count, int max_queue) {
    // Skip if already visited
    if (is_hostname_visited(hostname, visited, visited_count)) {
        return;
    }

    // Skip if already in queue
    for (int i = 0; i < *queue_count; i++) {
        if (strcmp(queue[i], hostname) == 0) {
            return;
        }
    }

    // Add to queue if space available
    if (*queue_count < max_queue) {
        strncpy(queue[*queue_count], hostname, 63);
        queue[*queue_count][63] = '\0';
        (*queue_count)++;
    } else {
        LOG_WARN("Crawl queue full, cannot add: %s", hostname);
    }
}

// File-scope counter for nodes without coordinates (stacks them on map left side)
static int g_no_coord_counter = 0;

/**
 * Reset the counter for nodes without coordinates (call at start of each BFS crawl)
 */
static void reset_no_coord_counter(void) {
    g_no_coord_counter = 0;
}

/**
 * Central function to add a router node with all its data:
 * - Router node with coordinates
 * - LQM neighbor connections
 * - Phones from OLSR services
 *
 * This function treats all routers the same way (including localhost)
 */
static int add_router(const char *hostname, char queue[][64], int *queue_count,
                      char visited[][64], int visited_count, int max_queue) {

    // Fetch node details (coordinates)
    char lat[32] = "";
    char lon[32] = "";
    int node_reachable = 0;
    int add_result = -1;

    if (fetch_node_details(hostname, lat, sizeof(lat), lon, sizeof(lon)) == 0) {
        node_reachable = 1;
        add_result = topology_db_add_node(hostname, "router", lat, lon, "ONLINE");
        if (add_result == 0) {
            LOG_INFO("Added new router: %s", hostname);
        }
    } else {
        // Unreachable: don't update topology (preserves last_seen for aging)
        node_reachable = 0;
        add_result = -1;

        // Default coordinates for logging only
        double default_lat = 46.279 + (g_no_coord_counter * 0.045);
        double default_lon = 5.950;
        snprintf(lat, sizeof(lat), "%.6f", default_lat);
        snprintf(lon, sizeof(lon), "%.6f", default_lon);
        g_no_coord_counter++;

        LOG_WARN("Router %s unreachable - NOT updating topology, continuing BFS", hostname);
        if (g_crawl_log) {
            fprintf(g_crawl_log, "  WARNING: Router '%s' unreachable, NOT updating topology, continuing BFS\n",
                    hostname);
            fflush(g_crawl_log);
        }
    }

    // Fetch neighbors even if unreachable (ensures BFS continues)
    char neighbors_buf[2048] = "";
    int links = fetch_lqm_links_from_host(hostname, neighbors_buf, sizeof(neighbors_buf));
    if (links > 0) {
        LOG_INFO("Router %s has %d LQM neighbor connections", hostname, links);
    }

    // Parse neighbors and add to crawl queue
    if (strlen(neighbors_buf) > 0) {
        if (g_crawl_log) {
            fprintf(g_crawl_log, "  NEIGHBORS of '%s': [%s]\n", hostname, neighbors_buf);
            fflush(g_crawl_log);
        }
        char *neighbor = strtok(neighbors_buf, ",");
        while (neighbor != NULL) {
            if (should_crawl_node(neighbor)) {
                int before_count = *queue_count;
                add_to_queue_if_new(neighbor, queue, queue_count, visited, visited_count, max_queue);
                if (*queue_count > before_count) {
                    if (g_crawl_log) {
                        fprintf(g_crawl_log, "    + QUEUED neighbor '%s' (queue size now: %d)\n", neighbor, *queue_count);
                        fflush(g_crawl_log);
                    }
                } else {
                    if (g_crawl_log) {
                        fprintf(g_crawl_log, "    - SKIPPED neighbor '%s' (already queued/visited)\n", neighbor);
                        fflush(g_crawl_log);
                    }
                }
            } else {
                if (g_crawl_log) {
                    fprintf(g_crawl_log, "    - FILTERED neighbor '%s' (non-HB callsign)\n", neighbor);
                    fflush(g_crawl_log);
                }
            }
            neighbor = strtok(NULL, ",");
        }
    } else {
        if (g_crawl_log) {
            fprintf(g_crawl_log, "  NEIGHBORS of '%s': NONE (empty LQM tracker list)\n", hostname);
            fflush(g_crawl_log);
        }
    }

    // ONLY fetch phones if router is reachable (prevents orphaned phones)
    if (node_reachable == 1) {
        fetch_phones_for_router(hostname, lat, lon);
    } else {
        if (g_crawl_log) {
            fprintf(g_crawl_log, "  PHONES: SKIPPED (router unreachable)\n");
            fflush(g_crawl_log);
        }
    }

    return (add_result == 0) ? 1 : 0;  // Return 1 if new router added, 0 if already existed
}

/**
 * Crawl the entire mesh network using BFS recursive discovery
 */
void topology_db_crawl_mesh_network(void) {
    LOG_INFO("Starting BFS mesh network crawl from localhost...");

    // Reset counter for nodes without coordinates (starts fresh each crawl)
    reset_no_coord_counter();

    // Open persistent log file in /tmp
    g_crawl_log = fopen("/tmp/bfs_crawl_log.txt", "w");
    if (g_crawl_log) {
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", gmtime(&now));
        fprintf(g_crawl_log, "========================================\n");
        fprintf(g_crawl_log, "BFS MESH NETWORK CRAWL LOG\n");
        fprintf(g_crawl_log, "Started: %s\n", timestamp);
        fprintf(g_crawl_log, "========================================\n\n");
        fflush(g_crawl_log);
    } else {
        LOG_WARN("Failed to open /tmp/bfs_crawl_log.txt for writing");
    }

    // Allocate BFS queues on heap to avoid stack overflow
    char (*crawl_queue)[64] = malloc(1000 * 64);  // Nodes to crawl
    char (*visited)[64] = malloc(1000 * 64);      // Nodes already crawled
    char (*initial_hostnames)[40] = malloc(500 * 40);

    if (!crawl_queue || !visited || !initial_hostnames) {
        LOG_ERROR("Failed to allocate memory for BFS crawl queues");
        if (g_crawl_log) {
            fprintf(g_crawl_log, "ERROR: Failed to allocate memory for BFS queues\n");
            fclose(g_crawl_log);
            g_crawl_log = NULL;
        }
        free(crawl_queue);
        free(visited);
        free(initial_hostnames);
        return;
    }

    int queue_count = 0;
    int visited_count = 0;

    // Get localhost hostname
    char localhost_hostname[256];
    FILE *hostname_fp = popen("cat /proc/sys/kernel/hostname", "r");
    if (hostname_fp && fgets(localhost_hostname, sizeof(localhost_hostname), hostname_fp)) {
        // Remove newline
        char *nl = strchr(localhost_hostname, '\n');
        if (nl) *nl = '\0';

        // Normalize to lowercase
        for (char *p = localhost_hostname; *p; p++) {
            *p = tolower(*p);
        }

        pclose(hostname_fp);

        // Add localhost using the same central function as all other routers
        LOG_INFO("Adding localhost router: %s", localhost_hostname);
        if (g_crawl_log) {
            fprintf(g_crawl_log, "STARTING NODE: %s (localhost)\n\n", localhost_hostname);
            fflush(g_crawl_log);
        }
        add_router(localhost_hostname, crawl_queue, &queue_count, visited, visited_count, 1000);
    } else {
        LOG_ERROR("Failed to get localhost hostname");
        if (g_crawl_log) {
            fprintf(g_crawl_log, "ERROR: Failed to get localhost hostname\n");
            fclose(g_crawl_log);
            g_crawl_log = NULL;
        }
        if (hostname_fp) pclose(hostname_fp);
        free(crawl_queue);
        free(visited);
        free(initial_hostnames);
        return;
    }

    int processed = 0;
    int discovered = 0;
    int queue_head = 0;  // Index of next node to process

    LOG_INFO("Starting BFS crawl with %d nodes in queue...", queue_count);
    if (g_crawl_log) {
        fprintf(g_crawl_log, "Initial queue size: %d\n\n", queue_count);
        fprintf(g_crawl_log, "========================================\n");
        fprintf(g_crawl_log, "BFS QUEUE PROCESSING\n");
        fprintf(g_crawl_log, "========================================\n\n");
        fflush(g_crawl_log);
    }

    // BFS: process queue until empty
    while (queue_head < queue_count && visited_count < 1000) {
        char *hostname = crawl_queue[queue_head];
        queue_head++;
        processed++;

        if (g_crawl_log) {
            fprintf(g_crawl_log, "[%d/%d] Processing: '%s'\n", queue_head, queue_count, hostname);
            fflush(g_crawl_log);
        }

        // Skip if already visited (shouldn't happen but safety check)
        if (is_hostname_visited(hostname, visited, visited_count)) {
            if (g_crawl_log) {
                fprintf(g_crawl_log, "  WARNING: Already visited (unexpected)\n\n");
                fflush(g_crawl_log);
            }
            continue;
        }

        // Mark as visited
        if (visited_count < 1000) {
            strncpy(visited[visited_count], hostname, 63);
            visited[visited_count][63] = '\0';
            visited_count++;
        }

        // Check if numeric (phone) - skip phones in crawl
        bool is_numeric = true;
        for (char *p = hostname; *p; p++) {
            if (!isdigit(*p)) { is_numeric = false; break; }
        }
        if (is_numeric) {
            if (g_crawl_log) {
                fprintf(g_crawl_log, "  SKIPPED: Phone number (not a router)\n\n");
                fflush(g_crawl_log);
            }
            continue;
        }

        // Add router using the same central function (handles node, neighbors, phones)
        int result = add_router(hostname, crawl_queue, &queue_count, visited, visited_count, 1000);
        if (result > 0) {
            discovered++;
        }

        usleep(100000);  // 100ms delay
    }

    LOG_INFO("BFS mesh crawl complete: processed %d nodes, discovered %d new routers, %d total in queue",
             processed, discovered, queue_count);

    // Write final statistics to log file
    if (g_crawl_log) {
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", gmtime(&now));
        fprintf(g_crawl_log, "\n========================================\n");
        fprintf(g_crawl_log, "BFS CRAWL COMPLETE\n");
        fprintf(g_crawl_log, "========================================\n");
        fprintf(g_crawl_log, "Finished: %s\n", timestamp);
        fprintf(g_crawl_log, "Nodes processed: %d\n", processed);
        fprintf(g_crawl_log, "New routers discovered: %d\n", discovered);
        fprintf(g_crawl_log, "Total nodes in queue: %d\n", queue_count);
        fprintf(g_crawl_log, "Visited count: %d\n", visited_count);
        fprintf(g_crawl_log, "========================================\n");
        fclose(g_crawl_log);
        g_crawl_log = NULL;
        LOG_INFO("BFS crawl log saved to /tmp/bfs_crawl_log.txt");
    }

    // Free allocated memory
    free(crawl_queue);
    free(visited);
    free(initial_hostnames);
}

/**
 * Create directory recursively
 */
static int create_directory(const char *path) {
    char temp_path[512];
    strncpy(temp_path, path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';

    char *dir = dirname(temp_path);

    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0755) == -1) {
            if (errno != EEXIST) {
                LOG_ERROR("Failed to create directory %s: %s", dir, strerror(errno));
                return -1;
            }
        }
    }

    return 0;
}

/**
 * Write topology database to JSON file
 */
int topology_db_write_to_file(const char *filepath) {
    if (!filepath) {
        LOG_ERROR("Invalid filepath for topology JSON");
        return -1;
    }

    LOG_INFO("Writing topology to %s...", filepath);

    if (create_directory(filepath) != 0) {
        return -1;
    }

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        LOG_ERROR("Failed to open %s for writing: %s", filepath, strerror(errno));
        return -1;
    }

    pthread_mutex_lock(&g_topology_mutex);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": \"2.0\",\n");

    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    fprintf(fp, "  \"generated_at\": \"%s\",\n", timestamp);

    fprintf(fp, "  \"source_node\": {\n");
    if (g_node_count > 0) {
        fprintf(fp, "    \"name\": \"%s\",\n", g_nodes[0].name);
        fprintf(fp, "    \"type\": \"server\"\n");
    } else {
        fprintf(fp, "    \"name\": \"unknown\",\n");
        fprintf(fp, "    \"type\": \"server\"\n");
    }
    fprintf(fp, "  },\n");

    // Count nodes by type for debugging
    int router_count = 0, phone_count = 0, other_count = 0;
    for (int i = 0; i < g_node_count; i++) {
        if (strcmp(g_nodes[i].type, "router") == 0) router_count++;
        else if (strcmp(g_nodes[i].type, "phone") == 0) phone_count++;
        else other_count++;
    }
    LOG_INFO("Writing topology: %d total nodes (%d routers, %d phones, %d other)",
             g_node_count, router_count, phone_count, other_count);

    fprintf(fp, "  \"nodes\": [\n");
    for (int i = 0; i < g_node_count; i++) {
        TopologyNode *node = &g_nodes[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"name\": \"%s\",\n", node->name);
        fprintf(fp, "      \"type\": \"%s\",\n", node->type);
        fprintf(fp, "      \"lat\": \"%s\",\n", node->lat);
        fprintf(fp, "      \"lon\": \"%s\",\n", node->lon);
        fprintf(fp, "      \"status\": \"%s\",\n", node->status);
        fprintf(fp, "      \"last_seen\": %ld\n", (long)node->last_seen);
        fprintf(fp, "    }%s\n", (i < g_node_count - 1) ? "," : "");
    }
    fprintf(fp, "  ],\n");

    fprintf(fp, "  \"connections\": [\n");
    bool first_connection = true;
    int skipped_connections = 0;
    for (int i = 0; i < g_connection_count; i++) {
        TopologyConnection *conn = &g_connections[i];

        // Validate that both endpoints exist as nodes
        bool source_exists = false;
        bool target_exists = false;
        for (int j = 0; j < g_node_count; j++) {
            if (strcmp(g_nodes[j].name, conn->from_name) == 0) source_exists = true;
            if (strcmp(g_nodes[j].name, conn->to_name) == 0) target_exists = true;
            if (source_exists && target_exists) break;
        }

        if (!source_exists || !target_exists) {
            skipped_connections++;
            LOG_DEBUG("Skipping connection %s -> %s (source_exists=%d, target_exists=%d)",
                     conn->from_name, conn->to_name, source_exists, target_exists);
            continue;
        }

        if (!first_connection) {
            fprintf(fp, ",\n");
        }
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"source\": \"%s\",\n", conn->from_name);
        fprintf(fp, "      \"target\": \"%s\",\n", conn->to_name);
        fprintf(fp, "      \"rtt_avg_ms\": %.3f,\n", conn->rtt_avg_ms);
        fprintf(fp, "      \"rtt_min_ms\": %.3f,\n", conn->rtt_min_ms);
        fprintf(fp, "      \"rtt_max_ms\": %.3f,\n", conn->rtt_max_ms);
        fprintf(fp, "      \"sample_count\": %d,\n", conn->sample_count);
        fprintf(fp, "      \"last_updated\": %ld\n", (long)conn->last_updated);
        fprintf(fp, "    }");
        first_connection = false;
    }
    fprintf(fp, "\n  ],\n");

    if (skipped_connections > 0) {
        LOG_INFO("Skipped %d orphaned connections (endpoints not in topology)", skipped_connections);
    }

    fprintf(fp, "  \"statistics\": {\n");
    fprintf(fp, "    \"total_nodes\": %d,\n", g_node_count);
    fprintf(fp, "    \"total_connections\": %d\n", g_connection_count);
    fprintf(fp, "  }\n");

    fprintf(fp, "}\n");

    pthread_mutex_unlock(&g_topology_mutex);

    fclose(fp);

    LOG_INFO("Topology written to %s (%d nodes, %d connections)",
             filepath, g_node_count, g_connection_count);
    return 0;
}

/**
 * Public wrapper: Strip hostname prefix (mid1., mid2., dtdlink., etc.)
 */
const char* topology_db_strip_hostname_prefix(const char *hostname, char *buffer, size_t buffer_size) {
    return strip_hostname_prefix_internal(hostname, buffer, buffer_size);
}
