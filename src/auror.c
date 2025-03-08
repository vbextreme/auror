#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/opt.h>
#include <notstd/tig.h>
#include <notstd/delay.h>
#include <notstd/request.h>

#include <auror/powerpellets.h>
#include <auror/auror.h>
#include <auror/www.h>
#include <auror/pacman.h>
#include <auror/aur.h>
#include <auror/inutility.h>
#include <auror/status.h>
#include <auror/jobs.h>
#include <auror/config.h>
#include <auror/database.h>
#include <auror/transaction.h>

#include <dirent.h>

#define BUF_ANALYZE_SIZE (4096*64)

//TODO
//	install
//		download package
//		add dependency
//		create sandbox
//		build
//		install
//	upgrade
//		download all pkgbuild not have in cache
//		search dependency
//		create sandbox
//		build
//		install
//

//__private char* CACHE_DIR = NULL;

option_s OPT[] = {
	{'u', "upgrade"       , "upgrade upstream and aur"    , OPT_NOARG, 0, 0},
	{'s', "search"        , "search in upstream and aur"  , OPT_STR, 0, 0},
	{'i', "install"       , "install"                     , OPT_SLURP | OPT_STR, 0, 0},
	{'a', "--aur"         , "revers aur option"           , OPT_NOARG, 0, 0},
	{'n', "--num-outputs" , "max output value"            , OPT_NUM, 0, 0},
	{'d', "--destdir"     , "destdir for install"         , OPT_PATH | OPT_EXISTS | OPT_DIR, 0, 0},
	{'c', "--config"      , "config path"                 , OPT_PATH | OPT_EXISTS, 0, 0},
	{'h', "--help"        , "display this"                , OPT_END | OPT_NOARG, 0, 0}
};

__private unsigned get_repo_color(const char* repo, config_s* conf){
	mforeach(conf->theme.repo, i){
		if( !strcmp(repo, conf->theme.repo[i].name) ) return conf->theme.repo[i].value;
	}
	return 255;
}

__private void print_desc_repo_name(desc_s* desc, config_s* conf){
	bold_set();
	colorfg_set(15);
	putchar('[');
	colorfg_set(0);
	colorfg_set(get_repo_color(desc->db->repo->name, conf));
	fputs(desc->db->repo->name, stdout);
	bold_set();
	colorfg_set(15);
	putchar('/');
	colorfg_set(0);
	colorfg_set(get_repo_color("pkg", conf));
	fputs(desc->name, stdout);
	bold_set();
	colorfg_set(15);
	putchar(']');
	colorfg_set(0);
}

__private void print_desc_installed(desc_s* desc){
	if( desc->flags & DESC_FLAG_INSTALLED ){
		colorfg_set(86);
		fputs("installed", stdout);
		colorfg_set(0);
	}
}

__private void print_desc_basic(desc_s* desc, config_s* conf){
	if( (desc->flags & DESC_FLAG_PROVIDE) || (desc->flags & DESC_FLAG_REPLACE)  ){
		print_desc_repo_name(desc, conf);
		if( desc->flags & DESC_FLAG_PROVIDE ){
			colorfg_set(get_repo_color(">>", conf));
			fputs(">>", stdout);
		}
		else{
			colorfg_set(get_repo_color("!>", conf));
			fputs("!>", stdout);
		}
		desc = desc->link;
	}
	print_desc_repo_name(desc, conf);
	putchar(' ');
	print_desc_installed(desc);
	putchar('\n');
	if( desc->desc ){
		putchar('\t');
		puts(desc->desc);
	}
}

