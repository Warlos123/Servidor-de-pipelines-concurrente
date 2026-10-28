
#include "../includes/net_util.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int nu_listen(unsigned short port, int backlog)
{
    int file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (file_descriptor < 0)
    {
        perror("socket");
        return -1;
    }

    int yes = 1;
    if (setsockopt(file_descriptor, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
    {
        perror("setsockopt");
        close(file_descriptor);
        return -1;
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);

    if (bind(file_descriptor, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
    {
        perror("bind");
        close(file_descriptor);
        return -1;
    }

    if (listen(file_descriptor, backlog) < 0)
    {
        perror("listen");
        close(file_descriptor);
        return -1;
    }

    return file_descriptor;
}

int nu_write_all(int file_descriptor, const void* buffer, size_t size)
{
    const char *p = buffer;
    size_t sent = 0;

    while (sent < size) {
        ssize_t n = send(file_descriptor, p + sent, size - sent, MSG_NOSIGNAL); //use send so server wont shutdown with SIGPIPE
        if (n < 0) {
            if (errno == EINTR)
                continue;          /* interrupted by a signal: retry */
            perror("write");
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

ssize_t nu_read_line(int file_descriptor, char *buffer, size_t size)
{
    if (buffer == NULL || size < 2){ 
        errno = EINVAL; 
        return -1; 
    }

    size_t len = 0;
    int found_newline = 0;

    while(len < size - 1){
        char c;
        ssize_t n = read(file_descriptor, &c, 1);

        if(n < 0){
            if(errno == EINTR)
                continue;
            
            return -1;
        }

        if(n == 0){
            if(len == 0) 
                return -1;

            break;
        }

        if(c == '\n'){
            found_newline = 1;
            break;
        }
        
        buffer[len++] = c;
    }

    if(!found_newline && len == size -1){
        errno = EMSGSIZE;
        return -1;
    }

    if(len > 0 && buffer[len - 1] == '\r')
        len--;
    
    buffer[len] = '\0';
    return(ssize_t)len;
}

int nu_send_line(int file_descriptor, const char *line)
{
    if(nu_write_all(file_descriptor, line, strlen(line)) < 0 )
        return -1;
    return nu_write_all(file_descriptor, "\n", 1);
}

int nu_connect(const char  *host, unsigned short port)
{
    int file_descriptor = socket(AF_INET, SOCK_STREAM, 0);

    if(file_descriptor < 0){
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if(inet_pton(AF_INET, host, &addr.sin_addr) != 1){
        fprintf(stderr, "invalid host '%s'\n", host);
        close(file_descriptor);
        return -1;
    }

    if(connect(file_descriptor, (struct sockaddr *)&addr, sizeof addr) < 0){
        perror("connect");
        close(file_descriptor);
        return -1;
    }

    return file_descriptor;

}