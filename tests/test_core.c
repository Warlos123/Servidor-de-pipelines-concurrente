/* 
The following block of code was partially made using: 
Anthropic (2026). Claude(Opus 5.5) https://claude.ai
*/
#define _POSIX_C_SOURCE 200809L

#include "dag.h"
#include "model.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do {                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            printf("  FALLA %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                             \
    } while (0)

/* ---------- Pipeline de prueba armado a mano ----------
 * stages:
 *   - stage: Build                 (jobs globales 0 y 1)
 *     jobs: compilar, lint
 *   - stage: Test                  (jobs globales 2 y 3)
 *     jobs: unit, integ (dependsOn: unit)
 *   - stage: Deploy, dependsOn: [] (job global 4)
 *     jobs: subir
 */
static char **names(size_t n, const char **src)
{
    char **out = calloc(n, sizeof *out);
    for (size_t i = 0; i < n; i++) {
        out[i] = strdup(src[i]);
    }
    return out;
}

static pipeline_t *make_pipeline(void)
{
    pipeline_t *p = calloc(1, sizeof *p);
    p->name = strdup("prueba");
    p->n_stages = 3;
    p->stages = calloc(3, sizeof *p->stages);

    stage_def_t *build = &p->stages[0];
    build->name = strdup("Build");
    build->n_jobs = 2;
    build->jobs = calloc(2, sizeof *build->jobs);
    build->jobs[0].name = strdup("compilar");
    build->jobs[1].name = strdup("lint");

    stage_def_t *test = &p->stages[1];
    test->name = strdup("Test");
    test->n_jobs = 2;
    test->jobs = calloc(2, sizeof *test->jobs);
    test->jobs[0].name = strdup("unit");
    test->jobs[1].name = strdup("integ");
    const char *integ_deps[] = { "unit" };
    test->jobs[1].depends_on = names(1, integ_deps);
    test->jobs[1].n_depends_on = 1;
    test->jobs[1].has_depends_on = true;

    stage_def_t *deploy = &p->stages[2];
    deploy->name = strdup("Deploy");
    deploy->has_depends_on = true; /* dependsOn: [] -> sin dependencias */
    deploy->n_jobs = 1;
    deploy->jobs = calloc(1, sizeof *deploy->jobs);
    deploy->jobs[0].name = strdup("subir");

    return p;
}

static bool contains(const size_t *arr, size_t n, size_t value)
{
    for (size_t i = 0; i < n; i++) {
        if (arr[i] == value) {
            return true;
        }
    }
    return false;
}

/* ---------- Pruebas ---------- */

static void test_status(void)
{
    printf("estados\n");
    CHECK(strcmp(status_name(ST_QUEUED), "queued") == 0);
    CHECK(strcmp(status_name(ST_PARTIALLY_SUCCEEDED), "partiallySucceeded") == 0);
    CHECK(!status_is_final(ST_QUEUED));
    CHECK(!status_is_final(ST_RUNNING));
    CHECK(status_is_final(ST_SKIPPED));
    CHECK(status_is_final(ST_FAILED));

    CHECK(status_can_transition(ST_QUEUED, ST_RUNNING));
    CHECK(status_can_transition(ST_QUEUED, ST_SKIPPED));
    CHECK(status_can_transition(ST_QUEUED, ST_CANCELED));
    CHECK(status_can_transition(ST_RUNNING, ST_SUCCEEDED));
    CHECK(status_can_transition(ST_RUNNING, ST_PARTIALLY_SUCCEEDED));
    CHECK(status_can_transition(ST_RUNNING, ST_FAILED));
    CHECK(status_can_transition(ST_RUNNING, ST_CANCELED));
    CHECK(!status_can_transition(ST_QUEUED, ST_SUCCEEDED));
    CHECK(!status_can_transition(ST_RUNNING, ST_QUEUED));
    CHECK(!status_can_transition(ST_SUCCEEDED, ST_RUNNING));
    CHECK(!status_can_transition(ST_CANCELED, ST_FAILED));
}

