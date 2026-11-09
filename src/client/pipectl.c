#define _POSIX_C_SOURCE 200809L

#include "net_util.h"
#include "protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


int main(int argc, char **argv) 
{
    if (argc < 4){
        fprintf(stderr, "usage: %s <host> <port> <command...>\n", argv[0]);
        return EXIT_FAILURE;
    }

    
    char *tail = NULL;
    errno = 0;
    long port = strtol(argv[2], &tail, 10);

    if (errno != 0 || tail == argv[2] || *tail != '\0' || port <= 0 || port > 65535){
        fprintf(stderr, "invalid port '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }


    int submit = strcmp(argv[3], "SUBMIT") == 0;

    if(submit && argc != 5){
        fprintf(stderr, "usage: %s <host> <port> SUBMIT <file.yaml>\n", argv[0]);
        return EXIT_FAILURE;
    }

  
    char request[LINE_MAX_LEN];
    size_t used = 0;
    request[0] = '\0';

    if(!submit){
        for (int i = 3; i < argc; ++i){

            int w = snprintf(request + used, sizeof request - used, "%s%s", (i > 3) ? " " : "", argv[i]);


            if (w < 0 || (size_t)w >= sizeof request - used){
                fprintf(stderr, "command too long\n");
                return EXIT_FAILURE;
            }


            used += (size_t)w;
        }
    }



    int follow = strcmp(argv[argc - 1], "FOLLOW") == 0;
    
    int file_descriptor = nu_connect(argv[1], (unsigned short)port);

    if (file_descriptor < 0)
        return EXIT_FAILURE;


    int sent;

    if(submit)
        sent = protocol_send_submit(file_descriptor, argv[4]);

    else
        sent = nu_send_line(file_descriptor, request);

    if(sent < 0){
        close(file_descriptor);
        return EXIT_FAILURE;
    }


    int result;
    size_t len;

    do{
        result = protocol_print_response(file_descriptor, &len);
    } while (follow && result == 1 && len > 0);


    close(file_descriptor);

    if(result < 0){
        fprintf(stderr, "no response from server\n");
        return EXIT_FAILURE;
    }

    if(result == 0)
        return EXIT_FAILURE;


    return EXIT_SUCCESS;
}