#ifndef __NOTSTD_REQUEST_H__
#define __NOTSTD_REQUEST_H__

#include <notstd/core.h>
#include <notstd/delay.h>

#include <netinet/in.h>
#include <sys/epoll.h>
#define _GNU_SOURCE
#include <liburing.h>

typedef struct deadpoll deadpoll_s; 

#define R_FLAG_NOWAIT    0x04
#define R_FLAG_SEQUENCE  0x08
#define R_FLAG_DIE       0x10


//#define R_FLAG_TIMER     0x04

typedef struct rreturn{
	void*    buffer;
	int      ret;
	unsigned flags;
	int      fd;
}rreturn_s;

typedef unsigned request_t;

typedef struct deadpoll{
	struct io_uring ring;
	rreturn_s*      dring;
	unsigned        autoqueuecqe;
	unsigned        autoqueuesqe;
	unsigned        pending;
	unsigned        inprocess;
	unsigned        count;
	unsigned        next;
	unsigned        mod;
	//int hfd[DEADPOLL_MAXHFD];
	//unsigned counthfd;
	//unsigned ithfd;
	//int      hfdinit;
	//unsigned flags;
}deadpoll_s;

deadpoll_s* deadpoll_ctor(deadpoll_s* dp, unsigned maxrequest);
void deadpoll_dtor(void* pdp);
void deadpoll_begin(unsigned maxrequest);
void deadpoll_end(void);

void r_commit(void);
int r_completed(request_t r);
int r_busy(request_t r);
void r_dispatch(int q);
rreturn_s r_await(request_t r);
request_t r_cancel(request_t rcanc, unsigned flags);
request_t r_nop(unsigned flags);
request_t r_open(const char* path, unsigned mode, unsigned privileges, unsigned flags);
request_t r_read(int fd, void* buf, unsigned size, off_t offset, unsigned flags);
request_t r_write(int fd, const void* buf, unsigned size, off_t offset, unsigned flags);
request_t r_fsync(int fd, unsigned flags);
request_t r_fdatasync(int fd, unsigned flags);
request_t r_close(int fd, unsigned flags);
request_t r_fallocate(int fd, int mode, off_t offset, off_t len, unsigned flags);
request_t r_ftruncate(int fd, loff_t len, unsigned flags);
request_t r_statx(const char *path, int flag, unsigned mask, struct statx *statxbuf, unsigned flags);
request_t r_unlink(const char *path, unsigned flags);
request_t r_rename(const char *oldp, const char* newp, unsigned flags);
request_t r_mkdir(const char *path, mode_t privileges, unsigned flags);
request_t r_symlink(const char *target, const char* linkpath, unsigned flags);
request_t r_link(const char *oldp, const char* newp, unsigned flags);






#endif
