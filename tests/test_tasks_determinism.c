#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "builtin_tasks.h"
#include "job_context.h"
#include "logstore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_FILE "tests/determinism_data.txt"
#define TEST_WORKSPACE "/tmp/pipelines_det_test"
// I will change the path in the task input instead.

static int compare_files(const char *f1, const char *f2) {
    FILE *p1 = fopen(f1, "rb");
    FILE *p2 = fopen(f2, "rb");
    if (!p1 || !p2) {
        if (p1) fclose(p1);
        if (p2) fclose(p2);
        return -1;
    }

    int res = 0;
    while (1) {
        int c1 = fgetc(p1);
        int c2 = fgetc(p2);
        if (c1 != c2) {
            res = 1;
            break;
        }
        if (c1 == EOF) break;
    }

    fclose(p1);
    fclose(p2);
    return res;
}

int main(void) {
    // Setup workspace
    mkdir(TEST_WORKSPACE, 0755);

    char *abs_test_file = realpath(TEST_FILE, NULL);
    if (!abs_test_file) {
        perror("realpath");
        return 1;
    }
    printf("Absolute test file path: %s\n", abs_test_file);

    job_log_t log;
    log_init(&log);
    atomic_bool cancel = false;
    
    job_context_t ctx = {
        .run_id = 1,
        .job_index = 0,
        .workspace = TEST_WORKSPACE,
        .log = &log,
        .cancel_flag = &cancel,
        .timeout_seconds = 0
    };
    memset(&ctx.vars, 0, sizeof(ctx.vars));

    const char *threads[] = {"1", "2", "4", "8"};
    const char *wc_outputs[] = {
        TEST_WORKSPACE "/wc_1.txt",
        TEST_WORKSPACE "/wc_2.txt",
        TEST_WORKSPACE "/wc_4.txt",
        TEST_WORKSPACE "/wc_8.txt"
    };
    const char *hist_outputs[] = {
        TEST_WORKSPACE "/hist_1.csv",
        TEST_WORKSPACE "/hist_2.csv",
        TEST_WORKSPACE "/hist_4.csv",
        TEST_WORKSPACE "/hist_8.csv"
    };

    printf("Running WordCount determinism test...\n");
    for (int i = 0; i < 4; i++) {
        kv_t inputs[] = {
            {"file", abs_test_file},
            {"threads", threads[i]},
            {"output", wc_outputs[i]},
            {"top", "5"}
        };
        fprintf(stderr, "  Thread count: %s\n", threads[i]);
        fprintf(stderr, "Inputs: file=%s, threads=%s, out=%s\n", 
            task_input_get(inputs, 4, "file"),
            task_input_get(inputs, 4, "threads"),
            task_input_get(inputs, 4, "output"));
        fprintf(stderr, "Before task_wordcount\n");
        if (task_wordcount(inputs, 4, &ctx) != 0) {
            fprintf(stderr, "WordCount failed for %s threads\n", threads[i]);
            fprintf(stderr, "Log: %.*s\n", (int)log.used, (char *)log.buf);
            return 1;
        }
    }

    printf("Running Histogram determinism test...\n");
    for (int i = 0; i < 4; i++) {
        kv_t inputs[] = {
            {"file", abs_test_file},
            {"threads", threads[i]},
            {"output", hist_outputs[i]},
            {"bins", "16"}
        };
        printf("  Thread count: %s\n", threads[i]);
        if (task_histogram(inputs, 4, &ctx) != 0) {
            fprintf(stderr, "Histogram failed for %s threads\n", threads[i]);
            return 1;
        }
    }

    int failures = 0;
    printf("Comparing WordCount outputs...\n");
    for (int i = 1; i < 4; i++) {
        if (compare_files(wc_outputs[0], wc_outputs[i]) != 0) {
            printf("  FAIL: wc_1.txt and wc_%s.txt differ!\n", threads[i]);
            failures++;
        } else {
            printf("  OK: wc_1.txt and wc_%s.txt are identical.\n", threads[i]);
        }
    }

    printf("Comparing Histogram outputs...\n");
    for (int i = 1; i < 4; i++) {
        if (compare_files(hist_outputs[0], hist_outputs[i]) != 0) {
            printf("  FAIL: hist_1.csv and hist_%s.csv differ!\n", threads[i]);
            failures++;
        } else {
            printf("  OK: hist_1.csv and hist_%s.csv are identical.\n", threads[i]);
        }
    }

    log_destroy(&log);
    
    if (failures == 0) {
        printf("\nSUCCESS: All tasks are deterministic.\n");
        return 0;
    } else {
        printf("\nFAIL: Determinism check failed with %d mismatches.\n", failures);
        return 1;
    }
}
