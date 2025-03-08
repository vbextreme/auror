//#undef DBG_ENABLE
#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/request.h>
#include <notstd/list.h>

#include <auror/database.h>
#include <auror/archive.h>
#include <auror/download.h>
#include <auror/www.h>
#include <auror/inutility.h>
#include <auror/jobs.h>
#include <auror/status.h>

#include <dirent.h>

typedef struct jobArg{
	config_s*           conf;
	database_s*         db;
	configRepository_s* repo;
	status_s*           status;
	delay_t             download;
}jobArg_s;

__private int desc_tree_cmp(const void* pa, const void* pb){
	desc_s* a = (void*)pa;
	desc_s* b = (void*)pb;
	return strcmp(a->name, b->name);
}

database_s* database_ctor(database_s* db, configRepository_s* repo, unsigned flags){
	db->mem   = NULL;
	db->repo  = repo;
	db->flags = flags;
	rbtree_ctor(&db->elements, desc_tree_cmp);
	return db;
}

desc_s* database_search_byname(database_s* db, const char* name){
	desc_s tmp = {.name = (char*)name};
	return rbtree_search(&db->elements, &tmp);
}

desc_s* database_search_bydesc(database_s* db, desc_s* desc){
	return rbtree_search(&db->elements, desc);
}
/*
void database_add(database_s* db, desc_s* desc){
		if( desc->flags & DESC_FLAG_PROVIDE ){
			dbg_info("  ++%s->%s", desc->name, desc->link->name);
		}
		else if( desc->flags & DESC_FLAG_REPLACE ){
			dbg_info("  ++%s~>%s", desc->name, desc->link->name);
		}
		else{
			dbg_info("  ++%s", desc->name);
		}
	rbtree_insert(&db->elements, &desc->node);
}
*/
void database_insert(database_s* db, desc_s* desc){
	desc_s* d = database_search_bydesc(db, desc);
	if( d ){
		if( desc->flags & DESC_FLAG_PROVIDE ){
			dbg_info("  !+%s->%s", desc->name, desc->link->name);
		}
		else if( desc->flags & DESC_FLAG_REPLACE ){
			dbg_info("  !+%s~>%s", desc->name, desc->link->name);
		}
		else{
			dbg_info("  !+%s", desc->name);
		}
		ld_before(d, desc);
	}
	else{
		if( desc->flags & DESC_FLAG_PROVIDE ){
			dbg_info("  ?+%s->%s", desc->name, desc->link->name);
		}
		else if( desc->flags & DESC_FLAG_REPLACE ){
			dbg_info("  ?+%s~>%s", desc->name, desc->link->name);
		}
		else{
			dbg_info("  ?+%s", desc->name);
		}
		rbtree_insert(&db->elements, &desc->node);
	}
}

void database_insert_provides(database_s* db, desc_s* desc){
	if( !desc->provides ) return;
	mforeach(desc->provides, i){
		char* version = NULL;
		const unsigned flags = desc_parse_and_split_name_version(desc->provides[i], &version) | DESC_FLAG_PROVIDE;
		database_insert(db,
			desc_link(db, desc, desc->provides[i], version, flags)
		);
	}
}

void database_insert_replaces(database_s* db, desc_s* desc){
	if( !desc->replaces ) return;
	mforeach(desc->replaces, i){
		char* version = NULL;
		unsigned flags = desc_parse_and_split_name_version(desc->replaces[i], &version);
		database_insert(db,
			desc_link(db, desc, desc->replaces[i], version, DESC_FLAG_REPLACE | flags)
		);
	}
}

desc_s* database_sync_find(database_s** db, const char* name){
	mforeach(db, i){
		desc_s* ret = database_search_byname(db[i], name);
		if( ret ) return ret;
	}
	return NULL;
}

__private char* database_path(config_s* conf, const char* dbname, int tmp){
	dbg_info("database path: %s/%s.db%s", conf->options.dbPath, dbname,  (tmp) ? ".download": "");
	return str_printf("%s/%s.db%s", conf->options.dbPath, dbname, (tmp) ? ".download": "");
}

__private delay_t required_sync(config_s* conf){
	delay_t lastsync;
	if( download_lastsync(conf, &lastsync) ){
		dbg_warning("unable to get last sync");
		return 0;
	}
	__free char* dbpath = database_path(conf, "core", 0);
	delay_t dbsync = file_time_sec_get(dbpath);
	if( dbsync && dbsync < lastsync ){
		dbg_info("required update database(%lu < %lu", dbsync, lastsync);
		return lastsync;
	}
	return 0;
}

