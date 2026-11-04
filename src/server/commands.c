#define _POSIX_C_SOURCE 200809L
 
#include "commands.h"
#include <stdio.h>



//Each helper returns 1 = OK, 0 = ERR, like command_execute.
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

    //THIS IS VOID FOR NOW. NEED TO IMPLEMENT FUNCTIONALITY
    //avoids compilation warnings
    (void) yaml;
    (void) yaml_len;


    //ALL CASES ARE NOT IMPLEMENTED YET, NEED TO CHANGE
    switch (command->type)
    {

    //SUBMIT
    case C_SUBMIT: 
        printf("NOT IMPLEMENTED");
        return 1;
    
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
