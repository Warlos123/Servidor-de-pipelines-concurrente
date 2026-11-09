#ifndef JOB_CONTEXT_H
#define JOB_CONTEXT_H

#include "model.h"
#include "logstore.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>


#define VAR_MAP_INITIAL 16  



typedef struct {

    uint64_t  run_id;
    size_t    job_index;      


    char     *workspace;      

    
    var_map_t vars;

   
    job_log_t *log;           


    atomic_bool *cancel_flag; 

  
    int timeout_seconds;
    struct timespec started;
} job_context_t;




const char *var_map_get(const var_map_t *m, const char *key);


int var_map_set(var_map_t *m, const char *key, const char *value);


void var_map_destroy(var_map_t *m);

bool job_timed_out(const job_context_t *ctx);

#endif 