#define _POSIX_C_SOURCE 200809L

#include "job_execute.h"

// Placeholder until the real step runner.
status_t execute_job(run_t *run, size_t job, void *work_directory){
    (void)run;
    (void)job;
    (void)work_directory;
    return ST_FAILED;
}