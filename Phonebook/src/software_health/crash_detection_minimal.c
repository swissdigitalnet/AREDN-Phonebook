// crash_detection_minimal.c
// Minimal crash detection without full health monitoring
// Provides only crash signal handlers and state persistence

#define MODULE_NAME "CRASH_DETECTION"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include <string.h>
#include <time.h>

// Minimal globals needed for crash detection
process_health_t g_process_health;
memory_health_t g_memory_health;
cpu_metrics_t g_cpu_metrics;
pthread_mutex_t g_health_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Initialize minimal crash detection system
 * Call once at startup before other initialization
 * @return 0 on success, -1 on error
 */
int crash_detection_init(void) {
    LOG_INFO("Initializing crash detection system");

    pthread_mutex_lock(&g_health_mutex);

    // Initialize process health (minimal - just what crash handler needs)
    memset(&g_process_health, 0, sizeof(g_process_health));
    g_process_health.process_start_time = time(NULL);
    g_process_health.last_restart_time = time(NULL);
    g_process_health.restart_count_24h = 0;
    g_process_health.crash_count_24h = 0;

    // Initialize memory and CPU metrics to zero (crash handler reads these)
    memset(&g_memory_health, 0, sizeof(g_memory_health));
    memset(&g_cpu_metrics, 0, sizeof(g_cpu_metrics));

    pthread_mutex_unlock(&g_health_mutex);

    // Check for previous crash
    crash_context_t prev_crash;
    if (health_load_crash_state_from_file(&prev_crash) == 0) {
        LOG_WARN("Previous crash detected: %s at %ld",
                 prev_crash.signal_name, prev_crash.crash_time);
        LOG_WARN("Crash reason: %s", prev_crash.description);
        g_process_health.crash_count_24h++;
        g_process_health.restart_count_24h++;

        // Copy crash info to process health
        strncpy(g_process_health.last_crash_reason, prev_crash.description,
                sizeof(g_process_health.last_crash_reason) - 1);
        g_process_health.last_crash_time = prev_crash.crash_time;
    }

    // Install crash signal handlers
    health_setup_crash_handlers();

    LOG_INFO("Crash detection system initialized");
    return 0;
}

/**
 * Shutdown crash detection system
 * Call before process exit
 */
void crash_detection_shutdown(void) {
    LOG_INFO("Crash detection system shutdown");
    // Nothing to clean up currently
}
