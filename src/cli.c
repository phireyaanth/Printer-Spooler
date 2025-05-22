#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#include "sf_readline.h"
#include "vaildargs.h"
#include "presi.h"
#include "globals.h"
#include "dispatch.h"

static volatile sig_atomic_t got_sigchld = 0;

void sigchld_handler(int sig) {
    got_sigchld = 1;
    printf("[DEBUG] SIGCHLD received\n");

}

void signal_hook(void) {
    if (got_sigchld) {
        got_sigchld = 0;
        reap_finished_jobs();
        dispatch_jobs();
    }
}

int run_cli(FILE *in, FILE *out) {
    static int initialized = 0;
    if (!initialized) {
        struct sigaction sa;
        sa.sa_handler = sigchld_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGCHLD, &sa, NULL);
        sf_set_readline_signal_hook(signal_hook);
        initialized = 1;
    }

    char prompt_buffer[1024];
    while (1) {
        reap_finished_jobs();
        char *line = NULL;

        if (in == stdin) {
            line = sf_readline("presi> ");
        } else {
            if (fgets(prompt_buffer, sizeof(prompt_buffer), in) == NULL)
                return 0;
            prompt_buffer[strcspn(prompt_buffer, "\n")] = '\0';
            line = strdup(prompt_buffer);
        }

        if (line == NULL) break;
        if (strlen(line) == 0 || strspn(line, " \t\r\n") == strlen(line)) {
            free(line);
            continue;
        }

        if (strncmp(line, "quit", 4) == 0) {
            sf_cmd_ok();
            free(line);
            return -1;
        }

        handle_user_command(line, out);

        if (in != stdin) {
            signal_hook();          // ✅ force signal handling
            usleep(1000);           // ✅ 1ms buffer to let SIGCHLD land
        }

        delete_expired_jobs_if_needed();
        free(line);
    }

    return (in == stdin) ? -1 : 0;
}
