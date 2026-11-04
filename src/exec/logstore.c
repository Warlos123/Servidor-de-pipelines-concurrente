#define _POSIX_C_SOURCE 200809L

#include "logstore.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>   /* nanosleep */

#define LOG_INITIAL_SIZE 4096

int log_init(job_log_t *log)
{
    log->buf = malloc(LOG_INITIAL_SIZE);
    if (log->buf == NULL) {
        return -1;
    }
    log->size   = LOG_INITIAL_SIZE;
    log->used   = 0;
    log->closed = false;
    omp_init_lock(&log->lock);
    return 0;
}

void log_append(job_log_t *log, const char *line)
{
    if (line == NULL) {
        return;
    }

    size_t len = strlen(line);
    /* Ensure trailing newline */
    bool needs_nl = (len == 0 || line[len - 1] != '\n');
    size_t total  = len + (needs_nl ? 1 : 0);

#ifndef SIN_PROTECCION
    omp_set_lock(&log->lock);
#endif

    /* Grow buffer if needed */
    while (log->used + total >= log->size) {
        size_t newsize = log->size * 2;
        char  *newbuf  = realloc(log->buf, newsize);
        if (newbuf == NULL) {
            /* Out of memory: drop the line silently rather than crash */
#ifndef SIN_PROTECCION
            omp_unset_lock(&log->lock);
#endif
            return;
        }
        log->buf  = newbuf;
        log->size = newsize;
    }

    memcpy(log->buf + log->used, line, len);
    log->used += len;
    if (needs_nl) {
        log->buf[log->used] = '\n';
        log->used++;
    }

#ifndef SIN_PROTECCION
    omp_unset_lock(&log->lock);
#endif
}

size_t log_read(job_log_t *log, size_t *offset, char *buf, size_t cap,
                bool *done)
{
    /* Spin-wait with a short sleep when no data is available yet.
     * This is called by the --follow handler; it may loop many times. */
    for (;;) {
        omp_set_lock(&log->lock);
        size_t available = (log->used > *offset) ? (log->used - *offset) : 0;
        bool   is_closed = log->closed;
        omp_unset_lock(&log->lock);

        if (available > 0) {
            size_t to_copy = (available < cap) ? available : cap;
            omp_set_lock(&log->lock);
            memcpy(buf, log->buf + *offset, to_copy);
            omp_unset_lock(&log->lock);
            *offset += to_copy;

            /* Re-check closed after reading */
            omp_set_lock(&log->lock);
            *done = log->closed && (*offset >= log->used);
            omp_unset_lock(&log->lock);

            return to_copy;
        }

        if (is_closed) {
            *done = true;
            return 0;
        }

        /* Wait a bit before polling again */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000L }; /* 10 ms */
        nanosleep(&ts, NULL);
    }
}

void log_close(job_log_t *log)
{
    omp_set_lock(&log->lock);
    log->closed = true;
    omp_unset_lock(&log->lock);
}

void log_destroy(job_log_t *log)
{
    omp_destroy_lock(&log->lock);
    free(log->buf);
    log->buf  = NULL;
    log->size = 0;
    log->used = 0;
}
