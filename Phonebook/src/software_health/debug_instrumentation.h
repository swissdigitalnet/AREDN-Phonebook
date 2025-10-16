// debug_instrumentation.h
// Detailed instrumentation for debugging malloc corruption
// Logs heap/stack metrics and sequence numbers

#ifndef DEBUG_INSTRUMENTATION_H
#define DEBUG_INSTRUMENTATION_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include <malloc.h>
#include <pthread.h>

// Get current heap size from /proc/self/statm
static inline long get_heap_size_kb(void) {
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp) return -1;

    long size, resident, shared, text, lib, data, dt;
    if (fscanf(fp, "%ld %ld %ld %ld %ld %ld %ld",
               &size, &resident, &shared, &text, &lib, &data, &dt) == 7) {
        fclose(fp);
        return data * 4; // Convert pages to KB (4KB pages)
    }
    fclose(fp);
    return -1;
}

// Get current stack size estimate
static inline long get_stack_used_kb(void) {
    char stack_var;
    void *stack_start = pthread_self(); // Approximate
    long stack_used = labs((long)&stack_var - (long)stack_start);
    return stack_used / 1024;
}

// Detailed logging macro with heap/stack metrics
#define DEBUG_LOG(seq, desc) do { \
    long heap_kb = get_heap_size_kb(); \
    long stack_kb = get_stack_used_kb(); \
    fprintf(stderr, "[SEQ:%03d] %s | heap=%ldKB stack~=%ldKB\n", \
            seq, desc, heap_kb, stack_kb); \
    fflush(stderr); \
} while(0)

// Log with pointer address
#define DEBUG_LOG_PTR(seq, desc, ptr) do { \
    long heap_kb = get_heap_size_kb(); \
    fprintf(stderr, "[SEQ:%03d] %s ptr=%p | heap=%ldKB\n", \
            seq, desc, (void*)ptr, heap_kb); \
    fflush(stderr); \
} while(0)

// Log malloc call
#define DEBUG_LOG_MALLOC(seq, desc, size, ptr) do { \
    long heap_kb = get_heap_size_kb(); \
    fprintf(stderr, "[SEQ:%03d] %s size=%zu ptr=%p | heap=%ldKB\n", \
            seq, desc, (size_t)size, (void*)ptr, heap_kb); \
    fflush(stderr); \
} while(0)

#endif // DEBUG_INSTRUMENTATION_H
