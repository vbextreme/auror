#ifndef __ARCHIVE_H__
#define __ARCHIVE_H__

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <zlib.h>

typedef enum { TAR_FILE, TAR_HARD_LINK, TAR_SYMBOLIC_LINK, TAR_CHAR_DEV, TAR_BLK_DEV, TAR_DIR, TAR_FIFO, TAR_CONTIGUOUS, TAR_GLOBAL, TAR_EXTEND, TAR_VENDOR } tartype_e;

typedef struct tarent_s{
	char      path[PATH_MAX];
	size_t    size;
	tartype_e type;
	void*     data;
	uid_t     uid;
	gid_t     gid;
	unsigned  perm;
}tarent_s;

typedef struct tar_s{
	void*     start;
	uintptr_t loaddr;
	size_t    end;
	tarent_s  global;
	int       err;
}tar_s;

typedef z_stream gzip_t;

gzip_t* gzip_ctor(gzip_t* gz);
void gzip_dtor(gzip_t* gz);
int gzip_decompress(gzip_t* gz, void* data, size_t size);
void* gzip_decompress_all(void* data);

void* zstd_decompress(void* data);

void tar_mopen(tar_s* tar, void* data);
tarent_s* tar_next(tar_s* tar, tarent_s* ent);
int tar_errno(tar_s* tar);
unsigned tar_count(void* data, tartype_e type);

#endif
