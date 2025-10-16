// health_metrics.c
// CPU and Memory Metrics Collection
// Reads from /proc/self/stat and /proc/self/status

#define MODULE_NAME "HEALTH_METRICS"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// ============================================================================
// CPU USAGE MEASUREMENT
// ============================================================================

/**
 * Get current CPU usage percentage
 * Reads /proc/self/stat and calculates percentage since last call
 * @return CPU percentage (0.0-100.0), or 0.0 on error
 */
float health_get_cpu_usage(void) {
    FILE *fp = fopen("/proc/self/stat", "r");
    if (!fp) {
        LOG_ERROR("Failed to open /proc/self/stat");
        return 0.0f;
    }

    // Parse /proc/self/stat
    // Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime ...
    // We need fields 14 (utime) and 15 (stime) in clock ticks

    int pid;
    char comm[256];
    char state;
    unsigned long utime, stime;

    // Skip to utime (field 14) and stime (field 15)
    int matched = fscanf(fp, "%d %s %c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
                         &pid, comm, &state, &utime, &stime);
    fclose(fp);

    if (matched != 5) {
        LOG_ERROR("Failed to parse /proc/self/stat (matched %d fields)", matched);
        return 0.0f;
    }

    // Get current wall clock time
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned long long current_time_ms = tv.tv_sec * 1000ULL + tv.tv_usec / 1000;

    // Calculate process time (utime + stime) in milliseconds
    long clock_ticks_per_sec = sysconf(_SC_CLK_TCK);
    unsigned long long process_time_ms = ((utime + stime) * 1000) / clock_ticks_per_sec;

    // Calculate CPU percentage
    // Need previous values for delta calculation
    // NOTE: Caller MUST hold g_health_mutex when calling this function
    extern cpu_metrics_t g_cpu_metrics;

    unsigned long long last_process_time = g_cpu_metrics.last_process_time;
    unsigned long long last_total_time = g_cpu_metrics.last_total_time;

    // Store current values for next calculation
    g_cpu_metrics.last_process_time = process_time_ms;
    g_cpu_metrics.last_total_time = current_time_ms;

    // First call - no previous data
    if (last_total_time == 0) {
        return 0.0f;
    }

    // Calculate deltas
    unsigned long long process_delta = process_time_ms - last_process_time;
    unsigned long long total_delta = current_time_ms - last_total_time;

    if (total_delta == 0) {
        return 0.0f;
    }

    // CPU percentage = (process_time_delta / wall_time_delta) * 100
    float cpu_pct = (float)process_delta / (float)total_delta * 100.0f;

    // Clamp to reasonable range (0-100%)
    if (cpu_pct < 0.0f) cpu_pct = 0.0f;
    if (cpu_pct > 100.0f) cpu_pct = 100.0f;

    return cpu_pct;
}

// ============================================================================
// MEMORY USAGE MEASUREMENT
// ============================================================================

/**
 * Get current memory usage (RSS) in bytes
 * Reads from /proc/self/status
 * @return RSS in bytes, or 0 on error
 */
size_t health_get_memory_usage(void) {
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp) {
        LOG_ERROR("Failed to open /proc/self/status");
        return 0;
    }

    size_t rss_kb = 0;
    char line[256];

    // Find VmRSS line
    // Format: VmRSS:    1234 kB
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%zu", &rss_kb);
            break;
        }
    }

    fclose(fp);

    if (rss_kb == 0) {
        LOG_WARN("Could not read VmRSS from /proc/self/status");
        return 0;
    }

    // Convert to bytes
    return rss_kb * 1024;
}

/**
 * Update memory statistics and detect leaks
 * Call periodically to track memory growth
 * NOTE: Caller MUST hold g_health_mutex when calling this function
 */
void health_update_memory_stats(void) {
    extern memory_health_t g_memory_health;

    size_t current_rss = health_get_memory_usage();
    if (current_rss == 0) {
        return;
    }

    time_t now = time(NULL);
    time_t elapsed = now - g_memory_health.last_check_time;

    if (elapsed < 1) {
        return; // Too soon
    }

    // Update current and peak
    g_memory_health.current_rss_bytes = current_rss;
    if (current_rss > g_memory_health.peak_rss_bytes) {
        g_memory_health.peak_rss_bytes = current_rss;
    }

    // Calculate growth rate (MB per hour)
    if (g_memory_health.initial_rss_bytes > 0) {
        size_t growth_bytes = current_rss - g_memory_health.initial_rss_bytes;
        float growth_mb = (float)growth_bytes / (1024.0f * 1024.0f);
        float elapsed_hours = (float)elapsed / 3600.0f;

        if (elapsed_hours > 0) {
            g_memory_health.growth_rate_mb_per_hour = growth_mb / elapsed_hours;
        }
    }

    // Detect suspected leak
    // If memory grew by >50% from initial, suspect leak
    if (current_rss > (g_memory_health.initial_rss_bytes * 3) / 2) {
        if (!g_memory_health.leak_suspected) {
            LOG_WARN("Memory leak suspected: RSS %.1f MB (started at %.1f MB)",
                     (float)current_rss / (1024.0f * 1024.0f),
                     (float)g_memory_health.initial_rss_bytes / (1024.0f * 1024.0f));
        }
        g_memory_health.leak_suspected = true;
    }

    g_memory_health.last_check_time = now;
}

/**
 * Get memory usage in megabytes (for display)
 * @return Memory usage in MB
 */
float health_get_memory_mb(void) {
    extern memory_health_t g_memory_health;
    extern pthread_mutex_t g_health_mutex;

    pthread_mutex_lock(&g_health_mutex);
    size_t rss = g_memory_health.current_rss_bytes;
    pthread_mutex_unlock(&g_health_mutex);

    return (float)rss / (1024.0f * 1024.0f);
}

/**
 * Get peak memory usage in megabytes
 * @return Peak memory usage in MB
 */
float health_get_peak_memory_mb(void) {
    extern memory_health_t g_memory_health;
    extern pthread_mutex_t g_health_mutex;

    pthread_mutex_lock(&g_health_mutex);
    size_t peak = g_memory_health.peak_rss_bytes;
    pthread_mutex_unlock(&g_health_mutex);

    return (float)peak / (1024.0f * 1024.0f);
}
