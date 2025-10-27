#define MODULE_NAME "PHONE_TEST_DB"

#include "phone_test_db.h"
#include "../common.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// Global pointer to shared memory database
static phone_test_db_t *g_test_db = NULL;
static int g_shm_fd = -1;

/**
 * Initialize the shared memory database
 * Creates or opens the shared memory region
 * Returns: 0 on success, -1 on failure
 */
int phone_test_db_init(void) {
    // Open/create shared memory object
    g_shm_fd = shm_open(PHONE_TEST_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (g_shm_fd == -1) {
        LOG_ERROR("Failed to open shared memory: %s", strerror(errno));
        return -1;
    }

    // Set the size of the shared memory
    if (ftruncate(g_shm_fd, sizeof(phone_test_db_t)) == -1) {
        LOG_ERROR("Failed to set shared memory size: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        return -1;
    }

    // Map the shared memory into process address space
    g_test_db = mmap(NULL, sizeof(phone_test_db_t), PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_shm_fd, 0);
    if (g_test_db == MAP_FAILED) {
        LOG_ERROR("Failed to map shared memory: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        g_test_db = NULL;
        return -1;
    }

    // Initialize database on first creation (check version)
    if (g_test_db->version != PHONE_TEST_DB_VERSION) {
        LOG_INFO("Initializing shared memory database (version %d)", PHONE_TEST_DB_VERSION);
        memset(g_test_db, 0, sizeof(phone_test_db_t));
        g_test_db->version = PHONE_TEST_DB_VERSION;
        g_test_db->num_results = 0;
        g_test_db->last_update = time(NULL);
        g_test_db->test_interval = 60;
    }

    LOG_DEBUG("Shared memory database initialized successfully");
    return 0;
}

/**
 * Write a test result to the database
 * Automatically finds an empty slot or overwrites oldest entry
 * Returns: 0 on success, -1 on failure
 */
int phone_test_db_write_result(const phone_test_result_t *result) {
    if (!g_test_db) {
        LOG_ERROR("Database not initialized");
        return -1;
    }

    if (!result) {
        LOG_ERROR("NULL result pointer");
        return -1;
    }

    // Find existing entry for this phone number or empty slot
    int slot = -1;
    int oldest_slot = 0;
    time_t oldest_time = g_test_db->results[0].timestamp;

    for (int i = 0; i < MAX_TEST_RESULTS; i++) {
        // Check if this slot has the same phone number (update existing)
        if (g_test_db->results[i].valid &&
            strcmp(g_test_db->results[i].phone_number, result->phone_number) == 0) {
            slot = i;
            break;
        }

        // Check for empty slot
        if (!g_test_db->results[i].valid && slot == -1) {
            slot = i;
            // Don't break - continue looking for existing entry
        }

        // Track oldest entry (for overwriting if needed)
        if (g_test_db->results[i].timestamp < oldest_time) {
            oldest_time = g_test_db->results[i].timestamp;
            oldest_slot = i;
        }
    }

    // If no empty slot and no matching phone, use oldest slot
    if (slot == -1) {
        slot = oldest_slot;
        LOG_DEBUG("Database full, overwriting oldest entry at slot %d", slot);
    }

    // Write the result
    memcpy(&g_test_db->results[slot], result, sizeof(phone_test_result_t));
    g_test_db->results[slot].valid = 1;
    g_test_db->results[slot].timestamp = time(NULL);

    // Update count if this was a new entry
    int count = 0;
    for (int i = 0; i < MAX_TEST_RESULTS; i++) {
        if (g_test_db->results[i].valid) {
            count++;
        }
    }
    g_test_db->num_results = count;
    g_test_db->last_update = time(NULL);

    LOG_DEBUG("Wrote test result for %s to slot %d", result->phone_number, slot);
    return 0;
}

/**
 * Update database header information
 * Returns: 0 on success, -1 on failure
 */
int phone_test_db_update_header(int num_results, int num_testable_phones, int test_interval) {
    if (!g_test_db) {
        LOG_ERROR("Database not initialized");
        return -1;
    }

    g_test_db->num_results = num_results;
    g_test_db->num_testable_phones = num_testable_phones;
    g_test_db->test_interval = test_interval;
    g_test_db->last_update = time(NULL);

    LOG_DEBUG("Updated database header: %d results, %d testable phones, %d second interval",
              num_results, num_testable_phones, test_interval);
    return 0;
}

/**
 * Close and cleanup the shared memory database
 */
void phone_test_db_close(void) {
    if (g_test_db) {
        munmap(g_test_db, sizeof(phone_test_db_t));
        g_test_db = NULL;
    }

    if (g_shm_fd != -1) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }

    LOG_DEBUG("Shared memory database closed");
}
