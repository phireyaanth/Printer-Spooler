#pragma once

#include <stdio.h>

/**
 * Handles a user-issued command line.
 *
 * Parses the line, identifies the command, validates arguments,
 * and dispatches to the appropriate handler function.
 *
 * @param line  The raw input line from the user.
 * @param out   Output stream for user-visible responses.
 */
void handle_user_command(char *line, FILE *out);
