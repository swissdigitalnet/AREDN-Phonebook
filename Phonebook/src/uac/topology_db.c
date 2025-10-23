// topology_db.c
// Network Topology Database Implementation (Hostname-based)

#define MODULE_NAME "TOPOLOGY_DB"

#include "topology_db.h"
#include "uac_http_client.h"
#include "../common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>

// Global topology database
static TopologyNode g_nodes[MAX_TOPOLOGY_NODES];
static int g_node_count = 0;
static TopologyConnection g_connections[MAX_TOPOLOGY_CONNECTIONS];
static int g_connection_count = 0;
static pthread_mutex_t g_topology_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_initialized = false;

/**
 * Initialize topology database
 */
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

/**
 * Clean up stale nodes and connections
 */
void topology_db_cleanup_stale_nodes(void) {
    extern int g_topology_node_timeout_seconds;

    pthread_mutex_lock(&g_topology_mutex);

    time_t now = time(NULL);
    int removed_nodes = 0;
    int removed_connections = 0;

    // Remove stale nodes
    int write_idx = 0;
    for (int read_idx = 0; read_idx < g_node_count; read_idx++) {
        time_t age = now - g_nodes[read_idx].last_seen;

        if (age <= g_topology_node_timeout_seconds) {
            if (write_idx != read_idx) {
                memcpy(&g_nodes[write_idx], &g_nodes[read_idx], sizeof(TopologyNode));
            }
            write_idx++;
        } else {
            removed_nodes++;
            LOG_DEBUG("Removing stale node: %s, last seen %ld seconds ago",
                     g_nodes[read_idx].name, age);
        }
    }
    g_node_count = write_idx;

    // Remove orphaned connections
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

    LOG_INFO("Cleanup complete: removed %d stale nodes, %d orphaned connections",
             removed_nodes, removed_connections);
}

/**
 * Add or update a node
 */
int topology_db_add_node(const char *name, const char *type,
                         const char *lat, const char *lon, const char *status) {
    if (!name || !type || !status) {
        LOG_ERROR("Invalid parameters for topology_db_add_node");
        return -1;
    }

    pthread_mutex_lock(&g_topology_mutex);

    // Check if node exists
    TopologyNode *existing = NULL;
    for (int i = 0; i < g_node_count; i++) {
        if (strcmp(g_nodes[i].name, name) == 0) {
            existing = &g_nodes[i];
            break;
        }
    }

    if (existing) {
        // Update existing
        strncpy(existing->type, type, sizeof(existing->type) - 1);
        if (lat) strncpy(existing->lat, lat, sizeof(existing->lat) - 1);
        if (lon) strncpy(existing->lon, lon, sizeof(existing->lon) - 1);
        strncpy(existing->status, status, sizeof(existing->status) - 1);
        existing->last_seen = time(NULL);

        pthread_mutex_unlock(&g_topology_mutex);
        LOG_DEBUG("Updated node: %s", name);
        return 0;
    }

    // Add new node
    if (g_node_count >= MAX_TOPOLOGY_NODES) {
        pthread_mutex_unlock(&g_topology_mutex);
        LOG_WARN("Topology database full: cannot add %s", name);
        return -1;
    }

    TopologyNode *node = &g_nodes[g_node_count];
    strncpy(node->name, name, sizeof(node->name) - 1);
    strncpy(node->type, type, sizeof(node->type) - 1);
    if (lat) strncpy(node->lat, lat, sizeof(node->lat) - 1);
    else node->lat[0] = '\0';
    if (lon) strncpy(node->lon, lon, sizeof(node->lon) - 1);
    else node->lon[0] = '\0';
    strncpy(node->status, status, sizeof(node->status) - 1);
    node->last_seen = time(NULL);

    g_node_count++;

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_DEBUG("Added node: %s (type=%s, status=%s)", name, type, status);
    return 0;
}

/**
 * Find a node by hostname
 */
TopologyNode* topology_db_find_node(const char *name) {
    if (!name) return NULL;

    pthread_mutex_lock(&g_topology_mutex);

    for (int i = 0; i < g_node_count; i++) {
        if (strcmp(g_nodes[i].name, name) == 0) {
            pthread_mutex_unlock(&g_topology_mutex);
            return &g_nodes[i];
        }
    }

    pthread_mutex_unlock(&g_topology_mutex);
    return NULL;
}

/**
 * Get node count
 */
int topology_db_get_node_count(void) {
    pthread_mutex_lock(&g_topology_mutex);
    int count = g_node_count;
    pthread_mutex_unlock(&g_topology_mutex);
    return count;
}

/**
 * Add RTT sample to connection
 */
