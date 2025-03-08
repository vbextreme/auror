#include <notstd/core.h>
#include <notstd/str.h>

#include <auror/archive.h>

#include <archive.h>
#include <zstd.h>

gzip_t* gzip_ctor(gzip_t* gz){
	memset(gz, 0, sizeof(gzip_t));
	if( inflateInit2(gz, 16 + MAX_WBITS) != Z_OK ) die("Unable to initialize zlib");
	return gz;
}

void gzip_dtor(gzip_t* gz){
	if( gz->next_out ) mem_free(gz->next_out);
	inflateEnd(gz);
}

int gzip_decompress(gzip_t* gz, void* data, size_t size){
	size_t framesize = size * 6;
	char* dec = (char*)gz->next_out;
	if( !dec ){
		dec = MANY(char, framesize);
	}
	else{
		dec = mem_upsize(dec, framesize);
	}
	
	gz->avail_in  = size;
	gz->next_in   = (Bytef*)data;
	gz->next_out  = (Bytef*)(dec + *mem_len(dec));
	gz->avail_out = framesize;
	
	int ret;
	do{
		if( (ret=inflate(gz, Z_NO_FLUSH)) == Z_ERRNO ){
			mem_free(dec);
			gz->next_out = NULL;
			errno = EIO;
			dbg_error("decompression failed");
			return -1;
		}
		//if( ++n == 5 ) die("");
		mem_header(dec)->len += framesize - gz->avail_out;
		if( gz->avail_out == 0 ){
			dec           = mem_upsize(dec, framesize);
			framesize     = mem_available(dec);
			gz->avail_out = framesize;
		}
		gz->next_out  = (Bytef*)(mem_addressing(dec, mem_header(dec)->len));
	}while( ret != Z_STREAM_END && (ret == 0 && gz->avail_in != 0));
	gz->next_out = (Bytef*)dec;
	return ret == Z_STREAM_END ? 0 : 1;
}

void* gzip_decompress_all(void* data) {
	z_stream strm;
	memset(&strm, 0, sizeof(strm));
	if( inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK ) die("Unable to initialize zlib");
	size_t datasize = mem_header(data)->len;
	size_t framesize = datasize * 2;
	void* dec = MANY(char, framesize);
	
	strm.avail_in  = datasize;
	strm.next_in   = (Bytef*)data;
	strm.avail_out = framesize;
	strm.next_out  = (Bytef*)dec;
	
	int ret;
	do{
		if( (ret=inflate(&strm, Z_NO_FLUSH)) == Z_ERRNO ){
			mem_free(dec);
			errno = EIO;
			dbg_error("decompression failed");
			return NULL;
		}
		mem_header(dec)->len += framesize - strm.avail_out;
		if (strm.avail_out == 0) {
			dec = mem_upsize(dec, framesize);
			framesize = mem_available(dec);
			strm.avail_out = framesize;
		}
		strm.next_out  = (Bytef*)(mem_addressing(dec, mem_header(dec)->len));
	}while( ret != Z_STREAM_END );
	
	inflateEnd(&strm);
	return dec;
}

void* zstd_decompress(void* data){
	const size_t isize = mem_header(data)->len;
	const size_t chunk = ZSTD_DStreamOutSize();
	char* buf = MANY(char, chunk);
	ZSTD_DCtx* const dctx = ZSTD_createDCtx();
	if( !dctx ) die("unable to create zstd ctx");
	ZSTD_inBuffer inp = {
		.src  = data,
		.size = isize,
		.pos  = 0
	};
	ZSTD_outBuffer out;
	while( inp.pos < inp.size ){
		buf = mem_upsize(buf, chunk);
		out.dst  = &buf[mem_header(buf)->len];
		out.size = chunk;
		out.pos  = 0;
		size_t const ret = ZSTD_decompressStream(dctx, &out , &inp);
		mem_header(buf)->len += out.pos;
		if( ZSTD_isError(ret) ) die("zstd unable get frame: %s", ZSTD_getErrorName(ret));
	}
	ZSTD_freeDCtx(dctx);
	return buf;
}


#define TAR_BLK  148
#define TAR_CHK  8
#define TAR_SIZE 512
#define TAR_MAGIC "ustar"

typedef struct htar_s{
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char checksum[8];
	char typeflag;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
	char pad[12];
}htar_s;

__private const htar_s zerotar;

void tar_mopen(tar_s* tar, void* data){
	tar->start  = data;
	tar->loaddr = (uintptr_t)data;
	tar->end    = tar->loaddr + mem_header(data)->len;
	tar->err    = 0;
	//dbg_info("start: %lu end: %lu tot: %lu n: %lu", tar->loaddr, tar->end, tar->end-tar->loaddr, (tar->end-tar->loaddr)/512);
	memset(&tar->global, 0, sizeof tar->global);
}

__private unsigned tar_checksum(void* data){
	uint8_t* d = (uint8_t*) data;
	unsigned i;
	unsigned chk = 0;
	
	for( i = 0; i < TAR_BLK; ++i )
		chk += d[i];
	for( unsigned k = 0; k < TAR_CHK; ++k )
		chk += ' ';
	i += TAR_CHK;
	for( ; i < sizeof(htar_s); ++i )
		chk += d[i];
	
	return chk;
}

