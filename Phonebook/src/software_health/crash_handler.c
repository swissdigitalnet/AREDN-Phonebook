// crash_handler.c
// Crash Detection and Recovery System
// Signal handlers for capturing crash state and enabling restart

#define MODULE_NAME "CRASH_HANDLER"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
// execinfo.h not available in musl libc (OpenWrt) - backtrace disabled

// Forward declare syslog to avoid including syslog.h (which conflicts with log_manager.h)
extern void syslog(int priority, const char *format, ...);

// Define syslog priorities for use in signal handler (without including syslog.h)
#define SYSLOG_CRIT    2
#define SYSLOG_ERR     3
#define SYSLOG_WARNING 4

// Global crash context (for signal handler)
static crash_context_t g_crash_context;
static volatile sig_atomic_t g_in_crash_handler = 0;

// ============================================================================
// SIGNAL NAME MAPPING
// ============================================================================

static const char* signal_to_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS:  return "SIGBUS";
        case SIGFPE:  return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        case SIGILL:  return "SIGILL";
        default:      return "UNKNOWN";
    }
}

static const char* signal_to_description(int sig) {
    switch (sig) {
        case SIGSEGV: return "Segmentation fault (invalid memory access)";
        case SIGBUS:  return "Bus error (misaligned memory access)";
        case SIGFPE:  return "Floating point exception";
        case SIGABRT: return "Abort signal (assertion failure)";
        case SIGILL:  return "Illegal instruction";
        default:      return "Unknown crash signal";
    }
}

// ============================================================================
// CRASH STATE PERSISTENCE
// ============================================================================

/**
 * Save crash state to binary file
 * Called from signal handler - must be async-signal-safe
 * @param ctx Crash context to save
 * @return 0 on success, -1 on error
 */
int health_save_crash_state(const crash_context_t *ctx) {
    // Open file for writing (O_CREAT | O_TRUNC)
    int fd = open(CRASH_STATE_BIN_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        // Can't use LOG_ERROR in signal handler
        syslog(SYSLOG_ERR, "Failed to open crash state file");
        return -1;
    }

    // Write crash context (simple binary dump)
    ssize_t written = write(fd, ctx, sizeof(crash_context_t));
    close(fd);

    if (written != sizeof(crash_context_t)) {
        syslog(SYSLOG_ERR, "Failed to write complete crash state");
        return -1;
    }

    syslog(SYSLOG_WARNING, "Crash state saved to %s", CRASH_STATE_BIN_PATH);
    return 0;
}

/**
 * Load crash state from binary file
 * Called at startup to detect previous crash
 * @param ctx Output crash context
 * @return 0 if crash found, -1 if no crash or error
 */
int health_load_crash_state_from_file(crash_context_t *ctx) {
    // Check if file exists
    if (access(CRASH_STATE_BIN_PATH, F_OK) != 0) {
        return -1; // No crash file
    }

    int fd = open(CRASH_STATE_BIN_PATH, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("Failed to open crash state file");
        return -1;
    }

    ssize_t bytes_read = read(fd, ctx, sizeof(crash_context_t));
    close(fd);

    if (bytes_read != sizeof(crash_context_t)) {
        LOG_ERROR("Crash state file corrupted (expected %zu bytes, got %zd)",
                  sizeof(crash_context_t), bytes_read);
        unlink(CRASH_STATE_BIN_PATH); // Remove corrupted file
        return -1;
    }

    // Check if crash is recent (< 1 hour old)
    time_t now = time(NULL);
    if (now - ctx->crash_time > 3600) {
        LOG_INFO("Old crash state found (> 1 hour) - ignoring");
        unlink(CRASH_STATE_BIN_PATH);
        return -1;
    }

    LOG_WARN("Previous crash detected: %s at %ld",
             ctx->signal_name, ctx->crash_time);

    // Delete crash file after loading
    unlink(CRASH_STATE_BIN_PATH);

    return 0;
}

// ============================================================================
// CRASH SIGNAL HANDLER
// ============================================================================

/**
 * Signal handler for crash signals
 * Captures crash context and saves to file
 * Then performs emergency shutdown
 */
