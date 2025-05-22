#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "globals.h"
#include "vaildargs.h"
#include "dispatch.h"
#include "conversions.h"
#include "presi.h"

#define MAX_ARGS 32

volatile sig_atomic_t got_sigchld = 0;



void handle_help(FILE *out) {
    fprintf(out, "Commands are: help quit type printer conversion printers jobs print cancel disable enable pause resume\n");
    sf_cmd_ok();
}

void handle_type(char *line) {
    char *type_name = line + 5;
    while (isspace(*type_name)) type_name++;
    if (strlen(type_name) == 0) {
        sf_cmd_error("Missing type name.");
    } else {
        FILE_TYPE *t = define_type(type_name);
        if (t != NULL) sf_cmd_ok();
        else sf_cmd_error("Failed to define type.");
    }
}

void handle_printers(FILE *out) {
    for (int i = 0; i < num_printers; i++) {
        PRINTER *p = &printers[i];
        printf("PRINTER: id=%d, name=%s, type=%s, status=%s\n",
               i,
               p->name ? p->name : "(null)",
               p->type && p->type->name ? p->type->name : "(null)",
               p->status == PRINTER_IDLE     ? "idle" :
               p->status == PRINTER_BUSY     ? "busy" :
               p->status == PRINTER_DISABLED ? "disabled" : "unknown");
    }

    sf_cmd_ok();
}



void handle_printer(char *line) {
    char *name = strtok(line + 8, " \t");
    char *type_name = strtok(NULL, " \t");

    if (!name || !type_name || strtok(NULL, " \t")) {
        sf_cmd_error("printer");
        return;
    }

    FILE_TYPE *ftype = find_type(type_name);
    if (!ftype) {
        sf_cmd_error("printer");
        return;
    }

    if (num_printers >= MAX_PRINTERS) {
        sf_cmd_error("printer");
        return;
    }

    PRINTER *p = &printers[num_printers++];
    p->name = strdup(name);
    p->type = ftype;
    p->status = PRINTER_DISABLED;
    p->current_pid = 0;

    sf_printer_defined(p->name, p->type->name);

    // ✅ This line prints immediately after creation (like your professor's output)
    printf("PRINTER: id=%d, name=%s, type=%s, status=disabled\n",
           num_printers - 1, p->name, p->type->name);
    
    sf_cmd_ok();
}


void handle_conversion(char *line) {
    char *args = line + 11;
    char *from_type = strtok(args, " \t");
    char *to_type = strtok(NULL, " \t");
    char *cmd = strtok(NULL, " \t");

    if (!from_type || !to_type || !cmd) {
        sf_cmd_error("Usage: conversion <from_type> <to_type> <cmd> [args...]");
        return;
    }

    FILE_TYPE *from = find_type(from_type);
    FILE_TYPE *to = find_type(to_type);
    if (!from || !to) {
        sf_cmd_error("One or both types not defined.");
        return;
    }

    char *cmd_and_args[MAX_ARGS];
    int i = 0;
    cmd_and_args[i++] = cmd;
    char *arg;
    while ((arg = strtok(NULL, " \t")) && i < MAX_ARGS - 1) {
        cmd_and_args[i++] = arg;
    }
    cmd_and_args[i] = NULL;

    if (define_conversion(from_type, to_type, cmd_and_args)) {
        sf_cmd_ok();
    } else {
        sf_cmd_error("Failed to define conversion.");
    }
}

void handle_enable(char *line) {
    char *printer_name = line + 7;
    while (isspace(*printer_name)) printer_name++;

    for (int i = 0; i < num_printers; i++) {
        if (strcmp(printers[i].name, printer_name) == 0) {
            if (printers[i].status == PRINTER_DISABLED) {
                printers[i].status = PRINTER_IDLE;
                sf_printer_status(printers[i].name, PRINTER_IDLE);

                fprintf(stdout, "PRINTER: id=%d, name=%s, type=%s, status=idle\n",
                        i, printers[i].name, printers[i].type->name);
                sf_cmd_ok();
                dispatch_jobs();
            } else {
                sf_cmd_error("Printer already enabled.");
            }
            return;
        }
    }

    sf_cmd_error("Printer not found.");
}

