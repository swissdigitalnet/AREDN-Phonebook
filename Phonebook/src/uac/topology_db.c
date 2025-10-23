// topology_db.c
// Network Topology Database Implementation

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
 * Reset topology database
 */
void topology_db_reset(void) {
    pthread_mutex_lock(&g_topology_mutex);

    memset(g_nodes, 0, sizeof(g_nodes));
    g_node_count = 0;
    memset(g_connections, 0, sizeof(g_connections));
    g_connection_count = 0;

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_DEBUG("Topology database reset");
}

/**
 * Add or update a node
 */
int topology_db_add_node(const char *ip, const char *type, const char *name,
                         const char *lat, const char *lon, const char *status) {
    if (!ip || !type || !name || !status) {
        LOG_ERROR("Invalid parameters for topology_db_add_node");
        return -1;
    }

    pthread_mutex_lock(&g_topology_mutex);

    // Check if node already exists
    TopologyNode *existing = NULL;
    for (int i = 0; i < g_node_count; i++) {
        if (strcmp(g_nodes[i].ip, ip) == 0) {
            existing = &g_nodes[i];
            break;
        }
    }

    if (existing) {
        // Update existing node
        strncpy(existing->type, type, sizeof(existing->type) - 1);
        strncpy(existing->name, name, sizeof(existing->name) - 1);
        if (lat) {
            strncpy(existing->lat, lat, sizeof(existing->lat) - 1);
        }
        if (lon) {
            strncpy(existing->lon, lon, sizeof(existing->lon) - 1);
        }
        strncpy(existing->status, status, sizeof(existing->status) - 1);
        existing->last_seen = time(NULL);

        pthread_mutex_unlock(&g_topology_mutex);
        LOG_DEBUG("Updated node: %s (%s)", ip, name);
        return 0;
    }

    // Add new node
    if (g_node_count >= MAX_TOPOLOGY_NODES) {
        pthread_mutex_unlock(&g_topology_mutex);
        LOG_WARN("Topology database full (nodes): cannot add %s", ip);
        return -1;
    }

    TopologyNode *node = &g_nodes[g_node_count];
    strncpy(node->ip, ip, sizeof(node->ip) - 1);
    strncpy(node->type, type, sizeof(node->type) - 1);
    strncpy(node->name, name, sizeof(node->name) - 1);
    if (lat) {
        strncpy(node->lat, lat, sizeof(node->lat) - 1);
    } else {
        node->lat[0] = '\0';
    }
    if (lon) {
        strncpy(node->lon, lon, sizeof(node->lon) - 1);
    } else {
        node->lon[0] = '\0';
    }
    strncpy(node->status, status, sizeof(node->status) - 1);
    node->last_seen = time(NULL);

    g_node_count++;

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_DEBUG("Added node: %s (%s) - type=%s, status=%s",
             ip, name, type, status);
    return 0;
}

/**
 * Find a node by IP
 */
