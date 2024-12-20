#include <notstd/str.h>

#define AUR_IMPLEMENT
#include <auror/aur.h>

aur_s* aur_ctor(aur_s* aur){
	restapi_ctor(&aur->ra, "https://aur.archlinux.org/rpc/v5", 0);
	return aur;
}

aur_s* aur_dtor(aur_s* aur){
	restapi_dtor(&aur->ra);
	return aur;
}

jvalue_s* aur_search_call(aur_s* aur, const char* search, const char* field){
	__free char* escsearch = url_escape(search);
	__free char* method = str_printf("/search/%s?by=%s", escsearch, field);
	restret_s rr = restapi_call(&aur->ra, method);
	if( rr.header == NULL ){
		dbg_error("aur reply null");
		return NULL;
	}

	rr.body = mem_nullterm(rr.body);
	dbg_info("<SEARCH REPLY>%s</SEARCH REPLY>", rr.body);
	jvalue_s* out = json_decode(rr.body, NULL, NULL);
	dbg_info("success decode");
	if( !out ){
		mem_free(rr.body);
		mem_free(rr.header);	
		return NULL;
	}

	mem_free(rr.body);
	mem_free(rr.header);	
	return out;
}

jvalue_s* aur_info_call(aur_s* aur, char** pkg){
	char inc = '?';
	__free char* method = MANY(char, 128);
	strcpy(method, "/info");
	mem_header(method)->len = 5;
	mforeach(pkg, i){
		__free char* esc = url_escape(pkg[i]);
		method = mem_upsize(method, mem_header(esc)->len + 7);
		method[mem_header(method)->len++] = inc;
		strcpy(&method[mem_header(method)->len], "arg[]=");
		mem_header(method)->len += 6;
		strcpy(&method[mem_header(method)->len], esc);
		mem_header(method)->len += mem_header(esc)->len;
		inc = '&';
	}
	method = mem_upsize(method, 1);
	method[mem_header(method)->len] = 0;
	
	restret_s rr = restapi_call(&aur->ra, method);
	if( rr.header == NULL ){
		dbg_error("aur reply null");
		return NULL;
	}
	
	rr.body = mem_nullterm(rr.body);
	jvalue_s* out = json_decode(rr.body, NULL, NULL);
	if( !out ){
		mem_free(rr.body);
		mem_free(rr.header);
		return NULL;
	}
	
	mem_free(rr.body);
	mem_free(rr.header);
	return out;
}

void aur_check_error(jvalue_s* jv){
	jvalue_s* v = jvalue_property_type(jv, JV_STRING, "type");
	if( v->type == JV_ERR || !strcmp(v->s, "error") ){
		if(v->type == JV_ERR ) die("rpc aur not respond with type");
		die("rpc aur error: %s", jvalue_property_type(jv, JV_STRING, "error")->s );
	}
}

ddatabase_s* aur_search(aur_s* aur, const char* name, fzs_s** matchs){
	ddatabase_s* db = database_new(
		str_dup(AUR_DB_NAME, 0),
		str_dup(AUR_URL, 0), 
		str_dup("", 0)
	);

	jvalue_s* jret = aur_search_call(aur, name, "name-desc");
	if( !jret ) return db;
	aur_check_error(jret);
	
	jvalue_s* results = jvalue_property_type(jret, JV_ARRAY, "results");
	if( results->type != JV_ARRAY ) die("internal error: report this issue,rpc aur no results array in reply but %d", results->type);
	
	database_import_json(db, results);
	database_flush(db);
	*matchs = database_match_fuzzy(*matchs, db, name);
	
	return db;
}

ddatabase_s* aur_search_test(jvalue_s* jret, const char* name, fzs_s** matchs){
	ddatabase_s* db = database_new(
		str_dup(AUR_DB_NAME, 0),
		str_dup(AUR_URL, 0), 
		str_dup("", 0)
	);

	aur_check_error(jret);
	
	jvalue_s* results = jvalue_property_type(jret, JV_ARRAY, "results");
	if( results->type != JV_ARRAY ) die("internal error: report this issue,rpc aur no results array in reply but %d", results->type);
	
	database_import_json(db, results);
	database_flush(db);
	*matchs = database_match_fuzzy(*matchs, db, name);
	
	return db;
}





