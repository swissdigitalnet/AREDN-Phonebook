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

#endif // PING_BULK_TEST_H
