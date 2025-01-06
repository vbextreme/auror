#include <notstd/core.h>
#include <notstd/str.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <pwd.h>
#include <limits.h>
#include <dirent.h>
#include <readline/readline.h>

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-bash.h>

char* load_file(const char* fname, int exists){
	dbg_info("loading %s", fname);
	int fd = open(fname, O_RDONLY);
	if( fd < 0 ){
		if( exists ) die("unable to open file: %s, error: %m", fname);
		return NULL;
	}
	char* buf = MANY(char, 4096);
	ssize_t nr;
	while( (nr=read(fd, &buf[mem_header(buf)->len], mem_available(buf))) > 0 ){
		mem_header(buf)->len += nr;
		buf = mem_upsize(buf, 4096);
	}
	close(fd);
	if( nr < 0 ) die("unable to read file: %s, error: %m", fname);
	buf = mem_fit(buf);
	return buf;
}

int dir_exists(const char* path){
	DIR* d = opendir(path);
	if( !d ) return 0;
	closedir(d);
	return 1;
}

int vercmp(const char *a, const char *b){
	const char *pa = a, *pb = b;
	int r = 0;
	
	while (*pa || *pb) {
		if (isdigit(*pa) || isdigit(*pb)) {
			long la = 0, lb = 0;
			while (*pa == '0') pa++;
			while (*pb == '0') pb++;
			while (isdigit(*pa)) {
				la = la * 10 + (*pa - '0');
				pa++;
			}
			while (isdigit(*pb)) {
				lb = lb * 10 + (*pb - '0');
				pb++;
			}
			if (la < lb) {
				return -1;
			}
			else if (la > lb) {
				return 1;
			}
		}
		else if (*pa && *pb && isalpha(*pa) && isalpha(*pb)) {
			r = tolower((unsigned char)*pa) - tolower((unsigned char)*pb);
			if (r != 0) return r;
			pa++;
			pb++;
		}
		else{
			char ca = *pa ? *pa : 0;
			char cb = *pb ? *pb : 0;
			
			if (ca == '-' || ca == '_') ca = '.';
			if (cb == '-' || cb == '_') cb = '.';
			if (ca != cb) return ca - cb;
			if (ca) pa++;
			if (cb) pb++;
		}
	}
	return 0;
}

char* path_home(char* path){
	char *hd;
	if( (hd = getenv("HOME")) == NULL ){
		struct passwd* spwd = getpwuid(getuid());
		if( !spwd ) die("impossible to get home directory");
        strcpy(path, spwd->pw_dir);
	}   
	else{
		strcpy(path, hd);
    }
	return path;
}

char* path_explode(const char* path){
	if( path[0] == '~' && path[1] == '/' ){
		char home[PATH_MAX];
		return str_printf("%s%s", path_home(home), &path[1]);
	}
	else if( path[0] == '.' && path[1] == '/' ){
		char cwd[PATH_MAX];
		return str_printf("%s%s", getcwd(cwd, PATH_MAX), &path[1]);
	}
	else if( path[0] == '.' && path[1] == '.' && path[2] == '/' ){
		char cwd[PATH_MAX];
		getcwd(cwd, PATH_MAX);
		const char* bk = strrchr(cwd, '/');
		iassert( bk );
		if( bk > cwd ) --bk;
		return str_printf("%s%s", bk, &path[2]);
	}
	return str_dup(path, 0);
}

void mk_dir(const char* path, unsigned privilege){
	unsigned len = 0;
	unsigned next = 0;
	const char* d;
	__free char* mkpath = MANY(char, strlen(path) + 2);
	unsigned l = 0;
	mkpath[0] = 0;

	while( *(d=str_tok(path, "/", 0, &len, &next)) ){
		memcpy(&mkpath[l], d, len);
		l += len;
		mkpath[l++] = '/';
		mkpath[l] = 0;
		if( !dir_exists(mkpath) ) mkdir(mkpath, privilege);
	}
}

