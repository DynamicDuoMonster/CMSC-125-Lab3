#include <stdio.h>
#include "metrics.h"
#include "transaction.h"
#include "bank.h"
#include "timer.h"

void metrics_print_summary(void)
{
    int committed = 0, aborted = 0;
    for (int i = 0; i < num_transactions; i++) {
        if (transactions[i].num_ops == 0) continue;
        if (transactions[i].status == TX_COMMITTED) committed++;
        else if (transactions[i].status == TX_ABORTED)  aborted++;
    }

    printf("\n=== Summary ===\n");
    printf("Total transactions    : %d\n", committed + aborted);
    printf("Committed             : %d\n", committed);
    printf("Aborted               : %d\n", aborted);
    printf("Total ticks           : %d\n", global_tick);
}

void metrics_check_conservation(int initial_total)
{
    int final_total = bank_total_balance();
    int committed_deposits  = 0;
    int committed_withdraws = 0;
    int net_external_transfers = 0;

    for (int i = 0; i < num_transactions; i++) {
        Transaction *tx = &transactions[i];
        if (tx->status != TX_COMMITTED) continue;
        for (int j = 0; j < tx->num_ops; j++) {
            if (tx->ops[j].type == OP_DEPOSIT)
                committed_deposits  += tx->ops[j].amount_centavos;
            else if (tx->ops[j].type == OP_WITHDRAW)
                committed_withdraws += tx->ops[j].amount_centavos;
            else if (tx->ops[j].type == OP_TRANSFER) {
                // Transfers preserve total within bank; no net effect
            }
        }
    }

    int expected = initial_total + committed_deposits - committed_withdraws;

    printf("\nInitial total : PHP %d.%02d\n",
           initial_total / 100, initial_total % 100);
    printf("Final total   : PHP %d.%02d\n",
           final_total / 100, final_total % 100);
    printf("Expected total: PHP %d.%02d  (initial + deposits - withdrawals)\n",
           expected / 100, expected % 100);
    printf("Conservation check : %s\n",
           (final_total == expected) ? "PASSED" : "FAILED");
    
    // Additional wallet-level check
    printf("\nNote: Conservation check validates bank ledger consistency.\n");
    printf("Full system-level conservation requires per-user wallet tracking\n");
    printf("not implemented in this version (DEPOSIT source/WITHDRAW destination unknown).\n");
}