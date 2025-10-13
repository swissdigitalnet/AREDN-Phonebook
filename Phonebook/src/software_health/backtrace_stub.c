// backtrace_stub.c
// Stub implementations for backtrace functions when libexecinfo is not available
// Used for systems like OpenWRT that don't have backtrace support

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// Weak symbols - these will be used only if the real functions aren't available
__attribute__((weak))
int backtrace(void **buffer, int size) {
    // Return 0 frames - backtrace not available
    return 0;
}

__attribute__((weak))
char **backtrace_symbols(void *const *buffer, int size) {
    // Return NULL - symbols not available
    return NULL;
}

__attribute__((weak))
void backtrace_symbols_fd(void *const *buffer, int size, int fd) {
    // No-op - not available
    (void)buffer;
    (void)size;
    (void)fd;
}
