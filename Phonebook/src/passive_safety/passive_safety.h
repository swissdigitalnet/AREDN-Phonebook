#ifndef PASSIVE_SAFETY_H
#define PASSIVE_SAFETY_H

#include "../common.h"

// Passive safety functions - self-healing without reporting
// Designed for static phonebook environment (no dynamic user changes)

// Week 1: Essential cleanup and self-correction
void passive_cleanup_stale_call_sessions(void);
void enable_graceful_degradation_if_needed(void);
void cleanup_orphaned_phonebook_files(void);

// Week 2: File protection and thread recovery
// Returns 0 on success, 1 on failure
int safe_phonebook_file_operation(const char *source_path, const char *dest_path);
void passive_thread_recovery_check(void);

// Background safety thread
void *passive_safety_thread(void *arg);

// Thread health tracking (minimal) - use atomics for thread-safe access
extern time_t g_fetcher_last_heartbeat;
extern time_t g_updater_last_heartbeat;
extern time_t g_bulk_tester_last_heartbeat;

// Atomic helpers for heartbeat access (lock-free, no mutex needed)
static inline void heartbeat_store(time_t *hb, time_t val) {
    __atomic_store_n(hb, val, __ATOMIC_RELAXED);
}
static inline time_t heartbeat_load(const time_t *hb) {
    return __atomic_load_n(hb, __ATOMIC_RELAXED);
}
extern pthread_t g_passive_safety_tid;

// Cooperative shutdown flags for graceful thread restart
extern volatile sig_atomic_t g_fetcher_restart_requested;
extern volatile sig_atomic_t g_updater_restart_requested;

#endif // PASSIVE_SAFETY_H