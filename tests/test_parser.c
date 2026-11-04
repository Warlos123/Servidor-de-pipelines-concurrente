#define _POSIX_C_SOURCE 200809L

#include "yaml_parser.h"
#include "validator.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;


#define CHECK(cond) do {                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                             \
    } while (0)


//Parses text and returns the pipeline
static pipeline_t *parse(const char *text, error_list_t *errors){
    pipeline_t *pipeline = yaml_parse_pipeline(text, strlen(text), errors);

    if(pipeline != NULL)
        validate_pipeline(pipeline, errors);

    return pipeline;
}


static int has_error(const error_list_t *errors, const char *piece){
    for(size_t i = 0; i < errors->count; i++){
        if(strstr(errors->items[i], piece) != NULL)
            return 1;
    }

    return 0;
}


static void expect_error(const char *text, const char *piece){
    error_list_t errors = {0};
    pipeline_t *pipeline = parse(text, &errors);

    CHECK(has_error(&errors, piece));

    if(!has_error(&errors, piece)){
        printf("    expected \"%s\", got:\n", piece);

        for(size_t i = 0; i < errors.count; i++)
            printf("      %s\n", errors.items[i]);
    }

    pipeline_free(pipeline);
    error_list_free(&errors);
}

/* 
The following block of code was partially made using: 
Anthropic (2026). Claude(Opus 5.5) https://claude.ai
*/


static void test_loading(void){
    printf("loading the YAML\n");

    expect_error("", "the YAML is empty");
    expect_error("# only a comment\n", "the YAML is empty");
    expect_error("stages:\n\t- stage: a\n", "line 2: invalid YAML");
    expect_error("stages:\n  - stage: a\n   jobs: x\n", "line 3: invalid YAML");
    expect_error("hello\n", "line 1: the pipeline must be a map");
    expect_error("- stage: a\n", "line 1: the pipeline must be a map");
}

static const char *VALID =
    "name: demo\n"
    "variables:\n"
    "  CONFIG: Release\n"
    "stages:\n"
    "- stage: build\n"
    "  jobs:\n"
    "  - job: compile\n"
    "    timeoutInSeconds: 60\n"
    "    steps:\n"
    "    - script: make $(CONFIG)\n"
    "      continueOnError: true\n"
    "    - publish: bin/app\n"
    "      artifact: app\n"
    "- stage: test\n"
    "  dependsOn: build\n"
    "  condition: always()\n"
    "  jobs:\n"
    "  - job: unit\n"
    "    steps:\n"
    "    - download: app\n"
    "      path: bin\n"
    "    - task: Sleep@1\n"
    "      inputs: {seconds: 2}\n"
    "  - job: report\n"
    "    dependsOn: [unit]\n"
    "    condition: failed()\n"
    "    steps:\n"
    "    - script: echo done\n"
    "- stage: docs\n"
    "  dependsOn: []\n"
    "  jobs:\n"
    "  - job: generate\n"
    "    steps:\n"
    "    - script: echo docs\n";


