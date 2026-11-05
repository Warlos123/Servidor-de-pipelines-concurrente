#ifndef ARTIFACTS_H
#define ARTIFACTS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Per-run artifact store.
 *
 * Artifact names are unique within a run. Only an ancestor job may have
 * published an artifact; this is checked during validation, so execution
 * assumes the artifact exists.
 *
 * Layout: <workdir>/artifacts/<run_id>/<artifact_name>/
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