TopologyNode* topology_db_find_node(const char *ip) {
    if (!ip) {
        return NULL;
    }

    pthread_mutex_lock(&g_topology_mutex);

    for (int i = 0; i < g_node_count; i++) {
        if (strcmp(g_nodes[i].ip, ip) == 0) {
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
int topology_db_add_connection(const char *from_ip, const char *to_ip, float rtt_ms) {
    if (!from_ip || !to_ip || rtt_ms < 0) {
        LOG_ERROR("Invalid parameters for topology_db_add_connection");
        return -1;
    }

    pthread_mutex_lock(&g_topology_mutex);

    // Check if connection already exists
    TopologyConnection *existing = NULL;
    for (int i = 0; i < g_connection_count; i++) {
        if (strcmp(g_connections[i].from_ip, from_ip) == 0 &&
            strcmp(g_connections[i].to_ip, to_ip) == 0) {
            existing = &g_connections[i];
            break;
        }
    }

    if (existing) {
        // Add sample to existing connection (circular buffer)
        int idx = existing->next_sample_index;
        existing->samples[idx].rtt_ms = rtt_ms;
        existing->samples[idx].timestamp = time(NULL);
        existing->next_sample_index = (idx + 1) % MAX_RTT_SAMPLES;

        if (existing->sample_count < MAX_RTT_SAMPLES) {
            existing->sample_count++;
        }

        existing->last_updated = time(NULL);

        pthread_mutex_unlock(&g_topology_mutex);
        LOG_DEBUG("Added RTT sample to connection %s -> %s: %.2f ms (total samples: %d)",
                 from_ip, to_ip, rtt_ms, existing->sample_count);
        return 0;
    }

    // Create new connection
    if (g_connection_count >= MAX_TOPOLOGY_CONNECTIONS) {
        pthread_mutex_unlock(&g_topology_mutex);
        LOG_WARN("Topology database full (connections): cannot add %s -> %s",
                from_ip, to_ip);
        return -1;
    }

    TopologyConnection *conn = &g_connections[g_connection_count];
    strncpy(conn->from_ip, from_ip, sizeof(conn->from_ip) - 1);
    strncpy(conn->to_ip, to_ip, sizeof(conn->to_ip) - 1);
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

    LOG_DEBUG("Added connection: %s -> %s (RTT: %.2f ms)",
             from_ip, to_ip, rtt_ms);
    return 0;
}

/**
 * Find a connection
 */
TopologyConnection* topology_db_find_connection(const char *from_ip, const char *to_ip) {
    if (!from_ip || !to_ip) {
        return NULL;
    }

    pthread_mutex_lock(&g_topology_mutex);

    for (int i = 0; i < g_connection_count; i++) {
        if (strcmp(g_connections[i].from_ip, from_ip) == 0 &&
            strcmp(g_connections[i].to_ip, to_ip) == 0) {
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
 * Phase 1: Fetch locations for routers and servers only (phones don't have sysinfo.json)
 * Phase 2: Propagate router locations to connected phones
 */
void topology_db_fetch_all_locations(void) {
    LOG_INFO("Fetching location data for %d nodes...", g_node_count);

    int fetched = 0;
    int failed = 0;
    int propagated = 0;

    pthread_mutex_lock(&g_topology_mutex);

    // Phase 1: Fetch locations for routers and servers only
    LOG_INFO("Phase 1: Fetching locations for routers and servers...");
    for (int i = 0; i < g_node_count; i++) {
        TopologyNode *node = &g_nodes[i];

        // Skip phones - they don't have sysinfo.json
        if (strcmp(node->type, "phone") == 0) {
            continue;
        }

        // Skip if already has location
        if (strlen(node->lat) > 0 && strlen(node->lon) > 0) {
            continue;
        }

        // Build URL
        char url[256];
        snprintf(url, sizeof(url), "http://%s/cgi-bin/sysinfo.json", node->ip);

        // Fetch location (HTTP GET with 2-second timeout)
        char lat[32] = "";
        char lon[32] = "";

        if (uac_http_get_location(url, lat, sizeof(lat), lon, sizeof(lon)) == 0) {
            strncpy(node->lat, lat, sizeof(node->lat) - 1);
            strncpy(node->lon, lon, sizeof(node->lon) - 1);
            fetched++;
            LOG_DEBUG("Fetched location for %s (%s): %s, %s", node->ip, node->type, lat, lon);
        } else {
            failed++;
            LOG_DEBUG("Failed to fetch location for %s (%s)", node->ip, node->type);
        }
    }

    // Phase 2: Propagate router locations to connected phones
    LOG_INFO("Phase 2: Propagating router locations to phones...");
    for (int i = 0; i < g_node_count; i++) {
        TopologyNode *phone = &g_nodes[i];

        // Skip non-phones
        if (strcmp(phone->type, "phone") != 0) {
            continue;
        }

        // Skip if already has location
        if (strlen(phone->lat) > 0 && strlen(phone->lon) > 0) {
            continue;
        }

        // Find a connection where this phone is the destination
        // The source should be a router with location data
        for (int c = 0; c < g_connection_count; c++) {
            TopologyConnection *conn = &g_connections[c];

            // Check if this connection leads to the phone
            if (strcmp(conn->to_ip, phone->ip) == 0) {
                // Find the source node (router)
                for (int j = 0; j < g_node_count; j++) {
                    TopologyNode *router = &g_nodes[j];

                    if (strcmp(router->ip, conn->from_ip) == 0) {
                        // Check if this router has location data
                        if (strlen(router->lat) > 0 && strlen(router->lon) > 0) {
                            // Copy router location to phone with random offset
                            // Offset by ~100m in a random direction for visibility on map
                            // 0.001 degrees ≈ 111 meters latitude, ~100m longitude at mid-latitudes

                            double router_lat = atof(router->lat);
                            double router_lon = atof(router->lon);

                            // Generate random offset: -0.001 to +0.001 degrees
                            // Use phone IP as seed for consistency across test cycles
                            unsigned int seed = 0;
                            for (const char *p = phone->ip; *p; p++) {
                                seed = seed * 31 + *p;
                            }
                            srand(seed);

                            double lat_offset = ((double)rand() / RAND_MAX * 0.002) - 0.001;
                            double lon_offset = ((double)rand() / RAND_MAX * 0.002) - 0.001;

                            double phone_lat = router_lat + lat_offset;
                            double phone_lon = router_lon + lon_offset;

                            snprintf(phone->lat, sizeof(phone->lat), "%.7f", phone_lat);
                            snprintf(phone->lon, sizeof(phone->lon), "%.7f", phone_lon);

                            propagated++;
                            LOG_DEBUG("Propagated location from %s (%s) to phone %s: %s, %s (offset: %.4f, %.4f)",
                                     router->ip, router->name, phone->name,
                                     phone->lat, phone->lon, lat_offset, lon_offset);
                            goto next_phone;  // Done with this phone
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
 * Calculate aggregate statistics for all connections
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

        // Calculate min, max, avg
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

        LOG_DEBUG("Connection %s -> %s: avg=%.2f ms, min=%.2f ms, max=%.2f ms (samples=%d)",
                 conn->from_ip, conn->to_ip, conn->rtt_avg_ms,
                 conn->rtt_min_ms, conn->rtt_max_ms, conn->sample_count);
    }

    pthread_mutex_unlock(&g_topology_mutex);

    LOG_INFO("Statistics calculation complete");
}

/**
 * Create directory recursively (helper function)
 */
static int create_directory(const char *path) {
    char temp_path[512];
    strncpy(temp_path, path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';

    // Extract directory from filepath
    char *dir = dirname(temp_path);

    // Try to create directory (mkdir -p equivalent)
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0755) == -1) {
            if (errno != EEXIST) {
                LOG_ERROR("Failed to create directory %s: %s", dir, strerror(errno));
                return -1;
            }
        }
        LOG_DEBUG("Created directory: %s", dir);
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

    // Ensure directory exists
    if (create_directory(filepath) != 0) {
        LOG_ERROR("Failed to create directory for %s", filepath);
        return -1;
    }

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        LOG_ERROR("Failed to open %s for writing: %s", filepath, strerror(errno));
        return -1;
    }

    pthread_mutex_lock(&g_topology_mutex);

    // Write JSON header
    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": \"2.0\",\n");

    // Write timestamp (ISO 8601 format)
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    fprintf(fp, "  \"generated_at\": \"%s\",\n", timestamp);

    // Write source node (this node - will be filled by caller, use first node for now)
    fprintf(fp, "  \"source_node\": {\n");
    if (g_node_count > 0) {
        fprintf(fp, "    \"ip\": \"%s\",\n", g_nodes[0].ip);
        fprintf(fp, "    \"name\": \"%s\",\n", g_nodes[0].name);
        fprintf(fp, "    \"type\": \"server\"\n");
    } else {
        fprintf(fp, "    \"ip\": \"0.0.0.0\",\n");
        fprintf(fp, "    \"name\": \"unknown\",\n");
        fprintf(fp, "    \"type\": \"server\"\n");
    }
    fprintf(fp, "  },\n");

    // Write nodes array
    fprintf(fp, "  \"nodes\": [\n");
    for (int i = 0; i < g_node_count; i++) {
        TopologyNode *node = &g_nodes[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"ip\": \"%s\",\n", node->ip);
        fprintf(fp, "      \"type\": \"%s\",\n", node->type);
        fprintf(fp, "      \"name\": \"%s\",\n", node->name);
        fprintf(fp, "      \"lat\": \"%s\",\n", node->lat);
        fprintf(fp, "      \"lon\": \"%s\",\n", node->lon);
        fprintf(fp, "      \"status\": \"%s\",\n", node->status);
        fprintf(fp, "      \"last_seen\": %ld\n", (long)node->last_seen);
        fprintf(fp, "    }%s\n", (i < g_node_count - 1) ? "," : "");
    }
    fprintf(fp, "  ],\n");

    // Write connections array
    fprintf(fp, "  \"connections\": [\n");
    for (int i = 0; i < g_connection_count; i++) {
        TopologyConnection *conn = &g_connections[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"from\": \"%s\",\n", conn->from_ip);
        fprintf(fp, "      \"to\": \"%s\",\n", conn->to_ip);
        fprintf(fp, "      \"rtt_avg_ms\": %.3f,\n", conn->rtt_avg_ms);
        fprintf(fp, "      \"rtt_min_ms\": %.3f,\n", conn->rtt_min_ms);
        fprintf(fp, "      \"rtt_max_ms\": %.3f,\n", conn->rtt_max_ms);
        fprintf(fp, "      \"sample_count\": %d,\n", conn->sample_count);
        fprintf(fp, "      \"last_updated\": %ld\n", (long)conn->last_updated);
        fprintf(fp, "    }%s\n", (i < g_connection_count - 1) ? "," : "");
    }
    fprintf(fp, "  ],\n");

    // Write statistics
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
 * Helper: Fetch hosts list from a node using curl
 * Returns number of hosts found, or -1 on error
 */
static int fetch_hosts_from_node(const char *node_ip, char hosts[][INET_ADDRSTRLEN], int max_hosts) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --connect-timeout 2 --max-time 5 'http://%s/cgi-bin/sysinfo.json?hosts=1' 2>/dev/null",
             node_ip);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        LOG_DEBUG("Failed to fetch hosts from %s", node_ip);
        return -1;
    }

    // Read response (simple JSON parsing for hosts array)
    char line[4096];
    int host_count = 0;
    bool in_hosts = false;

    while (fgets(line, sizeof(line), fp) && host_count < max_hosts) {
        // Look for "hosts": [
        if (strstr(line, "\"hosts\"")) {
            in_hosts = true;
            continue;
        }

        if (!in_hosts) continue;

        // End of hosts array
        if (strstr(line, "]")) {
            break;
        }

        // Extract IP from: "ip": "10.x.x.x"
        char *ip_start = strstr(line, "\"ip\":");
        if (ip_start) {
            // Move to the colon
            ip_start = strchr(ip_start, ':');
            if (ip_start) {
                ip_start++; // Move past the colon
                // Skip whitespace and opening quote
                while (*ip_start == ' ' || *ip_start == '"') ip_start++;
                // Find the end (closing quote or comma)
                char *ip_end = ip_start;
                while (*ip_end && *ip_end != '"' && *ip_end != ',' && *ip_end != '\n') ip_end++;

                int len = ip_end - ip_start;
                if (len > 0 && len < INET_ADDRSTRLEN) {
                    strncpy(hosts[host_count], ip_start, len);
                    hosts[host_count][len] = '\0';
                    host_count++;
                }
            }
        }
    }

    pclose(fp);
    LOG_DEBUG("Fetched %d hosts from %s", host_count, node_ip);
    return host_count;
}

/**
 * Helper: Fetch node details from sysinfo.json
 * Returns 0 on success, -1 on error
 */
static int fetch_node_details(const char *node_ip, char *name, size_t name_len,
                              char *lat, size_t lat_len, char *lon, size_t lon_len) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --connect-timeout 2 --max-time 5 'http://%s/cgi-bin/sysinfo.json' 2>/dev/null",
             node_ip);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return -1;
    }

    // Simple JSON parsing
    char line[1024];
    name[0] = '\0';
    lat[0] = '\0';
    lon[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        // Extract node name
        if (strstr(line, "\"node\":")) {
            char *val_start = strchr(line, ':');
            if (val_start) {
                val_start = strchr(val_start, '"');
                if (val_start) {
                    val_start++;
                    char *val_end = strchr(val_start, '"');
                    if (val_end) {
                        int len = val_end - val_start;
                        if (len > 0 && len < (int)name_len) {
                            strncpy(name, val_start, len);
                            name[len] = '\0';
                        }
                    }
                }
            }
        }

        // Extract latitude
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

        // Extract longitude
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

    if (name[0] != '\0') {
        LOG_DEBUG("Fetched details for %s: name=%s, lat=%s, lon=%s", node_ip, name, lat, lon);
        return 0;
    }

    return -1;
}

/**
 * Crawl the entire mesh network using BFS
 */
void topology_db_crawl_mesh_network(const char *seed_ip) {
    if (!seed_ip) {
        LOG_ERROR("Invalid seed IP for mesh crawl");
        return;
    }

    LOG_INFO("Starting mesh network crawl from %s...", seed_ip);

    // BFS queue (simple array-based queue)
    char queue[MAX_TOPOLOGY_NODES][INET_ADDRSTRLEN];
    int queue_head = 0;
    int queue_tail = 0;

    // Visited set (simple array for O(n) lookup - good enough for mesh networks)
    static char visited[MAX_TOPOLOGY_NODES][INET_ADDRSTRLEN];
    static int visited_count = 0;
    visited_count = 0;  // Reset for each crawl

    // Add seed to queue
    strncpy(queue[queue_tail], seed_ip, INET_ADDRSTRLEN - 1);
    queue[queue_tail][INET_ADDRSTRLEN - 1] = '\0';
    queue_tail++;

    // Mark seed as visited
    strncpy(visited[visited_count], seed_ip, INET_ADDRSTRLEN - 1);
    visited[visited_count][INET_ADDRSTRLEN - 1] = '\0';
    visited_count++;

    int nodes_discovered = 0;
    int nodes_processed = 0;

    // BFS loop
    while (queue_head < queue_tail && queue_tail < MAX_TOPOLOGY_NODES) {
        char current_ip[INET_ADDRSTRLEN];
        strncpy(current_ip, queue[queue_head], sizeof(current_ip) - 1);
        current_ip[sizeof(current_ip) - 1] = '\0';
        queue_head++;
        nodes_processed++;

        LOG_DEBUG("Crawling node %d/%d: %s", nodes_processed, queue_tail, current_ip);

        // Fetch node details
        char node_name[256] = "";
        char lat[32] = "";
        char lon[32] = "";

        if (fetch_node_details(current_ip, node_name, sizeof(node_name),
                              lat, sizeof(lat), lon, sizeof(lon)) == 0) {
            // Add node to topology database
            // Type is "router" for now (phones will be added by traceroute)
            topology_db_add_node(current_ip, "router", node_name, lat, lon, "ONLINE");
            nodes_discovered++;
        } else {
            LOG_DEBUG("Failed to fetch details for %s, skipping", current_ip);
            continue;
        }

        // Fetch hosts list from this node
        char hosts[200][INET_ADDRSTRLEN];  // Max 200 hosts per node
        int num_hosts = fetch_hosts_from_node(current_ip, hosts, 200);

        if (num_hosts > 0) {
            LOG_DEBUG("Node %s has %d hosts", current_ip, num_hosts);

            // Add unvisited hosts to queue
            for (int i = 0; i < num_hosts && queue_tail < MAX_TOPOLOGY_NODES; i++) {
                // Check if already visited
                bool already_visited = false;
                for (int v = 0; v < visited_count; v++) {
                    if (strcmp(visited[v], hosts[i]) == 0) {
                        already_visited = true;
                        break;
                    }
                }

                if (!already_visited) {
                    // Add to queue
                    strncpy(queue[queue_tail], hosts[i], INET_ADDRSTRLEN - 1);
                    queue[queue_tail][INET_ADDRSTRLEN - 1] = '\0';
                    queue_tail++;

                    // Mark as visited
                    if (visited_count < MAX_TOPOLOGY_NODES) {
                        strncpy(visited[visited_count], hosts[i], INET_ADDRSTRLEN - 1);
                        visited[visited_count][INET_ADDRSTRLEN - 1] = '\0';
                        visited_count++;
                    }
                }
            }
        }

        // Small delay to avoid overwhelming the network
        usleep(100000);  // 100ms delay between nodes
    }

    LOG_INFO("Mesh crawl complete: processed %d nodes, discovered %d nodes with details",
             nodes_processed, nodes_discovered);
}