__private void print_matchs(fzs_s* matchs, const char* name, unsigned max, config_s* conf){
	fzs_qsort(matchs, *mem_len(matchs), name, 0, fzs_levenshtein);
	const unsigned count = *mem_len(matchs);
	const unsigned end = max && max < count ? max : count;
	for( unsigned i = 0; i < end; ++i ){
		print_desc_basic(matchs[i].ctx, conf);
	}
}
/*
__private void print_pkg_deps(pkgInfo_s* pkg, unsigned tab, unsigned w){
	unsigned cw = tab * 2;
	print_repeats(tab, "  ");

	ldforeach(pkg, it){
		unsigned nw = snprintf(NULL, 0,
			"[%s/%s]%s",
			(it->flags & DB_FLAG_UPSTREAM ? "upstream": "aur"),
			it->name,
			(it->deps ? ":": "  ")
		);
		if( nw + cw > w ){
			putchar('\n');
			cw = tab * 2;
			print_repeats(tab, "  ");
		}
		printf("[%s/%s]%s",
			(it->flags & DB_FLAG_UPSTREAM ? "upstream": "aur"),
			it->name,
			(it->deps ? ":": "  ")
		);
		if( it->deps ){
			putchar('\n');
			print_pkg_deps(it->deps, tab+1, w);
			cw = tab * 2;
			print_repeats(tab, "  ");
		}
	}
	putchar('\n');
}

__private void print_packages(aurSync_s* async){
	if( !async->pkg ) return;
	unsigned w;
	term_wh(&w, NULL);
	print_pkg_deps(async->pkg, 0, w);
}

__private char* pacman_deps_list(char* cmd, pkgInfo_s* pkg, unsigned* count){
	ldforeach(pkg, it){
		if( it->flags & PKGINFO_FLAG_BUILD_DEPENDENCY ) continue;
		if( it->flags & DB_FLAG_UPSTREAM ){
			unsigned len = strlen(it->name);
			cmd = mem_upsize(cmd, len+2);
			cmd[mem_header(cmd)->len++] = ' ';
			memcpy(&cmd[mem_header(cmd)->len], it->name, len);
			mem_header(cmd)->len += len;
			cmd[mem_header(cmd)->len] = 0;
			++(*count);
		}
		if( it->deps ){
			cmd = pacman_deps_list(cmd, it->deps, count);
		}
	}
	return cmd;
}

__private char** aur_deps_list(char** deps, pkgInfo_s* pkg){
	ldforeach(pkg, it){
		if( it->flags & PKGINFO_FLAG_BUILD_DEPENDENCY ) continue;
		if( it->flags & DB_FLAG_UPSTREAM ) continue;
		deps = mem_upsize(deps, 1);
		deps[mem_header(deps)->len++] = it->name;
		if( it->deps ) deps = aur_deps_list(deps, it->deps);
	}
	return deps;
}

__private char* pkg_makedeps_list(pkgInfo_s* pkg){
	char* mkd = MANY(char, 32);
	mkd[0] = 0;
	if( !pkg ) return mkd;

	ldforeach(pkg, it){
		if( !(it->flags & PKGINFO_FLAG_BUILD_DEPENDENCY) ) continue;
		if( it->flags & PKGINFO_FLAG_DEPENDENCY ) continue;
		unsigned len = strlen(it->name);
		mkd = mem_upsize(mkd, len+2);
		mkd[mem_header(mkd)->len++] = ',';
		memcpy(&mkd[mem_header(mkd)->len], it->name, len);
		mem_header(mkd)->len += len;
		mkd[mem_header(mkd)->len] = 0;
	}
	return mkd;
}

__private void aur_git(const char* pkgname){
	__free char* pkgdst = str_printf("%s/%s/%s", CACHE_DIR, pkgname, pkgname);
	
	if( dir_exists(pkgdst) ){
		dbg_info("pull: '%s'", pkgdst);
		if( !tig_pull(NULL, pkgdst, "origin", NULL, NULL) ) return;
		dbg_error("fail pull, try delete and clone %s: %s", pkgdst, tig_strerror());
		rm(pkgdst);
	}
	
	__free char* url = str_printf(AUR_URL "/%s.git", pkgname);
	mk_dir(pkgdst, 0770);
	dbg_info("clone: '%s' -> '%s'", url, pkgdst);
	if( tig_clone(url, pkgdst, NULL, NULL) ) die("unable to get aur/%s/PKGBUILD", pkgname);
}

__private char* pkgbuild_analyze(const char* pkgname, unsigned* res){
	__free char* pkgbuild = str_printf("%s/%s/%s/PKGBUILD", CACHE_DIR, pkgname, pkgname);
	char* ret = aur_pkgbuild_analyzer(pkgbuild, res);
	switch( *res ){
		case 1: *res = 12; break;
		case 2: *res = 11; break;
		case 3: *res =  9; break;
	}
	dbg_info("%s::%s::%u", pkgbuild, ret, *res);
	return ret;
}

typedef struct hestiaAnalyzer{
	unsigned type;
	char* path;
}hestiaAnalyzer_s;

__private long hestia_get_type(const char** line){
	__private const char* name[] = {
		"reg",
		"dir",
		"fifo",
		"sock",
		"chr",
		"blk",
		"link"
	};
	__private const unsigned value[] = {
		DT_REG,
		DT_DIR,
		DT_FIFO,
		DT_SOCK,
		DT_CHR,
		DT_BLK,
		DT_LNK
	};
	
	const char* p = *line;
	if( *p != '[' ) return -1;
	++p;
	for( unsigned i = 0; i < sizeof_vector(name); ++i ){
		if( !strncmp(p, name[i], strlen(name[i])) ){
			p += strlen(name[i]);
			if( *p++ != ']' ) return -1;
			*line = p;
			return value[i];
		}
	}
	return -1;
}

__private hestiaAnalyzer_s* heastia_analyzer_parse(FILE* f){
	hestiaAnalyzer_s* ent = MANY(hestiaAnalyzer_s, 4);
	char line[BUF_ANALYZE_SIZE];
	while( fgets(line, BUF_ANALYZE_SIZE, f) ){
		str_chomp(line);
		const char* p = line;
		long type = hestia_get_type(&p);
		if( type == -1 ) return ent;
		ent = mem_upsize(ent,1);
		hestiaAnalyzer_s* e = &ent[mem_header(ent)->len++];
		e->type = type;
		e->path = str_dup(p, 0);
	}
	return ent;
}

__private int hestia_analyzer_begin(FILE* f, const char* match){
	char line[BUF_ANALYZE_SIZE];
	if( !fgets(line, BUF_ANALYZE_SIZE, f) ) return -1;
	if( strcmp(line, match) ){
		dbg_warning("first line unmatch to @analyzer@: %s", line);
		return -1;
	}
	return 0;
}

__private int hestia_analizer_exists(hestiaAnalyzer_s* from, hestiaAnalyzer_s* ent){
	mforeach(from, i){
		if( from[i].type == ent->type && !strcmp(from[i].path, ent->path) ) return 1;
	}
	return 0;
}

__private hestiaAnalyzer_s* hestia_analyzer_diff(hestiaAnalyzer_s* before, hestiaAnalyzer_s* after){
	hestiaAnalyzer_s* ent = MANY(hestiaAnalyzer_s, 4);
	
	mforeach(after, ia){
		if( !hestia_analizer_exists(before, &after[ia]) ){
			//TODO add to ent
		}
	}

	return ent;
}


__private int pkg_build(pkgInfo_s* pkg){
	printf("start download/build %s\n", pkg->name);
	__free char* mkdeps    = pkg_makedeps_list(pkg->deps);
	__free char* pkgsand   = str_printf("%s/%s", CACHE_DIR, pkg->name);
	__free char* snappath  = str_printf("%s/%s/build.snapshot", CACHE_DIR, pkg->name);
	__free char* analmatch = str_printf("@analyzer@%s/root\n", pkgsand);
	__free char* cmd       = str_printf("sudo hestia -cdaP auror %s -A %s%s%s -e /build/buildscript 2>&1", pkgsand, pkg->name, (*mkdeps ? "," : ""), mkdeps);

	FILE* fsnap = fopen(snappath, "w");
	if( !fsnap ){
		dbg_error("fail to open %s:: %m", snappath);
		return -1;
	}
	FILE* f = popen(cmd, "r");
	if( !f ){
		dbg_error("on popen");
		fclose(fsnap);
		return -1;
	}
	char buf[BUF_ANALYZE_SIZE];
	unsigned enterinanalize = 0;
	while( fgets(buf, BUF_ANALYZE_SIZE, f) != NULL ){
		if( enterinanalize ){
			if( buf[0] == '[' ){
				fputs(buf, fsnap);
				continue;
			}
			enterinanalize = 0;
		}
		if( strcmp(buf, analmatch) ){
			dbg_info("%s", buf);
		}
		else{
			dbg_info("enter in analyzer");
			fputs(buf, fsnap);	
			enterinanalize = 1;
		}
	}
	fclose(fsnap);
	int es = pclose(f);
	if( es == -1 ) return -1;
	if( !WIFEXITED(es) || WEXITSTATUS(es) != 0) return -1;
	return 0;
}

__private int pkg_analyzer(pkgInfo_s* pkg){
	__free char* snapbegin  = str_printf("%s/%s/begin.snapshot", CACHE_DIR, pkg->name);
	__free char* snapbuild  = str_printf("%s/%s/build.snapshot", CACHE_DIR, pkg->name);
	__free char* analyzer   = str_printf("@analyzer@%s/%s/root\n", CACHE_DIR, pkg->name);
	int ret = -1;

	FILE* begin = fopen(snapbegin, "r");
	if( !begin ){
		dbg_error("unable to open %s:: %m", snapbegin);
		return -1;
	}
	FILE* build = fopen(snapbuild, "r");
	if( !build ){
		dbg_error("unable to open %s:: %m", snapbuild);
		fclose(begin);
		return -1;
	}
	
	if( hestia_analyzer_begin(begin, analyzer) ){
		if( hestia_analyzer_begin(build, analyzer) ){
			ret = 0;
			goto ONEND;
		}
	}
	//hestiaAnalyzer_s* entbegin = heastia_analyzer_parse(begin);
	//hestiaAnalyzer_s* entbuild = heastia_analyzer_parse(build);
	
	
	
	
ONEND:
	fclose(begin);
	fclose(build);
	return ret;
}

__private pkgInfo_s** packages_install_list(pkgInfo_s** install, pkgInfo_s* pkg){
	ldforeach(pkg, it){
		if( it->deps ){
			ldforeach(it->deps, idep){
				if( !(idep->flags & PKGINFO_FLAG_DEPENDENCY) ) continue;
				if( idep->flags & DB_FLAG_UPSTREAM ) continue;
				install = packages_install_list(install, idep);
			}
		}
		install = mem_upsize(install, 1);
		install[mem_header(install)->len++] = it;
	}
	return install;
}

__private int packages_install(pkgInfo_s* pkg){
	//build
	pkgInfo_s** pkginstall = MANY(pkgInfo_s*, 16);
	pkginstall = packages_install_list(pkginstall, pkg);
	unsigned const countinstall = mem_header(pkginstall)->len;
	if( !countinstall ) return 0;

	for( unsigned i = 0; i < countinstall; ++i ){
		if( pkg_build(pkginstall[i]) ){
			//TODO disintallare
			dbg_error("error on build %s", pkginstall[i]->name);
			return -1;
		}
	}
	
	//analyzer build
	for( unsigned i = 0; i < countinstall; ++i ){
		if( pkg_analyzer(pkginstall[i]) ){
			//TODO disintallare
			dbg_error("user stop with analyzer %s", pkginstall[i]->name);
			return -1;
		}
	}
	
	
	//install
	
	return 0;
}
*/
desc_s** package_resolve(arch_s* arch);

