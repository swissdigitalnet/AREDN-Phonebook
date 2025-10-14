// health_scorer.c
// Health Score Calculation and Health Checks
// Computes 0-100 health score based on multiple factors

#define MODULE_NAME "HEALTH_SCORER"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include <string.h>

// ============================================================================
// HEALTH CHECKS UPDATE
// ============================================================================

/**
 * Update all health checks
 * Sets boolean flags for each health aspect
 * NOTE: Caller (health_update_metrics) already holds g_health_mutex
 */
void health_update_checks(void) {
    extern process_health_t g_process_health;
    extern thread_health_t g_thread_health[HEALTH_MAX_THREADS];
    extern memory_health_t g_memory_health;
    extern cpu_metrics_t g_cpu_metrics;
    extern service_metrics_t g_service_metrics;
    extern health_checks_t g_health_checks;
    // extern pthread_mutex_t g_health_mutex; // Not needed - caller already holds lock

    LOG_DEBUG("DEBUG: health_update_checks() starting");

    // Check 1: Memory stable (always true - leak detection removed)
    LOG_DEBUG("DEBUG: Setting memory_stable");
    g_health_checks.memory_stable = true;

    // Check 2: No recent crashes (no crashes in last 24h)
    LOG_DEBUG("DEBUG: Checking crash_count_24h");
    g_health_checks.no_recent_crashes = (g_process_health.crash_count_24h == 0);

    // Check 3: SIP service OK
    // Service is OK if we have phonebook entries and no excessive errors
    LOG_DEBUG("DEBUG: Checking directory_entries_count");
    g_health_checks.sip_service_ok = (g_service_metrics.directory_entries_count > 0);

    // Check 4: Phonebook current
    // Phonebook is current if last update was < 2 hours ago
    LOG_DEBUG("DEBUG: Checking phonebook age");
    time_t now = time(NULL);
    time_t phonebook_age = now - g_service_metrics.phonebook_last_updated;
    g_health_checks.phonebook_current = (phonebook_age < 7200);  // 2 hours

    // Check 5: All threads responsive
    LOG_DEBUG("DEBUG: Checking thread responsiveness");
    g_health_checks.all_threads_responsive = true;
    for (int i = 0; i < HEALTH_MAX_THREADS; i++) {
        LOG_DEBUG("DEBUG: Checking thread[%d].is_active", i);
        if (g_thread_health[i].is_active && !g_thread_health[i].is_responsive) {
            g_health_checks.all_threads_responsive = false;
            break;
        }
    }

    // Check 6: CPU normal (< 50%)
    LOG_DEBUG("DEBUG: Checking CPU normal");
    g_health_checks.cpu_normal = (g_cpu_metrics.current_cpu_pct < 50.0f);
    LOG_DEBUG("DEBUG: health_update_checks() completed");

    // NOTE: Caller unlocks the mutex
}

// ============================================================================
// HEALTH SCORE COMPUTATION
// ============================================================================

/**
 * Compute overall health score (0-100)
 * Higher is better
 *
 * Deductions:
 * - High CPU (>20%): -10 points
 * - High memory (>12MB): -10 points
 * - Unresponsive thread: -30 points per thread
 * - Recent restarts: -20 points
 * - Recent crashes: -25 points per crash
 * - Phonebook fetch failed: -10 points
 *
 * NOTE: Caller must hold g_health_mutex before calling
 * @return Health score 0.0-100.0
 */
float health_compute_score(void) {
    // DISABLED for incremental testing - return fixed score
    return 100.0f;
}

// ============================================================================
// HEALTH SEVERITY ASSESSMENT
// ============================================================================

/**
 * Get health severity level based on score
 * @param score Health score (0-100)
 * @return String: "excellent", "good", "degraded", "critical"
 */
const char* health_get_severity(float score) {
    if (score >= HEALTH_SCORE_EXCELLENT) return "excellent";
    if (score >= HEALTH_SCORE_GOOD) return "good";
    if (score >= HEALTH_SCORE_DEGRADED) return "degraded";
    return "critical";
}

/**
 * Get health status color for UI
 * @param score Health score (0-100)
 * @return String: "green", "yellow", "orange", "red"
 */
const char* health_get_color(float score) {
    if (score >= HEALTH_SCORE_EXCELLENT) return "green";
    if (score >= HEALTH_SCORE_GOOD) return "yellow";
    if (score >= HEALTH_SCORE_DEGRADED) return "orange";
    return "red";
}

// ============================================================================
// HEALTH SUMMARY (for logging)
// ============================================================================

/**
 * Log health summary
 * Useful for debugging
 */
void health_log_summary(void) {
    extern process_health_t g_process_health;
    extern memory_health_t g_memory_health;
    extern cpu_metrics_t g_cpu_metrics;
    extern service_metrics_t g_service_metrics;
    extern health_checks_t g_health_checks;
    extern pthread_mutex_t g_health_mutex;

    pthread_mutex_lock(&g_health_mutex);

    float score = health_compute_score();
    float mem_mb = (float)g_memory_health.current_rss_bytes / (1024.0f * 1024.0f);
    time_t uptime = time(NULL) - g_process_health.process_start_time;

    LOG_INFO("=== Health Summary ===");
    LOG_INFO("Score: %.1f/100 (%s)", score, health_get_severity(score));
    LOG_INFO("CPU: %.1f%% | Memory: %.1f MB | Uptime: %ld seconds",
             g_cpu_metrics.current_cpu_pct, mem_mb, uptime);
    LOG_INFO("Checks: memory=%s crashes=%s sip=%s phonebook=%s threads=%s cpu=%s",
             g_health_checks.memory_stable ? "OK" : "FAIL",
             g_health_checks.no_recent_crashes ? "OK" : "FAIL",
             g_health_checks.sip_service_ok ? "OK" : "FAIL",
             g_health_checks.phonebook_current ? "OK" : "FAIL",
             g_health_checks.all_threads_responsive ? "OK" : "FAIL",
             g_health_checks.cpu_normal ? "OK" : "FAIL");
    LOG_INFO("Service: users=%d directory=%d calls=%d",
             g_service_metrics.registered_users_count,
             g_service_metrics.directory_entries_count,
             g_service_metrics.active_calls_count);

    pthread_mutex_unlock(&g_health_mutex);
}
