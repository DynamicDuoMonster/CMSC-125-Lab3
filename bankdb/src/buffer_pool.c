#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "buffer_pool.h"

BufferPool buffer_pool;

void init_buffer_pool(BufferPool *pool)
{
    memset(pool, 0, sizeof(*pool));

    /*
     * empty_slots counts available slots. Starts at BUFFER_POOL_SIZE.
     * Each load_account claims one slot; each unload_account releases one.
     * No separate full_slots semaphore — each transaction manages its own
     * slots rather than acting as separate producer/consumer threads.
     */
    sem_init(&pool->empty_slots, 0, BUFFER_POOL_SIZE);
    pthread_mutex_init(&pool->pool_lock, NULL);

    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        pool->slots[i].account_id = -1;
        pool->slots[i].in_use     = false;
        pool->slots[i].data       = NULL;
    }
}

/*
 * Claim a buffer slot for account_id.
 * Blocks if pool is full — this is the bounded-buffer demonstration.
 * Each call claims an independent slot so concurrent transactions compete
 * for the limited pool space.
 */
void load_account(BufferPool *pool, int account_id)
{
    /* Try non-blocking first; record a block if the pool is full */
    if (sem_trywait(&pool->empty_slots) != 0) {
        __atomic_fetch_add(&pool->blocked_ops, 1, __ATOMIC_SEQ_CST);
        sem_wait(&pool->empty_slots); /* blocks until a slot is freed */
    }

    pthread_mutex_lock(&pool->pool_lock);

    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (!pool->slots[i].in_use) {
            pool->slots[i].account_id = account_id;
            pool->slots[i].data       = &bank.accounts[find_account_idx(account_id)];
            pool->slots[i].in_use     = true;
            pool->total_loads++;
            pool->current_usage++;
            if (pool->current_usage > pool->peak_usage)
                pool->peak_usage = pool->current_usage;
            break;
        }
    }

    pthread_mutex_unlock(&pool->pool_lock);
}

/*
 * Release the slot held for account_id.
 * Posts to empty_slots so a waiting thread can proceed.
 */
void unload_account(BufferPool *pool, int account_id)
{
    pthread_mutex_lock(&pool->pool_lock);

    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (pool->slots[i].in_use && pool->slots[i].account_id == account_id) {
            pool->slots[i].in_use     = false;
            pool->slots[i].account_id = -1;
            pool->slots[i].data       = NULL;
            pool->total_unloads++;
            pool->current_usage--;
            break;
        }
    }

    pthread_mutex_unlock(&pool->pool_lock);
    sem_post(&pool->empty_slots);
}

void print_buffer_pool_stats(BufferPool *pool)
{
    printf("\n=== Buffer Pool Report ===\n");
    printf("Pool size          : %d slots\n", BUFFER_POOL_SIZE);
    printf("Total loads        : %d\n", pool->total_loads);
    printf("Total unloads      : %d\n", pool->total_unloads);
    printf("Peak usage         : %d slots\n", pool->peak_usage);
    printf("Blocked operations : %d\n", pool->blocked_ops);
}