#ifndef BUILTIN_TASKS_H
#define BUILTIN_TASKS_H

#include "model.h"
#include "job_context.h"

/*
 * builtin_tasks.h — interface for the four built-in tasks.
 *
 * Each function takes the parsed inputs kv array from the step definition
 * plus the job context (for logging and variable injection) and returns
 * 0 on success, non-zero on failure.
 *
 * Tasks that use OpenMP parallelism (WordCount, Histogram) respect the
 * `threads` input and omp_set_max_active_levels(2) set by the scheduler.
 *
 * Implemented by Persona C (src/tasks/).
 */

/* Sleep@1  — inputs: seconds (integer string)
 * Sleeps for the requested number of seconds.
 * Respects the job's cancel flag: returns -1 if cancelled before finishing.
 */
int task_sleep(const kv_t *inputs, size_t n_inputs, job_context_t *ctx);

/* Cpu@1    — inputs: iterations, seed
 * Deterministic CPU-bound computation.
 * Sets variable CPU_RESULT in ctx->vars.
 */
int task_cpu(const kv_t *inputs, size_t n_inputs, job_context_t *ctx);

/* WordCount@1 — inputs: file, threads, output, top (optional)
 * Counts total words, distinct words, and optionally the top-N most frequent.
 * "Word" = maximal sequence of ASCII letters/digits (case-insensitive).
 * Result is deterministic for any thread count.
 * Writes output to the file specified by `output` input, inside workspace.
 */
int task_wordcount(const kv_t *inputs, size_t n_inputs, job_context_t *ctx);

/* Histogram@1 — inputs: file, bins, threads, output
 * Builds a byte-value histogram (bins evenly spaced over 0-255).
 * Writes CSV to `output` inside workspace.
 * Result is deterministic for any thread count.
 */
int task_histogram(const kv_t *inputs, size_t n_inputs, job_context_t *ctx);

/* Helper: find a key in a kv array; returns NULL if not found. */
const char *task_input_get(const kv_t *inputs, size_t n, const char *key);

#endif /* BUILTIN_TASKS_H */
