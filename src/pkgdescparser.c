#include <notstd/str.h>

#include <auror/inutility.h>
#include <auror/archive.h>
#include <unistd.h>
#define PKGDESC_IMPLEMENT
#include <auror/pkgdesc.h>

typedef struct substring{
	const char* str;
	unsigned len;
}substring_s;

__private const char* skip_hn(const char* data, const char* end){
	while( data < end && (*data == ' ' || *data == '\t' || *data == '\n') ) ++data;
	return data;
}

__private const char* tag_end(const char* data, const char* end){
	while( data < end && *data != '%') ++data;
	return data;
}

__private const char* end_line(const char* data, const char* end){
	while( data < end && *data != '\n' ) ++data;
	return data;
}

__private int desc_tag_tag_cmp(const void* a, const void* b){
	descTag_s* tagA = (descTag_s*)a;
	descTag_s* tagB = (descTag_s*)b;
	return strcmp(tagA->name, tagB->name);
}

__private descTag_s* tag_new(void){
	descTag_s* tag = NEW(descTag_s);
	rbtNode_ctor(&tag->node, tag);
	tag->name  = NULL;
	tag->value = MANY(char*, 1);
	return tag;
}

__private void desc_tag_add(desc_s* desc, descTag_s* tag){
	rbtree_insert(&desc->tags, &tag->node);
}

int desc_parse(desc_s* desc, const char* data, size_t len){
	const char* end = data + len;

	rbtree_ctor(&desc->tags, desc_tag_tag_cmp);
	desc->flags = 0;

	while( data < end ){
		data = skip_hn(data, end);
		if( *data++ != '%'){
			errno = EIO;
			dbg_error("not find begin %%");
			return -1;	
		}
		const char* starttag = data;
		const char* endtag   = tag_end(data, end);
		if( *endtag != '%' || endtag - data < 1 ){
			errno = EIO;
			dbg_error("not find end %%");
			return -1;
		}
		__free char* tagName = str_dup(starttag, endtag-starttag);
		descTag_s* tag = tag_new();
		tag->name  = mem_borrowed(tagName);
		tag->value = MANY(char*, 1);
		data = skip_hn(endtag + 1, end);
		while( *data != '%' && data < end){
			const char* el = end_line(data, end);
			if( el - data < 1 ) break;
			tag->value = mem_upsize(tag->value, 1);
			tag->value[mem_header(tag->value)->len++] = str_dup(data, el - data);
			data = skip_hn(el + 1, end);
		}
		desc_tag_add(desc, tag);
	}
	
	return 0;
}

void desc_dump(desc_s* desc){
	printf("database: %s flags: 0x%X\n", desc->db->name, desc->flags);
	rbtreeit_s it;
	rbtreeit_ctor(&it, &desc->tags, 0);
	descTag_s* tag;
	while( (tag=rbtree_iterate_inorder(&it)) ){
		printf("%%%s%%\n", tag->name);
		mforeach(tag->value, j){
			printf("%s\n", tag->value[j]);
		}
	}
	rbtreeit_dtor(&it);
}

char** desc_value(desc_s* desc, const char* tagName){
	descTag_s fitag = { .name = (char*)tagName };
	descTag_s* tag = rbtree_search(&desc->tags, &fitag);
	return tag ? tag->value: desc->db->err;
}

char* desc_value_name(desc_s* desc){
	descTag_s fitag = { .name = "NAME" };
	descTag_s* tag = rbtree_search(&desc->tags, &fitag);
	if( !tag ){
		fitag.name = "Name";
		tag = rbtree_search(&desc->tags, &fitag);
	}
	return tag ? tag->value[0] : "";
}

char** desc_value_description(desc_s* desc){
	descTag_s fitag = { .name = "DESC" };
	descTag_s* tag = rbtree_search(&desc->tags, &fitag);
	if( !tag ){
		fitag.name = "Description";
		tag = rbtree_search(&desc->tags, &fitag);
	}
	return tag ? tag->value : desc->db->err;
}

__private int desc_desc_desc_name_cmp(const void* a, const void* b){
	char* nameA = desc_value((void*)a, "NAME")[0];
	if( !*nameA ) nameA = desc_value((void*)a, "Name")[0];
	char* nameB = desc_value((void*)b, "NAME")[0];
	if( !*nameB ) nameB = desc_value((void*)b, "Name")[0];
	return strcmp(nameA, nameB);
}

