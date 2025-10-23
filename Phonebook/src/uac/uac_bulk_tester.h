// uac_bulk_tester.h - UAC Bulk Testing Thread
#ifndef UAC_BULK_TESTER_H
#define UAC_BULK_TESTER_H

#include <pthread.h>

/**
 * UAC Bulk Testing Thread
 *
 * Periodically tests all registered users from the phonebook:
 * - Loops through all registered users
 * - For each user, checks if DNS resolves (<phone_number>.local.mesh)
 * - If DNS resolves (node is reachable), triggers UAC test call
 * - Waits for configured interval before next test cycle
 */

/**
 * Start UAC bulk testing thread
 * @return 0 on success, -1 on failure
 */
void *uac_bulk_tester_thread(void *arg);

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

#endif // UAC_BULK_TESTER_H
