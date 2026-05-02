#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bank.h"
#include "lock_mgr.h"
#include "timer.h"
#include "buffer_pool.h"

Bank bank;

void bank_init(void)
{
    memset(&bank, 0, sizeof(bank));
    pthread_mutex_init(&bank.bank_lock, NULL);
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        pthread_rwlock_init(&bank.accounts[i].lock, NULL);
        bank.accounts[i].account_id = -1;
    }
}

/* Returns the array index for account_id, or -1 if not found. */
int find_account_idx(int account_id)
{
    for (int i = 0; i < bank.num_accounts; i++) {
        if (bank.accounts[i].account_id == account_id)
            return i;
    }
    return -1;
}

bool bank_load_accounts(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("fopen accounts");
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        int id, balance;
        if (sscanf(line, "%d %d", &id, &balance) != 2) continue;

        if (bank.num_accounts >= MAX_ACCOUNTS) {
            fprintf(stderr, "Too many accounts (max %d)\n", MAX_ACCOUNTS);
            fclose(fp);
            return false;
        }

        int idx = bank.num_accounts++;
        bank.accounts[idx].account_id      = id;
        bank.accounts[idx].balance_centavos = balance;
    }

    fclose(fp);
    return true;
}

int get_balance(int account_id)
{
    /*
     * Buffer pool: the account slot is pre-loaded by execute_transaction()
     * before the operation loop begins, so load_account/unload_account are
     * not called here.  All buffer pool accounting for BALANCE operations is
     * captured by the pre-load / post-unload in execute_transaction().
     */
    int idx = find_account_idx(account_id);
    if (idx < 0) { fprintf(stderr, "get_balance: unknown account %d\n", account_id); return -1; }
    Account *acc = &bank.accounts[idx];
    pthread_rwlock_rdlock(&acc->lock);
    int balance = acc->balance_centavos;
    pthread_rwlock_unlock(&acc->lock);
    return balance;
}

void deposit(int account_id, int amount_centavos)
{
    int idx = find_account_idx(account_id);
    if (idx < 0) { fprintf(stderr, "deposit: unknown account %d\n", account_id); return; }
    pthread_rwlock_wrlock(&bank.accounts[idx].lock);
    bank.accounts[idx].balance_centavos += amount_centavos;
    pthread_rwlock_unlock(&bank.accounts[idx].lock);
}

bool withdraw(int account_id, int amount_centavos)
{
    int idx = find_account_idx(account_id);
    if (idx < 0) { fprintf(stderr, "withdraw: unknown account %d\n", account_id); return false; }
    pthread_rwlock_wrlock(&bank.accounts[idx].lock);
    if (bank.accounts[idx].balance_centavos < amount_centavos) {
        pthread_rwlock_unlock(&bank.accounts[idx].lock);
        return false;
    }
    bank.accounts[idx].balance_centavos -= amount_centavos;
    pthread_rwlock_unlock(&bank.accounts[idx].lock);
    return true;
}
bool transfer(int from_id, int to_id, int amount_centavos, int tx_id)
{
    (void)tx_id;  /* unused in prevention-only mode */
    return transfer_prevention(from_id, to_id, amount_centavos);
}

int bank_total_balance(void)
{
    int total = 0;
    for (int i = 0; i < bank.num_accounts; i++) {
        pthread_rwlock_rdlock(&bank.accounts[i].lock);
        total += bank.accounts[i].balance_centavos;
        pthread_rwlock_unlock(&bank.accounts[i].lock);
    }
    return total;
}

void bank_print_all_balances(void)
{
    printf("\n=== Final Account Balances ===\n");
    for (int i = 0; i < bank.num_accounts; i++) {
        int bal = bank.accounts[i].balance_centavos;
        printf("  Account %d: PHP %d.%02d\n",
               bank.accounts[i].account_id,
               bal / 100, bal % 100);
    }
}