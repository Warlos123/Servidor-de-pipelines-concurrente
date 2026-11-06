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

#define TEST_WORKSPACE "/tmp/pipelines_det_test"

/*
 * The test generates its own deterministic input file: a LCG picks one of a
 * few hundred common words for every position, so every run of the suite
 * behaves the same and no repository file is needed.
 */
#define DATA_BYTES (256 * 1024)

static char data_file[sizeof TEST_WORKSPACE "/data.txt"];

static int write_data_file(void)
{
    static const char *words[] = {
        "alpha",  "beta",   "gamma",  "delta", "epsilon", "zeta", "eta",
        "theta",  "iota",   "kappa",  "luna",  "mango",   "nube", "oro",
        "pato",   "queso",  "roble",  "sal",   "tierra",  "uva",  "viento",
        "xilofon", "yema",  "zorro",  "agua",  "brasa",   "cielo", "dedo",
        "eco",    "flecha", "gota",   "huevo", "isla",    "jugo", "kilo",
    };
    int n_words = (int)(sizeof words / sizeof words[0]);

    FILE *f = fopen(data_file, "wb");
    if (f == NULL) {
        return -1;
    }

    unsigned long long state = 12345ULL;

    while (ftell(f) < DATA_BYTES) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const char *w = words[(state >> 33) % (unsigned)n_words];
        fputs(w, f);

        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        if ((state >> 33) % 8u == 0) {
            fputc('\n', f);
        } else {
            fputc(' ', f);
        }
    }

    fclose(f);
    return 0;
}

static int compare_files(const char *f1, const char *f2) {
    FILE *p1 = fopen(f1, "rb");
    FILE *p2 = fopen(f2, "rb");
    if (!p1 || !p2) {
        if (p1) fclose(p1);
        if (p2) fclose(p2);
        return -1;
    }

    while (1) {
        int c1 = fgetc(p1);
        int c2 = fgetc(p2);
        if (c1 != c2) {
            fclose(p1);
            fclose(p2);
            return -1;
        }
        if (c1 == EOF) break;
    }

    fclose(p1);
    fclose(p2);
    return 0;
}

int main(void) {
    mkdir(TEST_WORKSPACE, 0755);
    snprintf(data_file, sizeof data_file, "%s/data.txt", TEST_WORKSPACE);

    int total_failures = 0;

    if (write_data_file() != 0) {
        perror("write_data_file");
        return 1;
    }

    job_log_t log;
    log_init(&log);
    atomic_bool cancel = false;

    job_context_t ctx = {
        .run_id = 1,
        .job_index = 0,
        .workspace = TEST_WORKSPACE,
        .log = &log,
        .cancel_flag = &cancel,
        .timeout_seconds = 0,
        .started = {0, 0}
    };
    clock_gettime(CLOCK_MONOTONIC, &ctx.started);

    char threads[4][8] = {"1", "2", "4", "8"};
    char wc_outputs[4][256];
    char hist_outputs[4][256];

    for (int i = 0; i < 4; i++) {
        snprintf(wc_outputs[i], sizeof wc_outputs[i],
                 "%s/wc_%d.txt", TEST_WORKSPACE, 1 << i);
        snprintf(hist_outputs[i], sizeof hist_outputs[i],
                 "%s/hist_%d.csv", TEST_WORKSPACE, 1 << i);
    }

    printf("Running WordCount determinism test...\n");
    for (int i = 0; i < 4; i++) {
        kv_t inputs[] = {
            {"file", data_file},
            {"threads", threads[i]},
            {"output", wc_outputs[i]},
            {"top", "5"}
        };
        printf("  Thread count: %s\n", threads[i]);
        if (task_wordcount(inputs, 4, &ctx) != 0) {
            fprintf(stderr, "WordCount failed for %s threads\n", threads[i]);
            fprintf(stderr, "Log:\n%.*s\n", (int)log.used, (char *)log.buf);
            log_destroy(&log);
            return 1;
        }
    }

    printf("Running Histogram determinism test...\n");
    for (int i = 0; i < 4; i++) {
        kv_t inputs[] = {
            {"file", data_file},
            {"threads", threads[i]},
            {"output", hist_outputs[i]},
            {"bins", "16"}
        };
        printf("  Thread count: %s\n", threads[i]);
        if (task_histogram(inputs, 4, &ctx) != 0) {
            fprintf(stderr, "Histogram failed for %s threads\n", threads[i]);
            fprintf(stderr, "Log:\n%.*s\n", (int)log.used, (char *)log.buf);
            log_destroy(&log);
            return 1;
        }
    }

    printf("Comparing WordCount outputs...\n");
    for (int i = 1; i < 4; i++) {
        if (compare_files(wc_outputs[0], wc_outputs[i]) != 0) {
            printf("  FAIL: wc_1.txt and wc_%s.txt differ!\n", threads[i]);
            total_failures++;
        } else {
            printf("  OK: wc_1.txt and wc_%s.txt are identical.\n", threads[i]);
        }
    }

    printf("Comparing Histogram outputs...\n");
    for (int i = 1; i < 4; i++) {
        if (compare_files(hist_outputs[0], hist_outputs[i]) != 0) {
            printf("  FAIL: hist_1.csv and hist_%s.csv differ!\n", threads[i]);
            total_failures++;
        } else {
            printf("  OK: hist_1.csv and hist_%s.csv are identical.\n", threads[i]);
        }
    }

    //N times Top: with distinct word counts the top list has a single answer.
    char wc_top[4][256];
    for (int i = 0; i < 4; i++) {
        snprintf(wc_top[i], sizeof wc_top[i], "%s/wct_%d.txt",
                 TEST_WORKSPACE, 1 << i);
        kv_t inputs[] = {
            {"file", data_file},
            {"threads", threads[i]},
            {"output", wc_top[i]},
            {"top", "3"}
        };
        if (task_wordcount(inputs, 4, &ctx) != 0) {
            fprintf(stderr, "WordCount(top) failed for %s threads\n", threads[i]);
            log_destroy(&log);
            return 1;
        }
    }

    printf("Comparing WordCount top-3 outputs...\n");
    for (int i = 1; i < 4; i++) {
        if (compare_files(wc_top[0], wc_top[i]) != 0) {
            printf("  FAIL: wct_1.txt and wct_%s.txt differ!\n", threads[i]);
            total_failures++;
        } else {
            printf("  OK: wct_1.txt and wct_%s.txt are identical.\n", threads[i]);
        }
    }

    log_destroy(&log);

    if (total_failures == 0) {
        printf("\nSUCCESS: All tasks are deterministic.\n");
        return 0;
    }

    printf("\nFAIL: Determinism check failed with %d mismatches.\n", total_failures);
    return 1;
}
