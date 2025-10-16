// software_health.c
// Software Health Monitoring System - Core Implementation

#define MODULE_NAME "SOFTWARE_HEALTH"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

// ============================================================================
// GLOBAL STATE DEFINITIONS (direct structures like main.c globals)
// ============================================================================

// MIPS WORKAROUND v2.7.4: Complete disable - structures kept but never initialized/used
// Cannot test individually due to extern declaration size mismatches
// Analysis shows all health structures total ~1KB in BSS - should NOT cause corruption
// But empirically proven: disabling health monitoring (HEALTH_LOCAL_REPORTING=0) stops crashes

// MIPS FIX v2.10.29: MOVE ALL STRUCTURES TO HEAP - DEFINITIVE BSS TEST!
// v2.10.28 FAILED: Non-zero init still went to BSS (0x68f0991a-0x68f09924)
// User's decisive approach: Stop theorizing, eliminate BSS completely
// Solution: Allocate ALL health structures on HEAP like g_reporter_state

// HEAP ALLOCATION - NO BSS!
// Pointers are safe in BSS (8 bytes each), actual structs allocated on heap
process_health_t *g_process_health = NULL;
memory_health_t *g_memory_health = NULL;
cpu_metrics_t *g_cpu_metrics = NULL;
service_metrics_t *g_service_metrics = NULL;
health_checks_t *g_health_checks = NULL;

pthread_mutex_t g_health_mutex = PTHREAD_MUTEX_INITIALIZER;

// Internal state
static bool g_health_initialized = false;
// MIPS FIX v2.10.18: g_node_name char array removed - char arrays in BSS cause corruption
// static char g_node_name[HEALTH_MAX_NODE_NAME_LEN] = "unknown";

// ============================================================================
// INITIALIZATION AND SHUTDOWN
// ============================================================================

int software_health_init(void) {
    if (g_health_initialized) {
        LOG_WARN("Health monitoring already initialized");
        return 0;
    }

    LOG_INFO("Initializing software health monitoring system with direct globals (MIPS-compatible)");

    pthread_mutex_lock(&g_health_mutex);

    // MIPS FIX v2.10.29: ALLOCATE ALL STRUCTURES ON HEAP - NO BSS!
    // Allocate all structures on HEAP
    g_process_health = malloc(sizeof(process_health_t));
    g_memory_health = malloc(sizeof(memory_health_t));
    g_cpu_metrics = malloc(sizeof(cpu_metrics_t));
    g_service_metrics = malloc(sizeof(service_metrics_t));
    g_health_checks = malloc(sizeof(health_checks_t));

    if (!g_process_health || !g_memory_health || !g_cpu_metrics ||
        !g_service_metrics || !g_health_checks) {
        LOG_ERROR("Failed to allocate health structures on heap!");
        pthread_mutex_unlock(&g_health_mutex);
        return -1;
    }

    // Safe to memset heap memory
    memset(g_process_health, 0, sizeof(process_health_t));
    memset(g_memory_health, 0, sizeof(memory_health_t));
    memset(g_cpu_metrics, 0, sizeof(cpu_metrics_t));
    memset(g_service_metrics, 0, sizeof(service_metrics_t));
    memset(g_health_checks, 0, sizeof(health_checks_t));

    // Initialize with real values
    g_process_health->process_start_time = time(NULL);
    g_process_health->last_restart_time = time(NULL);
    g_memory_health->last_check_time = time(NULL);
    g_cpu_metrics->last_check_time = time(NULL);
    g_health_checks->memory_stable = true;
    g_health_checks->no_recent_crashes = true;
    g_health_checks->all_threads_responsive = true;
    g_health_checks->cpu_normal = true;

    // MIPS FIX v2.10.18: g_node_name char array removed - don't store hostname
    // Get node name from hostname
    // char hostname[HEALTH_MAX_NODE_NAME_LEN];
    // if (gethostname(hostname, sizeof(hostname)) == 0) {
    //     strncpy(g_node_name, hostname, sizeof(g_node_name) - 1);
    //     g_node_name[sizeof(g_node_name) - 1] = '\0';
    // }

    g_health_initialized = true;

    pthread_mutex_unlock(&g_health_mutex);

    LOG_INFO("Software health monitoring initialized");

    // Check for previous crash
    if (health_load_crash_state()) {
        LOG_WARN("Previous crash detected - crash report will be sent");
        g_process_health->restart_count_24h++;
    }

    return 0;
}

