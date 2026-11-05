#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "net_util.h"
#include "protocol.h"
#include "commands.h"
#include "scheduler.h"
#include "job_execute.h"
#include "builtin_tasks.h"


#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <semaphore.h>
#include <limits.h>
#include <sys/time.h>


#define DEFAULT_PORT 8080
#define LISTEN_BACKLOG 64


#define REPLY_BYTES 2048 //first size of the reply buffer; it doubles while the reply doesn't fit

#define READ_TIMEOUT_SECONDS 5 

#ifndef MAX_ACTIVE_THREADS
#define MAX_ACTIVE_THREADS 32
#endif

static volatile sig_atomic_t g_running = 1;
static int listen_file_descriptor = -1;

// Limits how many connection handlers can run at once.
static sem_t g_counting_sem;

static scheduler_t g_scheduler;

static registry_t g_registry;


scheduler_t *server_scheduler(void){
    return &g_scheduler;
}

registry_t *server_registry(void){
    return &g_registry;
}

typedef struct {
    int file_descriptor;
    unsigned long connection_id;
} connection_t;

static void on_sigint(int signum)
{
    (void)signum;
    g_running = 0;
    shutdown(listen_file_descriptor, SHUT_RD);
}

int server_is_running(void){
    return g_running;
}

void server_request_shutdown(void){
    g_running = 0;
    shutdown(listen_file_descriptor, SHUT_RD);
}

static int install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;

    if (sigaction(SIGINT, &sa, NULL) < 0)
    {
        perror("sigaction");
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;

    if (sigaction(SIGPIPE, &sa, NULL) < 0)
    {
        perror("sigaction");
        return -1;
    }

    return 0;
}


static void sem_wait_retry(sem_t *s){
    while (sem_wait(s) < 0 && errno == EINTR){}
}



static void server_command(int file_descriptor, const command_t *command){
    char *yaml = NULL;

    if(command->type == C_SUBMIT){
        yaml = malloc(command->body_len + 1);

        if(yaml == NULL){
            (void)protocol_send_response(file_descriptor, 0, "out of memory");
            return;
        }

        if(nu_read_exact(file_descriptor, yaml, command->body_len) < 0){
            (void)protocol_send_response(file_descriptor, 0, "incomplete body");
            free(yaml);
            return;
        }

        yaml[command->body_len] = '\0';
    }

    if(command->type == C_LOGS){
        command_logs(command, file_descriptor);        
        return;
    }

    char *body = NULL;
    size_t size = REPLY_BYTES;
    int succeeded = 0;

    //A reply that fills the buffer was probably cut: run the command again with twice the space.
    //Safe because only STATUS, LIST and a rejected SUBMIT can be long, and none changes anything.
    while(1){
        char *bigger = realloc(body, size);

        if(bigger == NULL){
            (void)protocol_send_response(file_descriptor, 0, "out of memory");
            free(body);
            free(yaml);
            return;
        }

        body = bigger;
        body[0] = '\0';

        succeeded = command_execute(command, yaml, command->body_len, body, size);

        if(strlen(body) < size - 1 || size >= MAX_RESPONSE_BYTES)
            break;

        size *= 2;
    }

    (void)protocol_send_response(file_descriptor, succeeded, body);

    free(body);
    free(yaml);
}

static void *handle_connection(void *arg)
{
    connection_t *conn = arg;
    char line[LINE_MAX_LEN];

    //A client that connects and sends nothing can't keep this handler blocked forever
    struct timeval timeout = {READ_TIMEOUT_SECONDS, 0};
    (void)setsockopt(conn->file_descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    errno = 0;
    ssize_t n = nu_read_line(conn->file_descriptor, line, sizeof line);

    if (n > 0){
       command_t command;

        if (protocol_parse(line, &command) < 0)
            (void)protocol_send_response(conn->file_descriptor, 0, command.error);

        else
            server_command(conn->file_descriptor, &command);

    }

    else if (n == 0)
        (void)protocol_send_response(conn->file_descriptor, 0, "empty line");

    else if (errno == EMSGSIZE)
        (void)protocol_send_response(conn->file_descriptor, 0, "line too long");
    

    else if (errno == EAGAIN || errno == EWOULDBLOCK)
        (void)protocol_send_response(conn->file_descriptor, 0, "timed out waiting for the request");

        
    if (close(conn->file_descriptor) < 0)
        perror("close(file_descriptor)");

    free(conn);
    sem_post(&g_counting_sem);
    return NULL;

}

static int parse_int(const char *text, long min, long max, long *out)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);

    if(errno != 0 || end == text || *end != '\0' || value < min || value > max)
        return -1;

    *out = value;
    return 0;
}


