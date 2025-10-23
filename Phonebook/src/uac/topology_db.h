// topology_db.h
// Network Topology Database for AREDNmon
// Stores discovered nodes and connections with RTT statistics

#ifndef TOPOLOGY_DB_H
#define TOPOLOGY_DB_H

#include <time.h>
#include <stdbool.h>
#include <netinet/in.h>

// Maximum database capacity
#define MAX_TOPOLOGY_NODES 500
#define MAX_TOPOLOGY_CONNECTIONS 2000
#define MAX_RTT_SAMPLES 10

/**
 * RTT sample for connection statistics
 */
typedef struct {
    float rtt_ms;        // RTT measurement in milliseconds
    time_t timestamp;    // When this sample was recorded
} RTT_Sample;

/**
 * Network node (phone, router, or server)
 */
typedef struct {
    char ip[INET_ADDRSTRLEN];  // Node IP address (unique key)
    char type[16];              // "phone", "router", "server"
    char name[256];             // Display name (phone number or hostname)
    char lat[32];               // Latitude (from sysinfo.json)
    char lon[32];               // Longitude (from sysinfo.json)
    char status[16];            // "ONLINE", "OFFLINE", "NO_DNS"
    time_t last_seen;           // Timestamp of last traceroute
} TopologyNode;

/**
 * Network connection between two nodes
 */
typedef struct {
    char from_ip[INET_ADDRSTRLEN]; // Source node IP
    char to_ip[INET_ADDRSTRLEN];   // Destination node IP
    RTT_Sample samples[MAX_RTT_SAMPLES]; // Circular buffer of RTT samples
    int sample_count;              // Number of samples stored (0-10)
    int next_sample_index;         // Next position in circular buffer
    float rtt_avg_ms;              // Calculated average RTT
    float rtt_min_ms;              // Minimum RTT
    float rtt_max_ms;              // Maximum RTT
    time_t last_updated;           // Timestamp of last update
} TopologyConnection;

/**
 * Initialize topology database
 * Must be called before using any other functions
 */
void topology_db_init(void);

/**
 * Reset topology database
 * Clears all nodes and connections (start of new scan cycle)
 */
void topology_db_reset(void);

/**
 * Add or update a node in the topology database
 *
 * If a node with the same IP already exists, it will be updated.
 * Otherwise, a new node will be added.
 *
 * @param ip Node IP address (required, unique key)
 * @param type Node type: "phone", "router", or "server" (required)
 * @param name Display name or hostname (required)
 * @param lat Latitude as string (can be NULL, updated later)
 * @param lon Longitude as string (can be NULL, updated later)
 * @param status Status: "ONLINE", "OFFLINE", "NO_DNS" (required)
 * @return 0 on success, -1 on error (database full)
 */
int topology_db_add_node(
    const char *ip,
    const char *type,
    const char *name,
    const char *lat,
    const char *lon,
    const char *status
);

/**
 * Find a node by IP address
 *
 * @param ip IP address to search for
 * @return Pointer to node, or NULL if not found
 */
TopologyNode* topology_db_find_node(const char *ip);

/**
 * Get total number of nodes in database
 *
 * @return Node count
 */
int topology_db_get_node_count(void);

/**
 * Add RTT sample to a connection
 *
 * If the connection doesn't exist, it will be created.
 * RTT samples are stored in a circular buffer (last 10 samples).
 *
 * @param from_ip Source node IP
 * @param to_ip Destination node IP
 * @param rtt_ms RTT measurement in milliseconds
 * @return 0 on success, -1 on error (database full)
 */
int topology_db_add_connection(
    const char *from_ip,
    const char *to_ip,
    float rtt_ms
);

/**
 * Find a connection by source and destination IP
 *
 * @param from_ip Source node IP
 * @param to_ip Destination node IP
 * @return Pointer to connection, or NULL if not found
 */
TopologyConnection* topology_db_find_connection(
    const char *from_ip,
    const char *to_ip
);

/**
 * Get total number of connections in database
 *
 * @return Connection count
 */
int topology_db_get_connection_count(void);

/**
 * Fetch location data for all nodes from sysinfo.json
 *
 * For each node without lat/lon, attempts to fetch from:
 * http://<node_ip>/cgi-bin/sysinfo.json
 *
 * Uses HTTP GET with 2-second timeout per node.
 * Updates node lat/lon fields on success.
 */
void topology_db_fetch_all_locations(void);

/**
 * Calculate aggregate statistics for all connections
 *
 * For each connection, calculates:
 * - Average RTT (mean of all samples)
 * - Minimum RTT (min of all samples)
 * - Maximum RTT (max of all samples)
 *
 * Should be called after scan cycle completes and before writing JSON.
 */
void topology_db_calculate_aggregate_stats(void);

/**
 * Write topology database to JSON file
 *
 * Generates JSON file with nodes, connections, and statistics.
 * Format matches the specification in route-analysis-fsd-v2.md.
 *
 * @param filepath Path to output JSON file (e.g., "/tmp/arednmon/network_topology.json")
 * @return 0 on success, -1 on error
 */
int topology_db_write_to_file(const char *filepath);

/**
 * Crawl the entire mesh network starting from a seed node
 *
 * Uses BFS (Breadth-First Search) to discover all reachable nodes:
 * 1. Fetches sysinfo.json?hosts=1 from each node to get its host list
 * 2. For each discovered node, fetches sysinfo.json to get details
 * 3. Adds all discovered nodes and their details to topology database
 * 4. Continues until no new nodes are found
 *
 * This provides complete network visibility instead of just nodes
 * in traceroute paths.
 *
 * @param seed_ip IP address to start crawling from (e.g., "127.0.0.1")
 */
void topology_db_crawl_mesh_network(const char *seed_ip);

#endif // TOPOLOGY_DB_H
