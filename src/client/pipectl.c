
#include "../include/net_util.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


//./fdtd_client <host> <port> <command...>


int main(int argc, char **argv) 
{
    /*argv[0] = program name
      argv[1] = host
      argv[2] = port
      argv[3...] = command
    */

    if (argc < 4){
        fprintf(stderr, "usage: %s <host> <port> <command...>\n", argv[0]);
        return EXIT_FAILURE;
    }

    
    char *tail = NULL;
    errno = 0;
    long port = strtol(argv[2], &tail, 10);

       //verification for invalid ports
    if (errno != 0 || tail == argv[2] || *tail != '\0' || port <= 0 || port > 65535){
        fprintf(stderr, "invalid port '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }


    char request[LINE_MAX_LEN];
    size_t used = 0;
    request[0] = '\0';


    for (int i = 3; i < argc; ++i){

        //use snprintf to send request text to socket.
                         //writes in       //available storage          //if(i>3)->blank space
        int w = snprintf(request + used, sizeof request - used, "%s%s", (i > 3) ? " " : "", argv[i]);


        if (w < 0 || (size_t)w >= sizeof request - used){
            fprintf(stderr, "command too long\n");
            return EXIT_FAILURE;
        }


        used += (size_t)w;
    }


    int file_descriptor = nu_connect(argv[1], (unsigned short)port);

    if (file_descriptor < 0)
        return EXIT_FAILURE;

    if (nu_send_line(file_descriptor, request) < 0){
        close(file_descriptor);
        return EXIT_FAILURE;
    }
    

    char response[LINE_MAX_LEN];
    ssize_t n = nu_read_line(file_descriptor, response, sizeof response);
    close(file_descriptor);

    if (n < 0){
        fprintf(stderr, "no response from server\n");
        return EXIT_FAILURE;
    }

    printf("%s\n", response);
    return EXIT_SUCCESS;
}