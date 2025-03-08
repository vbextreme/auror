#undef DBG_ENABLE
//#include <liburing.h>
#include <notstd/request.h>

#define R_FLAG_BUSY      0x01
#define R_FLAG_COMPLETED 0x02

typedef struct io_uring_sqe sqe_s;

__private __thread deadpoll_s aio;

//__private unsigned io_uring_cqe_available(struct io_uring* r){
//	return *r->cq.ktail - *r->cq.khead;
//}

//__private unsigned io_uring_sqe_available(struct io_uring* r){
//	return *r->sq.ktail - *r->sq.khead;
//}

deadpoll_s* deadpoll_ctor(deadpoll_s* dp, unsigned maxrequest){
	memset(dp, 0, sizeof(deadpoll_s));
	if( io_uring_queue_init(maxrequest, &dp->ring, IORING_SETUP_SQPOLL) ) die("liburing init queue: %m");
	dp->autoqueuecqe = *dp->ring.cq.kring_entries - 4;
	dp->autoqueuesqe = *dp->ring.sq.kring_entries - 4;
	if( !(dp->ring.features & IORING_FEAT_RW_CUR_POS) ) die("unsupported FRW_CUR_POS");
	maxrequest = *dp->ring.cq.kring_entries * 2;
	dp->mod = IS_POW_TWO(maxrequest) ? maxrequest : ROUND_UP_POW_TWO32(maxrequest);
	dbg_info("queuesize: %u sqe:%u cqe:%u", maxrequest, *dp->ring.sq.kring_entries, *dp->ring.cq.kring_entries);
	dp->dring = MANY(rreturn_s, dp->mod);
	mem_zero(dp->dring);
	return dp;
}

void deadpoll_dtor(void* pdp){
	deadpoll_s* dp = pdp;
	io_uring_queue_exit(&dp->ring);
	mem_free(dp->dring);
}

void deadpoll_begin(unsigned maxrequest){
	deadpoll_ctor(&aio, maxrequest);
}

void deadpoll_end(void){
	deadpoll_dtor(&aio);
}

__private sqe_s* request_raw(deadpoll_s* dp){
	sqe_s* raw;
	while( !(raw = io_uring_get_sqe(&dp->ring)) ){
		if( dp->pending > 0 ){
			if( io_uring_submit(&dp->ring) < 0 ) die("on submit sqe");
			aio.inprocess += aio.pending;
			aio.pending = 0;
		}
		else{
			die("internal error: real need to wait cqe");
		}
	}
	return raw;
}

__private request_t request_new(deadpoll_s* dp){
	if( dp->dring[dp->next].flags ) die("internal error, report this issue, dring no space available, dp->next: %u", dp->next);
	request_t r = dp->next;
	dp->next = FAST_MOD_POW_TWO(dp->next+1, dp->mod);
	return r;
}

__private void request_apply(deadpoll_s* dp, sqe_s* raw, request_t r, unsigned flags, int fd, void* buffer){
	if( flags & R_FLAG_SEQUENCE ) raw->flags |= IOSQE_IO_LINK;
	dp->dring[r].fd     = fd;
	dp->dring[r].buffer = buffer;
	dp->dring[r].flags  = flags | R_FLAG_BUSY;
	io_uring_sqe_set_data64(raw, r);
	++dp->pending;
}

/*
__private void kt_timer_set(struct __kernel_timespec* t, size_t us){
	t->tv_sec = (time_t) us / 1000000UL;
	t->tv_nsec = (long) ((us - (t->tv_sec * 1000000)) * 1000L);
	dbg_info("s: %llu ns: %llu", t->tv_sec, t->tv_nsec);
}
*/

request_t r_flag(request_t r, unsigned flag){
	aio.dring[r].flags = flag;
	return r;
}

void r_commit(void){
	if( aio.pending > 0 ){
		dbg_info("pending %u", aio.pending);
		if( io_uring_submit(&aio.ring) < 0 ) die("on submit sqe");
		aio.inprocess += aio.pending;
		aio.pending = 0;
		dbg_info("commit inprocess: %u", aio.inprocess);
	}
}

int r_completed(request_t r){
	return aio.dring[r].flags & R_FLAG_COMPLETED;
}

