#define _POSIX_C_SOURCE 200809L

#include "builtin_tasks.h"
#include "logstore.h"
#include "job_context.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *task_input_get(const kv_t *inputs, size_t n, const char *key)
{
    for (size_t i = 0; i < n; i++) {
        if (strcmp(inputs[i].key, key) == 0) {
            return inputs[i].value;
        }
    }
    return NULL;
}

/*
 * Server-wide cap on task threads. Written once in main() before the
 * scheduler starts, read-only from then on: no lock needed.
 * 0 means no cap (the task runs with the thread count it requested).
 */
static int g_max_task_threads = 0;

void task_set_max_threads(int max_threads)
{
    if (max_threads < 0) {
        max_threads = 0;
    }
    g_max_task_threads = max_threads;
}

int task_max_threads(void)
{
    return g_max_task_threads;
}

/*
 * Effective thread count for a data task: the requested `threads` input,
 * or the server cap when it is lower. With several agents running tasks at
 * the same time, agents x task threads can exceed the cores available;
 * the cap from --omp-threads keeps them from oversubscribing the machine.
 */
int task_effective_threads(int requested, job_context_t *ctx,
                                  const char *task_name)
{
    if (requested < 1) {
        requested = 1;
    }

    int cap = task_max_threads();
    if (cap > 0 && requested > cap) {
        char msg[128];
        snprintf(msg, sizeof msg,
                 "##[task] %s: threads=%d requested, clamped to %d by --omp-threads.",
                 task_name, requested, cap);
        log_append(ctx->log, msg);
        return cap;
    }

    return requested;
}

/* =========================================================
 * Sleep@1
 * Inputs: seconds (integer string)
 * ========================================================= */

int task_sleep(const kv_t *inputs, size_t n_inputs, job_context_t *ctx)
{
    const char *sec_str = task_input_get(inputs, n_inputs, "seconds");
    if (sec_str == NULL) {
        log_append(ctx->log, "##[error] Sleep@1: missing 'seconds' input.");
        return -1;
    }

    long seconds = strtol(sec_str, NULL, 10);
    if (seconds < 0) seconds = 0;

    char msg[64];
    snprintf(msg, sizeof msg, "##[task] Sleeping %ld second(s)...", seconds);
    log_append(ctx->log, msg);

    /* Sleep in 100ms increments so we can honour cancellation */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000L }; /* 100 ms */
    long ticks = seconds * 10; /* 100ms ticks */

    for (long t = 0; t < ticks; t++) {
        if (atomic_load(ctx->cancel_flag)) {
            log_append(ctx->log, "##[task] Sleep cancelled.");
            return -1;
        }

        if (job_timed_out(ctx)) {
            log_append(ctx->log, "##[error] Timeout exceeded.");
            return -1;
        }
        
        nanosleep(&ts, NULL);
    }

    log_append(ctx->log, "##[task] Sleep done.");
    return 0;
}

/* =========================================================
 * Cpu@1
 * Inputs: iterations, seed
 * Sets variable CPU_RESULT.
 * ========================================================= */

int task_cpu(const kv_t *inputs, size_t n_inputs, job_context_t *ctx)
{
    const char *iter_str = task_input_get(inputs, n_inputs, "iterations");
    const char *seed_str = task_input_get(inputs, n_inputs, "seed");

    if (iter_str == NULL || seed_str == NULL) {
        log_append(ctx->log, "##[error] Cpu@1: missing 'iterations' or 'seed' input.");
        return -1;
    }

    long iterations = strtol(iter_str, NULL, 10);
    unsigned long seed = (unsigned long)strtoul(seed_str, NULL, 10);

    if (iterations <= 0) {
        log_append(ctx->log, "##[error] Cpu@1: 'iterations' must be > 0.");
        return -1;
    }

    char msg[128];
    snprintf(msg, sizeof msg, "##[task] Cpu@1: iterations=%ld seed=%lu",
             iterations, seed);
    log_append(ctx->log, msg);

    /*
     * Deterministic CPU-bound computation: a simple linear congruential
     * generator iterated N times.  Result is the same regardless of thread
     * count because it is a sequential computation.
     */
    unsigned long state = seed;
    for (long i = 0; i < iterations; i++) {
        /* LCG constants from Knuth / MMIX */
        state = state * 6364136223846793005UL + 1442695040888963407UL;
    }

    char result_str[32];
    snprintf(result_str, sizeof result_str, "%lu", state);
    if (var_map_set(&ctx->vars, "CPU_RESULT", result_str) < 0) {
        return -1;
    }

    snprintf(msg, sizeof msg, "##[task] Cpu@1 done. CPU_RESULT=%s", result_str);
    log_append(ctx->log, msg);
    return 0;
}
