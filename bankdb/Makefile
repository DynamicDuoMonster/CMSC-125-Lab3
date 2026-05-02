CC      = gcc
CFLAGS  = -Wall -Wextra -Iinclude -pthread -O2
DBGFLAGS= -Wall -Wextra -Iinclude -pthread -g -fsanitize=thread
LDFLAGS = -pthread

SRCS = src/main.c \
       src/bank.c \
       src/transaction.c \
       src/timer.c \
       src/lock_mgr.c \
       src/buffer_pool.c \
       src/metrics.c \
       src/utils.c

OBJS     = $(SRCS:.c=.o)
TARGET   = bankdb
DBG_TARGET = bankdb_debug

ACCOUNTS = tests/accounts.txt

.PHONY: all debug clean test test1 test2 test3a test3b test4 test5

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

debug: $(SRCS)
	$(CC) $(DBGFLAGS) -o $(DBG_TARGET) $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(DBG_TARGET) src/*.o

# ── Individual tests ──────────────────────────────────────────────────────────

test1:
	@echo "\n=== Test 1: No Conflicts ==="
	./$(TARGET) --accounts=tests/accounts_test1.txt --trace=tests/trace_simple.txt \
	            --deadlock=prevention --tick-ms=50 --verbose

test2:
	@echo "\n=== Test 2: Concurrent Readers ==="
	./$(TARGET) --accounts=$(ACCOUNTS) --trace=tests/trace_readers.txt \
	            --deadlock=prevention --tick-ms=50 --verbose

test3:
	@echo "\n=== Test 3a: Deadlock Prevention ==="
	./$(TARGET) --accounts=$(ACCOUNTS) --trace=tests/trace_deadlock.txt \
	            --deadlock=prevention --tick-ms=50 --verbose

test4:
	@echo "\n=== Test 4: Insufficient Funds (Abort) ==="
	./$(TARGET) --accounts=$(ACCOUNTS) --trace=tests/trace_abort.txt \
	            --deadlock=prevention --tick-ms=50 --verbose

test5:
	@echo "\n=== Test 5: Buffer Pool Saturation ==="
	./$(TARGET) --accounts=$(ACCOUNTS) --trace=tests/trace_buffer.txt \
	            --deadlock=prevention --tick-ms=50 --verbose

test: all test1 test2 test3 test4 test5

# ── ThreadSanitizer run ───────────────────────────────────────────────────────
tsan: debug
	@echo "\n=== ThreadSanitizer: trace_readers ==="
	setarch x86_64 -R ./$(DBG_TARGET) --accounts=$(ACCOUNTS) --trace=tests/trace_readers.txt --tick-ms=50
	@echo "\n=== ThreadSanitizer: trace_deadlock (prevention) ==="
	setarch x86_64 -R ./$(DBG_TARGET) --accounts=$(ACCOUNTS) --trace=tests/trace_deadlock.txt --tick-ms=50