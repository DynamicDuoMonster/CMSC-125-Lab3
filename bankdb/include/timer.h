#ifndef TIMER_H
#define TIMER_H

#include <pthread.h>
#include <stdatomic.h>

extern _Atomic int global_tick;
extern pthread_mutex_t tick_lock;
extern pthread_cond_t  tick_changed;
extern int tick_interval_ms;

void timer_init(int interval_ms);
void *timer_thread(void *arg);
void wait_until_tick(int target_tick);
void timer_stop(void);

#endif /* TIMER_H */