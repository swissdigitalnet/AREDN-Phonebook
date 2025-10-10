/*
 * UAC Test Database Reader CGI
 * Reads shared memory database and outputs JSON
 */

#include "uac_test_db.h"
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

// Helper function to compare phone numbers for qsort
static int compare_phone_numbers(const void *a, const void *b) {
    const uac_test_result_t *result_a = (const uac_test_result_t *)a;
    const uac_test_result_t *result_b = (const uac_test_result_t *)b;

    // Invalid entries go to the end
    if (!result_a->valid && !result_b->valid) return 0;
    if (!result_a->valid) return 1;
    if (!result_b->valid) return -1;

    // Compare phone numbers as strings
    return strcmp(result_a->phone_number, result_b->phone_number);
}

int main(void) {
    // Open shared memory
    int shm_fd = shm_open(UAC_TEST_SHM_NAME, O_RDONLY, 0666);
    if (shm_fd == -1) {
        // No database exists yet - return empty JSON
        printf("Content-Type: application/json\r\n\r\n");
        printf("{\"error\":\"Database not initialized\",\"results\":[]}\n");
        return 0;
    }

    // Map shared memory
    uac_test_db_t *db = mmap(NULL, sizeof(uac_test_db_t), PROT_READ,
                             MAP_SHARED, shm_fd, 0);
    if (db == MAP_FAILED) {
        printf("Content-Type: application/json\r\n\r\n");
        printf("{\"error\":\"Failed to map shared memory\",\"results\":[]}\n");
        close(shm_fd);
        return 1;
    }

    // Copy results to local array for sorting
    uac_test_result_t sorted_results[MAX_TEST_RESULTS];
    memcpy(sorted_results, db->results, sizeof(sorted_results));

    // Sort results by phone number
    qsort(sorted_results, MAX_TEST_RESULTS, sizeof(uac_test_result_t),
          compare_phone_numbers);

    // Output HTTP headers
    printf("Content-Type: application/json\r\n");
    printf("Cache-Control: no-cache, no-store, must-revalidate\r\n");
    printf("Pragma: no-cache\r\n");
    printf("Expires: 0\r\n");
    printf("\r\n");

    // Output JSON
    printf("{\n");
    printf("  \"version\": %d,\n", db->version);
    printf("  \"num_results\": %d,\n", db->num_results);
    printf("  \"num_testable_phones\": %d,\n", db->num_testable_phones);
    printf("  \"last_update\": %ld,\n", (long)db->last_update);
    printf("  \"test_interval\": %d,\n", db->test_interval);
    printf("  \"results\": [\n");

    int first = 1;
    for (int i = 0; i < MAX_TEST_RESULTS; i++) {
        if (!sorted_results[i].valid) {
            continue;
        }

        if (!first) {
            printf(",\n");
        }
        first = 0;

        printf("    {\n");
        printf("      \"phone_number\": \"%s\",\n", sorted_results[i].phone_number);
        printf("      \"ping_status\": \"%s\",\n", sorted_results[i].ping_status);
        printf("      \"ping_rtt\": %.2f,\n", sorted_results[i].ping_rtt);
        printf("      \"ping_jitter\": %.2f,\n", sorted_results[i].ping_jitter);
        printf("      \"options_status\": \"%s\",\n", sorted_results[i].options_status);
        printf("      \"options_rtt\": %.2f,\n", sorted_results[i].options_rtt);
        printf("      \"options_jitter\": %.2f,\n", sorted_results[i].options_jitter);
        printf("      \"timestamp\": %ld\n", (long)sorted_results[i].timestamp);
        printf("    }");
    }

    printf("\n  ]\n");
    printf("}\n");

    // Cleanup
    munmap(db, sizeof(uac_test_db_t));
    close(shm_fd);

    return 0;
}