int topology_db_add_connection(const char *from_name, const char *to_name, float rtt_ms) {
    if (!from_name || !to_name || rtt_ms < 0) {
        LOG_ERROR("Invalid parameters for topology_db_add_connection");
        return -1;
    }

    pthread_mutex_lock(&g_topology_mutex);

    // Check if connection exists
    TopologyConnection *existing = NULL;
    for (int i = 0; i < g_connection_count; i++) {
        if (strcmp(g_connections[i].from_name, from_name) == 0 &&
            strcmp(g_connections[i].to_name, to_name) == 0) {
            existing = &g_connections[i];
            break;
        }
    }

    if (existing) {
        // Add sample to existing connection
        int idx = existing->next_sample_index;
        existing->samples[idx].rtt_ms = rtt_ms;
        existing->samples[idx].timestamp = time(NULL);
        existing->next_sample_index = (idx + 1) % MAX_RTT_SAMPLES;

        if (existing->sample_count < MAX_RTT_SAMPLES) {
            existing->sample_count++;
        }

        existing->last_updated = time(NULL);

        pthread_mutex_unlock(&g_topology_mutex);
        LOG_DEBUG("Added RTT sample %s -> %s: %.2f ms", from_name, to_name, rtt_ms);
        return 0;
    }

    // Create new connection
    if (g_connection_count >= MAX_TOPOLOGY_CONNECTIONS) {
        pthread_mutex_unlock(&g_topology_mutex);
        LOG_WARN("Topology database full (connections): cannot add %s -> %s",
                from_name, to_name);
        return -1;
    }

    TopologyConnection *conn = &g_connections[g_connection_count];
    strncpy(conn->from_name, from_name, sizeof(conn->from_name) - 1);
    strncpy(conn->to_name, to_name, sizeof(conn->to_name) - 1);
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

    LOG_DEBUG("Added connection: %s -> %s (RTT: %.2f ms)", from_name, to_name, rtt_ms);
    return 0;
}

/**
 * Find a connection
 */
TopologyConnection* topology_db_find_connection(const char *from_name, const char *to_name) {
    if (!from_name || !to_name) return NULL;

    pthread_mutex_lock(&g_topology_mutex);

    for (int i = 0; i < g_connection_count; i++) {
        if (strcmp(g_connections[i].from_name, from_name) == 0 &&
            strcmp(g_connections[i].to_name, to_name) == 0) {
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

        if (uac_http_get_location(url, lat, sizeof(lat), lon, sizeof(lon)) == 0) {
            strncpy(node->lat, lat, sizeof(node->lat) - 1);
            strncpy(node->lon, lon, sizeof(node->lon) - 1);
            fetched++;
            LOG_DEBUG("Fetched location for %s: %s, %s", node->name, lat, lon);
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
                // Find source router
                for (int j = 0; j < g_node_count; j++) {
                    TopologyNode *router = &g_nodes[j];

                    if (strcmp(router->name, conn->from_name) == 0) {
                        if (strlen(router->lat) > 0 && strlen(router->lon) > 0) {
                            // Copy with offset
                            double router_lat = atof(router->lat);
                            double router_lon = atof(router->lon);

                            unsigned int seed = 0;
                            for (const char *p = phone->name; *p; p++) {
                                seed = seed * 31 + *p;
                            }
                            srand(seed);

                            double lat_offset = ((double)rand() / RAND_MAX * 0.002) - 0.001;
                            double lon_offset = ((double)rand() / RAND_MAX * 0.002) - 0.001;

                            snprintf(phone->lat, sizeof(phone->lat), "%.7f", router_lat + lat_offset);
                            snprintf(phone->lon, sizeof(phone->lon), "%.7f", router_lon + lon_offset);

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
                        }
                    }
                }
            }
        }
    }

    pclose(fp);

    if (hostname[0] != '\0') {
        LOG_DEBUG("Resolved IP %s to hostname '%s'", ip, hostname);
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
        LOG_DEBUG("Fetched details for %s: lat=%s, lon=%s", hostname, lat, lon);
        return 0;
    }

    return -1;
}

/**
 * Helper: Fetch LQM links from a hostname
 */