static void test_valid_pipeline(void){
    printf("valid pipeline\n");
    error_list_t errors = {0};
    pipeline_t *pipeline = parse(VALID, &errors);

    CHECK(pipeline != NULL);
    CHECK(errors.count == 0);

    if(pipeline == NULL){
        error_list_free(&errors);
        return;
    }

    CHECK(strcmp(pipeline->name, "demo") == 0);
    CHECK(pipeline->n_variables == 1 && strcmp(pipeline->variables[0].key, "CONFIG") == 0);
    CHECK(pipeline->n_stages == 3);

    //Stage build: no dependsOn (depends on nothing, it is the first one).
    stage_def_t *build = &pipeline->stages[0];
    CHECK(strcmp(build->name, "build") == 0);
    CHECK(build->has_depends_on == false);
    CHECK(build->condition == COND_SUCCEEDED);
    CHECK(build->n_jobs == 1);

    job_def_t *compile = &build->jobs[0];
    CHECK(strcmp(compile->name, "compile") == 0);
    CHECK(compile->timeout_seconds == 60);
    CHECK(compile->n_steps == 2);
    CHECK(compile->steps[0].kind == STEP_SCRIPT && strcmp(compile->steps[0].script, "make $(CONFIG)") == 0);
    CHECK(compile->steps[0].continue_on_error == true);
    CHECK(compile->steps[1].kind == STEP_PUBLISH);
    CHECK(strcmp(compile->steps[1].path, "bin/app") == 0 && strcmp(compile->steps[1].artifact, "app") == 0);

    //Stage test: dependsOn as a single name, condition always().
    stage_def_t *test = &pipeline->stages[1];
    CHECK(test->has_depends_on == true && test->n_depends_on == 1);
    CHECK(strcmp(test->depends_on[0], "build") == 0);
    CHECK(test->condition == COND_ALWAYS);
    CHECK(test->n_jobs == 2);

    job_def_t *unit = &test->jobs[0];
    CHECK(unit->steps[0].kind == STEP_DOWNLOAD);
    CHECK(strcmp(unit->steps[0].artifact, "app") == 0 && strcmp(unit->steps[0].path, "bin") == 0);
    CHECK(unit->steps[1].kind == STEP_TASK && strcmp(unit->steps[1].task, "Sleep@1") == 0);
    CHECK(unit->steps[1].n_inputs == 1 && strcmp(unit->steps[1].inputs[0].value, "2") == 0);

    job_def_t *report = &test->jobs[1];
    CHECK(report->n_depends_on == 1 && strcmp(report->depends_on[0], "unit") == 0);
    CHECK(report->condition == COND_FAILED);

    //Stage docs: dependsOn: [] (written, but empty).
    stage_def_t *docs = &pipeline->stages[2];
    CHECK(docs->has_depends_on == true && docs->n_depends_on == 0);

    pipeline_free(pipeline);
    error_list_free(&errors);
}



//STRUCTURE ERRORS
static void test_structure_errors(void){
    printf("structure errors\n");

    expect_error("name: x\n", "line 1: pipeline without 'stages'");
    expect_error("stages: build\n", "line 1: 'stages' must be a list");
    expect_error("stages: []\n", "line 1: 'stages' can't be empty");
    expect_error("stages:\n- build\n", "line 2: each stage must be a map");
    expect_error("stages:\n- jobs: [{job: a, steps: [{script: x}]}]\n", "line 2: missing 'stage'");
    expect_error("stages:\n- stage: a\n", "line 2: stage without 'jobs'");
    expect_error("stages:\n- stage: a\n  jobs:\n  - job: j\n", "line 4: job without 'steps'");
    expect_error("stages:\n- stage: a\n  jobs:\n  - job: j\n    steps: []\n", "line 5: 'steps' can't be empty");
}



//STEP AND VALUE ERRORS
static void test_value_errors(void){
    printf("step and value errors\n");
    const char *start = "stages:\n- stage: a\n  jobs:\n  - job: j\n";
    char text[512];

    snprintf(text, sizeof text, "%s    steps:\n    - script: x\n      task: Sleep@1\n", start);
    expect_error(text, "line 6: a step needs exactly one of");

    snprintf(text, sizeof text, "%s    steps:\n    - continueOnError: true\n", start);
    expect_error(text, "line 6: a step needs exactly one of");

    snprintf(text, sizeof text, "%s    steps:\n    - publish: out\n", start);
    expect_error(text, "line 6: 'publish' needs 'artifact'");

    snprintf(text, sizeof text, "%s    steps:\n    - script: x\n      continueOnError: maybe\n", start);
    expect_error(text, "line 7: 'continueOnError' must be true or false");

    snprintf(text, sizeof text, "%s    timeoutInSeconds: 10s\n    steps: [{script: x}]\n", start);
    expect_error(text, "line 5: 'timeoutInSeconds' must be a positive integer");

    snprintf(text, sizeof text, "%s    condition: sometimes()\n    steps: [{script: x}]\n", start);
    expect_error(text, "line 5: 'condition' must be succeeded(), failed() or always()");

    snprintf(text, sizeof text, "%s    dependsOn: {a: 1}\n    steps: [{script: x}]\n", start);
    expect_error(text, "line 5: 'dependsOn' must be a name or a list of names");
}



