#define _POSIX_C_SOURCE 200809L

#include "registry.h"
#include "state.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define INITIAL_CAPACITY 16

typedef struct {
    char *buffer;
    size_t size;
    size_t used;
} out_t;

static void out_printf(out_t *out, const char *format, ...){
    if (out->used >= out->size) {
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(out->buffer + out->used, out->size - out->used, format, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    out->used += (size_t)written;
    if (out->used >= out->size) {
        out->used = out->size - 1;
    }
}

static bool is_zero(const struct timespec *t){
    return t->tv_sec == 0 && t->tv_nsec == 0;
}

static bool is_later(const struct timespec *a, const struct timespec *b){
    if (a->tv_sec != b->tv_sec) {
        return a->tv_sec > b->tv_sec;
    }
    return a->tv_nsec > b->tv_nsec;
}

static double elapsed(const struct timespec *start, const struct timespec *end, clockid_t clock){
    if (is_zero(start)) {
        return 0.0;
    }

    struct timespec stop = *end;
    if (is_zero(&stop)) {
        clock_gettime(clock, &stop);
    }
    return (double)(stop.tv_sec - start->tv_sec) + (double)(stop.tv_nsec - start->tv_nsec) / 1e9;
}

static double stage_elapsed(const run_t *run, size_t stage){
    struct timespec first = { 0, 0 };
    struct timespec last = { 0, 0 };
    bool all_final = true;

    for (size_t g = 0; g < run->n_jobs; g++) {
        const job_rt_t *job = &run->jobs[g];
        if (job->stage != stage) {
            continue;
        }
        if (!status_is_final(job->status)) {
            all_final = false;
        }
        if (!is_zero(&job->start) && (is_zero(&first) || is_later(&first, &job->start))) {
            first = job->start;
        }
        if (status_is_final(job->status) && is_later(&job->end, &last)) {
            last = job->end;
        }
    }

    if (!all_final) {
        last.tv_sec = 0;
        last.tv_nsec = 0;
    }
    return elapsed(&first, &last, CLOCK_MONOTONIC);
}

static const char *text_or(const char *text, const char *fallback){
    if (text == NULL) {
        return fallback;
    }
    return text;
}

static const char *step_label(const step_def_t *step){
    switch (step->kind) {
    case STEP_SCRIPT:
        return "script";
    case STEP_TASK:
        return text_or(step->task, "task");
    case STEP_PUBLISH:
        return "publish";
    case STEP_DOWNLOAD:
        return "download";
    default:
        return "step";
    }
}

static void format_run(const run_t *run, out_t *out){
    out_printf(out, "run %" PRIu64 " \"%s\" %s %.2fs\n", run->id,
               text_or(run->pipeline->name, "-"), status_name(run->status),
               elapsed(&run->start, &run->end, CLOCK_REALTIME));

    for (size_t s = 0; s < run->n_stages; s++) {
        out_printf(out, "stage %s %s %.2fs\n", text_or(run->pipeline->stages[s].name, "-"),
                   status_name(stage_aggregate(run, s)), stage_elapsed(run, s));

        for (size_t g = 0; g < run->n_jobs; g++) {
            const job_rt_t *job = &run->jobs[g];
            if (job->stage != s) {
                continue;
            }
            out_printf(out, "  job %s %s %.2fs\n", text_or(job->def->name, "-"),
                       status_name(job->status), elapsed(&job->start, &job->end, CLOCK_MONOTONIC));

            for (size_t k = 0; k < job->def->n_steps; k++) {
                const step_rt_t *step = &job->steps[k];
                out_printf(out, "    step %zu %s %s %.2fs\n", k + 1, step_label(&job->def->steps[k]),
                           status_name(step->status), elapsed(&step->start, &step->end, CLOCK_MONOTONIC));
            }
        }
    }
}

int registry_init(registry_t *registry){
    registry->runs = malloc(INITIAL_CAPACITY * sizeof *registry->runs);
    if (registry->runs == NULL) {
        return -1;
    }
    registry->count = 0;
    registry->capacity = INITIAL_CAPACITY;
    omp_init_lock(&registry->lock);
    return 0;
}

void registry_destroy(registry_t *registry){
    for (size_t i = 0; i < registry->count; i++) {
        run_destroy(registry->runs[i]);
    }
    free(registry->runs);
    registry->runs = NULL;
    registry->count = 0;
    registry->capacity = 0;
    omp_destroy_lock(&registry->lock);
}

static run_t *find_run(registry_t *registry, uint64_t run_id){
    run_t *run = NULL;
    omp_set_lock(&registry->lock);
    if (run_id >= 1 && run_id <= (uint64_t)registry->count) {
        run = registry->runs[run_id - 1];
    }
    omp_unset_lock(&registry->lock);
    return run;
}

int registry_submit(registry_t *registry, scheduler_t *scheduler, pipeline_t *pipeline, uint64_t *run_id){
    omp_set_lock(&registry->lock);

    if (registry->count == registry->capacity) {
        size_t capacity = registry->capacity * 2;
        run_t **grown = realloc(registry->runs, capacity * sizeof *grown);
        if (grown == NULL) {
            omp_unset_lock(&registry->lock);
            return -1;
        }
        registry->runs = grown;
        registry->capacity = capacity;
    }

    uint64_t id = (uint64_t)registry->count + 1;
    run_t *run = run_create(id, pipeline);
    if (run == NULL) {
        omp_unset_lock(&registry->lock);
        return -1;
    }
    registry->runs[registry->count] = run;
    registry->count++;

    omp_unset_lock(&registry->lock);

    if (scheduler_submit(scheduler, run) < 0) {
        omp_set_lock(&run->lock);
        run->status = ST_FAILED;
        omp_unset_lock(&run->lock);
    }

    *run_id = id;
    return 0;
}

int registry_status(registry_t *registry, uint64_t run_id, char *out, size_t size){
    run_t *run = find_run(registry, run_id);
    if (run == NULL) {
        return REG_NOT_FOUND;
    }
    if (size == 0) {
        return 0;
    }

    out_t o = { out, size, 0 };
    out[0] = '\0';

    omp_set_lock(&run->lock);
    format_run(run, &o);
    omp_unset_lock(&run->lock);
    return 0;
}

int registry_list(registry_t *registry, char *out, size_t size){
    if (size == 0) {
        return 0;
    }
    out_t o = { out, size, 0 };
    out[0] = '\0';

    omp_set_lock(&registry->lock);
    size_t count = registry->count;
    run_t **copy = malloc((count + 1) * sizeof *copy);
    if (copy == NULL) {
        omp_unset_lock(&registry->lock);
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        copy[i] = registry->runs[i];
    }
    omp_unset_lock(&registry->lock);

    for (size_t i = 0; i < count; i++) {
        run_t *run = copy[i];
        omp_set_lock(&run->lock);
        out_printf(&o, "%" PRIu64 " %s %s\n", run->id, text_or(run->pipeline->name, "-"),
                   status_name(run->status));
        omp_unset_lock(&run->lock);
    }

    free(copy);
    return 0;
}

int registry_cancel(registry_t *registry, uint64_t run_id){
    run_t *run = find_run(registry, run_id);
    if (run == NULL) {
        return REG_NOT_FOUND;
    }

    omp_set_lock(&run->lock);
    bool finished = status_is_final(run->status);
    omp_unset_lock(&run->lock);

    if (finished) {
        return REG_FINISHED;
    }
    atomic_store(&run->cancel_requested, true);
    return 0;
}


run_t *registry_find(registry_t *registry, uint64_t run_id){
    return find_run(registry, run_id);
}


void registry_close_logs(registry_t *registry){
    omp_set_lock(&registry->lock);

    for (size_t i = 0; i < registry->count; i++) {
        run_t *run = registry->runs[i];

        for (size_t g = 0; g < run->n_jobs; g++) {
            log_close(&run->jobs[g].log);
        }
    }

    omp_unset_lock(&registry->lock);
}