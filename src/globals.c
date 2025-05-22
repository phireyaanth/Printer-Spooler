#include <stdio.h>       // ✅ Fixes FILE issue

#include "globals.h"

struct printer printers[MAX_PRINTERS];
int num_printers = 0;

struct job jobs[MAX_JOBS];
int num_jobs = 0;

int next_job_id = 0;
