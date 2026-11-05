#define _POSIX_C_SOURCE 200809L

#include "builtin_tasks.h"
#include "job_context.h"
#include "logstore.h"

#include <ctype.h>
#include <limits.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
 * WordCount@1
 * Inputs: file, threads, output, top (optional)
 *
 * Counts:
 *   - Total words
 *   - Distinct words
 *   - Top-N most frequent (with alphabetical tie-break)
 *
 * "Word" = maximal sequence of ASCII letters/digits (case-insensitive).
 *
 * Parallelism: each thread processes a chunk of the file, then results
 * are merged in declaration order → deterministic.
 * ========================================================= */

#define MAX_WORD_LEN 256
#define HASH_SIZE    65536  /* power of 2, used for the per-thread word table */

typedef struct word_node {
    char             word[MAX_WORD_LEN];
    long             count;
    struct word_node *next;
} word_node_t;

typedef struct {
    word_node_t *buckets[HASH_SIZE];
    long         total;
} word_table_t;

static unsigned int hash_word(const char *w)
{
    unsigned int h = 5381;
    while (*w) h = h * 31 + (unsigned char)*w++;
    return h & (HASH_SIZE - 1);
}

static void table_add(word_table_t *t, const char *w)
{
    unsigned int h = hash_word(w);
    for (word_node_t *n = t->buckets[h]; n != NULL; n = n->next) {
        if (strcmp(n->word, w) == 0) {
            n->count++;
            t->total++;
            return;
        }
    }
    word_node_t *n = calloc(1, sizeof *n);
    if (n == NULL) return;
    strncpy(n->word, w, MAX_WORD_LEN - 1);
    n->count = 1;
    n->next  = t->buckets[h];
    t->buckets[h] = n;
    t->total++;
}

static void table_merge(word_table_t *dst, const word_table_t *src)
{
    dst->total += src->total;
    for (int b = 0; b < HASH_SIZE; b++) {
        for (word_node_t *n = src->buckets[b]; n != NULL; n = n->next) {
            table_add(dst, n->word);
        }
    }
}

static void table_destroy(word_table_t *t)
{
    for (int b = 0; b < HASH_SIZE; b++) {
        word_node_t *n = t->buckets[b];
        while (n != NULL) {
            word_node_t *next = n->next;
            free(n);
            n = next;
        }
        t->buckets[b] = NULL;
    }
    t->total = 0;
}

static int cmp_nodes(const void *a, const void *b)
{
    const word_node_t *na = *(const word_node_t **)a;
    const word_node_t *nb = *(const word_node_t **)b;
    if (nb->count != na->count) return (nb->count > na->count) ? 1 : -1;
    return strcmp(na->word, nb->word); /* alphabetical tie-break */
}

/*
 * Find the byte offset just past the end of a word boundary starting at or
 * before `start`.  Used to split the file at word boundaries so words are
 * never cut in half.
 */
static size_t align_to_word_boundary(const char *data, size_t start,
                                      size_t size)
{
    if (start >= size) return size;
    /* Move forward past any partial word */
    while (start < size && (isalnum((unsigned char)data[start]))) {
        start++;
    }
    return start;
}

