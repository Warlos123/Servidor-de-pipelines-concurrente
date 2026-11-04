#ifndef ARTIFACTS_H
#define ARTIFACTS_H

#include <stddef.h>
#include <stdint.h>

/*
 * artifacts.h — per-run artifact store.
 *
 * Rules (from Section 6 of the guide):
 *  - Artifact names are unique within a run; two `publish` steps with the
 *    same name are rejected at validation time (before execution).
 *  - `publish <path>  artifact: <name>` copies the directory/file at <path>
 *    (relative to the job workspace) into the artifact store.
 *  - `download <name> path: <dest>` copies a previously published artifact
 *    into <dest> (relative to the job workspace).
 *  - Only an ancestor job may have published the artifact; this is enforced
 *    at validation time by Persona A, so at execution time we just trust the
 *    artifact exists.
 *
 * The artifact store is a subdirectory under the server's work directory:
 *   <workdir>/artifacts/<run_id>/<artifact_name>/
 *
 * Implemented by Persona C (src/exec/artifacts.c).
 */

typedef struct {
    char   *base_dir;   /* <workdir>/artifacts/<run_id>  — created on init */
} artifact_store_t;

/*
 * Initialise the store for one run.
 * Creates <workdir>/artifacts/<run_id> if needed.
 * Returns 0 on success, -1 on error.
 */
int artifact_store_init(artifact_store_t *store, const char *workdir,
                        uint64_t run_id);

/*
 * Publish: recursively copy `src_path` (abs or relative to `workspace`) into
 * <store>/<name>/.
 * Returns 0 on success, -1 on error.
 */
int artifact_publish(const artifact_store_t *store, const char *name,
                     const char *src_path, const char *workspace);

/*
 * Download: recursively copy <store>/<name>/ into `dest_path`
 * (abs or relative to `workspace`).
 * Returns 0 on success, -1 on error.
 */
int artifact_download(const artifact_store_t *store, const char *name,
                      const char *dest_path, const char *workspace);

/* Free memory allocated by artifact_store_init. */
void artifact_store_destroy(artifact_store_t *store);

#endif /* ARTIFACTS_H */
