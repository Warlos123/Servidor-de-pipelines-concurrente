#define _POSIX_C_SOURCE 200809L

#include "artifacts.h"

#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------- internal helpers ---------- */

/* Build an absolute path from a base and a relative path.
 * Returns a heap-allocated string or NULL on OOM/overflow. */
static char *abs_path(const char *base, const char *rel)
{
    if (rel[0] == '/') {
        return strdup(rel);
    }
    size_t len = strlen(base) + 1 + strlen(rel) + 1;
    char  *p   = malloc(len);
    if (p == NULL) {
        return NULL;
    }
    snprintf(p, len, "%s/%s", base, rel);
    return p;
}

/* Create a directory and all its parents (like mkdir -p). */
static int mkdir_p(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/*
 * Recursive copy using nftw + a global destination prefix.
 * We use a simple approach: spawn "cp -r src dest" via system().
 * This avoids implementing a full recursive copy in C and is acceptable
 * since artifact operations are not performance-critical.
 */
static int copy_recursive(const char *src, const char *dest)
{
    /* Make sure dest parent exists */
    char *parent = strdup(dest);
    if (parent == NULL) {
        return -1;
    }
    char *slash = strrchr(parent, '/');
    if (slash != NULL) {
        *slash = '\0';
        if (mkdir_p(parent) < 0) {
            free(parent);
            return -1;
        }
    }
    free(parent);

    /* Use cp -r for simplicity; artifact dirs are small */
    size_t cmd_len = strlen(src) + strlen(dest) + 32;
    char  *cmd     = malloc(cmd_len);
    if (cmd == NULL) {
        return -1;
    }
    snprintf(cmd, cmd_len, "cp -r -- '%s' '%s'", src, dest);
    int rc = system(cmd);
    free(cmd);
    return (rc == 0) ? 0 : -1;
}

/* ---------- public API ---------- */

int artifact_store_init(artifact_store_t *store, const char *workdir,
                        uint64_t run_id)
{
    size_t len = strlen(workdir) + 32;
    store->base_dir = malloc(len);
    if (store->base_dir == NULL) {
        return -1;
    }
    snprintf(store->base_dir, len, "%s/artifacts/%llu", workdir,
             (unsigned long long)run_id);
    if (mkdir_p(store->base_dir) < 0) {
        free(store->base_dir);
        store->base_dir = NULL;
        return -1;
    }
    return 0;
}

int artifact_publish(const artifact_store_t *store, const char *name,
                     const char *src_path, const char *workspace)
{
    char *src_abs = abs_path(workspace, src_path);
    if (src_abs == NULL) {
        return -1;
    }

    size_t dst_len = strlen(store->base_dir) + strlen(name) + 2;
    char  *dst     = malloc(dst_len);
    if (dst == NULL) {
        free(src_abs);
        return -1;
    }
    snprintf(dst, dst_len, "%s/%s", store->base_dir, name);

    /* Create the artifact slot directory */
    if (mkdir_p(dst) < 0) {
        free(src_abs);
        free(dst);
        return -1;
    }

    int rc = copy_recursive(src_abs, dst);
    free(src_abs);
    free(dst);
    return rc;
}

int artifact_download(const artifact_store_t *store, const char *name,
                      const char *dest_path, const char *workspace)
{
    size_t src_len = strlen(store->base_dir) + strlen(name) + 2;
    char  *src     = malloc(src_len);
    if (src == NULL) {
        return -1;
    }
    snprintf(src, src_len, "%s/%s", store->base_dir, name);

    char *dst_abs = abs_path(workspace, dest_path);
    if (dst_abs == NULL) {
        free(src);
        return -1;
    }

    if (mkdir_p(dst_abs) < 0) {
        free(src);
        free(dst_abs);
        return -1;
    }

    int rc = copy_recursive(src, dst_abs);
    free(src);
    free(dst_abs);
    return rc;
}

void artifact_store_destroy(artifact_store_t *store)
{
    free(store->base_dir);
    store->base_dir = NULL;
}
