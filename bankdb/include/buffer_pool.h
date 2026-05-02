#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <semaphore.h>
#include <pthread.h>
#include <stdbool.h>
#include "bank.h"

#define BUFFER_POOL_SIZE 5

typedef struct {
    int     account_id;
    Account *data;
    bool    in_use;
} BufferSlot;

typedef struct {
    BufferSlot      slots[BUFFER_POOL_SIZE];
    sem_t           empty_slots;   /* counts free slots; load claims, unload releases */
    pthread_mutex_t pool_lock;

    /* Statistics */
    int total_loads;
    int total_unloads;
    int peak_usage;
    int current_usage;
    int blocked_ops;
} BufferPool;

extern BufferPool buffer_pool;

void init_buffer_pool(BufferPool *pool);
void load_account(BufferPool *pool, int account_id);
void unload_account(BufferPool *pool, int account_id);
void print_buffer_pool_stats(BufferPool *pool);

#endif /* BUFFER_POOL_H */