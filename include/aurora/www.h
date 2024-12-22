#ifndef __WWW_H__
#define __WWW_H__

#include <notstd/core.h>

#ifdef RESTAPI_IMPLEMENT
#include <notstd/field.h>
#endif

typedef struct restapi{
	__prv8 void* curl;
	__prv8 char* call;
	__prv8 char* url;
}restapi_s;

typedef struct restret{
	char* header;
	char* body;
}restret_s;

void www_begin(void);
void www_end(void);
const char* www_errno_str(void);
unsigned www_errno(void);
unsigned www_connection_error(unsigned error);
unsigned www_http_error(unsigned error);
const char* www_str_error(unsigned error);

char* url_escape(const char* url);

void* www_download(const char* url, unsigned onlyheader, unsigned touts, char** realurl);
void* www_download_retry(const char* url, unsigned onlyheader, unsigned touts, unsigned retry, unsigned retryms, char** realurl);
long www_ping(const char* url);

restapi_s* restapi_ctor(restapi_s* ra, const char* url, unsigned touts);
restret_s restapi_call(restapi_s* ra, const char* args);
restapi_s* restapi_dtor(restapi_s* ra);

#endif