__private int desc_desc_str_name_cmp(const void* a, const void* b){
	char* nameB = desc_value((void*)b, "NAME")[0];
	if( !*nameB ) nameB = desc_value((void*)b, "Name")[0];
	return strcmp(a, nameB);
}

__private void database_value_free(char** value){
	mforeach(value, i){
		mem_free(value[i]);
	}
	mem_free(value);
}

__private void database_tag_free(descTag_s* tag){
	mem_free(tag->name);
	database_value_free(tag->value);
	mem_free(tag);
}

__private void tag_clean(void* node){
	rbtNode_s* n = node;
	database_tag_free(n->data);
}

__private void database_vdesc_free(desc_s* desc){
	mforeach(desc, i){
		rbtree_dtor_cbk(&desc[i].tags, tag_clean);
	}
	mem_free(desc);
}

__private void database_cleanup(void* pdb){
	ddatabase_s* db = pdb;
	mem_free(db->name);
	mem_free(db->location);
	mem_free(db->url);
	database_vdesc_free(db->desc);
}

ddatabase_s* database_new(char* name, char* location, char* url){
	dbg_info("%s", name);
	ddatabase_s* db = NEW(ddatabase_s);
	db->name     = name;
	db->location = location;
	db->url      = url;
	db->desc     = MANY(desc_s, 16);
	db->err      = MANY(char*, 1);
	db->err[0]   = str_dup("", 0);
	++mem_header(db->err)->len;
	mem_header(db)->cleanup = database_cleanup;
	return db;
}

desc_s* database_add(ddatabase_s* db, void* descfile, unsigned size, unsigned flags){
	db->desc = mem_upsize(db->desc, 1);
	desc_s* ret = &db->desc[mem_header(db->desc)->len++];
	ret->db = db;
	if( desc_parse(ret, descfile, size) ){
		--mem_header(db->desc)->len;
		return NULL;
	}
	ret->flags = flags;
	return ret;
}

void database_flush(ddatabase_s* db){
	//dbg_info("");
	mem_qsort(db->desc, desc_desc_desc_name_cmp);
}

desc_s* database_import(ddatabase_s* db, desc_s* desc){
	db->desc = mem_upsize(db->desc, 1);
	desc_s* ret = &db->desc[mem_header(db->desc)->len++];
	ret->db    = mem_borrowed(db);
	ret->flags = desc->flags;
	rbtree_ctor(&ret->tags, desc_tag_tag_cmp);
	rbtreeit_s it;
	rbtreeit_ctor(&it, &ret->tags, 0);
	descTag_s* stag;
	while( (stag=rbtree_iterate_inorder(&it)) ){
		descTag_s* dtag = tag_new();
		dtag->name  = mem_borrowed(stag->name);		
		dtag->value = mem_borrowed(stag->value);
		mforeach(stag->value, i){
			mem_borrowed(stag->value[i]);
		}
	}
	rbtreeit_dtor(&it);
	return ret;
}

__private void cast_jvaue_tag_value(descTag_s* tag, jvalue_s* jv, const char* field){
	switch( jv->type ){
		default: die("internal error, report this issue, field %s is %d but not supported", field, jv->type); break;
		
		case JV_ERR: 
			tag->value[mem_header(tag->value)->len++] = str_dup("", 0);
		break;
		
		case JV_NULL: 
			tag->value[mem_header(tag->value)->len++] = str_dup("", 0);
		break;
		
		case JV_BOOLEAN:
			tag->value[mem_header(tag->value)->len++] = str_dup(jv->b ? "true": "false", 0);
		break;
		
		case JV_NUM:
			tag->value[mem_header(tag->value)->len++] = str_printf("%ld", jv->n);
		break;

		case JV_FLOAT:
			tag->value[mem_header(tag->value)->len++] = str_printf("%f", jv->f);
		break;
		
		case JV_STRING:
			tag->value[mem_header(tag->value)->len++] = mem_borrowed(jv->s);
		break;
	}
}

