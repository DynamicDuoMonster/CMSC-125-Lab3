#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>

#define MAX_TRANSACTIONS 256
#define MAX_OPS_PER_TX   256

typedef enum {
    OP_DEPOSIT,
    OP_WITHDRAW,
    OP_TRANSFER,
    OP_BALANCE,
    OP_WAIT,
} OpType;

typedef struct {
    OpType type;
    int account_id;
    int amount_centavos;
    int target_account;
} Operation;

typedef enum {
    TX_PENDING,
    TX_RUNNING,
    TX_COMMITTED,
    TX_ABORTED
} TxStatus;

typedef struct {
    int tx_id;
    Operation ops[MAX_OPS_PER_TX];
    int num_ops;
    int start_tick;
    pthread_t thread;

    /* Timing (measured in ticks) */
    int actual_start;
    int actual_end;
    int wait_ticks;

    /* Status */
    TxStatus status;
} Transaction;

extern Transaction transactions[MAX_TRANSACTIONS];
extern int num_transactions;
extern _Atomic bool all_transactions_done;

bool load_transactions(const char *filename);
void *execute_transaction(void *arg);
void run_all_transactions(void);
void print_transaction_metrics(void);

#endif /* TRANSACTION_H */