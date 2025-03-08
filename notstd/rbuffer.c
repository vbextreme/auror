#define RINGBUFFER_IMPLEMENTATION
#include <notstd/ringbuffer.h>
#include <notstd/futex.h>

rbuffer_s* rbuffer_ctor(rbuffer_s* rb, unsigned count, unsigned sof){
	if( !IS_POW_TWO(count) ) count = ROUND_UP_POW_TWO32(count);
	rb->buffer  = mem_alloc(sof, count, NULL);
	rb->veritas = MANY(__atomic char, count);
	rb->size    = count;
	rb->sof     = sof;
	STORE_Q(&rb->r, 0);
	STORE_Q(&rb->w, 0);
	return rb;
}

void rbuffer_dtor(void* mem){
	rbuffer_s* rb = mem;
	mem_free(rb->buffer);
	mem_free((void*)rb->veritas);
}

int rbuffer_empty(rbuffer_s* rb){
	unsigned w = LOAD_A(&rb->w);
	return (LOAD_A(&rb->r) - w) ? -1 : (int)w;
}

int rbuffer_full(rbuffer_s* rb){
	unsigned r = LOAD_A(&rb->r);
	return (LOAD_A(&rb->w) - r) >= (rb->size-1) ? (int)r : -1;
}

int rbuffer_pop(rbuffer_s* rb, void* out, int wait){
	int w;
	while( (w=rbuffer_empty(rb)) != -1 ){
		if( !wait ) return 0;
		futex_wait_private(&rb->w, w);
	}
	
	unsigned readable   = FADD_A(&rb->r, 1);
	unsigned normalized = FAST_MOD_POW_TWO(readable, rb->size);
	while( !CAS_A(&rb->veritas[normalized], (char)1, 0) ){
		cpu_relax();
	}
	memcpy(out, mem_addressing(rb->buffer, normalized), rb->sof);

	if( wait && LOAD_Q(&rb->w) - readable >= rb->size-1 ){
		futex_wake_private(&rb->r);
	}

	return 1;
}

int rbuffer_push(rbuffer_s* rb, void* in, int wait){
	int r;
	while( (r=rbuffer_full(rb)) != -1 ){
		if( !wait ) return 0;
		futex_wait_private(&rb->r, r);
	}
	
	unsigned writable   = FADD_B(&rb->w, 1);
	unsigned normalized = FAST_MOD_POW_TWO(writable, rb->size);
	memcpy(mem_addressing(rb->buffer, normalized), in, rb->sof);
	STORE_B(&rb->veritas[normalized], 1);
	
	if( wait && LOAD_Q(&rb->r) == writable ){
		while( futex_wake_private(&rb->w) != 0 );
	}
	
	return 1;
}