void handle_print(char *line) {
    char *args = line + 6;
    char *file = strtok(args, " \t");

    if (file == NULL) {
        sf_cmd_error("Missing file name.");
        sf_cmd_ok();
        return;
    }

    FILE_TYPE *ftype = infer_file_type(file);
    if (ftype == NULL) {
        sf_cmd_error("print");
        fprintf(stderr, "Command error: print (file type)\n");
        sf_cmd_ok();
        return;
    }

    if (num_jobs >= MAX_JOBS) {
        sf_cmd_error("Too many jobs.");
        sf_cmd_ok();
        return;
    }

    unsigned int eligibility_mask = 0;
    char *printer_name = strtok(NULL, " \t");

    if (printer_name == NULL) {
    eligibility_mask = 0xFFFFFFFF; // Eligible for all printers by default
}
 else {
        do {
            int found = 0;
            for (int i = 0; i < num_printers; i++) {
                if (strcmp(printers[i].name, printer_name) == 0) {
                    CONVERSION **path = find_conversion_path(ftype->name, printers[i].type->name);
                    if (path) {
                        eligibility_mask |= (1U << i);
                        free(path);
                    }
                    found = 1;
                    break;
                }
            }
            if (!found) {
                sf_cmd_error("Invalid printer name.");
                sf_cmd_ok();
                return;
            }
        } while ((printer_name = strtok(NULL, " \t")) != NULL);
    }

    int job_id = next_job_id++;
    jobs[num_jobs].id = job_id;
    jobs[num_jobs].file = strdup(file);
    jobs[num_jobs].type = ftype;
    jobs[num_jobs].status = JOB_CREATED;
    jobs[num_jobs].eligible = eligibility_mask;
    jobs[num_jobs].pgid = -1;
    jobs[num_jobs].status_changed_at = time(NULL);
    num_jobs++;

    sf_job_created(job_id, file, ftype->name);

    char created_str[64], status_str[64];
    format_time(jobs[job_id].status_changed_at, created_str, sizeof(created_str));
    format_time(jobs[job_id].status_changed_at, status_str, sizeof(status_str));
    printf("JOB[%d]: type=%s, creation(%s), status(%s)=%s, eligible=%08x, file=%s\n",
           job_id,
           ftype->name,
           created_str,
           status_str,
           job_status_names[JOB_CREATED],
           eligibility_mask,
           file);

    //sf_cmd_ok();
    dispatch_jobs();
}

void handle_jobs(FILE *out) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].status != JOB_DELETED) {
            char created_str[64], status_str[64];
            format_time(jobs[i].status_changed_at, status_str, sizeof(status_str));
            format_time(jobs[i].status_changed_at, created_str, sizeof(created_str));

            fprintf(out, "JOB[%d]: type=%s, creation(%s), status(%s)=%s, eligible=%08x, file=%s\n",
                jobs[i].id,
                jobs[i].type ? jobs[i].type->name : "(null)",
                created_str,
                status_str,
                job_status_names[jobs[i].status],
                jobs[i].eligible,
                jobs[i].file ? jobs[i].file : "(null)");

            sf_job_status(jobs[i].id, jobs[i].status);
        }
    }
    sf_cmd_ok();
}

void handle_pause(char *line) {
    //printf("[INSIDE PAUSE]\n");

    char *job_id_str = line + 6;
    while (isspace(*job_id_str)) job_id_str++;

    //printf("[DEBUG] Raw job_id_str = \"%s\"\n", job_id_str);

    if (strlen(job_id_str) == 0) {
        //printf("[DEBUG] Missing job ID\n");
        sf_cmd_error("Missing job ID.");
        return;
    }

    int job_id = atoi(job_id_str);
    //printf("[DEBUG] Parsed job_id = %d\n", job_id);

    if (job_id < 0 || job_id >= num_jobs) {
        //printf("[DEBUG] Invalid job ID: %d\n", job_id);
        sf_cmd_error("Invalid job ID.");
        return;
    }

    printf("[DEBUG] Current job status = %d\n", jobs[job_id].status);

    if (jobs[job_id].status == JOB_PAUSED) {
        //printf("[DEBUG] Job already paused, returning OK\n");
        sf_cmd_ok();
        return;
    }

    if (jobs[job_id].status != JOB_RUNNING) {
        //printf("[DEBUG] Job not running, cannot pause\n");
        sf_cmd_error("pause");
        return;
    }

    // Block SIGCHLD before sending SIGSTOP to prevent race conditions.
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    got_sigchld = 0;
    //printf("[DEBUG] Sending SIGSTOP to pgid: %d\n", jobs[job_id].pgid);
    //int result = kill(jobs[job_id].pgid, SIGSTOP);
    //printf("[DEBUG] kill() returned %d\n", result);

    // Instead of blocking indefinitely with sigsuspend, use a loop with a 1ms sleep.
    // Also, call reap_finished_jobs() in each iteration to process any pending SIGCHLD.
    int wait_loops = 0;
    while (jobs[job_id].status == JOB_RUNNING && wait_loops < 1000) {
        usleep(1000);  // wait for 1ms
        reap_finished_jobs();  // process any pending SIGCHLD
        wait_loops++;
    }

    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    //printf("[DEBUG] Exited wait loop. job status = %d\n", jobs[job_id].status);

    if (jobs[job_id].status == JOB_PAUSED) {
        printf("[DEBUG] Pause succeeded, returning OK\n");
        sf_cmd_ok();
    } else {
        printf("[DEBUG] Pause failed: job status still = %d\n", jobs[job_id].status);
        sf_cmd_error("pause: job didn't pause");
    }
}




