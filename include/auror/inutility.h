#ifndef __INUTILITY_H__
#define __INUTILITY_H__

char* load_file(const char* fname, int exists);
int dir_exists(const char* path);
int vercmp(const char *a, const char *b);
char* path_home(char* path);
char* path_explode(const char* path);
void mk_dir(const char* path, unsigned privilege);
void rm(const char* path);
void colorfg_set(unsigned color);
void colorbg_set(unsigned color);
void bold_set(void);
void term_wh(unsigned* w, unsigned* h);
void print_repeats(unsigned count, const char* ch);
void print_repeat(unsigned count, const char ch);
void shell(const char* errprompt, const char* exec);
int readline_yesno(void);
char** readline_listid(const char* prompt, char** list);
void progress_begin(const char* prompt, unsigned long max);
void progress(const char* prompt, unsigned long inc);
void progress_end(const char* prompt);
void print_highlight(const char* source, const char* lang);

#endif
