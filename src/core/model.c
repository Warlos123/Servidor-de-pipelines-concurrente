#define _POSIX_C_SOURCE 200809L

#include "../../include/model.h"
#include "dag.h"

#include <stdlib.h>
#include <string.h>

static run_t *run_create_fail(run_t *run)
{
    run->pipeline = NULL;
    run_destroy(run);
    return NULL;
}

run_t *run_create(uint64_t id, pipeline_t *pipeline)
{
    run_t *run = calloc(1, sizeof *run);
    if (run == NULL) {
        return NULL;
    }

    run->id = id;
    run->pipeline = pipeline;
    clock_gettime(CLOCK_REALTIME, &run->created);
    omp_init_lock(&run->lock);
    atomic_init(&run->cancel_requested, false);

    run->stages = calloc(pipeline->n_stages, sizeof *run->stages);
    if (run->stages == NULL && pipeline->n_stages > 0) {
        return run_create_fail(run);
    }
    run->n_stages = pipeline->n_stages;

    size_t n_jobs = 0;
    for (size_t s = 0; s < pipeline->n_stages; s++) {
        n_jobs += pipeline->stages[s].n_jobs;
    }

    run->jobs = calloc(n_jobs, sizeof *run->jobs);
    if (run->jobs == NULL && n_jobs > 0) {
        return run_create_fail(run);
    }
    run->n_jobs = n_jobs;

    size_t g = 0;
    for (size_t s = 0; s < pipeline->n_stages; s++) {
        for (size_t j = 0; j < pipeline->stages[s].n_jobs; j++) {
            job_rt_t *job = &run->jobs[g];
            job->def = &pipeline->stages[s].jobs[j];
            job->stage = s;
            atomic_init(&job->pending, 0);

            job->steps = calloc(job->def->n_steps, sizeof *job->steps);
            if (job->steps == NULL && job->def->n_steps > 0) {
                return run_create_fail(run);
            }
            g++;
        }
    }

    if (dag_build(run) < 0) {
        return run_create_fail(run);
    }

    return run;
}

void run_destroy(run_t *run)
{
    if (run == NULL) {
        return;
    }

    for (size_t i = 0; i < run->n_jobs; i++) {
        free(run->jobs[i].deps);
        free(run->jobs[i].dependents);
        free(run->jobs[i].steps);
    }

    free(run->jobs);
    free(run->stages);
    omp_destroy_lock(&run->lock);
    pipeline_free(run->pipeline);
    free(run);
}

static void free_kvs(kv_t *kvs, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        free(kvs[i].key);
        free(kvs[i].value);
    }
    free(kvs);
}

static void free_names(char **names, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        free(names[i]);
    }
    free(names);
}

static void step_def_free(step_def_t *step)
{
    free(step->script);
    free(step->task);
    free_kvs(step->inputs, step->n_inputs);
    free(step->path);
    free(step->artifact);
}

static void job_def_free(job_def_t *job)
{
    for (size_t k = 0; k < job->n_steps; k++) {
        step_def_free(&job->steps[k]);
    }
    free(job->steps);
    free_names(job->depends_on, job->n_depends_on);
    free(job->name);
}

void pipeline_free(pipeline_t *pipeline)
{
    if (pipeline == NULL) {
        return;
    }

    for (size_t s = 0; s < pipeline->n_stages; s++) {
        stage_def_t *stage = &pipeline->stages[s];
        for (size_t j = 0; j < stage->n_jobs; j++) {
            job_def_free(&stage->jobs[j]);
        }
        free(stage->jobs);
        free_names(stage->depends_on, stage->n_depends_on);
        free(stage->name);
    }

    free(pipeline->stages);
    free_kvs(pipeline->variables, pipeline->n_variables);
    free(pipeline->name);
    free(pipeline);
}

size_t run_find_stage(const run_t *run, const char *stage_name)
{
    for (size_t s = 0; s < run->pipeline->n_stages; s++) {
        if (strcmp(run->pipeline->stages[s].name, stage_name) == 0) {
            return s;
        }
    }
    return NO_INDEX;
}

size_t run_find_job(const run_t *run, size_t stage, const char *job_name)
{
    for (size_t g = 0; g < run->n_jobs; g++) {
        if (run->jobs[g].stage == stage && strcmp(run->jobs[g].def->name, job_name) == 0) {
            return g;
        }
    }
    return NO_INDEX;
}