#pragma once

#include <stdio.h>
#include <time.h>

void dispatch_jobs(void);
void reap_finished_jobs(void);
void delete_expired_jobs_if_needed(void);

char *format_time(time_t t, char *buf, size_t buf_size);

// ✅ Fix: Forward declare struct job here
struct job;

void print_job_debug(struct job *job, const char *printer_name);
