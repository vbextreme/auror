#ifndef __INUTILITY_H__
#define __INUTILITY_H__

#include <notstd/core.h>
#include <notstd/delay.h>

#define CHAR_UPPER "▀"
#define CHAR_LOWER "▄"

char* load_file(const char* fname, int exists);
delay_t file_time_sec_get(const char* path);
void file_time_sec_set(const char* path, delay_t sec);
int dir_exists(const char* path);
int vercmp(const char *a, const char *b);
char* path_cats(char* dst, char* src, unsigned len);
char* path_cat(char* dst, char* src);
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
void term_line_cls(void);
void term_scroll_region(unsigned y1, unsigned y2);
unsigned term_scroll_begin(unsigned bottomLine);
void term_scroll_end(void);
void term_cursor_store(void);
void term_cursor_load(void);
void term_gotoxy(unsigned x, unsigned y);
void term_cursor_show(int show);
void term_cursor_up(unsigned n);
void term_cursor_down(unsigned n);
void term_cursor_home(void);
void term_cursorxy(unsigned* c, unsigned* r);
void term_vbar(double v);
void term_hhbar(unsigned w, double v);
void term_bar_double(unsigned w, unsigned c1, unsigned c2, double v1, double v2);

void term_reserve_enable(void);
int term_reserve_line(int *line);
void term_release_line(int* line);
void term_lock(void);
void term_unlock(void);

int readline_yesno(void);
unsigned* readline_listid(const char* prompt, char** list, unsigned* fgcol);
void print_highlight(const char* source, const char* lang);

#endif
