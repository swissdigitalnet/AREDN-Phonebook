// topology_crawler.h
// Network Topology Crawler Thread

#ifndef TOPOLOGY_CRAWLER_H
#define TOPOLOGY_CRAWLER_H

/**
 * Topology Crawler Thread
 * Periodically crawls the entire AREDN mesh network using BFS
 */
void *topology_crawler_thread(void *arg);

#endif // TOPOLOGY_CRAWLER_H
