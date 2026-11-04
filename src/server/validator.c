#define _POSIX_C_SOURCE 200809L

#include "validator.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>


//VALIDATOR HELPERS

static bool same_name(const char *a, const char *b){
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}


//Index of the stage called 'name'
static size_t find_stage(const pipeline_t *pipeline, const char *name){
    for(size_t s = 0; s < pipeline->n_stages; s++){
        if(same_name(pipeline->stages[s].name, name))
            return s;
    }

    return NO_INDEX;
}


//Index of the job called 'name' 
static size_t find_job(const stage_def_t *stage, const char *name){
    for(size_t j = 0; j < stage->n_jobs; j++){
        if(same_name(stage->jobs[j].name, name))
            return j;
    }

    return NO_INDEX;
}


//DUPLICATE NAMES

//Each stage name is compared 
static void check_duplicate_stages(const pipeline_t *pipeline, error_list_t *errors){
    for(size_t s = 0; s < pipeline->n_stages; s++){
        for(size_t t = 0; t < s; t++){
            if(same_name(pipeline->stages[s].name, pipeline->stages[t].name)){
                error_list_add(errors, "stages[%zu]: duplicate stage name '%s'", s, pipeline->stages[s].name);
                break;
            }
        }
    }
}


//Job names only have to be unique inside their own stage.
static void check_duplicate_jobs(const stage_def_t *stage, size_t s, error_list_t *errors){
    for(size_t j = 0; j < stage->n_jobs; j++){
        for(size_t k = 0; k < j; k++){
            if(same_name(stage->jobs[j].name, stage->jobs[k].name)){
                error_list_add(errors, "stages[%zu].jobs[%zu]: duplicate job name '%s'", s, j, stage->jobs[j].name);
                break;
            }
        }
    }
}



//REFERENCES

static void check_stage_references(const pipeline_t *pipeline, size_t s, error_list_t *errors){
    const stage_def_t *stage = &pipeline->stages[s];

    for(size_t i = 0; i < stage->n_depends_on; i++){
        const char *name = stage->depends_on[i];

        if(name != NULL && find_stage(pipeline, name) == NO_INDEX)
            error_list_add(errors, "stages[%zu].dependsOn: unknown stage '%s'", s, name);
    }
}


static void check_job_references(const stage_def_t *stage, size_t s, error_list_t *errors){
    for(size_t j = 0; j < stage->n_jobs; j++){
        const job_def_t *job = &stage->jobs[j];

        for(size_t i = 0; i < job->n_depends_on; i++){
            const char *name = job->depends_on[i];

            if(name != NULL && find_job(stage, name) == NO_INDEX)
                error_list_add(errors, "stages[%zu].jobs[%zu].dependsOn: unknown job '%s' in this stage", s, j, name);
        }
    }
}


//CYCLES

/*
    DFS with:
    UNVISITED: not reached yet
    VISITING: on the current path
    DONE: no cycle
*/

typedef enum {
    UNVISITED,
    VISITING,
    DONE
} color_t;


static const char *display_name(const char *name){
    if(name == NULL)
        return "?";

    return name;
}

static void visit_stage(const pipeline_t *pipeline, size_t s, color_t *colors, error_list_t *errors){
    const stage_def_t *stage = &pipeline->stages[s];
    colors[s] = VISITING;

    size_t n_deps = stage->n_depends_on;

    if(!stage->has_depends_on){
        n_deps = 0;

        if(s > 0)
            n_deps = 1;
    }

    for(size_t i = 0; i < n_deps; i++){
        size_t t = s - 1;

        if(stage->has_depends_on)
            t = find_stage(pipeline, stage->depends_on[i]);

        //Unknown name
        if(t == NO_INDEX)
            continue;

        if(colors[t] == VISITING)
            error_list_add(errors, "stages: dependency cycle involving stage '%s'", display_name(pipeline->stages[t].name));

        else if(colors[t] == UNVISITED)
            visit_stage(pipeline, t, colors, errors);
    }

    colors[s] = DONE;
}


static void visit_job(const stage_def_t *stage, size_t s, size_t j, color_t *colors, error_list_t *errors){
    const job_def_t *job = &stage->jobs[j];
    colors[j] = VISITING;

    for(size_t i = 0; i < job->n_depends_on; i++){
        size_t k = find_job(stage, job->depends_on[i]);

        if(k == NO_INDEX)
            continue;

        if(colors[k] == VISITING)
            error_list_add(errors, "stages[%zu].jobs: dependency cycle involving job '%s'", s, display_name(stage->jobs[k].name));

        else if(colors[k] == UNVISITED)
            visit_job(stage, s, k, colors, errors);
    }

    colors[j] = DONE;
}



static void check_stage_cycles(const pipeline_t *pipeline, error_list_t *errors){
    if(pipeline->n_stages == 0)
        return;

    //every stage starts UNVISITED (0).
    color_t *colors = calloc(pipeline->n_stages, sizeof *colors);

    if(colors == NULL){
        error_list_add(errors, "out of memory while validating");
        return;
    }

    for(size_t s = 0; s < pipeline->n_stages; s++){
        if(colors[s] == UNVISITED)
            visit_stage(pipeline, s, colors, errors);
    }

    free(colors);
}


static void check_job_cycles(const stage_def_t *stage, size_t s, error_list_t *errors){
    if(stage->n_jobs == 0)
        return;

    //every job starts UNVISITED (0).
    color_t *colors = calloc(stage->n_jobs, sizeof *colors);

    if(colors == NULL){
        error_list_add(errors, "out of memory while validating");
        return;
    }

    for(size_t j = 0; j < stage->n_jobs; j++){
        if(colors[j] == UNVISITED)
            visit_job(stage, s, j, colors, errors);
    }

    free(colors);
}


//VALIDATOR

void validate_pipeline(const pipeline_t *pipeline, error_list_t *errors){
    check_duplicate_stages(pipeline, errors);

    for(size_t s = 0; s < pipeline->n_stages; s++){
        check_duplicate_jobs(&pipeline->stages[s], s, errors);
        check_stage_references(pipeline, s, errors);
        check_job_references(&pipeline->stages[s], s, errors);
        check_job_cycles(&pipeline->stages[s], s, errors);
    }

    check_stage_cycles(pipeline, errors);
}