#ifndef LOGSTORE_H
#define LOGSTORE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char   *buf; //heap-allocated, grows as needed              
    size_t  size; //allocated bytes                              
    size_t  used; //bytes written so far                         
    bool    closed; //true once the job finishes                   
    pthread_mutex_t mutex; //protects buf, used and closed
    pthread_cond_t changed; //signaled when a line is added or the log is closed
} job_log_t;


//Initialise an empty log.  Returns 0 on success, -1 on OOM. 
int  log_init(job_log_t *log);

/* Append one complete line (may or may not include a trailing '\n').
 * Adds '\n' automatically if missing.
 * Thread-safe.                                                          */
void log_append(job_log_t *log, const char *line);

//Returns the number of bytes copied.
//Sleeps on a condition variable until there is new data or the log is closed
size_t log_read(job_log_t *log, size_t *offset, char *buf, size_t cap, bool *done);

char *log_copy(job_log_t *log, size_t *len);


/* Signal that no more lines will be appended (job finished). */
void log_close(job_log_t *log);

/* Free all memory.  Call after all readers are gone. */
void log_destroy(job_log_t *log);

#endif /* LOGSTORE_H */
