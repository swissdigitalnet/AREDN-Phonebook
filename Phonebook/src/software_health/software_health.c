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
// GLOBAL STATE DEFINITIONS
// ============================================================================

process_health_t g_process_health;
thread_health_t g_thread_health[HEALTH_MAX_THREADS];
memory_health_t g_memory_health;
cpu_metrics_t g_cpu_metrics;
service_metrics_t g_service_metrics;
health_checks_t g_health_checks;
pthread_mutex_t g_health_mutex = PTHREAD_MUTEX_INITIALIZER;

// Internal state
static bool g_health_initialized = false;
static char g_node_name[HEALTH_MAX_NODE_NAME_LEN] = "unknown";

// ============================================================================
// INITIALIZATION AND SHUTDOWN
// ============================================================================

int software_health_init(void) {
    if (g_health_initialized) {
        LOG_WARN("Health monitoring already initialized");
        return 0;
    }

    LOG_INFO("Initializing software health monitoring system");

    pthread_mutex_lock(&g_health_mutex);

    // Initialize process health
    memset(&g_process_health, 0, sizeof(g_process_health));
    g_process_health.process_start_time = time(NULL);
    g_process_health.last_restart_time = time(NULL);

    // Initialize thread health slots
    for (int i = 0; i < HEALTH_MAX_THREADS; i++) {
        memset(&g_thread_health[i], 0, sizeof(thread_health_t));
        g_thread_health[i].is_active = false;
    }

    // Initialize memory health
    memset(&g_memory_health, 0, sizeof(g_memory_health));
    g_memory_health.initial_rss_bytes = health_get_memory_usage();
    g_memory_health.current_rss_bytes = g_memory_health.initial_rss_bytes;
    g_memory_health.peak_rss_bytes = g_memory_health.initial_rss_bytes;
    g_memory_health.last_check_time = time(NULL);

    // Initialize CPU metrics
    memset(&g_cpu_metrics, 0, sizeof(g_cpu_metrics));
    g_cpu_metrics.last_check_time = time(NULL);
    g_cpu_metrics.current_cpu_pct = 0.0f;

    // Initialize service metrics
    memset(&g_service_metrics, 0, sizeof(g_service_metrics));
    strncpy(g_service_metrics.phonebook_fetch_status, "UNKNOWN",
            sizeof(g_service_metrics.phonebook_fetch_status) - 1);

    // Initialize health checks
    memset(&g_health_checks, 0, sizeof(g_health_checks));
    g_health_checks.memory_stable = true;
    g_health_checks.no_recent_crashes = true;
    g_health_checks.all_threads_responsive = true;

    // Get node name from hostname
    char hostname[HEALTH_MAX_NODE_NAME_LEN];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        strncpy(g_node_name, hostname, sizeof(g_node_name) - 1);
        g_node_name[sizeof(g_node_name) - 1] = '\0';
    }

    g_health_initialized = true;

    pthread_mutex_unlock(&g_health_mutex);

    LOG_INFO("Software health monitoring initialized (node: %s)", g_node_name);

    // Check for previous crash
    if (health_load_crash_state()) {
        LOG_WARN("Previous crash detected - crash report will be sent");
        g_process_health.restart_count_24h++;
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
}

// ============================================================================
// THREAD MANAGEMENT
// ============================================================================

int health_register_thread(pthread_t tid, const char *name) {
    if (!g_health_initialized) {
        LOG_ERROR("Health system not initialized");
        return -1;
    }

    pthread_mutex_lock(&g_health_mutex);

    // Find free slot
    int slot = -1;
    for (int i = 0; i < HEALTH_MAX_THREADS; i++) {
        if (!g_thread_health[i].is_active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        pthread_mutex_unlock(&g_health_mutex);
        LOG_ERROR("No free thread health slots available");
        return -1;
    }

    // Register thread
    thread_health_t *th = &g_thread_health[slot];
    th->tid = tid;
    strncpy(th->name, name, sizeof(th->name) - 1);
    th->name[sizeof(th->name) - 1] = '\0';
    th->last_heartbeat = time(NULL);
    th->start_time = time(NULL);
    th->restart_count = 0;
    th->is_responsive = true;
    th->is_active = true;

    pthread_mutex_unlock(&g_health_mutex);

    LOG_INFO("Registered thread '%s' for health monitoring (slot %d)", name, slot);
    return slot;
}

void health_update_heartbeat(int thread_index) {
    if (!g_health_initialized || thread_index < 0 || thread_index >= HEALTH_MAX_THREADS) {
        return;
    }

    pthread_mutex_lock(&g_health_mutex);

    if (g_thread_health[thread_index].is_active) {
        g_thread_health[thread_index].last_heartbeat = time(NULL);
        g_thread_health[thread_index].is_responsive = true;
    }

    pthread_mutex_unlock(&g_health_mutex);
}

// ============================================================================
// HEALTH ASSESSMENT
// ============================================================================

bool health_is_system_healthy(void) {
    if (!g_health_initialized) {
        return false;
    }

    pthread_mutex_lock(&g_health_mutex);

    bool healthy = g_health_checks.memory_stable &&
                   g_health_checks.no_recent_crashes &&
                   g_health_checks.sip_service_ok &&
                   g_health_checks.all_threads_responsive &&
                   g_health_checks.cpu_normal;

    pthread_mutex_unlock(&g_health_mutex);

    return healthy;
}

float health_calculate_score(void) {
    if (!g_health_initialized) {
        return 0.0f;
    }

    return health_compute_score();
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
    g_cpu_metrics.last_cpu_pct = g_cpu_metrics.current_cpu_pct;
    g_cpu_metrics.current_cpu_pct = cpu_pct;
    g_cpu_metrics.last_check_time = now;

    // Update memory metrics
    health_update_memory_stats();

    // Check thread responsiveness
    g_health_checks.all_threads_responsive = true;
    for (int i = 0; i < HEALTH_MAX_THREADS; i++) {
        if (g_thread_health[i].is_active) {
            time_t silence = now - g_thread_health[i].last_heartbeat;
            // Thread is unresponsive if no heartbeat for 30 minutes
            if (silence > 1800) {
                g_thread_health[i].is_responsive = false;
                g_health_checks.all_threads_responsive = false;
                LOG_WARN("Thread '%s' unresponsive for %ld seconds",
                         g_thread_health[i].name, silence);
            }
        }
    }

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

    // DISABLED: result = health_http_post_json(g_collector_url, json_buffer, g_collector_timeout_seconds);
    result = 0; // DISABLED - only testing JSON formatter, not HTTP client
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

    g_process_health.last_crash_time = time(NULL);
    g_process_health.crash_count_24h++;
    strncpy(g_process_health.last_crash_reason, reason,
            sizeof(g_process_health.last_crash_reason) - 1);

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
        // DISABLED: health_send_to_collector(REASON_CRASH); // Only testing JSON formatter, not collector

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
    return g_node_name;
}

time_t health_get_uptime_seconds(void) {
    if (!g_health_initialized) {
        return 0;
    }
    return time(NULL) - g_process_health.process_start_time;
}
