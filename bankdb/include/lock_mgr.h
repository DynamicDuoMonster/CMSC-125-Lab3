#ifndef LOCK_MGR_H
#define LOCK_MGR_H

#include <stdbool.h>

typedef enum {
    DEADLOCK_PREVENTION,
} DeadlockStrategy;

extern DeadlockStrategy deadlock_strategy;

/*
 * transfer() implementations are in bank.c and call these helpers
 * depending on the chosen strategy.
 */
bool transfer_prevention(int from_id, int to_id, int amount_centavos);

#endif /* LOCK_MGR_H */