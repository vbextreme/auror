#include <notstd/str.h>

#include <auror/inutility.h>

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

__private jvalue_s* aur_search_call(aur_s* aur, const char* search, const char* field){
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

__private void sync_pkg_push(aurSync_s* sync, char* name, unsigned flags){
	pkgInfo_s* ref;
	sync->pkg = mem_upsize(sync->pkg, 1);
	ref = &sync->pkg[mem_header(sync->pkg)->len++];
	ref->name  = mem_borrowed(name);
	ref->flags = flags;
}

void aur_sync(aur_s* aur, pacman_s* pacman, aurSync_s* sync, char** name, unsigned flags){
	__free char** aurreq = MANY(char*, 4);
	mforeach(name, i){
		desc_s* localpkg = pacman_pkg_search(pacman, name[i]);
		if( localpkg && (localpkg->db->flags & DB_FLAG_UPSTREAM) ){
			if( !(localpkg->flags & PKG_FLAG_INSTALL) ){
				sync_pkg_push(sync, name[i], DB_FLAG_UPSTREAM);
			}
		}
		else{
			aurreq = mem_upsize(aurreq, 1);
			aurreq[mem_header(aurreq)->len++] = name[i];
		}
	}
	
	if( mem_header(aurreq)->len ){
		__free jvalue_s* req = aur_info_call(aur, aurreq);
		if( !req ) die("not find packages");
		aur_check_error(req);
		
		jvalue_s* results = jvalue_property_type(req, JV_ARRAY, "results");
		mforeach(results->a, ia){
			jvalue_s* depends =  jvalue_property_type(&results->a[ia], JV_ARRAY, "Depends");
			jvalue_s* makedepends =  jvalue_property_type(&results->a[ia], JV_ARRAY, "MakeDepends");
			jvalue_s* name =  jvalue_property_type(&results->a[ia], JV_STRING, "Name");
			jvalue_s* version = jvalue_property_type(&results->a[ia], JV_STRING, "Version");
		
			desc_s* localpkg = pacman_pkg_search(pacman, name->s);
			if( !localpkg ){
				sync_pkg_push(sync, name->s, 0);
			}
			else{
				char* localversion = desc_value_version(localpkg);
				if( (flags & SYNC_REINSTALL) || vercmp(version->s, localversion) > 0 ){
					sync_pkg_push(sync, name->s, flags & (~SYNC_REINSTALL));
				}
				else{
					continue;
				}
			}
			
			__free char** namedeps = MANY(char*, mem_header(depends->a)->len);
			mforeach(depends->a, id){
				namedeps[id] = depends->a[id].s;
			}
			mem_header(namedeps)->len = mem_header(depends->a)->len;
			aur_sync(aur, pacman, sync, namedeps, PKGINFO_FLAG_DEPENDENCY | flags);
			
			__free char** makedeps = MANY(char*, mem_header(makedepends->a)->len);
			mforeach(makedepends->a, id){
				makedeps[id] = makedepends->a[id].s;
			}
			mem_header(makedeps)->len = mem_header(makedepends->a)->len;
			aur_sync(aur, pacman, sync, makedeps, PKGINFO_FLAG_BUILD_DEPENDENCY | flags);
		}
	}
	
	if( !mem_header(sync->pkg)->len ){
		die("up to date");
	}
	
	unsigned totaldepsaur = 0;
	mforeach(sync->pkg, i){
		if( (sync->pkg->flags & PKGINFO_FLAG_DEPENDENCY) && !(sync->pkg->flags & DB_FLAG_UPSTREAM) ){
			++totaldepsaur;
		}
	}
	
	if( totaldepsaur ){
		puts("to proceed I will also have to install these dependencies coming from aur:");
		fputs("    ", stdout);
		mforeach(sync->pkg, i){
			if( (sync->pkg->flags & PKGINFO_FLAG_DEPENDENCY) && !(sync->pkg->flags & DB_FLAG_UPSTREAM) ){
				printf("%s ", sync->pkg[i].name);
				if( !((i+1) % 8) ){
					putchar('\n');
					fputs("    ", stdout);
				}
			}
		}
		putchar('\n');
		fputs("I proceed with the installation? ", stdout);
		if( !readline_yesno() ) die("terminated at the user's discretion");
	}
	
	mforeach(sync->pkg, i){
		printf("[%s/%s] ", (sync->pkg[i].flags & DB_FLAG_UPSTREAM ? "upstream": "aur"),  sync->pkg[i].name);
		if( sync->pkg[i].flags & PKGINFO_FLAG_DEPENDENCY ){
			printf("(dependency)");
		}
		if( sync->pkg[i].flags & PKGINFO_FLAG_DEPENDENCY ){
			printf("(make dependency)");
		}
		puts("");
	}
}



































