#include <notstd/core.h>
#include <notstd/str.h>

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


void www_dtor(void* pw){
	www_s* w = pw;
	curl_easy_cleanup(w->curl);
	mem_free(w->resturl);
}

__private void set_url(www_s* w, const char* url){
	if( !url ) return;
	curl_easy_setopt(w->curl, CURLOPT_URL, url);
	if( !strncmp(url, "https", 5) )
		curl_easy_setopt(w->curl, CURLOPT_SSL_VERIFYPEER, 1L);
	else
		curl_easy_setopt(w->curl, CURLOPT_SSL_VERIFYPEER, 0L);
	dbg_info("set url: %s", url);
}

www_s* www_ctor(www_s* w, const char* url, unsigned retry, delay_t relaxms){
	w->progctx = NULL;
	w->resturl = NULL;
	w->error   = 0;
	w->relax   = relaxms < 1 ? relaxms: 100;
	w->retry   = retry < 1 ? 1: retry;
	w->curl    = curl_easy_init();
	if( !w->curl ) die("unable to create curl object");
	curl_easy_setopt(w->curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(w->curl, CURLOPT_VERBOSE, 0L);
	curl_easy_setopt(w->curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(w->curl, CURLOPT_TCP_KEEPALIVE, 1L);
	set_url(w, url);
	return w;
}

int www_perform(www_s* w){
	CURLcode res;
	unsigned retry = w->retry;
	int ret = 0;
	while( retry-->0 ){
		res = curl_easy_perform(w->curl);
		if( res != CURLE_OK && res != CURLE_FTP_COULDNT_RETR_FILE ){
			w->error = res;
			ret = -1;
			dbg_error("perform return %d: %s", res, curl_easy_strerror(res));
			if( retry ) delay_ms(w->relax);
			continue;
		}
		long resCode;
		curl_easy_getinfo(w->curl, CURLINFO_RESPONSE_CODE, &resCode);
		if( resCode != 200L && resCode != 0 ) {
			dbg_info("http error");
			w->error = WWW_ERROR_HTTP + resCode;
			ret = -1; continue;
			if( retry ) delay_ms(w->relax);
			dbg_error("getinfo return %ld: %s", resCode, www_errno_http(resCode));
			continue;
		}
		break;
	}
	return ret;
}

char* www_real_url(www_s* w){
	char* followurl = NULL;
	curl_easy_getinfo(w->curl, CURLINFO_EFFECTIVE_URL, &followurl);
	return followurl;
}

void www_timeout(www_s* w, unsigned sec){
	if( sec ) curl_easy_setopt(w->curl, CURLOPT_TIMEOUT, sec);
}

void www_header_body(www_s* w, int header, int body){
	curl_easy_setopt(w->curl, CURLOPT_HEADER, header);
	curl_easy_setopt(w->curl, CURLOPT_NOBODY, !body);
}

__private size_t www_curl_buffer_recv(void* ptr, size_t size, size_t nmemb, void* userctx){
	void* ctx = *(void**)userctx;
	const size_t sizein = size * nmemb;
	uint8_t* data = mem_upsize(ctx, sizein);
	memcpy(data + mem_header(data)->len, ptr, sizein);
	mem_header(data)->len += sizein;
	*(void**)userctx = data;
	return sizein;
}

void www_download_mem(www_s* w, uint8_t** out){
	if( !*out ) *out = MANY(uint8_t, WWW_BUFFER_SIZE);
	curl_easy_setopt(w->curl, CURLOPT_WRITEFUNCTION, www_curl_buffer_recv);
	curl_easy_setopt(w->curl, CURLOPT_WRITEDATA, out);
}

void www_download_file(www_s* w, FILE* f){
	curl_easy_setopt(w->curl, CURLOPT_WRITEFUNCTION, fwrite);
	curl_easy_setopt(w->curl, CURLOPT_WRITEDATA, f);
}

void www_download_custom(www_s* w, wwwDownload_f fn, void* ctx){
	curl_easy_setopt(w->curl, CURLOPT_WRITEFUNCTION, fn);
	curl_easy_setopt(w->curl, CURLOPT_WRITEDATA, ctx);
}

__private size_t progress_callback(void *userctx, curl_off_t dltotal, curl_off_t dlnow, __unused curl_off_t ultotal, __unused curl_off_t ulnow){
	www_s* w = userctx;
	curl_off_t bs = 0;
	double mib = 0;
	double perc = 0;
	if( dltotal > 0 && dlnow > 0 ) perc = (100.0 * dlnow / dltotal);
	if( curl_easy_getinfo(w->curl, CURLINFO_SPEED_DOWNLOAD_T, &bs) == CURLE_OK){
		if( bs > 0 ){
			mib = bs / (1024.0 * 1024.0);
			w->holdeta = (dltotal - dlnow) / bs;
		}
		else{
			mib = 0.0;
			w->holdeta = 0;
		}
	}
	w->prog(w->progctx, perc, mib, w->holdeta);
	return 0;
}

void www_progress(www_s* w, wwwprogress_f fn, void* ctx){
	w->prog    = fn;
	w->progctx = ctx;
	w->holdeta = 0;
	curl_easy_setopt(w->curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(w->curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
	curl_easy_setopt(w->curl, CURLOPT_XFERINFODATA, w);
}

void www_restapi(www_s* w, const char* url){
	w->resturl = str_dup(url, 0);
	curl_easy_setopt(w->curl, CURLOPT_HTTPHEADER, HAPPJSON);
	curl_easy_setopt(w->curl, CURLOPT_NOPROGRESS, TRUE);
	curl_easy_setopt(w->curl, CURLOPT_WRITEFUNCTION, www_curl_buffer_recv);
}

restret_s www_restapi_call(www_s* w, const char* args){
	restret_s rr = {
		.header = MANY(char, WWW_BUFFER_SIZE),
		.body   = MANY(char, WWW_BUFFER_SIZE)
	};
	curl_easy_setopt(w->curl, CURLOPT_HEADERDATA, &rr.header);
	curl_easy_setopt(w->curl, CURLOPT_WRITEDATA,  &rr.body);
	__free char* call = str_printf("%s%s",w->resturl, args);
	set_url(w, call);
	if( www_perform(w) ){
		mem_free(rr.header);
		mem_free(rr.body);
		rr.header = NULL;
		rr.body   = NULL;
		dbg_error("on call aur: %s", www_str_error(w->error));
	}
	return rr;
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

