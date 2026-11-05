#define _POSIX_C_SOURCE 200809L

#include "yaml_parser.h"

#include <yaml.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>


#define MESSAGE_MAX 256

//ERROR LIST
void error_list_add(error_list_t *errors, const char *format, ...){
    char message[MESSAGE_MAX];

    //variable arguments (...) 
    va_list args;
    va_start(args, format);
    //va_list, writes in string, max size n
    vsnprintf(message, sizeof message, format, args);
    va_end(args);

    //Create new slot in array of messages.
    //realloc copies an existing block into a new size block with the same content.
    char **items = realloc(errors->items, (errors->count + 1) * sizeof *items);

    if(items == NULL)
        return;

    errors->items = items;

    char *copy = strdup(message);

    if(copy == NULL)
        return;

    errors->items[errors->count] = copy;
    errors->count++;
}

void error_list_free(error_list_t *errors){
    for(size_t i = 0; i < errors->count; i++)
        free(errors->items[i]);

    free(errors->items);
    errors->items = NULL;
    errors->count = 0;
}



//PARSER HELPERS

typedef struct {
    yaml_document_t *document; 
    error_list_t *errors;
} parser_state_t;

//Turns an id into the node
static yaml_node_t *get_node(parser_state_t *state, int id){
    return yaml_document_get_node(state->document, id);
}

//Text of a scalar node
static const char *text_of(const yaml_node_t *node){
    return (const char *)node->data.scalar.value;
}

//Adjust count
static int line_of(const yaml_node_t *node){
    return (int)node->start_mark.line + 1;
}

//Value of the key name in a mapping ( key : value )
static yaml_node_t *find_value(parser_state_t *state, const yaml_node_t *mapping, const char *name){
     size_t n_pairs = (size_t)(mapping->data.mapping.pairs.top - mapping->data.mapping.pairs.start);

    for(size_t i = 0; i < n_pairs; i++){
        yaml_node_t *key = get_node(state, mapping->data.mapping.pairs.start[i].key);

        if(key->type == YAML_SCALAR_NODE && strcmp(text_of(key), name) == 0)
            return get_node(state, mapping->data.mapping.pairs.start[i].value);
    }

    return NULL;
}

//Copy text into our memory
static char *copy_text(parser_state_t *state, const yaml_node_t *node){
    char *copy = strdup(text_of(node));

    if(copy == NULL)
        error_list_add(state->errors, "out of memory while reading the YAML");

    return copy;
}

//Allowed keys
static const char *const PIPELINE_KEYS[] = {"name", "variables", "stages"};
static const char *const STAGE_KEYS[] = {"stage", "dependsOn", "condition", "jobs"};
static const char *const JOB_KEYS[] = {"job", "dependsOn", "condition", "timeoutInSeconds", "steps"};
static const char *const STEP_KEYS[] = {"script", "task", "inputs", "publish", "artifact", "download", "path", "continueOnError"};

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

//Reports key that is not allowed
static void check_keys(parser_state_t *state, const yaml_node_t *mapping, const char *const *allowed, size_t n_allowed, const char *level){
    size_t n_pairs = (size_t)(mapping->data.mapping.pairs.top - mapping->data.mapping.pairs.start);

    for(size_t i = 0; i < n_pairs; i++){
        yaml_node_t *key = get_node(state, mapping->data.mapping.pairs.start[i].key);

        if(key->type != YAML_SCALAR_NODE){
            error_list_add(state->errors, "line %d: keys in %s must be names", line_of(key), level);
            continue;
        }

        bool known = false;

        for(size_t k = 0; k < n_allowed; k++){
            if(strcmp(text_of(key), allowed[k]) == 0)
                known = true;
        }

        if(!known)
            error_list_add(state->errors, "line %d: unknown key '%s' in %s", line_of(key), text_of(key), level);
    }
}


