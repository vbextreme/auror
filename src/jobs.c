#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/threads.h>
#include <notstd/ringbuffer.h>
#include <notstd/delay.h>
#include <notstd/request.h>

#include <auror/jobs.h>

__private thr_s**    JOBS;
__private rbuffer_s  TODO;
__private unsigned   JCOUNT;
__private zem_t      AWAIT;

typedef struct work{
	job_f fn;
	void* arg;
}work_s;

__private void job_work(__unused thr_s* self, __unused void* arg){
	dbg_info("start");
	deadpoll_begin(4096);
	while( 1 ){
		work_s current;
		dbg_info("wait new job");
		rbuffer_pop(&TODO, &current, 1);
		dbg_info("start new job");
		current.fn(current.arg);
		zem_pull(&AWAIT);
	}
	deadpoll_end();
}

void job_begin(unsigned count){
	const unsigned ncpu = cpu_count();
	JCOUNT = count;
	JOBS = MANY(thr_s*, JCOUNT);
	rbuffer_ctor(&TODO, count*2, sizeof(work_s));
	for( unsigned i = 0; i < JCOUNT; ++i ){
		JOBS[i] = thr_new(job_work, NULL, 0, (i%ncpu)+1, 0);
	}
	zem_ctor(&AWAIT);
}

void job_new(job_f j, void* arg, int waitable){
	if( waitable ) zem_push(&AWAIT, 1);
	work_s work = {j, arg};
	rbuffer_push(&TODO, &work, 1);
}

void job_wait(void){
	zem_wait(&AWAIT);
}

void job_end(void){
	job_wait();
	for( unsigned i = 0; i < JCOUNT; ++i ){
		mem_free(JOBS[i]);
	}
	mem_free(JOBS);
}




