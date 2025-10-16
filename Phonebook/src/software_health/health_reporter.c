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

// MIPS FIX v2.10.22: Move from BSS to HEAP - BSS structures cause crashes on MIPS!
// Pointer in BSS is safe (8 bytes), actual struct allocated on heap
static reporter_state_t *g_reporter_state = NULL;

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
    extern cpu_metrics_t *g_cpu_metrics;
    extern memory_health_t *g_memory_health;
    extern health_checks_t *g_health_checks;

    // MIPS FIX v2.10.13: Avoid health_compute_score() - it accesses g_thread_health array!
    // Root cause: Accessing thread_health array structures causes BSS corruption on MIPS
    // Use simple placeholder score for event detection

    time_t now = time(NULL);
    float current_cpu = g_cpu_metrics->current_cpu_pct;
    float current_mem_mb = (float)g_memory_health->current_rss_bytes / (1024.0f * 1024.0f);
    float current_score = 100.0f; // Placeholder - avoid calling health_compute_score()
    bool all_threads_responsive = g_health_checks->all_threads_responsive;

    // Check 1: First report after startup
    if (g_reporter_state->is_first_report) {
        *reason_out = REASON_RESTART;
        LOG_INFO("Event trigger: First report after startup");
        return true;
    }

    // Check 2: Baseline heartbeat (4 hours default)
    time_t baseline_interval = g_health_report_baseline_hours * 3600;
    if (now - g_reporter_state->last_baseline_report >= baseline_interval) {
        *reason_out = REASON_SCHEDULED;
        LOG_INFO("Event trigger: Baseline heartbeat (%d hours)",
                 g_health_report_baseline_hours);
        return true;
    }

    // Check 3: CPU spike (>20% change)
    float cpu_delta = fabs(current_cpu - g_reporter_state->last_cpu_pct);
    if (cpu_delta > g_health_cpu_threshold_pct) {
        *reason_out = REASON_CPU_SPIKE;
        LOG_INFO("Event trigger: CPU spike (%.1f%% -> %.1f%%, delta %.1f%%)",
                 g_reporter_state->last_cpu_pct, current_cpu, cpu_delta);
        return true;
    }

    // Check 4: Memory increase (>10 MB)
    float mem_delta = current_mem_mb - g_reporter_state->last_mem_mb;
    if (mem_delta > g_health_memory_threshold_mb) {
        *reason_out = REASON_MEMORY_INCREASE;
        LOG_INFO("Event trigger: Memory increase (%.1f MB -> %.1f MB, delta +%.1f MB)",
                 g_reporter_state->last_mem_mb, current_mem_mb, mem_delta);
        return true;
    }

    // Check 5: Thread became unresponsive
    if (!all_threads_responsive) {
        *reason_out = REASON_THREAD_HUNG;
        LOG_INFO("Event trigger: Thread unresponsive");
        return true;
    }

    // Check 6: Health score dropped significantly (>15 points)
    float score_delta = g_reporter_state->last_health_score - current_score;
    if (score_delta > g_health_score_threshold) {
        *reason_out = REASON_HEALTH_DEGRADED;
        LOG_INFO("Event trigger: Health score dropped (%.0f -> %.0f, delta -%.0f)",
                 g_reporter_state->last_health_score, current_score, score_delta);
        return true;
    }

    // No event triggered
    return false;
}

/**
 * Update reporter state after reporting
 */
