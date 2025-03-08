
#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/request.h>
#include <notstd/list.h>

#include <auror/database.h>
#include <auror/archive.h>
#include <auror/inutility.h>

typedef enum {VAR_TYPE_STR, VAR_TYPE_ARR, VAR_TYPE_VER, VAR_TYPE_NUM, VAR_TYPE_DBL} vartype_e;

#define HPERF_DIE() die("unknown field %.*s", (int)(strchrnul(p, '%') - p), p)
//#define HPERF_DIE() die("unknown field %s", p)

#define HPERF_CASE(CH, STR, TYPE, EL) case CH: do{\
	ret = strlen(STR);\
	if( !strncmp(p, STR, ret) ){\
		*offset = offsetof(desc_s, EL);\
		*type = TYPE;\
		return ret;\
	}\
	HPERF_DIE();\
}while(0)

#define HPERF_DEFAULT() default: HPERF_DIE()

__private unsigned desc_hperf(const char* p, unsigned* offset, vartype_e* type){
	unsigned ret;
	switch( p[0] ){
		HPERF_DEFAULT();
		HPERF_CASE('A', "ARCH"      , VAR_TYPE_STR, arch);
		HPERF_CASE('F', "FILENAME"  , VAR_TYPE_STR, filename);
		HPERF_CASE('G', "GROUPS"    , VAR_TYPE_ARR, groups);
		HPERF_CASE('L', "LICENSE"   , VAR_TYPE_ARR, license);
		HPERF_CASE('X', "XDATA"     , VAR_TYPE_ARR, xdata);
		
		case 'B': switch( p[1] ){
			HPERF_CASE('A', "BASE"     , VAR_TYPE_STR, base);
			HPERF_CASE('U', "BUILDDATE", VAR_TYPE_NUM, builddate);
			HPERF_DEFAULT();
		}
		
		case 'C': switch( p[1] ){
			HPERF_CASE('H', "CHECKDEPENDS", VAR_TYPE_VER, checkdepends);
			HPERF_CASE('O', "CONFLICTS"   , VAR_TYPE_VER, conflicts);
			HPERF_CASE('S', "CSIZE"       , VAR_TYPE_NUM, csize);
			HPERF_DEFAULT();
		}
		
		case 'D': switch( p[2] ){
			HPERF_CASE('S', "DESC"   , VAR_TYPE_STR, desc);
			HPERF_CASE('P', "DEPENDS", VAR_TYPE_VER, depends);
			HPERF_DEFAULT();
		}
		
		case 'I': switch( p[1] ){
			HPERF_CASE('S', "ISIZE"      , VAR_TYPE_NUM, isize);
			HPERF_CASE('N', "INSTALLDATE", VAR_TYPE_NUM, installdate);
			HPERF_DEFAULT();
		}
	
		case 'M': switch( p[2] ){
			HPERF_CASE('5', "MD5SUM"     , VAR_TYPE_STR, md5sum);
			HPERF_CASE('K', "MAKEDEPENDS", VAR_TYPE_VER, makedepends);
			HPERF_CASE('I', "MAINTAINER" , VAR_TYPE_STR, maintainer);
			HPERF_DEFAULT();
		}
		
		case 'N': switch( p[1] ){
			HPERF_CASE('A', "NAME"    , VAR_TYPE_STR, name);
			HPERF_CASE('U', "NUMVOTES", VAR_TYPE_NUM, numvotes);
			HPERF_DEFAULT();
		}
		
		case 'O': switch( p[1] ){
		  	HPERF_CASE('P', "OPTDEPENDS", VAR_TYPE_VER, optdepends);
			HPERF_CASE('U', "OUTOFDATE" , VAR_TYPE_NUM, outofdate);
			HPERF_DEFAULT();
		}
		
		case 'P': switch( p[1] ){
			HPERF_CASE('A', "PACKAGER"  , VAR_TYPE_STR, packager);
			HPERF_CASE('G', "PGPSIG"    , VAR_TYPE_STR, pgpsig);
			HPERF_CASE('R', "PROVIDES"  , VAR_TYPE_ARR, provides);
			HPERF_CASE('O', "POPULARITY", VAR_TYPE_DBL, popularity);
			HPERF_DEFAULT();
		}
		
		case 'R': switch( p[3] ){
			HPERF_CASE('L', "REPLACES", VAR_TYPE_ARR, replaces);
			HPERF_CASE('S', "REASON"  , VAR_TYPE_NUM, reason);
			HPERF_DEFAULT();
		}
		
		case 'S': switch( p[1] ){
			HPERF_CASE('H', "SHA256SUM", VAR_TYPE_STR, sha256sum);
			HPERF_CASE('I', "SIZE"     , VAR_TYPE_NUM, size);
			HPERF_DEFAULT();
		}
		
		case 'U':
			if( p[3] == 'P' ){
				ret = strlen("URLPATH");
				if( !strncmp(p, "URLPATH", ret) ){
					*offset = offsetof(desc_s, urlPath);
					*type = VAR_TYPE_STR;
					return ret;
				}
			}
			else{
				ret = strlen("URL");
				if( !strncmp(p, "URL", ret) ){
					*offset = offsetof(desc_s, url);
					*type = VAR_TYPE_STR;
					return ret;
				}
			}
			HPERF_DIE();
		
		case 'V': switch( p[1] ){
			HPERF_CASE('A', "VALIDATION", VAR_TYPE_STR, validation);
			HPERF_CASE('E', "VERSION"   , VAR_TYPE_ARR, version);
			HPERF_DEFAULT();
		}
	}
}

