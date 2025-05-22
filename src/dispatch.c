#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#include "dispatch.h"
#include "globals.h"
#include "presi.h"
#include "conversions.h"

char *format_time(time_t t, char *buf, size_t buf_size) {
    struct tm *tm_info = localtime(&t);
    strftime(buf, buf_size, "%d %b %H:%M:%S", tm_info);
    return buf;
}

void print_job_debug(struct job *job, const char *printer_name) {
    time_t now = time(NULL);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%d Apr %H:%M:%S", localtime(&now));

    const char *status_names[] = {
        "created", "running", "paused", "aborted", "finished", "deleted"
    };

    const char *status_str = job->status >= 0 && job->status <= 5
                             ? status_names[job->status]
                             : "unknown";

    fprintf(stderr,
        "JOB[%d]: type=%s, creation(%s), status(%s)=%s, eligible=%08x, file=%s, pgid=%d, printer=%s\n",
        job->id,
        job->type ? job->type->name : "(null)",
        time_buf,
        time_buf,
        status_str,
        job->eligible,
        job->file ? job->file : "(null)",
        job->pgid,
        printer_name ? printer_name : "(none)"
    );
}

int job_for_pgid(pid_t pgid) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].pgid == pgid)
            return i;
    }
    return -1;
}


void dispatch_jobs(void) {
    int dispatched;
    do {
        dispatched = 0;

        for (int j = 0; j < num_jobs; j++) {
            if (jobs[j].status != JOB_CREATED)
                continue;

            for (int p = 0; p < num_printers; p++) {
                if (printers[p].status != PRINTER_IDLE)
                    continue;

                if (!(jobs[j].eligible & (1U << p)))
                    continue;

                CONVERSION **path = find_conversion_path(jobs[j].type->name, printers[p].type->name);
                if (!path) continue;

                int printer_fd = presi_connect_to_printer(printers[p].name, printers[p].type->name, PRINTER_NORMAL);
                if (printer_fd < 0) {
                    free(path);
                    continue;
                }

                pid_t master = fork();
                if (master < 0) {
                    close(printer_fd);
                    free(path);
                    continue;
                }

                if (master == 0) {
                    setpgid(0, 0);  // Master creates its own process group
                    pid_t pgid = getpid();  // This will be used for all children

                    int in_fd = open(jobs[j].file, O_RDONLY);
                    if (in_fd < 0) exit(1);

                    int path_len = 0;
                    while (path[path_len]) path_len++;

                    if (path_len == 0) {
                        dup2(in_fd, STDIN_FILENO);
                        dup2(printer_fd, STDOUT_FILENO);
                        close(in_fd); close(printer_fd);
                        //fprintf(stderr, "[DEBUG] cat printing process (no conversion), pid=%d\n", getpid());
                        execlp("cat", "cat", NULL);
                        exit(1);
                    }

                    int pipes[path_len - 1][2];
                    for (int i = 0; i < path_len - 1; i++) {
                        if (pipe(pipes[i]) < 0) exit(1);
                    }

                    pid_t pids[path_len];
                    for (int i = 0; i < path_len; i++) {
                        pids[i] = fork();
                        if (pids[i] < 0) exit(1);

                        if (pids[i] == 0) {
                            setpgid(0, pgid);  // ✅ join master's process group

                            if (i == 0)
                                dup2(in_fd, STDIN_FILENO);
                            else
                                dup2(pipes[i - 1][0], STDIN_FILENO);

                            if (i == path_len - 1)
                                dup2(printer_fd, STDOUT_FILENO);
                            else
                                dup2(pipes[i][1], STDOUT_FILENO);

                            for (int k = 0; k < path_len - 1; k++) {
                                close(pipes[k][0]);
                                close(pipes[k][1]);
                            }

                            close(in_fd);
                            close(printer_fd);
                            execvp(path[i]->cmd_and_args[0], path[i]->cmd_and_args);
                            exit(1);
                        }
                    }

                    for (int i = 0; i < path_len - 1; i++) {
                        close(pipes[i][0]);
                        close(pipes[i][1]);
                    }

                    close(in_fd);
                    close(printer_fd);

                    int status, exit_status = 0;
                    for (int i = 0; i < path_len; i++) {
                        waitpid(pids[i], &status, 0);
                        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                            exit_status = 1;
                    }

                    exit(exit_status);
                }

                close(printer_fd);
                setpgid(master, master); // Parent sets pgid for master too
                jobs[j].pgid = master;   // ✅ Track pgid in parent

                jobs[j].status = JOB_RUNNING;
                sf_job_status(jobs[j].id, JOB_RUNNING);

                printers[p].status = PRINTER_BUSY;
                printers[p].current_pid = master;
                sf_printer_status(printers[p].name, PRINTER_BUSY);

                int path_len = 0;
                while (path[path_len]) path_len++;

                char *commands[path_len + 1];
                for (int i = 0; i < path_len; i++)
                    commands[i] = path[i]->cmd_and_args[0];
                commands[path_len] = NULL;

                sf_job_started(jobs[j].id, printers[p].name, master, commands);
                print_job_debug(&jobs[j], printers[p].name);
                sf_cmd_ok();
                free(path);
                dispatched = 1;
                break;
            }
            if (dispatched) break;
        }
    } while (dispatched);
}


