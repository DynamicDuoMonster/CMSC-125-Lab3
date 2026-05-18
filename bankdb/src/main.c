#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "bank.h"
#include "transaction.h"
#include "timer.h"
#include "lock_mgr.h"
#include "buffer_pool.h"
#include "metrics.h"

/* Shared verbose flag (extern'd from transaction.c) */
int verbose = 0;

/* Prototype from utils.c */
void parse_args(int argc, char **argv,
                const char **accounts_file,
                const char **trace_file,
                DeadlockStrategy *strategy,
                int *tick_ms,
                int *verbose_out);

int main(int argc, char **argv)
{
    const char       *accounts_file = NULL;
    const char       *trace_file    = NULL;
    DeadlockStrategy  strategy      = DEADLOCK_PREVENTION;
    int               tick_ms       = 100;

    parse_args(argc, argv, &accounts_file, &trace_file,
               &strategy, &tick_ms, &verbose);

    printf("=== Banking System Execution Log ===\n");
    printf("Deadlock strategy : %s\n",
           strategy == DEADLOCK_PREVENTION ? "prevention" : "unknown");

    /* Initialise subsystems */
    bank_init();
    init_buffer_pool(&buffer_pool);
    timer_init(tick_ms);

    /* Load data */
    if (!bank_load_accounts(accounts_file)) {
        fprintf(stderr, "Failed to load accounts from %s\n", accounts_file);
        return EXIT_FAILURE;
    }
    if (!load_transactions(trace_file)) {
        fprintf(stderr, "Failed to load transactions from %s\n", trace_file);
        return EXIT_FAILURE;
    }

    int initial_total = bank_total_balance();

    /* Start timer thread */
    pthread_t timer_tid;
    pthread_create(&timer_tid, NULL, timer_thread, NULL);

    /* Run all transaction threads */
    run_all_transactions();

    /* Stop timer */
    timer_stop();
    pthread_join(timer_tid, NULL);

    /* Reports */
    bank_print_all_balances();
    print_transaction_metrics();
    print_buffer_pool_stats(&buffer_pool);
    metrics_print_summary();
    metrics_check_conservation(initial_total);

    return EXIT_SUCCESS;
}
