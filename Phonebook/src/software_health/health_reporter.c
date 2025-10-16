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

    time_t now = time(NULL);
    float current_cpu = g_cpu_metrics->current_cpu_pct;
    float current_mem_mb = (float)g_memory_health->current_rss_bytes / (1024.0f * 1024.0f);
    float current_score = health_compute_score(); // Use actual health calculation
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

    g_reporter_state->last_cpu_pct = g_cpu_metrics->current_cpu_pct;
    g_reporter_state->last_mem_mb = (float)g_memory_health->current_rss_bytes /
                                    (1024.0f * 1024.0f);
    g_reporter_state->last_health_score = health_compute_score();
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

    // v2.10.45 - FULL IMPLEMENTATION: Enable complete health reporting!
    // v2.10.36-v2.10.44 confirmed ALL individual operations are SAFE on MIPS:
    // ✓ malloc() + memset() + struct field writes (v2.10.36-39)
    // ✓ health_register_thread() (v2.10.40)
    // ✓ health_should_report_now() event detection (v2.10.41)
    // ✓ update_reporter_state() (v2.10.42)
    // ✓ Report reason logging (v2.10.43)
    // ✓ Baseline timestamp updates (v2.10.44)
    //
    // Now enabling full reporter: local file writes + remote HTTP reporting!

    LOG_INFO("Health reporter thread started - v2.10.45 FULL IMPLEMENTATION");

    // Initialize reporter state on heap (MIPS-safe)
    if (!g_reporter_state) {
        g_reporter_state = malloc(sizeof(reporter_state_t));
        if (!g_reporter_state) {
            LOG_ERROR("Failed to allocate reporter state on heap!");
            return NULL;
        }
        memset(g_reporter_state, 0, sizeof(reporter_state_t));
        g_reporter_state->is_first_report = true;
        g_reporter_state->last_baseline_report = time(NULL);
        LOG_INFO("Reporter state initialized on heap");
    }

    // Register thread for health monitoring
    health_register_thread(pthread_self(), "health_reporter");

    // Main event-driven reporting loop
    LOG_INFO("Starting event-driven health monitoring loop...");
    while (1) {
        health_report_reason_t reason;

        // Check if reporting needed
        if (health_should_report_now(&reason)) {
            const char *reason_str = health_reason_to_string(reason);
            LOG_INFO("Health report triggered: %s", reason_str);

            // Write local health status file for AREDNmon dashboard
            if (g_health_local_reporting) {
                if (health_write_status_file(reason) == 0) {
                    LOG_INFO("Local health file updated successfully");
                } else {
                    LOG_WARN("Failed to write local health file");
                }
            }

            // Send to remote collector if configured
            extern char g_collector_url[];
            if (g_collector_url[0] != '\0') {
                if (health_send_to_collector(reason) == 0) {
                    LOG_INFO("Remote health report sent successfully");
                } else {
                    LOG_WARN("Failed to send remote health report");
                }
            }

            // Update reporter state after successful reporting
            update_reporter_state();
            if (reason == REASON_SCHEDULED) {
                g_reporter_state->last_baseline_report = time(NULL);
            }
        }

        sleep(g_health_local_update_seconds);  // Configurable check interval
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