//UNKNOWN KEYS
static void test_unknown_keys(void){
    printf("unknown keys\n");
    const char *text =
        "trigger: main\n"
        "stages:\n"
        "- stage: build\n"
        "  displayName: Build\n"
        "  jobs:\n"
        "  - job: a\n"
        "    pool: linux\n"
        "    steps:\n"
        "    - script: make\n"
        "      workingDirectory: src\n";

    expect_error(text, "line 1: unknown key 'trigger' in pipeline");
    expect_error(text, "line 4: unknown key 'displayName' in stage");
    expect_error(text, "line 7: unknown key 'pool' in job");
    expect_error(text, "line 10: unknown key 'workingDirectory' in step");
}



//DUPLICATES AND REFERENCES
static void test_duplicates_and_references(void){
    printf("duplicate names and unknown references\n");
    const char *text =
        "stages:\n"
        "- stage: build\n"
        "  jobs:\n"
        "  - {job: a, steps: [{script: x}]}\n"
        "  - {job: b, dependsOn: [a, ghost], steps: [{script: x}]}\n"
        "  - {job: a, steps: [{script: x}]}\n"
        "- stage: build\n"
        "  jobs: [{job: c, steps: [{script: x}]}]\n"
        "- stage: test\n"
        "  dependsOn: tset\n"
        "  jobs: [{job: d, dependsOn: c, steps: [{script: x}]}]\n";

    expect_error(text, "stages[1]: duplicate stage name 'build'");
    expect_error(text, "stages[0].jobs[2]: duplicate job name 'a'");
    expect_error(text, "stages[0].jobs[1].dependsOn: unknown job 'ghost'");
    expect_error(text, "stages[2].dependsOn: unknown stage 'tset'");

    //c exists, but in another stage: jobs can only depend on jobs of their own stage.
    expect_error(text, "stages[2].jobs[0].dependsOn: unknown job 'c'");
}


//CYCLES
static void test_cycles(void){
    printf("dependency cycles\n");

    //Jobs: a <-> b, and a job that depends on itself.
    expect_error(
        "stages:\n- stage: s\n  jobs:\n"
        "  - {job: a, dependsOn: b, steps: [{script: x}]}\n"
        "  - {job: b, dependsOn: a, steps: [{script: x}]}\n",
        "stages[0].jobs: dependency cycle");

    expect_error(
        "stages:\n- stage: s\n  jobs:\n"
        "  - {job: self, dependsOn: self, steps: [{script: x}]}\n",
        "dependency cycle involving job 'self'");

    //Stages: one depends on three; two and three depend on the previous stage (implicit).
    expect_error(
        "stages:\n"
        "- {stage: one, dependsOn: three, jobs: [{job: a, steps: [{script: x}]}]}\n"
        "- {stage: two, jobs: [{job: a, steps: [{script: x}]}]}\n"
        "- {stage: three, jobs: [{job: a, steps: [{script: x}]}]}\n",
        "stages: dependency cycle");

    //No cycle: d reaches a through b and through c (a diamond), which is allowed.
    error_list_t errors = {0};
    pipeline_t *pipeline = parse(
        "stages:\n- stage: s\n  jobs:\n"
        "  - {job: a, steps: [{script: x}]}\n"
        "  - {job: b, dependsOn: a, steps: [{script: x}]}\n"
        "  - {job: c, dependsOn: a, steps: [{script: x}]}\n"
        "  - {job: d, dependsOn: [b, c], steps: [{script: x}]}\n",
        &errors);

    CHECK(pipeline != NULL && errors.count == 0);

    pipeline_free(pipeline);
    error_list_free(&errors);
}



//ALL THE ERRORS AT ONCE
static void test_all_errors_reported(void){
    printf("parser and validator errors in one answer\n");
    error_list_t errors = {0};
    pipeline_t *pipeline = parse(
        "stages:\n"
        "- stage: build\n"
        "  displayName: Build\n"
        "  jobs: [{job: a, steps: [{script: x}]}]\n"
        "- stage: build\n"
        "  jobs: [{job: b, steps: [{script: x}]}]\n",
        &errors);

    CHECK(errors.count == 2);
    CHECK(has_error(&errors, "unknown key 'displayName'"));
    CHECK(has_error(&errors, "duplicate stage name 'build'"));

    pipeline_free(pipeline);
    error_list_free(&errors);
}



int main(void){
    test_loading();
    test_valid_pipeline();
    test_structure_errors();
    test_value_errors();
    test_unknown_keys();
    test_duplicates_and_references();
    test_cycles();
    test_all_errors_reported();

    printf("\n%d of %d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures != 0;
}