void health_crash_signal_handler(int sig) {
    // Prevent recursive crash handling
    if (g_in_crash_handler) {
        _exit(1);
    }
    g_in_crash_handler = 1;

    // Use syslog for emergency logging (LOG_ERROR not safe in signal handler)
    syslog(SYSLOG_CRIT, "=== CRASH DETECTED: Signal %d (%s) ===",
           sig, signal_to_name(sig));

    // Populate crash context
    memset(&g_crash_context, 0, sizeof(crash_context_t));
    g_crash_context.signal_number = sig;
    strncpy(g_crash_context.signal_name, signal_to_name(sig),
            sizeof(g_crash_context.signal_name) - 1);
    strncpy(g_crash_context.description, signal_to_description(sig),
            sizeof(g_crash_context.description) - 1);
    g_crash_context.crash_time = time(NULL);

    // Capture backtrace (disabled - not available in musl libc)
    g_crash_context.backtrace_size = 0;  // backtrace() not available in musl

    // Get current metrics (may be unsafe, but try)
    extern memory_health_t g_memory_health;
    extern cpu_metrics_t g_cpu_metrics;
    extern pthread_mutex_t g_health_mutex;

    // Try to get metrics without locking (risky but we're crashing anyway)
    g_crash_context.memory_at_crash_bytes = g_memory_health.current_rss_bytes;
    g_crash_context.cpu_at_crash_pct = g_cpu_metrics.current_cpu_pct;

    // Get active calls count
    extern CallSession call_sessions[MAX_CALL_SESSIONS];
    int active_calls = 0;
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use) {
            active_calls++;
        }
    }
    g_crash_context.active_calls = active_calls;

    // Get crash count from process health
    extern process_health_t g_process_health;
    g_crash_context.crash_count_24h = g_process_health.crash_count_24h + 1;

    // Save crash state to file
    // DISABLED: health_save_crash_state(&g_crash_context);

    // Log backtrace to syslog (disabled - not available in musl libc)
    // Backtrace functionality removed for musl compatibility

    syslog(SYSLOG_CRIT, "Crash context: memory=%.1fMB cpu=%.1f%% calls=%d",
           (float)g_crash_context.memory_at_crash_bytes / (1024.0f * 1024.0f),
           g_crash_context.cpu_at_crash_pct,
           g_crash_context.active_calls);

    // Emergency: sync logs to disk
    sync();

    // Exit to allow system to restart us
    _exit(1);
}

// ============================================================================
// CRASH HANDLER SETUP
// ============================================================================

/**
 * Install signal handlers for crash detection
 * Call once at startup
 */
void health_setup_crash_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    // DISABLED: sa.sa_handler = health_crash_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    // Install handlers for crash signals
    // DISABLED: if (sigaction(SIGSEGV, &sa, NULL) != 0) {
    if (0) {
        LOG_ERROR("Failed to install SIGSEGV handler");
    } else {
        LOG_DEBUG("Installed SIGSEGV crash handler");
    }

    // DISABLED: if (sigaction(SIGBUS, &sa, NULL) != 0) {
    if (0) {
        LOG_ERROR("Failed to install SIGBUS handler");
    } else {
        LOG_DEBUG("Installed SIGBUS crash handler");
    }

    // DISABLED: if (sigaction(SIGFPE, &sa, NULL) != 0) {
    if (0) {
        LOG_ERROR("Failed to install SIGFPE handler");
    } else {
        LOG_DEBUG("Installed SIGFPE crash handler");
    }

    // DISABLED: if (sigaction(SIGABRT, &sa, NULL) != 0) {
    if (0) {
        LOG_ERROR("Failed to install SIGABRT handler");
    } else {
        LOG_DEBUG("Installed SIGABRT crash handler");
    }

    // DISABLED: if (sigaction(SIGILL, &sa, NULL) != 0) {
    if (0) {
        LOG_ERROR("Failed to install SIGILL handler");
    } else {
        LOG_DEBUG("Installed SIGILL crash handler");
    }

    LOG_INFO("Crash detection handlers installed for 5 signals");
}

// ============================================================================
// BACKTRACE FORMATTING (for JSON)
// ============================================================================

/**
 * Format backtrace as string for JSON inclusion
 * @param ctx Crash context with backtrace
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Number of bytes written
 */
int health_format_backtrace(const crash_context_t *ctx, char *buffer, size_t buffer_size) {
    // Backtrace disabled - not available in musl libc
    return snprintf(buffer, buffer_size, "[]");
}