__private char* desc_next(char* p, const char* endparse){
	while( p < endparse && *p == '\n') ++p;
	if( p >= endparse ) return NULL;
	if( *p++ != '%' ) die("aspected field name started with %%\n%s", --p);
	return p;
}

unsigned desc_parse_and_split_name_version(char* name, char** version){
	*version = NULL;
	char* op = strpbrk(name, "<=>");
	if( !op ) return 0;
	char* endname = op;
	unsigned flags = 0;
	
	while( *op ){
		if(      *op == '<' ) flags |= DESC_FLAG_V_LESS;
		else if( *op == '=' ) flags |= DESC_FLAG_V_EQUAL;
		else if( *op == '>' ) flags |= DESC_FLAG_V_GREATER;
		else break;
		++op;
	}
	*endname = 0;
	*version = op;
	return flags;
}

__private void desc_fill(desc_s* desc, vartype_e const type, unsigned const offset, char** parse, const char* e){
	char* p = *parse;
	char* endl = memchr(p, '\n', e-p);
	iassert( endl > p);
	if( !endl ) die("aspectd value");
	void* ptr = (void*)((uintptr_t)desc + offset);
	
	switch( type ){
		default: die("internal error, unaspected var type: %d", type);
		
		case VAR_TYPE_STR:{
			char** field = ptr;
			*field = p;
			*endl = 0;
			p = endl+1;
			if( *p != '\n' && *p != '%' ) die("internal error, desc parse invalid string");
		}break;
		
		case VAR_TYPE_ARR:{
			int err = -1;
			char** d = MANY(char*, 4);
			while( endl && p <= e ){
				unsigned id = mem_ipush(&d);
				d[id] = p;
				*endl = 0;
				p = endl+1;
				if( *p == '\n' || *p == '%' ){
					err = 0;
					break;
				}
				endl = memchr(p, '\n', e-p);
			}
			if( err ) die("internal error, desc parse invalid array");
			memcpy(ptr, &d, sizeof(char**));
		}break;
	
		case VAR_TYPE_VER:{
			int err = -1;
			pkgver_s* d = MANY(pkgver_s, 4);
			while( endl && p <= e ){
				*endl = 0;
				unsigned id = mem_ipush(&d);
				d[id].name = p;
				d[id].flags = desc_parse_and_split_name_version(p, &d[id].version);
				p = endl+1;
				if( *p == '\n' || *p == '%' ){
					err = 0;
					break;
				}
				endl = memchr(p, '\n', e-p);
			}
			if( err ) die("internal error, desc parse invalid pkgver");
			memcpy(ptr, &d, sizeof(desc_s*));
		}break;
		
		case VAR_TYPE_NUM:{
			errno = 0;
			char* en = NULL;
			*((unsigned long*)ptr) = strtoul(p, &en, 10);
			if( errno || !en || en != endl ) die("internal error, desc parse invalid number");
			p = endl;
		}break;
	
		case VAR_TYPE_DBL:{
			errno = 0;
			char* en = NULL;
			*((double*)ptr) = strtod(p, &en);
			if( errno || !en || en != endl ) die("internal error, desc parse invalid number");
			p = endl;
		}break;
	}
	*parse = p;
}

