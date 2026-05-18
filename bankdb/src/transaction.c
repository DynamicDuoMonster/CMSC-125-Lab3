#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transaction.h"
#include "bank.h"
#include "timer.h"
#include "buffer_pool.h"
#include "lock_mgr.h"

Transaction transactions[MAX_TRANSACTIONS];
int         num_transactions       = 0;
_Atomic bool    all_transactions_done = false;

extern int verbose;

/* -------------------------------------------------------
 * Trace file parser
 * Format: TxID StartTick Operation AccountID [Amount] [TargetAccount]
 * TxID format: T<number>  (e.g. T1, T2 …)
 * ------------------------------------------------------- */
bool load_transactions(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) { perror("fopen trace"); return false; }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        char tx_label[32], op_str[32];
        int start_tick, account_id;

        if (sscanf(line, "%31s %d %31s %d", tx_label, &start_tick, op_str, &account_id) < 4)
            continue;

        /* Determine tx_id (T1 → 0, T2 → 1 …) */
        int tx_id = atoi(tx_label + 1) - 1;
        if (tx_id < 0 || tx_id >= MAX_TRANSACTIONS) continue;

        /* Initialise transaction slot on first reference */
        if (transactions[tx_id].tx_id == 0 && transactions[tx_id].num_ops == 0) {
            transactions[tx_id].tx_id      = tx_id + 1; /* 1-based for display */
            transactions[tx_id].start_tick = start_tick;
            transactions[tx_id].status     = TX_PENDING;
        }
        /* Keep the earliest start_tick */
        if (start_tick < transactions[tx_id].start_tick)
            transactions[tx_id].start_tick = start_tick;

        /* Build operation */
        Operation op;
        memset(&op, 0, sizeof(op));
        op.account_id = account_id;

        if (strcmp(op_str, "DEPOSIT") == 0) {
            op.type = OP_DEPOSIT;
            sscanf(line, "%*s %*d %*s %*d %d", &op.amount_centavos);
        } else if (strcmp(op_str, "WITHDRAW") == 0) {
            op.type = OP_WITHDRAW;
            sscanf(line, "%*s %*d %*s %*d %d", &op.amount_centavos);
        } else if (strcmp(op_str, "TRANSFER") == 0) {
            op.type = OP_TRANSFER;
            sscanf(line, "%*s %*d %*s %*d %d %d",
                   &op.target_account, &op.amount_centavos);
        } else if (strcmp(op_str, "BALANCE") == 0) {
            op.type = OP_BALANCE;
        } else {
            continue;
        }

        int n = transactions[tx_id].num_ops;
        if (n < MAX_OPS_PER_TX)
            transactions[tx_id].ops[n++] = op;
        transactions[tx_id].num_ops = n;

        /* Track highest tx_id seen */
        if (tx_id + 1 > num_transactions)
            num_transactions = tx_id + 1;
    }

    fclose(fp);
    return true;
}

/* -------------------------------------------------------
 * Transaction thread
 * ------------------------------------------------------- */