__private htar_s* htar_get(tar_s* tar){
	if( tar->loaddr >= tar->end ){
		dbg_error("out of tar bound");
		tar->err = ENOENT;
		return NULL;
	}
	htar_s* h = (htar_s*)tar->loaddr;
	if( !memcmp(h, &zerotar, sizeof zerotar) ){
		tar->loaddr += sizeof(htar_s);
		if( tar->loaddr >= tar->end ){
			dbg_error("no more data");
			tar->err = ENOENT;
			return NULL;
		}
		h = (htar_s*)tar->loaddr;
		if( !memcmp(h, &zerotar, sizeof zerotar) ){
			//dbg_info("end of tar");
			return NULL;
		}
		dbg_error("aspected end block");
		tar->err = EBADF;
		return NULL;
	}
	
	unsigned chk = strtoul(h->checksum, NULL, 8);
	if( chk != tar_checksum(h) ){
		dbg_error("wrong checksum");
		tar->err = EBADE;
		return NULL;
	}
	if( strcmp(h->magic, TAR_MAGIC) ){
		dbg_error("wrong magic");
		tar->err = ENOEXEC;
		return NULL;
	}
	return h;
}

__private int htar_pax(tar_s* tar, htar_s* h, tarent_s* ent){
	unsigned size = strtoul(h->size, NULL, 8);
	char* kv  = (char*)((uintptr_t)h + sizeof(htar_s));
	char* ekv = kv + size;
	while( kv < ekv ){
		unsigned kvsize = strtoul(kv, &kv, 10);
		++kv;
		char* k = kv;
		char* ek = strchr(k, '=');
		if( !ek ){
			tar->err = EINVAL;
			dbg_error("aspected assign: '%s'", kv);
			return -1;
		}
		char* v = ek+1;
		kv = k + kvsize;
		char* ev = kv - 1;
	
		if( !strncmp(k, "size", 4) ){
			ent->size = strtoul(v, NULL, 10);
		}
		else if( !strncmp(k, "path", 4) ){
			if( ev-v >= PATH_MAX ) die("tar ent unsupported path > %u", PATH_MAX );
			memcpy(ent->path, v, ev - v);
			ent->path[ev-v] = 0;
		}
		else{
			dbg_warning("todo add this: '%s' = '%.*s'", k, (int)(ev-v), v);
		}
	}

	return 0;
}

__private void htar_next_ent(tar_s* tar, tarent_s* ent){
	const size_t rawsize = ROUND_UP(ent->size, sizeof(htar_s));
	tar->loaddr += sizeof(htar_s) + rawsize;
}

__private void htar_next_htar(tar_s* tar, htar_s* h){
	size_t s = strtoul(h->size, NULL, 8);
	const size_t rawsize = ROUND_UP(s, sizeof(htar_s));
	tar->loaddr += sizeof(htar_s) + rawsize;
}

tarent_s* tar_next(tar_s* tar, tarent_s* ent){
	htar_s* h;
	tarent_s pax = {0};
	memset(ent, 0, sizeof(tarent_s));

	while( (h = htar_get(tar)) ){
		switch( h->typeflag ){
			case 'g':
				if( htar_pax(tar, h, &tar->global) ) goto ONERR;
				htar_next_htar(tar, h);
			break;
			
			case 'x':
				if( htar_pax(tar, h, &pax) ) goto ONERR;
				htar_next_htar(tar, h);
			break;
			
			case '0' ... '9':
				ent->type = h->typeflag - '0';
				if( pax.size > 0 ){
					ent->size = pax.size;
				}
				else if( tar->global.size > 0 ){
					ent->size = tar->global.size;
				}
				else{
					ent->size = strtoul(h->size, NULL, 8);
				}
				ent->data = ent->size ? (void*)(tar->loaddr + sizeof(htar_s)) : NULL;
				
				if( pax.path[0] ){
					strcpy(ent->path, pax.path);
					pax.path[0] = 0;
				}
				else if( tar->global.path[0] ){
					strcpy(ent->path, tar->global.path);
				}
				else{
					if( h->prefix[0] ){
						size_t pl = strnlen(h->prefix, sizeof h->prefix);
						size_t nl = strnlen(h->name, sizeof h->name);
						if( pl+nl+1 >= PATH_MAX ) die("tar ent unsupported path > %u", PATH_MAX );
						memcpy(ent->path, h->prefix, pl);
						ent->path[pl] = '/';
						memcpy(&ent->path[pl+1], h->name, nl);
						ent->path[pl+nl+1] = 0;
					}
					else{
						size_t nl = strnlen(h->name, sizeof h->name);
						if( nl >= PATH_MAX ) die("tar ent unsupported path > %u", PATH_MAX );
						memcpy(ent->path, h->name, nl);
						ent->path[nl] = 0;
					}
				}
				ent->uid  = strtoul(h->uid, NULL, 10);
				ent->gid  = strtoul(h->gid, NULL, 10);
				ent->perm = strtol(h->mode, NULL, 8);
				if( ent->type == TAR_SYMBOLIC_LINK ) ent->data = h->linkname;
				htar_next_ent(tar, ent);
			return ent;
			
			default:
				dbg_error("unknow type");
				goto ONERR;
			break;
		}
	}
	
ONERR:
	return NULL;
}

int tar_errno(tar_s* tar){
	return tar->err;
}

unsigned tar_count(void* data, tartype_e type){
	unsigned count = 0;
	tar_s tmp;
	tar_mopen(&tmp, data);
	tarent_s ent;
	while( tar_next(&tmp, &ent) ){
		if( ent.type == type ) ++count;
	}
	return count;
}