void handle_resume(char *line) {
    printf("[INSIDE RESUME HANDLING]");
    char *job_id_str = line + 7;
    while (isspace(*job_id_str)) job_id_str++;

    if (strlen(job_id_str) == 0) {
        sf_cmd_error("Missing job ID.");
        return;
    }

    int job_id = atoi(job_id_str);
    if (job_id < 0 || job_id >= num_jobs) {
        sf_cmd_error("Invalid job ID.");
        return;
    }

    printf("[DEBUG] resume requested for job_id=%d, current status=%d\n", job_id, jobs[job_id].status);

    // If not paused, silently succeed
    if (jobs[job_id].status != JOB_PAUSED) {
        sf_cmd_ok();
        return;
    }

    printf("[DEBUG] Sending SIGCONT to pgid: %d (job_id=%d)\n", jobs[job_id].pgid, job_id);
    got_sigchld = 0;
    kill(jobs[job_id].pgid, SIGCONT);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    while (!got_sigchld) {
        sigsuspend(&oldmask);
    }

    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    if (jobs[job_id].status == JOB_RUNNING) {
        sf_cmd_ok();
    } else {
        sf_cmd_error("resume: job didn't resume");
    }
}





void handle_cancel(char *line) {
    char *job_id_str = line + 7;
    while (isspace(*job_id_str)) job_id_str++;

    if (strlen(job_id_str) == 0) {
        sf_cmd_error("Missing job ID.");
        return;
    }

    int job_id = atoi(job_id_str);
    if (job_id >= 0 && job_id < num_jobs) {
        if (jobs[job_id].status != JOB_ABORTED &&
            jobs[job_id].status != JOB_FINISHED &&
            jobs[job_id].status != JOB_DELETED) {

            if (jobs[job_id].pgid > 0) {
                kill(-jobs[job_id].pgid, SIGTERM);
            }

            jobs[job_id].status = JOB_ABORTED;
            jobs[job_id].status_changed_at = time(NULL);
            sf_job_status(jobs[job_id].id, JOB_ABORTED);
            sf_job_aborted(jobs[job_id].id, 0);
            sf_cmd_ok();
        } else {
            sf_cmd_error("Job is already completed or aborted.");
        }
    } else {
        sf_cmd_error("Invalid job ID.");
    }
}

void handle_user_command(char *line, FILE *out) {
    if (strncmp(line, "help", 4) == 0) handle_help(out);
    else if (strncmp(line, "type ", 5) == 0) handle_type(line);
    else if (strncmp(line, "printer ", 8) == 0) handle_printer(line);
    else if (strncmp(line, "conversion ", 11) == 0) handle_conversion(line);
    else if (strncmp(line, "enable ", 7) == 0) handle_enable(line);
    else if (strncmp(line, "print ", 6) == 0) handle_print(line);
    else if (strcmp(line, "jobs") == 0) handle_jobs(out);
    else if (strncmp(line, "pause ", 6) == 0) handle_pause(line);
    else if (strcmp(line, "printers") == 0) handle_printers(out);
    else if (strncmp(line, "resume", 6) == 0 && isspace(line[6])) handle_resume(line);
    else if (strncmp(line, "cancel ", 7) == 0) handle_cancel(line);
    else sf_cmd_error("Unrecognized command.");
}
