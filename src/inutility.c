#include <notstd/core.h>
#include <notstd/str.h>

#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <pwd.h>
#include <limits.h>

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

void print_repeats(unsigned count, const char* ch){
	if( !count ) return;
	while( count --> 0 ) fputs(ch, stdout);
}

void print_repeat(unsigned count, const char ch){
	if( !count ) return;
	while( count --> 0 ) fputc(ch, stdout);
}

