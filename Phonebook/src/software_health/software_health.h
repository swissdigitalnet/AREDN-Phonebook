// software_health.h
// Software Health Monitoring System - Main API
// Monitors AREDN-Phonebook process health, threads, and crash detection

#ifndef SOFTWARE_HEALTH_H
#define SOFTWARE_HEALTH_H

#include <time.h>
#include <stdbool.h>
#include <pthread.h>

// ============================================================================
// CONFIGURATION CONSTANTS
// ============================================================================

#define HEALTH_MAX_THREADS 5
#define HEALTH_MAX_CRASH_REASON_LEN 256
#define HEALTH_MAX_THREAD_NAME_LEN 32
#define HEALTH_MAX_NODE_NAME_LEN 64
#define HEALTH_BACKTRACE_MAX_DEPTH 10

// Health score thresholds
#define HEALTH_SCORE_EXCELLENT 90
#define HEALTH_SCORE_GOOD 70
#define HEALTH_SCORE_DEGRADED 50

// File paths
#define HEALTH_STATUS_JSON_PATH "/tmp/software_health.json"
#define CRASH_REPORT_JSON_PATH "/tmp/last_crash.json"
#define CRASH_STATE_BIN_PATH "/tmp/meshmon_crash.bin"

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * Process Health - Overall process metrics
 */
typedef struct {
    time_t process_start_time;      // When process started
    time_t last_restart_time;        // Last restart timestamp
    int restart_count_24h;           // Restarts in last 24 hours
    int crash_count_24h;             // Crashes in last 24 hours
    char last_crash_reason[HEALTH_MAX_CRASH_REASON_LEN];
    time_t last_crash_time;
} process_health_t;

/**
 * Thread Health - Individual thread monitoring
 */
typedef struct {
    pthread_t tid;                   // Thread ID
    char name[HEALTH_MAX_THREAD_NAME_LEN];  // Thread name
    time_t last_heartbeat;           // Last heartbeat timestamp
    time_t start_time;               // Thread start time
    int restart_count;               // How many times restarted
    bool is_responsive;              // Currently responsive?
    bool is_active;                  // Thread slot in use?
} thread_health_t;

/**
 * Memory Health - Memory usage tracking
 */
typedef struct {
    size_t initial_rss_bytes;        // RSS at startup
    size_t current_rss_bytes;        // Current RSS
    size_t peak_rss_bytes;           // Peak RSS observed
    float growth_rate_mb_per_hour;   // Memory growth rate
    bool leak_suspected;             // Leak detection flag
    time_t last_check_time;          // Last check timestamp
} memory_health_t;

/**
 * CPU Metrics - CPU usage tracking
 */
typedef struct {
    float current_cpu_pct;           // Current CPU percentage
    float last_cpu_pct;              // Previous reading
    time_t last_check_time;          // Last check timestamp
    unsigned long long last_total_time;  // For calculation
    unsigned long long last_process_time; // For calculation
} cpu_metrics_t;

/**
 * Service Metrics - SIP service statistics
 */
typedef struct {
    int registered_users_count;      // Dynamic registrations
    int directory_entries_count;     // Phonebook entries
    int active_calls_count;          // Active SIP calls
    time_t phonebook_last_updated;   // Last phonebook fetch
    char phonebook_fetch_status[32]; // SUCCESS, FAILED, STALE
    char phonebook_csv_hash[33];     // Current CSV hash (hex)
    int phonebook_entries_loaded;    // Entries in memory
} service_metrics_t;

/**
 * Health Score Components - For calculation
 */
typedef struct {
    bool memory_stable;              // No leak detected
    bool no_recent_crashes;          // No crashes in 24h
    bool sip_service_ok;             // SIP working properly
    bool phonebook_current;          // Phonebook up to date
    bool all_threads_responsive;     // All threads OK
    bool cpu_normal;                 // CPU < 50%
} health_checks_t;

/**
 * Crash Context - Information at crash time
 */
typedef struct {
    int signal_number;               // Signal that caused crash
    char signal_name[32];            // Signal name (SIGSEGV, etc.)
    char description[128];           // Human-readable description
    time_t crash_time;               // When crash occurred
    int thread_id;                   // Which thread crashed
    char last_operation[128];        // What it was doing
    size_t memory_at_crash_bytes;    // Memory usage at crash
    float cpu_at_crash_pct;          // CPU usage at crash
    int active_calls;                // Active calls at crash
    int crash_count_24h;             // Total crashes today
    void* backtrace[HEALTH_BACKTRACE_MAX_DEPTH]; // Stack trace
    int backtrace_size;              // Number of frames
} crash_context_t;

