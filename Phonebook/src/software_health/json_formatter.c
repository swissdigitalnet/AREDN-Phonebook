// json_formatter.c
// JSON Message Formatting for Health Reporting
// Formats agent_health and crash_report messages per FSD schema

#define MODULE_NAME "JSON_FORMATTER"

#include "software_health.h"
#include "../common.h"
#include "../log_manager/log_manager.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <execinfo.h>  // For backtrace_symbols()

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Format timestamp as ISO8601 string
 * @param timestamp Unix timestamp
 * @param buffer Output buffer (min 32 bytes)
 */
static void format_iso8601(time_t timestamp, char *buffer) {
    struct tm tm;
    gmtime_r(&timestamp, &tm);
    strftime(buffer, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/**
 * JSON escape a string
 * Escapes quotes and backslashes
 * @param input Input string
 * @param output Output buffer
 * @param output_size Output buffer size
 */
static void json_escape(const char *input, char *output, size_t output_size) {
    size_t j = 0;
    for (size_t i = 0; input[i] && j < output_size - 2; i++) {
        if (input[i] == '"' || input[i] == '\\') {
            output[j++] = '\\';
        }
        output[j++] = input[i];
    }
    output[j] = '\0';
}

// ============================================================================
// AGENT HEALTH JSON FORMATTER
// ============================================================================

/**
 * Format agent_health JSON message
 * Follows schema: meshmon.v2 / agent_health
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param reason Why this report is being generated
 * @return 0 on success, -1 on error
 */
int health_format_agent_health_json(char *buffer, size_t buffer_size,
                                     health_report_reason_t reason) {
    extern process_health_t g_process_health;
    extern thread_health_t g_thread_health[HEALTH_MAX_THREADS];
    extern memory_health_t g_memory_health;
    extern cpu_metrics_t g_cpu_metrics;
    extern service_metrics_t g_service_metrics;
    extern health_checks_t g_health_checks;
    extern pthread_mutex_t g_health_mutex;

    pthread_mutex_lock(&g_health_mutex);

    // Get node name
    extern const char* health_get_node_name(void);
    const char *node_name = health_get_node_name();

    // Calculate metrics
    float health_score = health_compute_score();
    float mem_mb = (float)g_memory_health.current_rss_bytes / (1024.0f * 1024.0f);
    time_t uptime = time(NULL) - g_process_health.process_start_time;
    time_t now = time(NULL);

    // Format timestamps
    char timestamp_str[32];
    char phonebook_updated_str[32];
    format_iso8601(now, timestamp_str);
    format_iso8601(g_service_metrics.phonebook_last_updated, phonebook_updated_str);

    // Start building JSON
    size_t offset = 0;

    // Header
    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\n"
        "  \"schema\": \"meshmon.v2\",\n"
        "  \"type\": \"agent_health\",\n"
        "  \"node\": \"%s\",\n"
        "  \"timestamp\": %ld,\n"
        "  \"sent_at\": \"%s\",\n"
        "  \"reporting_reason\": \"%s\",\n",
        node_name,
        now,
        timestamp_str,
        health_reason_to_string(reason));

    // Process metrics
    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"cpu_pct\": %.1f,\n"
        "  \"mem_mb\": %.1f,\n"
        "  \"uptime_seconds\": %ld,\n"
        "  \"restart_count\": %d,\n"
        "  \"health_score\": %.0f,\n",
        g_cpu_metrics.current_cpu_pct,
        mem_mb,
        uptime,
        g_process_health.restart_count_24h,
        health_score);

    // Threads section
    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"threads\": {\n"
        "    \"all_responsive\": %s",
        g_health_checks.all_threads_responsive ? "true" : "false");

    // Individual threads
    for (int i = 0; i < HEALTH_MAX_THREADS; i++) {
        if (g_thread_health[i].is_active) {
            char thread_heartbeat_str[32];
            format_iso8601(g_thread_health[i].last_heartbeat, thread_heartbeat_str);

            time_t heartbeat_age = now - g_thread_health[i].last_heartbeat;

            offset += snprintf(buffer + offset, buffer_size - offset,
                ",\n    \"%s\": {\n"
                "      \"responsive\": %s,\n"
                "      \"last_heartbeat\": \"%s\",\n"
                "      \"heartbeat_age_seconds\": %ld\n"
                "    }",
                g_thread_health[i].name,
                g_thread_health[i].is_responsive ? "true" : "false",
                thread_heartbeat_str,
                heartbeat_age);
        }
    }

    offset += snprintf(buffer + offset, buffer_size - offset, "\n  },\n");

    // SIP service metrics
    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"sip_service\": {\n"
        "    \"registered_users\": %d,\n"
        "    \"directory_entries\": %d,\n"
        "    \"active_calls\": %d\n"
        "  },\n",
        g_service_metrics.registered_users_count,
        g_service_metrics.directory_entries_count,
        g_service_metrics.active_calls_count);

    // Phonebook status
    char csv_hash_escaped[64];
    json_escape(g_service_metrics.phonebook_csv_hash, csv_hash_escaped, sizeof(csv_hash_escaped));

    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"phonebook\": {\n"
        "    \"last_updated\": \"%s\",\n"
        "    \"fetch_status\": \"%s\",\n"
        "    \"csv_hash\": \"%s\",\n"
        "    \"entries_loaded\": %d\n"
        "  },\n",
        phonebook_updated_str,
        g_service_metrics.phonebook_fetch_status,
        csv_hash_escaped,
        g_service_metrics.phonebook_entries_loaded);

    // Health checks
    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"checks\": {\n"
        "    \"memory_stable\": %s,\n"
        "    \"no_recent_crashes\": %s,\n"
        "    \"sip_service_ok\": %s,\n"
        "    \"phonebook_current\": %s,\n"
        "    \"all_threads_responsive\": %s\n"
        "  }\n",
        g_health_checks.memory_stable ? "true" : "false",
        g_health_checks.no_recent_crashes ? "true" : "false",
        g_health_checks.sip_service_ok ? "true" : "false",
        g_health_checks.phonebook_current ? "true" : "false",
        g_health_checks.all_threads_responsive ? "true" : "false");

    // Close JSON
    offset += snprintf(buffer + offset, buffer_size - offset, "}\n");

    pthread_mutex_unlock(&g_health_mutex);

    if (offset >= buffer_size - 1) {
        LOG_ERROR("Health JSON buffer overflow (needed %zu, have %zu)", offset, buffer_size);
        return -1;
    }

    return 0;
}

