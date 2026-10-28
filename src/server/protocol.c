#define _POSIX_C_SOURCE 200809L

#include "../includes/protocol.h"
#include "../includes/net_util.h"  
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAX_WORDS 16
#define SEPARATOR " "
#define MIN_DIMENSIONS 3
#define MAX_DIMENSIONS 4096

static int parse_long (const char *text, long *result){
    char *end = NULL;
    
    errno = 0; //To clean a previous errno

    long value = strtol (text, &end, 10);

    if(errno != 0 || end == text || *end !='\0')
        return -1;

    *result = value;
    return 0;
}


static int fail(command_t *command, const char *reason){
    command->type = C_INVALID;

    snprintf(command->error, sizeof command->error, "%s", reason);

    
    return -1;
}

//TURNS A LINE OF TEXT INTO A COMMAND
int protocol_parse(const char *line, command_t *command){
    memset(command, 0, sizeof *command);

    char copy[LINE_MAX_LEN];
    snprintf(copy, sizeof copy, "%s", line);

    char *words[MAX_WORDS];
    int count = 0;
    char *save = NULL;

    for(char *t = strtok_r(copy, SEPARATOR, &save); t != NULL; t = strtok_r(NULL, SEPARATOR, &save)){
        if(count == MAX_WORDS)
            return fail(command, "too many arguments");
        
        words[count++] = t;
    }

    //FAIL
    if(count == 0)
        return fail(command, "empty command");
    


    //SHUTDOWN 
    if(strcmp(words[0], "SHUTDOWN") == 0){
        if(count != 1)
            return fail(command, "SHUTDOWN takes no arguments");

        command->type = C_SHUTDOWN;
        return 0;
    }
    

    //STATUS OR RESULT
    if(strcmp(words[0], "STATUS") == 0 || strcmp(words[0], "RESULT") == 0){
        long id;

        if(count != 2)
            return fail(command, "expected 1 argument");

        if(parse_long(words[1], &id) < 0 || id <= 0)
            return fail(command, "invalid id");

        command->type = (strcmp(words[0], "STATUS") == 0) ? C_STATUS : C_RESULT;

        command->id = (unsigned long)id;

        return 0;
    }

    
    //PEEK
    if(strcmp(words[0], "PEEK") == 0){
        long id, i, j;

        if(count != 4)
            return fail(command, "expected 3 arguments");

        if(parse_long(words[1], &id) < 0 || id <= 0)
            return fail(command, "invalid id");

        if(parse_long(words[2], &i) < 0 || i < 0 || i > INT_MAX)
            return fail(command, "invalid i");

        if(parse_long(words[3], &j) < 0 || j < 0 || j > INT_MAX)
            return fail(command, "invalid j");

        command->type = C_PEEK;
        command->id = (unsigned long)id;
        command->i = (int)i;
        command->j = (int)j;

        return 0;
    }


    //SUBMIT 
    if(strcmp(words[0], "SUBMIT") == 0){
        long params_submit[7];

        if(count != 8)
            return fail(command, "expected 7 arguments");

        for(int n = 0; n < 7; n++){
            if(parse_long(words[n + 1], &params_submit[n]) < 0)
                return fail(command, "invalid number");
        }

        long nx = params_submit[0];
        long ny = params_submit[1];
        long steps = params_submit[2];
        long threads = params_submit[3];
        long k = params_submit[4];
        long sx = params_submit[5];
        long sy = params_submit[6];      
        
        if(nx < MIN_DIMENSIONS || nx > MAX_DIMENSIONS)
            return fail(command, "nx out of range");

        if(ny < MIN_DIMENSIONS || ny > MAX_DIMENSIONS)
            return fail(command, "ny out of range");

        if(steps <= 0 || steps > INT_MAX)
            return fail(command, "invalid steps");
        
        if(threads < 1 || threads > nx - 2)
            return fail(command, "threads out of range");

        if(k < 0 || k > INT_MAX)
            return fail(command, "invalid k");

        if(sx < 1 || sx > nx - 2 || sy < 1 || sy > ny - 2)
            return fail(command, "source out of range");

        command->type = C_SUBMIT;
        command->params.nx = (int)nx;
        command->params.ny = (int)ny;
        command->params.steps = (int)steps;
        command->params.threads = (int)threads;
        command->params.k = (int)k;
        command->params.sx = (int)sx;
        command->params.sy = (int)sy;

        return 0;

    }

    return fail(command, "unknown command");

}