static int fetch_lqm_links_from_host(const char *hostname) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --connect-timeout 2 --max-time 5 'http://%s.local.mesh/cgi-bin/sysinfo.json?lqm=1' 2>/dev/null",
             hostname);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        LOG_DEBUG("Failed to fetch LQM data from %s", hostname);
        return -1;
    }

    char line[4096];
    int link_count = 0;
    bool in_trackers = false;
    bool in_tracker_entry = false;
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

            if (strchr(line, '}')) {
                if (neighbor_hostname[0] != '\0' && ping_time_ms > 0.0) {
                    topology_db_add_connection(hostname, neighbor_hostname, ping_time_ms);
                    link_count++;
                    LOG_DEBUG("Added LQM link: %s -> %s (%.2f ms)",
                             hostname, neighbor_hostname, ping_time_ms);
                } else if (neighbor_ip[0] != '\0' && ping_time_ms > 0.0) {
                    // Fallback: resolve IP to hostname
                    char resolved_hostname[256];
                    if (fetch_hostname_from_ip(neighbor_ip, resolved_hostname, sizeof(resolved_hostname)) == 0) {
                        topology_db_add_connection(hostname, resolved_hostname, ping_time_ms);
                        link_count++;
                        LOG_DEBUG("Added LQM link via IP resolution: %s -> %s (%.2f ms)",
                                 hostname, resolved_hostname, ping_time_ms);
                    }
                }
                in_tracker_entry = false;
            }
        }
    }

    pclose(fp);
    LOG_DEBUG("Fetched %d LQM links from %s", link_count, hostname);
    return link_count;
}

/**
 * Helper: Fetch hosts list from localhost
 */
static int fetch_hostnames(char hostnames[][256], int max_hostnames) {
    char cmd[] = "curl -s --connect-timeout 2 --max-time 5 'http://127.0.0.1/cgi-bin/sysinfo.json?hosts=1' 2>/dev/null";

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[4096];
    int count = 0;
    bool in_hosts = false;

    while (fgets(line, sizeof(line), fp) && count < max_hostnames) {
        if (strstr(line, "\"hosts\"")) {
            in_hosts = true;
            continue;
        }

        if (!in_hosts) continue;
        if (strstr(line, "]")) break;

        char *name_start = strstr(line, "\"name\":");
        if (name_start) {
            name_start = strchr(name_start, ':');
            if (name_start) {
                name_start = strchr(name_start, '"');
                if (name_start) {
                    name_start++;
                    char *name_end = strchr(name_start, '"');
                    if (name_end) {
                        int len = name_end - name_start;
                        if (len > 0 && len < 256 && strstr(name_start, "local.mesh") == NULL) {
                            strncpy(hostnames[count], name_start, len);
                            hostnames[count][len] = '\0';
                            count++;
                        }
                    }
                }
            }
        }
    }

    pclose(fp);
    LOG_INFO("Fetched %d hostnames from localhost", count);
    return count;
}

/**
 * Crawl the entire mesh network
 */
void topology_db_crawl_mesh_network(void) {
    LOG_INFO("Starting mesh network crawl from localhost...");

    char hostnames[200][256];
    int num_hosts = fetch_hostnames(hostnames, 200);

    if (num_hosts <= 0) {
        LOG_ERROR("Failed to fetch hostnames from localhost");
        return;
    }

    LOG_INFO("Crawling %d nodes...", num_hosts);

    int processed = 0;
    int discovered = 0;

    for (int i = 0; i < num_hosts; i++) {
        char *hostname = hostnames[i];

        processed++;
        LOG_DEBUG("Crawling node %d/%d: %s", processed, num_hosts, hostname);

        // Fetch node details
        char lat[32] = "";
        char lon[32] = "";

        if (fetch_node_details(hostname, lat, sizeof(lat), lon, sizeof(lon)) == 0) {
            topology_db_add_node(hostname, "router", lat, lon, "ONLINE");
            discovered++;
        } else {
            LOG_DEBUG("Failed to fetch details for %s", hostname);
            continue;
        }

        // Fetch LQM links
        fetch_lqm_links_from_host(hostname);

        usleep(100000);  // 100ms delay
    }

    LOG_INFO("Mesh crawl complete: processed %d nodes, discovered %d nodes with details",
             processed, discovered);
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
    for (int i = 0; i < g_connection_count; i++) {
        TopologyConnection *conn = &g_connections[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"from\": \"%s\",\n", conn->from_name);
        fprintf(fp, "      \"to\": \"%s\",\n", conn->to_name);
        fprintf(fp, "      \"rtt_avg_ms\": %.3f,\n", conn->rtt_avg_ms);
        fprintf(fp, "      \"rtt_min_ms\": %.3f,\n", conn->rtt_min_ms);
        fprintf(fp, "      \"rtt_max_ms\": %.3f,\n", conn->rtt_max_ms);
        fprintf(fp, "      \"sample_count\": %d,\n", conn->sample_count);
        fprintf(fp, "      \"last_updated\": %ld\n", (long)conn->last_updated);
        fprintf(fp, "    }%s\n", (i < g_connection_count - 1) ? "," : "");
    }
    fprintf(fp, "  ],\n");

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