int r_busy(request_t r){
	return aio.dring[r].flags & R_FLAG_BUSY;
}

__private request_t request_pop(deadpoll_s* dp){
	struct io_uring_cqe* cqe;
	int ret;
	if( (ret=io_uring_wait_cqe(&dp->ring, &cqe)) < 0 ){
		errno = -ret;
		die("on waiting cqe:%m");
	}
	request_t r = io_uring_cqe_get_data64(cqe);
	dp->dring[r].ret    = cqe->res;
	dp->dring[r].flags |= R_FLAG_COMPLETED;
	dp->dring[r].flags &= ~R_FLAG_BUSY;
	--dp->inprocess;
	io_uring_cqe_seen(&dp->ring, cqe);
	if( (dp->dring[r].flags & R_FLAG_DIE) && dp->dring[r].ret < 0 ){
		die("request %u on fd %d return error: %s", r, dp->dring[r].fd, strerror(-dp->dring[r].ret));
	}
	return r;
}

__private void r_ndispatch(deadpoll_s* dp, unsigned count){
	dbg_info("ndispatch:%u", count);
	while( count-->0 ){
		request_t r = request_pop(dp);
		if( dp->dring[r].flags & R_FLAG_NOWAIT ){
			dp->dring[r].flags = 0;
		}
	}
	dbg_info("completed");
}

void r_dispatch(int q){
	r_commit();
	unsigned disp;
	if( q < 0 ){
		disp = aio.inprocess;
	}
	else if( q == 0 ){
		disp = io_uring_cq_ready(&aio.ring);
	}
	else{
		disp = (unsigned)q > aio.inprocess ? aio.inprocess : (unsigned)q;
	}
	r_ndispatch(&aio, disp);
}

rreturn_s r_await(request_t r){
	if( aio.dring[r].flags & R_FLAG_NOWAIT )  die("unable to wait on unwaitable request");
	do{
		r_dispatch(1);
	}while( aio.dring[r].flags & R_FLAG_BUSY );
	aio.dring[r].flags = 0;
	if( aio.dring[r].ret < 0 ){
		errno = -aio.dring[r].ret;
		aio.dring[r].ret = -1;
	}
	return aio.dring[r];
}

request_t r_cancel(request_t rcanc, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_cancel64(raw, rcanc, 0);
	request_apply(&aio, raw, r, flags, -1, NULL);
	dbg_info("new cancel request %u", rcanc);
	return r;
}

request_t r_nop(unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_nop(raw);
	request_apply(&aio, raw, r, flags, -1, NULL);
	dbg_info("request nop");
	return r;
}

request_t r_open(const char* path, unsigned mode, unsigned privileges, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_open(raw, path, mode, privileges);
	request_apply(&aio, raw, r, flags, -1, (void*)path);
	dbg_info("request open %s", path);
	return r;
}

request_t r_read(int fd, void* buf, unsigned size, off_t offset, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_read(raw, fd, buf, size, offset);
	request_apply(&aio, raw, r, flags, fd, buf);
	dbg_info("request read(%u, %u, %lu)", fd, size, offset);
	return r;
}

request_t r_write(int fd, const void* buf, unsigned size, off_t offset, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_write(raw, fd, buf, size, offset);
	request_apply(&aio, raw, r, flags, fd, (void*)buf);
	dbg_info("request write(%u, %u, %lu)", fd, size, offset);
	return r;
}

request_t r_fsync(int fd, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_fsync(raw, fd, 0);
	request_apply(&aio, raw, r, flags, fd, NULL);
	dbg_info("request fsync(%u)", fd);
	return r;
}

request_t r_fdatasync(int fd, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_fsync(raw, fd, IORING_FSYNC_DATASYNC);
	request_apply(&aio, raw, r, flags, fd, NULL);
	dbg_info("request fdatasync(%u)", fd);
	return r;
}

request_t r_close(int fd, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_close(raw, fd);
	request_apply(&aio, raw, r, flags, fd, NULL);
	dbg_info("request close(%u)", fd);
	return r;
}

request_t r_fallocate(int fd, int mode, off_t offset, off_t len, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_fallocate(raw, fd, mode, offset, len);
	request_apply(&aio, raw, r, flags, fd, NULL);
	dbg_info("request fallocate(%u, %d, %lu, %lu)", fd, mode, offset, len);
	return r;
}

