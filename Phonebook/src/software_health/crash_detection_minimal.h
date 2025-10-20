// crash_detection_minimal.h
// Minimal crash detection without full health monitoring
// Provides crash signal handlers and state persistence

#ifndef CRASH_DETECTION_MINIMAL_H
#define CRASH_DETECTION_MINIMAL_H

/**
 * Initialize minimal crash detection system
 * Sets up signal handlers for SIGSEGV, SIGBUS, SIGFPE, SIGABRT, SIGILL
 * Checks for previous crash and logs it
 * @return 0 on success, -1 on error
 */
int crash_detection_init(void);

/**
 * Shutdown crash detection system
 * Call before process exit
 */
void crash_detection_shutdown(void);

#endif // CRASH_DETECTION_MINIMAL_H
