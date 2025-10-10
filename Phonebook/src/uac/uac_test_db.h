#ifndef UAC_TEST_DB_H
#define UAC_TEST_DB_H

#include <time.h>

// Maximum number of test results to store
#define MAX_TEST_RESULTS 100

// Shared memory name for the test database
#define UAC_TEST_SHM_NAME "/uac_test_db"

// Individual test result entry
typedef struct {
    char phone_number[32];
    char ping_status[16];      // "ONLINE", "OFFLINE", "NO_DNS", "DISABLED"
    float ping_rtt;            // Round-trip time in ms
    float ping_jitter;         // Jitter in ms
    char options_status[16];   // "ONLINE", "OFFLINE", "NO_DNS", "DISABLED"
    float options_rtt;         // Round-trip time in ms
    float options_jitter;      // Jitter in ms
    time_t timestamp;          // When this result was recorded
    int valid;                 // 1 = valid entry, 0 = empty slot
} uac_test_result_t;

// Shared memory database structure
typedef struct {
    int version;               // Database version (for future compatibility)
    int num_results;           // Number of valid results
    time_t last_update;        // Timestamp of last database update
    int test_interval;         // Test interval in seconds
    uac_test_result_t results[MAX_TEST_RESULTS];
} uac_test_db_t;

// Database version
#define UAC_TEST_DB_VERSION 1

// Function prototypes for database operations
int uac_test_db_init(void);
int uac_test_db_write_result(const uac_test_result_t *result);
int uac_test_db_update_header(int num_results, int test_interval);
void uac_test_db_close(void);

#endif // UAC_TEST_DB_H
