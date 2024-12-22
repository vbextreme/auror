#include <notstd/str.h>

#include <aurora/pacman.h>
#include <aurora/inutility.h>

#include <dirent.h>

__private void pacman_scan_installed_or_custom(pacman_s* pacman){
	ddatabase_s* install = pacman->db[pacman->iddbinstall];
	ddatabase_s* aur     = pacman->db[pacman->iddbaur];
	
	dbg_info("scan upstream");
	mforeach(install->desc, i){
		char* search = desc_value(&install->desc[i], "NAME")[0];
		if( !*search ) continue;
		mforeach(pacman->db, j){
			if( !(pacman->db[j]->flags & DB_FLAG_UPSTREAM) ) continue;
			desc_s* d = database_search_byname(pacman->db[j], search);
			if( d ){
				d->flags |= PKG_FLAG_INSTALL;
			}
			else{
				database_import(aur, &install->desc[j])->flags = PKG_FLAG_INSTALL;
			}
		}
	}
}

void pacman_scan_installed(pacman_s* pacman, ddatabase_s* db){
	ddatabase_s* install = pacman->db[pacman->iddbinstall];
	
	dbg_info("scan '%s'", db->name);
	mforeach(install->desc, i){
		char* search = desc_value_name(&install->desc[i]);
		//dbg_info("  search: %s", search);
		if( !*search ) continue;
		desc_s* d = database_search_byname(db, search);
		if( d ){
			//dbg_info("  mark");	
			d->flags |= PKG_FLAG_INSTALL;
		}
	}
	//dbg_info("OK");
}

__private void database_installed(pacman_s* pacman){
	DIR* d = opendir(pacman->db[pacman->iddbinstall]->location);
	if( !d ) die("unable to find %s", pacman->db[pacman->iddbinstall]->location);
	struct dirent* ent;
	while( (ent=readdir(d)) ){
		if( ent->d_type == DT_DIR ){
			if( !strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..") ) continue;
			__free char* descpath = str_printf("%s/%s/desc", pacman->db[pacman->iddbinstall]->location, ent->d_name);
			__free void* descfile = load_file(descpath, 0);
			if( descfile ){
				database_add(pacman->db[pacman->iddbinstall], descfile, mem_header(descfile)->len, 0);
			}
			else{
				dbg_warning("unable to load file %s: %m", descpath);
			}
		}
	}
}

void pacman_ctor(pacman_s* pacman){
	dbg_info("");
	memset(pacman, 0, sizeof(pacman_s));
	pacman->db = MANY(ddatabase_s*, 4);

	__free char* conf = load_file(PACMAN_CONFIG, 1);
	conf = mem_nullterm(conf);
	
	if( ini_unpack(&pacman->config, conf) ) die("unable to read pacman config: %s", PACMAN_CONFIG);

	dbg_info("reading options");
	iniSection_s* options = ini_section(&pacman->config, "options");
	pacman->dbLocalPath = mem_borrowed((char*)ini_value(options, "DBPath"));
	if( !pacman->dbLocalPath ) pacman->dbLocalPath = str_dup(PACMAN_LOCAL_DB, 0);

	mforeach(pacman->config.section, i){
		if( strcmp(pacman->config.section[i].name, "options") ){
			char* name = pacman->config.section[i].name;
			char* url  = (char*)ini_value(&pacman->config.section[i], "Include");
			if( !url ) die("aspectd Include in section [%s] in pacman.conf", name);
			
			__free char* path = str_printf("%s/%s.db", pacman->dbLocalPath, name);
			__free void* dbTarGz = load_file(path, 1);
			
			dbg_info("add database %s", name);
			pacman->db = mem_upsize(pacman->db, 1);
			const unsigned id = mem_header(pacman->db)->len++;
			pacman->db[id] = database_unpack(
				mem_borrowed(name),
				mem_borrowed(path),
				mem_borrowed(url),
				dbTarGz,
				0
			);
			if( !pacman->db[id] ) die("an error are occured when try to unpack database %s: %m", name);
			pacman->db[id]->flags = DB_FLAG_UPSTREAM;
		}
	}

	pacman->db = mem_upsize(pacman->db, 1);
	pacman->iddbinstall = mem_header(pacman->db)->len++;
	pacman->db[pacman->iddbinstall] = database_new(
		str_dup(PACMAN_DB_INSTALL_NAME, 0),
		str_dup(PACMAN_LOCAL_INSTALL, 0),
		str_dup(PACMAN_LOCAL_INSTALL, 0)
	);
	database_installed(pacman);
	database_flush(pacman->db[pacman->iddbinstall]);
	
	pacman->db = mem_upsize(pacman->db, 1);
	pacman->iddbaur = mem_header(pacman->db)->len++;
	pacman->db[pacman->iddbaur] = database_new(
		str_dup(PACMAN_DB_CUSTOM_NAME, 0),
		str_dup(PACMAN_DB_CUSTOM_URL, 0),
		str_dup(PACMAN_LOCAL_INSTALL, 0)
	);
	
	pacman_scan_installed_or_custom(pacman);
	database_flush(pacman->db[pacman->iddbaur]);
}

void pacman_upgrade(void){
	puts(PACMAN_UPGRADE);
	int ex = system(PACMAN_UPGRADE);
	if( ex == -1 ) die("pacman unable upgrade: %m");
	if( !WIFEXITED(ex) || WEXITSTATUS(ex) != 0) die("pacman exit with error: %d %d", WIFEXITED(ex), WEXITSTATUS(ex));
}

void pacman_search(pacman_s* pacman, const char* name, fzs_s** matchs){
	mforeach(pacman->db, i){
		if( pacman->db[i]->flags & DB_FLAG_UPSTREAM ){
			*matchs = database_match_fuzzy(*matchs, pacman->db[i], name);
		}
	}
}

desc_s* pacman_pkg_search(pacman_s* pacman, const char* name){
	desc_s* ret;
	mforeach(pacman->db, idb){
		if( pacman->db[idb]->flags & DB_FLAG_UPSTREAM ){
			if( (ret=database_search_byname(pacman->db[idb], name)) ) return ret;
		}
	}
	ddatabase_s* aur = pacman->db[pacman->iddbaur];
	ret = database_search_byname(aur, name);
	return ret;
}



































