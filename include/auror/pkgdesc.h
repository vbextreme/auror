#ifndef __PKGDESC_PARSER_H__
#define __PKGDESC_PARSER_H__

#include <notstd/core.h>
#include <notstd/fzs.h>
#include <notstd/json.h>
#include <notstd/rbtree.h>

#include <limits.h>
#include <stdint.h>

typedef struct ddatabase ddatabase_s;

typedef struct descTag{
	rbtNode_s node;
	char*     name;
	char**    value;
}descTag_s;

typedef struct desc{
	ddatabase_s* db;	
	rbtree_s     tags;
	unsigned     flags;
}desc_s;

struct ddatabase{
	char*    name;
	char*    location;
	char*    url;
	unsigned flags;
	desc_s*  desc;
	char**   err;
};

#define __desc __cleanup(desc_cleanup)

int desc_parse(desc_s* desc, const char* data, size_t len);
void desc_dump(desc_s* desc);
char** desc_value(desc_s* desc, const char* tagName);
char* desc_value_name(desc_s* desc);
char** desc_value_description(desc_s* desc);
char* desc_value_version(desc_s* desc);
ddatabase_s* database_new(char* name, char* location, char* url);
desc_s* database_add(ddatabase_s* db, void* descfile, unsigned size, unsigned flags);
void database_flush(ddatabase_s* db);
desc_s* database_import(ddatabase_s* db, desc_s* desc);
void database_import_json(ddatabase_s* db, jvalue_s* results);
void database_delete_byname(ddatabase_s* db, const char* find);
desc_s* database_search_byname(ddatabase_s* db, const char* name);
ddatabase_s* database_unpack(char* name, char* location, char* url, void* dbTarGz, unsigned flags);

fzs_s* database_match_fuzzy(fzs_s* vf, ddatabase_s* db, const char* name);

#endif