// ============================================================================
// CRASH REPORT JSON FORMATTER
// ============================================================================

/**
 * Format crash_report JSON message
 * Follows schema: meshmon.v2 / crash_report
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @param ctx Crash context
 * @return 0 on success, -1 on error
 */
int health_format_crash_report_json(char *buffer, size_t buffer_size,
                                     const crash_context_t *ctx) {
    extern const char* health_get_node_name(void);
    const char *node_name = health_get_node_name();

    time_t now = time(NULL);
    char sent_at_str[32];
    char crash_time_str[32];
    format_iso8601(now, sent_at_str);
    format_iso8601(ctx->crash_time, crash_time_str);

    // Calculate memory in MB
    float mem_mb = (float)ctx->memory_at_crash_bytes / (1024.0f * 1024.0f);

    // JSON escape description
    char description_escaped[256];
    json_escape(ctx->description, description_escaped, sizeof(description_escaped));

    char last_op_escaped[256];
    json_escape(ctx->last_operation, last_op_escaped, sizeof(last_op_escaped));

    // Start building JSON
    size_t offset = 0;

    offset += snprintf(buffer + offset, buffer_size - offset,
        "{\n"
        "  \"schema\": \"meshmon.v2\",\n"
        "  \"type\": \"crash_report\",\n"
        "  \"node\": \"%s\",\n"
        "  \"sent_at\": \"%s\",\n"
        "  \"crash_time\": \"%s\",\n"
        "  \"signal\": %d,\n"
        "  \"signal_name\": \"%s\",\n"
        "  \"description\": \"%s\",\n",
        node_name,
        sent_at_str,
        crash_time_str,
        ctx->signal_number,
        ctx->signal_name,
        description_escaped);

    // Context at crash
    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"thread_id\": \"%d\",\n"
        "  \"last_operation\": \"%s\",\n"
        "  \"memory_at_crash_mb\": %.1f,\n"
        "  \"cpu_at_crash_pct\": %.1f,\n"
        "  \"active_calls\": %d,\n"
        "  \"crash_count_24h\": %d",
        ctx->thread_id,
        last_op_escaped,
        mem_mb,
        ctx->cpu_at_crash_pct,
        ctx->active_calls,
        ctx->crash_count_24h);

    // Backtrace (if available)
    if (ctx->backtrace_size > 0) {
        offset += snprintf(buffer + offset, buffer_size - offset, ",\n  \"backtrace\": [");

        char **symbols = backtrace_symbols((void **)ctx->backtrace, ctx->backtrace_size);
        if (symbols) {
            for (int i = 0; i < ctx->backtrace_size && offset < buffer_size - 200; i++) {
                if (i > 0) {
                    offset += snprintf(buffer + offset, buffer_size - offset, ",");
                }

                // Escape backtrace symbol
                char symbol_escaped[512];
                json_escape(symbols[i], symbol_escaped, sizeof(symbol_escaped));

                offset += snprintf(buffer + offset, buffer_size - offset,
                    "\n    \"%s\"", symbol_escaped);
            }
            free(symbols);
        }

        offset += snprintf(buffer + offset, buffer_size - offset, "\n  ]");
    }

    // Close JSON
    offset += snprintf(buffer + offset, buffer_size - offset, "\n}\n");

    if (offset >= buffer_size - 1) {
        LOG_ERROR("Crash JSON buffer overflow (needed %zu, have %zu)", offset, buffer_size);
        return -1;
    }

    return 0;
}
