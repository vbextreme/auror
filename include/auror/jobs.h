#ifndef __PP_JOBS_H__
#define __PP_JOBS_H__

#include <notstd/core.h>
#include <notstd/threads.h>

typedef void(*job_f)(void* arg);

void job_begin(unsigned count);
void job_new(job_f j, void* arg, int waitable);
void job_wait(void);
void job_end(void);


#endif
