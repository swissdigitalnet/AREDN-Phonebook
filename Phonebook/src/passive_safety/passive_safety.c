#define MODULE_NAME "PASSIVE_SAFETY"

#include "passive_safety.h"
#include "../common.h"
#include "../call-sessions/call_sessions.h"
#include "../config_loader/config_loader.h"
#include "../file_utils/file_utils.h"
#include "../software_health/software_health.h"
#include <sys/stat.h>

// Thread health tracking
time_t g_fetcher_last_heartbeat = 0;
time_t g_updater_last_heartbeat = 0;
time_t g_bulk_tester_last_heartbeat = 0;
pthread_t g_passive_safety_tid = 0;

// Cooperative shutdown flags for graceful thread restart
volatile sig_atomic_t g_fetcher_restart_requested = 0;
volatile sig_atomic_t g_updater_restart_requested = 0;

// ============================================================================
// WEEK 1: ESSENTIAL PASSIVE SAFETY FEATURES
// ============================================================================

// 1. CALL SESSION CLEANUP - Remove stale call sessions that consume resources
void passive_cleanup_stale_call_sessions(void) {
    time_t now = time(NULL);
    int cleaned_count = 0;

    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use) {
            // Clean up sessions older than 24 hours (safety net for truly lost BYE)
            // With Record-Route enabled, BYE messages should route through proxy
            // This timeout is only a safety net for exceptional cases
            time_t session_age = now - call_sessions[i].creation_time;

            if (session_age > 86400) { // 24 hours = 86400 seconds
                LOG_INFO("Cleaning up stale call session: %s (age: %ld seconds)",
                         call_sessions[i].call_id, session_age);
                terminate_call_session(&call_sessions[i]);
                export_active_calls_json();
                cleaned_count++;
            }
        }
    }

    if (cleaned_count > 0) {
        LOG_INFO("Passive cleanup freed %d stale call sessions", cleaned_count);
    }
}

// 2. GRACEFUL DEGRADATION - Adapt to high load automatically
void enable_graceful_degradation_if_needed(void) {
    static time_t last_check = 0;
    time_t now = time(NULL);

    // Check every 60 seconds
    if (now - last_check < 60) {
        return;
    }
    last_check = now;

    // Count active call sessions
    int active_calls = 0;
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use) {
            active_calls++;
        }
    }

    // If approaching call session limits, reduce background activity
    if (active_calls > (MAX_CALL_SESSIONS * 0.8)) {
        // Double phonebook fetch interval to reduce background load
        if (g_pb_interval_seconds < 7200) { // Don't exceed 2 hours
            g_pb_interval_seconds *= 2;
            LOG_INFO("High call load detected (%d/%d), reducing phonebook fetch frequency to %d seconds",
                     active_calls, MAX_CALL_SESSIONS, g_pb_interval_seconds);
        }
    }
    // Restore normal interval when load decreases
    else if (active_calls < (MAX_CALL_SESSIONS * 0.5)) {
        if (g_pb_interval_seconds > 1800) { // Restore to minimum 30 minutes
            g_pb_interval_seconds = 1800;
            LOG_INFO("Call load normalized (%d/%d), restored phonebook fetch frequency",
                     active_calls, MAX_CALL_SESSIONS);
        }
    }
}

// 3.5. ORPHANED FILE CLEANUP - Remove any leftover backup/temp files
void cleanup_orphaned_phonebook_files(void) {
    char backup_path[512];
    char temp_path[512];

    // Check for orphaned backup and temp files
    snprintf(backup_path, sizeof(backup_path), "%s.backup", PB_XML_PUBLIC_PATH);
    snprintf(temp_path, sizeof(temp_path), "%s.temp", PB_XML_PUBLIC_PATH);

    // Remove orphaned backup file
    if (access(backup_path, F_OK) == 0) {
        if (remove(backup_path) == 0) {
            LOG_INFO("Cleaned up orphaned backup file: %s", backup_path);
        } else {
            LOG_WARN("Failed to remove orphaned backup file: %s", backup_path);
        }
    }

    // Remove orphaned temp file
    if (access(temp_path, F_OK) == 0) {
        if (remove(temp_path) == 0) {
            LOG_INFO("Cleaned up orphaned temp file: %s", temp_path);
        } else {
            LOG_WARN("Failed to remove orphaned temp file: %s", temp_path);
        }
    }
}

