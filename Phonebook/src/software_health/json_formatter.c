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
// STACK MONITORING HELPERS
// ============================================================================

/**
 * Get current stack pointer address (for debugging stack usage)
 */
static inline void* get_stack_pointer(void) {
    void *sp;
    #if defined(__x86_64__) || defined(__amd64__)
        __asm__ volatile ("movq %%rsp, %0" : "=r"(sp));
    #elif defined(__i386__)
        __asm__ volatile ("movl %%esp, %0" : "=r"(sp));
    #elif defined(__arm__)
        __asm__ volatile ("mov %0, sp" : "=r"(sp));
    #elif defined(__aarch64__)
        __asm__ volatile ("mov %0, sp" : "=r"(sp));
    #elif defined(__mips__)
        __asm__ volatile ("move %0, $sp" : "=r"(sp));
    #else
        // Fallback: get approximate stack location via local variable address
        sp = &sp;
    #endif
    return sp;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Format timestamp as ISO8601 string
 * @param timestamp Unix timestamp
 * @param buffer Output buffer (min 32 bytes)
 */
static void format_iso8601(time_t timestamp, char *buffer) {
    LOG_DEBUG("[JSON_FMT:001] format_iso8601 start");
    struct tm tm;
    LOG_DEBUG("[JSON_FMT:002] before gmtime_r");
    gmtime_r(&timestamp, &tm);
    LOG_DEBUG("[JSON_FMT:003] after gmtime_r, before strftime");
    strftime(buffer, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
    LOG_DEBUG("[JSON_FMT:004] format_iso8601 complete");
}

/**
 * JSON escape a string
 * Escapes quotes and backslashes
 * @param input Input string
 * @param output Output buffer
 * @param output_size Output buffer size
 */
static void json_escape(const char *input, char *output, size_t output_size) {
    LOG_DEBUG("[JSON_FMT:010] json_escape start, output_size=%zu", output_size);
    size_t j = 0;
    for (size_t i = 0; input[i] && j < output_size - 2; i++) {
        if (input[i] == '"' || input[i] == '\\') {
            output[j++] = '\\';
        }
        output[j++] = input[i];
    }
    LOG_DEBUG("[JSON_FMT:011] json_escape loop complete, j=%zu", j);
    output[j] = '\0';
    LOG_DEBUG("[JSON_FMT:012] json_escape complete");
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
    void *stack_start = get_stack_pointer();
    LOG_INFO("[STACK] health_format_agent_health_json ENTRY: stack_ptr=%p", stack_start);

    LOG_DEBUG("[JSON_FMT:100] health_format_agent_health_json START, buffer_size=%zu, reason=%d", buffer_size, reason);
    LOG_DEBUG("[JSON_FMT:100a] About to declare extern variables");
    extern process_health_t g_process_health;
    LOG_DEBUG("[JSON_FMT:100b] Declared g_process_health");
    extern thread_health_t g_thread_health[HEALTH_MAX_THREADS];
    LOG_DEBUG("[JSON_FMT:100c] Declared g_thread_health array");
    extern memory_health_t g_memory_health;
    LOG_DEBUG("[JSON_FMT:100d] Declared g_memory_health");
    extern cpu_metrics_t g_cpu_metrics;
    LOG_DEBUG("[JSON_FMT:100e] Declared g_cpu_metrics");
    extern service_metrics_t g_service_metrics;
    LOG_DEBUG("[JSON_FMT:100f] Declared g_service_metrics");
    extern health_checks_t g_health_checks;
    LOG_DEBUG("[JSON_FMT:100g] Declared g_health_checks");
    extern pthread_mutex_t g_health_mutex;
    LOG_DEBUG("[JSON_FMT:100h] Declared g_health_mutex");

    LOG_DEBUG("[JSON_FMT:101] before mutex_lock");
    pthread_mutex_lock(&g_health_mutex);
    LOG_DEBUG("[JSON_FMT:102] after mutex_lock");

    // Get node name
    LOG_DEBUG("[JSON_FMT:103] getting node name");
    extern const char* health_get_node_name(void);
    const char *node_name = health_get_node_name();
    LOG_DEBUG("[JSON_FMT:104] node_name=%s", node_name ? node_name : "NULL");

    // Calculate metrics
    LOG_DEBUG("[JSON_FMT:105] calculating metrics");
    float health_score = health_compute_score();
    float mem_mb = (float)g_memory_health.current_rss_bytes / (1024.0f * 1024.0f);
    time_t uptime = time(NULL) - g_process_health.process_start_time;
    time_t now = time(NULL);
    LOG_DEBUG("[JSON_FMT:106] metrics calculated: score=%.0f, mem_mb=%.1f, uptime=%ld", health_score, mem_mb, uptime);

    // Format timestamps - use heap allocation (stack-safe)
    LOG_DEBUG("[JSON_FMT:107] formatting timestamps");
    char *timestamp_str = malloc(32);
    char *phonebook_updated_str = malloc(32);
    if (!timestamp_str || !phonebook_updated_str) {
        LOG_ERROR("Failed to allocate memory for timestamp strings");
        if (timestamp_str) free(timestamp_str);
        if (phonebook_updated_str) free(phonebook_updated_str);
        pthread_mutex_unlock(&g_health_mutex);
        return -1;
    }
    LOG_DEBUG("[JSON_FMT:108] calling format_iso8601 for now=%ld", now);
    format_iso8601(now, timestamp_str);
    LOG_DEBUG("[JSON_FMT:109] calling format_iso8601 for phonebook_last_updated=%ld", g_service_metrics.phonebook_last_updated);
    format_iso8601(g_service_metrics.phonebook_last_updated, phonebook_updated_str);
    LOG_DEBUG("[JSON_FMT:110] timestamps formatted");

    // Start building JSON
    size_t offset = 0;
    LOG_DEBUG("[JSON_FMT:111] starting JSON build");

    // Header
    LOG_DEBUG("[JSON_FMT:112] building header section");
    LOG_DEBUG("[JSON_FMT:112a] Validating arguments: buffer=%p, offset=%zu, buffer_size=%zu",
              (void*)buffer, offset, buffer_size);
    LOG_DEBUG("[JSON_FMT:112b] node_name=%p (%s)", (void*)node_name, node_name ? node_name : "NULL");
    LOG_DEBUG("[JSON_FMT:112c] now=%ld", now);
    LOG_DEBUG("[JSON_FMT:112d] timestamp_str=%p (%s)", (void*)timestamp_str, timestamp_str);
    const char *reason_str = health_reason_to_string(reason);
    LOG_DEBUG("[JSON_FMT:112e] reason_str=%p (%s)", (void*)reason_str, reason_str ? reason_str : "NULL");

    void *stack_before_snprintf = get_stack_pointer();
    LOG_INFO("[STACK] BEFORE snprintf: stack_ptr=%p", stack_before_snprintf);
    LOG_INFO("[STACK] snprintf args: buffer=%p offset=%zu size=%zu", (void*)buffer, offset, buffer_size);
    LOG_INFO("[STACK] snprintf args: node_name=%p", (void*)node_name);
    LOG_INFO("[STACK] snprintf args: now=%ld", now);
    LOG_INFO("[STACK] snprintf args: timestamp_str=%p", (void*)timestamp_str);
    LOG_INFO("[STACK] snprintf args: reason_str=%p", (void*)reason_str);
    LOG_INFO("[STACK] Attempting to read node_name[0]='%c'", node_name ? node_name[0] : '?');
    LOG_INFO("[STACK] Attempting to read timestamp_str[0]='%c'", timestamp_str ? timestamp_str[0] : '?');
    LOG_INFO("[STACK] Attempting to read reason_str[0]='%c'", reason_str ? reason_str[0] : '?');

    LOG_DEBUG("[JSON_FMT:112f] About to call snprintf...");
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

    void *stack_after_snprintf = get_stack_pointer();
    LOG_INFO("[STACK] AFTER snprintf: stack_ptr=%p (used %ld bytes)",
             stack_after_snprintf,
             (long)(stack_before_snprintf - stack_after_snprintf));

    LOG_INFO("DEBUG: health_format_agent_health_json() - first snprintf done, offset=%zu", offset);
    LOG_DEBUG("[JSON_FMT:113] header complete, offset=%zu", offset);

    // Process metrics
    LOG_DEBUG("[JSON_FMT:114] building process metrics");
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
    LOG_DEBUG("[JSON_FMT:115] process metrics complete, offset=%zu", offset);

    // Threads section
    LOG_DEBUG("[JSON_FMT:116] building threads section");
    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"threads\": {\n"
        "    \"all_responsive\": %s",
        g_health_checks.all_threads_responsive ? "true" : "false");
    LOG_DEBUG("[JSON_FMT:117] threads header done, offset=%zu", offset);

    // Individual threads - allocate heartbeat string buffer once (reused in loop)
    char *thread_heartbeat_str = malloc(32);
    if (!thread_heartbeat_str) {
        LOG_ERROR("Failed to allocate memory for thread heartbeat string");
        free(timestamp_str);
        free(phonebook_updated_str);
        pthread_mutex_unlock(&g_health_mutex);
        return -1;
    }

    LOG_DEBUG("[JSON_FMT:118] iterating through %d threads", HEALTH_MAX_THREADS);
    for (int i = 0; i < HEALTH_MAX_THREADS; i++) {
        if (g_thread_health[i].is_active) {
            LOG_DEBUG("[JSON_FMT:119] processing active thread %d: %s", i, g_thread_health[i].name);
            LOG_DEBUG("[JSON_FMT:120] calling format_iso8601 for thread %d heartbeat", i);
            format_iso8601(g_thread_health[i].last_heartbeat, thread_heartbeat_str);
            LOG_DEBUG("[JSON_FMT:121] thread %d heartbeat formatted", i);

            time_t heartbeat_age = now - g_thread_health[i].last_heartbeat;
            LOG_DEBUG("[JSON_FMT:122] thread %d heartbeat_age=%ld", i, heartbeat_age);

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
            LOG_DEBUG("[JSON_FMT:123] thread %d data appended, offset=%zu", i, offset);
        }
    }
    LOG_DEBUG("[JSON_FMT:124] threads loop complete");

    // Free thread heartbeat string buffer
    free(thread_heartbeat_str);

    offset += snprintf(buffer + offset, buffer_size - offset, "\n  },\n");
    LOG_DEBUG("[JSON_FMT:125] threads section closed, offset=%zu", offset);

    // SIP service metrics
    LOG_DEBUG("[JSON_FMT:126] building SIP service metrics");
    offset += snprintf(buffer + offset, buffer_size - offset,
        "  \"sip_service\": {\n"
        "    \"registered_users\": %d,\n"
        "    \"directory_entries\": %d,\n"
        "    \"active_calls\": %d\n"
        "  },\n",
        g_service_metrics.registered_users_count,
        g_service_metrics.directory_entries_count,
        g_service_metrics.active_calls_count);
    LOG_DEBUG("[JSON_FMT:127] SIP service metrics complete, offset=%zu", offset);

    // Phonebook status - use heap allocation (stack-safe)
    LOG_DEBUG("[JSON_FMT:128] building phonebook status");
    char *csv_hash_escaped = malloc(64);
    if (!csv_hash_escaped) {
        LOG_ERROR("Failed to allocate memory for csv_hash_escaped");
        free(timestamp_str);
        free(phonebook_updated_str);
        pthread_mutex_unlock(&g_health_mutex);
        return -1;
    }
    LOG_DEBUG("[JSON_FMT:129] calling json_escape for csv_hash='%s'", g_service_metrics.phonebook_csv_hash);
    json_escape(g_service_metrics.phonebook_csv_hash, csv_hash_escaped, 64);
    LOG_DEBUG("[JSON_FMT:130] json_escape complete, csv_hash_escaped='%s'", csv_hash_escaped);

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
    LOG_DEBUG("[JSON_FMT:131] phonebook status complete, offset=%zu", offset);

    // Health checks
    LOG_DEBUG("[JSON_FMT:132] building health checks");
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
    LOG_DEBUG("[JSON_FMT:133] health checks complete, offset=%zu", offset);

    // Close JSON
    LOG_DEBUG("[JSON_FMT:134] closing JSON");
    offset += snprintf(buffer + offset, buffer_size - offset, "}\n");
    LOG_DEBUG("[JSON_FMT:135] JSON closed, final offset=%zu", offset);

    LOG_DEBUG("[JSON_FMT:136] before mutex_unlock");
    pthread_mutex_unlock(&g_health_mutex);
    LOG_DEBUG("[JSON_FMT:137] after mutex_unlock");

    // Free all heap-allocated strings
    free(timestamp_str);
    free(phonebook_updated_str);
    free(csv_hash_escaped);

    if (offset >= buffer_size - 1) {
        LOG_ERROR("Health JSON buffer overflow (needed %zu, have %zu)", offset, buffer_size);
        return -1;
    }

    LOG_DEBUG("[JSON_FMT:138] health_format_agent_health_json COMPLETE, returning 0");
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
    LOG_DEBUG("[JSON_FMT:200] health_format_crash_report_json START, buffer_size=%zu", buffer_size);
    extern const char* health_get_node_name(void);
    const char *node_name = health_get_node_name();
    LOG_DEBUG("[JSON_FMT:201] got node_name=%s", node_name ? node_name : "NULL");

    time_t now = time(NULL);
    char sent_at_str[32];
    char crash_time_str[32];
    LOG_DEBUG("[JSON_FMT:202] formatting timestamps");
    format_iso8601(now, sent_at_str);
    format_iso8601(ctx->crash_time, crash_time_str);
    LOG_DEBUG("[JSON_FMT:203] timestamps formatted");

    // Calculate memory in MB
    float mem_mb = (float)ctx->memory_at_crash_bytes / (1024.0f * 1024.0f);

    // JSON escape description
    LOG_DEBUG("[JSON_FMT:204] escaping description");
    char description_escaped[256];
    json_escape(ctx->description, description_escaped, sizeof(description_escaped));

    LOG_DEBUG("[JSON_FMT:205] escaping last_operation");
    char last_op_escaped[256];
    json_escape(ctx->last_operation, last_op_escaped, sizeof(last_op_escaped));
    LOG_DEBUG("[JSON_FMT:206] escaping complete");

    // Start building JSON
    size_t offset = 0;
    LOG_DEBUG("[JSON_FMT:207] building crash report JSON");

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
    LOG_DEBUG("[JSON_FMT:208] closing crash report JSON");
    offset += snprintf(buffer + offset, buffer_size - offset, "\n}\n");

    if (offset >= buffer_size - 1) {
        LOG_ERROR("Crash JSON buffer overflow (needed %zu, have %zu)", offset, buffer_size);
        return -1;
    }

    LOG_DEBUG("[JSON_FMT:209] health_format_crash_report_json COMPLETE, returning 0");
    return 0;
}
