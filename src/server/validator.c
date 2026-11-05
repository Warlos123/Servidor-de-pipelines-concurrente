#define _POSIX_C_SOURCE 200809L

#include "validator.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>


static bool same_name(const char *a, const char *b){
    return a != NULL && b != NULL && strcmp(a, b) == 0;
}


static size_t find_stage(const pipeline_t *pipeline, const char *name){
    for(size_t s = 0; s < pipeline->n_stages; s++){
        if(same_name(pipeline->stages[s].name, name))
            return s;
    }

    return NO_INDEX;
}


static size_t find_job(const stage_def_t *stage, const char *name){
    for(size_t j = 0; j < stage->n_jobs; j++){
        if(same_name(stage->jobs[j].name, name))
            return j;
    }

    return NO_INDEX;
}

static size_t stage_dependency_count(const pipeline_t *pipeline, size_t s){
    const stage_def_t *stage = &pipeline->stages[s];
 
    if(stage->has_depends_on)
        return stage->n_depends_on;
 
    if(s > 0)
        return 1;
 
    return 0;
}
 
static size_t stage_dependency(const pipeline_t *pipeline, size_t s, size_t i){
    const stage_def_t *stage = &pipeline->stages[s];
 
    if(stage->has_depends_on)
        return find_stage(pipeline, stage->depends_on[i]);
 
    return s - 1;
}


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


/* DFS states: UNVISITED, VISITING (on the current path) and DONE. */

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

    colors[s] = VISITING;

    size_t n_deps = stage_dependency_count(pipeline, s);

    
    for(size_t i = 0; i < n_deps; i++){
        size_t t = stage_dependency(pipeline, s, i);

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


static size_t global_index(const pipeline_t *pipeline, size_t s, size_t j){
    size_t g = 0;
 
    for(size_t t = 0; t < s; t++)
        g += pipeline->stages[t].n_jobs;
 
    return g + j;
}



static void mark_ancestors(const pipeline_t *pipeline, size_t s, size_t j, bool *reached){
    const stage_def_t *stage = &pipeline->stages[s];
    const job_def_t *job = &stage->jobs[j];
 
    //Jobs of the same stage named in dependsOn.
    for(size_t i = 0; i < job->n_depends_on; i++){
        size_t k = find_job(stage, job->depends_on[i]);
 
        if(k == NO_INDEX)
            continue;
 
        size_t g = global_index(pipeline, s, k);
 
        if(!reached[g]){
            reached[g] = true;
            mark_ancestors(pipeline, s, k, reached);
        }
    }
 
    //Every job of every stage this stage depends on.
    size_t n_deps = stage_dependency_count(pipeline, s);
 
    for(size_t i = 0; i < n_deps; i++){
        size_t t = stage_dependency(pipeline, s, i);
 
        if(t == NO_INDEX)
            continue;
 
        for(size_t k = 0; k < pipeline->stages[t].n_jobs; k++){
            size_t g = global_index(pipeline, t, k);
 
            if(!reached[g]){
                reached[g] = true;
                mark_ancestors(pipeline, t, k, reached);
            }
        }
    }
}



typedef struct {
    size_t s; //stage
    size_t j; //job
    size_t k; //step
} step_position_t;



static bool find_publisher(const pipeline_t *pipeline, const char *name, step_position_t *found){
    for(size_t s = 0; s < pipeline->n_stages; s++){
        const stage_def_t *stage = &pipeline->stages[s];
 
        for(size_t j = 0; j < stage->n_jobs; j++){
            const job_def_t *job = &stage->jobs[j];
 
            for(size_t k = 0; k < job->n_steps; k++){
                if(job->steps[k].kind == STEP_PUBLISH && same_name(job->steps[k].artifact, name)){
                    found->s = s;
                    found->j = j;
                    found->k = k;
                    return true;
                }
            }
        }
    }
 
    return false;
}
 


static void check_publish(const pipeline_t *pipeline, step_position_t at, error_list_t *errors){
    const step_def_t *step = &pipeline->stages[at.s].jobs[at.j].steps[at.k];
    step_position_t first;
 
    if(!find_publisher(pipeline, step->artifact, &first))
        return;
 
    //The first publisher is another step
    if(first.s != at.s || first.j != at.j || first.k != at.k){
        const step_def_t *other = &pipeline->stages[first.s].jobs[first.j].steps[first.k];
        error_list_add(errors, "line %d: artifact '%s' is already published at line %d", step->line, step->artifact, other->line);
    }
}



static void check_download(const pipeline_t *pipeline, step_position_t at, bool *reached, size_t n_jobs, error_list_t *errors){
    const step_def_t *step = &pipeline->stages[at.s].jobs[at.j].steps[at.k];
    step_position_t publisher;
 
    if(!find_publisher(pipeline, step->artifact, &publisher)){
        error_list_add(errors, "line %d: download of unknown artifact '%s'", step->line, step->artifact);
        return;
    }
 

    if(publisher.s == at.s && publisher.j == at.j){
        if(publisher.k > at.k){
            const step_def_t *publish = &pipeline->stages[at.s].jobs[at.j].steps[publisher.k];
            error_list_add(errors, "line %d: artifact '%s' is published later in this job (line %d)", step->line, step->artifact, publish->line);
        }
 
        return;
    }
 
    memset(reached, 0, n_jobs * sizeof *reached);
    mark_ancestors(pipeline, at.s, at.j, reached);
 
    if(!reached[global_index(pipeline, publisher.s, publisher.j)])
        error_list_add(errors, "line %d: artifact '%s' is published by job '%s', which doesn't always finish before this step", step->line, step->artifact, display_name(pipeline->stages[publisher.s].jobs[publisher.j].name));
}



static void check_artifacts(const pipeline_t *pipeline, error_list_t *errors){
    size_t n_jobs = global_index(pipeline, pipeline->n_stages, 0);
 
    if(n_jobs == 0)
        return;
 
    //One flag per job, reused by every download to mark its ancestors.
    bool *reached = calloc(n_jobs, sizeof *reached);
 
    if(reached == NULL){
        error_list_add(errors, "out of memory while validating");
        return;
    }
 
    for(size_t s = 0; s < pipeline->n_stages; s++){
        const stage_def_t *stage = &pipeline->stages[s];
 
        for(size_t j = 0; j < stage->n_jobs; j++){
            const job_def_t *job = &stage->jobs[j];
 
            for(size_t k = 0; k < job->n_steps; k++){
                step_position_t at = {s, j, k};
 
                if(job->steps[k].artifact == NULL)
                    continue;
 
                if(job->steps[k].kind == STEP_PUBLISH)
                    check_publish(pipeline, at, errors);
 
                else if(job->steps[k].kind == STEP_DOWNLOAD)
                    check_download(pipeline, at, reached, n_jobs, errors);
            }
        }
    }
 
    free(reached);
}


void validate_pipeline(const pipeline_t *pipeline, error_list_t *errors){
    check_duplicate_stages(pipeline, errors);

    for(size_t s = 0; s < pipeline->n_stages; s++){
        check_duplicate_jobs(&pipeline->stages[s], s, errors);
        check_stage_references(pipeline, s, errors);
        check_job_references(&pipeline->stages[s], s, errors);
        check_job_cycles(&pipeline->stages[s], s, errors);
    }

    check_stage_cycles(pipeline, errors);
    check_artifacts(pipeline, errors);
}