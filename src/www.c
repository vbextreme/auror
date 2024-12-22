#include <notstd/core.h>
#include <notstd/delay.h>
#include <notstd/str.h>

#include <curl/curl.h>
#include <string.h>
#include <sys/wait.h>

#define RESTAPI_IMPLEMENT
#include <auror/www.h>

#define __wcc __cleanup(www_curl_cleanup)

#define WWW_ERROR_HTTP 10000
#define WWW_BUFFER_SIZE 1024

__private char* URL_ESCAPE_MAP[255] = {
    [' '] = "%20",
    ['!'] = "%21",
    ['"'] = "%22",
    ['#'] = "%23",
    ['$'] = "%24",
    ['%'] = "%25",
    ['&'] = "%26",
    ['('] = "%28",
    [')'] = "%29",
    ['*'] = "%2A",
    ['+'] = "%2B",
    [','] = "%2C",
    ['/'] = "%2F",
    [':'] = "%3A",
    [';'] = "%3B",
    ['='] = "%3D",
    ['?'] = "%3F",
    ['@'] = "%40",
    ['['] = "%5B",
    [']'] = "%5D",
    ['<'] = "%3C",
    ['>'] = "%3E",
    ['{'] = "%7B",
    ['}'] = "%7D",
    ['|'] = "%7C",
    ['^'] = "%5E",
    ['~'] = "%7E",
    ['`'] = "%60",
    ['\\'] = "%5C",
    ['\''] = "%27"
};

__private struct curl_slist *HAPPJSON = NULL;

__thread unsigned wwwerrno;

void www_begin(void){
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl_slist_append(HAPPJSON, "Accept: application/json");
	curl_slist_append(HAPPJSON, "Content-Type: application/json");
	curl_slist_append(HAPPJSON, "charsets: utf-8");
}

void www_end(void){
	curl_slist_free_all(HAPPJSON);
	curl_global_cleanup();
}


__private const char* www_errno_http(long resCode) {
	switch (resCode) {
		case 200: return "HTTP OK";
		case 201: return "HTTP Created";
		case 202: return "HTTP Accepted";
		case 204: return "HTTP No Content";
		case 301: return "HTTP Moved Permanently";
		case 302: return "HTTP Found";
		case 400: return "HTTP Bad Request";
		case 401: return "HTTP Unauthorized";
		case 403: return "HTTP Forbidden";
		case 404: return "HTTP Not Found";
		case 500: return "HTTP Internal Server Error";
		case 502: return "HTTP Bad Gateway";
		case 503: return "HTTP Service Unavailable";
		default: return "Unknown HTTP Status Code";
	}
}

const char* www_errno_str(void){
	if( wwwerrno > WWW_ERROR_HTTP ) return www_errno_http(wwwerrno - WWW_ERROR_HTTP);
	return curl_easy_strerror(wwwerrno);
}

unsigned www_errno(void){
	return wwwerrno;
}

unsigned www_connection_error(unsigned error){
	if( error < WWW_ERROR_HTTP ) return error;
	return 0;
}

unsigned www_http_error(unsigned error){
	if( error < WWW_ERROR_HTTP ) return 0;
	return error - WWW_ERROR_HTTP;
}

const char* www_str_error(unsigned error){
	if( error > WWW_ERROR_HTTP ) return www_errno_http(error - WWW_ERROR_HTTP);
	return curl_easy_strerror(error);
}

char* url_escape(const char* url){
	char* ret = MANY(char, strlen(url) * 3 + 1);
	char* esc = ret;
	while( *url ){
		if( URL_ESCAPE_MAP[(unsigned)*url] ){
			strcpy(esc, URL_ESCAPE_MAP[(unsigned)*url++]);
			esc += 3;
		}
		else{
			*esc++ = *url++;
		}
	}
	*esc = 0;
	mem_header(ret)->len = esc - ret;
	ret = mem_fit(ret);
	return ret;
}

__private size_t www_curl_buffer_recv(void* ptr, size_t size, size_t nmemb, void* userctx){
	void* ctx = *(void**)userctx;
	const size_t sizein = size * nmemb;
	//dbg_info("in: %lu, buf.len: %u, buf.size: %lu ", sizein, mem_header(ctx)->len, mem_lenght(ctx));
	uint8_t* data = mem_upsize(ctx, sizein);
	memcpy(data + mem_header(data)->len, ptr, sizein);
	mem_header(data)->len += sizein;
	*(void**)userctx = data;
	return sizein;
}

__private void www_curl_cleanup(void* ch){
	curl_easy_cleanup(*(void**)ch);
}

__private CURL* www_curl_new(const char* url){
	CURL* ch = curl_easy_init();
	if( !ch ) die("curl init(%d): %s", errno, curl_easy_strerror(errno));
	curl_easy_setopt(ch, CURLOPT_URL, url);
	if( !strncmp(url, "https", 5) )
		curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 1L);
	else
		curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(ch, CURLOPT_VERBOSE, 0L);
	return ch;
}

