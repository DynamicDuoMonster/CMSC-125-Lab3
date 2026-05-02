#include <stdio.h>
#include <stdlib.h>
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

    /*
     * Economy-level conservation: model an external wallet that starts at 0.
     * Every committed DEPOSIT moves money *into* the bank from outside
     *   wallet decreases by amount (external party paid in).
     * Every committed WITHDRAW moves money *out* of the bank to outside
     *   wallet increases by amount (external party received).
     * Transfers are internal; they do not touch the wallet.
     *
     * Invariant: bank_total + external_wallet == initial_bank_total
     * i.e. money is neither created nor destroyed across the whole system.
     */
    int external_wallet = 0;

    for (int i = 0; i < num_transactions; i++) {
        Transaction *tx = &transactions[i];
        if (tx->status != TX_COMMITTED) continue;
        for (int j = 0; j < tx->num_ops; j++) {
            if (tx->ops[j].type == OP_DEPOSIT)
                external_wallet -= tx->ops[j].amount_centavos;
            else if (tx->ops[j].type == OP_WITHDRAW)
                external_wallet += tx->ops[j].amount_centavos;
        }
    }

    int system_total = final_total + external_wallet;

    printf("\nInitial bank total    : PHP %d.%02d\n",
           initial_total / 100, initial_total % 100);
    printf("Final bank total      : PHP %d.%02d\n",
           final_total / 100, final_total % 100);
    printf("External wallet delta : PHP %d.%02d  (%s)\n",
           abs(external_wallet) / 100, abs(external_wallet) % 100,
           external_wallet >= 0 ? "net outflow from bank" : "net inflow to bank");
    printf("System total          : PHP %d.%02d  (bank + wallet)\n",
           system_total / 100, system_total % 100);
    printf("Conservation check    : %s\n",
           (system_total == initial_total) ? "PASSED" : "FAILED");
}