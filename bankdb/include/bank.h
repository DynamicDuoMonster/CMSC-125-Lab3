#ifndef BANK_H
#define BANK_H

#include <pthread.h>
#include <stdbool.h>

#define MAX_ACCOUNTS 100

typedef struct {
    int account_id;
    int balance_centavos;
    pthread_rwlock_t lock;
} Account;

typedef struct {
    Account accounts[MAX_ACCOUNTS];
    int num_accounts;
    pthread_mutex_t bank_lock;
} Bank;

extern Bank bank;

void bank_init(void);
bool bank_load_accounts(const char *filename);
int  get_balance(int account_id);
void deposit(int account_id, int amount_centavos);
bool withdraw(int account_id, int amount_centavos);
bool transfer(int from_id, int to_id, int amount_centavos, int tx_id);
int  find_account_idx(int account_id);
int  bank_total_balance(void);
void bank_print_all_balances(void);

#endif /* BANK_H */