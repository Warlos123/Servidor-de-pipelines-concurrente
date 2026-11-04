#ifndef JOB_CONTEXT_H
#define JOB_CONTEXT_H

#include "model.h"
#include "logstore.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * job_context.h — execution context passed to execute_job and step runners.
 *
 * Contains everything a step needs at runtime:
 *   - workspace directory (unique per job, under the server work dir)
 *   - variable map (pipeline globals + ##setvariable outputs from prev steps)
 *   - pointer to the job's log
 *   - pointer to the run's cancellation flag
 *   - run id and global job index (for artifact naming)
 *
 * Implemented by Persona C.  Created inside execute_job before any step runs.
 */

#define VAR_MAP_INITIAL 16   /* initial capacity of the variable map */

typedef struct {
    char  *key;
    char  *value;
} var_entry_t;

typedef struct {
    var_entry_t *entries;
    size_t       count;
    size_t       capacity;
} var_map_t;

typedef struct {
    /* Identity */
    uint64_t  run_id;
    size_t    job_index;      /* index into run->jobs[] */

    /* Filesystem */
    char     *workspace;      /* absolute path, created before first step */

    /* Variables: pipeline-level + ancestor ##setvariable outputs */
    var_map_t vars;

    /* Logging */
    job_log_t *log;           /* one log per job, owned by execute_job      */

    /* Cancellation */
    atomic_bool *cancel_flag; /* points to run->cancel_requested            */

    /* Timeout (seconds, 0 = no limit) */
    int timeout_seconds;
} job_context_t;

/* ---------- var_map helpers (used inside exec layer) ---------- */

/* Look up a variable; returns NULL if not found. */
const char *var_map_get(const var_map_t *m, const char *key);

/* Set or overwrite a variable.  Returns 0 on success, -1 on OOM. */
int var_map_set(var_map_t *m, const char *key, const char *value);

/* Free all entries (does not free the map struct itself). */
void var_map_destroy(var_map_t *m);

#endif /* JOB_CONTEXT_H */
