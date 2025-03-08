#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/request.h>

#include <auror/inutility.h>
#include <auror/www.h>
#include <auror/config.h>
#include <auror/status.h>
#include <auror/archive.h>

__private char* replace_var(char* dst, const char* src, const char* repo, const char* arch){
	const char* var;
   	while( (var=strchr(src, '$')) ){
		dst = mem_upsize(dst, var-src);
		memcpy(&dst[mem_header(dst)->len], src, var-src);
		mem_header(dst)->len += var-src;
		if( !strncmp(var, "$repo", 5) ){
			unsigned const len = strlen(repo);
			dst = mem_upsize(dst, len);
			memcpy(&dst[mem_header(dst)->len], repo, len);
			mem_header(dst)->len += len;
			src = var+5;
		}
		else if( !strncmp(var, "$arch", 5) ){
			unsigned const len = strlen(arch);
			dst = mem_upsize(dst, len);
			memcpy(&dst[mem_header(dst)->len], arch, len);
			mem_header(dst)->len += len;
			src = var+5;
		}
		else{
			die("\nunknow variable in mirrorlist");
		}
	}
	unsigned const len = strlen(src);
	if( len ){
		dst = mem_upsize(dst, len);
		memcpy(&dst[mem_header(dst)->len], src, len);
		mem_header(dst)->len += len;
	}
	return mem_nullterm(dst);
}

char* mirror_url(config_s* conf, const char* repo, const char* fname, unsigned idmirror){
	mforeach(conf->repository, ir){
		if( !strcmp(conf->repository[ir].name, repo) ){
			if( idmirror >= mem_header(conf->repository[ir].mirror)->len ) return NULL;
			char* url = replace_var(MANY(char, 1024), conf->repository[ir].mirror[idmirror], repo, conf->options.arch);
			__free char* escname = url_escape(fname);
			url = mem_upsize(url, mem_header(escname)->len + 2);
			url[mem_header(url)->len++] = '/';
			memcpy(&url[mem_header(url)->len], escname, mem_header(escname)->len);
			mem_header(url)->len += mem_header(escname)->len;
			url[mem_header(url)->len++] = 0;
			return url;
		}
	}
	die("\nunable to resolve mirror, invalid repository %s", repo);
}

int download_lastsync(config_s* conf, delay_t* lastsync){
	dbg_info("get lastsync");
	mforeach(conf->repository[0].server, i){
		__free char* urlls = str_printf("%s/lastsync", conf->repository[0].server[i]);
		dbg_info("  try mirror %s", urlls);
		__www www_s w;
		www_ctor(&w, urlls, DEFAULT_RETRY, DEFAULT_RELAX);
		www_timeout(&w, conf->options.timeout);
		__free uint8_t* data = NULL;
		www_download_mem(&w, &data);
		if( !www_perform(&w) ){
			data = mem_nullterm(data);
			errno = 0;
			char* end = NULL;
			*lastsync = strtoul((char*)data, &end, 10);
			if( errno || end == NULL || end == (char*)data ) return -1;
			return 0;
		}
	}
	dbg_warning("unable to get lastsync, no downloading database");
	return -1;
}

typedef struct prvArg{
	int       fd;
	void*     buffer;
	void*     decompress;
	status_s* status;
	unsigned  idstatus;
	int       gzret;
	gzip_t    gz;
}prvArg_s;

__private size_t db_save_and_extract(void* ptr, size_t size, size_t nmemb, void* userctx){
	prvArg_s* arg = userctx;
	size_t const total = size * nmemb;
	iassert(total <= CURL_MAX_WRITE_SIZE);
	r_dispatch(-1);
	memcpy(arg->buffer, ptr, total);
	r_write(arg->fd, arg->buffer, total, -1, R_FLAG_NOWAIT | R_FLAG_DIE);
	r_commit();
	arg->gzret = gzip_decompress(&arg->gz, ptr, total);
	return total;
}

__private void db_download_progress(void* ctx, double perc, double speed, __unused unsigned long eta){
	prvArg_s* a = ctx;
	status_speed(a->status, speed);
	status_refresh(a->status, a->idstatus, perc, STATUS_TYPE_DOWNLOAD);
}

void* download_database(const char* dbtmpname, status_s* status, unsigned idstatus, configRepository_s* repo, config_s* conf){
	dbg_info("");
	prvArg_s a;
	a.status   = status;
	a.idstatus = idstatus;
	a.fd = open(dbtmpname, O_CREAT | O_WRONLY, 0755);
	if( a.fd == -1 ) die("unable to create temp database %s: %m", dbtmpname);
	a.buffer = MANY(char*, CURL_MAX_WRITE_SIZE);
	
	mforeach(repo->mirror, i){
		gzip_ctor(&a.gz);
		__free char* urlls = str_printf("%s/%s.db", repo->mirror[i], repo->name);
		dbg_info("try download from mirror %s", urlls);
		__www www_s w;
		www_ctor(&w, urlls, DEFAULT_RELAX, DEFAULT_RELAX);
		www_timeout(&w, conf->options.timeout);
		www_download_custom(&w, db_save_and_extract, &a);
		www_progress(&w, db_download_progress, &a);
		if( !www_perform(&w) ){
			r_dispatch(-1);
			dbg_info("download completed");
			void* dec = a.gz.next_out;
			a.gz.next_out = NULL;
			if( a.gzret ) die("wrong decompression database %s", repo->name);
			gzip_dtor(&a.gz);
			close(a.fd);
			mem_free(a.buffer);
			return dec;
		}
		r_dispatch(-1);
		lseek(a.fd, 0, SEEK_SET);
		ftruncate(a.fd, 0);
		gzip_dtor(&a.gz);
	}
	die("unable to download database %s", repo->name);
}





/*
typedef struct downctx{
	config_s*   conf;
	//ppProgress_s* prog;
}downctx_s;

__private void ppdownload_progress(void* ctx, double perc, double speed, unsigned long etas){
	downctx_s* dctx = ctx;
	//pp_progres_write(dctx->conf);
	//dctx->prog->eta   = etas;
	//dctx->prog->speed = speed;
	//dctx->prog->adv   = perc;
	//pp_progress_flush(dctx->conf, dctx->prog);
}
*/
/*
void pp_download_file(ppConfig_s* conf, ppProgress_s* prog, const char* repo, const char* file, const char* dest){
	downctx_s dctx = { conf, prog};
	unsigned idmirror = 0;
	__free char* mirror = NULL;
	while( (mirror=pp_mirror_url(conf, repo, file, idmirror++)) ){
		if( !www_download(mirror, 0, conf->options.timeout, conf->options.retry, NULL, dest, prog ? ppdownload_progress : NULL, &dctx) ){
			break;
		}
		mem_free(mirror);
	}
	if( !mirror ){
		die("\nunable to download file %s, probably internet problems", file);
	}
}
*/