void *execute_transaction(void *arg)
{
    Transaction *tx = (Transaction *)arg;

    wait_until_tick(tx->start_tick);
    tx->actual_start = atomic_load(&global_tick);
    tx->status       = TX_RUNNING;

    if (verbose)
        printf("Tick %d: T%d started\n", tx->actual_start, tx->tx_id);

    // Pre-load all accounts into buffer pool before acquiring locks
    // Collect unique account IDs
    int unique_accounts[MAX_OPS_PER_TX * 2]; // TRANSFER uses 2 accounts
    int num_unique = 0;
    
    for (int i = 0; i < tx->num_ops; i++) {
        Operation *op = &tx->ops[i];
        bool found = false;
        
        // Check if account_id already in list
        for (int j = 0; j < num_unique; j++) {
            if (unique_accounts[j] == op->account_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            unique_accounts[num_unique++] = op->account_id;
        }
        
        // TRANSFER also uses target_account
        if (op->type == OP_TRANSFER) {
            found = false;
            for (int j = 0; j < num_unique; j++) {
                if (unique_accounts[j] == op->target_account) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                unique_accounts[num_unique++] = op->target_account;
            }
        }
    }
    
    // Load all unique accounts atomically
    if (num_unique > 0) {
        load_all_accounts(&buffer_pool, unique_accounts, num_unique);
    }

    // Execute operations
    for (int i = 0; i < tx->num_ops; i++) {
        Operation *op = &tx->ops[i];
        int tick_before = atomic_load(&global_tick);

        switch (op->type) {

        case OP_DEPOSIT:
            if (verbose)
                printf("  T%d: DEPOSIT account %d amount PHP %d.%02d\n",
                       tx->tx_id, op->account_id,
                       op->amount_centavos / 100, op->amount_centavos % 100);
            deposit(op->account_id, op->amount_centavos);
            if (verbose)
                printf("  T%d: DEPOSIT successful\n", tx->tx_id);
            break;

        case OP_WITHDRAW:
            if (verbose)
                printf("  T%d: WITHDRAW account %d amount PHP %d.%02d\n",
                       tx->tx_id, op->account_id,
                       op->amount_centavos / 100, op->amount_centavos % 100);
            if (!withdraw(op->account_id, op->amount_centavos)) {
                printf("  T%d: ABORTED — insufficient funds (account %d)\n",
                       tx->tx_id, op->account_id);
                tx->status    = TX_ABORTED;
                tx->actual_end = atomic_load(&global_tick);
                // Unload all accounts on abort
                for (int j = 0; j < num_unique; j++) {
                    unload_account(&buffer_pool, unique_accounts[j]);
                }
                return NULL;
            }
            if (verbose)
                printf("  T%d: WITHDRAW successful\n", tx->tx_id);
            break;

        case OP_TRANSFER:
            if (verbose)
                printf("  T%d: TRANSFER from %d to %d amount PHP %d.%02d\n",
                       tx->tx_id, op->account_id, op->target_account,
                       op->amount_centavos / 100, op->amount_centavos % 100);
            if (!transfer(op->account_id, op->target_account, op->amount_centavos, tx->tx_id - 1)) {
                printf("  T%d: ABORTED — transfer failed (account %d → %d)\n",
                       tx->tx_id, op->account_id, op->target_account);
                tx->status    = TX_ABORTED;
                tx->actual_end = atomic_load(&global_tick);
                // Unload all accounts on abort
                for (int j = 0; j < num_unique; j++) {
                    unload_account(&buffer_pool, unique_accounts[j]);
                }
                return NULL;
            }
            if (verbose)
                printf("  T%d: TRANSFER successful\n", tx->tx_id);
            break;

        case OP_BALANCE: {
            int balance = get_balance(op->account_id);
            printf("  T%d: Account %d balance = PHP %d.%02d\n",
                   tx->tx_id, op->account_id,
                   balance / 100, balance % 100);
            break;
        }
        } /* switch */

        tx->wait_ticks += (atomic_load(&global_tick) - tick_before);
    }

    tx->actual_end = atomic_load(&global_tick);
    tx->status     = TX_COMMITTED;

    // Unload all accounts after successful completion
    for (int i = 0; i < num_unique; i++) {
        unload_account(&buffer_pool, unique_accounts[i]);
    }

    if (verbose)
        printf("Tick %d: T%d committed\n", tx->actual_end, tx->tx_id);

    return NULL;
}

/* -------------------------------------------------------
 * Launch all transaction threads
 * ------------------------------------------------------- */
void run_all_transactions(void)
{
    for (int i = 0; i < num_transactions; i++) {
        if (transactions[i].num_ops == 0) continue;
        pthread_create(&transactions[i].thread, NULL,
                       execute_transaction, &transactions[i]);
    }

    for (int i = 0; i < num_transactions; i++) {
        if (transactions[i].num_ops == 0) continue;
        pthread_join(transactions[i].thread, NULL);
    }

    atomic_store(&all_transactions_done, true);
}

/* -------------------------------------------------------
 * Metrics table
 * ------------------------------------------------------- */
void print_transaction_metrics(void)
{
    printf("\n=== Transaction Performance Metrics ===\n");
    printf("%-6s | %-10s | %-12s | %-5s | %-10s | %-10s\n",
           "TxID", "StartTick", "ActualStart", "End", "WaitTicks", "Status");
    printf("-------|------------|--------------|-------|------------|----------\n");

    int committed = 0, aborted = 0;
    double total_wait = 0;

    for (int i = 0; i < num_transactions; i++) {
        Transaction *tx = &transactions[i];
        if (tx->num_ops == 0) continue;

        const char *status_str =
            (tx->status == TX_COMMITTED) ? "COMMITTED" :
            (tx->status == TX_ABORTED)   ? "ABORTED"   : "RUNNING";

        printf("T%-5d | %-10d | %-12d | %-5d | %-10d | %s\n",
               tx->tx_id, tx->start_tick, tx->actual_start,
               tx->actual_end, tx->wait_ticks, status_str);

        if (tx->status == TX_COMMITTED) committed++;
        else if (tx->status == TX_ABORTED) aborted++;
        total_wait += tx->wait_ticks;
    }

    int total_ticks = atomic_load(&global_tick);
    int total_tx    = committed + aborted;

    printf("\nAverage wait time : %.2f ticks\n",
           total_tx > 0 ? total_wait / total_tx : 0.0);
    printf("Throughput        : %d transactions / %d ticks = %.2f tx/tick\n",
           total_tx, total_ticks,
           total_ticks > 0 ? (double)total_tx / total_ticks : 0.0);
}