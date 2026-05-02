# Design Documentation

## 1. Deadlock Strategy Choice

### Chosen Strategy: Prevention via Lock Ordering

We implemented **deadlock prevention via lock ordering**. Detection via a wait-for graph was prototyped but removed (see Known Limitations, §5).

### Why Prevention?

**Prevention (lock ordering)** was chosen as the final strategy because:
- It guarantees deadlocks never occur by breaking the circular-wait Coffman condition
- It is simple, correct, and has no runtime overhead for cycle detection
- It produced clean, deterministic results in all tests

Detection was explored but removed because at 50ms tick resolution, each transfer completes before the opposing thread acquires its first lock, so no actual circular wait ever formed in our test scenarios. The detection infrastructure would require blocking lock acquisition with explicit wait recording — a more complex design whose correctness could not be demonstrated with our current trace files.

---

### Observed Behavior (Test 3: Deadlock Prevention)

Both T1 and T2 committed successfully. Lock ordering fired for both transfers:

```
[DEADLOCK PREVENTED] Lock ordering: acquiring account 10 before account 20
```

T1 (`TRANSFER 10 → 20`) and T2 (`TRANSFER 20 → 10`) both acquire account 10's lock before account 20's lock, regardless of transfer direction. This eliminates the possibility of circular waiting.

---

## 2. Buffer Pool Integration

### Strategy Overview

A **bounded buffer pool using a single semaphore** (`empty_slots`) follows the pre-load-all pattern. Each transaction claims all required slots before acquiring any locks.

### When Accounts Are Loaded

Before the operation loop begins, `execute_transaction()` collects all unique account IDs touched by the transaction (including both sides of TRANSFER operations) and calls `load_account()` for each.

### When Accounts Are Unloaded

Accounts are unloaded after the transaction completes (commit or abort). This pins the accounts in the pool for the full duration of the transaction, preventing repeated load/unload overhead and avoiding mid-transaction eviction.

---

### Behavior When Pool Is Full

If the buffer pool is full:
- `sem_trywait(&empty_slots)` fails
- `blocked_ops` is incremented atomically
- The thread blocks on `sem_wait(&empty_slots)` until another transaction unloads

---

### Buffer Pool Deadlock Avoidance

A subtle deadlock class can arise independently of lock contention: if T1 holds a slot for Account A and blocks waiting for Account B, while T2 holds Account B and blocks waiting for Account A, the lock-based wait-for graph would not detect this cycle.

**Resolution:** Each transaction pre-loads all required accounts before acquiring any locks. This makes circular buffer-pool waiting structurally impossible.

---

### Measured Results (Test 5: Buffer Pool Saturation)

Trace: 6 concurrent transactions each performing 2 DEPOSIT operations on distinct accounts (12 total slot demands, pool size 5).

| Metric             | Value |
|--------------------|-------|
| Pool size          | 5 slots |
| Total loads        | 12 |
| Total unloads      | 12 |
| Peak usage         | 5 slots |
| Blocked operations | 5 |

The pool reached full capacity and 5 load operations blocked, demonstrating correct bounded-buffer behavior under contention.

---

## 3. Reader-Writer Lock Performance

### Experimental Setup

`pthread_rwlock_t` is used for all account accesses. BALANCE operations acquire a read lock (`pthread_rwlock_rdlock`), while DEPOSIT, WITHDRAW, and TRANSFER acquire a write lock (`pthread_rwlock_wrlock`). This allows multiple concurrent readers to proceed without blocking each other.

Workload used: `trace_readers.txt` — 4 concurrent BALANCE operations on account 10.

---

### Measured Results (Test 2: Concurrent Readers)

| Metric               | Value |
|----------------------|-------|
| Total transactions   | 4     |
| All committed        | yes   |
| Total ticks          | 1     |
| Throughput           | 4.00 tx/tick |
| Average wait time    | 0.00 ticks |
| Buffer pool loads    | 4     |
| Peak usage           | 1 slot |
| Blocked operations   | 0     |

