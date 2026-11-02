#include "dag.h"
#include "model.h"

#include <stdlib.h>
#include <string.h>

static bool has_index(const size_t *values, size_t n, size_t value)
{
    for (size_t i = 0; i < n; i++) {
        if (values[i] == value) {
            return true;
        }
    }
    return false;
}

static size_t add_unique(size_t *values, size_t n, size_t value)
{
    if (value == NO_INDEX || has_index(values, n, value)) {
        return n;
    }
    values[n] = value;
    return n + 1;
}

static size_t add_stage_jobs(const run_t *run, size_t stage, size_t *deps, size_t n_deps)
{
    for (size_t h = 0; h < run->n_jobs; h++) {
        if (run->jobs[h].stage == stage) {
            n_deps = add_unique(deps, n_deps, h);
        }
    }
    return n_deps;
}

static size_t collect_deps(const run_t *run, size_t g, size_t *deps)
{
    size_t n_deps = 0;
    size_t s = run->jobs[g].stage;
    const stage_def_t *stage = &run->pipeline->stages[s];
    const job_def_t *job = run->jobs[g].def;

    if (!stage->has_depends_on) {
        if (s > 0) {
            n_deps = add_stage_jobs(run, s - 1, deps, n_deps);
        }
    } else {
        for (size_t i = 0; i < stage->n_depends_on; i++) {
            size_t ds = run_find_stage(run, stage->depends_on[i]);
            if (ds != NO_INDEX) {
                n_deps = add_stage_jobs(run, ds, deps, n_deps);
            }
        }
    }

    for (size_t i = 0; i < job->n_depends_on; i++) {
        n_deps = add_unique(deps, n_deps, run_find_job(run, s, job->depends_on[i]));
    }

    return n_deps;
}

int dag_build(run_t *run)
{
    if (run->n_jobs == 0) {
        return 0;
    }

    size_t *buffer = malloc(run->n_jobs * sizeof *buffer);
    if (buffer == NULL) {
        return -1;
    }

    for (size_t g = 0; g < run->n_jobs; g++) {
        job_rt_t *job = &run->jobs[g];
        size_t n_deps = collect_deps(run, g, buffer);

        if (n_deps > 0) {
            job->deps = malloc(n_deps * sizeof *job->deps);
            if (job->deps == NULL) {
                free(buffer);
                return -1;
            }
            memcpy(job->deps, buffer, n_deps * sizeof *job->deps);
        }

        job->n_deps = n_deps;
        atomic_init(&job->pending, n_deps);
    }

    free(buffer);

    size_t *counts = calloc(run->n_jobs, sizeof *counts);
    if (counts == NULL) {
        return -1;
    }

    for (size_t g = 0; g < run->n_jobs; g++) {
        for (size_t i = 0; i < run->jobs[g].n_deps; i++) {
            counts[run->jobs[g].deps[i]]++;
        }
    }

    for (size_t d = 0; d < run->n_jobs; d++) {
        if (counts[d] > 0) {
            run->jobs[d].dependents = malloc(counts[d] * sizeof *run->jobs[d].dependents);
            if (run->jobs[d].dependents == NULL) {
                free(counts);
                return -1;
            }
        }
    }

    free(counts);

    for (size_t g = 0; g < run->n_jobs; g++) {
        for (size_t i = 0; i < run->jobs[g].n_deps; i++) {
            job_rt_t *dep = &run->jobs[run->jobs[g].deps[i]];
            dep->dependents[dep->n_dependents] = g;
            dep->n_dependents++;
        }
    }

    return 0;
}

size_t dag_release_dependents(run_t *run, size_t job, size_t *ready)
{
    size_t n_ready = 0;
    job_rt_t *finished = &run->jobs[job];

    for (size_t i = 0; i < finished->n_dependents; i++) {
        size_t d = finished->dependents[i];
        size_t before = atomic_fetch_sub(&run->jobs[d].pending, 1);
        if (before == 1) {
            ready[n_ready] = d;
            n_ready++;
        }
    }

    return n_ready;
}