int main(int argc, char** argv){
	notstd_begin();
	www_begin();
	tig_begin();

	__argv option_s* opt = argv_parse(OPT, argc, argv);
	if( opt[O_h].set ) argv_usage(opt, argv[0]);

	argv_default_num(opt, O_n, 0);
	__free char* root  = opt[O_d].set ? path_explode(opt[O_d].value->str) : str_dup("/", 1);	
	__free char* cfile = NULL;
	if( opt[O_c].set ){
		cfile = path_explode(opt[O_c].value->str);
	}
	else{
		cfile = str_dup(root, 0);
		cfile = path_cats(cfile, AUROR_CONFIG_PATH, 0);
	}
	dbg_info("root: %s", root);
	config_s* conf = config_load(cfile, root);
	arch_s arch;
	aur_s aur;
	
	transaction_begin(conf);

conf->options.parallel = 1;
	job_begin(conf->options.parallel);
	status_s status;
	status_ctor(&status, conf, conf->options.parallel);
	
	status_description(&status, "sync database");
	database_sync(&arch, conf, &status, 1);
	if( opt[O_a].set ) conf->options.aur = !conf->options.aur;
	
	if( conf->options.aur ){
		aur_ctor(&aur);
	}
	
	if( opt[O_s].set ){
		status_description(&status, "search in database");
		fzs_s* matchs = MANY(fzs_s, 128);
		for( unsigned i = 0; i < *mem_len(conf->repository); ++i ){
			matchs = database_match_fuzzy(matchs, arch.sync[i], opt[O_s].value->str);	
		}
		if( conf->options.aur ){
			aur_search(&aur, &arch, opt[O_s].value->str);
			matchs = database_match_fuzzy(matchs, arch.aur, opt[O_s].value->str);
		}
		print_matchs(matchs, opt[O_s].value->str, opt[O_n].value->ui, conf);
	}

	package_resolve(&arch);

	status_dtor(&status);
	job_end();
	return 0;
/*
	if( opt[O_u].set ){ 
		pacman_upgrade();
	}
	
	pacman_s pacman;
	pacman_ctor(&pacman);
	aur_s aur;
	aur_ctor(&aur);

	if( opt[O_i].set ){
		aurSync_s async;
		async.pkg = NULL;
		//async.pkg = MANY(pkgInfo_s, 4);
		char** pkgs = MANY(char*, opt[O_i].set + 1);
		for( unsigned i = 0; i < opt[O_i].set; ++i ){
			pkgs[i] = (char*)opt[O_i].value[i].str;
		}
		mem_header(pkgs)->len = opt[O_i].set;
		aur_dependency_resolve(&aur, &pacman, &async, NULL, pkgs, 0);
		print_packages(&async);
		
		fputs("Proceed with the installation? ", stdout);
		if( !readline_yesno() ) die("terminated at the user's discretion");
		
		//install pacman deps
		unsigned tcount = 0;
		__free char* pacgnam = pacman_deps_list(str_dup(PACMNA_INSTALL_DEPS, 0), async.pkg, &tcount);
		if( tcount ){
			dbg_info("%s", pacgnam);
			//TODO shell("pacman deps resolve", pacgnam);
		}
	
		__free char** aurgnam   = aur_deps_list(MANY(char*, 4), async.pkg);
		__free char** auranal   = MANY(char*, mem_header(aurgnam)->len);
		__free unsigned* pkgcol = MANY(unsigned, mem_header(aurgnam)->len);
		const char* prompt = "Downloading PKGBUILD";
		//progress_begin(prompt, mem_header(aurgnam)->len);
		mforeach(aurgnam, i){
			aur_git(aurgnam[i]);
			auranal[i] = pkgbuild_analyze(aurgnam[i], &pkgcol[i]);
			//progress(prompt, 1);
		}
		//progress_end(prompt);
		mem_header(auranal)->len = mem_header(aurgnam)->len;
		mem_header(pkgcol)->len  = mem_header(aurgnam)->len;
		
		__free unsigned* selview = readline_listid("Select PKGBUILD to introspect", aurgnam, pkgcol);
		mforeach(selview, i){
			__free char* pkgbuilddir = str_printf("%s/%s/%s/PKGBUILD", CACHE_DIR, aurgnam[selview[i]], aurgnam[selview[i]]);
			__free char* buf = load_file(pkgbuilddir, 1);
			buf = mem_nullterm(buf);
			colorfg_set(231);
			colorbg_set(239);
			bold_set();
			fputs(pkgbuilddir, stdout);
			colorfg_set(0);
			putchar('\n');
			if( *auranal[selview[i]] ){
				puts("PKGBUILD analyzer:");
				puts(auranal[selview[i]]);
				colorbg_set(239);
				print_repeat(strlen(pkgbuilddir), ' ');
				colorfg_set(0);
				putchar('\n');
			}
			print_highlight(buf, "bash");
			
			printf("\ncontinue? ");
			if( !readline_yesno() )  die("terminated at the user's discretion");
		}
		
		packages_install(async.pkg);
	}
	
	if( opt[O_s].set ){
		__free fzs_s* matchs = MANY(fzs_s, 64);
		pacman_search(&pacman, opt[O_s].value->str, &matchs);
		//__free ddatabase_s* search = aur_search(&aur, opt[O_s].value->str, &matchs);
		//pacman_scan_installed(&pacman, search);
		print_matchs(matchs, opt[O_s].value->str, opt[O_n].value->ui);
	}
*/
	tig_end();
	www_end();
	return 0;
}





















