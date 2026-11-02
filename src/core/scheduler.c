#define _POSIX_C_SOURCE 200809L

#include "scheduler.h"
#include "dag.h"
#include "state.h"

#include <stdlib.h>

static void track_start(scheduler_t *scheduler)
{
    size_t now = atomic_fetch_add(&scheduler->active, 1) + 1;
    size_t max = atomic_load(&scheduler->max_active);
    while (now > max && !atomic_compare_exchange_weak(&scheduler->max_active, &max, now)) {
    }
}

static bool should_run(run_t *run, size_t job)
{
    job_rt_t *j = &run->jobs[job];
    status_t deps_status[j->n_deps + 1];

    omp_set_lock(&run->lock);
    for (size_t i = 0; i < j->n_deps; i++) {
        deps_status[i] = run->jobs[j->deps[i]].status;
    }
    omp_unset_lock(&run->lock);

    return condition_met(j->def->condition, deps_status, j->n_deps);
}

static void finish_job(scheduler_t *scheduler, run_t *run, size_t job, status_t final)
{
    omp_set_lock(&run->lock);
    job_set_status(run, job, final);
    if (run->jobs_finished == run->n_jobs) {
        run->status = run_aggregate(run);
        clock_gettime(CLOCK_REALTIME, &run->end);
    }
    omp_unset_lock(&run->lock);

    size_t n_dependents = run->jobs[job].n_dependents;
    size_t *ready = malloc((n_dependents + 1) * sizeof *ready);
    if (ready == NULL) {
        return;
    }
    size_t n_ready = dag_release_dependents(run, job, ready);
    for (size_t i = 0; i < n_ready; i++) {
        job_queue_push(&scheduler->queue, run, ready[i]);
    }
    free(ready);
}

static void run_job(scheduler_t *scheduler, run_t *run, size_t job)
{
    if (atomic_load(&run->cancel_requested)) {
        finish_job(scheduler, run, job, ST_CANCELED);
        return;
    }
    if (!should_run(run, job)) {
        finish_job(scheduler, run, job, ST_SKIPPED);
        return;
    }

    omp_set_lock(&run->lock);
    job_set_status(run, job, ST_RUNNING);
    omp_unset_lock(&run->lock);

    track_start(scheduler);
    status_t final = scheduler->runner(run, job, scheduler->runner_ctx);
    atomic_fetch_sub(&scheduler->active, 1);

    finish_job(scheduler, run, job, final);
}

static void agent_loop(scheduler_t *scheduler)
{
    run_t *run = NULL;
    size_t job = 0;
    while (job_queue_pop(&scheduler->queue, &run, &job)) {
        run_job(scheduler, run, job);
    }
}

static void *scheduler_thread(void *arg)
{
    scheduler_t *scheduler = arg;
    omp_set_max_active_levels(2);

    #pragma omp parallel num_threads((int)scheduler->n_agents)
    {
        agent_loop(scheduler);
    }

    return NULL;
}

int scheduler_start(scheduler_t *scheduler, size_t n_agents, job_runner_t runner, void *ctx)
{
    if (n_agents == 0 || runner == NULL) {
        return -1;
    }
    scheduler->n_agents = n_agents;
    scheduler->runner = runner;
    scheduler->runner_ctx = ctx;
    atomic_init(&scheduler->active, 0);
    atomic_init(&scheduler->max_active, 0);

    if (job_queue_init(&scheduler->queue) < 0) {
        return -1;
    }
    if (pthread_create(&scheduler->thread, NULL, scheduler_thread, scheduler) != 0) {
        job_queue_destroy(&scheduler->queue);
        return -1;
    }
    return 0;
}

int scheduler_submit(scheduler_t *scheduler, run_t *run)
{
    omp_set_lock(&run->lock);
    run->status = ST_RUNNING;
    clock_gettime(CLOCK_REALTIME, &run->start);
    omp_unset_lock(&run->lock);

    for (size_t g = 0; g < run->n_jobs; g++) {
        if (run->jobs[g].n_deps == 0) {
            if (job_queue_push(&scheduler->queue, run, g) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

void scheduler_stop(scheduler_t *scheduler)
{
    job_queue_close(&scheduler->queue, scheduler->n_agents);
    pthread_join(scheduler->thread, NULL);
    job_queue_destroy(&scheduler->queue);
}