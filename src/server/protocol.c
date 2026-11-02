#define _POSIX_C_SOURCE 200809L

#include "protocol.h"
#include "net_util.h"  
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define MAX_WORDS 8
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

//Just checks if an id is not negative.
static int parse_id(const char *text, unsigned long *id){
    long value;

    if(parse_long(text, &value) < 0 || value <= 0)
        return -1;

    *id = (unsigned long)value;

    return 0;
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
    


    //SHUTDOWN OR LIST
    if(strcmp(words[0], "SHUTDOWN") == 0 || strcmp(words[0], "LIST") == 0){
        if(count != 1)
            return fail(command, "command takes no arguments");

        command->type = (strcmp(words[0], "SHUTDOWN") == 0) ? C_SHUTDOWN : C_LIST;;
        return 0;
    }
    

    //STATUS OR CANCEL
    if(strcmp(words[0], "STATUS") == 0 || strcmp(words[0], "CANCEL") == 0){

        if(count != 2)
            return fail(command, "expected 1 argument");

        if(parse_id(words[1], &command->id) < 0)
            return fail(command, "invalid id");

        command->type = (strcmp(words[0], "STATUS") == 0) ? C_STATUS : C_CANCEL;

        return 0;
    }

    
    //SUBMIT 
    if(strcmp(words[0], "SUBMIT") == 0){
        long nbytes;

        if(count != 2)
            return fail(command, "expected 1 argument");

        if(parse_long(words[1], &nbytes) < 0 || nbytes <= 0 || nbytes > MAX_YAML_BYTES)
            return fail(command, "invalid yaml size");

        command->type = C_SUBMIT;
        command->body_len = (size_t)nbytes;

        return 0;

    }


    //LOGS
    if(strcmp(words[0], "LOGS") == 0){

        if(count < 2 || count > 4)
            return fail(command, "expected 1 to 3 arguments");
        
        if(parse_id(words[1], &command->id) < 0)
            return fail(command, "invalid id");
        
        int next_word = 2;

        if(next_word < count && strcmp(words[next_word], "FOLLOW") != 0){ //Is not follow, is job
            if(strlen(words[next_word]) >= JOB_NAME_MAX)
                return fail(command, "job name too long");

            snprintf(command->job, sizeof command->job, "%s", words[next_word]);
            next_word++;
        }


        if(next_word < count){//There is still a word left, has to be FOLLOW
            if(strcmp(words[next_word], "FOLLOW") != 0)
                return fail(command, "expected FOLLOW");

            command->follow = 1;
            next_word++;
        }

        if(next_word != count)
            return fail(command, "too many arguments");

        command->type = C_LOGS;
        return 0;
        
    }



    return fail(command, "unknown command");

}



int protocol_send_response(int file_descriptor, int ok, const char *body){
    size_t len = (body != NULL) ? strlen(body) : 0;

    char header[64];

    snprintf(header, sizeof header, "%s %zu", ok ? "OK" : "ERR", len);

    if(nu_send_line(file_descriptor, header) <0)
        return -1;

    if(len == 0)
        return 0;

    return nu_write_all(file_descriptor, body, len);
}


int protocol_read_response(int file_descriptor, int *ok, char **body, size_t *len){
    
    char line[LINE_MAX_LEN]; //header

    if(nu_read_line(file_descriptor, line, sizeof line) < 0)
        return -1;

    char *space = strchr(line, ' ');

    if(space == NULL) //No blank space
        return -1;

    *space = '\0';

    if(strcmp(line, "OK") == 0)
        *ok = 1;

    else if(strcmp(line, "ERR") == 0)
        *ok = 0;

    else 
        return -1;

    long nbytes;

    if(parse_long(space + 1, &nbytes) < 0 || nbytes < 0 || nbytes > MAX_RESPONSE_BYTES)
        return -1;

    char *buffer = malloc((size_t)nbytes + 1);

    if(buffer == NULL)
        return -1;

    if (nbytes > 0 && nu_read_exact(file_descriptor, buffer, (size_t)nbytes) < 0){
        free(buffer);
        return -1;
    }

    buffer[nbytes] = '\0';

    *body = buffer;
    *len = (size_t)nbytes;
    return 0;
}
