// health_stubs.c
// Stub implementations for health monitoring functions when health monitoring is disabled

#include "../common.h"

// Stub: Register a thread with health monitoring
void health_register_thread(const char* thread_name) {
    // No-op when health monitoring disabled
    (void)thread_name;
}

// Stub: Update thread heartbeat
void health_update_heartbeat(const char* thread_name) {
    // No-op when health monitoring disabled
    (void)thread_name;
}