void rm(const char* path){
	DIR* d = opendir(path);
	if( !d ) return;

	struct dirent* ent;
	while( (ent=readdir(d)) ){
		if( !strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..") ) continue;
		char* fpath = str_printf("%s/%s", path, ent->d_name);
		if( ent->d_type == DT_DIR ){
			rm(fpath);
		}
		else{
			unlink(fpath);
		}
		mem_free(fpath);
	}
	closedir(d);
	rmdir(path);
}

void colorfg_set(unsigned color){
	if( !color ){
		fputs("\033[0m", stdout);
	}
	else{
		printf("\033[38;5;%um", color);
	}
}

void colorbg_set(unsigned color){
	if( !color ){
		fputs("\033[0m", stdout);
	}
	else{
		printf("\033[48;5;%um", color);
	}
}

void bold_set(void){
	fputs("\033[1m", stdout);
}

void term_wh(unsigned* w, unsigned* h){
	struct winsize ws;
    ioctl(0, TIOCGWINSZ, &ws);
	if( w ) *w = ws.ws_col;
	if( h ) *h = ws.ws_row;
}

void print_repeats(unsigned count, const char* ch){
	if( !count ) return;
	while( count --> 0 ) fputs(ch, stdout);
}

void print_repeat(unsigned count, const char ch){
	if( !count ) return;
	while( count --> 0 ) fputc(ch, stdout);
}

void shell(const char* errprompt, const char* exec){
	puts(exec);
	int ex = system(exec);
	if( ex == -1 ) die("%s: %m", errprompt);
	if( !WIFEXITED(ex) || WEXITSTATUS(ex) != 0) die("%s: %d %d", errprompt, WIFEXITED(ex), WEXITSTATUS(ex));
}

__private int isyesno(const char* in, int noyes){
	__private char* noyesmap[][5] = {
		{ "n", "no" , "N", "No" , "NO"  },
		{ "y", "yes", "Y", "Yes", "YES" }
	};
	in = str_skip_h(in);
	for( unsigned i = 0; i < sizeof_vector(noyesmap); ++i ){
		if( !strcmp(in, noyesmap[noyes][i]) ){
			in += strlen(noyesmap[noyes][i]);
			in = str_skip_h(in);
			if( *in && *in != '\n' ) return 0;
			return 1;
		}
	}
	return 0;
}

int readline_yesno(void){
	fflush(stdout);
	char* in;
	int ret = 1;
	while(1){
		in = readline("[Yes/no]: ");
		dbg_info("readline '%s'", in);
		if( !in ) continue;
		if( !*in ) break;
		if( isyesno(in, 1) ) break;
		if( isyesno(in, 0) ) { ret = 0; break; }
		free(in);
	}
	free(in);
	return ret;
}

__private int idnumber(const char** p){
	errno = 0;
	char* end = NULL;
	int ret = strtol(*p, &end, 10);
	if( errno || !end || end == *p ) return -1;
	*p = end;
	return ret;
}

__private int idname(char** list, const char* n, unsigned len){
	if( len == 1 && *n == '*' ) return INT_MIN;
	mforeach(list, i){
		unsigned ln = strlen(list[i]);
		if( ln == len && !strncmp(list[i], n, len) ) return i;
	}
	return -1;
}

__private char** idadd(char** sel, char* name){
	mforeach(sel, i){
		if( !strcmp(sel[i],name) ) return sel;
	}
	sel = mem_upsize(sel, 1);
	sel[mem_header(sel)->len++] = name;
	return sel;
}

__private char** idselection(const char* p, char** list){
	unsigned const count = mem_header(list)->len;
	char** sel = MANY(char*, count);
	if( !p ) return sel;
	p = str_skip_h(p);
	if( !*p ) return sel;
	do{
		const char* stp = p;
		int id = idnumber(&p);
		if( id == -1 ){
			const char* end = p;
			while( *end && *end != ' ' && *end != ',' && *end != '\t' ) ++end;
			id = idname(list, p, end-p);
			p = end;
		}
		if( id == INT_MIN ){
			for( unsigned i = 0; i < count; ++i ){
				sel[i] = list[i];
			}
			mem_header(sel)->len = count;
			break;
		}
		else if( id < 0 || id >= (int)count ){
			mem_free(sel);
			printf("invalid selection: %.*s\n", (int)(p-stp), stp);
			return NULL;
		}
		else{
			sel = idadd(sel, list[id]);
		}
		
		p = str_skip_h(p);
		if( *p && *p != ',' ){
			printf("invalid token '%c' aspected , at this point\n", *p );
			mem_free(sel);
			return NULL;
		}
		p = str_skip_h(p+1);
	}while( *p );
	return sel;
}

char** readline_listid(const char* prompt, char** list){
	unsigned w;
	term_wh(&w, NULL);
	unsigned cw = 0;
	mforeach(list, i){
		unsigned nw = snprintf(NULL, 0, "[%u]%s  ", i, list[i]);
		if( nw + cw > w ){
			cw = 0;
			putchar('\n');
		}
		printf("[%u]%s  ", i, list[i]);
		cw += nw;
	}
	putchar('\n');
	putchar('\n');
	char** sel = NULL;
	puts(prompt);
	do{
		puts("(Default nothing; 0,1,2,3; name,name,...; * all)");
		char* in = readline("> ");
		sel = idselection(in, list);
		free(in);
	}while(!sel);
	return sel;
}

__private __atomic volatile unsigned long PROG_I;
__private __atomic volatile unsigned long PROG_T;

void progress_begin(const char* prompt, unsigned long max){
	PROG_T = max;
	PROG_I = 0;
	fflush(stdout);
	dprintf(STDOUT_FILENO, "[%3u%%] %s", 0, prompt);
}

void progress(const char* prompt, unsigned long inc){
	PROG_I += inc;
	if( PROG_I > PROG_T ) PROG_I = PROG_T;
	dprintf(STDOUT_FILENO, "\r[%3lu%%] %s", PROG_I * 100 / PROG_T, prompt);
}

void progress_end(const char* prompt){
	dprintf(STDOUT_FILENO, "\r[%3u%%] %s\n", 100, prompt);
}







typedef struct highlight{
	unsigned fg;
	unsigned bg;
	unsigned bold;
	unsigned sep;
}highlight_s;

typedef struct revAstHighlight{
	const char* type;
	struct revAstHighlight* child;
	highlight_s* hi;
}revAstHighlight_s;
/*
__private highlight_s* hi_new(unsigned fg, unsigned bg, unsigned bold, unsigned sep){
	highlight_s* hi = NEW(highlight_s);
	hi->fg   = fg;
	hi->bg   = bg;
	hi->bold = bold;
	hi->sep  = sep;
	return hi;
}

__private revAstHighlight_s* rah_new(revAstHighlight_s* parent, const char* type, highlight_s* hi){
	mforeach(parent->child, i){
		if( !strcmp(parent->child[i].type, type) ){
			if( hi && !parent->child[i].hi ) parent->child[i].hi = mem_borrowed(hi);
			return &parent->child[i];
		}
	}
	parent->child = mem_upsize(parent->child, 1);
	revAstHighlight_s* rah = &parent->child[mem_header(parent->child)->len++];
	rah->hi    = mem_borrowed(hi);
	rah->type  = type;
	rah->child = MANY(revAstHighlight_s, 1);
	return rah;
}
*/
__private revAstHighlight_s* bash_highlight(void){
	__private highlight_s hcomment  = { 14, 0, 0, 0};
	__private highlight_s hfunction = {  7, 0, 0, 0};
	__private highlight_s hvardec   = { 14, 0, 1, 0};
	__private highlight_s hvar      = { 23, 0, 1, 0};
	__private highlight_s hcommand  = { 11, 0, 0, 0};
	__private highlight_s harg      = {255, 0, 0, 0};
	__private highlight_s hstring   = { 13, 0, 0, 0};
	__private highlight_s hassign   = {255, 0, 0, 0};
	
	__private revAstHighlight_s bashAny[] = {
		{ "function_definition" , NULL, &hfunction},
		{ "compound_statement"  , NULL, &hfunction},
		{ "simple_expansion"    , NULL, &hvar     },
		{ "expansion"           , NULL, &hvar     },
		{ "command_substitution", NULL, &hvar     },
		{ "pipeline"            , NULL, &hcommand },
		{ "if_statement"        , NULL, &hcommand },
		{ "for_statement"       , NULL, &hcommand },
		{ "while_statement"     , NULL, &hcommand },
		{ "do_group"            , NULL, &hcommand },
		{ "test_command"        , NULL, &hcommand },
		{ "binary_expression"   , NULL, &hcommand },
		{ "file_redirect"       , NULL, &hcommand },
		{ "declaration_command" , NULL, &hcommand },
		{ "arithmetic_expansion", NULL, &hassign  },
		{ "variable_assignment" , NULL, &hassign  },
		{ "string"              , NULL, &hstring  },
		{ NULL, NULL, NULL }
	};
	
	__private revAstHighlight_s varDec[] = {
		{ "variable_assignment", NULL, &hvardec },
		{ "for_statement"      , NULL, &hvardec },
		{ "binary_expression"  , NULL, &hvardec },
		{ "simple_expansion"   , NULL, &hvar    },
		{ "expansion"          , NULL, &hvar    },
		{ NULL, NULL, NULL }
	};
	
	__private revAstHighlight_s word[] = {
		{ "command_name"        , NULL, &hcommand },
		{ "declaration_command" , NULL, &hcommand },
		{ "function_definition" , NULL, &hfunction},
		{ "command"             , NULL, &harg     },
		{ "file_redirect"       , NULL, &hassign  },
		{ NULL, NULL, NULL }
	};
	
	__private revAstHighlight_s bash[] = {
		{ "word"         , word   , NULL      },
		{ "comment"      , NULL   , &hcomment },
		{ "variable_name", varDec , NULL      },
		{ "`"            , NULL   , &hassign  },
		{ ";"            , NULL   , &hassign  },
		{ "raw_string"   , NULL   , &hstring  },
		{ "number"       , NULL   , &hstring  },
		{ NULL           , bashAny, NULL      }
	};
	
	return bash;
}

__private revAstHighlight_s* rah_exists(revAstHighlight_s* rah, const char* type){
	unsigned i;
	for( i = 0; rah[i].type; ++i ){
		if( !strcmp(rah[i].type, type) ) return &rah[i];
	}
	return &rah[i];
}

__private highlight_s* rah_find(revAstHighlight_s* rah, char** stack){
	unsigned count = mem_header(stack)->len;
	iassert(count);
	do{
		rah=rah_exists(rah, stack[--count]);
		if( rah->hi ) return rah->hi;
		rah = rah->child;
	}while( rah );
	return NULL;
}

__private void dbg_print_stack(char** stack){
	unsigned count = mem_header(stack)->len;
	printf("<[@");
	while( count --> 0 ){
		printf("%s.", stack[count]);
	}
	printf("@]>");
}

__private void bash_tokenize_source(const TSNode node, const char *source, char*** stack, revAstHighlight_s* rah) {
	const char *type = ts_node_type(node);
	*stack = mem_upsize(*stack,1);
	(*stack)[mem_header(*stack)->len++] = (char*)type;

	unsigned child_count = ts_node_child_count(node);
	if( child_count ){
		for( unsigned i = 0; i < child_count; ++i ){
			TSNode child = ts_node_child(node, i);
			bash_tokenize_source(child, source, stack, rah);
		}
	}
	else{
		unsigned st = ts_node_start_byte(node);
		unsigned en = ts_node_end_byte(node);
		const char* sepst = &source[en];
		const char* sepen = sepst;
		while( *sepen && (*sepen == ' ' || *sepen == '\t' || *sepen == '\n') ) ++sepen;
		highlight_s* hi = rah_find(rah, *stack);
		if( !hi ){
			dbg_print_stack(*stack);
			printf("%.*s", en-st, &source[st]);
			if( sepen - sepst ) printf("%.*s", (int)(sepen-sepst), sepst);
			puts("");
			die("internal error, missing highlight");
		}
		else{
			colorfg_set(hi->fg);
			if( hi->bg ) colorbg_set(hi->bg);
			if( hi->bold ) bold_set();
			printf("%.*s", en-st, &source[st]);
			if( !hi->sep ) colorfg_set(0);
			if( sepen - sepst ) printf("%.*s", (int)(sepen-sepst), sepst);
			if( hi->sep ) colorfg_set(0);
		}
	}
	--mem_header(*stack)->len;
}

void print_highlight(const char* source, const char* lang) {
	TSParser *parser = ts_parser_new();
	revAstHighlight_s* rah = NULL;

	if( !strcmp(lang, "bash") ){
		ts_parser_set_language(parser, tree_sitter_bash());
		rah = bash_highlight();
	}
	else{
		die("unsupported %s", lang);
	}
	TSTree *tree = ts_parser_parse_string(parser, NULL, source, strlen(source));
	if (!tree) {
		dbg_error("on parsing code.");
		puts(source);
		ts_parser_delete(parser);
		return;
	}
	TSNode root_node = ts_tree_root_node(tree);
	dbg_info("Root node type: %s\n", ts_node_type(root_node));
	__free char** stack = MANY(char*, 10);
	bash_tokenize_source(root_node, source, &stack, rah);
	mem_free(stack);
	fflush(stdout);
	ts_tree_delete(tree);
	ts_parser_delete(parser);
}