int task_wordcount(const kv_t *inputs, size_t n_inputs, job_context_t *ctx)
{
    const char *file_path  = task_input_get(inputs, n_inputs, "file");
    const char *thr_str    = task_input_get(inputs, n_inputs, "threads");
    const char *out_path   = task_input_get(inputs, n_inputs, "output");
    const char *top_str    = task_input_get(inputs, n_inputs, "top");

    if (file_path == NULL || thr_str == NULL || out_path == NULL) {
        log_append(ctx->log, "##[error] WordCount@1: missing required inputs.");
        return -1;
    }

    int n_threads = (int)strtol(thr_str, NULL, 10);
    if (n_threads < 1) n_threads = 1;
    int top_n = top_str ? (int)strtol(top_str, NULL, 10) : 0;

    /* Build absolute paths */
    char abs_in[PATH_MAX], abs_out[PATH_MAX];
    if (file_path[0] == '/')
        snprintf(abs_in, sizeof abs_in, "%s", file_path);
    else
        snprintf(abs_in, sizeof abs_in, "%s/%s", ctx->workspace, file_path);
    if (out_path[0] == '/')
        snprintf(abs_out, sizeof abs_out, "%s", out_path);
    else
        snprintf(abs_out, sizeof abs_out, "%s/%s", ctx->workspace, out_path);

    /* Read the whole file into memory */
    FILE *f = fopen(abs_in, "rb");
    if (f == NULL) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, "##[error] WordCount@1: cannot open '%s'.", abs_in);
        log_append(ctx->log, msg);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize < 0) { fclose(f); return -1; }
    size_t size = (size_t)fsize;
    char  *data = malloc(size + 1);
    if (data == NULL) { fclose(f); return -1; }
     size_t read_bytes = fread(data, 1, size, f);
    fclose(f);

    if (read_bytes != size) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, "##[error] WordCount@1: cannot read '%s'.", abs_in);
        log_append(ctx->log, msg);
        free(data);
        return -1;
    }
    data[size] = '\0';

    /* Lowercase the entire buffer for case-insensitive comparison */
    for (size_t i = 0; i < size; i++) {
        data[i] = (char)tolower((unsigned char)data[i]);
    }

    /* Split into chunks at word boundaries */
    word_table_t *per_thread = calloc((size_t)n_threads, sizeof *per_thread);
    if (per_thread == NULL) { free(data); return -1; }

    size_t chunk = (size + (size_t)n_threads - 1) / (size_t)n_threads;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int t = 0; t < n_threads; t++) {
        size_t start = align_to_word_boundary(data,
                           (size_t)t * chunk == 0 ? 0
                           : align_to_word_boundary(data, (size_t)t * chunk, size),
                           size);
        size_t raw_end = ((size_t)t + 1) * chunk;
        size_t end = (raw_end >= size) ? size
                     : align_to_word_boundary(data, raw_end, size);

        char word[MAX_WORD_LEN];
        size_t wlen = 0;
        for (size_t i = start; i < end; i++) {
            char c = data[i];
            if (isalnum((unsigned char)c)) {
                if (wlen < MAX_WORD_LEN - 1) word[wlen++] = c;
            } else if (wlen > 0) {
                word[wlen] = '\0';
                table_add(&per_thread[t], word);
                wlen = 0;
            }
        }
        if (wlen > 0) {
            word[wlen] = '\0';
            table_add(&per_thread[t], word);
        }
    }
    free(data);

    /* Merge in thread-index order (deterministic) */
    for (int t = 1; t < n_threads; t++) {
        table_merge(&per_thread[0], &per_thread[t]);
        table_destroy(&per_thread[t]);
    }
    word_table_t *merged = &per_thread[0];

    /* Count distinct words */
    long distinct = 0;
    for (int b = 0; b < HASH_SIZE; b++) {
        for (word_node_t *n = merged->buckets[b]; n != NULL; n = n->next) {
            distinct++;
        }
    }

    /* Write output */
    FILE *out = fopen(abs_out, "w");
    if (out == NULL) {
        table_destroy(merged);
        free(per_thread);
        log_append(ctx->log, "##[error] WordCount@1: cannot open output file.");
        return -1;
    }
    fprintf(out, "total_words: %ld\n", merged->total);
    fprintf(out, "distinct_words: %ld\n", distinct);

    if (top_n > 0) {
        /* Collect all nodes into an array for sorting */
        word_node_t **arr = malloc((size_t)distinct * sizeof *arr);
        if (arr != NULL) {
            size_t idx = 0;
            for (int b = 0; b < HASH_SIZE; b++) {
                for (word_node_t *n = merged->buckets[b]; n != NULL; n = n->next) {
                    arr[idx++] = n;
                }
            }
            qsort(arr, (size_t)distinct, sizeof *arr, cmp_nodes);
            int show = (top_n < distinct) ? top_n : (int)distinct;
            fprintf(out, "top_%d:\n", show);
            for (int i = 0; i < show; i++) {
                fprintf(out, "  %s: %ld\n", arr[i]->word, arr[i]->count);
            }
            free(arr);
        }
    }
    fclose(out);
    long final_total = merged->total;
    table_destroy(merged);
    free(per_thread);

    char msg[128];
    snprintf(msg, sizeof msg,
             "##[task] WordCount@1 done. total=%ld distinct=%ld",
             final_total, distinct);
    log_append(ctx->log, msg);
    return 0;
}