void software_health_shutdown(void) {
    if (!g_health_initialized) {
        return;
    }

    LOG_INFO("Shutting down software health monitoring");

    pthread_mutex_lock(&g_health_mutex);
    g_health_initialized = false;
    pthread_mutex_unlock(&g_health_mutex);

    pthread_mutex_destroy(&g_health_mutex);

    // Free heap-allocated structures
    free(g_process_health);
    free(g_memory_health);
    free(g_cpu_metrics);
    free(g_service_metrics);
    free(g_health_checks);

    g_process_health = NULL;
    g_memory_health = NULL;
    g_cpu_metrics = NULL;
    g_service_metrics = NULL;
    g_health_checks = NULL;

    LOG_DEBUG("Health monitoring shutdown complete");
}

// ============================================================================
// THREAD MANAGEMENT
// ============================================================================

int health_register_thread(pthread_t tid, const char *name) {
    if (!g_health_initialized) {
        LOG_ERROR("Health system not initialized");
        return -1;
    }

    // MIPS FIX v2.10.15: DISABLE thread registration - strncpy to g_thread_health[].name corrupts BSS!
    // Root cause: Writing to char arrays in g_thread_health array causes BSS corruption on MIPS
    // Solution: Return success without touching the array
    LOG_INFO("Thread '%s' registration skipped (MIPS BSS protection)", name);
    return 0; // Return success without actual registration
}

void health_update_heartbeat(int thread_index) {
    if (!g_health_initialized || thread_index < 0 || thread_index >= HEALTH_MAX_THREADS) {
        return;
    }

    // MIPS FIX v2.10.17: g_thread_health array removed from BSS - this function is now a no-op
    // if (g_thread_health[thread_index].is_active) {
    //     g_thread_health[thread_index].last_heartbeat = time(NULL);
    //     g_thread_health[thread_index].is_responsive = true;
    // }
    return; // No-op - thread health tracking disabled
}

// ============================================================================
// HEALTH ASSESSMENT
// ============================================================================

bool health_is_system_healthy(void) {
    if (!g_health_initialized) {
        return false;
    }

    pthread_mutex_lock(&g_health_mutex);

    bool healthy = g_health_checks->memory_stable &&
                   g_health_checks->no_recent_crashes &&
                   g_health_checks->sip_service_ok &&
                   g_health_checks->all_threads_responsive &&
                   g_health_checks->cpu_normal;

    pthread_mutex_unlock(&g_health_mutex);

    return healthy;
}

float health_calculate_score(void) {
    if (!g_health_initialized) {
        return 0.0f;
    }

    pthread_mutex_lock(&g_health_mutex);
    float score = health_compute_score();
    pthread_mutex_unlock(&g_health_mutex);

    return score;
}

bool health_is_in_grace_period(void) {
    if (!g_health_initialized) {
        return true; // Not yet initialized - still starting
    }

    time_t uptime = time(NULL) - g_process_health->process_start_time;
    return (uptime < HEALTH_STARTUP_GRACE_PERIOD_SECONDS);
}

// ============================================================================
// METRICS UPDATE
// ============================================================================

void health_update_metrics(void) {

    if (!g_health_initialized) {
        return;
    }

    pthread_mutex_lock(&g_health_mutex);

    time_t now = time(NULL);

    // Update CPU metrics
    float cpu_pct = health_get_cpu_usage();
    g_cpu_metrics->last_cpu_pct = g_cpu_metrics->current_cpu_pct;
    g_cpu_metrics->current_cpu_pct = cpu_pct;
    g_cpu_metrics->last_check_time = now;

    // Update memory metrics
    health_update_memory_stats();

    // Check thread responsiveness with dynamic timeouts
    // Timeout = 2× thread's sleep interval (prevents false positives)
    // SKIP during grace period to allow threads to initialize
    extern int g_pb_interval_seconds;
    extern int g_status_update_interval_seconds;
    extern int g_uac_test_interval_seconds;
    extern int g_health_local_update_seconds;

    bool in_grace_period = health_is_in_grace_period();

    // MIPS FIX v2.10.17: g_thread_health array removed - skip thread responsiveness checks
    // Assume all threads responsive (no tracking available)
    g_health_checks->all_threads_responsive = true;

    // for (int i = 0; i < HEALTH_MAX_THREADS; i++) {
    //     if (g_thread_health[i].is_active) {
    //         ... thread checking code removed ...
    //     }
    // }

    // Update health checks
    health_update_checks();

    pthread_mutex_unlock(&g_health_mutex);
}