//Size of list (stages, jobs, steps)
static size_t list_size(parser_state_t *state, const yaml_node_t *node, const char *key){
    if(node->type != YAML_SEQUENCE_NODE){
        error_list_add(state->errors, "line %d: '%s' must be a list", line_of(node), key);
        return 0;
    }

    size_t count = (size_t)(node->data.sequence.items.top - node->data.sequence.items.start);

    if(count == 0)
        error_list_add(state->errors, "line %d: '%s' can't be empty", line_of(node), key);

    return count;
}

//Required name (stage, job)
static char *parse_name(parser_state_t *state, const yaml_node_t *mapping, const char *key){
    yaml_node_t *value = find_value(state, mapping, key);

    //Missing key
    if(value == NULL){
        error_list_add(state->errors, "line %d: missing '%s' (the name)", line_of(mapping), key);
        return NULL;
    }

    if(value->type != YAML_SCALAR_NODE || value->data.scalar.length == 0){
        error_list_add(state->errors, "line %d: '%s' must be a name", line_of(value), key);
        return NULL;
    }

    return copy_text(state, value);
}


//Required text (script, task, publish, artifact, download, path)
static char *parse_text(parser_state_t *state, const yaml_node_t *value, const char *key){
    if(value->type != YAML_SCALAR_NODE || value->data.scalar.length == 0){
        error_list_add(state->errors, "line %d: '%s' must be a single value", line_of(value), key);
        return NULL;
    }

    return copy_text(state, value);
}


//Reads a map of name, value (task inputsm, pipeline variables) into an array of kv_t entreis (model.h)
static void parse_key_value_map(parser_state_t *state, const yaml_node_t *node, const char *key, kv_t **entries, size_t *entry_count){
    if(node->type != YAML_MAPPING_NODE){
        error_list_add(state->errors, "line %d: '%s' must be a map of name: value", line_of(node), key);
        return;
    }

    size_t count = (size_t)(node->data.mapping.pairs.top - node->data.mapping.pairs.start);

    if(count == 0)
        return;

    //One kv_t per pair
    *entries = calloc(count, sizeof **entries);

    if(*entries == NULL){
        error_list_add(state->errors, "out of memory while reading the YAML");
        return;
    }

    *entry_count = count;

    for(size_t i = 0; i < count; i++){
        yaml_node_t *name = get_node(state, node->data.mapping.pairs.start[i].key);
        yaml_node_t *value = get_node(state, node->data.mapping.pairs.start[i].value);

        //Each entry must be "name: value"
        if(name->type != YAML_SCALAR_NODE || value->type != YAML_SCALAR_NODE){
            error_list_add(state->errors, "line %d: each entry of '%s' must be name: value", line_of(name), key);
            continue;
        }

        (*entries)[i].key = copy_text(state, name);
        (*entries)[i].value = copy_text(state, value);
    }
}


//dependsOn: name or list
static void parse_depends_on(parser_state_t *state, const yaml_node_t *node, char ***names, size_t *count){
    //A single name:
    if(node->type == YAML_SCALAR_NODE){
        *names = calloc(1, sizeof **names);

        if(*names == NULL){
            error_list_add(state->errors, "out of memory while reading the YAML");
            return;
        }

        (*names)[0] = parse_text(state, node, "dependsOn");
        *count = 1;
        return;
    }

    if(node->type != YAML_SEQUENCE_NODE){
        error_list_add(state->errors, "line %d: 'dependsOn' must be a name or a list of names", line_of(node));
        return;
    }

    //A list
    size_t n = (size_t)(node->data.sequence.items.top - node->data.sequence.items.start);

    if(n == 0)
        return;

    *names = calloc(n, sizeof **names);

    if(*names == NULL){
        error_list_add(state->errors, "out of memory while reading the YAML");
        return;
    }

    *count = n;

    for(size_t i = 0; i < n; i++){
        yaml_node_t *item = get_node(state, node->data.sequence.items.start[i]);
        (*names)[i] = parse_text(state, item, "dependsOn");
    }
}


