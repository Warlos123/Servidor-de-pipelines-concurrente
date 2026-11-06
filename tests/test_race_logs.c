#define _POSIX_C_SOURCE 200809L

#include "logstore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <omp.h>

#define NUM_THREADS 16
#define LINES_PER_THREAD 1000
#define LINE_CONTENT "This is a test line from thread %d, iteration %d\n"

int main(void) {
    job_log_t log;
    if (log_init(&log) != 0) {
        fprintf(stderr, "Failed to initialize log\n");
        return 1;
    }

    printf("Starting race test on logstore with %d threads, each writing %d lines...\n",
           NUM_THREADS, LINES_PER_THREAD);

    #pragma omp parallel num_threads(NUM_THREADS)
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < LINES_PER_THREAD; i++) {
            char line[128];
            snprintf(line, sizeof(line), LINE_CONTENT, tid, i);
            log_append(&log, line);
        }
    }

    log_close(&log);

    printf("Finished writing. Checking results...\n");

    // Check total lines
    int total_lines = 0;
    for (size_t i = 0; i < log.used; i++) {
        if (log.buf[i] == '\n') {
            total_lines++;
        }
    }

    printf("Total lines found: %d (expected: %d)\n", total_lines, NUM_THREADS * LINES_PER_THREAD);

    int status = 0;
    if (total_lines != NUM_THREADS * LINES_PER_THREAD) {
        printf("FAIL: Line count mismatch!\n");
        status = 1;
    } else {
        printf("SUCCESS: Line count matches.\n");
    }

    // A race shows up as torn or interleaved lines: every line ends in '\n',
    // so any lost or cut line changes the count. Comparing against the
    // expected total is enough to detect it.

    log_destroy(&log);
    return status;
}