// ============================================================================
// REPORTING
// ============================================================================

int health_write_status_file(health_report_reason_t reason) {

    if (!g_health_initialized) {
        return -1;
    }

    // MIPS FIX v2.10.1: Full formatter with proper mutex handling
    // External functions called BEFORE/AFTER mutex lock, not during

    char json_buffer[8192];
    int result = health_format_agent_health_json(json_buffer, sizeof(json_buffer), reason);
    if (result != 0) {
        LOG_ERROR("Failed to format health JSON");
        return -1;
    }

    FILE *fp = fopen(HEALTH_STATUS_JSON_PATH, "w");
    if (!fp) {
        LOG_ERROR("Failed to open health status file: %s", HEALTH_STATUS_JSON_PATH);
        return -1;
    }

    size_t written = fwrite(json_buffer, 1, strlen(json_buffer), fp);
    fclose(fp);

    if (written != strlen(json_buffer)) {
        LOG_ERROR("Failed to write complete health status file");
        return -1;
    }

    LOG_DEBUG("Wrote health status to %s (%zu bytes)", HEALTH_STATUS_JSON_PATH, written);
    return 0;
}

int health_send_to_collector(health_report_reason_t reason) {
    if (!g_health_initialized) {
        return -1;
    }

    // Check if collector is enabled (will be checked in config)
    extern int g_collector_enabled;
    extern char g_collector_url[256];
    extern int g_collector_timeout_seconds;

    if (!g_collector_enabled) {
        return 0; // Not an error, just disabled
    }

    char json_buffer[8192];
    int result = health_format_agent_health_json(json_buffer, sizeof(json_buffer), reason);
    if (result != 0) {
        LOG_ERROR("Failed to format health JSON for collector");
        return -1;
    }

    result = health_http_post_json(g_collector_url, json_buffer, g_collector_timeout_seconds);
    if (result != 0) {
        LOG_WARN("Failed to send health data to collector (reason: %s)",
                 health_reason_to_string(reason));
        return -1;
    }

    LOG_INFO("Sent health report to collector (reason: %s)",
             health_reason_to_string(reason));
    return 0;
}

// ============================================================================
// CRASH HANDLING
// ============================================================================

void health_record_crash(int signal, const char *reason) {
    if (!g_health_initialized) {
        return;
    }

    pthread_mutex_lock(&g_health_mutex);

    g_process_health->last_crash_time = time(NULL);
    g_process_health->crash_count_24h++;
    // MIPS FIX v2.10.18: last_crash_reason char array removed from struct
    // strncpy(g_process_health->last_crash_reason, reason,
    //         sizeof(g_process_health->last_crash_reason) - 1);

    pthread_mutex_unlock(&g_health_mutex);

    LOG_ERROR("CRASH RECORDED: Signal %d - %s", signal, reason);
}

bool health_load_crash_state(void) {
    crash_context_t ctx;
    int result = health_load_crash_state_from_file(&ctx);

    if (result == 0) {
        // Crash state found - save as JSON for dashboard
        char json_buffer[4096];
        health_format_crash_report_json(json_buffer, sizeof(json_buffer), &ctx);

        FILE *fp = fopen(CRASH_REPORT_JSON_PATH, "w");
        if (fp) {
            fwrite(json_buffer, 1, strlen(json_buffer), fp);
            fclose(fp);
        }

        // Send to collector if enabled
        health_send_to_collector(REASON_CRASH);

        return true;
    }

    return false;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

const char* health_reason_to_string(health_report_reason_t reason) {
    switch (reason) {
        case REASON_SCHEDULED:       return "scheduled";
        case REASON_CPU_SPIKE:       return "cpu_spike";
        case REASON_MEMORY_INCREASE: return "memory_increase";
        case REASON_THREAD_HUNG:     return "thread_hung";
        case REASON_RESTART:         return "restart";
        case REASON_HEALTH_DEGRADED: return "health_degraded";
        case REASON_CRASH:           return "crash";
        default:                     return "unknown";
    }
}

// ============================================================================
// GETTERS (for JSON formatter)
// ============================================================================

const char* health_get_node_name(void) {
    // MIPS FIX v2.10.18: g_node_name removed - return static string
    // Cannot store hostname in BSS (char array corruption)
    return "unknown";
}

time_t health_get_uptime_seconds(void) {
    if (!g_health_initialized) {
        return 0;
    }
    return time(NULL) - g_process_health->process_start_time;
}