// ============================================================================
// WEEK 2: ENHANCED RESILIENCE FEATURES
// ============================================================================

// 4. SMART FILE HANDLING - Never corrupt phonebook data
// Returns 0 on success, 1 on failure
int safe_phonebook_file_operation(const char *source_path, const char *dest_path) {
    char backup_path[512];
    char temp_path[512];
    bool rollback_needed = false;
    struct stat pre_stat, post_stat;
    bool had_existing_file = false;

    // Create backup and temporary file paths
    snprintf(backup_path, sizeof(backup_path), "%s.backup", dest_path);
    snprintf(temp_path, sizeof(temp_path), "%s.temp", dest_path);

    // Step 1: Create backup of current file (if it exists) and record its stats
    if (access(dest_path, F_OK) == 0) {
        had_existing_file = true;
        if (stat(dest_path, &pre_stat) != 0) {
            LOG_ERROR("Failed to stat existing phonebook before backup");
            return 1;
        }
        if (file_utils_copy_file(dest_path, backup_path) != 0) {
            LOG_ERROR("Failed to create backup before phonebook update");
            return 1; // Abort if we can't create backup
        }
    }

    // Step 2: Copy new data to temporary file
    if (file_utils_copy_file(source_path, temp_path) != 0) {
        LOG_ERROR("Failed to create temporary file for phonebook update");
        // Clean up backup file if we created one
        if (access(backup_path, F_OK) == 0) {
            remove(backup_path);
        }
        return 1; // Abort if copy fails
    }

    // Step 3: Verify temporary file integrity (basic check)
    FILE *verify_fp = fopen(temp_path, "r");
    if (!verify_fp) {
        LOG_ERROR("Cannot verify temporary phonebook file integrity");
        remove(temp_path); // Clean up failed temp file
        // Clean up backup file if we created one
        if (access(backup_path, F_OK) == 0) {
            remove(backup_path);
        }
        return 1;
    }

    // Basic verification: file should have some content
    fseek(verify_fp, 0, SEEK_END);
    long file_size = ftell(verify_fp);
    fclose(verify_fp);

    if (file_size < 50) { // Phonebook should be at least 50 bytes
        LOG_ERROR("Phonebook file appears corrupted (size: %ld bytes), aborting update", file_size);
        remove(temp_path);
        // Clean up backup file if we created one
        if (access(backup_path, F_OK) == 0) {
            remove(backup_path);
        }
        return 1;
    }

    // Step 4: Atomic rename (replace destination with verified temp file)
    if (rename(temp_path, dest_path) != 0) {
        LOG_ERROR("Failed to replace phonebook file, attempting rollback");
        rollback_needed = true;
    }

    // Step 5: Verify the rename actually worked by checking mtime/size changed
    if (!rollback_needed) {
        if (stat(dest_path, &post_stat) != 0) {
            LOG_ERROR("Failed to stat phonebook after rename - assuming failure");
            rollback_needed = true;
        } else {
            // If we had an existing file, verify something changed
            if (had_existing_file) {
                if (post_stat.st_mtime == pre_stat.st_mtime && post_stat.st_size == pre_stat.st_size) {
                    LOG_ERROR("Phonebook file unchanged after rename (mtime=%ld, size=%ld) - atomicity violated",
                             (long)post_stat.st_mtime, (long)post_stat.st_size);
                    rollback_needed = true;
                }
            }
            // Verify the new file has expected size
            if (post_stat.st_size != file_size) {
                LOG_ERROR("Phonebook size mismatch after rename: expected %ld, got %ld",
                         file_size, (long)post_stat.st_size);
                rollback_needed = true;
            }
        }
    }

    // Step 6: Rollback if needed
    if (rollback_needed && access(backup_path, F_OK) == 0) {
        if (rename(backup_path, dest_path) == 0) {
            LOG_INFO("Successfully rolled back to previous phonebook version");
        } else {
            LOG_ERROR("Rollback failed - phonebook may be unavailable");
            // Clean up backup file even if rollback failed
            remove(backup_path);
        }
        // Always clean up any remaining temporary files
        remove(temp_path);
        return 1; // Operation failed
    } else if (!rollback_needed) {
        // Success - clean up backup
        remove(backup_path);
        LOG_DEBUG("Phonebook update completed successfully");
        // Always clean up any remaining temporary files
        remove(temp_path);
        return 0; // Success
    } else {
        // Backup doesn't exist but rollback was needed - clean up anyway
        remove(backup_path);
        // Always clean up any remaining temporary files
        remove(temp_path);
        return 1; // Operation failed
    }
}