void reap_finished_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        printf("[DEBUG] waitpid caught pid=%d, status=0x%x\n", pid, status);

        for (int j = 0; j < num_jobs; j++) {
            if (jobs[j].pgid == pid) {
                printf("[DEBUG] Matched job[%d] with pgid=%d\n", j, pid);

                if (WIFEXITED(status)) {
                    printf("[DEBUG] WIFEXITED with status=%d\n", WEXITSTATUS(status));
                    if (WEXITSTATUS(status) == 0) {
                        jobs[j].status = JOB_FINISHED;
                        sf_job_status(jobs[j].id, JOB_FINISHED);
                        sf_job_finished(jobs[j].id, status);
                    } else {
                        jobs[j].status = JOB_ABORTED;
                        sf_job_status(jobs[j].id, JOB_ABORTED);
                        sf_job_aborted(jobs[j].id, status);
                    }
                    jobs[j].status_changed_at = time(NULL);

                    for (int p = 0; p < num_printers; p++) {
                        if (printers[p].current_pid == pid) {
                            printf("[DEBUG] Releasing printer[%d] (%s) from job[%d]\n", p, printers[p].name, j);
                            printers[p].status = PRINTER_IDLE;
                            printers[p].current_pid = 0;
                            sf_printer_status(printers[p].name, PRINTER_IDLE);
                            break;
                        }
                    }

                } else if (WIFSIGNALED(status)) {
                    printf("[DEBUG] WIFSIGNALED for job[%d]\n", j);
                    jobs[j].status = JOB_ABORTED;
                    jobs[j].status_changed_at = time(NULL);
                    sf_job_status(jobs[j].id, JOB_ABORTED);
                    sf_job_aborted(jobs[j].id, status);

                    for (int p = 0; p < num_printers; p++) {
                        if (printers[p].current_pid == pid) {
                            printf("[DEBUG] Resetting printer[%d] (%s) after abort\n", p, printers[p].name);
                            printers[p].status = PRINTER_IDLE;
                            printers[p].current_pid = 0;
                            sf_printer_status(printers[p].name, PRINTER_IDLE);
                            break;
                        }
                    }

                } else if (WIFSTOPPED(status)) {
                    //printf("[DEBUG] WIFSTOPPED: job[%d] is now paused\n", j);
                    jobs[j].status = JOB_PAUSED;
                    jobs[j].status_changed_at = time(NULL);
                    //sf_job_status(jobs[j].id, JOB_PAUSED);

                } else if (WIFCONTINUED(status)) {
                    //printf("[DEBUG] WIFCONTINUED: job[%d] is now running again\n", j);
                    jobs[j].status = JOB_RUNNING;
                    jobs[j].status_changed_at = time(NULL);
                    //sf_job_status(jobs[j].id, JOB_RUNNING);
                }

                break; // no need to check more jobs
            }
        }
    }
}



void delete_expired_jobs_if_needed(void) {
    time_t now = time(NULL);
    for (int i = 0; i < num_jobs;) {
        if ((jobs[i].status == JOB_FINISHED || jobs[i].status == JOB_ABORTED) &&
            difftime(now, jobs[i].status_changed_at) >= 10.0) {

            sf_job_deleted(jobs[i].id);
            free(jobs[i].file);

            for (int j = i + 1; j < num_jobs; j++)
                jobs[j - 1] = jobs[j];

            num_jobs--;
        } else {
            i++;
        }
    }
}
