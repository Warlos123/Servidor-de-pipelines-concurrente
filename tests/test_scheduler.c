#define _POSIX_C_SOURCE 200809L

#include "model.h"
#include "scheduler.h"
#include "state.h"

#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_AGENTS 3
#define WIDE_JOBS 6
#define JOB_MS 50
#define WAIT_LIMIT_MS 3000

typedef struct {
    size_t failing_job;
    atomic_size_t order;
    size_t finished_at[16];
} fake_ctx_t;

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static status_t fake_runner(run_t *run, size_t job, void *arg)
{
    (void)run;
    fake_ctx_t *ctx = arg;
    sleep_ms(JOB_MS);
    ctx->finished_at[job] = atomic_fetch_add(&ctx->order, 1);
    if (job == ctx->failing_job) {
        return ST_FAILED;
    }
    return ST_SUCCEEDED;
}

static pipeline_t *make_pipeline(void)
{
    pipeline_t *p = calloc(1, sizeof *p);
    p->name = strdup("scheduler-test");
    p->n_stages = 2;
    p->stages = calloc(2, sizeof *p->stages);

    stage_def_t *wide = &p->stages[0];
    wide->name = strdup("Wide");
    wide->n_jobs = WIDE_JOBS;
    wide->jobs = calloc(WIDE_JOBS, sizeof *wide->jobs);
    for (size_t i = 0; i < WIDE_JOBS; i++) {
        char name[8];
        snprintf(name, sizeof name, "w%zu", i);
        wide->jobs[i].name = strdup(name);
    }

    stage_def_t *final = &p->stages[1];
    final->name = strdup("Final");
    final->n_jobs = 1;
    final->jobs = calloc(1, sizeof *final->jobs);
    final->jobs[0].name = strdup("final");
    return p;
}

static bool wait_finished(run_t *run)
{
    for (int waited = 0; waited < WAIT_LIMIT_MS; waited += 10) {
        omp_set_lock(&run->lock);
        bool done = run->jobs_finished == run->n_jobs;
        omp_unset_lock(&run->lock);
        if (done) {
            return true;
        }
        sleep_ms(10);
    }
    return false;
}

static void test_all_succeed(void)
{
    check_section("wide pipeline, all succeed", "16 jobs, ningún fallo: todos succeed y max_active nunca supera N agentes");
    fake_ctx_t ctx = { .failing_job = NO_INDEX };
    atomic_init(&ctx.order, 0);

    scheduler_t scheduler;
    CHECK(scheduler_start(&scheduler, N_AGENTS, fake_runner, &ctx) == 0);
    run_t *run = run_create(1, make_pipeline());
    CHECK(run != NULL);
    if (run == NULL) {
        scheduler_stop(&scheduler);
        return;
    }

    CHECK(scheduler_submit(&scheduler, run) == 0);
    CHECK(wait_finished(run));

    size_t max = atomic_load(&scheduler.max_active);
    CHECK(max <= N_AGENTS);
    CHECK(max == N_AGENTS);

    for (size_t i = 0; i < WIDE_JOBS; i++) {
        CHECK(run->jobs[i].status == ST_SUCCEEDED);
        CHECK(ctx.finished_at[i] < ctx.finished_at[WIDE_JOBS]);
    }
    CHECK(run->jobs[WIDE_JOBS].status == ST_SUCCEEDED);
    CHECK(run->status == ST_SUCCEEDED);

    scheduler_stop(&scheduler);
    run_destroy(run);
}

static void test_failure_skips(void)
{
    check_section("one job fails, final is skipped", "condición succeeded(): con un fallo, lo que depende de él se salta");
    fake_ctx_t ctx = { .failing_job = 2 };
    atomic_init(&ctx.order, 0);

    scheduler_t scheduler;
    CHECK(scheduler_start(&scheduler, N_AGENTS, fake_runner, &ctx) == 0);
    run_t *run = run_create(2, make_pipeline());
    CHECK(run != NULL);
    if (run == NULL) {
        scheduler_stop(&scheduler);
        return;
    }

    CHECK(scheduler_submit(&scheduler, run) == 0);
    CHECK(wait_finished(run));
    CHECK(run->jobs[2].status == ST_FAILED);
    CHECK(run->jobs[WIDE_JOBS].status == ST_SKIPPED);
    CHECK(run->status == ST_FAILED);

    scheduler_stop(&scheduler);
    run_destroy(run);
}

static void test_two_runs_share_agents(void)
{
    check_section("two runs compete for the same agents", "dos pipelines a la vez: el pool global los intercala sin inanición");
    fake_ctx_t ctx = { .failing_job = NO_INDEX };
    atomic_init(&ctx.order, 0);

    scheduler_t scheduler;
    CHECK(scheduler_start(&scheduler, N_AGENTS, fake_runner, &ctx) == 0);
    run_t *a = run_create(3, make_pipeline());
    run_t *b = run_create(4, make_pipeline());
    CHECK(a != NULL && b != NULL);
    if (a == NULL || b == NULL) {
        scheduler_stop(&scheduler);
        run_destroy(a);
        run_destroy(b);
        return;
    }

    CHECK(scheduler_submit(&scheduler, a) == 0);
    CHECK(scheduler_submit(&scheduler, b) == 0);
    CHECK(wait_finished(a));
    CHECK(wait_finished(b));
    CHECK(atomic_load(&scheduler.max_active) <= N_AGENTS);
    CHECK(a->status == ST_SUCCEEDED);
    CHECK(b->status == ST_SUCCEEDED);

    scheduler_stop(&scheduler);
    run_destroy(a);
    run_destroy(b);
}

int main(void)
{
    check_header("scheduler", "pool global de agentes, cola FIFO y salto de etapas — aquí nace y muere cada job");
    test_all_succeed();
    test_failure_skips();
    test_two_runs_share_agents();
    return check_summary("scheduler — DAG y pool");
}