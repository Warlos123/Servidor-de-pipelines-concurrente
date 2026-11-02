#define _POSIX_C_SOURCE 200809L

#include "state.h"

#include <time.h>

typedef struct {
    size_t total;
    size_t queued;
    size_t running;
    size_t skipped;
    size_t canceled;
    size_t failed;
    size_t partial;
} status_count_t;

static const bool transitions[ST_COUNT][ST_COUNT] = {
    [ST_QUEUED] = {
        [ST_RUNNING] = true,
        [ST_SKIPPED] = true,
        [ST_CANCELED] = true,
    },
    [ST_RUNNING] = {
        [ST_SUCCEEDED] = true,
        [ST_PARTIALLY_SUCCEEDED] = true,
        [ST_FAILED] = true,
        [ST_CANCELED] = true,
    },
};

const char *status_name(status_t status)
{
    switch (status) {
    case ST_QUEUED:
        return "queued";
    case ST_RUNNING:
        return "running";
    case ST_SUCCEEDED:
        return "succeeded";
    case ST_PARTIALLY_SUCCEEDED:
        return "partiallySucceeded";
    case ST_FAILED:
        return "failed";
    case ST_CANCELED:
        return "canceled";
    case ST_SKIPPED:
        return "skipped";
    default:
        return "unknown";
    }
}

bool status_is_final(status_t status)
{
    switch (status) {
    case ST_SUCCEEDED:
    case ST_PARTIALLY_SUCCEEDED:
    case ST_FAILED:
    case ST_CANCELED:
    case ST_SKIPPED:
        return true;
    default:
        return false;
    }
}

bool status_can_transition(status_t from, status_t to)
{
    if ((size_t)from >= ST_COUNT || (size_t)to >= ST_COUNT) {
        return false;
    }
    return transitions[from][to];
}

int job_set_status(run_t *run, size_t job, status_t to)
{
    job_rt_t *j = &run->jobs[job];

    if (!status_can_transition(j->status, to)) {
        return -1;
    }

    if (to == ST_RUNNING) {
        clock_gettime(CLOCK_MONOTONIC, &j->start);
    }

    if (status_is_final(to)) {
        clock_gettime(CLOCK_MONOTONIC, &j->end);
        run->jobs_finished++;
    }

    j->status = to;
    return 0;
}

static void count_add(status_count_t *count, status_t status)
{
    count->total++;
    switch (status) {
    case ST_QUEUED:
        count->queued++;
        break;
    case ST_RUNNING:
        count->running++;
        break;
    case ST_SKIPPED:
        count->skipped++;
        break;
    case ST_CANCELED:
        count->canceled++;
        break;
    case ST_FAILED:
        count->failed++;
        break;
    case ST_PARTIALLY_SUCCEEDED:
        count->partial++;
        break;
    default:
        break;
    }
}

static status_t count_result(const status_count_t *count)
{
    if (count->running > 0) {
        return ST_RUNNING;
    }
    if (count->queued == count->total) {
        return ST_QUEUED;
    }
    if (count->queued > 0) {
        return ST_RUNNING;
    }
    if (count->canceled > 0) {
        return ST_CANCELED;
    }
    if (count->failed > 0) {
        return ST_FAILED;
    }
    if (count->partial > 0) {
        return ST_PARTIALLY_SUCCEEDED;
    }
    if (count->skipped == count->total) {
        return ST_SKIPPED;
    }
    return ST_SUCCEEDED;
}

status_t stage_aggregate(const run_t *run, size_t stage)
{
    status_count_t count = { 0 };

    for (size_t g = 0; g < run->n_jobs; g++) {
        if (run->jobs[g].stage == stage) {
            count_add(&count, run->jobs[g].status);
        }
    }

    return count_result(&count);
}

status_t run_aggregate(const run_t *run)
{
    status_count_t count = { 0 };

    for (size_t s = 0; s < run->n_stages; s++) {
        count_add(&count, stage_aggregate(run, s));
    }

    return count_result(&count);
}

bool condition_met(condition_t condition, const status_t *deps_status, size_t n_deps)
{
    if (condition == COND_ALWAYS) {
        return true;
    }

    bool any_failed = false;
    bool any_canceled = false;

    for (size_t i = 0; i < n_deps; i++) {
        if (deps_status[i] == ST_FAILED) {
            any_failed = true;
        }
        if (deps_status[i] == ST_CANCELED) {
            any_canceled = true;
        }
    }

    if (condition == COND_FAILED) {
        return any_failed;
    }

    return !any_failed && !any_canceled;
}