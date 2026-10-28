
#ifndef NET_UTIL_H
#define NET_UTIL_H


#include <stddef.h>
#include <sys/types.h>

#define LINE_MAX_LEN 512

int nu_listen(unsigned short port, int backlog);

int nu_write_all(int file_descriptor, const void *buffer, size_t size);

ssize_t nu_read_line(int file_descriptor, char *buffer, size_t size);
int nu_send_line(int file_descriptor, const char *line);
int nu_connect(const char *host, unsigned short port);

#endif //NET_UTIL_H