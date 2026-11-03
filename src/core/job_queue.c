#define _POSIX_C_SOURCE 200809L

#include "job_queue.h"

#include <errno.h>
#include <stdlib.h>

int job_queue_init(job_queue_t *queue)
{
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    queue->closed = false;
    if (sem_init(&queue->items, 0, 0) < 0) {
        return -1;
    }
    omp_init_lock(&queue->lock);
    return 0;
}

void job_queue_destroy(job_queue_t *queue)
{
    job_node_t *node = queue->head;
    while (node != NULL) {
        job_node_t *next = node->next;
        free(node);
        node = next;
    }
    queue->head = NULL;
    queue->tail = NULL;
    omp_destroy_lock(&queue->lock);
    sem_destroy(&queue->items);
}

int job_queue_push(job_queue_t *queue, run_t *run, size_t job)
{
    job_node_t *node = malloc(sizeof *node);
    if (node == NULL) {
        return -1;
    }
    node->run = run;
    node->job = job;
    node->next = NULL;

    omp_set_lock(&queue->lock);
    if (queue->tail == NULL) {
        queue->head = node;
    } else {
        queue->tail->next = node;
    }
    queue->tail = node;
    queue->size++;
    omp_unset_lock(&queue->lock);

    sem_post(&queue->items);
    return 0;
}

bool job_queue_pop(job_queue_t *queue, run_t **run, size_t *job)
{
    while (sem_wait(&queue->items) < 0 && errno == EINTR) {
    }

    omp_set_lock(&queue->lock);
    job_node_t *node = queue->head;
    if (node == NULL) {
        omp_unset_lock(&queue->lock);
        return false;
    }
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->size--;
    omp_unset_lock(&queue->lock);

    *run = node->run;
    *job = node->job;
    free(node);
    return true;
}

void job_queue_close(job_queue_t *queue, size_t n_waiters)
{
    omp_set_lock(&queue->lock);
    queue->closed = true;
    omp_unset_lock(&queue->lock);

    for (size_t i = 0; i < n_waiters; i++) {
        sem_post(&queue->items);
    }
}

size_t job_queue_size(job_queue_t *queue)
{
    omp_set_lock(&queue->lock);
    size_t size = queue->size;
    omp_unset_lock(&queue->lock);
    return size;
}