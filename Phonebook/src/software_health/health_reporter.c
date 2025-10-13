// health_reporter.c
// Health Reporter Thread - Event-Driven Reporting
// Orchestrates health monitoring, local file updates, and remote reporting

#define MODULE_NAME "HEALTH_REPORTER"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include <unistd.h>
#include <math.h>

// ============================================================================
// STATE TRACKING FOR EVENT DETECTION
// ============================================================================

typedef struct {
    float last_cpu_pct;
    float last_mem_mb;
    float last_health_score;
    time_t last_baseline_report;
    time_t last_remote_report;
    bool is_first_report;
} reporter_state_t;

static reporter_state_t g_reporter_state;

// Configuration (external, loaded from config file)
extern int g_health_local_reporting;
extern int g_health_local_update_seconds;
extern int g_collector_enabled;
extern int g_health_report_baseline_hours;
extern float g_health_cpu_threshold_pct;
extern float g_health_memory_threshold_mb;
extern float g_health_score_threshold;

// ============================================================================
// EVENT DETECTION
// ============================================================================

/**
 * Check if we should report now and determine reason
 * @param reason_out Output parameter for report reason
 * @return true if report needed, false otherwise
 */
bool health_should_report_now(health_report_reason_t *reason_out) {
    extern cpu_metrics_t g_cpu_metrics;
    extern memory_health_t g_memory_health;
    extern health_checks_t g_health_checks;
    extern pthread_mutex_t g_health_mutex;

    pthread_mutex_lock(&g_health_mutex);

    time_t now = time(NULL);
    float current_cpu = g_cpu_metrics.current_cpu_pct;
    float current_mem_mb = (float)g_memory_health.current_rss_bytes / (1024.0f * 1024.0f);
    float current_score = health_compute_score();
    bool all_threads_responsive = g_health_checks.all_threads_responsive;

    pthread_mutex_unlock(&g_health_mutex);

    // Check 1: First report after startup
    if (g_reporter_state.is_first_report) {
        *reason_out = REASON_RESTART;
        LOG_INFO("Event trigger: First report after startup");
        return true;
    }

    // Check 2: Baseline heartbeat (4 hours default)
    time_t baseline_interval = g_health_report_baseline_hours * 3600;
    if (now - g_reporter_state.last_baseline_report >= baseline_interval) {
        *reason_out = REASON_SCHEDULED;
        LOG_INFO("Event trigger: Baseline heartbeat (%d hours)",
                 g_health_report_baseline_hours);
        return true;
    }

    // Check 3: CPU spike (>20% change)
    float cpu_delta = fabs(current_cpu - g_reporter_state.last_cpu_pct);
    if (cpu_delta > g_health_cpu_threshold_pct) {
        *reason_out = REASON_CPU_SPIKE;
        LOG_INFO("Event trigger: CPU spike (%.1f%% -> %.1f%%, delta %.1f%%)",
                 g_reporter_state.last_cpu_pct, current_cpu, cpu_delta);
        return true;
    }

    // Check 4: Memory increase (>10 MB)
    float mem_delta = current_mem_mb - g_reporter_state.last_mem_mb;
    if (mem_delta > g_health_memory_threshold_mb) {
        *reason_out = REASON_MEMORY_INCREASE;
        LOG_INFO("Event trigger: Memory increase (%.1f MB -> %.1f MB, delta +%.1f MB)",
                 g_reporter_state.last_mem_mb, current_mem_mb, mem_delta);
        return true;
    }

    // Check 5: Thread became unresponsive
    if (!all_threads_responsive) {
        *reason_out = REASON_THREAD_HUNG;
        LOG_INFO("Event trigger: Thread unresponsive");
        return true;
    }

    // Check 6: Health score dropped significantly (>15 points)
    float score_delta = g_reporter_state.last_health_score - current_score;
    if (score_delta > g_health_score_threshold) {
        *reason_out = REASON_HEALTH_DEGRADED;
        LOG_INFO("Event trigger: Health score dropped (%.0f -> %.0f, delta -%.0f)",
                 g_reporter_state.last_health_score, current_score, score_delta);
        return true;
    }

    // No event triggered
    return false;
}