__private void cast_jvaue_tag(desc_s* desc, jvalue_s* jv, const char* field){
	descTag_s* tag = tag_new();
	tag->name  = str_dup(field, 0);
	desc_tag_add(desc, tag);

	switch( jv->type ){
		case JV_OBJECT: die("internal error, report this issue, field %s is object but not supported", field); break;
	
		default: cast_jvaue_tag_value(tag, jv, field); break;
	
		case JV_ARRAY:
			if( mem_header(jv->a)->len ){
				dbg_info("JV_ARRAY with elements");
				mforeach(jv->a, i){
					tag->value = mem_upsize(tag->value, 1);
					cast_jvaue_tag_value(tag, &jv->a[i], field);
				}
			}
			else{
				dbg_info("JV_ARRAY empty");
				jvalue_s tmp = {
					.type = JV_NULL,
					.p = NULL
				};
				cast_jvaue_tag_value(tag, &tmp, field);
			}
		break;
	}
}

void database_import_json(ddatabase_s* db, jvalue_s* results){
	__private char* fields[] = { 
		"Name", "PackageBase", "Version", "Description", "URL", "Maintainer", "URLPath",
		"ID", "PackageBaseID", "NumVotes", "OutOfDate", "FirstSubmitted", "LastModified",
		"Depends", "MakeDepends", "License", "Keywords"
	};
	
	mforeach(results->a, i){
		db->desc = mem_upsize(db->desc, 1);
		desc_s* desc = &db->desc[mem_header(db->desc)->len++];
		desc->db = db;
		desc->flags = 0;
		rbtree_ctor(&desc->tags, desc_tag_tag_cmp);
		for( unsigned j = 0; j < sizeof_vector(fields); ++j ){
			jvalue_s* prp = jvalue_property(&results->a[i], fields[j]);
			cast_jvaue_tag(desc, prp, fields[j]);
		}
	}
}

__private descTag_s* desc_tag_name(desc_s* desc){
	descTag_s fitag = { .name = "NAME" };
	descTag_s* tag = rbtree_search(&desc->tags, &fitag);
	if( !tag ){
		fitag.name = "Name";
		tag = rbtree_search(&desc->tags, &fitag);
	}
	return tag;
}

void database_delete_byname(ddatabase_s* db, const char* find){
	mforeach(db->desc, i){
		descTag_s* tag = desc_tag_name(&db->desc[i]);
		if( !tag ) continue;
		if( strcmp(tag->name, find) ) continue;
		rbtree_remove(&db->desc[i].tags, &tag->node);
		database_tag_free(tag);
		db->desc = mem_delete(db->desc, i, 1);
		break;
	}
}

desc_s* database_search_byname(ddatabase_s* db, const char* name){
	return mem_bsearch(db->desc, (char*)name, desc_desc_str_name_cmp);
}

ddatabase_s* database_unpack(char* name, char* location, char* url, void* dbTarGz, unsigned flags){
	dbg_info("unpack %s", name);
	__free void* dbTar   = gzip_decompress(dbTarGz);
	if( !dbTar ){
		dbg_error("%m ungzip");	
		return NULL;
	}
	
	__tar tar_s tar;
	tar_mopen(&tar, dbTar);
	tarent_s* ent;
	ddatabase_s* db  = database_new(name, location, url);

	while( (ent=tar_next(&tar)) ){
		if( ent->type == TAR_FILE ){
			if( !database_add(db, ent->data, ent->size, flags) ){
				mem_free(ent);
				mem_free(db);
				return NULL;
			}
			mem_free(ent);
		}
	}
	if( (errno=tar_errno(&tar)) ){
		mem_free(db);
		return NULL;
	}
	database_flush(db);
	return db;
}

fzs_s* database_match_fuzzy(fzs_s* vf, ddatabase_s* db, const char* name){
	unsigned const count = mem_header(db->desc)->len;
	for( unsigned i = 0; i < count; ++i ){
		char*  dn = desc_value_name(&db->desc[i]);
		
		if( strstr(dn, name) ){
			vf = mem_upsize(vf, 1);
			fzs_s* e = &vf[mem_header(vf)->len++];
			e->ctx = &db->desc[i];
			e->len = 0;
			e->str = dn;
		}
		else{
			char** ds = desc_value_description(&db->desc[i]);
			mforeach(ds, j){
				if( strstr(ds[j], name) ){
					vf = mem_upsize(vf, 1);
					fzs_s* e = &vf[mem_header(vf)->len++];
					e->ctx = &db->desc[i];
					e->len = 0;
					e->str = ds[j];
					break;
				}
			}
		}
	}
	return vf;
}





