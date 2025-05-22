#pragma once
#include "presi.h"
#include <time.h>
#include <unistd.h>

struct file_type;
typedef struct file_type FILE_TYPE;

struct printer {
    char *name;
    FILE_TYPE *type;
    PRINTER_STATUS status;
    pid_t current_pid;
};

struct job {
    int id;
    char *file;
    FILE_TYPE *type;
    JOB_STATUS status;
    char **pipeline;
    pid_t pgid;
    unsigned int eligible;
    time_t status_changed_at;
};

extern struct printer printers[MAX_PRINTERS];
extern int num_printers;

extern struct job jobs[MAX_JOBS];
extern int num_jobs;

extern int next_job_id;