__private void* db_load_and_extract(const char* dbpath){
	dbg_info("load %s", dbpath);
	
	void* buffers[2] = {
		MANY(char, BUFLOAD_DEFAULT_SIZE),
		MANY(char, BUFLOAD_DEFAULT_SIZE)
	};
	gzip_t gz;
	gzip_ctor(&gz);
	void* ret = NULL;
	int fd = open(dbpath, O_RDONLY);
	if( fd == -1 ){
		dbg_error("unable to open %s: %m", dbpath);
		goto ATEND;
	}
	
	unsigned id = 0;
	request_t r = r_read(fd, buffers[0], BUFLOAD_DEFAULT_SIZE, -1, 0);
	r_commit();
	
	int res = -1;
	while( 1 ){
		rreturn_s ret = r_await(r);
		if( ret.ret < 0 ){
			dbg_error("error on reading %s: %m", dbpath);
			goto ATEND;
		}
		size_t nr   = ret.ret;
		void*  data = buffers[id];
		if( !nr ) break;
		id = (id+1) & 1;
		r = r_read(fd, buffers[id], BUFLOAD_DEFAULT_SIZE, -1, 0);
		r_commit();
		res = gzip_decompress(&gz, data, nr);
	}
	
	if( !res ){
		ret = gz.next_out;
		gz.next_out = NULL;
	}
	else{
		dbg_error("fail decompression gz %s", dbpath);
	}
ATEND:
	gzip_dtor(&gz);
	mem_free(buffers[0]);
	mem_free(buffers[1]);
	if( fd != -1 ) close(fd);
	return ret;
}

__private void db_sync_job(void* arg){
	jobArg_s* ja = arg;
	unsigned idstatus = status_new_id(ja->status);
	ja->db = database_ctor(NEW(database_s), ja->repo, 0);
	void* decdb = NULL;
	__free char* dbpath = database_path(ja->conf, ja->repo->name, 0);
	dbg_info("download/load");
	if( ja->download || !(decdb=db_load_and_extract(dbpath)) ){
		dbg_info("need download");
		__free char* dbtmppath = database_path(ja->conf, ja->repo->name, 1);
		decdb = download_database(dbtmppath, ja->status, idstatus, ja->repo, ja->conf);
		dbg_info("rename %s -> %s", dbtmppath, dbpath);
		r_rename(dbtmppath, dbpath, R_FLAG_SEQUENCE | R_FLAG_NOWAIT | R_FLAG_DIE);
		r_unlink(dbtmppath, R_FLAG_NOWAIT);
		r_commit();
	}
	ja->db->mem = decdb;
	if( ja->download ){
		file_time_sec_set(dbpath, ja->download);
	}
	
	unsigned total = tar_count(ja->db->mem, TAR_FILE);
	status_refresh(ja->status, idstatus, 0, STATUS_TYPE_WORKING);
	tar_s tar;
	tar_mopen(&tar, ja->db->mem);
	tarent_s ent;
	unsigned inc = 0;
	dbg_info("  total package %u", total);
	if( total == 0  ) die("internal error, aspected element in database now");
	while( tar_next(&tar, &ent) ){
		if( ent.type == TAR_FILE ){
			desc_s* desc = desc_unpack(ja->db, 0, ent.data, ent.size, 0);
			dbg_info("unpack %s", desc->name);
			database_insert(ja->db, desc);
			database_insert_provides(ja->db, desc);
			database_insert_replaces(ja->db, desc);
			++inc;
			if( !(inc % 10) ){
				const unsigned prog = (100 * (inc+1)) / total;
				status_refresh(ja->status, idstatus, prog, STATUS_TYPE_WORKING);
			}
		}
	}
	if( (errno=tar_errno(&tar)) ) die("unable to unpack database");
	
	dbg_info("sync %s success", ja->repo->name);
	status_completed(ja->status, idstatus);
	r_dispatch(-1);
}

__private void database_multimem_cleanup(void* mdb){
	char** mb = mdb;
	mforeach(mb, i){
		mem_free(mb);
	}
}