__private desc_s* desc_new(database_s* db, unsigned flags){
	desc_s* desc = NEW(desc_s);
	mem_zero(desc);
	ld_ctor(desc);
	desc->db    = db;
	desc->flags = flags;
	desc->var   = -1;
	rbtNode_ctor(&desc->node, desc);
	return desc;
}

desc_s* desc_unpack(database_s* db, unsigned flags, char* parse, size_t size, int checkmakepkg){
	desc_s* desc = desc_new(db, flags);
	const char* e = parse+size;
	while( (parse=desc_next(parse, e)) ){
		unsigned offset;
		vartype_e type;	
		parse += desc_hperf(parse, &offset, &type);
		if( *parse++ != '%' ) die("unterminated field, aspected %%");
		if( *parse++ != '\n') die("aspected new line after %%");
		desc_fill(desc, type, offset, &parse, e);
	}
	if( !desc->name ) die("desc not have a valid name");
	if( checkmakepkg && desc->validation && !strcmp(desc->validation, "none") && desc->packager && !strcmp(desc->packager, "Unknown Packager") ){
		desc->flags |= DESC_FLAG_MAKEPKG;
	}
	return desc;
}

desc_s* desc_link(database_s* db, desc_s* link, char* name, char* version, unsigned flags){
	desc_s* vrt  = desc_new(db, flags);
	vrt->name    = name;
	vrt->version = version;
	vrt->link    = link;
	return vrt;
}


__private void cast_jvaue_tag(desc_s* desc, jvalue_s* jv, unsigned offset, vartype_e type, const char* field){
	void* ptr = (void*)((uintptr_t)desc + offset);
	
	switch( jv->type ){
		default: case JV_OBJECT: die("internal error, report this issue, json %s is %s but not supported", field, jvalue_type_to_name(jv->type)); break;
		
		case JV_STRING:
			switch( type ){
				default: die("internal error, report this issue, unaspected var type: %d", type);
				case VAR_TYPE_DBL: case VAR_TYPE_NUM: die("internal error, report this issue, desc %s unsapected num", field);
				case VAR_TYPE_STR:{
					char** set = ptr;
					*set = mem_borrowed(jv->s);
				}
				break;
				case VAR_TYPE_ARR:{
					char** d = MANY(char*, 1);
					*mem_len(d) = 1;
					d[0] =  mem_borrowed(jv->s);
					memcpy(ptr, &d, sizeof(char**));
				}
				break;
				case VAR_TYPE_VER:{
					pkgver_s* d = MANY(pkgver_s, 1);
					*mem_len(d) = 1;
					d[0].name =  mem_borrowed(jv->s);
					d[0].flags = desc_parse_and_split_name_version(jv->s, &d[0].version);
					memcpy(ptr, &d, sizeof(pkgver_s*));
				}
				break;
			}
		break;
			
		case JV_ARRAY:
			switch( type ){
				default: die("internal error, report this issue, unaspected var type: %d", type);
				case VAR_TYPE_DBL: case VAR_TYPE_NUM: die("internal error, report this issue, desc %s unaspected num", field);
				case VAR_TYPE_STR:{
					if( *mem_len(jv->a) != 1 || jv->a[0].type != JV_STRING ) die("internal error, desc %s aspected string but give array", field);
					char** set = ptr;
					*set = mem_borrowed(jv->a[0].s);
				}
				break;
				case VAR_TYPE_ARR:{
					unsigned const count = *mem_len(jv->a);
					char** d = MANY(char*, count);
					*mem_len(d) = count;
					for( unsigned i = 0; i < count; ++i ){
						if( jv->a[i].type != JV_STRING ) die("internal error, report this issue, desc %s aspected array of string but give %s", field, jvalue_type_to_name(jv->a[i].type));
						d[0] =  mem_borrowed(jv->a[i].s);
					}
					memcpy(ptr, &d, sizeof(char**));
				}
				break;
				case VAR_TYPE_VER:{
					unsigned const count = *mem_len(jv->a);
					pkgver_s* d = MANY(pkgver_s, count);
					*mem_len(d) = count;
					for( unsigned i = 0; i < count; ++i ){
						if( jv->a[i].type != JV_STRING ) die("internal error, report this issue, desc %s aspected array of string but give %s", field, jvalue_type_to_name(jv->a[i].type));
						d[i].name =  mem_borrowed(jv->a[i].s);
						d[i].flags = desc_parse_and_split_name_version(jv->a[i].s, &d[i].version);
					}
					memcpy(ptr, &d, sizeof(pkgver_s*));
				}
				break;
			}
		
		__fallthrough;
		case JV_FLOAT:
		case JV_UNUM:
		case JV_NUM:{
			switch( type ){
				default: die("internal error, report this issue, unaspected var type: %d", type);
				case VAR_TYPE_STR: die("internal error, report this issue, desc %s aspected num but give string", field);
				case VAR_TYPE_ARR: die("internal error, report this issue, desc %s unsupported array of num", field);
				case VAR_TYPE_NUM:
					*((unsigned long*)ptr) = jv->type == JV_NUM ? jv->u: jv->f;
				break;
				case VAR_TYPE_DBL:
					*((double*)ptr) = jv->type == JV_NUM ? jv->u: jv->f;
				break;
			}
		}
		break;
		
		case JV_NULL:{
			switch( type ){
				default: die("internal error, report this issue, unaspected var type: %d", type);
				case VAR_TYPE_ARR: die("internal error, report this issue, desc %s unsupported array when jv_null", field);
				case VAR_TYPE_STR:{
					char** str = ptr;
					*str = NEW(char);
					**str = 0;
				}
				break;
				case VAR_TYPE_NUM:
					*((unsigned long*)ptr) = 0;
				break;
				case VAR_TYPE_DBL:
					*((double*)ptr) = 0;
				break;
			}
		}
		
		break;
	}
}