request_t r_ftruncate(int fd, loff_t len, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_ftruncate(raw, fd, len);
	request_apply(&aio, raw, r, flags, fd, NULL);
	dbg_info("request ftruncate(%u, %lu)", fd, len);
	return r;
}

request_t r_statx(const char *path, int flag, unsigned mask, struct statx *statxbuf, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_statx(raw, AT_FDCWD, path, flag, mask, statxbuf);
	request_apply(&aio, raw, r, flags, -1, (char*)path);
	dbg_info("request statx(%s, %d, 0x%X)", path, flag, mask);
	return r;
}

request_t r_unlink(const char *path, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_unlink(raw, path, 0);
	request_apply(&aio, raw, r, flags, -1, (char*)path);
	dbg_info("request unlink(%s)", path);
	return r;
}

request_t r_rename(const char *oldp, const char* newp, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_rename(raw, oldp, newp);
	request_apply(&aio, raw, r, flags, -1, (char*)oldp);
	dbg_info("request rename(%s, %s)", oldp, newp);
	return r;
}

request_t r_mkdir(const char *path, mode_t privileges, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_mkdir(raw, path, privileges);
	request_apply(&aio, raw, r, flags, -1, (char*)path);
	dbg_info("request mkdir(%s, 0%o)", path, privileges);
	return r;
}

request_t r_symlink(const char *target, const char* linkpath, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_symlink(raw, target, linkpath);
	request_apply(&aio, raw, r, flags, -1, (char*)target);
	dbg_info("request symlink(%s, %s)", target, linkpath);
	return r;
}

request_t r_link(const char *oldp, const char* newp, unsigned flags){
	request_t r = request_new(&aio);
	sqe_s* raw  = request_raw(&aio);
	io_uring_prep_link(raw, oldp, newp, 0);
	request_apply(&aio, raw, r, flags, -1, (char*)oldp);
	dbg_info("request link(%s, %s)", oldp, newp);
	return r;
}

/*
__private void request_alloc(deadpoll_s* dp){
   	unsigned entries = *dp->ring.sq.kring_entries;
	unsigned perPage = PAGE_SIZE / sizeof(request_t);
	unsigned maxReq = ROUND_UP(entries, perPage);
	dbg_info("allocate request");
	request_t* mem = (request_t*)malloc(sizeof(request_t)*maxReq);
	for( unsigned i = 0; i < maxReq - 1; ++i ){
		mem[i].next = &mem[i+1];
	}
	mem[maxReq-1].next = dp->mem;
	dp->mem = mem;
}
*/

