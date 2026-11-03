#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "job_queue.h"
#include "model.h"

#include <pthread.h>

typedef status_t (*job_runner_t)(run_t *run, size_t job, void *ctx);

typedef struct {
    size_t n_agents;
    job_queue_t queue;
    job_runner_t runner;
    void *runner_ctx;
    pthread_t thread;
    atomic_size_t active;
    atomic_size_t max_active;
} scheduler_t;

int scheduler_start(scheduler_t *scheduler, size_t n_agents, job_runner_t runner, void *ctx);

int scheduler_submit(scheduler_t *scheduler, run_t *run);

void scheduler_stop(scheduler_t *scheduler);

#endif