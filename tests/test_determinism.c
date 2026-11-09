#define _POSIX_C_SOURCE 200809L

#include "model.h"
#include "scheduler.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "check.h"

#define FAN_JOBS 32
#define JOIN_JOBS 8
#define RACE_AGENTS 8
#define RACE_RUNS 50
#define DET_AGENTS 4
#define DET_RUNS 100
#define MAX_JOBS 64
#define WAIT_LIMIT_MS 5000

typedef struct {
    atomic_int runs[MAX_JOBS];
    atomic_uint seed;
} counter_ctx_t;

static void sleep_us(long us){
    struct timespec ts = { us / 1000000L, (us % 1000000L) * 1000L };
    nanosleep(&ts, NULL);
}

static char *name_of(const char *prefix, size_t i){
    char name[32];
    snprintf(name, sizeof name, "%s%zu", prefix, i);
    return strdup(name);
}

static bool wait_finished(run_t *run){
    for(int waited = 0; waited < WAIT_LIMIT_MS; waited++){
        omp_set_lock(&run->lock);
        bool done = run->jobs_finished == run->n_jobs;
        omp_unset_lock(&run->lock);
        if(done){
            return true;
        }
        sleep_us(1000);
    }
    return false;
}

static void reset_counts(counter_ctx_t *ctx){
    for(int i = 0; i < MAX_JOBS; i++){
        atomic_store(&ctx->runs[i], 0);
    }
}



static status_t counting_runner(run_t *run, size_t job, void *arg){
    (void)run;
    counter_ctx_t *ctx = arg;
    atomic_fetch_add(&ctx->runs[job], 1);
    return ST_SUCCEEDED;
}

// stage Fan: f0..f31 (independent), stage Join: j0..j7 (each waits for all of Fan)
static pipeline_t *make_fan_join(void){
    pipeline_t *p = calloc(1, sizeof *p);
    p->name = strdup("fan-join");
    p->n_stages = 2;
    p->stages = calloc(2, sizeof *p->stages);

    p->stages[0].name = strdup("Fan");
    p->stages[0].n_jobs = FAN_JOBS;
    p->stages[0].jobs = calloc(FAN_JOBS, sizeof(job_def_t));
    for(size_t i = 0; i < FAN_JOBS; i++){
        p->stages[0].jobs[i].name = name_of("f", i);
    }

    p->stages[1].name = strdup("Join");
    p->stages[1].n_jobs = JOIN_JOBS;
    p->stages[1].jobs = calloc(JOIN_JOBS, sizeof(job_def_t));
    for(size_t i = 0; i < JOIN_JOBS; i++){
        p->stages[1].jobs[i].name = name_of("j", i);
    }
    return p;
}

static void test_each_job_runs_once(void){
    check_section("race: every job runs exactly once", "50 corridas con 8 agentes y retardos aleatorios: ningún job se pierde ni se repite");
    counter_ctx_t ctx;
    scheduler_t scheduler;
    CHECK(scheduler_start(&scheduler, RACE_AGENTS, counting_runner, &ctx) == 0);

    int bad_runs = 0;
    for(int r = 0; r < RACE_RUNS; r++){
        reset_counts(&ctx);
        run_t *run = run_create((uint64_t)r + 1, make_fan_join());
        if(run == NULL){
            bad_runs++;
            continue;
        }
        scheduler_submit(&scheduler, run);

        bool finished = wait_finished(run);
        bool once = true;
        for(size_t g = 0; g < run->n_jobs; g++){
            if(atomic_load(&ctx.runs[g]) != 1){
                once = false;
            }
        }
        if(!finished || !once){
            // a repeated job may still be running: keep the run alive and stop here
            bad_runs++;
            break;
        }
        run_destroy(run);
    }

    if(bad_runs > 0){
        printf("  %d run(s) lost or repeated a job\n", bad_runs);
    }
    CHECK(bad_runs == 0);
    scheduler_stop(&scheduler);
}



static status_t random_runner(run_t *run, size_t job, void *arg){
    counter_ctx_t *ctx = arg;
    unsigned int seed = atomic_fetch_add(&ctx->seed, 7919u);
    sleep_us((long)(rand_r(&seed) % 3000));

    if(strncmp(run->jobs[job].def->name, "boom", 4) == 0){
        return ST_FAILED;
    }
    return ST_SUCCEEDED;
}

static void set_job(job_def_t *job, const char *name, condition_t condition){
    job->name = strdup(name);
    job->condition = condition;
}

