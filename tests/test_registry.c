#define _POSIX_C_SOURCE 200809L

#include "model.h"
#include "registry.h"
#include "scheduler.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_AGENTS 3
#define WIDE_JOBS 6
#define MANY_RUNS 20
#define WAIT_LIMIT_MS 5000

static int g_failures = 0;
static int g_checks = 0;


/* 
The following block of code was partially made using: 
Anthropic (2026). Claude(Opus 5.5) https://claude.ai
*/


#define CHECK(cond) do {                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                             \
    } while (0)

typedef struct {
    long ms;
} fake_ctx_t;

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static status_t fake_runner(run_t *run, size_t job, void *arg)
{
    (void)run;
    (void)job;
    const fake_ctx_t *ctx = arg;
    sleep_ms(ctx->ms);
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
    for (int waited = 0; waited < WAIT_LIMIT_MS; waited += 5) {
        omp_set_lock(&run->lock);
        bool done = run->jobs_finished == run->n_jobs;
        omp_unset_lock(&run->lock);
        if (done) {
            return true;
        }
        sleep_ms(5);
    }
    return false;
}

static void test_status_and_list(void)
{
    printf("status and list\n");
    fake_ctx_t ctx = { .ms = 20 };
    scheduler_t scheduler;
    registry_t registry;

    int init = registry_init(&registry);
    CHECK(init == 0);
    if (init != 0) {
        return;
    }
    CHECK(scheduler_start(&scheduler, N_AGENTS, fake_runner, &ctx) == 0);

    uint64_t first = 0;
    uint64_t second = 0;
    CHECK(registry_submit(&registry, &scheduler, make_pipeline(), &first) == 0);
    CHECK(registry_submit(&registry, &scheduler, make_pipeline(), &second) == 0);
    CHECK(first == 1 && second == 2);

    if (registry.count == 2) {
        CHECK(wait_finished(registry.runs[0]));
        CHECK(wait_finished(registry.runs[1]));

        char out[4096];
        CHECK(registry_status(&registry, first, out, sizeof out) == 0);
        CHECK(strstr(out, "run 1 ") != NULL);
        CHECK(strstr(out, "succeeded") != NULL);
        CHECK(strstr(out, "stage Wide") != NULL);
        CHECK(strstr(out, "job w5") != NULL);
        CHECK(strstr(out, "stage Final") != NULL);

        CHECK(registry_status(&registry, 99, out, sizeof out) == REG_NOT_FOUND);
        CHECK(registry_status(&registry, 0, out, sizeof out) == REG_NOT_FOUND);

        CHECK(registry_list(&registry, out, sizeof out) == 0);
        CHECK(strstr(out, "1 scheduler-test succeeded") != NULL);
        CHECK(strstr(out, "2 scheduler-test succeeded") != NULL);

        char tiny[16];
        CHECK(registry_status(&registry, first, tiny, sizeof tiny) == 0);
        CHECK(strlen(tiny) < sizeof tiny);
    }

    scheduler_stop(&scheduler);
    registry_destroy(&registry);
}

static void test_cancel(void)
{
    printf("cancel\n");
    fake_ctx_t ctx = { .ms = 50 };
    scheduler_t scheduler;
    registry_t registry;

    int init = registry_init(&registry);
    CHECK(init == 0);
    if (init != 0) {
        return;
    }
    CHECK(scheduler_start(&scheduler, 1, fake_runner, &ctx) == 0);

    uint64_t id = 0;
    CHECK(registry_submit(&registry, &scheduler, make_pipeline(), &id) == 0);

    if (registry.count == 1) {
        sleep_ms(20);
        CHECK(registry_cancel(&registry, id) == 0);
        CHECK(registry_cancel(&registry, 99) == REG_NOT_FOUND);
        CHECK(wait_finished(registry.runs[0]));
        CHECK(registry.runs[0]->status == ST_CANCELED);
        CHECK(registry.runs[0]->jobs[WIDE_JOBS].status == ST_CANCELED);
        CHECK(registry_cancel(&registry, id) == REG_FINISHED);
    }

    scheduler_stop(&scheduler);
    registry_destroy(&registry);
}