/**
 * Update reporter state after reporting
 */
static void update_reporter_state(void) {
    extern cpu_metrics_t g_cpu_metrics;
    extern memory_health_t g_memory_health;
    extern pthread_mutex_t g_health_mutex;

    pthread_mutex_lock(&g_health_mutex);

    g_reporter_state.last_cpu_pct = g_cpu_metrics.current_cpu_pct;
    g_reporter_state.last_mem_mb = (float)g_memory_health.current_rss_bytes /
                                    (1024.0f * 1024.0f);
    g_reporter_state.last_health_score = health_compute_score();
    g_reporter_state.last_remote_report = time(NULL);
    g_reporter_state.is_first_report = false;

    pthread_mutex_unlock(&g_health_mutex);
}

// ============================================================================
// HEALTH REPORTER THREAD
// ============================================================================

/**
 * Health reporter thread main function
 * Runs continuously, updating health metrics and reporting
 * @param arg Thread argument (unused)
 * @return NULL
 */
void* health_reporter_thread(void *arg) {
    (void)arg;

    LOG_INFO("Health reporter thread started");

    // Initialize state
    memset(&g_reporter_state, 0, sizeof(g_reporter_state));
    g_reporter_state.is_first_report = true;
    g_reporter_state.last_baseline_report = time(NULL);

    // Register this thread for health monitoring
    int thread_index = health_register_thread(pthread_self(), "health_reporter");
    if (thread_index < 0) {
        LOG_ERROR("Failed to register health reporter thread");
        return NULL;
    }

    while (1) {
        // Update heartbeat
        health_update_heartbeat(thread_index);

        // Update all health metrics
        health_update_metrics();

        // Update service metrics (from global state)
        extern service_metrics_t g_service_metrics;
        extern int num_registered_users;
        extern int num_directory_entries;
        extern CallSession call_sessions[MAX_CALL_SESSIONS];
        extern pthread_mutex_t g_health_mutex;

        pthread_mutex_lock(&g_health_mutex);

        g_service_metrics.registered_users_count = num_registered_users;
        g_service_metrics.directory_entries_count = num_directory_entries;

        // Count active calls
        int active_calls = 0;
        for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
            if (call_sessions[i].in_use) {
                active_calls++;
            }
        }
        g_service_metrics.active_calls_count = active_calls;

        pthread_mutex_unlock(&g_health_mutex);

        // Always write to local file (for AREDNmon dashboard)
        if (g_health_local_reporting) {
            health_report_reason_t local_reason = REASON_SCHEDULED;
            if (health_write_status_file(local_reason) != 0) {
                LOG_ERROR("Failed to write health status file");
            }
        }

        // Check if remote reporting is needed (event-driven)
        if (g_collector_enabled) {
            health_report_reason_t remote_reason;
            if (health_should_report_now(&remote_reason)) {
                // Send to remote collector
                if (health_send_to_collector(remote_reason) == 0) {
                    // Update baseline timestamp if this was a baseline report
                    if (remote_reason == REASON_SCHEDULED) {
                        g_reporter_state.last_baseline_report = time(NULL);
                    }

                    // Update state after successful report
                    update_reporter_state();
                } else {
                    LOG_WARN("Failed to send health report to collector");
                    // Continue anyway - will retry next cycle or next event
                }
            }
        }

        // Sleep for configured interval (default: 60 seconds for local updates)
        sleep(g_health_local_update_seconds);
    }

    return NULL;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize health reporter
 * Call before starting reporter thread
 */
void health_reporter_init(void) {
    memset(&g_reporter_state, 0, sizeof(g_reporter_state));
    g_reporter_state.is_first_report = true;
    g_reporter_state.last_baseline_report = time(NULL);

    LOG_INFO("Health reporter initialized");
}
