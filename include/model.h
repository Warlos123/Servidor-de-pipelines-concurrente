#ifndef MODEL_H
#define MODEL_H
#include "logstore.h"


#include <omp.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

//Value returns index of search in case of failure
#define NO_INDEX SIZE_MAX

typedef enum {
    ST_QUEUED,
    ST_RUNNING,
    ST_SUCCEEDED,
    ST_PARTIALLY_SUCCEEDED,
    ST_FAILED,
    ST_CANCELED,
    ST_SKIPPED,
    ST_COUNT 
} status_t;

typedef enum {
    COND_SUCCEEDED, 
    COND_FAILED,   
    COND_ALWAYS     
} condition_t;

typedef enum {
    STEP_SCRIPT,
    STEP_TASK,
    STEP_PUBLISH,
    STEP_DOWNLOAD
} step_kind_t;

typedef struct {
    char *key;
    char *value;
} kv_t;

typedef struct {
    step_kind_t kind;
    char *script;       // STEP_SCRIPT: shell command
    char *task;         // STEP_TASK: "WordCount@1", etc. 
    kv_t *inputs;       // STEP_TASK: map inputs 
    size_t n_inputs;
    char *path;         // STEP_PUBLISH: route to publish / STEP_DOWNLOAD: destination 
    char *artifact;     // artifact's name (publish y download)
    bool continue_on_error;
    int line;           // line of YAML for message error
} step_def_t;

typedef struct {
    char *name;
    char **depends_on;      // Worker's names in the same stage 
    size_t n_depends_on;
    bool has_depends_on;    // flase = dependsOn wasnt writen on; true n = 0 = empty list
    condition_t condition;
    int timeout_seconds;    // 0 = no limit
    step_def_t *steps;
    size_t n_steps;
} job_def_t;

typedef struct {
    char *name;
    char **depends_on;      // stages names 
    size_t n_depends_on;
    bool has_depends_on;    // flase = depends on the previous stage 
    condition_t condition;
    job_def_t *jobs;
    size_t n_jobs;
} stage_def_t;

typedef struct {
    char *name;
    kv_t *variables;
    size_t n_variables;
    stage_def_t *stages;
    size_t n_stages;
} pipeline_t;



typedef struct {
    char  *key;
    char  *value;
} var_entry_t;

typedef struct {
    var_entry_t *entries;
    size_t       count;
    size_t       capacity;
} var_map_t;

typedef struct {
    status_t status;            
    struct timespec start;      
    struct timespec end;        
} step_rt_t;

typedef struct {
    const job_def_t *def;       
    size_t stage;               

    size_t *deps;               // global index for worker with dependency
    size_t n_deps;
    size_t *dependents;        
    size_t n_dependents;
    atomic_size_t pending;      // pending dependencies

    status_t status;           
    struct timespec start;      
    struct timespec end;        
    step_rt_t *steps;         
    job_log_t log;  
    var_map_t vars;             // variables the job exported; written before it finishes
} job_rt_t;

typedef struct {
    status_t status;            
    struct timespec start;      
    struct timespec end;        
} stage_rt_t;

typedef struct {
    uint64_t id;
    pipeline_t *pipeline;      

    omp_lock_t lock;           
    atomic_bool cancel_requested;

    status_t status;            
    struct timespec created;
    struct timespec start;     
    struct timespec end;        

    stage_rt_t *stages;         
    size_t n_stages;

    job_rt_t *jobs;
    size_t n_jobs;
    size_t jobs_finished;      
} run_t;


run_t *run_create(uint64_t id, pipeline_t *pipeline);

void run_destroy(run_t *run);

void pipeline_free(pipeline_t *pipeline);

size_t run_find_stage(const run_t *run, const char *stage_name);

size_t run_find_job(const run_t *run, size_t stage, const char *job_name);

#endif