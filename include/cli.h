#pragma once

#include <stdio.h>

/**
 * Starts the command-line interface for the presi print spooler.
 *
 * This function enters an interactive or batch command loop, depending on the input stream.
 * It sets up signal handlers and hooks for processing SIGCHLD and dispatching jobs.
 *
 * @param in  The input stream (stdin for interactive mode or a file for batch mode).
 * @param out The output stream (usually stdout).
 * @return -1 if quit command was received, 0 if EOF was reached in batch mode.
 */
int run_cli(FILE *in, FILE *out);
