#ifndef LOGSTORE_H
#define LOGSTORE_H

#include <omp.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * logstore.h — per-job append-only log with blocking-read support.
 *
 * Rules (from Section 6 of the guide):
 *  - One JobLog per job, created before the job starts.
 *  - log_append writes one COMPLETE line per call (never partial); protected
 *    by its own mutex so concurrent writes never interleave.
 *  - log_read is used by the "logs --follow" handler: it returns whatever bytes
 *    are available starting at *offset*, and sets *done* = true once the job
 *    has finished AND all bytes have been consumed.
 *  - log_close marks the log as finished so a waiting reader can stop.
 *  - The buffer grows dynamically (realloc).
 *
 * Implemented by Persona C (src/exec/logstore.c).
 */

typedef struct {
    char   *buf;        /* heap-allocated, grows as needed              */
    size_t  size;       /* allocated bytes                              */
    size_t  used;       /* bytes written so far                         */
    bool    closed;     /* true once the job finishes                   */
    omp_lock_t lock;    /* protects buf / used / closed                 */
} job_log_t;

/* Initialise an empty log.  Returns 0 on success, -1 on OOM. */
int  log_init(job_log_t *log);

/* Append one complete line (may or may not include a trailing '\n').
 * Adds '\n' automatically if missing.
 * Thread-safe.                                                          */
void log_append(job_log_t *log, const char *line);

/*
 * Copy up to `cap` bytes from position `*offset` into `buf`.
 * Updates `*offset` to point just past what was copied.
 * Sets `*done` = true when the log is closed AND offset >= used.
 * Returns the number of bytes copied (0 means no new data yet).
 * Blocks briefly (spin with a short sleep) when waiting for --follow.
 */
size_t log_read(job_log_t *log, size_t *offset, char *buf, size_t cap,
                bool *done);

/* Signal that no more lines will be appended (job finished). */
void log_close(job_log_t *log);

/* Free all memory.  Call after all readers are gone. */
void log_destroy(job_log_t *log);

#endif /* LOGSTORE_H */
