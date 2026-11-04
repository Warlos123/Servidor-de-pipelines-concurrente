#define _POSIX_C_SOURCE 200809L

#include "logstore.h"

#include <stdlib.h>
#include <string.h>


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
    pthread_mutex_init(&log->mutex, NULL);
    pthread_cond_init(&log->changed, NULL);
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
    pthread_mutex_lock(&log->mutex);
#endif

    /* Grow buffer if needed */
    while (log->used + total >= log->size) {
        size_t newsize = log->size * 2;
        char  *newbuf  = realloc(log->buf, newsize);
        if (newbuf == NULL) {
            /* Out of memory: drop the line silently rather than crash */
#ifndef SIN_PROTECCION
            pthread_mutex_unlock(&log->mutex);
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

    pthread_cond_broadcast(&log->changed);

#ifndef SIN_PROTECCION
    pthread_mutex_unlock(&log->mutex);
#endif
}

size_t log_read(job_log_t *log, size_t *offset, char *buf, size_t cap, bool *done)
{
    pthread_mutex_lock(&log->mutex);

    //Sleeps (releasing the mutex) until there is new text or the job finished.
    while (log->used <= *offset && !log->closed) {
        pthread_cond_wait(&log->changed, &log->mutex);
    }

    size_t to_copy = 0;

    if (log->used > *offset) {
        to_copy = log->used - *offset;

        if (to_copy > cap) {
            to_copy = cap;
        }

        memcpy(buf, log->buf + *offset, to_copy);
        *offset += to_copy;
    }

    *done = log->closed && *offset >= log->used;

    pthread_mutex_unlock(&log->mutex);
    return to_copy;
}

char *log_copy(job_log_t *log, size_t *len)
{
    pthread_mutex_lock(&log->mutex);

    char *copy = malloc(log->used + 1);

    if (copy != NULL) {
        memcpy(copy, log->buf, log->used);
        copy[log->used] = '\0';
        *len = log->used;
    }

    pthread_mutex_unlock(&log->mutex);
    return copy;
}

void log_close(job_log_t *log)
{
    pthread_mutex_lock(&log->mutex);
    log->closed = true;

    //Wakes the readers so they see the log is finished and stop waiting.
    pthread_cond_broadcast(&log->changed);
    pthread_mutex_unlock(&log->mutex);
}

void log_destroy(job_log_t *log)
{
    if (log->buf == NULL) {
        return;
    }

    pthread_mutex_destroy(&log->mutex);
    pthread_cond_destroy(&log->changed);
    free(log->buf);
    log->buf  = NULL;
    log->size = 0;
    log->used = 0;
}
