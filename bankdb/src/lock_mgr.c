#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lock_mgr.h"
#include "bank.h"
#include "buffer_pool.h"
#include "transaction.h"

DeadlockStrategy deadlock_strategy = DEADLOCK_PREVENTION;

/* =========================================================
 * Strategy A: Lock Ordering (Prevention)
 *   Always acquire the lower account_id lock first.
 *   This breaks the "circular wait" Coffman condition.
 * ========================================================= */
bool transfer_prevention(int from_id, int to_id, int amount_centavos)
{
    int from_idx = find_account_idx(from_id);
    int to_idx   = find_account_idx(to_id);
    if (from_idx < 0 || to_idx < 0) {
        fprintf(stderr, "transfer_prevention: unknown account\n");
        return false;
    }

    /* Determine lock order by account_id (not array index) */
    int first_id  = (from_id < to_id) ? from_id : to_id;
    int second_id = (from_id < to_id) ? to_id   : from_id;
    int first_idx  = find_account_idx(first_id);
    int second_idx = find_account_idx(second_id);

    if (deadlock_strategy == DEADLOCK_PREVENTION) {
        printf("  [DEADLOCK PREVENTED] Lock ordering: acquiring account %d before account %d\n",
               first_id, second_id);
    }

    pthread_rwlock_wrlock(&bank.accounts[first_idx].lock);
    pthread_rwlock_wrlock(&bank.accounts[second_idx].lock);

    /* Check sufficient funds */
    if (bank.accounts[from_idx].balance_centavos < amount_centavos) {
        pthread_rwlock_unlock(&bank.accounts[second_idx].lock);
        pthread_rwlock_unlock(&bank.accounts[first_idx].lock);
        return false;
    }

    bank.accounts[from_idx].balance_centavos -= amount_centavos;
    bank.accounts[to_idx].balance_centavos   += amount_centavos;

    pthread_rwlock_unlock(&bank.accounts[second_idx].lock);
    pthread_rwlock_unlock(&bank.accounts[first_idx].lock);
    return true;
}

