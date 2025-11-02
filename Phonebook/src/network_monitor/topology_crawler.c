// topology_crawler.c
// Network Topology Crawler Thread

#define MODULE_NAME "TOPOLOGY_CRAWLER"

#include "topology_crawler.h"
#include "topology_db.h"
#include "../config_loader/config_loader.h"
#include "../software_health/software_health.h"
#include "../common.h"
#include <unistd.h>
#include <pthread.h>

/**
 * Topology Crawler Thread
 * Periodically crawls the entire AREDN mesh network using BFS
 */
void *topology_crawler_thread(void *arg) {
    (void)arg;

    LOG_INFO("Topology Crawler thread started");

    // Register this thread for health monitoring
    int thread_index = health_register_thread(pthread_self(), "topology_crawler");
    if (thread_index < 0) {
        LOG_WARN("Failed to register topology crawler thread for health monitoring");
    }

    // Check if crawler is enabled
    if (!g_topology_crawler_enabled) {
        LOG_INFO("Topology crawler disabled. Thread exiting.");
        return NULL;
    }

    // Wait for initial system startup
    LOG_INFO("Waiting 10 seconds for system initialization...");
    sleep(10);

    while (g_keep_running) { // Check shutdown flag for graceful termination
        // Health monitoring heartbeat
        if (thread_index >= 0) {
            health_update_heartbeat(thread_index);
        }

        LOG_INFO("=== Starting mesh network crawl ===");

        // Initialize topology database
        topology_db_init();
        topology_db_cleanup_stale_nodes();

        // Crawl the mesh starting from localhost
        topology_db_crawl_mesh_network();

        // Get statistics
        int node_count = topology_db_get_node_count();
        int connection_count = topology_db_get_connection_count();

        LOG_INFO("Mesh crawl discovered %d nodes, %d connections",
                 node_count, connection_count);

        // Fetch location data for all nodes
        if (g_topology_fetch_locations && node_count > 0) {
            LOG_INFO("Fetching location data for %d nodes...", node_count);
            topology_db_fetch_all_locations();
        }

        // Calculate aggregate statistics
        if (connection_count > 0) {
            LOG_INFO("Calculating aggregate statistics...");
            topology_db_calculate_aggregate_stats();
        }

        // Write topology to JSON file
        if (node_count > 0) {
            LOG_INFO("Writing topology to /tmp/arednmon/network_topology.json...");
            topology_db_write_to_file("/tmp/arednmon/network_topology.json");
        }

        LOG_INFO("=== Mesh crawl complete ===");
        LOG_INFO("Next mesh crawl in %d seconds...", g_topology_crawler_interval_seconds);

        // Wait for next crawl, checking shutdown flag periodically
        for (int i = 0; i < g_topology_crawler_interval_seconds && g_keep_running; i++) {
            sleep(1);
        }
    }

    LOG_INFO("Topology Crawler thread exiting");
    return NULL;
}