/**
 * Health Report Reasons - Why report was sent
 */
typedef enum {
    REASON_SCHEDULED,                // 4-hour baseline
    REASON_CPU_SPIKE,                // CPU changed >20%
    REASON_MEMORY_INCREASE,          // Memory grew >10MB
    REASON_THREAD_HUNG,              // Thread stopped responding
    REASON_RESTART,                  // Process restarted
    REASON_HEALTH_DEGRADED,          // Health score dropped >15
    REASON_CRASH                     // Crash detected
} health_report_reason_t;

// ============================================================================
// GLOBAL STATE (defined in software_health.c)
// ============================================================================

extern process_health_t g_process_health;
extern thread_health_t g_thread_health[HEALTH_MAX_THREADS];
extern memory_health_t g_memory_health;
extern cpu_metrics_t g_cpu_metrics;
extern service_metrics_t g_service_metrics;
extern health_checks_t g_health_checks;
extern pthread_mutex_t g_health_mutex;

// ============================================================================
// PUBLIC API FUNCTIONS
// ============================================================================

/**
 * Initialize health monitoring system
 * Call once at startup before creating threads
 * @return 0 on success, -1 on error
 */
int software_health_init(void);

/**
 * Shutdown health monitoring system
 * Call before process exit
 */
void software_health_shutdown(void);

/**
 * Register a thread for health monitoring
 * Call from each thread at startup
 * @param tid Thread ID (pthread_self())
 * @param name Thread name for display
 * @return Thread index (0-4), or -1 on error
 */
int health_register_thread(pthread_t tid, const char *name);

/**
 * Update thread heartbeat
 * Call periodically from each thread (every cycle)
 * @param thread_index Index returned by health_register_thread()
 */
void health_update_heartbeat(int thread_index);

/**
 * Check if system is healthy overall
 * @return true if healthy, false if degraded
 */
bool health_is_system_healthy(void);

/**
 * Calculate health score (0-100)
 * Higher is better
 * @return Health score
 */
float health_calculate_score(void);

/**
 * Update all health metrics
 * Call periodically (every 60 seconds)
 * Reads CPU, memory, thread status
 */
void health_update_metrics(void);

/**
 * Write health status to JSON file
 * For local AREDNmon dashboard
 * @param reason Why this report is being generated
 * @return 0 on success, -1 on error
 */
int health_write_status_file(health_report_reason_t reason);

/**
 * Send health status to remote collector
 * HTTP POST to configured collector URL
 * @param reason Why this report is being sent
 * @return 0 on success, -1 on error
 */
int health_send_to_collector(health_report_reason_t reason);

/**
 * Record crash event
 * Call from signal handler
 * @param signal Signal number
 * @param reason Human-readable reason
 */
void health_record_crash(int signal, const char *reason);

/**
 * Load crash state from persistent storage
 * Call at startup to detect previous crash
 * @return true if crash detected, false otherwise
 */
bool health_load_crash_state(void);

/**
 * Get reason string for logging
 * @param reason Report reason enum
 * @return Human-readable string
 */
const char* health_reason_to_string(health_report_reason_t reason);

// ============================================================================
// SUBMODULE HEADERS (implementation in separate .c files)
// ============================================================================

// health_metrics.c - CPU and memory collection
float health_get_cpu_usage(void);
size_t health_get_memory_usage(void);
void health_update_memory_stats(void);

// health_scorer.c - Health score calculation
float health_compute_score(void);
void health_update_checks(void);

// crash_handler.c - Signal handlers and crash detection
void health_setup_crash_handlers(void);
void health_crash_signal_handler(int sig);
int health_save_crash_state(const crash_context_t *ctx);
int health_load_crash_state_from_file(crash_context_t *ctx);

// json_formatter.c - JSON message building
int health_format_agent_health_json(char *buffer, size_t buffer_size,
                                     health_report_reason_t reason);
int health_format_crash_report_json(char *buffer, size_t buffer_size,
                                     const crash_context_t *ctx);

// http_client.c - HTTP POST to collector
int health_http_post_json(const char *url, const char *json_data,
                           int timeout_seconds);

// health_reporter.c - Main reporting thread
void* health_reporter_thread(void *arg);
bool health_should_report_now(health_report_reason_t *reason_out);

#endif // SOFTWARE_HEALTH_H
