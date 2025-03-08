#ifndef __NOTSTD_RING_BUFFER_H__
#define __NOTSTD_RING_BUFFER_H__

#include <notstd/core.h>
#include <notstd/threads.h>

#ifdef RINGBUFFER_IMPLEMENTATION
#include <notstd/field.h>
#endif

typedef struct rbuffer{
	__prv8 void*             buffer;
	__prv8 __atomic char*    veritas;
	__prv8 __atomic unsigned r;
	__prv8 __atomic unsigned w;
	__prv8 unsigned          size;
	__prv8 unsigned          sof;
}rbuffer_s;

rbuffer_s* rbuffer_ctor(rbuffer_s* rb, unsigned count, unsigned sof);
void rbuffer_dtor(void* mem);
 
int rbuffer_empty(rbuffer_s* rb);
int rbuffer_full(rbuffer_s* rb);
int rbuffer_pop(rbuffer_s* rb, void* out, int wait);
int rbuffer_push(rbuffer_s* rb, void* in, int wait);

#endif
