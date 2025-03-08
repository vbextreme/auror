#ifndef __NOTSTD_CORE_THREADS_H__
#define __NOTSTD_CORE_THREADS_H__

#include <notstd/core.h>
#include <notstd/list.h>

/**************/
/*** ATOMIC ***/
/**************/

#include <stdatomic.h>

/*
 * B sync before, all memory before this call is visible after this atomic call, release
 * A sync after, all memory is visible after this call, acquire
 * Q sync all
 * L order can mixed before and after, only var is atomic
 * W weak for used in loop
*/

// assign/compare operations

// a = v
#define STORE_Q(A, V) atomic_store_explicit(A, V, memory_order_seq_cst)
#define STORE_B(A, V) atomic_store_explicit(A, V, memory_order_release)
#define STORE_L(A, V) atomic_store_explicit(A, V, memory_order_relaxed)
// ret = a
#define LOAD_Q(A) atomic_load_explicit(A, memory_order_seq_cst)
#define LOAD_B(A) atomic_load_explicit(A, memory_order_release)
#define LOAD_A(A) atomic_load_explicit(A, memory_order_acquire)
#define LOAD_L(A) atomic_load_explicit(A, memory_order_relaxed)
// ret = a; a = v
#define EXCHANGE_Q(A, V) atomic_exchange_explicit(A, V, memory_order_seq_cst)
#define EXCHANGE_B(A, V) atomic_exchange_explicit(A, V, memory_order_release)
#define EXCHANGE_A(A, V) atomic_exchange_explicit(A, V, memory_order_acquire)
#define EXCHANGE_L(A, V) atomic_exchange_explicit(A, V, memory_order_relaxed)
//compare and swap
// P version required T as pointer
// a == t ? a = v && ret = 1 : ret = 0
#define CAS_PQ(A, T, V) atomic_compare_exchange_strong_explicit(A, T, V, memory_order_seq_cst, memory_order_relaxed)
#define CAS_PB(A, T, V) atomic_compare_exchange_strong_explicit(A, T, V, memory_order_release, memory_order_relaxed)
#define CAS_PA(A, T, V) atomic_compare_exchange_strong_explicit(A, T, V, memory_order_acquire, memory_order_relaxed)
#define CAS_PL(A, T, V) atomic_compare_exchange_strong_explicit(A, T, V, memory_order_relaxed, memory_order_relaxed)
#define CAS_PWQ(A, T, V) atomic_compare_exchange_weak_explicit(A, T, V, memory_order_seq_cst, memory_order_relaxed)
#define CAS_PWB(A, T, V) atomic_compare_exchange_weak_explicit(A, T, V, memory_order_release, memory_order_relaxed)
#define CAS_PWA(A, T, V) atomic_compare_exchange_weak_explicit(A, T, V, memory_order_acquire, memory_order_relaxed)
#define CAS_PWL(A, T, V) atomic_compare_exchange_weak_explicit(A, T, V, memory_order_relaxed, memory_order_relaxed)
#define CAS_Q(A, T, V) ({ typeofbase(T) _p_ = T; CAS_PQ(A, &_p_, V); })
#define CAS_B(A, T, V) ({ typeofbase(T) _p_ = T; CAS_PB(A, &_p_, V); })
#define CAS_A(A, T, V) ({ typeofbase(T) _p_ = T; CAS_PA(A, &_p_, V); })
#define CAS_L(A, T, V) ({ typeofbase(T) _p_ = T; CAS_PL(A, &_p_, V); })
#define CAS_WQ(A, T, V) ({ typeofbase(T) _p_ = T; CAS_PWQ(A, &_p_, V); })
#define CAS_WB(A, T, V) ({ typeofbase(T) _p_ = T; CAS_PWB(A, &_p_, V); })
#define CAS_WA(A, T, V) ({ typeofbase(T) _p_ = T; CAS_PWA(A, &_p_, V); })
#define CAS_WL(A, T, V) ({ typeofbase(T) _p_ = T; CAS_PWL(A, &_p_, V); })
// ret = a; a = true;
#define BOOLSET_Q(A) atomic_flag_test_and_set_explicit(A, memory_order_seq_cst)
#define BOOLSET_B(A) atomic_flag_test_and_set_explicit(A, memory_order_release)
#define BOOLSET_A(A) atomic_flag_test_and_set_explicit(A, memory_order_acquire)
#define BOOLSET_L(A) atomic_flag_test_and_set_explicit(A, memory_order_relaxed)
// a = false;
#define BOOLCLR_Q(A) atomic_flag_clear_explicit(A, memory_order_seq_cst)
#define BOOLCLR_B(A) atomic_flag_clear_explicit(A, memory_order_release)
#define BOOLCLR_A(A) atomic_flag_clear_explicit(A, memory_order_acquire)
#define BOOLCLR_L(A) atomic_flag_clear_explicit(A, memory_order_relaxed)


