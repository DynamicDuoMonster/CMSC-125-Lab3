# CMSC-125 Lab 3 — Concurrent Banking System

## Members

| Name |
|------|
| _Almonia, D. L._ |
| _Niedes, S._ |

---

## Project Overview

A simulated concurrent banking system written in C. It models multiple transactions executing
concurrently against shared bank accounts, using POSIX threads and synchronization primitives
to guarantee correctness under contention.

Core concepts demonstrated:

- Multithreaded transaction execution with a logical simulation clock
- Reader-writer locks for per-account concurrency
- Deadlock prevention via lock ordering AND detection via wait-for graph (DFS cycle detection)
- A bounded buffer pool using semaphores (producer-consumer pattern)
- Metrics collection for throughput, lock contention, and wait times

---

## Repository Structure

```
.
├── include/
│   ├── bank.h           # Bank and Account structs
│   ├── buffer_pool.h    # BufferPool struct and API
│   ├── lock_mgr.h       # Deadlock strategy enum and lock manager API
│   ├── metrics.h        # Metrics struct and collection API
│   ├── timer.h          # Global tick and timer thread declarations
│   └── transaction.h    # Transaction, Operation, TxStatus types
│
├── src/
│   ├── bank.c           # Account initialization, balance operations, transfer dispatch
│   ├── buffer_pool.c    # Semaphore-based pool: load/unload account
│   ├── lock_mgr.c       # Lock ordering and deadlock detection via wait-for graph
│   ├── metrics.c        # Metrics recording and printing
│   ├── timer.c          # Timer thread: increments global_tick each tick
│   ├── transaction.c    # execute_transaction, thread execution logic
│   ├── utils.c          # CLI argument parser
│   └── main.c           # Entry point: load accounts and trace, spawn threads, print results
│
├── tests/
│   ├── accounts.txt         # Initial account balances (general)
│   ├── accounts_test1.txt   # Initial balances for Test 1
│   ├── trace_simple.txt     # Test 1: No conflicts
│   ├── trace_readers.txt    # Test 2: Concurrent readers
│   ├── trace_deadlock.txt   # Test 3: Deadlock scenario
│   ├── trace_abort.txt      # Test 4: Insufficient funds
│   └── trace_buffer.txt     # Test 5: Buffer pool saturation
│
├── docs/
│   └── design.md        # Full design rationale and measured results
├── Makefile
└── README.md            # This file
```

---

## Compilation

```bash
make
```

**Debug build with ThreadSanitizer:**
```bash
make debug
```

**Run:**
```bash
./bankdb --accounts=<accounts_file> --trace=<trace_file> \
         [--deadlock=prevention|detection] \
         [--tick-ms=N] \
         [--verbose]
```

| Option | Description | Default |
|--------|-------------|---------|
| `--accounts=FILE` | Initial account balances file | required |
| `--trace=FILE` | Transaction workload trace file | required |
| `--deadlock=prevention\|detection` | Deadlock handling strategy | `prevention` |
| `--tick-ms=N` | Milliseconds per simulation tick | `100` |
| `--verbose` | Print detailed per-operation logs | off |

---

## Trace File Format

Each line describes one operation belonging to a transaction:

```
T<id> <start_tick> <OPERATION> [args...]
```

Supported operations:

| Operation | Format | Description |
|-----------|--------|-------------|
| `DEPOSIT` | `T1 0 DEPOSIT <acc> <amount>` | Add centavos to account |
| `WITHDRAW` | `T2 1 WITHDRAW <acc> <amount>` | Remove centavos (aborts if insufficient) |
| `TRANSFER` | `T3 2 TRANSFER <from> <to> <amount>` | Move centavos between accounts |
| `BALANCE` | `T4 3 BALANCE <acc>` | Print current balance |

Amounts are in **centavos** (100 centavos = PHP 1.00).

---

## Implemented Features

### Week 1

- Updated design documentation (`design.md`)
- Initial repository commits and project structure

### Week 2

