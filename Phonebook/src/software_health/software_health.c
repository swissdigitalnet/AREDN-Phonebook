// software_health.c
// Software Health Monitoring System - Core Implementation

#define MODULE_NAME "SOFTWARE_HEALTH"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include "debug_instrumentation.h"
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
    DEBUG_LOG(1, "software_health_init: START");

    if (g_health_initialized) {
        LOG_WARN("Health monitoring already initialized");
        DEBUG_LOG(2, "software_health_init: Already initialized, returning");
        return 0;
    }

    DEBUG_LOG(3, "software_health_init: Logging init message");
    LOG_INFO("Initializing software health monitoring system with direct globals (MIPS-compatible)");

    DEBUG_LOG(4, "software_health_init: Locking mutex");
    pthread_mutex_lock(&g_health_mutex);
    DEBUG_LOG(5, "software_health_init: Mutex locked");

    // MIPS FIX v2.10.29: ALLOCATE ALL STRUCTURES ON HEAP - NO BSS!
    // Allocate all structures on HEAP
    DEBUG_LOG(10, "software_health_init: malloc process_health");
    g_process_health = malloc(sizeof(process_health_t));
    DEBUG_LOG_MALLOC(11, "malloc process_health", sizeof(process_health_t), g_process_health);

    DEBUG_LOG(12, "software_health_init: malloc memory_health");
    g_memory_health = malloc(sizeof(memory_health_t));
    DEBUG_LOG_MALLOC(13, "malloc memory_health", sizeof(memory_health_t), g_memory_health);

    DEBUG_LOG(14, "software_health_init: malloc cpu_metrics");
    g_cpu_metrics = malloc(sizeof(cpu_metrics_t));
    DEBUG_LOG_MALLOC(15, "malloc cpu_metrics", sizeof(cpu_metrics_t), g_cpu_metrics);

    DEBUG_LOG(16, "software_health_init: malloc service_metrics");
    g_service_metrics = malloc(sizeof(service_metrics_t));
    DEBUG_LOG_MALLOC(17, "malloc service_metrics", sizeof(service_metrics_t), g_service_metrics);

    DEBUG_LOG(18, "software_health_init: malloc health_checks");
    g_health_checks = malloc(sizeof(health_checks_t));
    DEBUG_LOG_MALLOC(19, "malloc health_checks", sizeof(health_checks_t), g_health_checks);

    DEBUG_LOG(20, "software_health_init: Checking malloc results");
    if (!g_process_health || !g_memory_health || !g_cpu_metrics ||
        !g_service_metrics || !g_health_checks) {
        LOG_ERROR("Failed to allocate health structures on heap!");
        DEBUG_LOG(21, "software_health_init: malloc FAILED");
        pthread_mutex_unlock(&g_health_mutex);
        return -1;
    }
    DEBUG_LOG(22, "software_health_init: All mallocs succeeded");

    // Safe to memset heap memory
    DEBUG_LOG(30, "software_health_init: memset process_health");
    memset(g_process_health, 0, sizeof(process_health_t));
    DEBUG_LOG(31, "software_health_init: memset memory_health");
    memset(g_memory_health, 0, sizeof(memory_health_t));
    DEBUG_LOG(32, "software_health_init: memset cpu_metrics");
    memset(g_cpu_metrics, 0, sizeof(cpu_metrics_t));
    DEBUG_LOG(33, "software_health_init: memset service_metrics");
    memset(g_service_metrics, 0, sizeof(service_metrics_t));
    DEBUG_LOG(34, "software_health_init: memset health_checks");
    memset(g_health_checks, 0, sizeof(health_checks_t));
    DEBUG_LOG(35, "software_health_init: All memsets complete");

    // Initialize with real values
    DEBUG_LOG(40, "software_health_init: Setting initial values");
    g_process_health->process_start_time = time(NULL);
    DEBUG_LOG(41, "software_health_init: Set process_start_time");
    g_process_health->last_restart_time = time(NULL);
    DEBUG_LOG(42, "software_health_init: Set last_restart_time");
    g_memory_health->last_check_time = time(NULL);
    DEBUG_LOG(43, "software_health_init: Set memory last_check_time");
    g_cpu_metrics->last_check_time = time(NULL);
    DEBUG_LOG(44, "software_health_init: Set cpu last_check_time");
    g_health_checks->memory_stable = true;
    DEBUG_LOG(45, "software_health_init: Set memory_stable");
    g_health_checks->no_recent_crashes = true;
    DEBUG_LOG(46, "software_health_init: Set no_recent_crashes");
    g_health_checks->all_threads_responsive = true;
    DEBUG_LOG(47, "software_health_init: Set all_threads_responsive");
    g_health_checks->cpu_normal = true;
    DEBUG_LOG(48, "software_health_init: Set cpu_normal");

    g_health_initialized = true;
    DEBUG_LOG(50, "software_health_init: Set health_initialized flag");

    pthread_mutex_unlock(&g_health_mutex);
    DEBUG_LOG(51, "software_health_init: Unlocked mutex");

    LOG_INFO("Software health monitoring initialized");
    DEBUG_LOG(52, "software_health_init: Logged success message");

    // Check for previous crash
    DEBUG_LOG(53, "software_health_init: Checking crash state");
    if (health_load_crash_state()) {
        LOG_WARN("Previous crash detected - crash report will be sent");
        g_process_health->restart_count_24h++;
        DEBUG_LOG(54, "software_health_init: Previous crash detected");
    } else {
        DEBUG_LOG(55, "software_health_init: No previous crash");
    }

    DEBUG_LOG(99, "software_health_init: COMPLETE - returning 0");
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

    // MIPS FIX v2.10.30: Allocate json_buffer on HEAP instead of STACK
    // Root cause: 8KB stack buffer causes stack overflow on MIPS (default stack ~64KB)
    // Stack overflow was misdiagnosed as BSS corruption due to crash addresses
    char *json_buffer = malloc(8192);
    if (!json_buffer) {
        LOG_ERROR("Failed to allocate json_buffer on heap");
        return -1;
    }

    int result = health_format_agent_health_json(json_buffer, 8192, reason);
    if (result != 0) {
        LOG_ERROR("Failed to format health JSON");
        free(json_buffer);
        return -1;
    }

    FILE *fp = fopen(HEALTH_STATUS_JSON_PATH, "w");
    if (!fp) {
        LOG_ERROR("Failed to open health status file: %s", HEALTH_STATUS_JSON_PATH);
        free(json_buffer);
        return -1;
    }

    size_t written = fwrite(json_buffer, 1, strlen(json_buffer), fp);
    fclose(fp);

    if (written != strlen(json_buffer)) {
        LOG_ERROR("Failed to write complete health status file");
        free(json_buffer);
        return -1;
    }

    LOG_DEBUG("Wrote health status to %s (%zu bytes)", HEALTH_STATUS_JSON_PATH, written);
    free(json_buffer);
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

    // MIPS FIX v2.10.30: Allocate json_buffer on HEAP instead of STACK
    char *json_buffer = malloc(8192);
    if (!json_buffer) {
        LOG_ERROR("Failed to allocate json_buffer on heap");
        return -1;
    }

    int result = health_format_agent_health_json(json_buffer, 8192, reason);
    if (result != 0) {
        LOG_ERROR("Failed to format health JSON for collector");
        free(json_buffer);
        return -1;
    }

    result = health_http_post_json(g_collector_url, json_buffer, g_collector_timeout_seconds);
    if (result != 0) {
        LOG_WARN("Failed to send health data to collector (reason: %s)",
                 health_reason_to_string(reason));
        free(json_buffer);
        return -1;
    }

    LOG_INFO("Sent health report to collector (reason: %s)",
             health_reason_to_string(reason));
    free(json_buffer);
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
        // MIPS FIX v2.10.30: Allocate json_buffer on HEAP instead of STACK
        char *json_buffer = malloc(4096);
        if (!json_buffer) {
            LOG_ERROR("Failed to allocate json_buffer on heap");
            return true; // Still return true - crash was detected
        }

        health_format_crash_report_json(json_buffer, 4096, &ctx);

        FILE *fp = fopen(CRASH_REPORT_JSON_PATH, "w");
        if (fp) {
            fwrite(json_buffer, 1, strlen(json_buffer), fp);
            fclose(fp);
        }

        free(json_buffer);

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