static void test_concurrent_submit(void)
{
    printf("concurrent submits get unique ids\n");
    fake_ctx_t ctx = { .ms = 1 };
    scheduler_t scheduler;
    registry_t registry;

    int init = registry_init(&registry);
    CHECK(init == 0);
    if (init != 0) {
        return;
    }
    CHECK(scheduler_start(&scheduler, N_AGENTS, fake_runner, &ctx) == 0);

    uint64_t ids[MANY_RUNS] = { 0 };
    int failed = 0;

    #pragma omp parallel for num_threads(8)
    for (int i = 0; i < MANY_RUNS; i++) {
        if (registry_submit(&registry, &scheduler, make_pipeline(), &ids[i]) != 0) {
            #pragma omp atomic
            failed++;
        }
    }

    CHECK(failed == 0);
    CHECK(registry.count == MANY_RUNS);

    bool seen[MANY_RUNS + 1] = { false };
    for (int i = 0; i < MANY_RUNS; i++) {
        if (ids[i] >= 1 && ids[i] <= MANY_RUNS) {
            seen[ids[i]] = true;
        }
    }
    bool all_unique = true;
    for (int id = 1; id <= MANY_RUNS; id++) {
        if (!seen[id]) {
            all_unique = false;
        }
    }
    CHECK(all_unique);

    if (registry.count == MANY_RUNS) {
        bool all_done = true;
        for (size_t i = 0; i < registry.count; i++) {
            if (!wait_finished(registry.runs[i])) {
                all_done = false;
            }
        }
        CHECK(all_done);
        CHECK(atomic_load(&scheduler.max_active) <= N_AGENTS);
    }

    scheduler_stop(&scheduler);
    registry_destroy(&registry);
}

static double elapsed_ms(const struct timespec *start){
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start->tv_sec) * 1000.0 + (double)(now.tv_nsec - start->tv_nsec) / 1e6;
}

static void test_cancel_all(void){
    printf("cancel all: shutdown stops unfinished runs\n");
    fake_ctx_t ctx = { .ms = 100 };
    scheduler_t scheduler;
    registry_t registry;

    int init = registry_init(&registry);
    CHECK(init == 0);
    if(init != 0){
        return;
    }
    CHECK(scheduler_start(&scheduler, 2, fake_runner, &ctx) == 0);

    uint64_t id = 0;
    CHECK(registry_submit(&registry, &scheduler, make_pipeline(), &id) == 0);
    CHECK(registry_submit(&registry, &scheduler, make_pipeline(), &id) == 0);
    CHECK(registry_submit(&registry, &scheduler, make_pipeline(), &id) == 0);
    sleep_ms(30);

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(registry_cancel_all(&registry) == 3);
    scheduler_stop(&scheduler);

    double ms = elapsed_ms(&start);
    printf("  stopped in %.0f ms (all the work would take about 1000 ms)\n", ms);
    CHECK(ms < 500.0);

    size_t running = 0;
    size_t canceled = 0;
    for(size_t r = 0; r < registry.count; r++){
        run_t *run = registry.runs[r];
        for(size_t g = 0; g < run->n_jobs; g++){
            if(run->jobs[g].status == ST_RUNNING){
                running++;
            }
            if(run->jobs[g].status == ST_CANCELED){
                canceled++;
            }
        }
    }
    CHECK(running == 0);
    CHECK(canceled >= 3 * WIDE_JOBS - 2);

    registry_destroy(&registry);
}

static void test_cancel_all_skips_finished(void){
    printf("cancel all leaves finished runs alone\n");
    fake_ctx_t ctx = { .ms = 1 };
    scheduler_t scheduler;
    registry_t registry;

    int init = registry_init(&registry);
    CHECK(init == 0);
    if(init != 0){
        return;
    }
    CHECK(scheduler_start(&scheduler, N_AGENTS, fake_runner, &ctx) == 0);

    uint64_t id = 0;
    CHECK(registry_submit(&registry, &scheduler, make_pipeline(), &id) == 0);
    if(registry.count == 1){
        CHECK(wait_finished(registry.runs[0]));
    }
    CHECK(registry_cancel_all(&registry) == 0);
    if(registry.count == 1){
        CHECK(registry.runs[0]->status == ST_SUCCEEDED);
    }

    scheduler_stop(&scheduler);
    registry_destroy(&registry);
}

int main(void)
{
    test_status_and_list();
    test_cancel();
    test_concurrent_submit();
    test_cancel_all();
    test_cancel_all_skips_finished();
    printf("\n%d of %d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures == 0) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}