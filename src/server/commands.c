#define _POSIX_C_SOURCE 200809L
 
#include "commands.h"
#include <stdio.h>


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
        printf("NOT IMPLEMENTED");
        return 1;

    //LOGS
    case C_LOGS:
        printf("NOT IMPLEMENTED");
        return 1;

    //CANCEL
    case C_CANCEL:
        printf("NOT IMPLEMENTED");
        return 1;
    
    //LIST
    case C_LIST:
        printf("NOT IMPLEMENTED");
        return 1;

    //SHUTDOWN
    case C_SHUTDOWN:
        printf("NOT IMPLEMENTED");
        return 1;

    default:
        snprintf(body, size, "ERROR");
        return 0;
    }
}