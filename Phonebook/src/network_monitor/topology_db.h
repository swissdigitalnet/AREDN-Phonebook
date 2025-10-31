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
    char name[256];             // Hostname (unique key, e.g., "HB9BLA-VM-1")
    char type[16];              // "phone", "router", "server"
    char lat[32];               // Latitude (from sysinfo.json)
    char lon[32];               // Longitude (from sysinfo.json)
    char status[16];            // "ONLINE", "OFFLINE", "NO_DNS"
    time_t last_seen;           // Timestamp of last traceroute
} TopologyNode;

/**
 * Network connection between two nodes
 */
typedef struct {
    char from_name[256];        // Source node hostname
    char to_name[256];          // Destination node hostname
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
 * Clean up stale nodes and connections
 * Removes nodes that haven't been seen for longer than g_topology_node_timeout_seconds
 * and removes orphaned connections
 */
void topology_db_cleanup_stale_nodes(void);

/**
 * Add or update a node in the topology database
 *
 * If a node with the same hostname already exists, it will be updated.
 * Otherwise, a new node will be added.
 *
 * @param name Node hostname (required, unique key, e.g., "HB9BLA-VM-1")
 * @param type Node type: "phone", "router", or "server" (required)
 * @param lat Latitude as string (can be NULL, updated later)
 * @param lon Longitude as string (can be NULL, updated later)
 * @param status Status: "ONLINE", "OFFLINE", "NO_DNS" (required)
 * @return 0 on success, -1 on error (database full)
 */
int topology_db_add_node(
    const char *name,
    const char *type,
    const char *lat,
    const char *lon,
    const char *status
);

/**
 * Find a node by hostname
 *
 * @param name Hostname to search for
 * @return Pointer to node, or NULL if not found
 */
TopologyNode* topology_db_find_node(const char *name);

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
 * @param from_name Source node hostname
 * @param to_name Destination node hostname
 * @param rtt_ms RTT measurement in milliseconds
 * @return 0 on success, -1 on error (database full)
 */
int topology_db_add_connection(
    const char *from_name,
    const char *to_name,
    float rtt_ms
);

/**
 * Find a connection by source and destination hostname
 *
 * @param from_name Source node hostname
 * @param to_name Destination node hostname
 * @return Pointer to connection, or NULL if not found
 */
TopologyConnection* topology_db_find_connection(
    const char *from_name,
    const char *to_name
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
 * Crawl the entire mesh network starting from localhost
 *
 * Uses sysinfo.json?hosts=1 and LQM data to discover all mesh nodes:
 * 1. Fetches hosts list from localhost to get all known hostnames
 * 2. For each hostname, fetches sysinfo.json from hostname.local.mesh
 * 3. Fetches LQM data to discover direct neighbor connections with RTT
 * 4. Adds all nodes and connections to topology database
 *
 * This provides complete network visibility including tunnel connections.
 */
void topology_db_crawl_mesh_network(void);

/**
 * Fetch phones for all routers in topology
 *
 * Iterates through all router nodes and fetches their phones from OLSR services.
 * Useful for catching routers that were added via traceroute but not crawled.
 *
 * @return Number of phones added
 */
int topology_db_fetch_phones_for_all_routers(void);

/**
 * Strip hostname prefix (mid1., mid2., dtdlink., etc.)
 *
 * Removes interface prefixes from hostnames to avoid duplicates.
 * Since hostnames are unique, we always strip prefixes.
 *
 * @param hostname Hostname to strip (e.g., "mid1.HB9HFM-HAP-1")
 * @param buffer Output buffer for cleaned hostname
 * @param buffer_size Size of output buffer
 * @return Pointer to cleaned hostname in buffer
 */
const char* topology_db_strip_hostname_prefix(const char *hostname, char *buffer, size_t buffer_size);

#endif // TOPOLOGY_DB_H