desc_s* desc_unpack_json(database_s* db, unsigned flags, jvalue_s* pkgobj){
	__private char* fields[] = { 
		"Name", "PackageBase", "Version", "Description", "URL", "Maintainer", "URLPath",
		"NumVotes", "Popularity", "OutOfDate",
		"Depends", "MakeDepends", "License", "Provides", "Replaces", "Conflicts"
	};
	__private char* translate[] = {
		"NAME", "BASE", "VERSION", "DESC", "URL", "MAINTAINER", "URLPATH", 
		"NUMVOTES", "POPULARITY", "OUTOFDATE",
		"DEPENDS", "MAKEDEPENDS", "LICENSE", "PROVIDES", "REPLACES", "CONFLICTS"
	};
	
	desc_s* desc = desc_new(db, flags);
	for( unsigned j = 0; j < sizeof_vector(fields); ++j ){
		unsigned offset;
		vartype_e type;
		unsigned len = desc_hperf(translate[j], &offset, &type);
		if( translate[j][len] != 0 ) die("internal cast translate aur error, unterminated field");
		jvalue_s* prp = jvalue_property(pkgobj, fields[j]);
		if( prp->type == JV_ERR && j != 0 ) continue;
		cast_jvaue_tag(desc, prp, offset, type, fields[j]);
	}
	if( !desc->name ) die("invalid json package");
	return desc;
}

desc_s* desc_nonvirtual(desc_s* desc){
	if( !desc ){
		dbg_warning("caller NULL");
		return NULL;
	}
	ldforeach(desc, it){
		if( it->flags & DESC_FLAG_PROVIDE ){
		//	dbg_info("%s is provide of %s, skip", it->name, it->link->name);
			continue;
		}
		if( it->flags & DESC_FLAG_REPLACE ){
		//	dbg_info("%s is provide of %s, skip", it->name, it->link->name);
			continue;
		}
		//dbg_info("%s is non virtual", it->name);
		return it;
	}
	//dbg_info("all desc are virtual");
	return NULL;
}

desc_s* desc_nonvirtual_dump(desc_s* desc){
	if( !desc ){
		dbg_warning("caller NULL");
		return NULL;
	}
	ldforeach(desc, it){
		dbg_info("%s %lX", it->name, it->flags);
	}
	return NULL;
}

bool desc_accept_version(desc_s* d, unsigned flags, const char* version){
	if( !d->version ){
		dbg_warning("desc %s not have any version", d->name);
		return true;
	}
	if( !version || !*version ){
		dbg_warning("not setted version for compare desc %s", d->name);
		return true;
	}
	int cmp = vercmp(d->version, version);
	if( cmp < 0 ){
		return flags & DESC_FLAG_V_LESS ? true : false;
	}
	else if( cmp == 0 ){
		return flags & DESC_FLAG_V_EQUAL ? true : false;
	}
	else{
		return flags & DESC_FLAG_V_GREATER ? true : false;
	}
}
