- Implemented CLI argument parsing (`utils.c`)
- Implemented timer thread — increments `global_tick` at fixed intervals, broadcasts on `tick_changed` (`timer.c`, `timer.h`)
- Implemented bank and account logic — initialization from file, balance mutations, transfer dispatch (`bank.c`)
- Implemented buffer pool — bounded producer-consumer pool using `sem_t` and `pthread_mutex_t` for thread-safe account load/unload (`buffer_pool.c`)
- Implemented lock manager — deadlock prevention via lock ordering and deadlock detection via wait-for graph with DFS cycle detection; resolves deadlock by aborting the youngest waiting transaction (`lock_mgr.c`)
- Implemented transaction execution — each transaction runs in its own pthread, waits for its `start_tick`, executes all operations, and reports commit or abort (`transaction.c`)
- Implemented metrics — transaction performance table, throughput, average wait time, buffer pool statistics, and money conservation check (`metrics.c`)
- Implemented full thread lifecycle in `main.c` — spawns one pthread per transaction, joins all threads, starts and stops the timer thread
- Added `Makefile` with `all`, `debug`, `clean`, `test`, and `tsan` targets

### Week 3

- Fixed `tx_id` routing: added `tx_id` parameter to `transfer()` in `bank.c` and propagated it from `execute_transaction()` through to `transfer_detection()` in `lock_mgr.c`, replacing the previous hard-coded `-1`
- Fixed buffer pool pre-loading: `execute_transaction()` now collects all unique account IDs (including both sides of TRANSFER operations) and calls `load_account()` for each before the operation loop begins; `unload_account()` is called on both commit and abort paths
- Fixed `get_balance()` in `bank.c` to call `load_account()` and `unload_account()` so BALANCE operations go through the buffer pool consistently with all other account accesses
- Fixed `buffer_pool.c` semaphore design: removed the `full_slots` semaphore and its mismatched producer-consumer pattern; replaced with a single `empty_slots` semaphore where `load_account` decrements (claims a slot) and `unload_account` increments (releases a slot)
- Removed `sem_t full_slots` from `buffer_pool.h` struct to match the updated implementation
- Added `pthread_barrier_t` in `transaction.c` to synchronize all transaction threads at startup, ensuring they compete for buffer slots simultaneously and producing observable pool saturation in Test 5
- Removed unused `net_external_transfers` variable from `metrics.c` to eliminate compiler warning
- Updated `trace_buffer.txt` to use TRANSFER operations so each transaction pre-loads two distinct account slots, driving 12 total slot demands against a pool of 5
- Updated `trace_deadlock.txt` so both T1 and T2 start at tick 0 with opposing transfers, enabling the prevention strategy to demonstrate lock ordering for both directions
- Updated `docs/design.md` with actual measured results from all five tests, replaced all predicted/future-tense content, and added a known limitations section covering Test 3b behavior, WaitTicks resolution, and conservation check scope

---

## Running Tests

```bash
make test       # Run all 5 provided test cases
make test1      # Test 1: No conflicts
make test2      # Test 2: Concurrent readers
make test3a     # Test 3: Deadlock prevention
make test3b     # Test 3: Deadlock detection
make test4      # Test 4: Insufficient funds (abort)
make test5      # Test 5: Buffer pool saturation
make tsan       # ThreadSanitizer runs
```

---

## Design Decisions

See [`docs/design.md`](docs/design.md) for full rationale. Key choices:

**Deadlock handling:** Both strategies are implemented and selectable via `--deadlock=`. Prevention uses lock ordering (always acquire the lower `account_id` lock first, breaking the circular wait Coffman condition). Detection maintains a wait-for graph and runs DFS cycle detection on every lock block; resolution aborts the youngest waiting transaction to minimize wasted work.

**Buffer pool:** Bounded pool using a single `sem_t` (`empty_slots`) and a `pthread_mutex_t` for the critical section. Each transaction pre-loads all required accounts before acquiring any locks, then unloads all on commit or abort. 

**Locking:** Per-account `pthread_rwlock_t` allows concurrent reads (BALANCE operations) while serializing writes (DEPOSIT, WITHDRAW, TRANSFER). Significantly improves throughput on read-heavy workloads compared to plain mutexes.

**Simulation clock:** A dedicated timer thread increments `global_tick` at fixed intervals and broadcasts on `tick_changed`. Transactions call `wait_until_tick()` before starting, enabling reproducible concurrency scenarios.

---

## Progress Log

| Week | Status |
|------|--------|
| Week 1 | Design documentation, initial repository structure and commits |
| Week 2 | Full implementation: CLI parsing, timer, bank operations, buffer pool, lock manager (prevention + detection), transaction execution, metrics, Makefile |
| Week 3 | Correctness fixes: tx_id routing, buffer pool pre-loading, get_balance pool integration, semaphore redesign, barrier-based thread synchronization. Trace file updates for deadlock and saturation tests. Compiler warning fix. design.md updated with actual measured results and known limitations. |