#ifndef PHONE_PING_H
#define PHONE_PING_H

#include <time.h>

// Maximum number of ping results to store
#define MAX_PING_RESULTS 100

// Shared memory name for the phone ping database
#define PHONE_PING_SHM_NAME "/phone_ping_db"

// Individual ping result entry
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
} phone_ping_result_t;

// Shared memory database structure
typedef struct {
    int version;               // Database version (for future compatibility)
    int num_results;           // Number of valid results
    int num_testable_phones;   // Total number of phones with DNS resolution (testable)
    time_t last_update;        // Timestamp of last database update
    int test_interval;         // Test interval in seconds
    phone_ping_result_t results[MAX_PING_RESULTS];
} phone_ping_db_t;

// Database version (increment when structure changes to force re-initialization)
#define PHONE_PING_DB_VERSION 2

// Function prototypes for database operations
int phone_ping_init(void);
int phone_ping_write_result(const phone_ping_result_t *result);
int phone_ping_update_header(int num_results, int num_testable_phones, int test_interval);
void phone_ping_close(void);

#endif // PHONE_PING_H