__private int www_curl_perform(CURL* ch, char** realurl){
	CURLcode res;
	res = curl_easy_perform(ch);
	if ( res != CURLE_OK && res != CURLE_FTP_COULDNT_RETR_FILE ){
		dbg_error("perform return %d: %s", res, curl_easy_strerror(res));
		wwwerrno = res;
		return -1;
	}
	if( realurl ){
		char* followurl = NULL;
		curl_easy_getinfo(ch, CURLINFO_EFFECTIVE_URL, &followurl);
		*realurl = str_dup(followurl, 0);
		dbg_info("realurl: %s", *realurl);
	}
	long resCode;
	curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &resCode);
    if( resCode != 200L && resCode != 0 ) {
		dbg_error("getinfo return %ld: %s", resCode, www_errno_http(resCode));
		wwwerrno = WWW_ERROR_HTTP + resCode;
		return -1;
    }
	return 0;
}

void* www_download(const char* url, unsigned onlyheader, unsigned touts, char** realurl){
	dbg_info("'%s'", url);
	__wcc CURL* ch = www_curl_new(url);
	__free uint8_t* data = MANY(uint8_t, WWW_BUFFER_SIZE);
	curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, www_curl_buffer_recv);
	curl_easy_setopt(ch, CURLOPT_WRITEDATA, &data);
	if( onlyheader ){
		curl_easy_setopt(ch, CURLOPT_HEADER, 1L);
		curl_easy_setopt(ch, CURLOPT_NOBODY, 1L);
	}
	if( touts ) curl_easy_setopt(ch, CURLOPT_TIMEOUT, touts);
	if( www_curl_perform(ch, realurl) ) return NULL;
	return mem_borrowed(data);
}

void* www_download_retry(const char* url, unsigned onlyheader, unsigned touts, unsigned retry, unsigned retryms, char** realurl){
	void* ret = NULL;
	delay_t retrytime = retryms;
	while( retry-->0 && !(ret=www_download(url, onlyheader, touts, realurl)) ){
		dbg_warning("fail download %s tout: %u touts, retry: %u", url, touts, retry);
		if( retry ){
			delay_ms(retrytime);
			retrytime *= 2;
		}
	}
	return ret;
}

long www_ping(const char* url){
	//sorry for this but raw socket required a special privilege on Linux, probably I can add privilege in future
	__free char* server = NULL;
	unsigned proto = 0;
	if( !strncmp(url, "http://", 7) ) proto = 7;
	else if( !strncmp(url, "https://", 8) ) proto = 8;
	if( proto ){
		url += proto;
		const char* end = strchrnul(url, '/');
		server = str_dup(url, end-url);
		url = server;
	}

	__free char* cmd = str_printf("ping -c 1 %s 2>&1", url);
	FILE* f = popen(cmd, "r");
	if( !f ){
		dbg_error("on popen");
		return -1;
	}
	
	long ping = -1;
	char buf[4096];
	while( fgets(buf, 4096, f) != NULL ){
		if( ping == -1 ){
			const char* time = strstr(buf, "time=");
			if( !time ) continue;
			time += 5;
			errno = 0;
			char* endp = NULL;
			double ms = strtod(time, &endp);
			if( errno || !endp || *endp != ' ' ){
				dbg_error("wrong time in ping output %s", buf);
				break;
			}
			ping = ms * 1000.0;	
		}
		else if( buf[0] == '1' ){
			if( !strstr(buf, "1 received") ) ping = -2;
		}
	}
	int es = pclose(f);
	if( es == -1 ) return -1;
	if( ping < 0 ) return -1;
	if( !WIFEXITED(es) || WEXITSTATUS(es) != 0) return -1;
	return ping;
}

restapi_s* restapi_ctor(restapi_s* ra, const char* url, unsigned touts){
	dbg_info("'%s'", url);
	ra->url    = str_dup(url, 0);
	ra->call   = NULL;
	ra->curl   = www_curl_new(ra->url);
	curl_easy_setopt(ra->curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(ra->curl, CURLOPT_HTTPHEADER, HAPPJSON);	
	curl_easy_setopt(ra->curl, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(ra->curl, CURLOPT_NOPROGRESS, TRUE);
	curl_easy_setopt(ra->curl, CURLOPT_NOSIGNAL, 1L);	
	curl_easy_setopt(ra->curl, CURLOPT_VERBOSE, 0L);	
	curl_easy_setopt(ra->curl, CURLOPT_WRITEFUNCTION, www_curl_buffer_recv);
	if( touts ) curl_easy_setopt(ra->curl, CURLOPT_TIMEOUT, touts);
	return ra;
}

restret_s restapi_call(restapi_s* ra, const char* args){
	restret_s rr = {
		.header = MANY(char, WWW_BUFFER_SIZE),
		.body   = MANY(char, WWW_BUFFER_SIZE)
	};
	curl_easy_setopt(ra->curl, CURLOPT_HEADERDATA, &rr.header);
	curl_easy_setopt(ra->curl, CURLOPT_WRITEDATA,  &rr.body);

	mem_free(ra->call);
	ra->call = str_printf("%s%s",ra->url, args);
	curl_easy_setopt(ra->curl, CURLOPT_URL, ra->call);

	if( www_curl_perform(ra->curl, NULL) ){
		mem_free(rr.header);
		mem_free(rr.body);
		rr.header = NULL;
		rr.body   = NULL;
	}
	return rr;
}

restapi_s* restapi_dtor(restapi_s* ra){
	curl_easy_cleanup(ra->curl);
	mem_free(ra->url);
	mem_free(ra->call);
	return ra;
}