// 5. THREAD RECOVERY - Cooperative restart of hung threads
void passive_thread_recovery_check(void) {
    time_t now = time(NULL);

    // Check phonebook fetcher thread health
    // Timeout must exceed PB_INTERVAL_SECONDS since fetcher sleeps the full interval
    int fetcher_timeout = g_pb_interval_seconds + 600; // interval + 10 minute grace
    time_t fetcher_hb = heartbeat_load(&g_fetcher_last_heartbeat);
    if (fetcher_hb > 0 && (now - fetcher_hb) > fetcher_timeout) {
        LOG_WARN("Phonebook fetcher thread appears hung (no heartbeat for %ld seconds)",
                 now - fetcher_hb);

        // Request cooperative shutdown instead of pthread_cancel
        // The fetcher thread will check this flag and exit gracefully
        if (!g_fetcher_restart_requested) {
            g_fetcher_restart_requested = 1;
            LOG_INFO("Requested cooperative restart of phonebook fetcher thread");
        } else {
            // If restart was already requested but thread is still hung, it may be truly stuck
            // Log warning but avoid forcing cancellation (could leave mutexes locked)
            LOG_ERROR("Fetcher thread not responding to restart request for %ld seconds - may be deadlocked",
                     now - fetcher_hb);
        }
    }

    // Check status updater thread health
    time_t updater_hb = heartbeat_load(&g_updater_last_heartbeat);
    if (updater_hb > 0 && (now - updater_hb) > 1200) { // 20 minutes
        LOG_WARN("Status updater thread appears hung (no heartbeat for %ld seconds)",
                 now - updater_hb);

        // Request cooperative shutdown instead of pthread_cancel
        if (!g_updater_restart_requested) {
            g_updater_restart_requested = 1;
            LOG_INFO("Requested cooperative restart of status updater thread");
        } else {
            LOG_ERROR("Updater thread not responding to restart request for %ld seconds - may be deadlocked",
                     now - updater_hb);
        }
    }
}

// ============================================================================
// PASSIVE SAFETY BACKGROUND THREAD
// ============================================================================

void *passive_safety_thread(void *arg) {
    (void)arg;
    LOG_INFO("Passive safety thread started - silent self-healing enabled");

    // Register this thread for health monitoring
    int thread_index = health_register_thread(pthread_self(), "passive_safety");
    if (thread_index < 0) {
        LOG_WARN("Failed to register passive safety thread for health monitoring");
        // Continue anyway - health monitoring is not critical for operation
    }

    while (g_keep_running) { // Check shutdown flag for graceful termination
        // Update health heartbeat
        if (thread_index >= 0) {
            health_update_heartbeat(thread_index);
        }

        // Run safety checks every 5 minutes, checking shutdown flag periodically
        for (int i = 0; i < 300 && g_keep_running; i++) {
            sleep(1);
        }

        if (!g_keep_running) {
            break; // Exit immediately on shutdown signal
        }

        // Week 1: Essential safety checks
        passive_cleanup_stale_call_sessions();
        enable_graceful_degradation_if_needed();
        cleanup_orphaned_phonebook_files();

        // Week 2: Enhanced resilience checks (every 15 minutes)
        static int cycle_count = 0;
        if ((++cycle_count % 3) == 0) {
            passive_thread_recovery_check();
        }
    }

    LOG_INFO("Passive safety thread exiting");
    return NULL;
}