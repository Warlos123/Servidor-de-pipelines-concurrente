#define _POSIX_C_SOURCE 200809L

#include "job_execute.h"
#include "model.h"
#include "scheduler.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "check.h"

#define AGENTS 3
#define RUNS 20
#define WAIT_LIMIT_MS 10000

static void sleep_ms(long ms){
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static bool wait_finished(run_t *run){
    for(int waited = 0; waited < WAIT_LIMIT_MS; waited += 5){
        omp_set_lock(&run->lock);
        bool done = run->jobs_finished == run->n_jobs;
        omp_unset_lock(&run->lock);
        if(done){
            return true;
        }
        sleep_ms(5);
    }
    return false;
}

static void add_script(job_def_t *job, const char *script){
    job->steps = realloc(job->steps, (job->n_steps + 1) * sizeof *job->steps);
    memset(&job->steps[job->n_steps], 0, sizeof job->steps[job->n_steps]);
    job->steps[job->n_steps].kind = STEP_SCRIPT;
    job->steps[job->n_steps].script = strdup(script);
    job->n_steps++;
}

static void add_job(stage_def_t *stage, const char *name){
    stage->jobs = realloc(stage->jobs, (stage->n_jobs + 1) * sizeof *stage->jobs);
    memset(&stage->jobs[stage->n_jobs], 0, sizeof stage->jobs[stage->n_jobs]);
    stage->jobs[stage->n_jobs].name = strdup(name);
    stage->n_jobs++;
}

/* variables: GREETING=hola
 * stage Set:    early  -> X=early (after a short sleep), only_early -> E=1
 *               late   -> X=late  (no sleep, usually finishes first)
 * stage Middle: middle -> sets Y=mid, does not touch X
 * stage Check:  check  -> must see X=late, Y=mid, E=1, GREETING=hola */
static pipeline_t *make_pipeline(void){
    pipeline_t *p = calloc(1, sizeof *p);
    p->name = strdup("variables");
    p->n_variables = 1;
    p->variables = calloc(1, sizeof *p->variables);
    p->variables[0].key = strdup("GREETING");
    p->variables[0].value = strdup("hola");

    p->n_stages = 3;
    p->stages = calloc(3, sizeof *p->stages);

    stage_def_t *set = &p->stages[0];
    set->name = strdup("Set");
    add_job(set, "early");
    add_script(&set->jobs[0], "sleep 0.05");
    add_script(&set->jobs[0], "echo '##setvariable X=early'");
    add_script(&set->jobs[0], "echo '##setvariable E=1'");
    add_job(set, "late");
    add_script(&set->jobs[1], "echo '##setvariable X=late'");

    stage_def_t *middle = &p->stages[1];
    middle->name = strdup("Middle");
    add_job(middle, "middle");
    add_script(&middle->jobs[0], "test '$(X)' = late");
    add_script(&middle->jobs[0], "echo '##setvariable Y=mid'");

    stage_def_t *check = &p->stages[2];
    check->name = strdup("Check");
    add_job(check, "check");
    add_script(&check->jobs[0], "test '$(X)' = late");
    add_script(&check->jobs[0], "test '$(Y)' = mid");
    add_script(&check->jobs[0], "test '$(E)' = 1");
    add_script(&check->jobs[0], "test '$(GREETING)' = hola");
    return p;
}

int main(void)
{
    check_header("variables", "herencia entre stages/jobs: mismo YAML, mismo resultado, 20 corridas");
    char workdir[] = "/tmp/test_variables_XXXXXX";
    if(mkdtemp(workdir) == NULL){
        perror("mkdtemp");
        return EXIT_FAILURE;
    }

    scheduler_t scheduler;
    CHECK(scheduler_start(&scheduler, AGENTS, execute_job, workdir) == 0);

    check_section("variables: herencia de ancestros", "el job hereda de todos sus ancestros y gana el declarado después (20 corridas)");
    int wrong = 0;
    for(int r = 0; r < RUNS; r++){
        run_t *run = run_create((uint64_t)r + 1, make_pipeline());
        if(run == NULL){
            wrong++;
            continue;
        }
        scheduler_submit(&scheduler, run);
        if(!wait_finished(run)){
            wrong++;
            break;
        }

        bool ok = run->status == ST_SUCCEEDED;
        if(run->jobs[2].vars.count != 1 || strcmp(run->jobs[2].vars.entries[0].key, "Y") != 0){
            ok = false;
        }
        if(!ok && wrong == 0){
            printf("  run %d ended %s (middle %s, check %s)\n", r + 1, status_name(run->status),
                   status_name(run->jobs[2].status), status_name(run->jobs[3].status));
        }
        if(!ok){
            wrong++;
        }
        run_destroy(run);
    }
    CHECK(wrong == 0);

    scheduler_stop(&scheduler);

    char command[64];
    snprintf(command, sizeof command, "rm -rf %s", workdir);
    if(system(command) != 0){
        printf("  could not remove %s\n", workdir);
    }

    return check_summary("variables — herencia de ancestros y gana el declarado después");
}