#ifndef __WWW_H__
#define __WWW_H__

#include <notstd/core.h>
#include <notstd/list.h>
#include <notstd/delay.h>
#include <curl/curl.h>

#ifdef RESTAPI_IMPLEMENT
#include <notstd/field.h>
#endif

#define DOWNLOAD_RETRY_TIME 500

typedef void (*wwwprogress_f)(void* ctx, double perc, double speed, unsigned long eta);
typedef size_t (*wwwDownload_f)(void* ptr, size_t size, size_t nmemb, void* userctx);

typedef struct www{
	__prv8 CURL* curl;
	__rdon unsigned error;
	__rdon unsigned retry;
	__rdon delay_t relax;
	__prv8 wwwprogress_f prog;
	__prv8 void* progctx;
	__prv8 unsigned long holdeta; 
	__prv8 char* resturl;
}www_s;

typedef struct restret{
	char* header;
	char* body;
}restret_s;


void www_begin(void);
void www_end(void);

unsigned www_connection_error(unsigned error);
unsigned www_http_error(unsigned error);
const char* www_str_error(unsigned error);
char* url_escape(const char* url);

#define __www __cleanup(www_dtor)

void www_dtor(void* pw);
www_s* www_ctor(www_s* w, const char* url, unsigned retry, delay_t relaxms);
int www_perform(www_s* w);
char* www_real_url(www_s* w);
void www_timeout(www_s* w, unsigned sec);
void www_header_body(www_s* w, int header, int body);
void www_download_mem(www_s* w, uint8_t** out);
void www_download_file(www_s* w, FILE* f);
void www_download_custom(www_s* w, wwwDownload_f fn, void* ctx);
void www_progress(www_s* w, wwwprogress_f fn, void* ctx);
void www_restapi(www_s* w, const char* url);
restret_s www_restapi_call(www_s* w, const char* args);

long www_ping(const char* url);




#endif