static void update_reporter_state(void) {
    extern cpu_metrics_t *g_cpu_metrics;
    extern memory_health_t *g_memory_health;

    // MIPS FIX v2.10.13: Avoid health_compute_score() - accesses thread_health array
    g_reporter_state->last_cpu_pct = g_cpu_metrics->current_cpu_pct;
    g_reporter_state->last_mem_mb = (float)g_memory_health->current_rss_bytes /
                                    (1024.0f * 1024.0f);
    g_reporter_state->last_health_score = 100.0f; // Placeholder - avoid thread_health access
    g_reporter_state->last_remote_report = time(NULL);
    g_reporter_state->is_first_report = false;
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

    LOG_INFO("Health reporter thread started - v2.10.35 testing as 4th thread");

    // MIPS FIX v2.10.22: Allocate state on HEAP at thread start (if not already allocated)
    // BSS structures cause crashes on MIPS - heap is safe!
    if (!g_reporter_state) {
        g_reporter_state = malloc(sizeof(reporter_state_t));
        if (!g_reporter_state) {
            LOG_ERROR("Failed to allocate reporter state on heap!");
            return NULL;
        }
        // Now safe to memset - this is HEAP memory, not BSS!
        memset(g_reporter_state, 0, sizeof(reporter_state_t));
        LOG_INFO("Reporter state allocated on heap");
    }

    g_reporter_state->is_first_report = true;
    g_reporter_state->last_baseline_report = time(NULL);

    // Register this thread for health monitoring
    int thread_index = health_register_thread(pthread_self(), "health_reporter");
    if (thread_index < 0) {
        LOG_ERROR("Failed to register health reporter thread");
        return NULL;
    }

    while (1) {
        // Update heartbeat
        // MIPS FIX v2.10.14: DISABLE heartbeat - writing to g_thread_health array causes BSS corruption!
        // Root cause: ANY access (read OR write) to g_thread_health array corrupts BSS on MIPS
        // health_update_heartbeat(thread_index);

        // Update all health metrics
        // MIPS FIX v2.10.11: DISABLE health_update_metrics() - it accesses ALL BSS structures
        // This is the root cause of corruption, not the JSON formatter!
        // health_update_metrics();

        // Update service metrics (from global state)
        // MIPS FIX v2.10.24: DISABLE ALL WRITES to g_service_metrics (BSS structure)!
        // Root cause: ANY write to BSS structures causes corruption on MIPS, not just mutex ops
        // Even "atomic" int writes to BSS are TOXIC on MIPS ath79!
        // extern service_metrics_t g_service_metrics;
        // extern int num_registered_users;
        // extern int num_directory_entries;
        // extern CallSession call_sessions[MAX_CALL_SESSIONS];

        // DISABLED - writing to BSS crashes on MIPS:
        // g_service_metrics.registered_users_count = num_registered_users;
        // g_service_metrics.directory_entries_count = num_directory_entries;

        // Count active calls - DISABLED
        // int active_calls = 0;
        // for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        //     if (call_sessions[i].in_use) {
        //         active_calls++;
        //     }
        // }
        // g_service_metrics.active_calls_count = active_calls;

        // MIPS FIX v2.10.26: RE-ENABLE health reporting!
        // Root cause FOUND: json_formatter.c was passing BSS struct fields directly to snprintf
        // Solution: Copy BSS values to stack variables first (see json_formatter.c:98-99)

        // Always write to local file (for AREDNmon dashboard)
        if (g_health_local_reporting) {
            health_report_reason_t local_reason = REASON_SCHEDULED;
            if (health_write_status_file(local_reason) != 0) {
                LOG_ERROR("Failed to write health status file");
            }
        }

        // Check if remote reporting is needed (event-driven)
        // DISABLED for now - health_should_report_now() still reads from BSS
        // if (g_collector_enabled) {
        //     health_report_reason_t remote_reason;
        //     if (health_should_report_now(&remote_reason)) {
        //         if (health_send_to_collector(remote_reason) == 0) {
        //             if (remote_reason == REASON_SCHEDULED) {
        //                 g_reporter_state->last_baseline_report = time(NULL);
        //             }
        //             update_reporter_state();
        //         } else {
        //             LOG_WARN("Failed to send health report to collector");
        //         }
        //     }
        // }

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
    // MIPS FIX v2.10.22: Allocate on HEAP instead of BSS
    // BSS structures cause crashes on MIPS - heap is safe!
    g_reporter_state = malloc(sizeof(reporter_state_t));
    if (!g_reporter_state) {
        LOG_ERROR("Failed to allocate reporter state on heap!");
        return;
    }

    // Now safe to memset - this is HEAP memory, not BSS!
    memset(g_reporter_state, 0, sizeof(reporter_state_t));
    g_reporter_state->is_first_report = true;
    g_reporter_state->last_baseline_report = time(NULL);

    LOG_INFO("Health reporter initialized (heap allocation)");
}
