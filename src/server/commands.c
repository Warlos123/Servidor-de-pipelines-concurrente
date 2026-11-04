#define _POSIX_C_SOURCE 200809L
 
#include "commands.h"
#include "validator.h"
#include "yaml_parser.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>



//Each helper returns 1 = OK, 0 = ERR, like command_execute.

//Writes every error, one per line, so the client sees them all at once.
static void write_errors(const error_list_t *errors, char *body, size_t size){
    size_t used = 0;
    body[0] = '\0';

    for(size_t i = 0; i < errors->count && used < size; i++){
        int written = snprintf(body + used, size - used, "%s\n", errors->items[i]);

        if(written < 0)
            break;

        used += (size_t)written;
    }
}

static int command_submit(const char *yaml, size_t yaml_len, char *body, size_t size){

    //no new runs once the shutdown started.
    if(!server_is_running()){
        snprintf(body, size, "server is shutting down");
        return 0;
    }


    error_list_t errors = {0};
    pipeline_t *pipeline = yaml_parse_pipeline(yaml, yaml_len, &errors);

    if(pipeline != NULL)
        validate_pipeline(pipeline, &errors);

    if(pipeline == NULL || errors.count > 0){
        if(errors.count == 0)
            snprintf(body, size, "invalid pipeline");
        else
            write_errors(&errors, body, size);

        pipeline_free(pipeline);
        error_list_free(&errors);
        return 0;
    }

    error_list_free(&errors);

    uint64_t run_id = 0;

    if(registry_submit(server_registry(), server_scheduler(), pipeline, &run_id) < 0){
        pipeline_free(pipeline);
        snprintf(body, size, "could not create the run");
        return 0;
    }

    snprintf(body, size, "%" PRIu64, run_id);
    return 1;
}
static int command_status(const command_t *command, char *body, size_t size){
    int result = registry_status(server_registry(), (uint64_t)command->id, body, size);
 
    if(result == REG_NOT_FOUND){
        snprintf(body, size, "run %lu not found", command->id);
        return 0;
    }
 
    return 1;
}
 
static int command_list(char *body, size_t size){
    if(registry_list(server_registry(), body, size) < 0){
        snprintf(body, size, "out of memory");
        return 0;
    }
 
    if(body[0] == '\0')
        snprintf(body, size, "no runs");
 
    return 1;
}
 
static int command_cancel(const command_t *command, char *body, size_t size){
    int result = registry_cancel(server_registry(), (uint64_t)command->id);
 
    if(result == REG_NOT_FOUND){
        snprintf(body, size, "run %lu not found", command->id);
        return 0;
    }
 
    if(result == REG_FINISHED){
        snprintf(body, size, "run %lu already finished", command->id);
        return 0;
    }
 
    snprintf(body, size, "run %lu canceling", command->id);
    return 1;
}
 

int command_execute(const command_t *command, const char *yaml, size_t yaml_len, char *body, size_t size){


    switch (command->type)
    {

    //SUBMIT
    case C_SUBMIT:
        return command_submit(yaml, yaml_len, body, size);
    
    //STATUS 
    case C_STATUS:
        return command_status(command, body, size);


    //CANCEL
    case C_CANCEL:
        return command_cancel(command, body, size);
    
    //LIST
    case C_LIST:
        return command_list(body, size);

    //SHUTDOWN
    case C_SHUTDOWN:
        server_request_shutdown();
        snprintf(body, size, "shutting down");
        return 1;

    default:
        snprintf(body, size, "ERROR");
        return 0;
    }
}


//LOGS [FOLLOW]

#define LOG_CHUNK 4096

typedef struct {
    char *text;
    size_t used;
    size_t size;
} text_buffer_t;


//Adds len bytes of piece at the end of buffer
static int text_append(text_buffer_t *buffer, const char *piece, size_t len){
    if(buffer->used + len + 1 > buffer->size){
        size_t size = buffer->size * 2 + len + 1;
        char *text = realloc(buffer->text, size);

        if(text == NULL)
            return -1;

        buffer->text = text;
        buffer->size = size;
    }

    memcpy(buffer->text + buffer->used, piece, len);
    buffer->used += len;
    buffer->text[buffer->used] = '\0';
    return 0;
}


static bool job_selected(const run_t *run, size_t g, const command_t *command){
    if(command->job[0] == '\0')
        return true;

    return strcmp(run->jobs[g].def->name, command->job) == 0;
}


//Header written before each job's lines
static void job_header(const run_t *run, size_t g, char *header, size_t size){
    snprintf(header, size, "== %s/%s ==\n", run->pipeline->stages[run->jobs[g].stage].name, run->jobs[g].def->name);
}


//LOGS <id> [job]
static void send_whole_logs(int file_descriptor, run_t *run, const command_t *command){
    text_buffer_t buffer = {NULL, 0, 0};
    int failed = 0;

    for(size_t g = 0; g < run->n_jobs && !failed; g++){
        if(!job_selected(run, g, command))
            continue;

        char header[256];
        job_header(run, g, header, sizeof header);

        size_t len = 0;
        char *lines = log_copy(&run->jobs[g].log, &len);

        if(lines == NULL || text_append(&buffer, header, strlen(header)) < 0 || text_append(&buffer, lines, len) < 0)
            failed = 1;

        free(lines);
    }

    if(failed)
        (void)protocol_send_response(file_descriptor, 0, "out of memory");
    else
        (void)protocol_send_response(file_descriptor, 1, buffer.text);

    free(buffer.text);
}


//LOGS <id> FOLLOW
static void follow_logs(int file_descriptor, run_t *run, const command_t *command){
    char chunk[LOG_CHUNK + 1];

    for(size_t g = 0; g < run->n_jobs; g++){
        if(!job_selected(run, g, command))
            continue;

        job_header(run, g, chunk, sizeof chunk);

       
        if(protocol_send_response(file_descriptor, 1, chunk) < 0)
            return;

        size_t offset = 0;
        bool done = false;

        while(!done){
            size_t n = log_read(&run->jobs[g].log, &offset, chunk, LOG_CHUNK, &done);

            if(n == 0)
                continue;

            
            chunk[n] = '\0';

            if(protocol_send_response(file_descriptor, 1, chunk) < 0)
                return;
        }
    }

    //Empty response: the run's selected logs are complete.
    (void)protocol_send_response(file_descriptor, 1, "");
}


void command_logs(const command_t *command, int file_descriptor){
    run_t *run = registry_find(server_registry(), (uint64_t)command->id);
    char message[128];

    if(run == NULL){
        snprintf(message, sizeof message, "run %lu not found", command->id);
        (void)protocol_send_response(file_descriptor, 0, message);
        return;
    }

    //With a job name, at least one job of the run must have it.
    bool found = false;

    for(size_t g = 0; g < run->n_jobs; g++){
        if(job_selected(run, g, command))
            found = true;
    }

    if(!found){
        snprintf(message, sizeof message, "job '%s' not found in run %lu", command->job, command->id);
        (void)protocol_send_response(file_descriptor, 0, message);
        return;
    }

    if(command->follow)
        follow_logs(file_descriptor, run, command);
    else
        send_whole_logs(file_descriptor, run, command);
}