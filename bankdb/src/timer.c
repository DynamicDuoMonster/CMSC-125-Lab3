#include <stdio.h>
#include <unistd.h>
#include <stdatomic.h>
#include "timer.h"
#include "transaction.h"

volatile int    global_tick     = 0;
pthread_mutex_t tick_lock       = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  tick_changed    = PTHREAD_COND_INITIALIZER;
int             tick_interval_ms = 100;

static _Atomic int timer_running = 0;

void timer_init(int interval_ms)
{
    tick_interval_ms = interval_ms;
    global_tick      = 0;
    timer_running    = 1;
    printf("Timer thread started (tick interval: %d ms)\n\n", interval_ms);
}

void *timer_thread(void *arg)
{
    (void)arg;
    while (atomic_load(&timer_running) && !atomic_load(&all_transactions_done)) {
        usleep((unsigned int)tick_interval_ms * 1000);

        pthread_mutex_lock(&tick_lock);
        global_tick++;
        pthread_cond_broadcast(&tick_changed);
        pthread_mutex_unlock(&tick_lock);
    }
    return NULL;
}

void wait_until_tick(int target_tick)
{
    pthread_mutex_lock(&tick_lock);
    while (global_tick < target_tick) {
        pthread_cond_wait(&tick_changed, &tick_lock);
    }
    pthread_mutex_unlock(&tick_lock);
}

void timer_stop(void)
{
    atomic_store(&timer_running, 0);
    /* Wake anyone still sleeping on tick_changed */
    pthread_mutex_lock(&tick_lock);
    pthread_cond_broadcast(&tick_changed);
    pthread_mutex_unlock(&tick_lock);
}