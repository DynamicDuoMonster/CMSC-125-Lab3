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
/* =========================================================
 * Strategy B: Deadlock Detection via Wait-For Graph
 * ========================================================= */

typedef struct {
    int tx_id;
    int waiting_for_tx;       /* -1 if not waiting */
    int waiting_for_account;
} WaitForEntry;

static WaitForEntry wait_graph[MAX_TRANSACTIONS];
static pthread_mutex_t graph_lock = PTHREAD_MUTEX_INITIALIZER;

void lock_mgr_init(DeadlockStrategy strategy)
{
    deadlock_strategy = strategy;
    for (int i = 0; i < MAX_TRANSACTIONS; i++) {
        wait_graph[i].tx_id              = i;
        wait_graph[i].waiting_for_tx     = -1;
        wait_graph[i].waiting_for_account = -1;
    }
}

void record_wait(int tx_id, int account_id, int holder_tx)
{
    if (tx_id < 0 || tx_id >= MAX_TRANSACTIONS) return;
    pthread_mutex_lock(&graph_lock);
    wait_graph[tx_id].waiting_for_tx      = holder_tx;
    wait_graph[tx_id].waiting_for_account = account_id;
    pthread_mutex_unlock(&graph_lock);
}

void clear_wait(int tx_id)
{
    if (tx_id < 0 || tx_id >= MAX_TRANSACTIONS) return;
    pthread_mutex_lock(&graph_lock);
    wait_graph[tx_id].waiting_for_tx      = -1;
    wait_graph[tx_id].waiting_for_account = -1;
    pthread_mutex_unlock(&graph_lock);
}

static bool has_cycle(int tx_id, bool *visited, bool *rec_stack,
                      int depth, int max_depth)
{
    if (depth > max_depth) return false;
    visited[tx_id]   = true;
    rec_stack[tx_id] = true;

    int next = wait_graph[tx_id].waiting_for_tx;
    if (next != -1 && next < MAX_TRANSACTIONS) {
        if (!visited[next]) {
            if (has_cycle(next, visited, rec_stack, depth + 1, max_depth))
                return true;
        } else if (rec_stack[next]) {
            return true; /* cycle detected */
        }
    }

    rec_stack[tx_id] = false;
    return false;
}

bool detect_deadlock(void)
{
    pthread_mutex_lock(&graph_lock);

    bool visited[MAX_TRANSACTIONS]   = {false};
    bool rec_stack[MAX_TRANSACTIONS] = {false};

    for (int i = 0; i < num_transactions; i++) {
        if (!visited[i]) {
            if (has_cycle(i, visited, rec_stack, 0, num_transactions)) {
                pthread_mutex_unlock(&graph_lock);
                return true;
            }
        }
    }

    pthread_mutex_unlock(&graph_lock);
    return false;
}

void resolve_deadlock(void)
{
    /*
     * Abort the youngest transaction that is currently waiting —
     * i.e., the one with the highest tx_id that has waiting_for_tx != -1.
     */
    pthread_mutex_lock(&graph_lock);
    int victim = -1;
    for (int i = num_transactions - 1; i >= 0; i--) {
        if (wait_graph[i].waiting_for_tx != -1) {
            victim = i;
            break;
        }
    }
    pthread_mutex_unlock(&graph_lock);

    if (victim >= 0) {
        printf("  [DEADLOCK DETECTED] Aborting youngest waiting transaction T%d\n",
               victim + 1);
        transactions[victim].status = TX_ABORTED;
        clear_wait(victim);
    }
}

bool transfer_detection(int from_id, int to_id, int amount_centavos, int tx_id)
{
    int from_idx = find_account_idx(from_id);
    int to_idx   = find_account_idx(to_id);
    if (from_idx < 0 || to_idx < 0) {
        fprintf(stderr, "transfer_detection: unknown account\n");
        return false;
    }

    /* Try to acquire from lock */
    if (pthread_rwlock_trywrlock(&bank.accounts[from_idx].lock) != 0) {
        /* Could not get lock — record wait and check for deadlock */
        record_wait(tx_id, from_id, /*holder_tx*/ -1);
        if (detect_deadlock()) {
            resolve_deadlock();
            clear_wait(tx_id);
            return false;
        }
        /* Fall back to blocking acquire */
        pthread_rwlock_wrlock(&bank.accounts[from_idx].lock);
        clear_wait(tx_id);
    }

    if (pthread_rwlock_trywrlock(&bank.accounts[to_idx].lock) != 0) {
        record_wait(tx_id, to_id, /*holder_tx*/ -1);
        if (detect_deadlock()) {
            resolve_deadlock();
            clear_wait(tx_id);
            pthread_rwlock_unlock(&bank.accounts[from_idx].lock);
            return false;
        }
        pthread_rwlock_wrlock(&bank.accounts[to_idx].lock);
        clear_wait(tx_id);
    }

    if (bank.accounts[from_idx].balance_centavos < amount_centavos) {
        pthread_rwlock_unlock(&bank.accounts[to_idx].lock);
        pthread_rwlock_unlock(&bank.accounts[from_idx].lock);
        return false;
    }

    bank.accounts[from_idx].balance_centavos -= amount_centavos;
    bank.accounts[to_idx].balance_centavos   += amount_centavos;

    pthread_rwlock_unlock(&bank.accounts[to_idx].lock);
    pthread_rwlock_unlock(&bank.accounts[from_idx].lock);
    return true;
}