__private void db_local_job(void* arg){
	jobArg_s* ja = arg;
	unsigned idstatus = status_new_id(ja->status);
	ja->db = database_ctor(NEW(database_s), ja->repo, DATABASE_FLAG_MULTIMEM);

	DIR* d = opendir(ja->conf->options.localDir);
	if( !d ) die("unable to open path %s :: %m", ja->conf->options.localDir);
	unsigned total = 0;
	struct dirent* ent;
	while( (ent=readdir(d)) ){
		if( ent->d_type == DT_DIR && strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..") ) ++total;
	}
	rewinddir(d);
	dbg_info("installed package %u", total);

	char** multibuf = MANY(char*, total, database_multimem_cleanup);
	status_refresh(ja->status, idstatus, 0, STATUS_TYPE_WORKING);
	
	while( (ent=readdir(d)) ){
		if( ent->d_type == DT_DIR ){
			if( !strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..") ) continue;
			__free char* descpath = str_printf("%s/%s/desc", ja->conf->options.localDir, ent->d_name);
			unsigned idbuf = mem_ipush(&multibuf);
			multibuf[idbuf] = load_file(descpath, 1);
			database_insert(ja->db,
				desc_unpack(ja->db, 0, multibuf[idbuf], *mem_len(multibuf[idbuf]), 1)
			);
			if( !(idbuf%10) ){
				const unsigned prog = (100 * (idbuf+1)) / total;
				status_refresh(ja->status, idstatus, prog, STATUS_TYPE_WORKING);
			}
		}
	}
	ja->db->mem  = multibuf;
	status_completed(ja->status, idstatus);
}

void database_sync(arch_s* arch, config_s* conf, status_s* status, int forcenodowanload){
	dbg_info("");
	configRepository_s* aurRepo = NEW(configRepository_s);
	aurRepo->mirror = NULL;
	aurRepo->name   = "aur";
	aurRepo->path   = NULL;
	aurRepo->server = NULL;
	configRepository_s* localRepo = NEW(configRepository_s);
	localRepo->mirror = NULL;
	localRepo->name   = "local";
	localRepo->path   = NULL;
	localRepo->server = NULL;	
	unsigned const repoCount  = *mem_len(conf->repository);
	arch->aur   = database_ctor(NEW(database_s), aurRepo, 0);
	arch->local = NULL;
	arch->sync  = MANY(database_s*, repoCount);
	*mem_len(arch->sync) = repoCount;
	
	if( repoCount > 256 ) die("wtf, to many repository");
	jobArg_s ja[512];
	
	status->total = repoCount + 1;
	const delay_t  download  = forcenodowanload ? 0 : required_sync(conf);
	
	ja[0].conf     = conf;
	ja[0].download = 0;
	ja[0].status   = status;
	ja[0].repo     = localRepo;
	ja[0].db       = NULL;
	job_new(db_local_job, &ja[0], 1);
	for(unsigned i = 0; i < repoCount; ++i){
		ja[i+1].conf     = conf;
		ja[i+1].download = download;
		ja[i+1].status   = status;
		ja[i+1].repo     = &conf->repository[i];
		ja[i+1].db       = NULL;
		job_new(db_sync_job, &ja[i+1], 1);
	}
	job_wait();

	arch->local = ja[0].db;
	for(unsigned i = 0; i< repoCount; ++i ){
		arch->sync[i] = ja[i+1].db;
		/*test conflics
		rbtreeit_s it;
		rbtreeit_ctor(&it, &arch->local->elements, 0);
		desc_s* desc;
		while( (desc=rbtree_iterate_inorder(&it)) ){
			desc = desc_nonvirtual(desc);
			if( !desc ) continue;
			if( !desc->conflicts ) continue;
			mforeach(desc->conflicts, i){
				if( strpbrk(desc->conflicts[i], "<=>") ) die("need split conflicts with version split: %s", desc->conflicts[i]);
			}
		}
		rbtreeit_dtor(&it);
		*/
	}
	
	rbtreeit_s it;
	rbtreeit_ctor(&it, &arch->local->elements, 0);
	desc_s* desc;
	while( (desc=rbtree_iterate_inorder(&it)) ){
		desc_s* dsync = database_sync_find(arch->sync, desc->name);
		if( dsync ){
			if( (dsync = desc_nonvirtual(dsync)) ){
				dsync->flags |= DESC_FLAG_INSTALLED;
			}
		}
		else if( !(desc->flags & DESC_FLAG_MAKEPKG) ){
			desc->flags |= DESC_FLAG_REMOVED;
		}
	}
	rbtreeit_dtor(&it);
}

void database_import_json(database_s* db, unsigned flags, jvalue_s* results){
	mforeach(results->a, i){
		desc_s* desc = desc_unpack_json(db, flags, &results->a[i]);
		database_insert(db, desc);
		database_insert_provides(db, desc);
		database_insert_replaces(db, desc);
		dbg_info("add aur: %s", desc->name);
	}
}

__private fzs_s* match_add(fzs_s* m, void* ctx, const char* name){
	unsigned id = mem_ipush(&m);
	m[id].ctx = ctx;
	m[id].len = 0;
	m[id].str = name;
	return m;
}

fzs_s* database_match_fuzzy(fzs_s* vf, database_s* db, const char* name){
	rbtreeit_s it;
	rbtreeit_ctor(&it, &db->elements, 0);
	desc_s* desc;
	while( (desc=rbtree_iterate_inorder(&it)) ){
		if( strstr(desc->name, name) ){
			dbg_info("....");
			ldforeach(desc, it){
				dbg_info("%s", it->name);
				vf = match_add(vf, it, it->name);
			}
			dbg_info("-----");
		}
		else{
			ldforeach(desc, it){
				if( it->flags & (DESC_FLAG_PROVIDE|DESC_FLAG_REPLACE) ){
					if( it->link->desc && strstr(it->link->desc, name) ){
						vf = match_add(vf, it, it->name);
					}
				}
				else if( it->desc && strstr(it->desc, name) ){
					vf = match_add(vf, it, it->name);
				}
			}
		}
	}
	rbtreeit_dtor(&it);
	return vf;
}
