static int parse_args(int argc, char **argv, unsigned short *port, size_t *agents,
                      const char **work_directory, int *omp_threads)
{
    long value_port, value_agents;

    *omp_threads = 0; //0: no cap, tasks use the thread count they request

    if(argc < 4 || argc > 6){
        fprintf(stderr, "usage: %s <port> <agents> <work directory> [--omp-threads <max>]\n", argv[0]);
        return -1;
    }

    if(parse_int(argv[1], 1, 65535, &value_port) < 0){
        fprintf(stderr, "invalid port '%s'\n", argv[1]);
        return -1;
    }

    if(parse_int(argv[2], 1, INT_MAX, &value_agents) < 0){
        fprintf(stderr, "invalid agents '%s'\n", argv[2]);
        return -1;
    }

    *port = (unsigned short)value_port;
    *agents = (size_t)value_agents;
    *work_directory = argv[3];

    //Optional cap on the OpenMP threads each data task may use
    for(int i = 4; i < argc; i++){
        if(strcmp(argv[i], "--omp-threads") == 0){
            long value_threads;

            if(i + 1 >= argc){
                fprintf(stderr, "missing value for --omp-threads\n");
                return -1;
            }

            if(parse_int(argv[i + 1], 1, 4096, &value_threads) < 0){
                fprintf(stderr, "invalid --omp-threads '%s'\n", argv[i + 1]);
                return -1;
            }

            i++;
            *omp_threads = (int)value_threads;
        }
        else {
            fprintf(stderr, "unknown argument '%s'\n", argv[i]);
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{

    unsigned short port;
    size_t agents;
    const char *work_directory;
    int omp_threads;

    if(parse_args(argc, argv, &port, &agents, &work_directory, &omp_threads) < 0)
        return EXIT_FAILURE;

    //Cap for the OpenMP threads of the data tasks (WordCount, Histogram).
    //Must be set before the agents start: they read it without a lock.
    task_set_max_threads(omp_threads);

    if (install_signal_handlers() < 0)
        return EXIT_FAILURE;

    if (sem_init(&g_counting_sem, 0, MAX_ACTIVE_THREADS) < 0){
        perror("sem_init");
        return EXIT_FAILURE;
    }

    if(registry_init(&g_registry) < 0){
        fprintf(stderr, "ERROR: can't create the registry\n");
        sem_destroy(&g_counting_sem);
        return EXIT_FAILURE;
    }

    if(scheduler_start(&g_scheduler, agents, execute_job, (void *)work_directory) < 0){
        fprintf(stderr, "ERROR: can't start scheduler\n");
        registry_destroy(&g_registry);
        sem_destroy(&g_counting_sem);
        return EXIT_FAILURE;
    }
 

    listen_file_descriptor = nu_listen(port, LISTEN_BACKLOG);

    if (listen_file_descriptor < 0)
    {
        scheduler_stop(&g_scheduler);
        registry_destroy(&g_registry);
        sem_destroy(&g_counting_sem);
        return EXIT_FAILURE;
    }

    printf("listening on port %u — Ctrl-C to stop\n", port);
    fflush(stdout);

    unsigned long accepted = 0; 

    while (g_running)
    {                        
         
        if (sem_wait(&g_counting_sem) < 0){
            
            if (errno == EINTR){
                continue;             
            }

            perror("sem_wait");
            break;
        }
        
        
        int client_file_descriptor = accept4(listen_file_descriptor, NULL, NULL, SOCK_CLOEXEC);
        if (client_file_descriptor < 0)
        {
            sem_post(&g_counting_sem);
            if (errno == EINTR)
            {
                continue;
            }

            if(!g_running)
            {
                break;
            }

            perror("accept");
            break;
        }
        
        connection_t *conn = malloc(sizeof(connection_t));

        if (conn == NULL)
        {
            fprintf(stderr, "out of memory, dropping connection\n");
            close(client_file_descriptor);
            sem_post(&g_counting_sem); 
            continue;
        }

        conn->file_descriptor = client_file_descriptor;
        conn->connection_id = ++accepted;

        pthread_t thread_id;

        int pthread_created = pthread_create(&thread_id, NULL, handle_connection, conn);

        if (pthread_created != 0)
        {
            fprintf(stderr, "pthread_create failed %s\n", strerror(pthread_created));
            close(client_file_descriptor);
            free(conn);
            --accepted;
            sem_post(&g_counting_sem); 
            continue;
        }
        
        pthread_created = pthread_detach(thread_id);
        if (pthread_created != 0)
        {
            fprintf(stderr, "pthread_detach failed %s\n", strerror(pthread_created));
        }

    }

    if (close(listen_file_descriptor))
    {
        perror("close(listen_file_descriptor)");
    }

    //wake every handler blocked in LOGS FOLLOW
    registry_close_logs(&g_registry);

    //wait for every handler first: a handler may still submit to the scheduler
    for(int i = 0; i < MAX_ACTIVE_THREADS; i++){
        sem_wait_retry(&g_counting_sem);
    }


    size_t canceled = registry_cancel_all(&g_registry);

    scheduler_stop(&g_scheduler);
    registry_destroy(&g_registry);

    sem_destroy(&g_counting_sem);

    printf("server stopped (%lu connections accepted, %zu runs canceled)\n", accepted, canceled);

    return EXIT_SUCCESS;
}