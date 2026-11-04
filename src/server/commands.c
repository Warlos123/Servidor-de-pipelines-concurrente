#define _POSIX_C_SOURCE 200809L
 
#include "commands.h"
#include "validator.h"
#include "yaml_parser.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>



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



    //ALL CASES ARE NOT IMPLEMENTED YET, NEED TO CHANGE
    switch (command->type)
    {

    //SUBMIT
    case C_SUBMIT:
        return command_submit(yaml, yaml_len, body, size);
    
    //STATUS 
    case C_STATUS:
        return command_status(command, body, size);

    //LOGS
    case C_LOGS:
        snprintf(body, size, "use --follow for streaming logs");
        return 1;

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
void command_follow(const command_t *command, int file_descriptor){

    (void)command;

    (void)protocol_send_response(file_descriptor, 0, "NOT IMPLEMENTED");
}