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
// execinfo.h not available in musl libc (OpenWrt) - backtrace disabled

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

    // Format timestamps - use heap allocation (stack-safe)
    char *timestamp_str = malloc(32);
    char *phonebook_updated_str = malloc(32);
    if (!timestamp_str || !phonebook_updated_str) {
        LOG_ERROR("Failed to allocate memory for timestamp strings");
        if (timestamp_str) free(timestamp_str);
        if (phonebook_updated_str) free(phonebook_updated_str);
        pthread_mutex_unlock(&g_health_mutex);
        return -1;
    }
    format_iso8601(now, timestamp_str);
    format_iso8601(g_service_metrics.phonebook_last_updated, phonebook_updated_str);

    // Start building JSON
    size_t offset = 0;

    // Header
    const char *reason_str = health_reason_to_string(reason);



    // SOLUTION: Break large snprintf into multiple small calls to reduce stack usage
    // Each snprintf uses less internal working memory
    offset += snprintf(buffer + offset, buffer_size - offset, "{\n");
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"schema\": \"meshmon.v2\",\n");
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"type\": \"agent_health\",\n");
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"node\": \"%s\",\n", node_name);
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"timestamp\": %lld,\n", (long long)now);
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"sent_at\": \"%s\",\n", timestamp_str);
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"reporting_reason\": \"%s\",\n", reason_str);

    // Process metrics - break into smaller calls
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"cpu_pct\": %.1f,\n", g_cpu_metrics.current_cpu_pct);
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"mem_mb\": %.1f,\n", mem_mb);
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"uptime_seconds\": %lld,\n", (long long)uptime);
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"restart_count\": %d,\n", g_process_health.restart_count_24h);
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"health_score\": %.0f,\n", health_score);

    // Threads section - break into smaller calls
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"threads\": {\n");
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"all_responsive\": %s",
                      g_health_checks.all_threads_responsive ? "true" : "false");

    // Individual threads - allocate heartbeat string buffer once (reused in loop)
    char *thread_heartbeat_str = malloc(32);
    if (!thread_heartbeat_str) {
        LOG_ERROR("Failed to allocate memory for thread heartbeat string");
        free(timestamp_str);
        free(phonebook_updated_str);
        pthread_mutex_unlock(&g_health_mutex);
        return -1;
    }

    for (int i = 0; i < HEALTH_MAX_THREADS; i++) {
        if (g_thread_health[i].is_active) {
            format_iso8601(g_thread_health[i].last_heartbeat, thread_heartbeat_str);

            time_t heartbeat_age = now - g_thread_health[i].last_heartbeat;

            // Break thread info into smaller snprintf calls
            offset += snprintf(buffer + offset, buffer_size - offset, ",\n    \"%s\": {\n", g_thread_health[i].name);
            offset += snprintf(buffer + offset, buffer_size - offset, "      \"responsive\": %s,\n",
                              g_thread_health[i].is_responsive ? "true" : "false");
            offset += snprintf(buffer + offset, buffer_size - offset, "      \"last_heartbeat\": \"%s\",\n", thread_heartbeat_str);
            offset += snprintf(buffer + offset, buffer_size - offset, "      \"heartbeat_age_seconds\": %lld\n", (long long)heartbeat_age);
            offset += snprintf(buffer + offset, buffer_size - offset, "    }");
        }
    }

    // Free thread heartbeat string buffer
    free(thread_heartbeat_str);

    offset += snprintf(buffer + offset, buffer_size - offset, "\n  },\n");

    // SIP service metrics - break into smaller calls
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"sip_service\": {\n");
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"registered_users\": %d,\n",
                      g_service_metrics.registered_users_count);
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"directory_entries\": %d,\n",
                      g_service_metrics.directory_entries_count);
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"active_calls\": %d\n",
                      g_service_metrics.active_calls_count);
    offset += snprintf(buffer + offset, buffer_size - offset, "  },\n");

    // Phonebook status - use heap allocation (stack-safe)
    char *csv_hash_escaped = malloc(64);
    if (!csv_hash_escaped) {
        LOG_ERROR("Failed to allocate memory for csv_hash_escaped");
        free(timestamp_str);
        free(phonebook_updated_str);
        pthread_mutex_unlock(&g_health_mutex);
        return -1;
    }
    json_escape(g_service_metrics.phonebook_csv_hash, csv_hash_escaped, 64);

    // Break phonebook section into smaller calls
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"phonebook\": {\n");
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"last_updated\": \"%s\",\n", phonebook_updated_str);
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"fetch_status\": \"%s\",\n",
                      g_service_metrics.phonebook_fetch_status);
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"csv_hash\": \"%s\",\n", csv_hash_escaped);
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"entries_loaded\": %d\n",
                      g_service_metrics.phonebook_entries_loaded);
    offset += snprintf(buffer + offset, buffer_size - offset, "  },\n");

    // Health checks - break into smaller calls
    offset += snprintf(buffer + offset, buffer_size - offset, "  \"checks\": {\n");
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"memory_stable\": %s,\n",
                      g_health_checks.memory_stable ? "true" : "false");
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"no_recent_crashes\": %s,\n",
                      g_health_checks.no_recent_crashes ? "true" : "false");
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"sip_service_ok\": %s,\n",
                      g_health_checks.sip_service_ok ? "true" : "false");
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"phonebook_current\": %s,\n",
                      g_health_checks.phonebook_current ? "true" : "false");
    offset += snprintf(buffer + offset, buffer_size - offset, "    \"all_threads_responsive\": %s\n",
                      g_health_checks.all_threads_responsive ? "true" : "false");
    offset += snprintf(buffer + offset, buffer_size - offset, "  }\n");

    // Close JSON
    offset += snprintf(buffer + offset, buffer_size - offset, "}\n");

    pthread_mutex_unlock(&g_health_mutex);

    // Free all heap-allocated strings
    free(timestamp_str);
    free(phonebook_updated_str);
    free(csv_hash_escaped);

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

    // Backtrace disabled - not available in musl libc
    // (backtrace_size is always 0)

    // Close JSON
    offset += snprintf(buffer + offset, buffer_size - offset, "\n}\n");

    if (offset >= buffer_size - 1) {
        LOG_ERROR("Crash JSON buffer overflow (needed %zu, have %zu)", offset, buffer_size);
        return -1;
    }

    return 0;
}