static void test_condition(void)
{
    printf("condiciones\n");
    status_t ok[] = { ST_SUCCEEDED, ST_SUCCEEDED };
    status_t one_failed[] = { ST_SUCCEEDED, ST_FAILED };

    CHECK(condition_met(COND_SUCCEEDED, ok, 2));
    CHECK(!condition_met(COND_SUCCEEDED, one_failed, 2));
    CHECK(condition_met(COND_FAILED, one_failed, 2));
    CHECK(!condition_met(COND_FAILED, ok, 2));
    CHECK(condition_met(COND_ALWAYS, one_failed, 2));
    CHECK(condition_met(COND_SUCCEEDED, NULL, 0));
    CHECK(!condition_met(COND_FAILED, NULL, 0));
}

static run_t *test_model(void)
{
    printf("modelo\n");
    run_t *run = run_create(7, make_pipeline());
    CHECK(run != NULL);
    if (run == NULL) {
        return NULL;
    }
    CHECK(run->id == 7);
    CHECK(run->n_stages == 3);
    CHECK(run->n_jobs == 5);
    CHECK(run->jobs[2].stage == 1);
    CHECK(strcmp(run->jobs[3].def->name, "integ") == 0);
    CHECK(run->jobs[0].status == ST_QUEUED);

    CHECK(run_find_stage(run, "Test") == 1);
    CHECK(run_find_stage(run, "NoExiste") == NO_INDEX);
    CHECK(run_find_job(run, 1, "integ") == 3);
    CHECK(run_find_job(run, 0, "integ") == NO_INDEX);
    return run;
}

static void test_dag(run_t *run)
{
    printf("dag\n");
    /* Build no depende de nadie */
    CHECK(run->jobs[0].n_deps == 0);
    CHECK(run->jobs[1].n_deps == 0);

    /* unit depende de toda la etapa Build */
    CHECK(run->jobs[2].n_deps == 2);
    CHECK(contains(run->jobs[2].deps, run->jobs[2].n_deps, 0));
    CHECK(contains(run->jobs[2].deps, run->jobs[2].n_deps, 1));

    /* integ: toda la etapa Build + unit */
    CHECK(run->jobs[3].n_deps == 3);
    CHECK(contains(run->jobs[3].deps, run->jobs[3].n_deps, 2));

    /* Deploy declaró dependsOn: [] */
    CHECK(run->jobs[4].n_deps == 0);

    CHECK(atomic_load(&run->jobs[3].pending) == 3);
    CHECK(run->jobs[0].n_dependents == 2);

    /* Liberar dependientes */
    size_t ready[8];
    size_t n = dag_release_dependents(run, 0, ready); /* termina compilar */
    CHECK(n == 0);
    n = dag_release_dependents(run, 1, ready);        /* termina lint */
    CHECK(n == 1 && ready[0] == 2);                   /* unit queda lista */
    n = dag_release_dependents(run, 2, ready);        /* termina unit */
    CHECK(n == 1 && ready[0] == 3);                   /* integ queda lista */
}

static void test_set_status(run_t *run)
{
    printf("job_set_status y agregación\n");
    omp_set_lock(&run->lock);
    CHECK(job_set_status(run, 0, ST_RUNNING) == 0);
    CHECK(job_set_status(run, 0, ST_QUEUED) == -1);
    CHECK(stage_aggregate(run, 0) == ST_RUNNING);
    CHECK(job_set_status(run, 0, ST_SUCCEEDED) == 0);
    CHECK(job_set_status(run, 1, ST_RUNNING) == 0);
    CHECK(job_set_status(run, 1, ST_SUCCEEDED) == 0);
    CHECK(run->jobs_finished == 2);
    CHECK(stage_aggregate(run, 0) == ST_SUCCEEDED);
    CHECK(stage_aggregate(run, 1) == ST_QUEUED);
    CHECK(run_aggregate(run) == ST_RUNNING);
    omp_unset_lock(&run->lock);
}

int main(void)
{
    test_status();
    test_condition();
    run_t *run = test_model();
    if (run != NULL) {
        test_dag(run);
        test_set_status(run);
        run_destroy(run);
    }
    printf("\n%d de %d verificaciones pasaron\n", g_checks - g_failures, g_checks);
    if (g_failures == 0) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
