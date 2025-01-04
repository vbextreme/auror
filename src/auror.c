#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/opt.h>

#include <auror/auror.h>
#include <auror/www.h>
#include <auror/pacman.h>
#include <auror/aur.h>
#include <auror/inutility.h>

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

option_s OPT[] = {
	{'u', "upgrade"       , "upgrade upstream and aur",    OPT_NOARG, 0, 0},
	{'s', "search"        , "search in upstream and aur",  OPT_STR, 0, 0},
	{'i', "install"       , "install",                     OPT_ARRAY | OPT_STR, 0, 0},
	{'n', "--num-outputs" , "max output value",            OPT_NUM, 0, 0},
	{'h', "--help"        , "display this",                OPT_END | OPT_NOARG, 0, 0}
};

__private void print_matchs(fzs_s* matchs, const char* name, unsigned max){
	fzs_qsort(matchs, mem_header(matchs)->len, name, 0, fzs_levenshtein);
	const unsigned count = mem_header(matchs)->len;
	const unsigned end = max && max < count ? max : count;
	for( unsigned i = 0; i < end; ++i ){
		desc_s* d = matchs[i].ctx;
		char* n =  desc_value(d, "NAME")[0];
		if( !*n ) n = desc_value(d, "Name")[0];
		char** dsc =  desc_value(d, "DESC");
		if( !dsc[0][0] ) dsc = desc_value(d, "Description");
		
		printf("%s/%s", d->db->name, n);
		if( d->flags & PKG_FLAG_INSTALL ) fputs(" (Installed)", stdout);
		putchar('\n');
		mforeach(dsc, k){
			printf("    %s\n", dsc[k]);
		}
	}
}

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
			cmd[mem_header(cmd)->len] = 0;;
			++(*count);
		}
		if( it->deps ){
			cmd = pacman_deps_list(cmd, it->deps, count);
		}
	}
	return cmd;
}

int main(int argc, char** argv){
	notstd_begin();
	www_begin();
	
	__argv option_s* opt = argv_parse(OPT, argc, argv);
	if( opt[O_h].set ) argv_usage(opt, argv[0]);

	argv_default_num(opt, O_n, 0);

	if( opt[O_u].set ){ //TODO | opt[O_i].set,
		pacman_upgrade();
	}
	
	pacman_s pacman;
	pacman_ctor(&pacman);
	aur_s aur;
	aur_ctor(&aur);
	
	/*
	char* test = load_file("testjson", 1);
	mem_nullterm(test);
	jvalue_s* jv = json_decode(test, NULL, NULL);
	jvalue_s* res = jvalue_property(jv, "results");
	mforeach(res->a, i){
		jvalue_s* name = jvalue_property(&res->a[i], "Name");
		if( !strcmp(name->s, "interception-vimproved-git") ){
			jvalue_dump(&res->a[i]);
		}
	}
	__free fzs_s* matchs = MANY(fzs_s, 64);
	ddatabase_s* search = aur_search_test(jv, opt[O_s].value->str, &matchs);
	desc_s* tdesc = database_search_byname(search, "interception-vimproved-git");
	desc_dump(tdesc);;
	pacman_scan_installed(&pacman, search);
	print_matchs(matchs, opt[O_s].value->str, opt[O_n].value->ui);
	
	mem_free(search);
	die("OK");
	*/
	
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
		unsigned pacgnamcount = 0;
		__free char* pacgnam = pacman_deps_list(str_dup(PACMNA_INSTALL_DEPS, 0), async.pkg, &pacgnamcount);
		if( pacgnamcount ) 
			dbg_info("%s", pacgnam);
			//shell("pacman deps resolve", pacgnam);
		
		//create sandbox
		//install build deps
		//makepkg
		//check sandbox
		//extract pkg
		//~persistent delete sandbox
		//install pkg with pacman
	}
	
	if( opt[O_s].set ){
		__free fzs_s* matchs = MANY(fzs_s, 64);
		pacman_search(&pacman, opt[O_s].value->str, &matchs);
		__free ddatabase_s* search = aur_search(&aur, opt[O_s].value->str, &matchs);
		pacman_scan_installed(&pacman, search);
		print_matchs(matchs, opt[O_s].value->str, opt[O_n].value->ui);
	}

	www_end();
	return 0;
}





