/*
__private void request_init(request_t* rq, deadpoll_s* dp, int fdpath, rq_f fn, void* ctx, unsigned flags){
	memset(rq, 0, sizeof(request_t));
	rq->dp     = dp;
	rq->fn     = fn;
	rq->ctx    = ctx;
	rq->fdpath = fdpath;
	rq->state  = RQ_STATE_WAIT;
	rq->flags  = flags;
	rq->raw    = io_uring_get_sqe(&dp->ring);
}

__private request_t* request_new(deadpoll_s* dp, int fdpath, rq_f fn, void* ctx, unsigned flags){
	//if( !dp->mem ) request_alloc(dp);
	if( io_uring_cqe_available(&dp->ring) >= dp->autoqueuecqe || io_uring_sqe_available(&dp->ring) >= dp->autoqueuesqe ){
		dbg_info("autodispatch");
		dispatch();
	}
	request_t* ret = mb_alloc(dp->mem);
	//request_t* ret = dp->mem;
	//dp->mem = dp->mem->next;
	request_init(ret, dp, fdpath, fn, ctx, flags);
	ret->next = NULL;
	return ret;
}

void request_free(request_t* rq){
	dbg_info("release %p", rq);
	if( !rq ) return;
	if( rq->state == RQ_STATE_DELETE ) die("double release request");
	if( rq->fdpath != -1 ) request_close(rq->fdpath, NULL, NULL, REQUEST_FLAG_CLEAN);
	rq->state = RQ_STATE_DELETE;

	deadpoll_s* dp = rq->dp;
	mb_free(dp->mem, rq);
	//rq->next = dp->mem;
	//dp->mem = rq;
}

__private deadpoll_s* deadpoll_new(unsigned queueSize, unsigned flags){
	deadpoll_s* dp = NEW(deadpoll_s);
	unsigned ioflags = 0;
	if( flags & DEADPOLL_FLAG_REQUEST_POLL ) ioflags |= IORING_SETUP_SQPOLL;

	if( io_uring_queue_init(queueSize, &dp->ring, ioflags) ) die("liburing init queue");
	dp->mem       = NULL;
	dp->count     = 0;
	dp->inprocess = 0;
	dp->counthfd  = 0;
	dp->ithfd     = 0;
	dp->hfdinit   = 0;
	for( unsigned i = 0; i < DEADPOLL_MAXHFD; ++i ){
		dp->hfd[i] = -1;
	}
	dp->autoqueuecqe = *dp->ring.cq.kring_entries - 4;
	dp->autoqueuesqe = *dp->ring.sq.kring_entries - 4;
	dbg_info("queuesize: %u sqe:%u cqe:%u", queueSize, *dp->ring.sq.kring_entries, *dp->ring.cq.kring_entries);
	//request_alloc(dp);
	dp->mem = mb_new(sizeof(request_t), 16);
	return dp;
}

__ctor_core
void deadpoll_begin(void){
	dbg_info("deadpoll flags:0x%X", DEADPOLL_FLAGS);
	deadpoll_end();
	aio = deadpoll_new(DEADPOLL_BEGIN_SIZE, DEADPOLL_FLAGS);
}

__dtor_low9
void deadpoll_end(void){
	if( !aio ) return;
	dbg_info("");
	io_uring_queue_exit(&aio->ring);
	mb_delete(aio->mem);
	free(aio);
	aio = NULL;
}

__private void rq_stop(__unused request_t* req){
	aio->flags |= DEADPOLL_FLAG_STOP;
	dbg_info("stopping deadpoll loop:0x%X", aio->flags);
}

void deadpoll_stop(int stop){
	if( stop ){
		request_event(rq_stop, NULL, 0);
	}
	else{
		aio->flags &= ~DEADPOLL_FLAG_STOP;
	}
}

__private void request_ctor(request_t* req, size_t offset, unsigned rqflags){
	unsigned flags = offset == -1UL ? IORING_FEAT_RW_CUR_POS : 0;
	if( rqflags & REQUEST_FLAG_HANDLE ) flags |= IOSQE_FIXED_FILE;
	io_uring_sqe_set_data(req->raw, req);
	io_uring_sqe_set_flags(req->raw, flags);
	req->sqe = *req->raw;
	++req->dp->pending;
}

__private void update_request(request_t* rq, struct io_uring_cqe* cqe){
	if( cqe->res < 0 ){
		rq->error = -cqe->res;
		rq->ret = -1;
	}
	else{
		rq->error = 0;
		rq->ret = cqe->res;
	}
	rq->state = RQ_STATE_COMPLETE;
	--rq->dp->inprocess;
}

__private request_t* request_get(deadpoll_s* dp){
	struct io_uring_cqe* cqe;
	int ret;
	if( (ret=io_uring_wait_cqe(&dp->ring, &cqe)) < 0 ){
		errno = -ret;
		die("on waiting cqe:%m");
	}
	request_t* rq = (request_t*)io_uring_cqe_get_data(cqe);
	dbg_info("get request fn:%p fd(%d) result(%d): %p", rq->fn, rq->sqe.fd, cqe->res, rq);
	iassert(rq);
	update_request(rq, cqe);
	io_uring_cqe_seen(&dp->ring, cqe);
	return rq;
}

int ndispatch(unsigned count){
	if( aio->pending ) request_commit();
	dbg_info("dispatch:%u", count);
	while( count-->0 ){
		request_t* rq = request_get(aio);
		int autoc = rq->flags & REQUEST_FLAG_CLEAN;
		if( rq->fn ) rq->fn(rq);
		if( autoc )  request_free(rq);
	}
	dbg_info("completed");
	return aio->flags & DEADPOLL_FLAG_STOP;
}


int dispatch_all(void){
	unsigned count = aio->inprocess;
	aio->inprocess = 0;
	return ndispatch(count);
}

int dispatch(void){
	return ndispatch(io_uring_cq_ready(&aio->ring));
}

int await(request_t* rq){
	if( aio->pending ) request_commit();
	if( !rq || rq->state != RQ_STATE_WAIT ) return 0;
	deadpoll_s* dp = rq->dp;
	int loop = 1;

	while( loop && !(aio->flags & DEADPOLL_FLAG_STOP) ){
		request_t* newrq = request_get(dp);
		int autoc = newrq->flags & REQUEST_FLAG_CLEAN;
		if( newrq == rq ) loop = 0;
		if( newrq->fn ) newrq->fn(newrq);
		if( autoc ) request_free(newrq);
	}
	return aio->flags & DEADPOLL_FLAG_STOP;
}

void event_loop(void){
	while( !(aio->flags & DEADPOLL_FLAG_STOP) ){
		dbg_info("flags:0x%X", aio->flags);
		if( aio->pending ) request_commit();
		request_t* rq = request_get(aio);
		int autoc = rq->flags & REQUEST_FLAG_CLEAN;
		if( rq->fn ) rq->fn(rq);
		if( autoc ) request_free(rq);
	}
	dbg_warning("stopped");
}

void request_commit(void){
	if( io_uring_submit(&aio->ring) < 0 ) die("on submit sqe");
	aio->inprocess += aio->pending;
	aio->pending = 0;
}

request_t* request_sequence(request_t* rq){
	rq->sqe.flags |= IOSQE_IO_LINK;
	io_uring_sqe_set_flags(rq->raw, rq->sqe.flags);
	return rq;
}

__private int handle_fd(void){
	if( aio->counthfd + 1 > DEADPOLL_MAXHFD ) return -1;
	while( aio->hfd[aio->ithfd] != -1 ){
		aio->ithfd = (aio->ithfd+1) % DEADPOLL_MAXHFD;
	}
	++aio->counthfd;
	int ret = aio->ithfd;
	aio->ithfd = (aio->ithfd+1) % DEADPOLL_MAXHFD;
	return ret;
}

int register_fd(int fd){
	int ret = 0;
	int hit = handle_fd();
	if( hit == -1 ) return -1;
	aio->hfd[hit] = fd;

	if( aio->hfdinit ){
		ret = io_uring_register_files_update(&aio->ring, hit, aio->hfd, 1);
	}
	else{
		ret = io_uring_register_files(&aio->ring, aio->hfd, DEADPOLL_MAXHFD);
		aio->hfdinit = 1;
	}

	if( ret < 0 ){
		errno = -ret;
		hit = -1;
	}

	return hit;
}

int unregister_handle(int h, int closefd){
	if( h == -1 ) return -1;
	if( !aio->hfdinit ) return -1;
	if( h > DEADPOLL_MAXHFD ) return -1;
	if( aio->hfd[h] == -1 ) return 0;
	
	int ret;
	int oldfd = aio->hfd[h];
	aio->hfd[h] = -1;
	if( (ret=io_uring_register_files_update(&aio->ring, h, aio->hfd, 1)) < 0 ){
		errno = -ret;
		aio->hfd[h] = oldfd;
		return -1;
	}
	if( closefd ) close(oldfd);
	return 0;
}

int unregister_all(int closefd){
	int ret;
	if( (ret=io_uring_unregister_files(&aio->ring)) < 0 ){
		errno = -ret;
		return -1;
	}
	aio->hfdinit = 0;
	for( unsigned i = 0; i < DEADPOLL_MAXHFD; ++i ){
		if( closefd ) close(aio->hfd[i]);
		aio->hfd[i] = -1;
	}
	return 0;
}

__private void onopen(request_t* rq){
	dbg_info("rq ret:%d error:%d", rq->ret, rq->error);
	iassert(rq->sqe.opcode == IORING_OP_OPENAT);
	if( rq->error ){
		rq->sqe.fd = -1;
	}
	else{
		rq->sqe.fd = rq->ret;
		dbg_info("open fd %d", rq->sqe.fd);
	}
	if( rq->userFN ) rq->userFN(rq);
}

request_t* request_open(int cwdfd, const char* path, unsigned mode, unsigned privileges, rq_f fn, void* ctx, unsigned rqflags){
	int op = -1;
	if( cwdfd == -1 ) op = cwdfd = open("./", O_PATH);
	request_t* rq = request_new(aio, op, onopen, ctx, rqflags);
	rq->userFN = fn;
	dbg_info("new open(cwd:%d) fn:%p request %p", cwdfd, onopen, rq);	
	io_uring_prep_openat(rq->raw, cwdfd, path, mode, privileges);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_close(int fd, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new close(%d) request %p", fd, rq);
	io_uring_prep_close(rq->raw, fd);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_read(int fd, void* data, size_t size, size_t offset, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new read(%d) request %p", fd, rq);
	io_uring_prep_read(rq->raw, fd, data, size, offset);
	request_ctor(rq, offset, rqflags);
	return rq;
}

request_t* request_readv(int fd, const struct iovec* iovecs, size_t count, size_t offset, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new readv request %p", rq);
	io_uring_prep_readv(rq->raw, fd, iovecs, count, offset);
	request_ctor(rq, offset, rqflags);
	return rq;
}

request_t* request_write(int fd, const void* data, size_t size, size_t offset, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new write request %p", rq);
	io_uring_prep_write(rq->raw, fd, data, size, offset);
	request_ctor(rq, offset, rqflags);
	return rq;
}

request_t* request_writev(int fd, const struct iovec* iovecs, size_t count, size_t offset, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new writev request %p", rq);
	io_uring_prep_writev(rq->raw, fd, iovecs, count, offset);
	request_ctor(rq, offset, rqflags);
	return rq;
}

request_t* request_splice(int fdin, ssize_t offin, ssize_t fdout, size_t offout, size_t size, unsigned flags, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new splice request %p", rq);
	io_uring_prep_splice(rq->raw, fdin, offin, fdout, offout, size, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_tee(int fdin, ssize_t fdout, size_t size, unsigned flags, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new tee request %p", rq);
	io_uring_prep_tee(rq->raw, fdin, fdout, size, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_fsync(int fd, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new fsync request %p", rq);
	io_uring_prep_fsync(rq->raw, fd, 0);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_accept(int fd, struct sockaddr *addr, socklen_t *addrlen, unsigned flags, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new accept request %p", rq);
	io_uring_prep_accept(rq->raw, fd, addr, addrlen, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_connect(int fd, struct sockaddr *addr, socklen_t addrlen, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new connect request %p", rq);
	io_uring_prep_connect(rq->raw, fd, addr, addrlen);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_recv(int fd, void* data, size_t size, int flags, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new recv request %p", rq);
	io_uring_prep_recv(rq->raw, fd, data, size, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_recvmsg(int fd, struct msghdr* data, int flags, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new recv request %p", rq);
	io_uring_prep_recvmsg(rq->raw, fd, data, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

__private struct msghdr* msghdr_new(void* desc, size_t size, int fd){
    struct msghdr* msg = NEW(struct msghdr);
	msg->msg_name = NULL;
	msg->msg_namelen = 0;
	msg->msg_flags = 0;

	struct iovec* io = NEW(struct iovec);
	if( desc ){
		io->iov_base = desc;
	}
	else{
		io->iov_base = MANY(char, size);
		memset(io->iov_base, 0, size);
	}
	io->iov_len = size;

    msg->msg_iov = io;
    msg->msg_iovlen = 1;

	msg->msg_controllen = CMSG_SPACE(sizeof(fd));
	msg->msg_control = MANY(char, msg->msg_controllen);
	memset(msg->msg_control, 0, msg->msg_controllen);

	if( fd != -1 ){
	    struct cmsghdr* cmsg = CMSG_FIRSTHDR(msg);
		cmsg->cmsg_level = SOL_SOCKET;
	    cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
	    *((int*)CMSG_DATA(cmsg)) = fd;
	}

	return msg;
}

__private void msghdr_free(struct msghdr* msg){
	free(msg->msg_control);
	free(msg->msg_iov);
	free(msg);
}

__private void recv_fd_wrap(request_t* req){
	struct msghdr* msg = req->ctx;

	if( req->ret > 0 ){
		struct cmsghdr* cmsg = CMSG_FIRSTHDR(msg);
		unsigned char*  data = CMSG_DATA(cmsg);
		req->ret = *((int*)data);
		dbg_info("recv fd:%d", req->ret);
	}
	else{
		req->ret = -1;
	}
	
	req->ctx = req->usrctx;
	req->fn  = req->userFN;
	req->usrctx = msg->msg_iov->iov_base;
	if( req->fn ) req->fn(req);
	msghdr_free(msg);
}

request_t* request_recv_fd(int sck, size_t size, rq_f fn, void* ctx, unsigned rqflags){
	dbg_info("");
	struct msghdr* msg = msghdr_new(NULL, size, -1);

	request_t* rq = request_new(aio, -1, recv_fd_wrap, msg, rqflags);
	dbg_info("new recvfd request %p", rq);
	rq->userFN = fn;
	rq->usrctx = ctx;
	
	io_uring_prep_recvmsg(rq->raw, sck, msg, MSG_WAITALL);

	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_send(int fd, const void* data, size_t size, size_t flags, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new send request %p", rq);
	io_uring_prep_send(rq->raw, fd, data, size, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_sendmsg(int fd, const struct msghdr* data, int flags, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new recv request %p", rq);
	io_uring_prep_sendmsg(rq->raw, fd, data, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

__private void send_fd_wrap(request_t* req){
	struct msghdr* msg = req->ctx;
	req->ctx    = req->usrctx;
	req->fn     = req->userFN;
	req->usrctx = msg->msg_iov->iov_base;
	if( req->fn ) req->fn(req);
	msghdr_free(req->ctx);
}

request_t* request_send_fd(int sck, int fd, void* desc, size_t size, rq_f fn, void* ctx, unsigned rqflags){
	struct msghdr* msg = msghdr_new(desc, size, fd);

	request_t* rq = request_new(aio, -1, send_fd_wrap, msg, rqflags);
	dbg_info("new sendfd request %p fd:%d", rq, fd);
	rq->userFN = fn;
	rq->usrctx = ctx;
	
	io_uring_prep_sendmsg(rq->raw, sck, msg, 0);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_shutdown(int fd, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new shutdown request %p", rq);
	io_uring_prep_shutdown(rq->raw, fd, 0);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_poll_add(int fd, int pollmask, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new poll_add request %p", rq);
	io_uring_prep_poll_add(rq->raw, fd, pollmask);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_poll_multishot(int fd, int pollmask, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new poll_add request %p", rq);
	io_uring_prep_poll_multishot(rq->raw, fd, pollmask);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_poll_remove(request_t* req, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new poll_remove request %p", rq);
	io_uring_prep_poll_remove(rq->raw, req->ctx);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_epoll_ctl(int epfd, int fd, int op, struct epoll_event* ev, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new epollctx request %p", rq);
	io_uring_prep_epoll_ctl(rq->raw, epfd, fd, op, ev);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_statx(int cwdfd, const char* path, struct statx* stx, unsigned flags, unsigned mask, rq_f fn, void* ctx, unsigned rqflags){
	int op = -1;
	if( mask == 0  ) mask = STATX_ALL;
	if( cwdfd == -1 ) op = cwdfd = open("./", O_PATH);
	request_t* rq = request_new(aio, op, fn, ctx, rqflags);
	dbg_info("new statx request %p", rq);
	io_uring_prep_statx(rq->raw, cwdfd, path, flags, mask, stx);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_unlink(int cwdfd, const char* path, unsigned flags, rq_f fn, void* ctx, unsigned rqflags){
	int op = -1;
	if( cwdfd == -1 ) op = cwdfd = open("./", O_PATH);
	request_t* rq = request_new(aio, op, fn, ctx, rqflags);
	dbg_info("new unlink request %p", rq);
	io_uring_prep_unlinkat(rq->raw, cwdfd, path, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_rename(int oldcwdfd, const char* oldpath, int cwdfd, const char* path, unsigned flags, rq_f fn, void* ctx, unsigned rqflags){
	int op = -1;
	if( cwdfd == -1 && oldcwdfd == -1 ) op = cwdfd = oldcwdfd = open("./", O_PATH);
	request_t* rq = request_new(aio, op, fn, ctx, rqflags);
	dbg_info("new rename request %p", rq);
	io_uring_prep_renameat(rq->raw, oldcwdfd, oldpath, cwdfd, path, flags);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_mkdir(int cwdfd, const char* path, unsigned privileges, rq_f fn, void* ctx, unsigned rqflags){
	int op = -1;
	if( cwdfd == -1 ) op = cwdfd = open("./", O_PATH);
	request_t* rq = request_new(aio, op, fn, ctx, rqflags);
	dbg_info("new unlink request %p", rq);
	io_uring_prep_mkdirat(rq->raw, cwdfd, path, privileges);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_event(rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new nop request %p", rq);
	io_uring_prep_nop(rq->raw);
	request_ctor(rq, 0, rqflags);
	return rq;
}

__private void request_timer_set(request_t* rq, size_t us){
	rq->timer.tv_sec = (time_t) us / 1000000UL;
	rq->timer.tv_nsec = (long) ((us - (rq->timer.tv_sec * 1000000)) * 1000L);
}

__private void dotimer(request_t* rq){
	unsigned long ellapse = time_us() - rq->startus;
	if( ellapse < rq->us ){
		rq_f usr = rq->userFN;
		unsigned long us = rq->us;
		unsigned long st = rq->startus;
		request_init(rq, aio, -1, dotimer, rq->ctx, rq->flags);
		rq->us      = us;
		rq->startus = st;
		rq->userFN  = usr;
		us -= ellapse;
		request_timer_set(rq, us);
		io_uring_prep_timeout(rq->raw, &rq->timer, 1, IORING_TIMEOUT_REALTIME);
		request_ctor(rq, 0, rq->flags);
		request_commit();
	}
	else if( rq->userFN ) rq->userFN(rq);
}

request_t* request_timer(size_t us, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, dotimer, ctx, rqflags);
	rq->us      = us;
	rq->startus = time_us();
	rq->userFN  = fn;
	request_timer_set(rq, us);
	dbg_info("new timer request %p wait %llus %lluns", rq, rq->timer.tv_sec, rq->timer.tv_nsec);
	io_uring_prep_timeout(rq->raw, &rq->timer, 1, IORING_TIMEOUT_REALTIME);
	request_ctor(rq, 0, rqflags);
	return rq;
}

__private void ontimeout(request_t* req){
	if( req->error != ECANCELED && req->userFN ) req->userFN(req);
}

request_t* request_timeout(request_t* req, size_t us, rq_f fn, void* ctx, unsigned rqflags){
	request_sequence(req);
	request_t* rq = request_new(aio, -1, ontimeout, ctx, rqflags);
	rq->userFN = fn;
	dbg_info("new timeout request %p", rq);
	request_timer_set(rq, us);
	io_uring_prep_link_timeout(rq->raw, &rq->timer, 0);
	request_ctor(rq, 0, rqflags);
	return rq;
}

request_t* request_cancel(request_t* request, rq_f fn, void* ctx, unsigned rqflags){
	request_t* rq = request_new(aio, -1, fn, ctx, rqflags);
	dbg_info("new cancel request %p", rq);
	io_uring_prep_cancel(rq->raw, request, 0);
	request_ctor(rq, 0, rqflags);
	return rq;
}

int request_fd(request_t* rq){
	return rq->sqe.fd;
}

void request_fd_set(request_t* rq, int fd){
	rq->sqe.fd = fd;
}

int request_fd_path(request_t* rq){
	return rq->fdpath;
}

unsigned long request_us(request_t* rq){
	return rq->us;
}

unsigned long request_start_us(request_t* rq){
	return rq->startus;
}

void* request_buffer(request_t* rq){
	return (void*)rq->sqe.addr;
}

ssize_t request_offset(request_t* rq){
	return rq->sqe.off;
}

size_t request_size(request_t* rq){
	return rq->sqe.len;
}

void* request_ctx(request_t* rq){
	return rq->ctx;
}

void request_ctx_set(request_t* rq, void* ctx){
	rq->ctx = ctx;
}

void* request_usrctx(request_t* rq){
	return rq->usrctx;
}

void request_usrctx_set(request_t* rq, void* ctx){
	rq->usrctx = ctx;
}

int request_return(request_t* rq){
	return rq->ret;
}

int request_error(request_t* rq){
	if( rq->error ){
		errno = rq->error;
		return -1;
	}
	return 0;
}

rqState_e request_state(request_t* rq){
	return rq->state;
}

request_t** request_next(request_t* rq){
	return &rq->next;
}

*/
