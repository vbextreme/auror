#ifndef __DATABASE_H__
#define __DATABASE_H__

#include <notstd/json.h>
#include <notstd/fzs.h>
#include <notstd/rbtree.h>
#include <notstd/list.h>

#include <auror/config.h>
#include <auror/status.h>

#define DESC_DEFAULT_SIZE    1024
#define BUFLOAD_DEFAULT_SIZE (4096*1024)

typedef struct desc     desc_s;
typedef struct database database_s;

#define DESC_FLAG_PROVIDE   0x0001
#define DESC_FLAG_REPLACE   0x0002
#define DESC_FLAG_MAKEPKG   0x0004
#define DESC_FLAG_REMOVED   0x0008
#define DESC_FLAG_INSTALLED 0x0010
#define DESC_FLAG_CROSS     0x0020
#define DESC_FLAG_V_LESS    0x0100
#define DESC_FLAG_V_EQUAL   0x0200
#define DESC_FLAG_V_GREATER 0x0400

#define DATABASE_FLAG_MULTIMEM 0x01

typedef struct pkgver{
	char*    name;
	char*    version;
	unsigned flags;
}pkgver_s;

struct desc{
	inherit_ld(struct desc);
	rbtNode_s node;
	database_s* db;
	desc_s* link;
	char* name;
	char* filename;
	char* base;
	char* desc;
	char* url;
	char* urlPath;
	char* arch;
	char* packager;
	char* validation;
	char* version;
	char* md5sum;
	char* sha256sum;
	char* pgpsig;
	char* maintainer;
	char** groups;
	char** replaces;
	char** provides;
	char** xdata;
	char** license;
	pkgver_s* depends;
	pkgver_s* makedepends;
	pkgver_s* checkdepends;
	pkgver_s* optdepends;
	pkgver_s* conflicts;
	unsigned long builddate;
	unsigned long installdate;
	unsigned long csize;
	unsigned long isize;
	unsigned long size;
	unsigned long reason;
	unsigned long flags;
	unsigned long numvotes;
	unsigned long outofdate;
	double popularity;
	int var;
};

struct database{
	void*               mem;
	configRepository_s* repo;
	rbtree_s            elements;
	unsigned            flags;
};

typedef struct arch{
	database_s** sync;
	database_s*  local;
	database_s*  aur;
}arch_s;

unsigned desc_parse_and_split_name_version(char* name, char** version);
desc_s* desc_unpack(database_s* db, unsigned flags, char* parse, size_t size, int checkmakepkg);
desc_s* desc_link(database_s* db, desc_s* link, char* name, char* version, unsigned flags);
desc_s* desc_unpack_json(database_s* db, unsigned flags, jvalue_s* pkgobj);
desc_s* desc_nonvirtual(desc_s* desc);
desc_s* desc_nonvirtual_dump(desc_s* desc);
bool desc_accept_version(desc_s* d, unsigned flags, const char* version);

database_s* database_ctor(database_s* db, configRepository_s* repo, unsigned flags);
desc_s* database_search_byname(database_s* db, const char* name);
desc_s* database_search_bydesc(database_s* db, desc_s* desc);
void database_insert(database_s* db, desc_s* desc);
void database_insert_provides(database_s* db, desc_s* desc);
void database_insert_replaces(database_s* db, desc_s* desc);
desc_s* database_sync_find(database_s** db, const char* name);
void database_sync(arch_s* arch, config_s* conf, status_s* status, int forcenodowanload);
void database_import_json(database_s* db, unsigned flags, jvalue_s* results);
fzs_s* database_match_fuzzy(fzs_s* vf, database_s* db, const char* name);



#endif