/* =========================================================
 * Histogram@1
 * Inputs: file, bins, threads, output
 *
 * Builds a byte-value histogram (bins evenly spaced over 0-255).
 * Writes CSV: bin_start,bin_end,count
 * Result deterministic for any thread count (integer arithmetic only).
 * ========================================================= */

int task_histogram(const kv_t *inputs, size_t n_inputs, job_context_t *ctx)
{
    const char *file_path = task_input_get(inputs, n_inputs, "file");
    const char *bins_str  = task_input_get(inputs, n_inputs, "bins");
    const char *thr_str   = task_input_get(inputs, n_inputs, "threads");
    const char *out_path  = task_input_get(inputs, n_inputs, "output");

    if (file_path == NULL || bins_str == NULL || thr_str == NULL
        || out_path == NULL) {
        log_append(ctx->log, "##[error] Histogram@1: missing required inputs.");
        return -1;
    }

    int n_bins    = (int)strtol(bins_str, NULL, 10);
    int n_threads = (int)strtol(thr_str,  NULL, 10);
    if (n_bins < 1)    n_bins    = 1;
    if (n_threads < 1) n_threads = 1;
    if (n_bins > 256)  n_bins    = 256;

    char abs_in[PATH_MAX], abs_out[PATH_MAX];
    if (file_path[0] == '/')
        snprintf(abs_in, sizeof abs_in, "%s", file_path);
    else
        snprintf(abs_in, sizeof abs_in, "%s/%s", ctx->workspace, file_path);
    if (out_path[0] == '/')
        snprintf(abs_out, sizeof abs_out, "%s", out_path);
    else
        snprintf(abs_out, sizeof abs_out, "%s/%s", ctx->workspace, out_path);

    /* Read file */
    FILE *f = fopen(abs_in, "rb");
    if (f == NULL) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg,
                 "##[error] Histogram@1: cannot open '%s'.", abs_in);
        log_append(ctx->log, msg);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize < 0) { fclose(f); return -1; }
    size_t size = (size_t)fsize;
    unsigned char *data = malloc(size);
    if (data == NULL) { fclose(f); return -1; }
    size_t read_bytes = fread(data, 1, size, f);
    fclose(f);
    
    if (read_bytes != size) {
        char msg[PATH_MAX + 64];
        snprintf(msg, sizeof msg, "##[error] Histogram@1: cannot read '%s'.", abs_in);
        log_append(ctx->log, msg);
        free(data);
        return -1;
    }

    /* Per-thread local counts (256 byte values) */
    long (*local_counts)[256] = calloc((size_t)n_threads,
                                        256 * sizeof(long));
    if (local_counts == NULL) { free(data); return -1; }

    size_t chunk = (size + (size_t)n_threads - 1) / (size_t)n_threads;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int t = 0; t < n_threads; t++) {
        size_t start = (size_t)t * chunk;
        size_t end   = start + chunk;
        if (end > size) end = size;
        for (size_t i = start; i < end; i++) {
            local_counts[t][data[i]]++;
        }
    }
    free(data);

    /* Merge in thread-index order (deterministic) */
    long global_counts[256] = {0};
    for (int t = 0; t < n_threads; t++) {
        for (int v = 0; v < 256; v++) {
            global_counts[v] += local_counts[t][v];
        }
    }
    free(local_counts);

    /* Aggregate into bins */
    long *bin_counts = calloc((size_t)n_bins, sizeof *bin_counts);
    if (bin_counts == NULL) return -1;

    for (int v = 0; v < 256; v++) {
        int bin = v * n_bins / 256;
        if (bin >= n_bins) bin = n_bins - 1;
        bin_counts[bin] += global_counts[v];
    }

    /* Write CSV */
    FILE *out = fopen(abs_out, "w");
    if (out == NULL) {
        free(bin_counts);
        log_append(ctx->log, "##[error] Histogram@1: cannot open output.");
        return -1;
    }
    fprintf(out, "bin_start,bin_end,count\n");
    for (int b = 0; b < n_bins; b++) {
        int lo = b * 256 / n_bins;
        int hi = (b + 1) * 256 / n_bins - 1;
        fprintf(out, "%d,%d,%ld\n", lo, hi, bin_counts[b]);
    }
    fclose(out);
    free(bin_counts);

    log_append(ctx->log, "##[task] Histogram@1 done.");
    return 0;
}
