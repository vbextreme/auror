#undef DBG_ENABLE

#include <notstd/core.h>
#include <notstd/str.h>
#include <notstd/threads.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <pwd.h>
#include <limits.h>
#include <dirent.h>
#include <termios.h>

#include <readline/readline.h>

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-bash.h>

#include <auror/inutility.h>

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

delay_t file_time_sec_get(const char* path){
	struct stat info;
	if( stat(path, &info) ){
		dbg_error("stat fail: %m");
		return 0;
	}
	return info.st_mtim.tv_sec;
}

void file_time_sec_set(const char* path, delay_t sec){
	struct stat info;
	struct timespec ts[2];
	if( stat(path, &info) ){
		dbg_error("stat fail: %m");
		return;
	}
	ts[0] = info.st_atim;
	ts[1].tv_sec = sec;
	ts[1].tv_nsec = 0;
	if( utimensat(AT_FDCWD, path, ts, 0) ) {
		dbg_error("fail to set modified time: %m");
	}
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

char* path_cats(char* dst, char* src, unsigned len){
	if( !src || !dst ) return dst;
	if( !len ) len = strlen(src);
	if( !len ) return dst;
	unsigned dl = mem_header(dst)->len;
	dst = mem_upsize(dst, len + 2);
	if( dl && dst[dl-1] == '/' ){
		if( src[0] == '/' ){
			memcpy(&dst[dl-1], src, len);
			dl += len - 1;
		}
		else{
			memcpy(&dst[dl], src, len);
			dl += len;
		}
	}
	else{
		if( src[0] == '/' ){
			memcpy(&dst[dl], src, len);
			dl += len;
		}
		else{
			dst[dl] = '/';
			memcpy(&dst[dl+1], src, len);
			dl += len + 1;
		}
	}
	dst[dl] = 0;
	mem_header(dst)->len = dl;
	return dst;
}

char* path_cat(char* dst, char* src){
	return path_cats(dst, src, mem_header(src)->len);
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
		return str_printf("%.*s%s", (int)(bk-cwd), cwd, &path[2]);
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

void term_line_cls(void){
	fputs("\033[2K", stdout);
}

void term_scroll_region(unsigned y1, unsigned y2){
	printf("\033[%u;%ur", y1, y2);
}

unsigned term_scroll_begin(unsigned bottomLine){
	unsigned w, h;
	term_wh(&w, &h);
	unsigned x, y;
	term_cursorxy(&x, &y);
	if( y >= h - bottomLine ){
		print_repeat(bottomLine, '\n');
		y -= bottomLine;
		term_gotoxy(0, y);
		fflush(stdout);
	}
	term_scroll_region(1, h-bottomLine);
	term_gotoxy(0,y);
	fflush(stdout);
	return h-bottomLine;
}

void term_scroll_end(void){
	unsigned w, h;
	term_wh(&w, &h);
	term_cursor_store();
	fflush(stdout);
	term_scroll_region(1, h);
	fflush(stdout);
	term_cursor_load();
	fflush(stdout);
}

void term_cursor_store(void){
	fputs("\033[s", stdout);
}

void term_cursor_load(void){
	fputs("\033[u", stdout);
}

void term_gotoxy(unsigned x, unsigned y){
	printf("\033[%u;%uH", y+1, x+1);
}

void term_cursor_show(int show){
	printf("\033[?25%c", show ? 'h' : 'l');
}

void term_cursor_up(unsigned n){
	printf("\033[%uA", n);
}

void term_cursor_down(unsigned n){
	printf("\033[%uB", n);
}

void term_cursor_home(void){
	fputs("\033[G", stdout);
}

void term_cursorxy(unsigned* c, unsigned* r){
    const char *dev;
	struct termios oldtio;
	struct termios newtio;
    int fd = -1;

	dev = ttyname(STDIN_FILENO);
    if( !dev ) dev = ttyname(STDOUT_FILENO);
	if( !dev ) return;

    while( (fd = open(dev, O_RDWR | O_NOCTTY)) == -1 && errno == EINTR);
    if( fd == -1) return;

    if( tcgetattr(fd, &oldtio) ){
		dbg_error("tcgetattr: %m");
		close(fd);
		return;
	}

	newtio = oldtio;
	newtio.c_lflag &= ~ICANON;
    newtio.c_lflag &= ~ECHO;
    newtio.c_cflag &= ~CREAD;
	if( tcsetattr(fd, TCSANOW, &newtio) ){
		dbg_error("tcsetattr: %m");
		close(fd);
		return;
	}
	
	write(fd, "\033[6n",4);
	char ch;
	if( read(fd, &ch, 1) != 1 || ch != 0x1B ){
		dbg_error("aspectd 0x1B, got 0x%X '%c'", ch, ch);	
		goto END;
	}
	if( read(fd, &ch, 1) != 1 || ch != '[' ){
		dbg_error("aspectd [");	
		goto END;
	}
	*r = 0;
	while( read(fd, &ch, 1) == 1 && ch >= '0' && ch <= '9' ){
		*r = 10 * *r + (ch-'0');
	}
	if( ch != ';' ){
		dbg_error("aspectd ;");
		goto END;
	}

	*c = 0;
	while( read(fd, &ch, 1) == 1 && ch >= '0' && ch <= '9' ){
		*c = 10 * *c + (ch-'0');
	}
	
	if( ch != 'R' ){
		dbg_error("aspectd R");
		goto END;
	}

	--(*r);
	--(*c);
END:
	tcsetattr(fd, TCSANOW, &oldtio);
	close(fd);
}

__private char* CHAR_VBAR[] = { " ", "▂","▃","▄","▅","▆","▇","█" };
void term_vbar(double v){
	unsigned const h = sizeof_vector(CHAR_VBAR);
	unsigned q = (v*h)/100.0;
	if( q >= h ) q = h-1;
	fputs(CHAR_VBAR[q], stdout);
}

__private char* CHAR_HBAR[] = { " ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█" };
void term_hhbar(unsigned w, double v){
	unsigned const tb = sizeof_vector(CHAR_HBAR);
	unsigned e = (v * (w*(tb-1)) / 100.0);
	unsigned f = e / (tb-1);
	if( f > w ) f = w;
	print_repeats(f, CHAR_HBAR[tb-1]);
	w -= f;
	f = e % (tb-1);
	fputs(CHAR_HBAR[f], stdout);
	if( w ) --w;
	print_repeat(w, ' ');
}

void term_bar_double(unsigned w, unsigned c1, unsigned c2, double v1, double v2){
	unsigned w1 = (v1 * w) / 100.0;
	unsigned w2 = (v2 * w) / 100.0;
	unsigned eq = 0;
	if( w1 > w ) w1 = w;
	if( w2 > w ) w2 = w;

	if( w1 > w2 ){
		eq = w2;
	}
	else{
		eq = w1;
	}
	w2 -= eq;
	w1 -= eq;
	if( eq ){
		colorfg_set(c2);
		colorbg_set(c1);
		print_repeats(eq, CHAR_LOWER);
		colorfg_set(0);
		w -= eq;
	}
	if( w1 ){
		colorfg_set(c1);
		print_repeats(w1, CHAR_UPPER);
		colorfg_set(0);
		w -= w1;
	}
	else if( w2 ){
		colorfg_set(c2);
		print_repeats(w2, CHAR_LOWER);
		colorfg_set(0);
		w -= w2;
	}
	if( w ){
		print_repeat(w, ' ');
	}
}

__private mutex_t LLOCK;
__private int**   RLINE;

void term_reserve_enable(void){
	mutex_ctor(&LLOCK);
	RLINE = MANY(int*, 16);
}

int term_reserve_line(int *line){
	int ret = 0;
	mlock(&LLOCK){
		unsigned x, y;
		term_cursorxy(&x, &y);
		*line = y;
		putchar('\n');
		term_cursorxy(&x, &y);
		if( (int)y == *line ){
			ret = 1;
			mforeach(RLINE, i){
				--(*RLINE[i]);
			}
		}
		unsigned il = mem_ipush(&RLINE);
		RLINE[il] = line;
	}
	return ret;
}

void term_release_line(int* line){
	mlock(&LLOCK){
		mforeach(RLINE, i){
			if( RLINE[i] == line ){
				RLINE = mem_delete(RLINE, i, 1);
				break;
			}
		}
	}
}

void term_lock(void){
	mutex_lock(&LLOCK);
}

void term_unlock(void){
	mutex_unlock(&LLOCK);
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

__private unsigned* idadd(unsigned* sel, unsigned id){
	mforeach(sel, i){
		if( sel[i] == id ) return sel;
	}
	sel = mem_upsize(sel, 1);
	sel[mem_header(sel)->len++] = id;
	return sel;
}

__private unsigned* idselection(const char* p, char** list){
	unsigned const count = mem_header(list)->len;
	unsigned* sel = MANY(unsigned, count);
	if( !p ) return sel;
	p = str_skip_h(p);
	if( !*p ) return sel;
	dbg_info("");
	do{
		dbg_info("");
		const char* stp = p;
		int id = idnumber(&p);
		dbg_info("id: %d line:%.*s", id, (int)(p-stp), stp);
		if( id == -1 ){
			const char* end = p;
			while( *end && *end != ' ' && *end != ',' && *end != '\t' ) ++end;
			id = idname(list, p, end-p);
			p = end;
		}
		if( id == INT_MIN ){
			for( unsigned i = 0; i < count; ++i ){
				sel[i] = i;
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
			sel = idadd(sel, id);
		}
		
		p = str_skip_h(p);
		if( *p && *p != ',' ){
			printf("invalid token '%c' aspected , at this point\n", *p );
			mem_free(sel);
			return NULL;
		}
		if( *p == ',' ) ++p;
		dbg_info("line:'%s'", p);
		p = str_skip_h(p);
	}while( *p );
	return sel;
}

unsigned* readline_listid(const char* prompt, char** list, unsigned* fgcol){
	unsigned w;
	term_wh(&w, NULL);
	unsigned cw = 0;
	unsigned ncol = fgcol ? mem_header(fgcol)->len : 0;
	mforeach(list, i){
		unsigned nw = snprintf(NULL, 0, "[%u]%s  ", i, list[i]);
		if( nw + cw > w ){
			cw = 0;
			putchar('\n');
		}
		if( i < ncol ) colorfg_set(fgcol[i]);
		printf("[%u]%s  ", i, list[i]);
		if( i < ncol ) colorfg_set(0);
		cw += nw;
	}
	putchar('\n');
	putchar('\n');
	unsigned* sel = NULL;
	puts(prompt);
	do{
		puts("(Default nothing; 0,1,2,3; name,name,...; * all)");
		char* in = readline("> ");
		dbg_info("in:'%s'", in);
		sel = idselection(in, list);
		free(in);
	}while(!sel);
	return sel;
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

__private revAstHighlight_s* bash_highlight(void){
	__private highlight_s hcomment  = { 14, 0, 0, 0};
	__private highlight_s hfunction = {  7, 0, 0, 0};
	__private highlight_s hvardec   = { 14, 0, 1, 0};
	__private highlight_s hvar      = { 23, 0, 1, 0};
	__private highlight_s hcommand  = { 11, 0, 0, 0};
	__private highlight_s harg      = { 15, 0, 0, 0};
	__private highlight_s hstring   = { 13, 0, 0, 0};
	__private highlight_s hassign   = { 15, 0, 0, 0};
	
	__private revAstHighlight_s array[] = {
		{ "variable_assignment", NULL, &harg },
		{ NULL, NULL, NULL }
	};

	__private revAstHighlight_s list[] = {
		{ "compound_statement", NULL, &hcommand },
		{ NULL, NULL, NULL }
	};

	__private revAstHighlight_s bashAny[] = {
		{ "function_definition" , NULL, &hfunction},
		{ "compound_statement"  , NULL, &hfunction},
		{ "simple_expansion"    , NULL, &hvar     },
		{ "expansion"           , NULL, &hvar     },
		{ "command_substitution", NULL, &hvar     },
		{ "array"               , NULL, &hvar     },
		{ "subscript"           , NULL, &hvar     },
		{ "subshell"            , NULL, &hvar     },
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
		{ "list"                , list, NULL      },
		{ NULL, NULL, NULL }
	};
	
	__private revAstHighlight_s varDec[] = {
		{ "variable_assignment", NULL, &hvardec },
		{ "for_statement"      , NULL, &hvardec },
		{ "binary_expression"  , NULL, &hvardec },
		{ "declaration_command", NULL, &harg    },
		{ "simple_expansion"   , NULL, &hvar    },
		{ "expansion"          , NULL, &hvar    },
		{ "subscript"          , NULL, &hvar    },
		{ NULL, NULL, NULL }
	};
	
	__private revAstHighlight_s word[] = {
		{ "command_name"        , NULL , &hcommand },
		{ "declaration_command" , NULL , &hcommand },
		{ "concatenation"       , NULL , &hcommand },
		{ "subscript"           , NULL , &hcommand },
		{ "function_definition" , NULL , &hfunction},
		{ "command"             , NULL , &harg     },
		{ "file_redirect"       , NULL , &hassign  },
		{ "variable_assignment" , NULL , &hcommand },
		{ "array"               , array, NULL      },
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

__private void highlight_source(const TSNode node, const char *source, char*** stack, revAstHighlight_s* rah) {
	const char *type = ts_node_type(node);
	*stack = mem_upsize(*stack,1);
	(*stack)[mem_header(*stack)->len++] = (char*)type;

	unsigned child_count = ts_node_child_count(node);
	if( child_count ){
		for( unsigned i = 0; i < child_count; ++i ){
			TSNode child = ts_node_child(node, i);
			highlight_source(child, source, stack, rah);
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
	highlight_source(root_node, source, &stack, rah);
	mem_free(stack);
	fflush(stdout);
	ts_tree_delete(tree);
	ts_parser_delete(parser);
}