All 4 BALANCE transactions completed within tick 0 with zero wait, confirming that concurrent read locks do not block each other. Under a mutex-based design, these operations would serialize and throughput would drop to approximately 1.00 tx/tick under any non-trivial tick resolution.

---

### Workload With Greatest Improvement

Read-heavy workloads (multiple concurrent BALANCE operations) show the greatest benefit. Write operations (DEPOSIT, WITHDRAW, TRANSFER) are unaffected since they require exclusive locks regardless of lock type.

---

### Known Limitations

- **Writer starvation:** Under sustained read load, new readers can continuously acquire the lock before a waiting writer proceeds. This is acceptable for our read-heavy benchmark but is a known limitation of the reader-writer lock model.
- **WaitTicks resolution:** At 50ms tick resolution, all operations complete within tick 0, so `wait_ticks` is always 0. The buffer pool `blocked_ops` counter is the more meaningful contention metric at this resolution.

---

## 4. Timer Thread Design

### Purpose of the Timer Thread

The timer thread maintains a **global logical clock (`global_tick`)** that controls when transactions begin execution and synchronizes all threads using condition variables.

`global_tick` is protected by `tick_lock` (`pthread_mutex_t`), held when calling `pthread_cond_wait()` and `pthread_cond_broadcast()` as required by the POSIX condition variable API.

---

### Shared Flag Synchronization (`_Atomic`)

Two flags are shared between the timer thread and the main thread:

- `timer_running` — written by `timer_stop()` (main thread), read by `timer_thread()`
- `all_transactions_done` — written by `run_all_transactions()` (main thread), read by `timer_thread()`

Both were originally declared `volatile`. `volatile` only prevents the compiler from caching a value in a register — it does not insert memory barriers, does not prevent CPU reordering, and is not recognized by the C11 memory model as a synchronization primitive. ThreadSanitizer correctly flagged both as data races because it builds a happens-before graph from explicit synchronization operations (`pthread_*`, `atomic_*`), and plain `volatile` stores/loads create no edges in that graph.

Both flags are now declared `_Atomic` (`_Atomic int` and `_Atomic bool`). `atomic_store`/`atomic_load` are formally recognized synchronization points: a store *synchronizes-with* a subsequent load of the same object, establishing the happens-before edge TSan requires. The `tsan` build target now exits clean.

---

### Why It Is Necessary

Without a timer thread:
- Transactions would execute immediately upon creation
- There would be no notion of simulated time
- Start ticks would be meaningless and concurrency scenarios unreproducible

---

### Observed Behavior

In all five tests, the timer thread started correctly and incremented `global_tick` at 50ms intervals. All transactions in the test suite complete within tick 0 at this interval. This confirms the timer is functioning but also explains why `WaitTicks` is uniformly 0 — operations are fast relative to the tick interval.

For tests requiring genuine concurrency (Test 5), the trace file is designed so each transaction pre-loads two distinct account slots, driving 12 total slot demands against a pool of 5 with 6 concurrent threads.

---

### What Breaks Without It

If the timer thread is removed:
- `wait_until_tick()` would block forever on transactions with `start_tick > 0`
- No controlled scheduling of transaction start times would be possible
- Reproducible concurrency scenarios (e.g., Test 3) could not be constructed

---

## 5. Known Limitations and Observations

| Issue | Description |
|---|---|
| Deadlock detection removed | Detection was prototyped but removed. At 50ms tick resolution, opposing transfers complete before any circular wait forms. A correct implementation would require blocking `pthread_rwlock_wrlock` calls with explicit wait-graph recording, which was outside the scope of the final submission. |
| WaitTicks always 0 | All operations complete within a single tick. `blocked_ops` in the buffer pool report is the meaningful contention metric. |
| Conservation check is ledger-level only | `metrics_check_conservation()` verifies `final = initial + deposits − withdrawals` within the bank. Full system-level conservation (bank + external wallets = constant) is not implemented as per-user wallet tracking is outside the scope of this lab. |
| Writer starvation under rwlock | Under sustained read load, new readers can starve waiting writers. Acceptable for our workloads but a known property of the reader-writer lock model. |