//condition: succeeded(), failed(), always()
static void parse_condition(parser_state_t *state, const yaml_node_t *node, condition_t *condition){
    if(node->type == YAML_SCALAR_NODE && strcmp(text_of(node), "succeeded()") == 0)
        *condition = COND_SUCCEEDED;

    else if(node->type == YAML_SCALAR_NODE && strcmp(text_of(node), "failed()") == 0)
        *condition = COND_FAILED;

    else if(node->type == YAML_SCALAR_NODE && strcmp(text_of(node), "always()") == 0)
        *condition = COND_ALWAYS;

    else
        error_list_add(state->errors, "line %d: 'condition' must be succeeded(), failed() or always()", line_of(node));
}


//timeOutInSeconds
static void parse_timeout(parser_state_t *state, const yaml_node_t *node, int *timeout){
    if(node->type != YAML_SCALAR_NODE){
        error_list_add(state->errors, "line %d: 'timeoutInSeconds' must be a positive integer", line_of(node));
        return;
    }

    char *end = NULL;
    errno = 0;
    long value = strtol(text_of(node), &end, 10);

    if(errno != 0 || end == text_of(node) || *end != '\0' || value <= 0 || value > INT_MAX){
        error_list_add(state->errors, "line %d: 'timeoutInSeconds' must be a positive integer", line_of(node));
        return;
    }

    *timeout = (int)value;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//Built-in tasks
typedef struct {
    const char *name;
    const char *required[4];
    const char *optional;
} task_info_t;

static const task_info_t TASKS[] = {
    {"Sleep@1", {"seconds"}, NULL},
    {"Cpu@1", {"iterations", "seed"}, NULL},
    {"WordCount@1", {"file", "threads", "output"}, "top"},
    {"Histogram@1", {"file", "bins", "threads", "output"}, NULL},
};

//Rejects an unknown task, a missing input or an unknown input before anything runs
static void check_task(parser_state_t *state, const step_def_t *step){
    const task_info_t *info = NULL;

    for(size_t i = 0; i < COUNT_OF(TASKS); i++){
        if(strcmp(step->task, TASKS[i].name) == 0)
            info = &TASKS[i];
    }

    if(info == NULL){
        error_list_add(state->errors, "line %d: unknown task '%s' (Sleep@1, Cpu@1, WordCount@1 or Histogram@1)", step->line, step->task);
        return;
    }

    for(size_t r = 0; r < 4 && info->required[r] != NULL; r++){
        bool found = false;

        for(size_t i = 0; i < step->n_inputs; i++){
            if(strcmp(step->inputs[i].key, info->required[r]) == 0)
                found = true;
        }

        if(!found)
            error_list_add(state->errors, "line %d: task '%s' needs input '%s'", step->line, info->name, info->required[r]);
    }

    for(size_t i = 0; i < step->n_inputs; i++){
        const char *key = step->inputs[i].key;
        bool known = info->optional != NULL && strcmp(key, info->optional) == 0;

        for(size_t r = 0; r < 4 && info->required[r] != NULL; r++){
            if(strcmp(key, info->required[r]) == 0)
                known = true;
        }

        if(!known)
            error_list_add(state->errors, "line %d: unknown input '%s' for task '%s'", step->line, key, info->name);
    }
}





//STEP
static void parse_step(parser_state_t *state, const yaml_node_t *node, step_def_t *step){
    if(node->type != YAML_MAPPING_NODE){
        error_list_add(state->errors, "line %d: each step must be a map (- script: command)", line_of(node));
        return;
    }

    step->line = line_of(node);
    check_keys(state, node, STEP_KEYS, COUNT_OF(STEP_KEYS), "step");

    yaml_node_t *script = find_value(state, node, "script");
    yaml_node_t *task = find_value(state, node, "task");
    yaml_node_t *publish = find_value(state, node, "publish");
    yaml_node_t *download = find_value(state, node, "download");

    int kinds = 0;

    if(script != NULL)
        kinds++;
    if(task != NULL)
        kinds++;
    if(publish != NULL)
        kinds++;
    if(download != NULL)
        kinds++;

    if(kinds != 1){
        error_list_add(state->errors, "line %d: a step needs exactly one of script, task, publish or download", step->line);
        return;
    }

    yaml_node_t *value;

    if(script != NULL){
        step->kind = STEP_SCRIPT;
        step->script = parse_text(state, script, "script");
    }



    ////TASK : Name@1 with its inputs map
    else if(task != NULL){
        step->kind = STEP_TASK;
        step->task = parse_text(state, task, "task");
        value = find_value(state, node, "inputs");

        if(value != NULL)
            parse_key_value_map(state, value, "inputs", &step->inputs, &step->n_inputs);

        if(step->task != NULL)
            check_task(state, step);
    }



    //PUBLISH 
    else if(publish != NULL){
        step->kind = STEP_PUBLISH;
        step->path = parse_text(state, publish, "publish");
        value = find_value(state, node, "artifact");

        if(value == NULL)
            error_list_add(state->errors, "line %d: 'publish' needs 'artifact'", step->line);
        else
            step->artifact = parse_text(state, value, "artifact");
    }



    //DOWNLOAD
    else {
        step->kind = STEP_DOWNLOAD;
        step->artifact = parse_text(state, download, "download");
        value = find_value(state, node, "path");

        if(value != NULL)
            step->path = parse_text(state, value, "path");
    }

    value = find_value(state, node, "continueOnError");

    if(value != NULL){
        if(value->type == YAML_SCALAR_NODE && strcmp(text_of(value), "true") == 0)
            step->continue_on_error = true;

        else if(value->type == YAML_SCALAR_NODE && strcmp(text_of(value), "false") == 0)
            step->continue_on_error = false;

        else
            error_list_add(state->errors, "line %d: 'continueOnError' must be true or false", line_of(value));
    }
}


//JOB
static void parse_job(parser_state_t *state, const yaml_node_t *node, job_def_t *job){
    if(node->type != YAML_MAPPING_NODE){
        error_list_add(state->errors, "line %d: each job must be a map (- job: name)", line_of(node));
        return;
    }

    check_keys(state, node, JOB_KEYS, COUNT_OF(JOB_KEYS), "job");

    //A missing name doesn't stop the job
    job->name = parse_name(state, node, "job");

    yaml_node_t *value = find_value(state, node, "dependsOn");

    if(value != NULL){
        job->has_depends_on = true;
        parse_depends_on(state, value, &job->depends_on, &job->n_depends_on);
    }

    value = find_value(state, node, "condition");

    if(value != NULL)
        parse_condition(state, value, &job->condition);

    value = find_value(state, node, "timeoutInSeconds");

    if(value != NULL)
        parse_timeout(state, value, &job->timeout_seconds);




    value = find_value(state, node, "steps");

    if(value == NULL){
        error_list_add(state->errors, "line %d: job without 'steps'", line_of(node));
        return;
    }

    size_t count = list_size(state, value, "steps");

    if(count == 0)
        return;

    job->steps = calloc(count, sizeof *job->steps);

    if(job->steps == NULL){
        error_list_add(state->errors, "out of memory while reading the YAML");
        return;
    }

    job->n_steps = count;

    for(size_t i = 0; i < count; i++){
        yaml_node_t *step = get_node(state, value->data.sequence.items.start[i]);
        parse_step(state, step, &job->steps[i]);
    }
}


//STAGE
static void parse_stage(parser_state_t *state, const yaml_node_t *node, stage_def_t *stage){
    if(node->type != YAML_MAPPING_NODE){
        error_list_add(state->errors, "line %d: each stage must be a map (- stage: name)", line_of(node));
        return;
    }

    check_keys(state, node, STAGE_KEYS, COUNT_OF(STAGE_KEYS), "stage");
    //A missing name doesn't stop the stage
    stage->name = parse_name(state, node, "stage");

    yaml_node_t *value = find_value(state, node, "dependsOn");

    
    if(value != NULL){
        stage->has_depends_on = true;
        parse_depends_on(state, value, &stage->depends_on, &stage->n_depends_on);
    }

    value = find_value(state, node, "condition");

    if(value != NULL)
        parse_condition(state, value, &stage->condition);



    value = find_value(state, node, "jobs");

    if(value == NULL){
        error_list_add(state->errors, "line %d: stage without 'jobs'", line_of(node));
        return;
    }

    size_t count = list_size(state, value, "jobs");

    if(count == 0)
        return;

    stage->jobs = calloc(count, sizeof *stage->jobs);

    if(stage->jobs == NULL){
        error_list_add(state->errors, "out of memory while reading the YAML");
        return;
    }

    stage->n_jobs = count;

    for(size_t i = 0; i < count; i++){
        yaml_node_t *job = get_node(state, value->data.sequence.items.start[i]);
        parse_job(state, job, &stage->jobs[i]);
    }
}


//PIPELINE
static void parse_pipeline(parser_state_t *state, const yaml_node_t *root, pipeline_t *pipeline){
    check_keys(state, root, PIPELINE_KEYS, COUNT_OF(PIPELINE_KEYS), "pipeline");
    yaml_node_t *value = find_value(state, root, "name");

    if(value != NULL){
        if(value->type != YAML_SCALAR_NODE)
            error_list_add(state->errors, "line %d: 'name' must be a single value", line_of(value));

        else
            pipeline->name = copy_text(state, value);
    }


    value = find_value(state, root, "variables");

    if(value != NULL)
        parse_key_value_map(state, value, "variables", &pipeline->variables, &pipeline->n_variables);




    value = find_value(state, root, "stages");

    if(value == NULL){
        error_list_add(state->errors, "line %d: pipeline without 'stages'", line_of(root));
        return;
    }

    if(value->type != YAML_SEQUENCE_NODE){
        error_list_add(state->errors, "line %d: 'stages' must be a list", line_of(value));
        return;
    }

    //how many stages
    size_t count = list_size(state, value, "stages");

    if(count == 0)
        return;

    pipeline->stages = calloc(count, sizeof *pipeline->stages);

    if(pipeline->stages == NULL){
        error_list_add(state->errors, "out of memory while reading the YAML");
        return;
    }

    pipeline->n_stages = count;

    for(size_t i = 0; i < count; i++){
        yaml_node_t *stage = get_node(state, value->data.sequence.items.start[i]);
        parse_stage(state, stage, &pipeline->stages[i]); //CALLS STAGE
    }   
}



//PARSER 
pipeline_t *yaml_parse_pipeline(const char *yaml, size_t yaml_len, error_list_t *errors){
    yaml_parser_t parser;
    yaml_document_t document; 

    if(!yaml_parser_initialize(&parser)){
        error_list_add(errors, "out of memory while reading YAML");
        return NULL;
    }

    //reads from the buffer
    yaml_parser_set_input_string(&parser, (const unsigned char *)yaml, yaml_len);

    //Tree of nodes(fails if it's not a valid YAML)
    if(!yaml_parser_load(&parser, &document)){
        //Say where is the error
        error_list_add(errors, "line %d: invalid YAML: %s", (int)parser.problem_mark.line + 1, parser.problem);
        yaml_parser_delete(&parser);
        return NULL;
    }

    pipeline_t *pipeline = NULL;
    yaml_node_t *root = yaml_document_get_root_node(&document);

    //No root
    if(root == NULL)
        error_list_add(errors, "the YAML is empty");

    //if the root is not a map, then its not a pipeline
    else if(root->type != YAML_MAPPING_NODE)
        error_list_add(errors, "line %d: the pipeline must be a map with 'stages'", (int)root->start_mark.line + 1);

    //valid pipeline
    else{
        //empty pipeline
        pipeline = calloc(1, sizeof *pipeline);

        if(pipeline == NULL)
            error_list_add(errors, "out of memory while reading YAML");

        else{
            parser_state_t state = {&document, errors};
            parse_pipeline(&state, root, pipeline); //CALLS PIPELINE
        }
    }

    //Free the tree
    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
    return pipeline;

}