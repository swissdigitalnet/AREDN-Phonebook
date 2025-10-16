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

// Declare backtrace functions (provided by backtrace_stub.c or system libc)
int backtrace(void **buffer, int size);
char **backtrace_symbols(void *const *buffer, int size);

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Format timestamp as ISO8601 string
 * @param timestamp Unix timestamp
 * @param buffer Output buffer (min 32 bytes)
 */
static void format_iso8601(time_t timestamp, char *buffer) {
    // v2.10.49: DEBUG TRACE - diagnose silent failure in JSON formatting
    LOG_INFO("DEBUG: format_iso8601() ENTRY - timestamp=%ld, buffer=%p", (long)timestamp, (void*)buffer);

    struct tm tm;
    LOG_INFO("DEBUG: format_iso8601() - calling gmtime_r()");
    gmtime_r(&timestamp, &tm);

    LOG_INFO("DEBUG: format_iso8601() - calling strftime()");
    strftime(buffer, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);

    LOG_INFO("DEBUG: format_iso8601() - COMPLETE, result='%s'", buffer);
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
    // v2.10.49: DEBUG TRACE - comprehensive logging to find silent failure
    LOG_INFO("DEBUG: health_format_agent_health_json() ENTRY - buffer=%p, buffer_size=%zu, reason=%d",
             (void*)buffer, buffer_size, reason);

    extern service_metrics_t *g_service_metrics;
    LOG_INFO("DEBUG: health_format_agent_health_json() - checking g_service_metrics=%p", (void*)g_service_metrics);

    if (!g_service_metrics) {
        LOG_ERROR("DEBUG: health_format_agent_health_json() FAILED - g_service_metrics is NULL");
        return -1;
    }

    // MIPS FIX v2.10.10: EXACTLY like v2.10.0 - ONLY 2 int fields
    // NO time_t reads from BSS, NO format_iso8601() on BSS data
    // Test if format_iso8601(BSS time_t) is the problem

    LOG_INFO("DEBUG: health_format_agent_health_json() - calling health_get_node_name()");
    extern const char* health_get_node_name(void);
    const char *node_name = health_get_node_name();
    LOG_INFO("DEBUG: health_format_agent_health_json() - node_name='%s'", node_name);

    LOG_INFO("DEBUG: health_format_agent_health_json() - calling time(NULL)");
    time_t now = time(NULL);
    LOG_INFO("DEBUG: health_format_agent_health_json() - now=%ld", (long)now);

    LOG_INFO("DEBUG: health_format_agent_health_json() - declaring timestamp_str on stack");
    char timestamp_str[32];
    LOG_INFO("DEBUG: health_format_agent_health_json() - calling format_iso8601()");
    format_iso8601(now, timestamp_str);
    LOG_INFO("DEBUG: health_format_agent_health_json() - format_iso8601() returned, timestamp_str='%s'", timestamp_str);

    // Start building JSON - ONLY 2 int fields from g_service_metrics
    LOG_INFO("DEBUG: health_format_agent_health_json() - initializing offset=0");
    size_t offset = 0;

    // Header
    LOG_INFO("DEBUG: health_format_agent_health_json() - calling health_reason_to_string()");
    const char *reason_str = health_reason_to_string(reason);
    LOG_INFO("DEBUG: health_format_agent_health_json() - reason_str='%s'", reason_str);

    LOG_INFO("DEBUG: health_format_agent_health_json() - first snprintf (header) - node='%s', now=%ld, timestamp='%s', reason='%s'",
             node_name, (long)now, timestamp_str, reason_str);
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
        reason_str);
    LOG_INFO("DEBUG: health_format_agent_health_json() - first snprintf done, offset=%zu", offset);

    // MIPS FIX v2.10.27: Test DATA section - structures now initialized (not BSS)
    // Hypothesis: DATA section might allow direct reads (BSS was toxic)
    // If this works, proves the problem was BSS address range, not global access
    LOG_INFO("DEBUG: health_format_agent_health_json() - reading g_service_metrics fields");
    int reg_users = g_service_metrics->registered_users_count;
    int dir_entries = g_service_metrics->directory_entries_count;
    LOG_INFO("DEBUG: health_format_agent_health_json() - reg_users=%d, dir_entries=%d", reg_users, dir_entries);

    LOG_INFO("DEBUG: health_format_agent_health_json() - second snprintf (data)");
    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"registered_users\": %d,\n"
        "  \"directory_entries\": %d\n",
        reg_users,
        dir_entries);
    LOG_INFO("DEBUG: health_format_agent_health_json() - second snprintf done, offset=%zu", offset);

    // Close JSON
    LOG_INFO("DEBUG: health_format_agent_health_json() - third snprintf (close JSON)");
    offset += snprintf(buffer + offset, buffer_size - offset, "}\n");
    LOG_INFO("DEBUG: health_format_agent_health_json() - third snprintf done, offset=%zu", offset);

    LOG_INFO("DEBUG: health_format_agent_health_json() - checking buffer overflow");
    if (offset >= buffer_size - 1) {
        LOG_ERROR("Health JSON buffer overflow (needed %zu, have %zu)", offset, buffer_size);
        return -1;
    }

    LOG_INFO("DEBUG: health_format_agent_health_json() - SUCCESS, returning 0");
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
