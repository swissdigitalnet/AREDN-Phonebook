// ping_bulk_test.h - Phone Bulk Testing Thread
#ifndef PING_BULK_TEST_H
#define PING_BULK_TEST_H

#include <pthread.h>

/**
 * Phone Bulk Testing Thread
 *
 * Periodically tests all registered users from the phonebook:
 * - Loops through all registered users
 * - For each user, checks if DNS resolves (<phone_number>.local.mesh)
 * - If DNS resolves (node is reachable), triggers test call
 * - Waits for configured interval before next test cycle
 */

/**
 * Start phone bulk testing thread
 * @return 0 on success, -1 on failure
 */
void *ping_bulk_test_thread(void *arg);

/**
 * Topology Crawler Thread
 *
 * Periodically crawls the entire AREDN mesh network to discover all nodes:
 * - Uses BFS (Breadth-First Search) starting from localhost
 * - Fetches sysinfo.json?hosts=1 from each node to get its host list
 * - Fetches sysinfo.json to get node details (name, lat/lon)
 * - Adds all discovered nodes to topology database
 * - Runs independently from bulk testing (different schedule)
 *
 * @param arg Thread argument (unused)
 * @return NULL
 */
void *topology_crawler_thread(void *arg);

#endif // PING_BULK_TEST_H
