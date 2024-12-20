#ifndef __INUTILITY_H__
#define __INUTILITY_H__

char* load_file(const char* fname, int exists);
int vercmp(const char *a, const char *b);
char* path_home(char* path);
char* path_explode(const char* path);
void colorfg_set(unsigned color);
void colorbg_set(unsigned color);
void bold_set(void);
void print_repeats(unsigned count, const char* ch);
void print_repeat(unsigned count, const char ch);

#endif
