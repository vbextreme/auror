#include <notstd/str.h>

#include <auror/inutility.h>

#define AUR_IMPLEMENT
#include <auror/aur.h>

//TODO
//	risolvere Conflicts
//		Provide
//		Replaces
//
//	URL


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
	dbg_info("<INFO REPLY>%s</INFO REPLY>", rr.body);
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

__private pkgInfo_s* pkginfo_rec_find(pkgInfo_s* p, const char* name){
	if( !strcmp(p->name, name) ) return p;
	pkgInfo_s* ret = NULL;
	ldforeach(p->deps, it){
		if( (ret=pkginfo_rec_find(it, name)) ) return ret;
	}
	return NULL;
}

__private pkgInfo_s* pkginfo_find(aurSync_s* sync, const char* name){
	pkgInfo_s* ret = NULL;
	ldforeach(sync->pkg, it){
		if( (ret=pkginfo_rec_find(it, name)) ) return ret;
	}
	return NULL;
}

__private void sync_move_dependency(pkgInfo_s* newparent, pkgInfo_s* pkg){
	if( pkg->parent ){
		if( pkg->parent->deps == pkg ){
			if( pkg->parent->deps->next == pkg ){
				pkg->parent->deps = NULL;
			}
			else{
				pkg->parent->deps = pkg->parent->deps->next;
			}
		}
	}
	ld_extract(pkg);
	pkg->parent = newparent;
	if( newparent->deps ){
		ld_before(newparent->deps, pkg);
	}
	else{
		newparent->deps = pkg;
	}
}

__private pkgInfo_s* sync_pkg_push(aurSync_s* sync, pkgInfo_s* parent, char* name, unsigned flags){
	dbg_info("new %s", name);
	pkgInfo_s* f = pkginfo_find(sync, name);
	if( f ){
		if( parent && !f->parent ){
			f->flags |= PKGINFO_FLAG_DEPENDENCY;
			dbg_info("  pkg %s is already present, move to %s->deps", name, f->name);
			sync_move_dependency(parent, f);
		}
		dbg_info("  already exists");
		return f;
	}

	pkgInfo_s* p = NEW(pkgInfo_s);
	ld_ctor(p);
	p->deps   = NULL;
	p->name   =  mem_borrowed(name);
	p->flags  = flags;
	p->parent = parent;
	if( parent ){
		if( !parent->deps ){
			dbg_info("  first parent deps %s", p->name);
			parent->deps = p;
		}
		else{
			dbg_info("  add parent deps %s", p->name);
			ld_before(parent->deps, p);
			//dbg_info("[-1]%s [0]%s [1]%s [2]%s", parent->deps->prev->name, parent->deps->name, parent->deps->next->name, parent->deps->next->next->name);
		}
	}
	else if( !sync->pkg ){
		dbg_info("  first package");
		sync->pkg = p;
	}
	else{
		dbg_info("  add package");
		ld_before(sync->pkg, p);
	}
	return p;
}

__private void unroll_dependency(aur_s* aur, pacman_s* pacman, aurSync_s* async, pkgInfo_s* parent, jvalue_s* jv, unsigned flags, int optional){
	if( jv->type != JV_ARRAY ) return;
	const unsigned count = mem_header(jv->a)->len;
	__free char** namedeps = MANY(char*, count + 1);
	unsigned add = 0;
	for( unsigned i = 0; i < count; ++i ){
		if( optional ){
			printf("package '%s' have optional dependency '%s', you want install? ", parent->name, jv->a[i].s);
			if( !readline_yesno() ) continue;
		}
		dbg_info("recursive resolve dependency %s", jv->a[i].s);
		namedeps[add++] = jv->a[i].s;
	}
	if( add ){
		mem_header(namedeps)->len = add;
		aur_dependency_resolve(aur, pacman, async, parent, namedeps, flags);
	}
}

void aur_dependency_resolve(aur_s* aur, pacman_s* pacman, aurSync_s* sync, pkgInfo_s* parent, char** name, unsigned flags){
	__free char** aurreq = MANY(char*, 4);
	mforeach(name, i){
		desc_s* localpkg = pacman_pkg_search(pacman, name[i]);
		if( localpkg && (localpkg->db->flags & DB_FLAG_UPSTREAM) ){
			if( !(localpkg->flags & PKG_FLAG_INSTALL) ){
				dbg_info("install '%s' with pacman", name[i]);
				sync_pkg_push(sync, parent, name[i], flags | DB_FLAG_UPSTREAM);
			}
		}
		else{
			dbg_info("add aur request '%s'", name[i]);
			if( flags & (PKGINFO_FLAG_DEPENDENCY | PKGINFO_FLAG_BUILD_DEPENDENCY) ){
				printf("aur package require %sdependency %s coming from aur\n", (flags & PKGINFO_FLAG_BUILD_DEPENDENCY ? "make": ""), name[i]);
				fputs("I proceed with the installation? ", stdout);
				if( !readline_yesno() ) die("terminated at the user's discretion");
			}
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
			jvalue_s* depends     = jvalue_property_type(&results->a[ia], JV_ARRAY , "Depends");
			jvalue_s* makedepends = jvalue_property_type(&results->a[ia], JV_ARRAY , "MakeDepends");
			jvalue_s* optdepends  = jvalue_property_type(&results->a[ia], JV_ARRAY , "OptDepends");
			jvalue_s* name        = jvalue_property_type(&results->a[ia], JV_STRING, "Name");
			jvalue_s* version     = jvalue_property_type(&results->a[ia], JV_STRING, "Version");
				
			desc_s* localpkg = pacman_pkg_search(pacman, name->s);
			pkgInfo_s* newp = NULL;

			if( !localpkg ){
				dbg_info("install '%s' with aur", name->s);
				newp = sync_pkg_push(sync, parent, name->s, flags);
			}
			else{
				char* localversion = desc_value_version(localpkg);
				if( (flags & SYNC_REINSTALL) || vercmp(version->s, localversion) > 0 ){
					dbg_info("upgrade '%s' with aur", name->s);
					newp = sync_pkg_push(sync, parent, name->s, flags);
				}
				else{
					continue;
				}
			}
			
			unroll_dependency(aur, pacman, sync, newp, depends, PKGINFO_FLAG_DEPENDENCY | flags, 0);
			unroll_dependency(aur, pacman, sync, newp, optdepends, PKGINFO_FLAG_DEPENDENCY | flags, 1);
			unroll_dependency(aur, pacman, sync, newp, makedepends, PKGINFO_FLAG_BUILD_DEPENDENCY | flags, 0);
		}
	}
	
	if( !sync->pkg ) die("up to date");
}
































