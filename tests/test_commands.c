#define _POSIX_C_SOURCE 200809L

#include "commands.h"
#include "model.h"
#include "registry.h"
#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "check.h"

#define BODY_BYTES 2048
#define WAIT_LIMIT_MS 3000



static scheduler_t g_scheduler;
static registry_t g_registry;

scheduler_t *server_scheduler(void){
    return &g_scheduler;
}

registry_t *server_registry(void){
    return &g_registry;
}

void server_request_shutdown(void){
}

//The tests never shut the server down.
int server_is_running(void){
    return 1;
}

static void sleep_ms(long ms){
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static status_t slow_runner(run_t *run, size_t job, void *ctx){
    (void)run;
    (void)job;
    (void)ctx;
    sleep_ms(60);
    return ST_SUCCEEDED;
}

static pipeline_t *make_pipeline(void){
    pipeline_t *p = calloc(1, sizeof *p);
    p->name = strdup("cmd-test");
    p->n_stages = 2;
    p->stages = calloc(2, sizeof *p->stages);
    p->stages[0].name = strdup("Build");
    p->stages[0].n_jobs = 1;
    p->stages[0].jobs = calloc(1, sizeof(job_def_t));
    p->stages[0].jobs[0].name = strdup("compile");
    p->stages[1].name = strdup("Test");
    p->stages[1].n_jobs = 1;
    p->stages[1].jobs = calloc(1, sizeof(job_def_t));
    p->stages[1].jobs[0].name = strdup("unit");
    return p;
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

static int run_command(c_type_t type, unsigned long id, char *body){
    command_t command;
    memset(&command, 0, sizeof command);
    command.type = type;
    command.id = id;
    body[0] = '\0';
    return command_execute(&command, NULL, 0, body, BODY_BYTES);
}

static int run_yaml(const char *yaml, char *body){
    command_t command;
    memset(&command, 0, sizeof command);
    command.type = C_SUBMIT;
    command.body_len = strlen(yaml);
    body[0] = '\0';
    return command_execute(&command, yaml, strlen(yaml), body, BODY_BYTES);
}

int main(void)
{
    check_header("commands", "comandos del servidor sobre runs reales: formato exacto de respuestas");
    char body[BODY_BYTES];

    CHECK(registry_init(&g_registry) == 0);
    CHECK(scheduler_start(&g_scheduler, 1, slow_runner, NULL) == 0);

    check_section("empty registry", "STATUS/LIST/CANCEL sobre el registro vacío: errores limpios, sin runs falsos");
    CHECK(run_command(C_LIST, 0, body) == 1);
    CHECK(strcmp(body, "no runs") == 0);
    CHECK(run_command(C_STATUS, 1, body) == 0);
    CHECK(strstr(body, "not found") != NULL);
    CHECK(run_command(C_CANCEL, 1, body) == 0);
    CHECK(strstr(body, "not found") != NULL);

    check_section("status, list and cancel of real runs", "STATUS/LIST/CANCEL sobre runs vivos: formato exacto y estados consistentes");
    uint64_t first = 0;
    uint64_t second = 0;
    CHECK(registry_submit(&g_registry, &g_scheduler, make_pipeline(), &first) == 0);
    CHECK(registry_submit(&g_registry, &g_scheduler, make_pipeline(), &second) == 0);

    CHECK(run_command(C_CANCEL, second, body) == 1);
    CHECK(strstr(body, "canceling") != NULL);

    if(g_registry.count == 2){
        CHECK(wait_finished(g_registry.runs[0]));
        CHECK(wait_finished(g_registry.runs[1]));
    }

    CHECK(run_command(C_STATUS, first, body) == 1);
    CHECK(strstr(body, "run 1 \"cmd-test\" succeeded") != NULL);
    CHECK(strstr(body, "job unit succeeded") != NULL);

    CHECK(run_command(C_STATUS, second, body) == 1);
    CHECK(strstr(body, "canceled") != NULL);

    CHECK(run_command(C_LIST, 0, body) == 1);
    CHECK(strstr(body, "1 cmd-test succeeded") != NULL);
    CHECK(strstr(body, "2 cmd-test canceled") != NULL);

    CHECK(run_command(C_CANCEL, first, body) == 0);
    CHECK(strstr(body, "already finished") != NULL);

    check_section("submit", "SUBMIT válido e inválido: id único, cuerpo correcto y exit codes");
    const char *valid =
        "name: from-yaml\n"
        "stages:\n"
        "- stage: Build\n"
        "  jobs:\n"
        "  - job: compile\n"
        "    steps:\n"
        "    - script: make\n";
    CHECK(run_yaml(valid, body) == 1);
    CHECK(strcmp(body, "3") == 0);

    const char *invalid =
        "stages:\n"
        "- stage: build\n"
        "  displayName: Build\n"
        "  jobs: [{job: a, steps: [{script: x}]}]\n"
        "- stage: build\n"
        "  jobs: [{job: b, steps: [{script: x}]}]\n";
    CHECK(run_yaml(invalid, body) == 0);
    CHECK(strstr(body, "displayName") != NULL);
    CHECK(strchr(body, '\n') != strrchr(body, '\n'));
    CHECK(g_registry.count == 3);

    CHECK(run_yaml("not: [valid", body) == 0);
    CHECK(body[0] != '\0');
    CHECK(g_registry.count == 3);

    if(g_registry.count == 3){
        CHECK(wait_finished(g_registry.runs[2]));
    }
    CHECK(run_command(C_STATUS, 3, body) == 1);
    CHECK(strstr(body, "\"from-yaml\" succeeded") != NULL);

    scheduler_stop(&g_scheduler);
    registry_destroy(&g_registry);

    return check_summary("commands — STATUS, LIST y SUBMIT: el texto que recibe el cliente es la interfaz");
}