// fetch and operations
// ret = a; a += v
#define FADD_Q(A, V) atomic_fetch_add_explicit(A, V, memory_order_seq_cst)
#define FADD_B(A, V) atomic_fetch_add_explicit(A, V, memory_order_release)
#define FADD_A(A, V) atomic_fetch_add_explicit(A, V, memory_order_acquire)
#define FADD_L(A, V) atomic_fetch_add_explicit(A, V, memory_order_relaxed)
// ret = a; a -= v
#define FSUB_Q(A, V) atomic_fetch_sub_explicit(A, V, memory_order_seq_cst)
#define FSUB_B(A, V) atomic_fetch_sub_explicit(A, V, memory_order_release)
#define FSUB_A(A, V) atomic_fetch_sub_explicit(A, V, memory_order_acquire)
#define FSUB_L(A, V) atomic_fetch_sub_explicit(A, V, memory_order_relaxed)
// ret = a; a |= v
#define FBOR_Q(A, V) atomic_fetch_or_explicit(A, V, memory_order_seq_cst)
#define FBOR_B(A, V) atomic_fetch_or_explicit(A, V, memory_order_release)
#define FBOR_A(A, V) atomic_fetch_or_explicit(A, V, memory_order_acquire)
#define FBOR_L(A, V) atomic_fetch_or_explicit(A, V, memory_order_relaxed)
// ret = a; a ^= v
#define FXOR_Q(A, V) atomic_fetch_xor_explicit(A, V, memory_order_seq_cst)
#define FXOR_B(A, V) atomic_fetch_xor_explicit(A, V, memory_order_release)
#define FXOR_A(A, V) atomic_fetch_xor_explicit(A, V, memory_order_acquire)
#define FXOR_L(A, V) atomic_fetch_xor_explicit(A, V, memory_order_relaxed)
// ret = a; a &= v
#define FAND_Q(A, V) atomic_fetch_and_explicit(A, V, memory_order_seq_cst)
#define FAND_B(A, V) atomic_fetch_and_explicit(A, V, memory_order_release)
#define FAND_A(A, V) atomic_fetch_and_explicit(A, V, memory_order_acquire)
#define FAND_L(A, V) atomic_fetch_and_explicit(A, V, memory_order_relaxed)

// generyc sync
#define THREAD_Q(A, V) atomic_thread_fence(A, V, memory_order_seq_cst)
#define THREAD_B(A, V) atomic_thread_fence(A, V, memory_order_release)
#define THREAD_A(A, V) atomic_thread_fence(A, V, memory_order_acquire)
#define THREAD_L(A, V) atomic_thread_fence(A, V, memory_order_relaxed)
#define SIGNAL_Q(A, V) atomic_signal_fence(A, V, memory_order_seq_cst)
#define SIGNAL_B(A, V) atomic_signal_fence(A, V, memory_order_release)
#define SIGNAL_A(A, V) atomic_signal_fence(A, V, memory_order_acquire)
#define SIGNAL_L(A, V) atomic_signal_fence(A, V, memory_order_relaxed)








/*************/
/*** mutex ***/
/*************/

typedef __atomic int mutex_t;

mutex_t* mutex_ctor(mutex_t* mtx);
int mutex_lock(mutex_t* mtx);
int mutex_unlock(mutex_t* mtx);
int mutex_trylock(mutex_t* mtx);
void mutex_cleanup(void* pmtx);

#define __mlock __cleanup(mutex_cleanup)
#define mlock(MTX) for( int _guard_ = mutex_lock(MTX); _guard_; _guard_ = 0, mutex_unlock(MTX) )

/*****************/
/*** semaphore ***/
/*****************/

typedef __atomic int sem_t;

sem_t* sem_ctor(sem_t* sem, int val);
void sem_npost(sem_t* sem, unsigned n);
int sem_wait(sem_t* sem);
int sem_trywait(sem_t* sem);
#define sem_post(S) sem_npost((S), 1)

/***************************/
/*** zero wait semaphore ***/
/***************************/

typedef __atomic int zem_t;
zem_t* zem_ctor(zem_t* zem);
void zem_push(zem_t* zem, int nval);
void zem_wait(zem_t* zem);
void zem_npull(zem_t* zem, int nval);
#define zem_pull(Z) zem_npull((Z), 1)

/*************/
/*** event ***/
/*************/

/*
typedef __atomic int event_t;

event_t* event_ctor(event_t* e);
void event_raise(event_t* ev);
void event_reset(event_t* ev);
int event_wait(event_t* ev);
*/

/*************************/
/* multi read one writer */
/*************************/

typedef __atomic int mrw_t;

void mrw_ctor(mrw_t* rw);
int mrw_unlock(mrw_t* rw);
int mrw_read(mrw_t* rw);
int mrw_write(mrw_t* rw);

#define acquire_read(ADDR) for(int __acquire__ = mem_lock_read(ADDR); __acquire__; __acquire__ = 0, mem_unlock(ADDR) )
#define acquire_write(ADDR) for(int __acquire__ = mem_lock_write(ADDR); __acquire__; __acquire__ = 0, mem_unlock(ADDR) )


/**************/
/*** thread ***/
/**************/

typedef enum { THR_STATE_STOP, THR_STATE_RUN } thr_e;

typedef struct thr thr_s;

typedef void(*thr_f)(thr_s* self, void* ctx);

struct thr{
	pthread_t id;
    pthread_attr_t attr;
	thr_e state;
	thr_f fn;
	void* arg;
	void* ret;
};


#define START(FN, ARG) thr_new(FN, ARG, 0, 0, 0)
unsigned cpu_count(void);
void thr_stop(thr_s* thr);
void thr_cpu_set(thr_s* thr, unsigned cpu);
thr_s* thr_new(thr_f fn, void* arg, unsigned stackSize, unsigned oncpu, int detach);
void thr_wait(thr_s* thr);
void thr_yield(void);


#endif
