#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lock_mgr.h"

/* Minimal getopt-style long-option parser */

static const char *get_opt_val(int argc, char **argv, const char *key)
{
    size_t klen = strlen(key);
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--", 2) == 0 && strncmp(argv[i] + 2, key, klen) == 0) {
            char *eq = strchr(argv[i], '=');
            if (eq) return eq + 1;
        }
    }
    return NULL;
}

static int has_flag(int argc, char **argv, const char *key)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], key) == 0) return 1;
    }
    return 0;
}

void parse_args(int argc, char **argv,
                const char **accounts_file,
                const char **trace_file,
                DeadlockStrategy *strategy,
                int *tick_ms,
                int *verbose_out)
{
    *accounts_file = get_opt_val(argc, argv, "accounts");
    *trace_file    = get_opt_val(argc, argv, "trace");
    *verbose_out   = has_flag(argc, argv, "--verbose");

    const char *ds = get_opt_val(argc, argv, "deadlock");
    if (!ds || strcmp(ds, "prevention") == 0) {
        *strategy = DEADLOCK_PREVENTION;
    } else if (strcmp(ds, "detection") == 0) {
        fprintf(stderr, "Error: --deadlock=detection is no longer supported; "
                        "deadlock detection code has been removed.\n"
                        "Use --deadlock=prevention (or omit the flag).\n");
        exit(EXIT_FAILURE);
    } else {
        fprintf(stderr, "Error: unknown deadlock strategy '%s'. "
                        "Supported: prevention\n", ds);
        exit(EXIT_FAILURE);
    }

    const char *tm = get_opt_val(argc, argv, "tick-ms");
    *tick_ms = tm ? atoi(tm) : 100;

    if (!*accounts_file || !*trace_file) {
        fprintf(stderr,
                "Usage: bankdb --accounts=FILE --trace=FILE\n"
                "              [--tick-ms=N] [--verbose]\n");
        exit(EXIT_FAILURE);
    }
}