/* stage A:  a0, boom, a2                                  -> boom fails
 * stage B:  condition always(): ok_only (succeeded), on_fail (failed), always
 * stage C:  dependsOn [] -> c0, c1 (dependsOn c0)          -> independent of A and B
 * stage D:  dependsOn A, default condition: d0 (always)    -> skipped, A failed */
static pipeline_t *make_mixed(void){
    pipeline_t *p = calloc(1, sizeof *p);
    p->name = strdup("mixed");
    p->n_stages = 4;
    p->stages = calloc(4, sizeof *p->stages);

    stage_def_t *a = &p->stages[0];
    a->name = strdup("A");
    a->n_jobs = 3;
    a->jobs = calloc(3, sizeof(job_def_t));
    set_job(&a->jobs[0], "a0", COND_SUCCEEDED);
    set_job(&a->jobs[1], "boom", COND_SUCCEEDED);
    set_job(&a->jobs[2], "a2", COND_SUCCEEDED);

    stage_def_t *b = &p->stages[1];
    b->name = strdup("B");
    b->condition = COND_ALWAYS;
    b->n_jobs = 3;
    b->jobs = calloc(3, sizeof(job_def_t));
    set_job(&b->jobs[0], "ok_only", COND_SUCCEEDED);
    set_job(&b->jobs[1], "on_fail", COND_FAILED);
    set_job(&b->jobs[2], "always", COND_ALWAYS);

    stage_def_t *c = &p->stages[2];
    c->name = strdup("C");
    c->has_depends_on = true;
    c->n_jobs = 2;
    c->jobs = calloc(2, sizeof(job_def_t));
    set_job(&c->jobs[0], "c0", COND_SUCCEEDED);
    set_job(&c->jobs[1], "c1", COND_SUCCEEDED);
    c->jobs[1].depends_on = calloc(1, sizeof(char *));
    c->jobs[1].depends_on[0] = strdup("c0");
    c->jobs[1].n_depends_on = 1;
    c->jobs[1].has_depends_on = true;

    stage_def_t *d = &p->stages[3];
    d->name = strdup("D");
    d->has_depends_on = true;
    d->depends_on = calloc(1, sizeof(char *));
    d->depends_on[0] = strdup("A");
    d->n_depends_on = 1;
    d->n_jobs = 1;
    d->jobs = calloc(1, sizeof(job_def_t));
    set_job(&d->jobs[0], "d0", COND_ALWAYS);
    return p;
}

static void test_same_result_every_run(void){
    check_section("determinism: same final states", "100 corridas con usleep al azar: los estados finales son siempre idénticos");
    counter_ctx_t ctx;
    atomic_init(&ctx.seed, 12345u);
    scheduler_t scheduler;
    CHECK(scheduler_start(&scheduler, DET_AGENTS, random_runner, &ctx) == 0);

    const status_t expected[9] = {
        ST_SUCCEEDED, ST_FAILED, ST_SUCCEEDED,
        ST_SKIPPED, ST_SUCCEEDED, ST_SUCCEEDED,
        ST_SUCCEEDED, ST_SUCCEEDED,
        ST_SKIPPED
    };

    int different = 0;
    for(int r = 0; r < DET_RUNS; r++){
        run_t *run = run_create((uint64_t)r + 1, make_mixed());
        if(run == NULL){
            different++;
            continue;
        }
        scheduler_submit(&scheduler, run);
        if(!wait_finished(run)){
            different++;
            break;
        }

        bool same = run->status == ST_FAILED;
        for(size_t g = 0; g < run->n_jobs; g++){
            if(run->jobs[g].status != expected[g]){
                same = false;
            }
        }
        if(stage_aggregate(run, 0) != ST_FAILED || stage_aggregate(run, 1) != ST_SUCCEEDED ||
           stage_aggregate(run, 2) != ST_SUCCEEDED || stage_aggregate(run, 3) != ST_SKIPPED){
            same = false;
        }
        if(!same){
            different++;
        }
        run_destroy(run);
    }

    if(different > 0){
        printf("  %d run(s) ended differently\n", different);
    }
    CHECK(different == 0);
    scheduler_stop(&scheduler);
}

int main(void)
{
    check_header("determinism", "cada job corre exactamente una vez y nada cambia con el plan del SO");
    test_each_job_runs_once();
    test_same_result_every_run();
    return check_summary("determinism — cada job una sola vez y estados idénticos con retardos aleatorios");
}