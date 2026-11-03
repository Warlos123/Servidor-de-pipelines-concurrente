#define _POSIX_C_SOURCE 200809L

#include "yaml_parser.h"

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
    return yaml_parse_pipeline(text, strlen(text), errors);
}


static int first_error_has(const error_list_t *errors, const char *piece){
    return errors->count > 0 && strstr(errors->items[0], piece) != NULL;
}


/* 
The following block of code was partially made using: 
Anthropic (2026). Claude(Opus 5.5) https://claude.ai
*/

static void test_valid_map(void){
    printf("valid map\n");
    error_list_t errors = {0};
    pipeline_t *pipeline = parse("name: demo\nstages: []\n", &errors);

    CHECK(pipeline != NULL);
    CHECK(errors.count == 0);

    pipeline_free(pipeline);
    error_list_free(&errors);
}


static void test_empty(void){
    printf("empty YAML\n");
    error_list_t errors = {0};
    pipeline_t *pipeline = parse("", &errors);

    CHECK(pipeline == NULL);
    CHECK(errors.count == 1);
    CHECK(first_error_has(&errors, "empty"));

    error_list_free(&errors);

    //Only comment
    pipeline = parse("# nothing here\n", &errors);

    CHECK(pipeline == NULL);
    CHECK(first_error_has(&errors, "empty"));

    error_list_free(&errors);
}


static void test_tab(void){
    printf("tab used as indentation\n");
    error_list_t errors = {0};
    pipeline_t *pipeline = parse("stages:\n\t- stage: a\n", &errors);

    CHECK(pipeline == NULL);
    CHECK(errors.count == 1);
    CHECK(first_error_has(&errors, "line 2: invalid YAML"));

    error_list_free(&errors);
}


static void test_bad_indentation(void){
    printf("bad indentation\n");
    error_list_t errors = {0};
    pipeline_t *pipeline = parse("stages:\n  - stage: a\n   jobs: x\n", &errors);

    CHECK(pipeline == NULL);
    CHECK(first_error_has(&errors, "line 3: invalid YAML"));

    error_list_free(&errors);
}


static void test_root_not_map(void){
    printf("root is not a map\n");
    error_list_t errors = {0};

    //A single value.
    pipeline_t *pipeline = parse("hello\n", &errors);

    CHECK(pipeline == NULL);
    CHECK(first_error_has(&errors, "line 1: the pipeline must be a map"));

    error_list_free(&errors);

    //A list.
    pipeline = parse("- stage: a\n- stage: b\n", &errors);

    CHECK(pipeline == NULL);
    CHECK(first_error_has(&errors, "must be a map"));

    error_list_free(&errors);
}


int main(void){
    test_valid_map();
    test_empty();
    test_tab();
    test_bad_indentation();
    test_root_not_map();

    printf("\n%d of %d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures != 0;
}