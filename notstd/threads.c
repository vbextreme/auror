#define _GNU_SOURCE
#include <sched.h>

#include <stdatomic.h>
#include <pthread.h>
#include <sys/eventfd.h>

#include <notstd/threads.h>
#include <notstd/futex.h>

/*************/
/*** mutex ***/
/*************/

mutex_t* mutex_ctor(mutex_t* mtx){
	STORE_L(mtx, 0);
	return mtx;
}

int mutex_lock(mutex_t* mtx){
	if( !CAS_A(mtx, 0, 1) ){
		do{
			CAS_B(mtx, 1, 2);
			futex_wait_private(mtx, 2);
		}while( !CAS_WL(mtx, 0, 2) );
	}
	return 1;
}

int mutex_unlock(mutex_t* mtx){
	if( FSUB_B(mtx, 1) != 1)  {
		STORE_B(mtx, 0);
		futex_wake_private(mtx);
	}
	return 1;
}

int mutex_trylock(mutex_t* mtx){
	return CAS_A(mtx, 0, 1);
}

void mutex_cleanup(void* pmtx){
	mutex_unlock(*(void**)pmtx);
}

/*****************/
/*** semaphore ***/
/*****************/

sem_t* sem_ctor(sem_t* sem, int val){
	STORE_L(sem, val);
	return sem;
}

void sem_npost(sem_t* sem, unsigned n){
	if( FADD_B(sem, n) == 0 ){
		futex_wake_private(sem);
	}
}

int sem_wait(sem_t* sem){
	while( CAS_WB(sem, 0, 0) ){
		futex_wait_private(sem, 0);
	}
	FSUB_A(sem, 1);
	return 1;
}

int sem_trywait(sem_t* sem){
	if( CAS_B(sem, 0, 0) ){
		return 0;
	}
	FSUB_A(sem, 1);
	return 1;
}

/***************************/
/*** zero wait semaphore ***/
/***************************/

zem_t* zem_ctor(zem_t* zem){
	STORE_L(zem, 0);
	return zem;
}

void zem_push(zem_t* zem, int nval){
	FADD_A(zem, nval);
}

void zem_wait(zem_t* zem){
	while( !CAS_B(zem, 0, 0) ){
		futex_wait_private(zem, *zem);
	}
}

void zem_npull(zem_t* zem, int nval){
	iassert( nval <= *zem );
	if( FSUB_B(zem, nval) == nval ){
		futex_wake_private(zem);
	}
}

/*****************/
/***   event   ***/
/*****************/
/*
event_t* event_ctor(event_t* e){
	*e = 0;
	return e;
}

void event_raise(event_t* ev){
	if( fetch_add(ev, 1) == 0 ){
	    futex((int*)ev, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, NULL, 0);
	}
}

void event_reset(event_t* ev){
	*ev = 0;
}

int event_wait(event_t* ev){
	while( compare_and_swap(ev, 0, 0) ){
		futex((int*)ev, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, NULL, NULL, 0);
	}
	return 1;
}
*/

/*************************/
/* multi read one writer */
/*************************/

#define LOCK_OPEN    1
#define LOCK_WLOCKED 0

void mrw_ctor(mrw_t* rw){
	STORE_L(rw, LOCK_OPEN);
}

int mrw_unlock(mrw_t* rw){
	int current, wanted;
	do {
		current = LOAD_L(rw);
		if( current == LOCK_OPEN ) return 1;
		wanted = current == LOCK_WLOCKED ? LOCK_OPEN : current - 1;
	}while( !CAS_PB(rw, &current, wanted) );
	futex_wake_private(rw);
	return 1;
}

int mrw_read(mrw_t* rw){
	int current;
	while( (current = LOAD_A(rw)) == LOCK_WLOCKED || !CAS_PA(rw, &current, current + 1) ){
		while( futex_wait_private(rw, current) != 0 ){
			cpu_relax();
			if( LOAD_A(rw) >= LOCK_OPEN) break;
		}
	}
	return 1;
}

int mrw_write(mrw_t* rw){
	while( !CAS_A(rw, LOCK_OPEN, LOCK_WLOCKED) ){
		while( futex_wait_private(rw, LOCK_OPEN) != 0 ){
			cpu_relax();
			if( LOAD_A(rw) == LOCK_OPEN ) break;
		}
		if( LOAD_A(rw) != LOCK_OPEN ){
			futex_wake_private(rw);
		}
	}
	return 1;
}

/**************/
/*** thread ***/
/**************/

unsigned cpu_count(void){
	return sysconf(_SC_NPROCESSORS_ONLN);
}

__private cpu_set_t* _setcpu(unsigned mcpu){
	static cpu_set_t cpu;
	CPU_ZERO(&cpu);
	if (mcpu == 0 ) return &cpu;
	unsigned s;
	while ( (s = mcpu % 10) ){
		CPU_SET(s - 1,&cpu);
		mcpu /= 10;
	}
	return &cpu;
}

void thr_stop(thr_s* thr){
	if( thr->state != THR_STATE_STOP ){
		if( pthread_cancel(thr->id) ) die("pthread cancel");
		thr->state = THR_STATE_STOP;
	}
}

__private void _dtor(void* cthr){
	thr_s* t = cthr;
	thr_stop(t);
	pthread_attr_destroy(&t->attr);
}

__private void* pthr_wrap(void* ctx){
	thr_s* t = ctx;
	t->state = THR_STATE_RUN;
	t->fn(t, t->arg);
	return t->ret;
}
void thr_cpu_set(thr_s* thr, unsigned cpu){
	if( cpu > 0 ){
		cpu_set_t* ncpu = _setcpu(cpu);
		if( pthread_attr_setaffinity_np(&thr->attr, CPU_SETSIZE, ncpu) ) die("pthread set affinity");
	}
}

thr_s* thr_new(thr_f fn, void* arg, unsigned stackSize, unsigned oncpu, int detach){
	thr_s* thr = NEW(thr_s, _dtor);
	thr->state = THR_STATE_STOP;
	thr->fn    = fn;
	thr->arg   = arg;
	thr->ret   = NULL;

	pthread_attr_init(&thr->attr);
	if ( stackSize > 0 && pthread_attr_setstacksize(&thr->attr, stackSize) ) die("pthread stack size");
	thr_cpu_set(thr, oncpu);
	if( pthread_attr_setdetachstate(&thr->attr, detach ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE) ) die("pthread detach state");
	if( pthread_create(&thr->id, &thr->attr, pthr_wrap, thr) ) die("pthread create");
	return thr;
}

void thr_wait(thr_s* thr){
	pthread_join(thr->id, NULL);
}

void thr_yield(void){
	sched_yield();
}


