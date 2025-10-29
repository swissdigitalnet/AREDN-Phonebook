#include "call_sessions.h"
#include "../common.h" // For logging macros

#define MODULE_NAME "SESSION"

CallSession* find_call_session_by_callid(const char *call_id) {
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use &&
            strcmp(call_sessions[i].call_id, call_id) == 0) {
            return &call_sessions[i];
        }
    }
    return NULL;
}

CallSession* create_call_session() {
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (!call_sessions[i].in_use) {
            call_sessions[i].in_use = 1;
            call_sessions[i].state = CALL_STATE_FREE;
            call_sessions[i].creation_time = time(NULL); // For passive cleanup
            memset(call_sessions[i].call_id, 0, sizeof(call_sessions[i].call_id));
            memset(call_sessions[i].cseq, 0, sizeof(call_sessions[i].cseq));
            memset(call_sessions[i].from_tag, 0, sizeof(call_sessions[i].from_tag));
            memset(call_sessions[i].to_tag, 0, sizeof(call_sessions[i].to_tag));
            memset(&call_sessions[i].caller_addr, 0, sizeof(struct sockaddr_in));
            memset(&call_sessions[i].callee_addr, 0, sizeof(struct sockaddr_in));
            memset(&call_sessions[i].original_caller_addr, 0, sizeof(struct sockaddr_in));
            // Clear call details fields
            memset(call_sessions[i].caller_user_id, 0, sizeof(call_sessions[i].caller_user_id));
            memset(call_sessions[i].caller_display_name, 0, sizeof(call_sessions[i].caller_display_name));
            memset(call_sessions[i].callee_user_id, 0, sizeof(call_sessions[i].callee_user_id));
            memset(call_sessions[i].callee_display_name, 0, sizeof(call_sessions[i].callee_display_name));
            memset(call_sessions[i].codec, 0, sizeof(call_sessions[i].codec));
            memset(call_sessions[i].callee_hostname, 0, sizeof(call_sessions[i].callee_hostname));
            LOG_DEBUG("Call Sessions: Created new call session at index %d.", i);
            return &call_sessions[i];
        }
    }
    LOG_WARN("Call Sessions: Max call sessions reached (%d), cannot create new session.",
                MAX_CALL_SESSIONS);
    return NULL;
}

void terminate_call_session(CallSession *session) {
    if (session && session->in_use) {
        LOG_INFO("Call Sessions: Terminating call session Call-ID: %s", session->call_id);
        session->in_use = 0;
        session->state = CALL_STATE_FREE;
        memset(session->call_id, 0, sizeof(session->call_id));
        memset(session->cseq, 0, sizeof(session->cseq));
        memset(session->from_tag, 0, sizeof(session->from_tag));
        memset(session->to_tag, 0, sizeof(session->to_tag));
        memset(&session->caller_addr, 0, sizeof(struct sockaddr_in));
        memset(&session->callee_addr, 0, sizeof(struct sockaddr_in));
        memset(&session->original_caller_addr, 0, sizeof(struct sockaddr_in));
        // Clear call details fields
        memset(session->caller_user_id, 0, sizeof(session->caller_user_id));
        memset(session->caller_display_name, 0, sizeof(session->caller_display_name));
        memset(session->callee_user_id, 0, sizeof(session->callee_user_id));
        memset(session->callee_display_name, 0, sizeof(session->callee_display_name));
        memset(session->codec, 0, sizeof(session->codec));
        memset(session->callee_hostname, 0, sizeof(session->callee_hostname));
    }
}

void init_call_sessions() {
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        call_sessions[i].in_use = 0;
        call_sessions[i].state = CALL_STATE_FREE;
    }
    LOG_INFO("Initialized call session table (max %d sessions).",
                MAX_CALL_SESSIONS);
}

// Export active calls to JSON file for CGI access
void export_active_calls_json() {
    const char *json_path = "/tmp/active_calls.json";
    FILE *f = fopen(json_path, "w");
    if (!f) {
        LOG_ERROR("Failed to open %s for writing: %s", json_path, strerror(errno));
        return;
    }

    fprintf(f, "{\n  \"calls\": [\n");

    int call_count = 0;
    for (int i = 0; i < MAX_CALL_SESSIONS; i++) {
        if (call_sessions[i].in_use && call_sessions[i].state != CALL_STATE_FREE) {
            if (call_count > 0) {
                fprintf(f, ",\n");
            }

            const char *state_str = "UNKNOWN";
            switch (call_sessions[i].state) {
                case CALL_STATE_INVITE_SENT: state_str = "INVITE_SENT"; break;
                case CALL_STATE_RINGING: state_str = "RINGING"; break;
                case CALL_STATE_ESTABLISHED: state_str = "ESTABLISHED"; break;
                case CALL_STATE_TERMINATING: state_str = "TERMINATING"; break;
                default: state_str = "UNKNOWN"; break;
            }

            fprintf(f, "    {\n");
            fprintf(f, "      \"caller_user_id\": \"%s\",\n", call_sessions[i].caller_user_id);
            fprintf(f, "      \"caller_display_name\": \"%s\",\n", call_sessions[i].caller_display_name);
            fprintf(f, "      \"callee_user_id\": \"%s\",\n", call_sessions[i].callee_user_id);
            fprintf(f, "      \"callee_display_name\": \"%s\",\n", call_sessions[i].callee_display_name);
            fprintf(f, "      \"codec\": \"%s\",\n", call_sessions[i].codec);
            fprintf(f, "      \"callee_hostname\": \"%s\",\n", call_sessions[i].callee_hostname);
            fprintf(f, "      \"state\": \"%s\",\n", state_str);
            fprintf(f, "      \"call_id\": \"%s\"\n", call_sessions[i].call_id);
            fprintf(f, "    }");

            call_count++;
        }
    }

    fprintf(f, "\n  ],\n");
    fprintf(f, "  \"total_active_calls\": %d\n", call_count);
    fprintf(f, "}\n");

    fclose(f);
    LOG_DEBUG("Exported %d active calls to %s", call_count, json_path);
}