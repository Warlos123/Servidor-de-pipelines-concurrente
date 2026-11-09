#ifndef BUILTIN_TASKS_H
#define BUILTIN_TASKS_H

#include "model.h"
#include "job_context.h"

/*
 * Built-in tasks. Each returns 0 on success and non-zero on failure.
 * WordCount and Histogram respect the `threads` input.
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

const char *task_input_get(const kv_t *inputs, size_t n, const char *key);

/*
 * Global cap on the number of threads a data task may use. The server sets
 * it once at startup from the --omp-threads argument, before any agent
 * thread runs, and the agents only read it afterwards, so no lock is needed.
 * It is one value for every data task: several parallel agents running tasks
 * at once multiply agent x task threads, and the cap keeps that count close
 * to the cores available instead of oversubscribing them.
 */
void task_set_max_threads(int max_threads);

/* Returns the cap. 0 means no cap: tasks use their requested thread count. */
int task_max_threads(void);

/*
 * Effective thread count for a data task: the requested `threads` input
 * clamped to the server cap when it is lower, and never below 1. Logs a
 * line into the job's log when the cap applies. Internal helper shared by
 * WordCount@1 and Histogram@1.
 */
int task_effective_threads(int requested, job_context_t *ctx,
                           const char *task_name);

#endif /* BUILTIN_TASKS_H */
